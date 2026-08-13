#!/usr/bin/env node
import { existsSync, lstatSync, readFileSync } from "node:fs";
import { request } from "node:http";
import { homedir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const eventName = process.argv[2] || "Hook";
const credentialProvenance = "bridge-csprng-v1";
const tokenPattern = /^[A-Za-z0-9_-]{32}$/;
const maxHookBytes = 32 * 1024;

function tokenAllowed(value) {
  if (!tokenPattern.test(value) || /(.)\1{7}/.test(value)) return false;
  for (let period = 1; period <= value.length / 2; period += 1) {
    if ([...value].slice(period).every((character, index) => character === value[index % period])) return false;
  }
  return true;
}

export function hookSocketPath(configPath) {
  return join(dirname(resolve(configPath)), "hook.sock");
}

const longEvents = new Set(["PreToolUse", "Elicitation", "PermissionRequest"]);
const requestedTimeout = Number(process.env.CARDPUTER_HOOK_TIMEOUT_MS ||
  (longEvents.has(eventName) ? 125000 : 2500));
const timeoutMs = Number.isFinite(requestedTimeout)
  ? Math.max(100, Math.min(requestedTimeout, 130000)) : 2500;

export async function readHookInput(stream = process.stdin) {
  const chunks = [];
  let total = 0;
  for await (const chunk of stream) {
    const bytes = Buffer.from(chunk);
    total += bytes.length;
    if (total > maxHookBytes) return null;
    chunks.push(bytes);
  }
  if (chunks.length === 0) return {};
  try {
    const parsed = JSON.parse(Buffer.concat(chunks).toString("utf8"));
    return parsed && typeof parsed === "object" && !Array.isArray(parsed) ? parsed : null;
  } catch {
    return null;
  }
}

export function serializeHookRequest(name, event) {
  const body = JSON.stringify({ eventName: name, event });
  return Buffer.byteLength(body) <= maxHookBytes ? body : null;
}

function plainObject(value) {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

export function validateHookOutput(raw) {
  if (Buffer.byteLength(raw) > maxHookBytes) return null;
  let parsed;
  try { parsed = JSON.parse(raw); } catch { return null; }
  if (!plainObject(parsed)) return null;
  if (Object.keys(parsed).length === 0) return "{}";
  if (Object.keys(parsed).length !== 1 || !plainObject(parsed.hookSpecificOutput)) return null;
  const output = parsed.hookSpecificOutput;
  if (output.hookEventName === "PreToolUse" && output.permissionDecision === "allow" && plainObject(output.updatedInput)) {
    return JSON.stringify(parsed);
  }
  if (output.hookEventName === "Elicitation" && output.action === "accept" && plainObject(output.content)) {
    return JSON.stringify(parsed);
  }
  return null;
}

export function storedHookToken(path) {
  if (!existsSync(path)) return null;
  try {
    const file = lstatSync(path);
    const directory = lstatSync(dirname(resolve(path)));
    if (!file.isFile() || file.isSymbolicLink() || (file.mode & 0o077) !== 0) return null;
    if (!directory.isDirectory() || directory.isSymbolicLink() || (directory.mode & 0o077) !== 0) return null;
    const config = JSON.parse(readFileSync(path, "utf8"));
    if (config.credentialProvenance !== credentialProvenance) return null;
    const value = config.hookToken;
    return typeof value === "string" && tokenAllowed(value) ? value : null;
  } catch {
    return null;
  }
}

function postHook(socketPath, token, body, signal) {
  return new Promise(resolveResponse => {
    const req = request({
      socketPath,
      path: "/hook",
      method: "POST",
      headers: {
        "content-type": "application/json",
        "content-length": Buffer.byteLength(body),
        authorization: `Bearer ${token}`
      },
      signal
    }, response => {
      let total = 0;
      const chunks = [];
      response.on("data", chunk => {
        total += chunk.length;
        if (total > maxHookBytes) response.destroy();
        else chunks.push(Buffer.from(chunk));
      });
      response.on("end", () => {
        if (response.statusCode !== 200 || total > maxHookBytes) resolveResponse(null);
        else resolveResponse(validateHookOutput(Buffer.concat(chunks).toString("utf8").trim()));
      });
      response.on("error", () => resolveResponse(null));
    });
    req.on("error", () => resolveResponse(null));
    req.end(body);
  });
}

async function main() {
  const event = await readHookInput();
  if (!event) return;
  const configPath = process.env.CARDPUTER_BRIDGE_CONFIG || join(homedir(), ".claude-cardputer-bridge", "config.json");
  const token = storedHookToken(configPath);
  const body = serializeHookRequest(eventName, event);
  if (!token || !body) return;
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), timeoutMs);
  try {
    const output = await postHook(hookSocketPath(configPath), token, body, controller.signal);
    if (output && output !== "{}") process.stdout.write(`${output}\n`);
  } catch {
    // Bridge is optional. Silent fallback keeps Claude's native UI unblocked.
  } finally {
    clearTimeout(timer);
  }
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) main();
