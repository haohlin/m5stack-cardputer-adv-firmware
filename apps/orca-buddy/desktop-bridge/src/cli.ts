import { homedir } from "node:os";
import { dirname } from "node:path";
import { fileURLToPath } from "node:url";
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
} from "./management.js";

const usage = `Usage:
  orca-cardputer init [--host ADDRESS] [--port PORT]
  orca-cardputer install | start | stop | status | uninstall
  orca-cardputer provision OUTPUT_FILE
  orca-cardputer mcp install | remove
`;

function option(args: string[], name: string): string | undefined {
  const index = args.indexOf(name);
  if (index === -1) return undefined;
  const value = args[index + 1];
  if (value === undefined || value.startsWith("--")) throw new Error(`${name} requires a value`);
  return value;
}

export async function runManagementCli(args = process.argv.slice(2)): Promise<void> {
  const home = homedir();
  const uid = process.getuid?.();
  if (uid === undefined) throw new Error("per-user LaunchAgent requires a POSIX user");
  const binDirectory = dirname(fileURLToPath(import.meta.url));
  const common = {
    home,
    uid,
    launchctlPath: "/bin/launchctl",
    bridgeExecutable: fileURLToPath(new URL("./orca-cardputer-bridge.js", import.meta.url))
  };
  switch (args[0]) {
    case "init": {
      const host = option(args, "--host");
      const portText = option(args, "--port");
      const allowed = new Set(["init", "--host", host, "--port", portText].filter((value): value is string => value !== undefined));
      if (args.some((value) => !allowed.has(value))) throw new Error(usage);
      const result = await initializeBridge({
        home,
        ...(host === undefined ? {} : { host }),
        ...(portText === undefined ? {} : { port: Number(portText) })
      });
      process.stdout.write(`${result.terminalMessage}\n`);
      return;
    }
    case "install":
      await installLaunchAgent(common);
      process.stdout.write("LaunchAgent installed and loaded.\n");
      return;
    case "start":
      await startLaunchAgent(common);
      process.stdout.write("LaunchAgent started.\n");
      return;
    case "stop":
      await stopLaunchAgent(common);
      process.stdout.write("LaunchAgent stopped.\n");
      return;
    case "status":
      process.stdout.write((await statusLaunchAgent(common)).output);
      return;
    case "uninstall":
      await uninstallLaunchAgent(common);
      process.stdout.write("LaunchAgent unloaded and removed. Protected pairing state retained.\n");
      return;
    case "provision": {
      if (args.length !== 2 || args[1] === undefined) throw new Error(usage);
      const result = await createProvisioningPayload({ home, outputPath: args[1] });
      process.stdout.write(`${result.terminalMessage}\n`);
      return;
    }
    case "mcp":
      if (args[1] === "install" && args.length === 2) {
        await installCodexMcp({ mcpExecutable: `${binDirectory}/orca-cardputer-mcp.js` });
        process.stdout.write("Codex MCP entry installed.\n");
        return;
      }
      if (args[1] === "remove" && args.length === 2) {
        await removeCodexMcp({});
        process.stdout.write("Codex MCP entry removed.\n");
        return;
      }
      throw new Error(usage);
    default:
      throw new Error(usage);
  }
}
