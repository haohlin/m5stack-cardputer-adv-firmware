export const MAX_TEXT_BYTES = 480;
export const MAX_LABEL_BYTES = 80;
export const MAX_PENDING_QUESTIONS = 8;

export class ValidationError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "ValidationError";
  }
}

export function assertBoundedText(value: unknown, field: string, maximum = MAX_TEXT_BYTES): asserts value is string {
  if (typeof value !== "string" || value.length === 0) {
    throw new ValidationError(`${field} must be a non-empty string`);
  }
  if (Buffer.byteLength(value, "utf8") > maximum) {
    throw new ValidationError(`${field} must be at most ${maximum} UTF-8 bytes`);
  }
  if (/\p{Cc}/u.test(value)) {
    throw new ValidationError(`${field} must not contain control characters`);
  }
}

export function assertQuestionLabels(value: unknown): asserts value is string[] {
  if (!Array.isArray(value) || value.length < 2 || value.length > 5) {
    throw new ValidationError("questions require 2 to 5 answer labels");
  }
  const seen = new Set<string>();
  for (const label of value) {
    assertBoundedText(label, "answer label", MAX_LABEL_BYTES);
    if (seen.has(label)) throw new ValidationError("answer labels must be unique");
    seen.add(label);
  }
}

export function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}
