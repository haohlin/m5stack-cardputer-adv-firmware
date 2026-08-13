#!/usr/bin/env node
import { timingSafeEqual, randomBytes } from "node:crypto";
import { createServer, type IncomingMessage, type ServerResponse } from "node:http";
import { createServer as createSecureServer } from "node:https";
import { chmodSync, existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { homedir } from "node:os";
import { join, resolve } from "node:path";
import { WebSocketServer, type WebSocket } from "ws";
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import * as z from "zod/v4";

const DEFAULT_HOOK_PORT = 17877;
const PROTOCOL_VERSION = 1;
const MAX_TEXT = 480;
const MAX_HOOK_BODY_BYTES = 32 * 1024;
const MAX_PENDING_QUESTIONS = 8;
const TOKEN_LENGTH = 32;
const TOKEN_PATTERN = /^[A-Za-z0-9_-]{32}$/;
const HOOK_REQUEST_TIMEOUT_MS = 15_000;
const HOOK_HEADERS_TIMEOUT_MS = 10_000;
const HOOK_KEEP_ALIVE_TIMEOUT_MS = 5_000;
const HOOK_MAX_CONNECTIONS = 32;
const HOOK_MAX_REQUESTS_PER_SOCKET = 16;

type Json = null | boolean | number | string | Json[] | { [key: string]: Json };

export interface BridgeConfig {
  hookPort: number;
  devicePort: number;
  deviceHost: string;
  token: string;
  hookToken: string;
  configPath: string;
  tlsCertPath?: string;
  tlsKeyPath?: string;
  tlsCaPath?: string;
}

interface PersistedConfig {
  hookPort?: number;
  devicePort?: number;
  token?: string;
  hookToken?: string;
}

interface PromptDraft { id: string; text: string; source: "keyboard" | "voice"; createdAt: string; }
interface PendingQuestion { id: string; resolve: (answer: string | null) => void; timer: NodeJS.Timeout; }
interface DeviceMessage { v?: number; type?: string; id?: string; [key: string]: unknown; }

class HttpError extends Error {
  constructor(readonly status: number, message: string) { super(message); }
}

function parsePort(value: string | undefined, fallback: number): number {
  const parsed = Number(value);
  return Number.isInteger(parsed) && parsed >= 1024 && parsed <= 65535 ? parsed : fallback;
}

function requireStrongToken(value: string, label: string): string {
  let repeated = false;
  for (let period = 1; period <= TOKEN_LENGTH / 2; period += 1) {
    if (TOKEN_LENGTH % period === 0 && value === value.slice(0, period).repeat(TOKEN_LENGTH / period)) {
      repeated = true;
      break;
    }
  }
  if (!TOKEN_PATTERN.test(value) || repeated) {
    throw new Error(`${label} must be a ${TOKEN_LENGTH}-character URL-safe token provisioned from a CSPRNG`);
  }
  return value;
}

function configPath(): string {
  return process.env.CARDPUTER_BRIDGE_CONFIG || join(homedir(), ".claude-cardputer-bridge", "config.json");
}

function readPersisted(path: string): PersistedConfig {
  if (!existsSync(path)) return {};
  try { return JSON.parse(readFileSync(path, "utf8")) as PersistedConfig; }
  catch { throw new Error(`invalid bridge config: ${path}`); }
}

function writePersisted(path: string, config: PersistedConfig): void {
  mkdirSync(join(path, ".."), { recursive: true, mode: 0o700 });
  writeFileSync(path, `${JSON.stringify(config, null, 2)}\n`, { mode: 0o600 });
  // `mode` only governs a newly-created file; tighten an existing config too.
  chmodSync(path, 0o600);
}

export function loadConfig(env: NodeJS.ProcessEnv = process.env): BridgeConfig {
  const path = env.CARDPUTER_BRIDGE_CONFIG || configPath();
  const persisted = readPersisted(path);
  const hookPort = parsePort(env.CARDPUTER_BRIDGE_PORT, persisted.hookPort || DEFAULT_HOOK_PORT);
  const defaultDevicePort = hookPort < 65535 ? hookPort + 1 : DEFAULT_HOOK_PORT + 1;
  const devicePort = parsePort(env.CARDPUTER_BRIDGE_DEVICE_PORT, persisted.devicePort || defaultDevicePort);
  const configuredToken = env.CARDPUTER_PAIRING_TOKEN?.trim();
  const configuredHookToken = env.CARDPUTER_HOOK_TOKEN?.trim();
  const token = configuredToken ? requireStrongToken(configuredToken, "CARDPUTER_PAIRING_TOKEN")
    : persisted.token ? requireStrongToken(persisted.token, "stored pairing token")
      : randomBytes(24).toString("base64url");
  const hookToken = configuredHookToken ? requireStrongToken(configuredHookToken, "CARDPUTER_HOOK_TOKEN")
    : persisted.hookToken ? requireStrongToken(persisted.hookToken, "stored hook token")
      : randomBytes(24).toString("base64url");
  writePersisted(path, { hookPort, devicePort, token, hookToken });
  return {
    hookPort,
    devicePort,
    deviceHost: env.CARDPUTER_BRIDGE_DEVICE_BIND_HOST?.trim() ||
      env.CARDPUTER_BRIDGE_BIND_HOST?.trim() || "127.0.0.1",
    token,
    hookToken,
    configPath: path,
    tlsCertPath: env.CARDPUTER_BRIDGE_TLS_CERT?.trim(),
    tlsKeyPath: env.CARDPUTER_BRIDGE_TLS_KEY?.trim(),
    tlsCaPath: env.CARDPUTER_BRIDGE_TLS_CA?.trim()
  };
}

function text(value: unknown, fallback = ""): string {
  if (typeof value === "string") return value.slice(0, MAX_TEXT);
  if (typeof value === "number" || typeof value === "boolean") return String(value);
  return fallback;
}

function jsonResponse(res: ServerResponse, status: number, body: Json): void {
  const payload = `${JSON.stringify(body)}\n`;
  res.writeHead(status, { "content-type": "application/json; charset=utf-8", "content-length": Buffer.byteLength(payload) });
  res.end(payload);
}

function isLoopback(address: string | undefined): boolean {
  return address === "127.0.0.1" || address === "::1" || address === "::ffff:127.0.0.1";
}

function secureEqual(actual: string | undefined, expected: string): boolean {
  if (!actual) return false;
  const a = Buffer.from(actual);
  const b = Buffer.from(expected);
  return a.length === b.length && timingSafeEqual(a, b);
}

function bearer(req: IncomingMessage): string | undefined {
  const value = req.headers.authorization;
  return value?.startsWith("Bearer ") ? value.slice("Bearer ".length) : undefined;
}

async function readJson(req: IncomingMessage): Promise<Record<string, unknown>> {
  const declared = Number(req.headers["content-length"] || "0");
  if (Number.isFinite(declared) && declared > MAX_HOOK_BODY_BYTES) throw new HttpError(413, "request body too large");
  let total = 0;
  const chunks: Buffer[] = [];
  for await (const chunk of req) {
    const bytes = Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk);
    total += bytes.length;
    if (total > MAX_HOOK_BODY_BYTES) throw new HttpError(413, "request body too large");
    chunks.push(bytes);
  }
  if (chunks.length === 0) return {};
  try { return JSON.parse(Buffer.concat(chunks).toString("utf8")) as Record<string, unknown>; }
  catch { throw new HttpError(400, "invalid JSON"); }
}

