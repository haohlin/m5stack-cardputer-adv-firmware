import { execFile } from "node:child_process";
import { randomUUID } from "node:crypto";
import { lstat } from "node:fs/promises";
import { homedir, networkInterfaces } from "node:os";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);
const PLUGIN_ROOT = dirname(fileURLToPath(import.meta.url));
const BRIDGE_ROOT = resolve(PLUGIN_ROOT, "..", "desktop-bridge");
const BRIDGE_CLI = join(BRIDGE_ROOT, "dist", "bin", "orca-cardputer.js");
const BRIDGE_WS_MODULE = join(BRIDGE_ROOT, "node_modules", "ws", "index.js");
const PAIRING_SENDER = resolve(PLUGIN_ROOT, "..", "scripts", "write_pairing_serial.sh");
const BRIDGE_PORT = "17654";

const FAILURE_MESSAGES = Object.freeze({
  "pair-storage": "device rejected pairing because its saved configuration could not be updated",
  "pair-no-ack": "device did not acknowledge USB pairing within 8 seconds",
  "pair-rejected": "device rejected USB pairing",
  "pair-usb": "Cardputer USB serial connection is unavailable",
  "pair-sender": "local USB pairing sender failed",
});

export const COMMANDS = Object.freeze({
  enable: "enable-bridge",
  pair: "pair-connected-device",
  status: "bridge-status",
  disable: "disable-bridge",
});

function isPrivateIpv4(address) {
  const octets = address.split(".").map((part) => Number(part));
  if (octets.length !== 4 || octets.some((part) => !Number.isInteger(part) || part < 0 || part > 255)) return false;
  return octets[0] === 10 ||
    (octets[0] === 172 && octets[1] >= 16 && octets[1] <= 31) ||
    (octets[0] === 192 && octets[1] === 168);
}

function isTunnelInterface(name) {
  return /^(utun|tun|tap|ppp|ipsec|wg|tailscale)/i.test(name);
}

export function selectUniquePrivateIpv4(interfaces) {
  const candidates = new Set();
  for (const [name, records] of Object.entries(interfaces)) {
    if (isTunnelInterface(name)) continue;
    for (const record of records ?? []) {
      if (String(record.family) === "IPv4" && !record.internal && isPrivateIpv4(record.address)) {
        candidates.add(record.address);
      }
    }
  }
  if (candidates.size !== 1) throw new Error("exactly one private LAN IPv4 address is required");
  return [...candidates][0];
}

async function isRegularFile(path) {
  try {
    const info = await lstat(path);
    return info.isFile() && !info.isSymbolicLink();
  } catch (error) {
    if (error?.code === "ENOENT") return false;
    throw error;
  }
}

function bridgePaths(home) {
  const root = join(home, ".orca-cardputer-bridge");
  return {
    config: join(root, "config.json"),
    launchAgent: join(home, "Library", "LaunchAgents", "com.haohanlin.orca-cardputer-bridge.plist"),
    exportDirectory: join(root, "export"),
  };
}

async function runFixed(executable, args, options) {
  try {
    return await execFileAsync(executable, args, {
      cwd: options.cwd,
      env: options.env,
      encoding: "utf8",
      maxBuffer: 64 * 1024,
      shell: false,
      timeout: options.timeout,
      windowsHide: true,
    });
  } catch {
    throw new Error("local bridge command failed");
  }
}

function cardputerFailure(code) {
  const error = new Error(FAILURE_MESSAGES[code] ?? "local bridge command failed");
  error.cardputerFailure = code;
  return error;
}

function classifyPairingSenderFailure(error) {
  const output = `${error?.stderr ?? ""}\n${error?.message ?? ""}`;
  if (output.includes("ERR pairing persist failed; active config unchanged")) {
    return cardputerFailure("pair-storage");
  }
  if (output.includes("device did not acknowledge pairing within 8 seconds")) {
    return cardputerFailure("pair-no-ack");
  }
  if (output.includes("device rejected pairing:")) return cardputerFailure("pair-rejected");
  if (output.includes("no unique Cardputer USB serial port detected") ||
      output.includes("could not open port") || output.includes("Device not configured")) {
    return cardputerFailure("pair-usb");
  }
  return cardputerFailure("pair-sender");
}

function safeFailureMessage(error) {
  return FAILURE_MESSAGES[error?.cardputerFailure] ?? "local bridge command failed";
}

