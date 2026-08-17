import assert from "node:assert/strict";
import { readFile, stat } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import test from "node:test";
import { mkdtemp } from "node:fs/promises";

import activate, { COMMANDS, createBridgeRuntime, createPluginController, runFixed, selectUniquePrivateIpv4 } from "../main.mjs";

test("selectUniquePrivateIpv4 accepts exactly one LAN address", () => {
  assert.equal(selectUniquePrivateIpv4({
    lo0: [{ family: "IPv4", address: "127.0.0.1", internal: true }],
    en0: [{ family: "IPv4", address: "192.168.1.50", internal: false }],
  }), "192.168.1.50");
});

test("selectUniquePrivateIpv4 fails closed for ambiguous or public addresses", () => {
  assert.throws(() => selectUniquePrivateIpv4({
    en0: [{ family: "IPv4", address: "192.168.1.50", internal: false }],
    en1: [{ family: "IPv4", address: "10.0.0.5", internal: false }],
  }), /exactly one private LAN IPv4/);
  assert.throws(() => selectUniquePrivateIpv4({
    en0: [{ family: "IPv4", address: "8.8.8.8", internal: false }],
  }), /exactly one private LAN IPv4/);
});

test("selectUniquePrivateIpv4 ignores macOS tunnel interfaces", () => {
  assert.equal(selectUniquePrivateIpv4({
    en0: [{ family: "IPv4", address: "192.168.31.209", internal: false }],
    utun8: [{ family: "IPv4", address: "172.29.230.152", internal: false }],
  }), "192.168.31.209");
});

test("fixed runner retains device rejection text for secret-free pairing classification", async () => {
  await assert.rejects(
    () => runFixed(process.execPath, ["-e", "process.stderr.write('device rejected pairing: ERR pairing persist failed; active config unchanged'); process.exit(1)"], {
      cwd: process.cwd(),
      env: process.env,
      timeout: 5_000,
    }),
    (error) => error?.message === "local bridge command failed" &&
      String(error?.stderr).includes("ERR pairing persist failed; active config unchanged"),
  );
});

test("bridge runtime uses fixed local commands and a single private LAN address", async () => {
  const bridgeRoot = "/repo/apps/orca-buddy/desktop-bridge";
  const bridgeCli = `${bridgeRoot}/dist/bin/orca-cardputer.js`;
  const bridgeWsModule = `${bridgeRoot}/node_modules/ws/index.js`;
  const home = "/home/tester";
  const config = `${home}/.orca-cardputer-bridge/config.json`;
  const launchAgent = `${home}/Library/LaunchAgents/com.haohanlin.orca-cardputer-bridge.plist`;
  let built = false;
  let dependencies = false;
  let configured = false;
  let installed = false;
  const calls = [];
  const runtime = createBridgeRuntime({
    pluginRoot: "/repo/apps/orca-buddy/orca-plugin",
    bridgeRoot,
    bridgeCli,
    bridgeWsModule,
    home,
    interfaces: () => ({ en0: [{ family: "IPv4", address: "192.168.4.2", internal: false }] }),
    regularFile: async (path) =>
      (path === bridgeCli && built) ||
      (path === bridgeWsModule && dependencies) ||
      (path === config && configured) ||
      (path === launchAgent && installed),
    run: async (executable, args, options) => {
      calls.push({ executable, args, options });
      if (executable === "npm" && args.join(" ") === "ci") dependencies = true;
      if (executable === "npm" && args.join(" ") === "run build") built = true;
      if (executable === "node" && args[1] === "init") configured = true;
      if (executable === "node" && args[1] === "install") installed = true;
      if (executable === "node" && args[1] === "status") return { stdout: "running\n", stderr: "" };
      return { stdout: "", stderr: "" };
    },
  });

  assert.equal(await runtime.enable(), "running");
  assert.deepEqual(
    calls.map(({ executable, args }) => [executable, args]),
    [
      ["npm", ["ci"]],
      ["npm", ["run", "build"]],
      ["node", [bridgeCli, "init", "--host", "192.168.4.2", "--port", "17654"]],
      ["node", [bridgeCli, "install"]],
      ["node", [bridgeCli, "status"]],
    ],
  );
  assert.equal(calls.every(({ options }) => options.cwd === bridgeRoot), true);
});