function validDeviceEndpoint(endpoint: string): URL {
  const parsed = new URL(endpoint);
  if (parsed.protocol !== "wss:" || parsed.username || parsed.password ||
      parsed.pathname !== "/device" || parsed.search || parsed.hash) {
    throw new Error("pairing endpoint must be wss://host:port/device without query data");
  }
  return parsed;
}

export class CardputerBridge {
  private device: WebSocket | null = null;
  private deviceInfo: Record<string, unknown> = {};
  private deviceState: Record<string, unknown> = {};
  private promptQueue: PromptDraft[] = [];
  private pending = new Map<string, PendingQuestion>();
  private lastSummary = "";
  private hookServer = createServer((req, res) => void this.handleHookHttp(req, res));
  private deviceServer: ReturnType<typeof createSecureServer> | null = null;
  private wsServer = new WebSocketServer({ noServer: true });

  constructor(private readonly config: BridgeConfig) {
    this.hookServer.requestTimeout = HOOK_REQUEST_TIMEOUT_MS;
    this.hookServer.headersTimeout = HOOK_HEADERS_TIMEOUT_MS;
    this.hookServer.keepAliveTimeout = HOOK_KEEP_ALIVE_TIMEOUT_MS;
    this.hookServer.maxConnections = HOOK_MAX_CONNECTIONS;
    this.hookServer.maxRequestsPerSocket = HOOK_MAX_REQUESTS_PER_SOCKET;
  }

