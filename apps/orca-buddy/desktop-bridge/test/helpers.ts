import { chmod, mkdtemp, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { createInterface } from "node:readline";
import type { Readable } from "node:stream";

export async function temporaryDirectory(prefix: string): Promise<string> {
  return mkdtemp(join(tmpdir(), prefix));
}

export async function writeExecutable(path: string, source: string): Promise<void> {
  await writeFile(path, source, { mode: 0o700 });
  await chmod(path, 0o700);
}

export function waitForEvent(target: NodeJS.EventEmitter, event: string): Promise<unknown[]> {
  return new Promise((resolve, reject) => {
    const onError = (error: Error) => {
      cleanup();
      reject(error);
    };
    const onEvent = (...args: unknown[]) => {
      cleanup();
      resolve(args);
    };
    const cleanup = () => {
      target.off("error", onError);
      target.off(event, onEvent);
    };
    target.once("error", onError);
    target.once(event, onEvent);
  });
}

export function jsonLineQueue(stream: Readable): { next: (timeoutMs?: number) => Promise<unknown> } {
  const values: unknown[] = [];
  const waiters: Array<(value: unknown) => void> = [];
  const lines = createInterface({ input: stream, crlfDelay: Infinity });
  lines.on("line", (line) => {
    const value: unknown = JSON.parse(line);
    const waiter = waiters.shift();
    if (waiter === undefined) values.push(value);
    else waiter(value);
  });
  return {
    next: (timeoutMs = 3_000) => new Promise((resolve, reject) => {
      const value = values.shift();
      if (value !== undefined) {
        resolve(value);
        return;
      }
      const timeout = setTimeout(() => {
        const index = waiters.indexOf(onValue);
        if (index !== -1) waiters.splice(index, 1);
        reject(new Error("timed out waiting for JSON line"));
      }, timeoutMs);
      const onValue = (nextValue: unknown) => {
        clearTimeout(timeout);
        resolve(nextValue);
      };
      waiters.push(onValue);
    })
  };
}
