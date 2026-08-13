#!/usr/bin/env node
const eventName = process.argv[2] || "Hook";
const bridgeUrl = (process.env.CARDPUTER_BRIDGE_URL || "http://127.0.0.1:17877").replace(/\/$/, "");

const longEvents = new Set(["PreToolUse", "Elicitation", "PermissionRequest"]);
const timeoutMs = Number(process.env.CARDPUTER_HOOK_TIMEOUT_MS ||
  (longEvents.has(eventName) ? 125000 : 2500));

async function readStdin() {
  const chunks = [];
  for await (const chunk of process.stdin) chunks.push(Buffer.from(chunk));
  if (chunks.length === 0) return {};
  try {
    return JSON.parse(Buffer.concat(chunks).toString("utf8"));
  } catch {
    return {};
  }
}

async function main() {
  const event = await readStdin();
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), timeoutMs);
  try {
    const response = await fetch(`${bridgeUrl}/hook`, {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ eventName, event }),
      signal: controller.signal
    });
    if (!response.ok) return;
    const text = (await response.text()).trim();
    if (text && text !== "{}") process.stdout.write(`${text}\n`);
  } catch {
    // Bridge is optional. Silent fallback keeps Claude's native UI unblocked.
  } finally {
    clearTimeout(timer);
  }
}

main();

