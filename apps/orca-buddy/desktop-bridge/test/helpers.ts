import { chmod, mkdtemp, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";

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
