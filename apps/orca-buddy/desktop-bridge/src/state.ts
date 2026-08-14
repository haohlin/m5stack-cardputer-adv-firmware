import { randomUUID } from "node:crypto";
import type { BridgePublicStatus, OrcaSnapshot, ServerMessage } from "./types.js";
import {
  assertBoundedText,
  assertQuestionLabels,
  isRecord,
  MAX_PENDING_QUESTIONS
} from "./validation.js";

type SendDeviceMessage = (message: ServerMessage) => boolean | void;

interface PendingQuestion {
  labels: string[];
  resolve: (value: { questionId: string; answer: string }) => void;
  reject: (error: Error) => void;
  timeout: NodeJS.Timeout;
}

export interface BridgeStateOptions {
  questionTimeoutMs?: number;
}

export class BridgeState {
  private snapshot: OrcaSnapshot = { connected: false, worktreeCount: 0, items: [] };
  private readonly devices = new Set<SendDeviceMessage>();
  private readonly pending = new Map<string, PendingQuestion>();
  private readonly questionTimeoutMs: number;
  private prompt: string | null = null;

  constructor(options: BridgeStateOptions = {}) {
    this.questionTimeoutMs = options.questionTimeoutMs ?? 60_000;
  }

  setSnapshot(snapshot: OrcaSnapshot): void {
    this.snapshot = structuredClone(snapshot);
    this.broadcast({ version: "orca-cardputer/v1", type: "snapshot", snapshot: this.snapshot });
  }

  attachDevice(send: SendDeviceMessage): () => void {
    this.devices.add(send);
    send({ version: "orca-cardputer/v1", type: "snapshot", snapshot: structuredClone(this.snapshot) });
    return () => this.devices.delete(send);
  }

  notify(text: unknown): { delivered: boolean } {
    assertBoundedText(text, "notification text");
    return {
      delivered: this.broadcast({ version: "orca-cardputer/v1", type: "notify", text }) > 0
    };
  }

  async ask(question: unknown, labels: unknown, timeoutMs = this.questionTimeoutMs): Promise<{ questionId: string; answer: string }> {
    assertBoundedText(question, "question text");
    assertQuestionLabels(labels);
    if (this.pending.size >= MAX_PENDING_QUESTIONS) {
      throw new Error("bridge already has eight pending questions");
    }
    if (this.devices.size === 0) throw new Error("no device connected");
    if (!Number.isInteger(timeoutMs) || timeoutMs < 1 || timeoutMs > 120_000) {
      throw new Error("question timeout must be 1 to 120000 milliseconds");
    }

    const questionId = randomUUID();
    const answer = new Promise<{ questionId: string; answer: string }>((resolve, reject) => {
      const timeout = setTimeout(() => {
        this.pending.delete(questionId);
        reject(new Error("question timed out"));
      }, timeoutMs);
      timeout.unref();
      this.pending.set(questionId, { labels: [...labels], resolve, reject, timeout });
    });
    this.broadcast({
      version: "orca-cardputer/v1",
      type: "question.request",
      questionId,
      question,
      labels: [...labels]
    });
    return answer;
  }

  handleDeviceMessage(message: unknown): void {
    if (!isRecord(message) || message.version !== "orca-cardputer/v1") {
      throw new Error("invalid device protocol version");
    }
    switch (message.type) {
      case "hello":
      case "state":
        return;
      case "prompt.draft":
        assertBoundedText(message.text, "prompt text");
        this.prompt = message.text;
        return;
      case "question.answer":
        this.acceptAnswer(message.questionId, message.answer);
        return;
      default:
        throw new Error("unsupported device message type");
    }
  }

  getPrompt(): { prompt: string | null } {
    const prompt = this.prompt;
    this.prompt = null;
    return { prompt };
  }

  publicStatus(): BridgePublicStatus {
    return {
      snapshot: structuredClone(this.snapshot),
      deviceConnected: this.devices.size > 0,
      pendingQuestions: this.pending.size
    };
  }

  private acceptAnswer(questionId: unknown, answer: unknown): void {
    if (typeof questionId !== "string" || typeof answer !== "string") {
      throw new Error("question answer requires string questionId and answer");
    }
    const pending = this.pending.get(questionId);
    if (pending === undefined) throw new Error("unknown questionId");
    if (!pending.labels.includes(answer)) throw new Error("answer label was not offered");
    clearTimeout(pending.timeout);
    this.pending.delete(questionId);
    pending.resolve({ questionId, answer });
  }

  private broadcast(message: ServerMessage): number {
    let delivered = 0;
    for (const send of this.devices) {
      if (send(structuredClone(message)) !== false) delivered += 1;
    }
    return delivered;
  }
}