  private tlsMaterial(): { cert: string; key: string; ca: string } | null {
    const { tlsCertPath, tlsKeyPath, tlsCaPath } = this.config;
    if (!tlsCertPath && !tlsKeyPath && !tlsCaPath) return null;
    if (!tlsCertPath || !tlsKeyPath || !tlsCaPath) throw new Error("secure device listener needs CARDPUTER_BRIDGE_TLS_CERT, CARDPUTER_BRIDGE_TLS_KEY, and CARDPUTER_BRIDGE_TLS_CA");
    const cert = readFileSync(tlsCertPath, "utf8");
    const key = readFileSync(tlsKeyPath, "utf8");
    const ca = readFileSync(tlsCaPath, "utf8");
    if (!ca.includes("-----BEGIN CERTIFICATE-----") || !ca.includes("-----END CERTIFICATE-----")) throw new Error("CARDPUTER_BRIDGE_TLS_CA is not PEM certificate material");
    return { cert, key, ca };
  }

  async start(): Promise<void> {
    await this.listen(this.hookServer, this.config.hookPort, "127.0.0.1");
    const tls = this.tlsMaterial();
    if (!tls) {
      console.error("Cardputer hook service listening on 127.0.0.1:%d; secure device listener disabled until TLS paths are configured", this.config.hookPort);
      return;
    }
    this.deviceServer = createSecureServer({ cert: tls.cert, key: tls.key }, (_req, res) => jsonResponse(res, 404, { ok: false, error: "not found" }));
    this.deviceServer.requestTimeout = 15_000;
    this.deviceServer.on("upgrade", (req, socket, head) => {
      const url = new URL(req.url || "/", `https://${req.headers.host || "localhost"}`);
      if (url.pathname !== "/device" || url.search || !secureEqual(bearer(req), this.config.token)) {
        socket.write("HTTP/1.1 401 Unauthorized\r\n\r\n");
        socket.destroy();
        return;
      }
      this.wsServer.handleUpgrade(req, socket, head, ws => this.attachDevice(ws));
    });
    await this.listen(this.deviceServer, this.config.devicePort, this.config.deviceHost);
    console.error(`Cardputer secure device listener on ${this.config.deviceHost}:${this.config.devicePort}; hook service on 127.0.0.1:${this.config.hookPort}`);
  }

  async stop(): Promise<void> {
    for (const pending of this.pending.values()) {
      clearTimeout(pending.timer);
      pending.resolve(null);
    }
    this.pending.clear();
    if (this.device) this.device.close();
    await Promise.all([
      this.close(this.hookServer),
      this.deviceServer ? this.close(this.deviceServer) : Promise.resolve()
    ]);
    this.wsServer.close();
  }

  private listen(server: ReturnType<typeof createServer> | ReturnType<typeof createSecureServer>, port: number, host: string): Promise<void> {
    return new Promise((resolveStart, rejectStart) => {
      server.once("error", rejectStart);
      server.listen(port, host, () => { server.off("error", rejectStart); resolveStart(); });
    });
  }

