import { lstat, readFile } from "node:fs/promises";
import { homedir } from "node:os";
import { bridgePaths } from "./paths.js";
import { isRecord } from "./validation.js";

export interface RuntimeConfiguration {
  home: string;
  listenAddress: string;
  port: number;
  socketPath: string;
  tlsCertificatePath: string;
  tlsPrivateKeyPath: string;
  localAuthToken: string;
  pairingToken: string;
  certificate: Buffer;
  privateKey: Buffer;
}

export async function readProtectedFile(path: string): Promise<Buffer> {
  const info = await lstat(path);
  if (!info.isFile() || info.isSymbolicLink()) throw new Error(`protected path is not a regular file: ${path}`);
  if ((info.mode & 0o077) !== 0) throw new Error(`protected file permissions are too broad: ${path}`);
  if (process.getuid !== undefined && info.uid !== process.getuid()) throw new Error(`protected file has wrong owner: ${path}`);
  return readFile(path);
}

export async function loadRuntimeConfiguration(home = homedir()): Promise<RuntimeConfiguration> {
  const paths = bridgePaths(home);
  const configValue: unknown = JSON.parse((await readProtectedFile(paths.config)).toString("utf8"));
  if (!isRecord(configValue) || configValue.version !== 1) throw new Error("unsupported bridge configuration");
  const listenAddress = configValue.listenAddress;
  const port = configValue.port;
  const socketPath = configValue.socketPath;
  const tlsCertificatePath = configValue.tlsCertificatePath;
  const tlsPrivateKeyPath = configValue.tlsPrivateKeyPath;
  if (
    typeof listenAddress !== "string" || listenAddress.length === 0 || listenAddress === "0.0.0.0" || listenAddress === "::" ||
    typeof port !== "number" || !Number.isInteger(port) || port < 1 || port > 65535 ||
    typeof socketPath !== "string" || socketPath !== paths.socket ||
    typeof tlsCertificatePath !== "string" || tlsCertificatePath !== paths.serverCert ||
    typeof tlsPrivateKeyPath !== "string" || tlsPrivateKeyPath !== paths.serverKey
  ) {
    throw new Error("invalid bridge configuration");
  }
  const [localAuthToken, pairingToken, certificate, privateKey] = await Promise.all([
    readProtectedFile(paths.localAuthToken),
    readProtectedFile(paths.pairingToken),
    readProtectedFile(paths.serverCert),
    readProtectedFile(paths.serverKey)
  ]);
  const localToken = localAuthToken.toString("utf8").trim();
  const deviceToken = pairingToken.toString("utf8").trim();
  if (!/^[A-Za-z0-9_-]{43}$/.test(localToken) || !/^[A-Za-z0-9_-]{43}$/.test(deviceToken)) {
    throw new Error("invalid bridge credential file");
  }
  return {
    home,
    listenAddress,
    port,
    socketPath,
    tlsCertificatePath,
    tlsPrivateKeyPath,
    localAuthToken: localToken,
    pairingToken: deviceToken,
    certificate,
    privateKey
  };
}
