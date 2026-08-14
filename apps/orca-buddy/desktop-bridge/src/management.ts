import { execFile } from "node:child_process";
import { randomBytes } from "node:crypto";
import { chmod, lstat, mkdir, readFile, rename, rm, unlink, writeFile } from "node:fs/promises";
import { isIP } from "node:net";
import { basename, dirname, relative, sep } from "node:path";
import { promisify } from "node:util";
import { bridgePaths, CODEX_MCP_NAME, LAUNCH_AGENT_LABEL } from "./paths.js";
import { assertExplicitBindAddress } from "./validation.js";

const execFileAsync = promisify(execFile);

export interface CommandOptions {
  home: string;
  uid: number;
  launchctlPath: string;
  bridgeExecutable: string;
  env?: NodeJS.ProcessEnv;
}

export interface InitializeOptions {
  home: string;
  host?: string;
  port?: number;
  opensslPath?: string;
}

export interface InitializeResult {
  pairingFile: string;
  protectedFiles: string[];
  terminalMessage: string;
}

async function protectedDirectory(path: string): Promise<void> {
  await mkdir(path, { recursive: true, mode: 0o700 });
  const info = await lstat(path);
  if (!info.isDirectory() || info.isSymbolicLink()) throw new Error(`refusing unsafe directory path: ${path}`);
  await chmod(path, 0o700);
}

async function preserveExistingDirectory(path: string): Promise<void> {
  let created = false;
  try {
    await lstat(path);
  } catch (error) {
    if ((error as NodeJS.ErrnoException).code !== "ENOENT") throw error;
    await mkdir(path, { recursive: true, mode: 0o700 });
    created = true;
  }
  const info = await lstat(path);
  if (!info.isDirectory() || info.isSymbolicLink()) throw new Error(`refusing unsafe directory path: ${path}`);
  if (created) await chmod(path, 0o700);
}

async function protectedWrite(path: string, data: string | Buffer, protectParent = true): Promise<void> {
  if (protectParent) await protectedDirectory(dirname(path));
  else await preserveExistingDirectory(dirname(path));
  try {
    const existing = await lstat(path);
    if (!existing.isFile() || existing.isSymbolicLink()) throw new Error(`refusing unsafe output path: ${path}`);
  } catch (error) {
    if ((error as NodeJS.ErrnoException).code !== "ENOENT") throw error;
  }
  const temporary = `${path}.tmp-${process.pid}-${randomBytes(8).toString("hex")}`;
  await writeFile(temporary, data, { flag: "wx", mode: 0o600 });
  await chmod(temporary, 0o600);
  await rename(temporary, path);
}

function token(): string {
  return randomBytes(32).toString("base64url");
}

export function redactedHomePath(path: string, home: string): string {
  const child = relative(home, path);
  if (child === "" || (!child.startsWith(`..${sep}`) && child !== "..")) return `$HOME/${child}`;
  return basename(path);
}

