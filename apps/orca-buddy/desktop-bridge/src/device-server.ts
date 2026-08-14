import { timingSafeEqual } from "node:crypto";
import https from "node:https";
import type { AddressInfo } from "node:net";
import { WebSocket, WebSocketServer } from "ws";
import type { ServerMessage } from "./types.js";

export interface DeviceServerOptions {
  host?: string;
  port?: number;
  cert: string | Buffer;
  key: string | Buffer;
  bearerToken: string;
  maxConnections?: number;
  maxFrameBytes?: number;
  onMessage?: (message: unknown) => void;
  onConnection?: (send: (message: ServerMessage) => boolean) => void | (() => void);
}

export interface DeviceServerHandle {
  host: string;
  port: number;
  close: () => Promise<void>;
}

function equalBearer(header: string | undefined, token: string): boolean {
  if (header === undefined || !header.startsWith("Bearer ")) return false;
  const supplied = Buffer.from(header.slice("Bearer ".length));
  const expected = Buffer.from(token);
  return supplied.length === expected.length && timingSafeEqual(supplied, expected);
}

function rejectUpgrade(socket: NodeJS.WritableStream, status: number, reason: string): void {
  socket.write(`HTTP/1.1 ${status} ${reason}\r\nConnection: close\r\nContent-Length: 0\r\n\r\n`);
  if ("destroy" in socket && typeof socket.destroy === "function") socket.destroy();
}

export async function startDeviceServer(options: DeviceServerOptions): Promise<DeviceServerHandle> {
  const host = options.host ?? "127.0.0.1";
  if (host === "0.0.0.0" || host === "::" || host.length === 0) {
    throw new Error("wildcard device listener addresses are forbidden; choose loopback or an explicit interface address");
  }
  const maximumConnections = options.maxConnections ?? 2;
  const maximumFrameBytes = options.maxFrameBytes ?? 4096;
  let activeConnections = 0;

  const server = https.createServer(
    { cert: options.cert, key: options.key, maxHeaderSize: 8192 },
    (_request, response) => {
      response.writeHead(426, { Connection: "close", "Content-Type": "text/plain" });
      response.end("WebSocket upgrade required\n");
    }
  );
  server.headersTimeout = 5_000;
  server.requestTimeout = 5_000;
  server.keepAliveTimeout = 1_000;

  const webSockets = new WebSocketServer({ noServer: true, maxPayload: maximumFrameBytes, perMessageDeflate: false });
  server.on("upgrade", (request, socket, head) => {
    if (request.url !== "/device") {
      rejectUpgrade(socket, 404, "Not Found");
      return;
    }
    if (!equalBearer(request.headers.authorization, options.bearerToken)) {
      rejectUpgrade(socket, 401, "Unauthorized");
      return;
    }
    if (activeConnections >= maximumConnections) {
      rejectUpgrade(socket, 503, "Service Unavailable");
      return;
    }
    webSockets.handleUpgrade(request, socket, head, (webSocket) => {
      activeConnections += 1;
      webSockets.emit("connection", webSocket, request);
    });
  });

  webSockets.on("connection", (webSocket) => {
    let detached: void | (() => void);
    if (options.onConnection !== undefined) {
      detached = options.onConnection((message) => {
        if (webSocket.readyState !== WebSocket.OPEN) return false;
        const encoded = JSON.stringify(message);
        if (Buffer.byteLength(encoded, "utf8") > maximumFrameBytes) return false;
        webSocket.send(encoded);
        return true;
      });
    }
    webSocket.on("message", (data, isBinary) => {
      if (isBinary) {
        webSocket.close(1003, "text JSON required");
        return;
      }
      try {
        const text = data.toString();
        if (Buffer.byteLength(text, "utf8") > maximumFrameBytes) {
          webSocket.close(1009, "frame too large");
          return;
        }
        options.onMessage?.(JSON.parse(text));
      } catch (error) {
        webSocket.close(error instanceof SyntaxError ? 1007 : 1008, "invalid device message");
      }
    });
    webSocket.on("error", () => {});
    webSocket.once("close", () => {
      activeConnections -= 1;
      detached?.();
    });
  });

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
    server.listen(options.port ?? 17654, host);
  });
  const address = server.address() as AddressInfo;

  return {
    host,
    port: address.port,
    close: async () => {
      for (const client of webSockets.clients) client.close(1001, "server stopping");
      await new Promise<void>((resolve, reject) => webSockets.close((error) => error ? reject(error) : resolve()));
      await new Promise<void>((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
    }
  };
}
