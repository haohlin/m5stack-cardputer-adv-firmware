import { createInterface } from "node:readline";
import type { Readable, Writable } from "node:stream";
import type { OrcaSnapshot, OrcaSnapshotItem } from "./types.js";
import { assertBoundedText, assertQuestionLabels, isRecord } from "./validation.js";

interface JsonRpcRequest {
  jsonrpc?: unknown;
  id?: unknown;
  method?: unknown;
  params?: unknown;
}

interface JsonRpcResponse {
  jsonrpc: "2.0";
  id: unknown;
  result?: unknown;
  error?: { code: number; message: string };
}

export interface McpHandlerOptions {
  request: (method: string, params: unknown) => Promise<unknown>;
}

const tools = [
  {
    name: "orca_buddy_status",
    description: "Read sanitized Orca and Cardputer bridge status.",
    inputSchema: { type: "object", properties: {}, additionalProperties: false }
  },
  {
    name: "orca_buddy_notify",
    description: "Show a short notification on the paired Cardputer.",
    inputSchema: {
      type: "object",
      properties: { text: { type: "string", description: "Maximum 480 UTF-8 bytes." } },
      required: ["text"],
      additionalProperties: false
    }
  },
  {
    name: "orca_buddy_ask",
    description: "Ask the paired Cardputer one bounded multiple-choice question.",
    inputSchema: {
      type: "object",
      properties: {
        question: { type: "string", description: "Maximum 480 UTF-8 bytes." },
        labels: {
          type: "array",
          minItems: 2,
          maxItems: 5,
          items: { type: "string", description: "Maximum 80 UTF-8 bytes." }
        }
      },
      required: ["question", "labels"],
      additionalProperties: false
    }
  },
  {
    name: "orca_buddy_get_prompt",
    description: "Consume the latest bounded prompt draft from the paired Cardputer.",
    inputSchema: { type: "object", properties: {}, additionalProperties: false }
  }
] as const;

function readItem(value: unknown): OrcaSnapshotItem {
  if (!isRecord(value)) return {};
  const item: OrcaSnapshotItem = {};
  if (typeof value.displayName === "string") item.displayName = value.displayName.slice(0, 160);
  if (typeof value.workspaceStatus === "string") item.workspaceStatus = value.workspaceStatus.slice(0, 160);
  if (typeof value.agentState === "string") item.agentState = value.agentState.slice(0, 160);
  return item;
}

function publicSnapshot(value: unknown): OrcaSnapshot {
  if (!isRecord(value)) return { connected: false, worktreeCount: 0, items: [] };
  return {
    connected: value.connected === true,
    worktreeCount: typeof value.worktreeCount === "number" && Number.isSafeInteger(value.worktreeCount)
      ? Math.max(0, value.worktreeCount)
      : 0,
    items: Array.isArray(value.items) ? value.items.slice(0, 12).map(readItem) : []
  };
}

function whitelistResult(method: string, value: unknown): unknown {
  const record = isRecord(value) ? value : {};
  switch (method) {
    case "status":
      return {
        snapshot: publicSnapshot(record.snapshot),
        deviceConnected: record.deviceConnected === true,
        pendingQuestions: typeof record.pendingQuestions === "number" && Number.isSafeInteger(record.pendingQuestions)
          ? Math.max(0, Math.min(8, record.pendingQuestions))
          : 0
      };
    case "notify":
      return { delivered: record.delivered === true };
    case "ask": {
      const result: { questionId?: string; answer?: string } = {};
      if (typeof record.questionId === "string") result.questionId = record.questionId;
      if (typeof record.answer === "string") result.answer = record.answer;
      return result;
    }
    case "getPrompt": {
      const prompt = typeof record.prompt === "string" && Buffer.byteLength(record.prompt, "utf8") <= 480
        ? record.prompt
        : null;
      return { prompt };
    }
    default:
      return {};
  }
}

function toolResult(value: unknown, isError = false): unknown {
  return {
    content: [{ type: "text", text: typeof value === "string" ? value : JSON.stringify(value) }],
    structuredContent: typeof value === "string" ? { message: value } : value,
    ...(isError ? { isError: true } : {})
  };
}

export function createMcpHandler(options: McpHandlerOptions): (message: JsonRpcRequest) => Promise<JsonRpcResponse> {
  return async (message) => {
    const id = message.id ?? null;
    try {
      if (message.jsonrpc !== "2.0" || typeof message.method !== "string") {
        return { jsonrpc: "2.0", id, error: { code: -32600, message: "Invalid Request" } };
      }
      if (message.method === "initialize") {
        return {
          jsonrpc: "2.0",
          id,
          result: {
            protocolVersion: "2025-06-18",
            capabilities: { tools: {} },
            serverInfo: { name: "orca-cardputer-mcp", version: "0.1.5" }
          }
        };
      }
      if (message.method === "tools/list") {
        return { jsonrpc: "2.0", id, result: { tools } };
      }
      if (message.method === "ping") return { jsonrpc: "2.0", id, result: {} };
      if (message.method !== "tools/call" || !isRecord(message.params) || typeof message.params.name !== "string") {
        return { jsonrpc: "2.0", id, error: { code: -32601, message: "Method not found" } };
      }
      const args = isRecord(message.params.arguments) ? message.params.arguments : {};
      let daemonMethod: string;
      let daemonParams: unknown = {};
      switch (message.params.name) {
        case "orca_buddy_status":
          daemonMethod = "status";
          break;
        case "orca_buddy_notify":
          assertBoundedText(args.text, "notification text");
          daemonMethod = "notify";
          daemonParams = { text: args.text };
          break;
        case "orca_buddy_ask":
          assertBoundedText(args.question, "question text");
          assertQuestionLabels(args.labels);
          daemonMethod = "ask";
          daemonParams = { question: args.question, labels: args.labels };
          break;
        case "orca_buddy_get_prompt":
          daemonMethod = "getPrompt";
          break;
        default:
          return { jsonrpc: "2.0", id, result: toolResult("unknown Orca Buddy tool", true) };
      }
      const result = whitelistResult(daemonMethod, await options.request(daemonMethod, daemonParams));
      return { jsonrpc: "2.0", id, result: toolResult(result) };
    } catch (error) {
      const messageText = error instanceof Error ? error.message : "tool call failed";
      return { jsonrpc: "2.0", id, result: toolResult(messageText, true) };
    }
  };
}

export async function runMcpServer(
  options: McpHandlerOptions,
  input: Readable = process.stdin,
  output: Writable = process.stdout
): Promise<void> {
  const handle = createMcpHandler(options);
  const lines = createInterface({ input, crlfDelay: Infinity });
  for await (const line of lines) {
    if (Buffer.byteLength(line, "utf8") > 32 * 1024) continue;
    try {
      const request = JSON.parse(line) as JsonRpcRequest;
      if (request.method === "notifications/initialized") continue;
      const response = await handle(request);
      output.write(`${JSON.stringify(response)}\n`);
    } catch {
      output.write(`${JSON.stringify({ jsonrpc: "2.0", id: null, error: { code: -32700, message: "Parse error" } })}\n`);
    }
  }
}
