import { execFile } from "node:child_process";
import { promisify } from "node:util";
import type { OrcaSnapshot, OrcaSnapshotItem } from "./types.js";
import { isRecord } from "./validation.js";

const execFileAsync = promisify(execFile);
const MAX_COMMAND_OUTPUT_BYTES = 1024 * 1024;

export interface CollectorOptions {
  orcaPath?: string;
  env?: NodeJS.ProcessEnv;
}

async function runApprovedCommand(
  executable: string,
  args: readonly string[],
  env: NodeJS.ProcessEnv | undefined
): Promise<unknown> {
  const { stdout } = await execFileAsync(executable, [...args], {
    encoding: "utf8",
    env,
    maxBuffer: MAX_COMMAND_OUTPUT_BYTES,
    timeout: 10_000,
    shell: false
  });
  try {
    return JSON.parse(stdout);
  } catch {
    throw new Error(`Orca command ${args.join(" ")} did not return valid JSON`);
  }
}

function readWorktrees(value: unknown): unknown[] {
  if (Array.isArray(value)) return value;
  if (!isRecord(value)) return [];
  if (value.ok === true && isRecord(value.result) && Array.isArray(value.result.worktrees)) {
    return value.result.worktrees;
  }
  for (const key of ["worktrees", "items", "processes"] as const) {
    if (Array.isArray(value[key])) return value[key];
  }
  return [];
}

function boundedMetadata(value: unknown): string | undefined {
  if (typeof value !== "string") return undefined;
  const normalized = value.trim();
  if (normalized.length === 0 || Buffer.byteLength(normalized, "utf8") > 80) return undefined;
  if (/\p{Cc}/u.test(normalized)) return undefined;
  return normalized;
}

function sanitizeWorktree(value: unknown): OrcaSnapshotItem {
  if (!isRecord(value)) return {};
  const item: OrcaSnapshotItem = {};
  const displayName = boundedMetadata(value.displayName);
  const workspaceStatus = boundedMetadata(value.workspaceStatus);
  let agentState = boundedMetadata(value.agentState);
  if (agentState === undefined && Array.isArray(value.agents)) {
    for (const agent of value.agents) {
      if (!isRecord(agent)) continue;
      agentState = boundedMetadata(agent.state);
      if (agentState !== undefined) break;
    }
  }
  if (displayName !== undefined) item.displayName = displayName;
  if (workspaceStatus !== undefined) item.workspaceStatus = workspaceStatus;
  if (agentState !== undefined) item.agentState = agentState;
  return item;
}

function readConnected(value: unknown): boolean {
  if (!isRecord(value)) return false;
  if (typeof value.connected === "boolean") return value.connected;
  if (isRecord(value.orca) && typeof value.orca.connected === "boolean") return value.orca.connected;
  if (
    value.ok === true &&
    isRecord(value.result) &&
    isRecord(value.result.runtime) &&
    typeof value.result.runtime.reachable === "boolean"
  ) {
    return value.result.runtime.reachable;
  }
  return false;
}

export function sanitizeSnapshot(status: unknown, worktreeStatus: unknown): OrcaSnapshot {
  const worktrees = readWorktrees(worktreeStatus);
  const officialTotal = isRecord(worktreeStatus) && worktreeStatus.ok === true && isRecord(worktreeStatus.result)
    ? worktreeStatus.result.totalCount
    : undefined;
  return {
    connected: readConnected(status),
    worktreeCount: typeof officialTotal === "number" && Number.isSafeInteger(officialTotal) && officialTotal >= worktrees.length
      ? officialTotal
      : worktrees.length,
    items: worktrees.slice(0, 12).map(sanitizeWorktree)
  };
}

export async function collectOrcaSnapshot(options: CollectorOptions = {}): Promise<OrcaSnapshot> {
  const executable = options.orcaPath ?? "orca";
  const status = await runApprovedCommand(executable, ["status", "--json"], options.env);
  const worktrees = await runApprovedCommand(executable, ["worktree", "ps", "--json"], options.env);
  return sanitizeSnapshot(status, worktrees);
}
