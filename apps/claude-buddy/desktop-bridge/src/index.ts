#!/usr/bin/env node
import { createServer, type IncomingMessage, type ServerResponse } from "node:http";
import { randomBytes } from "node:crypto";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { homedir } from "node:os";
import { WebSocketServer, type WebSocket } from "ws";
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import * as z from "zod/v4";

const DEFAULT_PORT = 17877;
const PROTOCOL_VERSION = 1;
const MAX_TEXT = 480;

type Json = null | boolean | number | string | Json[] | { [key: string]: Json };

interface BridgeConfig {
  port: number;
  host: string;
  token: string;
  configPath: string;
}

interface PromptDraft {
  id: string;
  text: string;
  source: "keyboard" | "voice";
  createdAt: string;
}

interface PendingQuestion {
  id: string;
  resolve: (answer: string | null) => void;
  timer: NodeJS.Timeout;
}

interface DeviceMessage {
  v?: number;
  type?: string;
  id?: string;
  [key: string]: unknown;
}

function parsePort(value: string | undefined): number {
  const parsed = Number(value);
  if (Number.isInteger(parsed) && parsed >= 1024 && parsed <= 65535) return parsed;
  return DEFAULT_PORT;
}

function configPath(): string {
  return process.env.CARDPUTER_BRIDGE_CONFIG ||
    join(homedir(), ".claude-cardputer-bridge", "config.json");
}

function loadConfig(): BridgeConfig {
  const path = configPath();
  const port = parsePort(process.env.CARDPUTER_BRIDGE_PORT);
  const host = process.env.CARDPUTER_BRIDGE_BIND_HOST?.trim() || "127.0.0.1";
  const envToken = process.env.CARDPUTER_PAIRING_TOKEN?.trim();
  if (envToken) return { port, host, token: envToken, configPath: path };

  if (existsSync(path)) {
    try {
      const raw = JSON.parse(readFileSync(path, "utf8")) as Partial<BridgeConfig>;
      if (typeof raw.token === "string" && raw.token.length >= 24) {
        return {
          port: typeof raw.port === "number" ? raw.port : port,
          host,
          token: raw.token,
          configPath: path
        };
      }
    } catch {
      // Fall through and rewrite a valid config.
    }
  }

  const token = randomBytes(24).toString("base64url");
  mkdirSync(join(path, ".."), { recursive: true });
  writeFileSync(path, `${JSON.stringify({ port, token }, null, 2)}\n`, { mode: 0o600 });
  return { port, host, token, configPath: path };
}

function text(value: unknown, fallback = ""): string {
  if (typeof value === "string") return value.slice(0, MAX_TEXT);
  if (typeof value === "number" || typeof value === "boolean") return String(value);
  return fallback;
}

function jsonResponse(res: ServerResponse, status: number, body: Json): void {
  const payload = `${JSON.stringify(body)}\n`;
  res.writeHead(status, {
    "content-type": "application/json; charset=utf-8",
    "content-length": Buffer.byteLength(payload)
  });
  res.end(payload);
}

async function readJson(req: IncomingMessage): Promise<Record<string, unknown>> {
  const chunks: Buffer[] = [];
  for await (const chunk of req) chunks.push(Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk));
  if (chunks.length === 0) return {};
  return JSON.parse(Buffer.concat(chunks).toString("utf8")) as Record<string, unknown>;
}

class CardputerBridge {
  private device: WebSocket | null = null;
  private deviceInfo: Record<string, unknown> = {};
  private deviceState: Record<string, unknown> = {};
  private promptQueue: PromptDraft[] = [];
  private pending = new Map<string, PendingQuestion>();
  private lastSummary = "";
  private httpServer = createServer((req, res) => void this.handleHttp(req, res));
  private wsServer = new WebSocketServer({ noServer: true });