test("bridge runtime explicitly repairs a stale LAN binding and requires new USB pairing", async () => {
  const bridgeRoot = "/repo/apps/orca-buddy/desktop-bridge";
  const bridgeCli = `${bridgeRoot}/dist/bin/orca-cardputer.js`;
  const bridgeWsModule = `${bridgeRoot}/node_modules/ws/index.js`;
  const home = "/home/tester";
  const config = `${home}/.orca-cardputer-bridge/config.json`;
  const launchAgent = `${home}/Library/LaunchAgents/com.haohanlin.orca-cardputer-bridge.plist`;
  const calls = [];
  let statusCalls = 0;
  const runtime = createBridgeRuntime({
    pluginRoot: "/repo/apps/orca-buddy/orca-plugin",
    bridgeRoot,
    bridgeCli,
    bridgeWsModule,
    home,
    interfaces: () => ({ en0: [{ family: "IPv4", address: "172.20.10.6", internal: false }] }),
    regularFile: async (path) => path === bridgeCli || path === bridgeWsModule || path === config || path === launchAgent,
    readConfig: async () => JSON.stringify({ listenAddress: "192.168.31.209" }),
    run: async (executable, args, options) => {
      calls.push({ executable, args, options });
      if (executable === "node" && args[1] === "status") {
        statusCalls += 1;
        return { stdout: statusCalls === 1 ? "failed\n" : "running\n", stderr: "" };
      }
      return { stdout: "", stderr: "" };
    },
  });

  assert.equal(await runtime.enable(), "running");
  assert.deepEqual(calls.map(({ executable, args }) => [executable, args]), [
    ["node", [bridgeCli, "status"]],
    ["node", [bridgeCli, "stop"]],
    ["node", [bridgeCli, "init", "--host", "172.20.10.6", "--port", "17654", "--replace"]],
    ["node", [bridgeCli, "install"]],
    ["node", [bridgeCli, "status"]],
  ]);
});

test("controller registers fixed commands and never exposes pairing material", async () => {
  const calls = [];
  const notices = [];
  const registered = new Map();
  const controller = createPluginController({
    runtime: {
      enable: async () => calls.push("enable"),
      pair: async () => calls.push("pair"),
      status: async () => {
        calls.push("status");
        return "running";
      },
      disable: async () => calls.push("disable"),
    },
    notify: async (body) => notices.push(body),
    log: () => {},
  });

  controller.register({ register: (id, handler) => registered.set(id, handler) });
  assert.deepEqual([...registered.keys()], Object.values(COMMANDS));

  await registered.get(COMMANDS.enable)();
  await registered.get(COMMANDS.pair)();
  await registered.get(COMMANDS.status)();
  await registered.get(COMMANDS.disable)();

  assert.deepEqual(calls, ["enable", "pair", "status", "disable"]);
  assert.equal(notices.join(" ").toLowerCase().includes("token"), false);
  assert.equal(notices.join(" ").toLowerCase().includes("bearer"), false);
});

test("controller reports a safe failure without causing Orca command failure", async () => {
  const notices = [];
  const registered = new Map();
  const controller = createPluginController({
    runtime: {
      enable: async () => { throw new Error("private bearer must never appear"); },
      pair: async () => {},
      status: async () => "stopped",
      disable: async () => "stopped",
    },
    notify: async (body) => notices.push(body),
    log: () => {},
  });

  controller.register({ register: (id, handler) => registered.set(id, handler) });
  await assert.doesNotReject(registered.get(COMMANDS.enable)());
  assert.match(notices[0], /bridge enable failed/i);
  assert.doesNotMatch(notices[0], /bearer|private/i);
});

test("controller reports a specific secret-free device storage failure", async () => {
  const notices = [];
  const logs = [];
  const registered = new Map();
  const failure = new Error("private bearer must never appear");
  failure.cardputerFailure = "pair-storage";
  const controller = createPluginController({
    runtime: {
      enable: async () => {},
      pair: async () => { throw failure; },
      status: async () => "stopped",
      disable: async () => "stopped",
    },
    notify: async (body) => notices.push(body),
    log: (message) => logs.push(message),
  });

  controller.register({ register: (id, handler) => registered.set(id, handler) });
  await assert.doesNotReject(registered.get(COMMANDS.pair)());

  assert.match(notices[0], /saved configuration could not be updated/i);
  assert.match(logs[0], /saved configuration could not be updated/i);
  assert.doesNotMatch(`${notices.join(" ")} ${logs.join(" ")}`, /bearer|private/i);
});

