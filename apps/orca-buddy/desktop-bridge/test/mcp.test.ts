import assert from "node:assert/strict";
import test from "node:test";
import { createMcpHandler } from "../src/mcp.js";

test("MCP lists only four Orca Buddy tools", async () => {
  const handle = createMcpHandler({ request: async () => ({}) });
  const response = await handle({ jsonrpc: "2.0", id: 1, method: "tools/list" });
  const names = (response.result as { tools: Array<{ name: string }> }).tools.map((tool) => tool.name);
  assert.deepEqual(names, [
    "orca_buddy_status",
    "orca_buddy_notify",
    "orca_buddy_ask",
    "orca_buddy_get_prompt"
  ]);
});

test("MCP calls only daemon methods and whitelists every tool result", async () => {
  const calls: Array<{ method: string; params: unknown }> = [];
  const secret = "PAIRING-SECRET-MUST-NOT-RETURN";
  const handle = createMcpHandler({
    request: async (method, params) => {
      calls.push({ method, params });
      if (method === "status") {
        return {
          snapshot: { connected: true, worktreeCount: 0, items: [] },
          deviceConnected: false,
          pendingQuestions: 0,
          pairingToken: secret,
          config: { privateKey: secret }
        };
      }
      if (method === "notify") return { delivered: true, pairingToken: secret };
      if (method === "ask") return { questionId: "question-1", answer: "No", bearer: secret };
      return { prompt: "Draft", localAuthToken: secret };
    }
  });

  const messages = [
    { id: 1, name: "orca_buddy_status", arguments: {} },
    { id: 2, name: "orca_buddy_notify", arguments: { text: "Notice" } },
    { id: 3, name: "orca_buddy_ask", arguments: { question: "Proceed?", labels: ["Yes", "No"] } },
    { id: 4, name: "orca_buddy_get_prompt", arguments: {} }
  ];
  for (const message of messages) {
    const response = await handle({
      jsonrpc: "2.0",
      id: message.id,
      method: "tools/call",
      params: { name: message.name, arguments: message.arguments }
    });
    assert.doesNotMatch(JSON.stringify(response), new RegExp(secret));
    assert.doesNotMatch(JSON.stringify(response), /pairingToken|privateKey|bearer|localAuthToken/);
  }
  assert.deepEqual(calls.map((call) => call.method), ["status", "notify", "ask", "getPrompt"]);
});

test("MCP validates UTF-8 and question bounds before daemon calls", async () => {
  let calls = 0;
  const handle = createMcpHandler({ request: async () => { calls += 1; return {}; } });
  const response = await handle({
    jsonrpc: "2.0",
    id: 1,
    method: "tools/call",
    params: { name: "orca_buddy_notify", arguments: { text: "界".repeat(161) } }
  });
  assert.equal(calls, 0);
  assert.equal((response.result as { isError: boolean }).isError, true);
  assert.match(JSON.stringify(response), /480 UTF-8 bytes/);
});