  constructor(private readonly config: BridgeConfig) {
    this.httpServer.on("upgrade", (req, socket, head) => {
      const url = new URL(req.url || "/", `http://${req.headers.host || "127.0.0.1"}`);
      if (url.pathname !== "/device" || url.searchParams.get("token") !== this.config.token) {
        socket.write("HTTP/1.1 401 Unauthorized\r\n\r\n");
        socket.destroy();
        return;
      }
      this.wsServer.handleUpgrade(req, socket, head, ws => this.attachDevice(ws));
    });
  }

  async start(): Promise<void> {
    await new Promise<void>((resolveStart, rejectStart) => {
      this.httpServer.once("error", rejectStart);
      this.httpServer.listen(this.config.port, this.config.host, () => {
        this.httpServer.off("error", rejectStart);
        resolveStart();
      });
    });
    console.error(`Cardputer bridge listening on ${this.config.host}:${this.config.port}`);
  }

  status(): Record<string, unknown> {
    const hasDevice = Boolean(this.device && this.device.readyState === this.device.OPEN);
    return {
      ok: true,
      port: this.config.port,
      bindHost: this.config.host,
      hasDevice,
      queuedPrompts: this.promptQueue.length,
      pendingQuestions: this.pending.size,
      deviceInfo: this.deviceInfo,
      deviceState: this.deviceState,
      lastSummary: this.lastSummary
    };
  }

  notify(title: string, message: string, level = "info"): boolean {
    this.lastSummary = message.slice(0, MAX_TEXT);
    return this.send({
      v: PROTOCOL_VERSION,
      type: "display.summary",
      title: title.slice(0, 48),
      text: this.lastSummary,
      level: level.slice(0, 16)
    });
  }

  nextPrompt(consume: boolean): PromptDraft | null {
    if (this.promptQueue.length === 0) return null;
    return consume ? this.promptQueue.shift() || null : this.promptQueue[0] || null;
  }

