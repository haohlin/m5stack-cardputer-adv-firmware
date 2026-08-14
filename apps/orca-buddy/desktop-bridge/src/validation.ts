import { BlockList, isIP } from "node:net";

export const MAX_TEXT_BYTES = 480;
export const MAX_LABEL_BYTES = 80;
export const MAX_PENDING_QUESTIONS = 8;

export class ValidationError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "ValidationError";
  }
}

const unspecifiedAddresses = new BlockList();
unspecifiedAddresses.addAddress("0.0.0.0", "ipv4");
unspecifiedAddresses.addAddress("::", "ipv6");

export function assertExplicitBindAddress(host: string): void {
  if (host.length === 0 || host.trim() !== host || host === "*" || host === "0" || host.startsWith("[") || host.endsWith("]")) {
    throw new ValidationError("wildcard or invalid device listener address is forbidden");
  }
  const family = isIP(host);
  if (
    (family === 4 && unspecifiedAddresses.check(host, "ipv4")) ||
    (family === 6 && unspecifiedAddresses.check(host, "ipv6"))
  ) {
    throw new ValidationError("wildcard device listener address is forbidden");
  }
  if (family === 0) {
    const labels = host.split(".");
    if (
      host.length > 253 ||
      labels.some((label) => label.length === 0 || label.length > 63 || !/^[A-Za-z0-9](?:[A-Za-z0-9-]*[A-Za-z0-9])?$/.test(label))
    ) {
      throw new ValidationError("invalid explicit device listener hostname");
    }
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
