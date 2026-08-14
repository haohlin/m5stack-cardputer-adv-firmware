export interface OrcaSnapshotItem {
  displayName?: string;
  workspaceStatus?: string;
  agentState?: string;
}

export interface OrcaSnapshot {
  connected: boolean;
  worktreeCount: number;
  items: OrcaSnapshotItem[];
}

export interface BridgePublicStatus {
  snapshot: OrcaSnapshot;
  deviceConnected: boolean;
  pendingQuestions: number;
}

export interface DeviceMessage {
  version: "orca-cardputer/v1";
  type: "hello" | "state" | "question.answer" | "prompt.draft";
  questionId?: string;
  answer?: string;
  text?: string;
}

export interface ServerMessage {
  version: "orca-cardputer/v1";
  type: "snapshot" | "notify" | "question.request";
  snapshot?: OrcaSnapshot;
  text?: string;
  questionId?: string;
  question?: string;
  labels?: string[];
}