  async ask(question: string, options: string[], timeoutSeconds: number): Promise<string | null> {
    if (!this.device || this.device.readyState !== this.device.OPEN) return null;

    const id = `q_${Date.now().toString(36)}_${randomBytes(3).toString("hex")}`;
    const timeoutMs = Math.max(5, Math.min(timeoutSeconds, 300)) * 1000;
    const result = new Promise<string | null>(resolveAnswer => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        resolveAnswer(null);
      }, timeoutMs);
      this.pending.set(id, { id, resolve: resolveAnswer, timer });
    });

    this.send({
      v: PROTOCOL_VERSION,
      type: "question.request",
      id,
      question: question.slice(0, MAX_TEXT),
      options: options.slice(0, 5).map(opt => opt.slice(0, 64))
    });

    return result;
  }

  pairingConfig(endpoint?: string): Record<string, unknown> {
    const defaultHost = this.config.host === "0.0.0.0" ? "<mac-lan-ip>" : this.config.host;
    return {
      v: PROTOCOL_VERSION,
      endpoint: endpoint || `ws://${defaultHost}:${this.config.port}/device`,
      token: this.config.token,
      note: "Transfer this only to your own Cardputer ADV. Do not commit the token."
    };
  }

  writePairingConfig(dir: string, endpoint?: string): void {
    const target = resolve(dir);
    mkdirSync(target, { recursive: true });
    writeFileSync(join(target, "manifest.json"), `${JSON.stringify({
      name: "bridge-config",
      type: "claude-cardputer-bridge",
      version: PROTOCOL_VERSION
    }, null, 2)}\n`);
    writeFileSync(join(target, "bridge.json"), `${JSON.stringify(this.pairingConfig(endpoint), null, 2)}\n`, {
      mode: 0o600
    });
  }

  private attachDevice(ws: WebSocket): void {
    if (this.device && this.device.readyState === this.device.OPEN) {
      this.device.close(1000, "replaced");
    }
    this.device = ws;
    this.deviceInfo = {};
    this.deviceState = {};
    ws.on("message", data => this.handleDeviceMessage(String(data)));
    ws.on("close", () => {
      if (this.device === ws) this.device = null;
    });
    this.send({ v: PROTOCOL_VERSION, type: "bridge.status", connected: true });
  }

  private handleDeviceMessage(raw: string): void {
    let msg: DeviceMessage;
    try {
      msg = JSON.parse(raw) as DeviceMessage;
    } catch {
      return;
    }
    if (msg.v !== PROTOCOL_VERSION || typeof msg.type !== "string") return;

    if (msg.type === "hello") {
      this.deviceInfo = { ...msg };
      this.send({ v: PROTOCOL_VERSION, type: "bridge.status", connected: true });
      return;
    }

    if (msg.type === "state") {
      this.deviceState = { ...msg };
      return;
    }

    if (msg.type === "prompt.draft" || msg.type === "voice.text") {
      const draft: PromptDraft = {
        id: text(msg.id, `draft_${Date.now().toString(36)}`),
        text: text(msg.text),
        source: msg.type === "voice.text" ? "voice" : "keyboard",
        createdAt: new Date().toISOString()
      };
      if (draft.text) {
        this.promptQueue.push(draft);
        this.promptQueue = this.promptQueue.slice(-10);
      }
      this.send({ v: PROTOCOL_VERSION, type: "prompt.result", id: draft.id, status: "queued" });
      return;
    }

    if (msg.type === "question.answer" && typeof msg.id === "string") {
      const pending = this.pending.get(msg.id);
      if (!pending) return;
      clearTimeout(pending.timer);
      this.pending.delete(msg.id);
      pending.resolve(text(msg.answer, text(msg.text)) || null);
    }
  }

  private send(payload: Record<string, unknown>): boolean {
    if (!this.device || this.device.readyState !== this.device.OPEN) return false;
    this.device.send(JSON.stringify(payload));
    return true;
  }

  private async handleHttp(req: IncomingMessage, res: ServerResponse): Promise<void> {
    const url = new URL(req.url || "/", `http://${req.headers.host || "127.0.0.1"}`);
    try {
      if (req.method === "GET" && url.pathname === "/health") {
        jsonResponse(res, 200, this.status() as Json);
        return;
      }
      if (req.method === "POST" && url.pathname === "/hook") {
        const body = await readJson(req);
        const response = await this.handleHook(body);
        jsonResponse(res, 200, response as Json);
        return;
      }
      jsonResponse(res, 404, { ok: false, error: "not found" });
    } catch (error) {
      jsonResponse(res, 500, { ok: false, error: error instanceof Error ? error.message : String(error) });
    }
  }

  private async handleHook(body: Record<string, unknown>): Promise<Record<string, unknown>> {
    const eventName = text(body.eventName, text(body.hook_event_name, "hook"));
    const event = typeof body.event === "object" && body.event ? body.event as Record<string, unknown> : body;
    const toolName = text(event.tool_name, text(event.tool, ""));
    const toolInput = typeof event.tool_input === "object" && event.tool_input
      ? event.tool_input as Record<string, unknown>
      : typeof event.input === "object" && event.input
        ? event.input as Record<string, unknown>
        : {};

    if (eventName === "PreToolUse" && toolName === "AskUserQuestion") {
      const questions = Array.isArray(toolInput.questions) ? toolInput.questions : [];
      const firstQuestion = typeof questions[0] === "object" && questions[0]
        ? questions[0] as Record<string, unknown>
        : {};
      const question = text(firstQuestion.question, text(toolInput.question, "Claude has a question"));
      const rawOptions = Array.isArray(firstQuestion.options)
        ? firstQuestion.options
        : Array.isArray(toolInput.options)
          ? toolInput.options
          : [];
      const options = rawOptions
        .map(opt => typeof opt === "string" ? opt : text((opt as Record<string, unknown>).label))
        .filter(Boolean);
      const answer = await this.ask(question, options, 120);
      if (!answer) return {};
      return {
        hookSpecificOutput: {
          hookEventName: "PreToolUse",
          permissionDecision: "allow",
          updatedInput: { ...toolInput, answers: [answer] }
        }
      };
    }

    if (eventName === "Elicitation") {
      const prompt = text(event.message, text(event.prompt, "Claude needs input"));
      const answer = await this.ask(prompt, [], 120);
      if (!answer) return {};
      return {
        hookSpecificOutput: {
          hookEventName: "Elicitation",
          action: "accept",
          content: { answer }
        }
      };
    }

    if (eventName === "PermissionRequest" || eventName === "Notification") {
      this.notify(eventName, text(event.message, toolName || "Claude needs attention"), "attention");
    }

    if (eventName === "PostCompact" || eventName === "Stop") {
      this.notify("Claude", text(event.summary, text(event.message, eventName)), "info");
    }

    return {};
  }
}

