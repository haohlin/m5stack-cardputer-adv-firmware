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

export function selectUniquePrivateIpv4(interfaces) {
  const candidates = new Set();
  for (const records of Object.values(interfaces)) {
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

export function createBridgeRuntime({
  pluginRoot = PLUGIN_ROOT,
  bridgeRoot = BRIDGE_ROOT,
  bridgeCli = BRIDGE_CLI,
  bridgeWsModule = BRIDGE_WS_MODULE,
  pairingSender = PAIRING_SENDER,
  home = homedir(),
  nodePath = process.execPath,
  interfaces = networkInterfaces,
  run = runFixed,
  regularFile = isRegularFile,
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
      const serialEnvironment = { ...process.env };
      delete serialEnvironment.CARDPUTER_ADV_PORT;
      await run("/bin/bash", [pairingSender, output], {
        cwd: pluginRoot,
        env: serialEnvironment,
        timeout: 20_000,
      });
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
  const reportFailure = async (action) => {
    log(`Orca Cardputer ${action} failed`);
    await notify(`${action} failed. Check plugin README; no pairing secret was shown.`);
    throw new Error(`Orca Cardputer ${action} failed. See plugin README.`);
  };

  return {
    register(commands) {
      commands.register(COMMANDS.enable, async () => {
        try {
          await runtime.enable();
          await notify("Bridge enabled. Pair device once over USB, then check Cardputer display.");
        } catch {
          await reportFailure("bridge enable");
        }
      });
      commands.register(COMMANDS.pair, async () => {
        try {
          await runtime.pair();
          await notify("USB pairing saved. Disconnect USB; Cardputer reconnects over Wi-Fi.");
        } catch {
          await reportFailure("device pairing");
        }
      });
      commands.register(COMMANDS.status, async () => {
        try {
          const state = await runtime.status();
          await notify(`Bridge status: ${state}. Device display is live connection status.`);
          return state;
        } catch {
          await reportFailure("bridge status");
        }
      });
      commands.register(COMMANDS.disable, async () => {
        try {
          const state = await runtime.disable();
          await notify(`Bridge status: ${state}. Pairing state is retained.`);
          return state;
        } catch {
          await reportFailure("bridge disable");
        }
      });
    },
  };
}

export default function activate(api) {
  const controller = createPluginController({
    runtime: createBridgeRuntime(),
    notify: (body) => api.host.call("notifications.show", {
      title: "Orca Cardputer Buddy",
      body,
    }),
    log: (message) => api.log(message),
  });
  controller.register(api.commands);
}