  private close(server: ReturnType<typeof createServer> | ReturnType<typeof createSecureServer>): Promise<void> {
    return new Promise(resolveClose => server.close(() => resolveClose()));
  }

  status(): Record<string, unknown> {
    const hasDevice = Boolean(this.device && this.device.readyState === this.device.OPEN);
    return { ok: true, hookPort: this.config.hookPort, devicePort: this.config.devicePort, deviceListener: Boolean(this.deviceServer), hasDevice, queuedPrompts: this.promptQueue.length, pendingQuestions: this.pending.size, deviceInfo: this.deviceInfo, deviceState: this.deviceState };
  }

  health(): Record<string, unknown> {
    return { ok: true, hookPort: this.config.hookPort, deviceListener: Boolean(this.deviceServer) };
  }

  notify(title: string, message: string, level = "info"): boolean {
    this.lastSummary = message.slice(0, MAX_TEXT);
    return this.send({ v: PROTOCOL_VERSION, type: "display.summary", title: title.slice(0, 48), text: this.lastSummary, level: level.slice(0, 16) });
  }

  nextPrompt(consume: boolean): PromptDraft | null { return this.promptQueue.length === 0 ? null : consume ? this.promptQueue.shift() || null : this.promptQueue[0] || null; }

  async ask(question: string, options: string[], timeoutSeconds: number): Promise<string | null> {
    if (!this.device || this.device.readyState !== this.device.OPEN || this.pending.size >= MAX_PENDING_QUESTIONS) return null;
    const id = `q_${Date.now().toString(36)}_${randomBytes(3).toString("hex")}`;
    const timeoutMs = Math.max(5, Math.min(timeoutSeconds, 300)) * 1000;
    const result = new Promise<string | null>(resolveAnswer => {
      const timer = setTimeout(() => { this.pending.delete(id); resolveAnswer(null); }, timeoutMs);
      this.pending.set(id, { id, resolve: resolveAnswer, timer });
    });
    this.send({ v: PROTOCOL_VERSION, type: "question.request", id, question: question.slice(0, MAX_TEXT), options: options.slice(0, 5).map(opt => opt.slice(0, 64)) });
    return result;
  }

  pairingConfig(endpoint?: string): Record<string, unknown> {
    const tls = this.tlsMaterial();
    if (!tls) throw new Error("secure pairing config requires CARDPUTER_BRIDGE_TLS_CERT, CARDPUTER_BRIDGE_TLS_KEY, and CARDPUTER_BRIDGE_TLS_CA");
    const defaultHost = this.config.deviceHost === "0.0.0.0" ? "<mac-lan-ip>" : this.config.deviceHost;
    const value = endpoint || `wss://${defaultHost}:${this.config.devicePort}/device`;
    validDeviceEndpoint(value);
    return { v: PROTOCOL_VERSION, type: "claude-cardputer-bridge", endpoint: value, token: this.config.token, ca: tls.ca, note: "Public CA only. Transfer to your own Cardputer ADV; never commit credentials." };
  }

  writePairingConfig(dir: string, endpoint?: string): void {
    const target = resolve(dir);
    mkdirSync(target, { recursive: true, mode: 0o700 });
    writeFileSync(join(target, "manifest.json"), `${JSON.stringify({ name: "bridge-config", type: "claude-cardputer-bridge", version: PROTOCOL_VERSION }, null, 2)}\n`, { mode: 0o600 });
    writeFileSync(join(target, "bridge.json"), `${JSON.stringify(this.pairingConfig(endpoint), null, 2)}\n`, { mode: 0o600 });
  }

  private attachDevice(ws: WebSocket): void {
    if (this.device && this.device.readyState === this.device.OPEN) this.device.close(1000, "replaced");
    this.device = ws; this.deviceInfo = {}; this.deviceState = {};
    ws.on("message", data => this.handleDeviceMessage(String(data)));
    ws.on("close", () => { if (this.device === ws) this.device = null; });
    this.send({ v: PROTOCOL_VERSION, type: "bridge.status", connected: true });
  }

