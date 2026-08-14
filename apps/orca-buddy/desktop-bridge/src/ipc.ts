import { timingSafeEqual } from "node:crypto";
import { chmod, lstat, mkdir, unlink } from "node:fs/promises";
import net from "node:net";
import { dirname } from "node:path";
import { isRecord } from "./validation.js";

const PROTOCOL_VERSION = "orca-cardputer/local/v1";
const DEFAULT_MAX_REQUEST_BYTES = 32 * 1024;

export interface IpcServerOptions {
  socketPath: string;
  authToken: string;
  maxRequestBytes?: number;
  handle: (method: string, params: unknown) => Promise<unknown>;
}

export interface IpcClientOptions {
  socketPath: string;
  authToken: string;
  method: string;
  params?: unknown;
  maxResponseBytes?: number;
}

export interface IpcServerHandle {
  close: () => Promise<void>;
}

function equalSecret(left: unknown, right: string): boolean {
  if (typeof left !== "string") return false;
  const leftBytes = Buffer.from(left);
  const rightBytes = Buffer.from(right);
  return leftBytes.length === rightBytes.length && timingSafeEqual(leftBytes, rightBytes);
}

async function removeStaleSocket(path: string): Promise<void> {
  try {
    const existing = await lstat(path);
    if (!existing.isSocket()) throw new Error(`refusing to replace non-socket path: ${path}`);
    if (existing.uid !== process.getuid?.()) throw new Error(`refusing to replace socket not owned by current user: ${path}`);
    await unlink(path);
  } catch (error) {
    if ((error as NodeJS.ErrnoException).code !== "ENOENT") throw error;
  }
}

function writeResponse(socket: net.Socket, body: unknown): void {
  socket.end(`${JSON.stringify(body)}\n`);
}

export async function startIpcServer(options: IpcServerOptions): Promise<IpcServerHandle> {
  const maximum = options.maxRequestBytes ?? DEFAULT_MAX_REQUEST_BYTES;
  await mkdir(dirname(options.socketPath), { recursive: true, mode: 0o700 });
  await chmod(dirname(options.socketPath), 0o700);
  await removeStaleSocket(options.socketPath);

  const server = net.createServer((socket) => {
    socket.setTimeout(5_000, () => socket.destroy());
    let buffer = Buffer.alloc(0);
    let handled = false;
    socket.on("data", (chunk: Buffer) => {
      if (handled) return;
      buffer = Buffer.concat([buffer, chunk]);
      if (buffer.length > maximum) {
        handled = true;
        writeResponse(socket, { ok: false, error: "request too large" });
        return;
      }
      const newline = buffer.indexOf(0x0a);
      if (newline === -1) return;
      handled = true;
      socket.setTimeout(125_000, () => socket.destroy());
      void dispatch(buffer.subarray(0, newline).toString("utf8"), socket);
    });
  });

  const dispatch = async (line: string, socket: net.Socket): Promise<void> => {
    try {
      const request: unknown = JSON.parse(line);
      if (!isRecord(request) || request.version !== PROTOCOL_VERSION || typeof request.method !== "string") {
        throw new Error("malformed request");
      }
      if (!equalSecret(request.authToken, options.authToken)) throw new Error("unauthorized");
      const result = await options.handle(request.method, request.params);
      writeResponse(socket, { ok: true, result });
    } catch (error) {
      writeResponse(socket, { ok: false, error: error instanceof Error ? error.message : "request failed" });
    }
  };

  await new Promise<void>((resolve, reject) => {
    const onError = (error: Error) => {
      server.off("listening", onListening);
      reject(error);
    };
    const onListening = () => {
      server.off("error", onError);
      resolve();
    };
    server.once("error", onError);
    server.once("listening", onListening);
    server.listen(options.socketPath);
  });
  await chmod(options.socketPath, 0o600);

  return {
    close: async () => {
      await new Promise<void>((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
      await unlink(options.socketPath).catch((error: NodeJS.ErrnoException) => {
        if (error.code !== "ENOENT") throw error;
      });
    }
  };
}

export async function requestDaemon(options: IpcClientOptions): Promise<unknown> {
  const maximum = options.maxResponseBytes ?? DEFAULT_MAX_REQUEST_BYTES;
  return new Promise((resolve, reject) => {
    const socket = net.createConnection(options.socketPath);
    let buffer = Buffer.alloc(0);
    let settled = false;
    const finishError = (error: Error) => {
      if (settled) return;
      settled = true;
      socket.destroy();
      reject(error);
    };
    socket.setTimeout(125_000, () => finishError(new Error("daemon request timed out")));
    socket.once("connect", () => {
      const request = {
        version: PROTOCOL_VERSION,
        authToken: options.authToken,
        method: options.method,
        params: options.params ?? {}
      };
      const encoded = Buffer.from(`${JSON.stringify(request)}\n`);
      if (encoded.length > maximum) {
        finishError(new Error("request too large"));
        return;
      }
      socket.write(encoded);
    });
    socket.on("data", (chunk: Buffer) => {
      buffer = Buffer.concat([buffer, chunk]);
      if (buffer.length > maximum) {
        finishError(new Error("daemon response too large"));
        return;
      }
      const newline = buffer.indexOf(0x0a);
      if (newline === -1 || settled) return;
      settled = true;
      socket.end();
      try {
        const response: unknown = JSON.parse(buffer.subarray(0, newline).toString("utf8"));
        if (!isRecord(response) || response.ok !== true) {
          const message = isRecord(response) && typeof response.error === "string" ? response.error : "invalid daemon response";
          reject(new Error(message));
          return;
        }
        resolve(response.result);
      } catch (error) {
        reject(error);
      }
    });
    socket.once("error", finishError);
  });
}