function registerTools(server: McpServer, bridge: CardputerBridge): void {
  server.registerTool("notify_cardputer", {
    description: "Send a concise status or summary to the connected Cardputer.",
    inputSchema: {
      title: z.string().max(48).default("Claude"),
      message: z.string().max(MAX_TEXT),
      level: z.enum(["info", "attention", "success", "warning", "error"]).default("info")
    }
  }, async ({ title, message, level }) => {
    const delivered = bridge.notify(title, message, level);
    return {
      content: [{ type: "text", text: delivered ? "Sent to Cardputer." : "No Cardputer connected." }],
      structuredContent: { delivered }
    };
  });

  server.registerTool("ask_cardputer", {
    description: "Ask the Cardputer user a short question and wait for an answer.",
    inputSchema: {
      question: z.string().max(MAX_TEXT),
      options: z.array(z.string().max(64)).max(5).default([]),
      timeoutSeconds: z.number().int().min(5).max(300).default(120)
    }
  }, async ({ question, options, timeoutSeconds }) => {
    const answer = await bridge.ask(question, options, timeoutSeconds);
    return {
      content: [{ type: "text", text: answer ? `Answer: ${answer}` : "No answer received." }],
      structuredContent: { answer }
    };
  });

  server.registerTool("get_cardputer_prompt", {
    description: "Read the next prompt draft queued from the Cardputer.",
    inputSchema: {
      consume: z.boolean().default(true)
    }
  }, async ({ consume }) => {
    const draft = bridge.nextPrompt(consume);
    return {
      content: [{ type: "text", text: draft ? draft.text : "No prompt draft queued." }],
      structuredContent: { draft }
    };
  });

  server.registerTool("device_status", {
    description: "Report bridge and Cardputer connection status.",
    inputSchema: {}
  }, async () => {
    const status = bridge.status();
    return {
      content: [{ type: "text", text: JSON.stringify(status, null, 2) }],
      structuredContent: status
    };
  });

  server.registerTool("generate_pairing_config", {
    description: "Return the bridge pairing config JSON for transferring to the Cardputer.",
    inputSchema: {
      endpoint: z.string().optional()
    }
  }, async ({ endpoint }) => {
    const config = bridge.pairingConfig(endpoint);
    return {
      content: [{ type: "text", text: JSON.stringify(config, null, 2) }],
      structuredContent: config
    };
  });
}

async function main(): Promise<void> {
  const config = loadConfig();
  const bridge = new CardputerBridge(config);

  const writeIndex = process.argv.indexOf("--write-pairing-config");
  if (writeIndex !== -1) {
    const dir = process.argv[writeIndex + 1] || "bridge-config";
    bridge.writePairingConfig(dir, process.env.CARDPUTER_BRIDGE_ENDPOINT);
    console.error(`Wrote Cardputer bridge config to ${dir}`);
    return;
  }

  if (process.argv.includes("--print-config")) {
    console.log(JSON.stringify({
      port: config.port,
      bindHost: config.host,
      configPath: config.configPath,
      tokenConfigured: Boolean(config.token)
    }, null, 2));
    return;
  }

  await bridge.start();

  const mcpServer = new McpServer({ name: "claude-cardputer-bridge", version: "0.1.0" });
  registerTools(mcpServer, bridge);
  const transport = new StdioServerTransport();
  await mcpServer.connect(transport);
}

main().catch(error => {
  console.error("Cardputer bridge fatal error:", error);
  process.exit(1);
});