  private handleDeviceMessage(raw: string): void {
    let msg: DeviceMessage;
    try { msg = JSON.parse(raw) as DeviceMessage; } catch { return; }
    if (msg.v !== PROTOCOL_VERSION || typeof msg.type !== "string") return;
    if (msg.type === "hello") { this.deviceInfo = { ...msg }; this.send({ v: PROTOCOL_VERSION, type: "bridge.status", connected: true }); return; }
    if (msg.type === "state") { this.deviceState = { ...msg }; return; }
    if (msg.type === "prompt.draft" || msg.type === "voice.text") {
      const draft: PromptDraft = { id: text(msg.id, `draft_${Date.now().toString(36)}`), text: text(msg.text), source: msg.type === "voice.text" ? "voice" : "keyboard", createdAt: new Date().toISOString() };
      if (draft.text) { this.promptQueue.push(draft); this.promptQueue = this.promptQueue.slice(-10); }
      this.send({ v: PROTOCOL_VERSION, type: "prompt.result", id: draft.id, status: "queued" });
      return;
    }
    if (msg.type === "question.answer" && typeof msg.id === "string") {
      const pending = this.pending.get(msg.id); if (!pending) return;
      clearTimeout(pending.timer); this.pending.delete(msg.id); pending.resolve(text(msg.answer, text(msg.text)) || null);
    }
  }

  private send(payload: Record<string, unknown>): boolean {
    if (!this.device || this.device.readyState !== this.device.OPEN) return false;
    this.device.send(JSON.stringify(payload)); return true;
  }

  private async handleHookHttp(req: IncomingMessage, res: ServerResponse): Promise<void> {
    const url = new URL(req.url || "/", "http://127.0.0.1");
    try {
      if (!isLoopback(req.socket.remoteAddress)) throw new HttpError(403, "loopback only");
      if (req.method === "GET" && url.pathname === "/health") { jsonResponse(res, 200, this.health() as Json); return; }
      if (req.method === "POST" && url.pathname === "/hook") {
        if (!secureEqual(bearer(req), this.config.hookToken)) throw new HttpError(401, "unauthorized");
        const body = await readJson(req); const response = await this.handleHook(body); jsonResponse(res, 200, response as Json); return;
      }
      jsonResponse(res, 404, { ok: false, error: "not found" });
    } catch (error) {
      const known = error instanceof HttpError ? error : new HttpError(500, "internal error");
      jsonResponse(res, known.status, { ok: false, error: known.message });
    }
  }

  private async handleHook(body: Record<string, unknown>): Promise<Record<string, unknown>> {
    const eventName = text(body.eventName, text(body.hook_event_name, "hook"));
    const event = typeof body.event === "object" && body.event ? body.event as Record<string, unknown> : body;
    const toolName = text(event.tool_name, text(event.tool, ""));
    const toolInput = typeof event.tool_input === "object" && event.tool_input ? event.tool_input as Record<string, unknown> : typeof event.input === "object" && event.input ? event.input as Record<string, unknown> : {};
    if (eventName === "PreToolUse" && toolName === "AskUserQuestion") {
      const questions = Array.isArray(toolInput.questions) ? toolInput.questions : [];
      const first = typeof questions[0] === "object" && questions[0] ? questions[0] as Record<string, unknown> : {};
      const question = text(first.question, text(toolInput.question, "Claude has a question"));
      const rawOptions = Array.isArray(first.options) ? first.options : Array.isArray(toolInput.options) ? toolInput.options : [];
      const options = rawOptions.map(opt => typeof opt === "string" ? opt : text((opt as Record<string, unknown>).label)).filter(Boolean);
      const answer = await this.ask(question, options, 120);
      return answer ? { hookSpecificOutput: { hookEventName: "PreToolUse", permissionDecision: "allow", updatedInput: { ...toolInput, answers: [answer] } } } : {};
    }
    if (eventName === "Elicitation") {
      const answer = await this.ask(text(event.message, text(event.prompt, "Claude needs input")), [], 120);
      return answer ? { hookSpecificOutput: { hookEventName: "Elicitation", action: "accept", content: { answer } } } : {};
    }
    if (eventName === "PermissionRequest" || eventName === "Notification") this.notify(eventName, text(event.message, toolName || "Claude needs attention"), "attention");
    if (eventName === "PostCompact" || eventName === "Stop") this.notify("Claude", text(event.summary, text(event.message, eventName)), "info");
    return {};
  }
}