export async function initializeBridge(options: InitializeOptions): Promise<InitializeResult> {
  const host = options.host ?? "127.0.0.1";
  const port = options.port ?? 17654;
  assertExplicitBindAddress(host);
  if (!Number.isInteger(port) || port < 1 || port > 65535) throw new Error("port must be 1 to 65535");
  const paths = bridgePaths(options.home);
  try {
    await lstat(paths.pairingFile);
    throw new Error("bridge is already initialized");
  } catch (error) {
    if ((error as NodeJS.ErrnoException).code !== "ENOENT") throw error;
  }
  for (const directory of [paths.root, paths.run, paths.tls, paths.secrets, paths.pairing, paths.logs]) {
    await protectedDirectory(directory);
  }

  const openssl = options.opensslPath ?? "openssl";
  const csr = `${paths.serverCert}.csr`;
  const extensions = `${paths.serverCert}.ext`;
  const serial = `${paths.caCert}.srl`;
  const subjectAlternativeName = isIP(host) ? `IP:${host}` : `DNS:${host}`;
  await protectedWrite(extensions, `subjectAltName=${subjectAlternativeName}\nextendedKeyUsage=serverAuth\n`);
  try {
    await execFileAsync(openssl, ["genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:2048", "-out", paths.caKey], { timeout: 30_000 });
    await execFileAsync(openssl, [
      "req", "-x509", "-new", "-sha256", "-days", "3650", "-key", paths.caKey,
      "-subj", "/CN=Orca Cardputer Buddy Local CA", "-out", paths.caCert
    ], { timeout: 30_000 });
    await execFileAsync(openssl, ["genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:2048", "-out", paths.serverKey], { timeout: 30_000 });
    await execFileAsync(openssl, [
      "req", "-new", "-sha256", "-key", paths.serverKey, "-subj", `/CN=${host}`, "-out", csr
    ], { timeout: 30_000 });
    await execFileAsync(openssl, [
      "x509", "-req", "-sha256", "-days", "825", "-in", csr,
      "-CA", paths.caCert, "-CAkey", paths.caKey, "-CAcreateserial", "-CAserial", serial,
      "-extfile", extensions, "-out", paths.serverCert
    ], { timeout: 30_000 });
  } finally {
    await Promise.all([csr, extensions, serial].map((path) => rm(path, { force: true })));
  }

  const pairingToken = token();
  const localAuthToken = token();
  const ca = await readFile(paths.caCert, "utf8");
  await protectedWrite(paths.pairingToken, `${pairingToken}\n`);
  await protectedWrite(paths.localAuthToken, `${localAuthToken}\n`);
  await protectedWrite(paths.config, `${JSON.stringify({
    version: 1,
    listenAddress: host,
    port,
    socketPath: paths.socket,
    tlsCertificatePath: paths.serverCert,
    tlsPrivateKeyPath: paths.serverKey
  }, null, 2)}\n`);
  await protectedWrite(paths.pairingFile, `${JSON.stringify({
    version: "orca-cardputer/v1",
    endpoint: `wss://${isIP(host) === 6 ? `[${host}]` : host}:${port}/device`,
    token: pairingToken,
    ca
  }, null, 2)}\n`);

  const protectedFiles = [
    paths.caKey,
    paths.caCert,
    paths.serverKey,
    paths.serverCert,
    paths.pairingToken,
    paths.localAuthToken,
    paths.config,
    paths.pairingFile
  ];
  await Promise.all(protectedFiles.map((path) => chmod(path, 0o600)));
  return {
    pairingFile: paths.pairingFile,
    protectedFiles,
    terminalMessage: `Bridge initialized. Protected pairing file: ${redactedHomePath(paths.pairingFile, options.home)}`
  };
}

export async function createProvisioningPayload(options: { home: string; outputPath: string }): Promise<{ terminalMessage: string }> {
  const paths = bridgePaths(options.home);
  const pairing = JSON.parse(await readFile(paths.pairingFile, "utf8")) as unknown;
  if (typeof pairing !== "object" || pairing === null) throw new Error("invalid pairing file");
  const record = pairing as Record<string, unknown>;
  if (typeof record.endpoint !== "string" || typeof record.token !== "string" || typeof record.ca !== "string") {
    throw new Error("incomplete pairing file");
  }
  const payload = `orca-pair ${JSON.stringify({
    version: "orca-cardputer/v1",
    endpoint: record.endpoint,
    bearer: record.token,
    ca: record.ca
  })}\n`;
  try {
    await lstat(options.outputPath);
    throw new Error("provisioning output already exists");
  } catch (error) {
    if ((error as NodeJS.ErrnoException).code !== "ENOENT") throw error;
  }
  await preserveExistingDirectory(dirname(options.outputPath));
  await writeFile(options.outputPath, payload, { flag: "wx", mode: 0o600 });
  await chmod(options.outputPath, 0o600);
  return { terminalMessage: `Protected USB-serial payload: ${redactedHomePath(options.outputPath, options.home)}` };
}

function xml(value: string): string {
  return value.replaceAll("&", "&amp;").replaceAll("<", "&lt;").replaceAll(">", "&gt;").replaceAll('"', "&quot;");
}

async function run(executable: string, args: string[], env?: NodeJS.ProcessEnv): Promise<{ stdout: string; stderr: string }> {
  return execFileAsync(executable, args, { encoding: "utf8", env, timeout: 15_000, shell: false });
}

export async function installLaunchAgent(options: CommandOptions): Promise<{ plistPath: string }> {
  const paths = bridgePaths(options.home);
  await preserveExistingDirectory(paths.launchAgents);
  await protectedDirectory(paths.logs);
  const plist = `<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>Label</key><string>${LAUNCH_AGENT_LABEL}</string>
<key>ProgramArguments</key><array><string>${xml(options.bridgeExecutable)}</string></array>
<key>RunAtLoad</key><true/><key>KeepAlive</key><true/>
<key>StandardOutPath</key><string>${xml(paths.stdoutLog)}</string>
<key>StandardErrorPath</key><string>${xml(paths.stderrLog)}</string>
</dict></plist>
`;
  await protectedWrite(paths.launchAgentPlist, plist, false);
  await run(options.launchctlPath, ["bootstrap", `gui/${options.uid}`, paths.launchAgentPlist], options.env);
  return { plistPath: paths.launchAgentPlist };
}

export async function startLaunchAgent(options: CommandOptions): Promise<void> {
  const paths = bridgePaths(options.home);
  await run(options.launchctlPath, ["bootstrap", `gui/${options.uid}`, paths.launchAgentPlist], options.env);
}

export async function stopLaunchAgent(options: CommandOptions): Promise<void> {
  const paths = bridgePaths(options.home);
  await run(options.launchctlPath, ["bootout", `gui/${options.uid}`, paths.launchAgentPlist], options.env);
}

export async function statusLaunchAgent(options: CommandOptions): Promise<{ output: string }> {
  try {
    await run(options.launchctlPath, ["print", `gui/${options.uid}/${LAUNCH_AGENT_LABEL}`], options.env);
    return { output: "running\n" };
  } catch (error) {
    if (typeof (error as NodeJS.ErrnoException).code === "number") return { output: "stopped\n" };
    throw error;
  }
}

export async function uninstallLaunchAgent(options: CommandOptions): Promise<void> {
  const paths = bridgePaths(options.home);
  await run(options.launchctlPath, ["bootout", `gui/${options.uid}`, paths.launchAgentPlist], options.env);
  await unlink(paths.launchAgentPlist).catch((error: NodeJS.ErrnoException) => {
    if (error.code !== "ENOENT") throw error;
  });
}

export async function installCodexMcp(options: { codexPath?: string; mcpExecutable: string; env?: NodeJS.ProcessEnv }): Promise<void> {
  await run(options.codexPath ?? "codex", ["mcp", "add", CODEX_MCP_NAME, "--", options.mcpExecutable], options.env);
}

export async function removeCodexMcp(options: { codexPath?: string; env?: NodeJS.ProcessEnv }): Promise<void> {
  await run(options.codexPath ?? "codex", ["mcp", "remove", CODEX_MCP_NAME], options.env);
}