test("controller shows a fixed secret-free USB device diagnostic", async () => {
  const notices = [];
  const registered = new Map();
  const controller = createPluginController({
    runtime: {
      enable: async () => {},
      pair: async () => {},
      status: async () => "running",
      deviceStatus: async () =>
        "version=0.1.6 saved_wifi=yes wifi=connected saved_pairing=yes bridge=connected pairing_store=nvs",
      disable: async () => {},
    },
    notify: async (body) => notices.push(body),
    log: () => {},
  });

  controller.register({ register: (id, handler) => registered.set(id, handler) });
  await registered.get(COMMANDS.diagnostics)();

  assert.match(notices[0], /saved_wifi=yes.*bridge=connected/);
  assert.doesNotMatch(notices[0], /ssid|bearer|token|certificate|private/i);
});

test("device diagnostics runs only its fixed helper and validates its output", async () => {
  const calls = [];
  const reader = "/repo/apps/orca-buddy/scripts/read_device_status.sh";
  const runtime = createBridgeRuntime({
    pluginRoot: "/repo/apps/orca-buddy/orca-plugin",
    statusReader: reader,
    home: "/home/tester",
    run: async (executable, args, options) => {
      calls.push({ executable, args, options });
      return {
        stdout: "version=0.1.6 saved_wifi=yes wifi=connected saved_pairing=yes bridge=connected pairing_store=nvs\n",
        stderr: "",
      };
    },
  });

  assert.equal(
    await runtime.deviceStatus(),
    "version=0.1.6 saved_wifi=yes wifi=connected saved_pairing=yes bridge=connected pairing_store=nvs",
  );
  assert.deepEqual(calls.map(({ executable, args }) => [executable, args]), [
    ["/bin/bash", [reader]],
  ]);
  assert.equal("CARDPUTER_ADV_PORT" in calls[0].options.env, false);

  const invalid = createBridgeRuntime({
    pluginRoot: "/repo/apps/orca-buddy/orca-plugin",
    statusReader: reader,
    home: "/home/tester",
    run: async () => ({ stdout: "bearer=must-not-display\n", stderr: "" }),
  });
  await assert.rejects(
    () => invalid.deviceStatus(),
    (error) => error?.cardputerFailure === "status-invalid" && !/bearer|token|private/i.test(error.message),
  );
});

test("pair maps sender storage rejection to a safe failure code", async () => {
  const bridgeRoot = "/repo/apps/orca-buddy/desktop-bridge";
  const bridgeCli = `${bridgeRoot}/dist/bin/orca-cardputer.js`;
  const bridgeWsModule = `${bridgeRoot}/node_modules/ws/index.js`;
  const home = "/home/tester";
  const config = `${home}/.orca-cardputer-bridge/config.json`;
  const senderFailure = Object.assign(new Error("private bearer must never appear"), {
    stderr: "pairing: device rejected pairing: ERR pairing persist failed; active config unchanged",
  });
  const runtime = createBridgeRuntime({
    pluginRoot: "/repo/apps/orca-buddy/orca-plugin",
    bridgeRoot,
    bridgeCli,
    bridgeWsModule,
    pairingSender: "/repo/apps/orca-buddy/scripts/write_pairing_serial.sh",
    home,
    regularFile: async (path) => path === bridgeCli || path === bridgeWsModule || path === config,
    run: async (executable, args) => {
      if (executable === "node" && args[1] === "provision") return { stdout: "", stderr: "" };
      if (executable === "/bin/bash") throw senderFailure;
      throw new Error("unexpected command");
    },
  });

  await assert.rejects(
    () => runtime.pair(),
    (error) => error?.cardputerFailure === "pair-storage" && !/bearer|private/i.test(error.message),
  );
});