function registerTools(server: McpServer, bridge: CardputerBridge): void {
  server.registerTool("notify_cardputer", { description: "Send a concise status or summary to connected Cardputer.", inputSchema: { title: z.string().max(48).default("Claude"), message: z.string().max(MAX_TEXT), level: z.enum(["info", "attention", "success", "warning", "error"]).default("info") } }, async ({ title, message, level }) => {
    const delivered = bridge.notify(title, message, level); return { content: [{ type: "text", text: delivered ? "Sent to Cardputer." : "No Cardputer connected." }], structuredContent: { delivered } };
  });
  server.registerTool("ask_cardputer", { description: "Ask Cardputer user short question and wait for answer.", inputSchema: { question: z.string().max(MAX_TEXT), options: z.array(z.string().max(64)).max(5).default([]), timeoutSeconds: z.number().int().min(5).max(300).default(120) } }, async ({ question, options, timeoutSeconds }) => {
    const answer = await bridge.ask(question, options, timeoutSeconds); return { content: [{ type: "text", text: answer ? `Answer: ${answer}` : "No answer received." }], structuredContent: { answer } };
  });
  server.registerTool("get_cardputer_prompt", { description: "Read next prompt draft queued from Cardputer.", inputSchema: { consume: z.boolean().default(true) } }, async ({ consume }) => {
    const draft = bridge.nextPrompt(consume); return { content: [{ type: "text", text: draft ? draft.text : "No prompt draft queued." }], structuredContent: { draft } };
  });
  server.registerTool("device_status", { description: "Report bridge and Cardputer connection status.", inputSchema: {} }, async () => {
    const status = bridge.status(); return { content: [{ type: "text", text: JSON.stringify(status, null, 2) }], structuredContent: status };
  });
  server.registerTool("generate_pairing_config", { description: "Return secure bridge pairing JSON for Cardputer.", inputSchema: { endpoint: z.string().optional() } }, async ({ endpoint }) => {
    const config = bridge.pairingConfig(endpoint); return { content: [{ type: "text", text: JSON.stringify(config, null, 2) }], structuredContent: config };
  });
}

async function main(): Promise<void> {
  const config = loadConfig();
  const bridge = new CardputerBridge(config);
  const writeIndex = process.argv.indexOf("--write-pairing-config");
  if (writeIndex !== -1) { bridge.writePairingConfig(process.argv[writeIndex + 1] || "bridge-config", process.env.CARDPUTER_BRIDGE_ENDPOINT); console.error("Wrote secure Cardputer bridge config"); return; }
  if (process.argv.includes("--print-config")) { console.log(JSON.stringify({ hookPort: config.hookPort, devicePort: config.devicePort, deviceBindHost: config.deviceHost, configPath: config.configPath, pairingTokenConfigured: Boolean(config.token), hookTokenConfigured: Boolean(config.hookToken), tlsConfigured: Boolean(config.tlsCertPath && config.tlsKeyPath && config.tlsCaPath) }, null, 2)); return; }
  await bridge.start();
  const mcpServer = new McpServer({ name: "claude-cardputer-bridge", version: "0.1.0" });
  registerTools(mcpServer, bridge);
  await mcpServer.connect(new StdioServerTransport());
}

if (process.argv[1] && resolve(process.argv[1]) === new URL(import.meta.url).pathname) {
  main().catch(error => { console.error("Cardputer bridge fatal error:", error instanceof Error ? error.message : error); process.exit(1); });
}