export function createBridgeRuntime({
  pluginRoot = PLUGIN_ROOT,
  bridgeRoot = BRIDGE_ROOT,
  bridgeCli = BRIDGE_CLI,
  bridgeWsModule = BRIDGE_WS_MODULE,
  pairingSender = PAIRING_SENDER,
  home = homedir(),
  nodePath = "node",
  interfaces = networkInterfaces,
  run = runFixed,
  regularFile = isRegularFile,
  progress = () => {},
} = {}) {
  const paths = bridgePaths(home);
  const runNode = (args, timeout = 30_000) => run(nodePath, [bridgeCli, ...args], {
    cwd: bridgeRoot,
    env: process.env,
    timeout,
  });

  async function ensureBridgeBuild() {
    if ((await regularFile(bridgeCli)) && (await regularFile(bridgeWsModule))) return;
    await run("npm", ["ci"], { cwd: bridgeRoot, env: process.env, timeout: 120_000 });
    await run("npm", ["run", "build"], { cwd: bridgeRoot, env: process.env, timeout: 120_000 });
    if (!(await regularFile(bridgeCli)) || !(await regularFile(bridgeWsModule))) {
      throw new Error("local bridge build missing");
    }
  }

  async function bridgeStatus() {
    if (!(await regularFile(paths.config))) return "not configured";
    await ensureBridgeBuild();
    const { stdout } = await runNode(["status"]);
    const state = stdout.trim();
    if (state === "running" || state === "stopped") return state;
    throw new Error("local bridge returned an invalid status");
  }

  return {
    async enable() {
      await ensureBridgeBuild();
      let state = await bridgeStatus();
      if (state === "running") return state;
      if (state === "not configured") {
        const host = selectUniquePrivateIpv4(interfaces());
        await runNode(["init", "--host", host, "--port", BRIDGE_PORT], 90_000);
      }
      if (await regularFile(paths.launchAgent)) await runNode(["start"]);
      else await runNode(["install"]);
      state = await bridgeStatus();
      if (state !== "running") throw new Error("local bridge did not start");
      return state;
    },

    async pair() {
      await ensureBridgeBuild();
      if (!(await regularFile(paths.config))) throw new Error("bridge is not configured");
      const output = join(paths.exportDirectory, `orca-pair-${Date.now()}-${randomUUID()}.txt`);
      await runNode(["provision", output]);
      progress("pair: protected payload created");
      const serialEnvironment = { ...process.env };
      delete serialEnvironment.CARDPUTER_ADV_PORT;
      progress("pair: sending protected USB payload");
      try {
        await run("/bin/bash", [pairingSender, output], {
          cwd: pluginRoot,
          env: serialEnvironment,
          timeout: 20_000,
        });
      } catch (error) {
        throw classifyPairingSenderFailure(error);
      }
      progress("pair: device acknowledged pairing");
    },

    async status() {
      return bridgeStatus();
    },

    async disable() {
      const state = await bridgeStatus();
      if (state !== "running") return state;
      await runNode(["stop"]);
      return bridgeStatus();
    },
  };
}

export function createPluginController({ runtime, notify, log }) {
  const reportFailure = async (action, error) => {
    const reason = safeFailureMessage(error);
    log(`Orca Cardputer ${action} failed: ${reason}`);
    await notify(`${action} failed: ${reason}. Check plugin README; no pairing secret was shown.`);
    return { ok: false };
  };

  return {
    register(commands) {
      commands.register(COMMANDS.enable, async () => {
        try {
          await runtime.enable();
          await notify("Bridge enabled. Pair device once over USB, then check Cardputer display.");
        } catch (error) {
          return reportFailure("bridge enable", error);
        }
      });
      commands.register(COMMANDS.pair, async () => {
        try {
          await runtime.pair();
          await notify("USB pairing saved. Disconnect USB; Cardputer reconnects over Wi-Fi.");
        } catch (error) {
          return reportFailure("device pairing", error);
        }
      });
      commands.register(COMMANDS.status, async () => {
        try {
          const state = await runtime.status();
          await notify(`Bridge status: ${state}. Device display is live connection status.`);
          return state;
        } catch (error) {
          return reportFailure("bridge status", error);
        }
      });
      commands.register(COMMANDS.disable, async () => {
        try {
          const state = await runtime.disable();
          await notify(`Bridge status: ${state}. Pairing state is retained.`);
          return state;
        } catch (error) {
          return reportFailure("bridge disable", error);
        }
      });
    },
  };
}

export default function activate(api) {
  const controller = createPluginController({
    runtime: createBridgeRuntime({ progress: (message) => api.log(`Orca Cardputer ${message}`) }),
    notify: (body) => api.host.call("notifications.show", {
      title: "Orca Cardputer Buddy",
      body,
    }),
    log: (message) => api.log(message),
  });
  controller.register(api.commands);
}