test("pair maps repeated USB CDC reset to a specific safe failure code", async () => {
  const bridgeRoot = "/repo/apps/orca-buddy/desktop-bridge";
  const bridgeCli = `${bridgeRoot}/dist/bin/orca-cardputer.js`;
  const bridgeWsModule = `${bridgeRoot}/node_modules/ws/index.js`;
  const home = "/home/tester";
  const config = `${home}/.orca-cardputer-bridge/config.json`;
  const runtime = createBridgeRuntime({
    pluginRoot: "/repo/apps/orca-buddy/orca-plugin",
    bridgeRoot,
    bridgeCli,
    bridgeWsModule,
    pairingSender: "/repo/apps/orca-buddy/scripts/write_pairing_serial.sh",
    home,
    regularFile: async (path) => path === bridgeCli || path === bridgeWsModule || path === config,
    run: async (executable, args) => {
      if (executable === "node" && args[1] === "provision") return { stdout: "", stderr: "" };
      if (executable === "/bin/bash") {
        throw Object.assign(new Error("sender failed"), {
          stderr: "pairing: USB serial device reset during pairing and did not reconnect",
        });
      }
      throw new Error("unexpected command");
    },
  });

  await assert.rejects(
    () => runtime.pair(),
    (error) => error?.cardputerFailure === "pair-usb-reset" && !/pairing secret|bearer|token/i.test(error.message),
  );
});

test("pair emits secret-free progress at each host-device boundary", async () => {
  const bridgeRoot = "/repo/apps/orca-buddy/desktop-bridge";
  const bridgeCli = `${bridgeRoot}/dist/bin/orca-cardputer.js`;
  const bridgeWsModule = `${bridgeRoot}/node_modules/ws/index.js`;
  const home = "/home/tester";
  const config = `${home}/.orca-cardputer-bridge/config.json`;
  const progress = [];
  const runtime = createBridgeRuntime({
    pluginRoot: "/repo/apps/orca-buddy/orca-plugin",
    bridgeRoot,
    bridgeCli,
    bridgeWsModule,
    pairingSender: "/repo/apps/orca-buddy/scripts/write_pairing_serial.sh",
    home,
    progress: (message) => progress.push(message),
    regularFile: async (path) => path === bridgeCli || path === bridgeWsModule || path === config,
    run: async (executable, args) => {
      if (executable === "node" && args[1] === "provision") return { stdout: "", stderr: "" };
      if (executable === "/bin/bash") return { stdout: "Pairing saved by device.\n", stderr: "" };
      throw new Error("unexpected command");
    },
  });

  await runtime.pair();

  assert.deepEqual(progress, [
    "pair: protected payload created",
    "pair: sending protected USB payload",
    "pair: device acknowledged pairing",
  ]);
  assert.doesNotMatch(progress.join(" "), /bearer|token|certificate|private/i);
});

test("pair stores only safe USB-unavailable diagnostics for later debugging", async () => {
  const bridgeRoot = "/repo/apps/orca-buddy/desktop-bridge";
  const bridgeCli = `${bridgeRoot}/dist/bin/orca-cardputer.js`;
  const bridgeWsModule = `${bridgeRoot}/node_modules/ws/index.js`;
  const home = await mkdtemp(join(tmpdir(), "orca-cardputer-plugin-"));
  const config = `${home}/.orca-cardputer-bridge/config.json`;
  const runtime = createBridgeRuntime({
    pluginRoot: "/repo/apps/orca-buddy/orca-plugin",
    bridgeRoot,
    bridgeCli,
    bridgeWsModule,
    pairingSender: "/repo/apps/orca-buddy/scripts/write_pairing_serial.sh",
    home,
    regularFile: async (path) => path === bridgeCli || path === bridgeWsModule || path === config,
    run: async (executable, args) => {
      if (executable === "node" && args[1] === "provision") return { stdout: "", stderr: "" };
      if (executable === "/bin/bash") {
        throw Object.assign(new Error("sender failed"), {
          stderr: "Cardputer USB serial port not detected.",
        });
      }
      throw new Error("unexpected command");
    },
  });

  await assert.rejects(() => runtime.pair(), (error) => error?.cardputerFailure === "pair-usb-none");
  const log = join(home, ".orca-cardputer-bridge", "logs", "plugin-events.jsonl");
  const content = await readFile(log, "utf8");
  assert.match(content, /pair\.payload-created/);
  assert.match(content, /pair\.usb-transfer-begin/);
  assert.match(content, /pair\.failed\.usb-none/);
  assert.doesNotMatch(content, /bearer|token|certificate|private/i);
  assert.equal((await stat(log)).mode & 0o777, 0o600);
});

test("activate registers same fixed command set with Orca host", () => {
  const registered = new Map();
  activate({
    commands: { register: (id, handler) => registered.set(id, handler) },
    host: { call: async () => ({ delivered: true }) },
    log: () => {},
  });
  assert.deepEqual([...registered.keys()], Object.values(COMMANDS));
});
