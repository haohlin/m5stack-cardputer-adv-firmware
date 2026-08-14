import assert from "node:assert/strict";
import test from "node:test";
import { BridgeState } from "../src/state.js";

test("question request and answer make a bounded device round trip", async () => {
  const state = new BridgeState({ questionTimeoutMs: 1_000 });
  const sent: unknown[] = [];
  state.attachDevice((message) => sent.push(message));

  const answerPromise = state.ask("Deploy build?", ["Yes", "No"]);
  const request = sent.at(-1) as { version: string; type: string; questionId: string };
  assert.equal(request.version, "orca-cardputer/v1");
  assert.equal(request.type, "question.request");

  state.handleDeviceMessage({
    version: "orca-cardputer/v1",
    type: "question.answer",
    questionId: request.questionId,
    answer: "No"
  });
  assert.deepEqual(await answerPromise, { questionId: request.questionId, answer: "No" });
  assert.equal(state.publicStatus().pendingQuestions, 0);
});

test("prompt draft round trip consumes only bounded text", () => {
  const state = new BridgeState();
  state.handleDeviceMessage({ version: "orca-cardputer/v1", type: "prompt.draft", text: "Run focused tests" });
  assert.deepEqual(state.getPrompt(), { prompt: "Run focused tests" });
  assert.deepEqual(state.getPrompt(), { prompt: null });
  assert.throws(
    () => state.handleDeviceMessage({ version: "orca-cardputer/v1", type: "prompt.draft", text: "界".repeat(161) }),
    /480 UTF-8 bytes/
  );
});

test("text, label, and pending-question limits reject before device dispatch", async () => {
  const state = new BridgeState({ questionTimeoutMs: 5_000 });
  const sent: Array<{ type?: string; questionId?: string; labels?: string[] }> = [];
  state.attachDevice((message) => sent.push(message as typeof sent[number]));

  assert.throws(() => state.notify("界".repeat(161)), /480 UTF-8 bytes/);
  await assert.rejects(() => state.ask("Question", ["only one"]), /2 to 5/);
  await assert.rejects(() => state.ask("Question", ["x".repeat(81), "No"]), /80 UTF-8 bytes/);

  const pending = Array.from({ length: 8 }, (_, index) => state.ask(`Question ${index}`, ["Yes", "No"]));
  await assert.rejects(() => state.ask("Question 9", ["Yes", "No"]), /eight pending/i);
  assert.equal(sent.filter((message) => message.type === "question.request").length, 8);

  for (const request of sent.filter((message) => message.type === "question.request")) {
    state.handleDeviceMessage({
      version: "orca-cardputer/v1",
      type: "question.answer",
      questionId: request.questionId,
      answer: "Yes"
    });
  }
  await Promise.all(pending);
});

test("device protocol rejects unknown names and invalid question answers", async () => {
  const state = new BridgeState({ questionTimeoutMs: 1_000 });
  const sent: Array<{ questionId?: string }> = [];
  state.attachDevice((message) => sent.push(message as { questionId?: string }));
  assert.throws(() => state.handleDeviceMessage({ version: "claude/v1", type: "hello" }), /protocol/);
  assert.throws(() => state.handleDeviceMessage({ version: "orca-cardputer/v1", type: "terminal.read" }), /message type/);

  const pending = state.ask("Question", ["A", "B"]);
  const questionId = sent.at(-1)?.questionId;
  assert.throws(
    () => state.handleDeviceMessage({ version: "orca-cardputer/v1", type: "question.answer", questionId, answer: "C" }),
    /answer label/
  );
  state.handleDeviceMessage({ version: "orca-cardputer/v1", type: "question.answer", questionId, answer: "A" });
  await pending;
});

test("notification reports undelivered when device rejects outbound backpressure", () => {
  const state = new BridgeState();
  state.attachDevice(() => false);
  assert.deepEqual(state.notify("Notice"), { delivered: false });
});
