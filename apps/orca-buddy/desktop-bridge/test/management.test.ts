import assert from "node:assert/strict";
import { execFile } from "node:child_process";
import { chmod, mkdir, readFile, stat } from "node:fs/promises";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { promisify } from "node:util";
import test from "node:test";
import {
  createProvisioningPayload,
  initializeBridge,
  installCodexMcp,
  installLaunchAgent,
  removeCodexMcp,
  startLaunchAgent,
  statusLaunchAgent,
  stopLaunchAgent,
  uninstallLaunchAgent
} from "../src/management.js";
import { temporaryDirectory, writeExecutable } from "./helpers.js";

const execFileAsync = promisify(execFile);
const packageDirectory = join(dirname(fileURLToPath(import.meta.url)), "..");

test("init and provisioning keep generated tokens in mode-0600 files and redact terminal output", async () => {
  const home = await temporaryDirectory("orca-home-");
  const initialized = await initializeBridge({ home, host: "127.0.0.1", port: 18765 });
  const pairing = JSON.parse(await readFile(initialized.pairingFile, "utf8")) as { token: string; endpoint: string; ca: string };
  assert.match(pairing.token, /^[A-Za-z0-9_-]{43}$/);
  assert.equal(pairing.endpoint, "wss://127.0.0.1:18765/device");
  assert.match(pairing.ca, /BEGIN CERTIFICATE/);

  for (const path of initialized.protectedFiles) {
    assert.equal((await stat(path)).mode & 0o777, 0o600, path);
  }
  assert.equal((await stat(join(home, ".orca-cardputer-bridge"))).mode & 0o777, 0o700);

  const payloadParent = join(home, "pairing-output");
  await mkdir(payloadParent, { mode: 0o755 });
  await chmod(payloadParent, 0o755);
  const payloadPath = join(payloadParent, "orca-pair.txt");
  const provisioned = await createProvisioningPayload({ home, outputPath: payloadPath });
  const payload = await readFile(payloadPath, "utf8");
  assert.match(payload, /^orca-pair /);
  assert.match(payload, new RegExp(pairing.token));
  assert.equal((await stat(payloadPath)).mode & 0o777, 0o600);
  assert.equal((await stat(payloadParent)).mode & 0o777, 0o755);
  assert.doesNotMatch(initialized.terminalMessage, new RegExp(pairing.token));
  assert.doesNotMatch(provisioned.terminalMessage, new RegExp(pairing.token));
  assert.match(initialized.terminalMessage, /\$HOME\//);
  assert.match(provisioned.terminalMessage, /\$HOME\//);
});

test("LaunchAgent lifecycle uses temporary HOME and exact launchctl argv", async () => {
  const home = await temporaryDirectory("orca-launch-home-");
  const tools = await temporaryDirectory("orca-launch-tools-");
  const argumentLog = join(tools, "arguments.jsonl");
  const launchctl = join(tools, "launchctl");
  await writeExecutable(launchctl, `#!/usr/bin/env node
const fs = require("node:fs");
fs.appendFileSync(process.env.ARGUMENT_LOG, JSON.stringify(process.argv.slice(2)) + "\\n");
if (process.argv[2] === "print") process.stdout.write("state = running\\npath = /private/user/worktree\\ntoken = private-secret\\n");
`);
  const options = {
    home,
    uid: 501,
    launchctlPath: launchctl,
    bridgeExecutable: "/Applications/Orca Buddy/orca-cardputer-bridge",
    env: { ...process.env, ARGUMENT_LOG: argumentLog }
  };

  const launchAgents = join(home, "Library", "LaunchAgents");
  await mkdir(launchAgents, { recursive: true, mode: 0o755 });
  await chmod(launchAgents, 0o755);
  const installed = await installLaunchAgent(options);
  const plist = await readFile(installed.plistPath, "utf8");
  assert.match(plist, /Orca Buddy\/orca-cardputer-bridge/);
  assert.doesNotMatch(plist, /token|bearer|private key/i);
  assert.equal((await stat(installed.plistPath)).mode & 0o777, 0o600);
  assert.equal((await stat(launchAgents)).mode & 0o777, 0o755);
  await stopLaunchAgent(options);
  await startLaunchAgent(options);
  assert.equal((await statusLaunchAgent(options)).output, "running\n");
  await uninstallLaunchAgent(options);

  const commands = (await readFile(argumentLog, "utf8")).trim().split("\n").map((line) => JSON.parse(line));
  assert.deepEqual(commands, [
    ["bootstrap", "gui/501", installed.plistPath],
    ["bootout", "gui/501", installed.plistPath],
    ["bootstrap", "gui/501", installed.plistPath],
    ["print", "gui/501/com.haohanlin.orca-cardputer-bridge"],
    ["bootout", "gui/501", installed.plistPath]
  ]);
  await assert.rejects(() => stat(installed.plistPath));
});

test("initialization rejects normalized unspecified IPv6 before invoking OpenSSL", async () => {
  const home = await temporaryDirectory("orca-wildcard-home-");
  await assert.rejects(
    () => initializeBridge({ home, host: "0:0:0:0:0:0:0:0", opensslPath: "/must/not/run" }),
    /wildcard/i
  );
});

test("built management CLI installs actual dist/bin executables", async () => {
  const home = await temporaryDirectory("orca-built-cli-home-");
  const tools = await temporaryDirectory("orca-built-cli-tools-");
  const argumentLog = join(tools, "arguments.jsonl");
  const launchctl = join(tools, "launchctl");
  const codex = join(tools, "codex");
  const capture = `#!/usr/bin/env node
const fs = require("node:fs");
fs.appendFileSync(process.env.ARGUMENT_LOG, JSON.stringify({ tool: require("node:path").basename(process.argv[1]), args: process.argv.slice(2) }) + "\\n");
`;
  await writeExecutable(launchctl, capture);
  await writeExecutable(codex, capture);
  const cli = join(packageDirectory, "dist", "bin", "orca-cardputer.js");
  const expectedBridge = join(packageDirectory, "dist", "bin", "orca-cardputer-bridge.js");
  const expectedMcp = join(packageDirectory, "dist", "bin", "orca-cardputer-mcp.js");
  const env = {
    ...process.env,
    HOME: home,
    PATH: `${tools}:${process.env.PATH ?? ""}`,
    ARGUMENT_LOG: argumentLog,
    ORCA_CARDPUTER_LAUNCHCTL_PATH: launchctl
  };
  await execFileAsync(process.execPath, [cli, "install"], { env });
  await execFileAsync(process.execPath, [cli, "mcp", "install"], { env });

  const plist = await readFile(join(home, "Library", "LaunchAgents", "com.haohanlin.orca-cardputer-bridge.plist"), "utf8");
  assert.match(plist, new RegExp(expectedBridge.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")));
  const commands = (await readFile(argumentLog, "utf8")).trim().split("\n").map((line) => JSON.parse(line));
  assert.deepEqual(commands, [
    { tool: "launchctl", args: ["bootstrap", `gui/${process.getuid?.()}`, join(home, "Library", "LaunchAgents", "com.haohanlin.orca-cardputer-bridge.plist")] },
    { tool: "codex", args: ["mcp", "add", "orca-cardputer-buddy", "--", expectedMcp] }
  ]);
});

test("Codex MCP management invokes codex mcp with exact safe arguments", async () => {
  const directory = await temporaryDirectory("orca-codex-");
  const argumentLog = join(directory, "arguments.jsonl");
  const codex = join(directory, "codex");
  await writeExecutable(codex, `#!/usr/bin/env node
const fs = require("node:fs");
fs.appendFileSync(process.env.ARGUMENT_LOG, JSON.stringify(process.argv.slice(2)) + "\\n");
`);
  const env = { ...process.env, ARGUMENT_LOG: argumentLog };
  await installCodexMcp({ codexPath: codex, mcpExecutable: "/Applications/Orca Buddy/orca-cardputer-mcp", env });
  await removeCodexMcp({ codexPath: codex, env });
  const commands = (await readFile(argumentLog, "utf8")).trim().split("\n").map((line) => JSON.parse(line));
  assert.deepEqual(commands, [
    ["mcp", "add", "orca-cardputer-buddy", "--", "/Applications/Orca Buddy/orca-cardputer-mcp"],
    ["mcp", "remove", "orca-cardputer-buddy"]
  ]);
});
