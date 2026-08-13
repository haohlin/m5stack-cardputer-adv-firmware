#!/usr/bin/env node
import { existsSync, readFileSync, statSync } from "node:fs";
import { homedir } from "node:os";
import { join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const eventName = process.argv[2] || "Hook";
const credentialProvenance = "bridge-csprng-v1";
const tokenPattern = /^[A-Za-z0-9_-]{32}$/;

function tokenAllowed(value) {
  if (!tokenPattern.test(value) || /(.)\1{7}/.test(value)) return false;
  for (let period = 1; period <= value.length / 2; period += 1) {
    if ([...value].slice(period).every((character, index) => character === value[index % period])) return false;
  }
  return true;
}

export function localBridgeUrl(raw) {
  try {
    const url = new URL(raw || "http://127.0.0.1:17877");
    const loopback = url.hostname === "127.0.0.1" || url.hostname === "::1" || url.hostname === "[::1]";
    if (url.protocol !== "http:" || !loopback || url.username || url.password || url.search || url.hash) return null;
    return url.toString().replace(/\/$/, "");
  } catch {
    return null;
  }
}

const bridgeUrl = localBridgeUrl(process.env.CARDPUTER_BRIDGE_URL);

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

export function storedHookToken(path) {
  if (!existsSync(path)) return null;
  try {
    if ((statSync(path).mode & 0o077) !== 0) return null;
    const config = JSON.parse(readFileSync(path, "utf8"));
    if (config.credentialProvenance !== credentialProvenance) return null;
    const value = config.hookToken;
    return typeof value === "string" && tokenAllowed(value) ? value : null;
  } catch {
    return null;
  }
}

function hookToken() {
  const path = process.env.CARDPUTER_BRIDGE_CONFIG || join(homedir(), ".claude-cardputer-bridge", "config.json");
  return storedHookToken(path);
}

async function main() {
  const event = await readStdin();
  const token = hookToken();
  if (!token || !bridgeUrl) return;
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), timeoutMs);
  try {
    const response = await fetch(`${bridgeUrl}/hook`, {
      method: "POST",
      headers: { "content-type": "application/json", authorization: `Bearer ${token}` },
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

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) main();
