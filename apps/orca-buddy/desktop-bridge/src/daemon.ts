import { homedir } from "node:os";
import { collectOrcaSnapshot } from "./collector.js";
import { startDeviceServer } from "./device-server.js";
import { startIpcServer } from "./ipc.js";
import { loadRuntimeConfiguration } from "./runtime.js";
import { BridgeState } from "./state.js";
import { isRecord } from "./validation.js";

export interface DaemonOptions {
  home?: string;
  orcaPath?: string;
  collectionIntervalMs?: number;
}

export async function runDaemon(options: DaemonOptions = {}): Promise<void> {
  const configuration = await loadRuntimeConfiguration(options.home ?? homedir());
  const state = new BridgeState();
  const collect = async () => {
    try {
      state.setSnapshot(await collectOrcaSnapshot(options.orcaPath === undefined ? {} : { orcaPath: options.orcaPath }));
    } catch {
      state.setSnapshot({ connected: false, worktreeCount: 0, items: [] });
      process.stderr.write("Orca snapshot collection failed\n");
    }
  };
  await collect();

  let device: Awaited<ReturnType<typeof startDeviceServer>> | undefined;
  let ipc: Awaited<ReturnType<typeof startIpcServer>> | undefined;
  try {
    device = await startDeviceServer({
      host: configuration.listenAddress,
      port: configuration.port,
      cert: configuration.certificate,
      key: configuration.privateKey,
      bearerToken: configuration.pairingToken,
      onMessage: (message) => state.handleDeviceMessage(message),
      onConnection: (send) => state.attachDevice(send)
    });
    ipc = await startIpcServer({
      socketPath: configuration.socketPath,
      authToken: configuration.localAuthToken,
      handle: async (method, params) => {
        const args = isRecord(params) ? params : {};
        switch (method) {
          case "status":
            return state.publicStatus();
          case "notify":
            return state.notify(args.text);
          case "ask":
            return state.ask(args.question, args.labels);
          case "getPrompt":
            return state.getPrompt();
          default:
            throw new Error("unsupported daemon method");
        }
      }
    });
  } catch (error) {
    if (ipc !== undefined) await ipc.close().catch(() => {});
    if (device !== undefined) await device.close().catch(() => {});
    throw error;
  }
  const interval = setInterval(() => void collect(), options.collectionIntervalMs ?? 2_000);

  let stop: (() => void) | undefined;
  try {
    await new Promise<void>((resolve) => {
      stop = resolve;
      process.once("SIGINT", stop);
      process.once("SIGTERM", stop);
    });
  } finally {
    clearInterval(interval);
    if (stop !== undefined) {
      process.off("SIGINT", stop);
      process.off("SIGTERM", stop);
    }
    const results = await Promise.allSettled([ipc.close(), device.close()]);
    const failure = results.find((result): result is PromiseRejectedResult => result.status === "rejected");
    if (failure !== undefined) throw failure.reason;
  }
}
