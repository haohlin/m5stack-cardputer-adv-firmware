import { homedir } from "node:os";
import { join } from "node:path";

export const LAUNCH_AGENT_LABEL = "com.haohanlin.orca-cardputer-bridge";
export const CODEX_MCP_NAME = "orca-cardputer-buddy";

export interface BridgePaths {
  root: string;
  run: string;
  socket: string;
  tls: string;
  caKey: string;
  caCert: string;
  serverKey: string;
  serverCert: string;
  secrets: string;
  pairingToken: string;
  localAuthToken: string;
  pairing: string;
  pairingFile: string;
  config: string;
  logs: string;
  stdoutLog: string;
  stderrLog: string;
  launchAgents: string;
  launchAgentPlist: string;
}

export function bridgePaths(home = homedir()): BridgePaths {
  const root = join(home, ".orca-cardputer-bridge");
  const run = join(root, "run");
  const tls = join(root, "tls");
  const secrets = join(root, "secrets");
  const pairing = join(root, "pairing");
  const logs = join(root, "logs");
  return {
    root,
    run,
    socket: join(run, "bridge.sock"),
    tls,
    caKey: join(tls, "ca-key.pem"),
    caCert: join(tls, "ca-cert.pem"),
    serverKey: join(tls, "server-key.pem"),
    serverCert: join(tls, "server-cert.pem"),
    secrets,
    pairingToken: join(secrets, "pairing-token"),
    localAuthToken: join(secrets, "local-auth-token"),
    pairing,
    pairingFile: join(pairing, "device-pairing.json"),
    config: join(root, "config.json"),
    logs,
    stdoutLog: join(logs, "bridge.stdout.log"),
    stderrLog: join(logs, "bridge.stderr.log"),
    launchAgents: join(home, "Library", "LaunchAgents"),
    launchAgentPlist: join(home, "Library", "LaunchAgents", `${LAUNCH_AGENT_LABEL}.plist`)
  };
}
