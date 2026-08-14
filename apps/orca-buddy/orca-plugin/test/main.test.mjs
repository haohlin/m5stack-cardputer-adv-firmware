import assert from "node:assert/strict";
import test from "node:test";

import activate, { COMMANDS, createBridgeRuntime, createPluginController, selectUniquePrivateIpv4 } from "../main.mjs";

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

test("activate registers same fixed command set with Orca host", () => {
  const registered = new Map();
  activate({
    commands: { register: (id, handler) => registered.set(id, handler) },
    host: { call: async () => ({ delivered: true }) },
    log: () => {},
  });
  assert.deepEqual([...registered.keys()], Object.values(COMMANDS));
});
