#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

constexpr uint32_t RFID_WRITE_ARM_WINDOW_MS = 8000;
constexpr size_t RFID_MAX_SLOT_FILE_BYTES = 16 * 1024;
constexpr size_t RFID_MAX_KEY_FILE_BYTES = 16 * 1024;
constexpr size_t RFID_MAX_PERSIST_LINE_CHARS = 128;
constexpr size_t RFID_MAX_MIFARE_KEYS = 256;

inline size_t rfidConfirmedCommandLength(const char* command) {
  if (!command) return 0;

  size_t end = strlen(command);
  while (end > 0 && (command[end - 1] == ' ' || command[end - 1] == '\t')) end--;

  size_t tokenStart = end;
  while (tokenStart > 0 && command[tokenStart - 1] != ' ' && command[tokenStart - 1] != '\t') tokenStart--;
  constexpr char kConfirm[] = "confirm";
  if (end - tokenStart != sizeof(kConfirm) - 1 ||
      strncmp(command + tokenStart, kConfirm, sizeof(kConfirm) - 1) != 0) {
    return 0;
  }

  size_t bodyEnd = tokenStart;
  while (bodyEnd > 0 && (command[bodyEnd - 1] == ' ' || command[bodyEnd - 1] == '\t')) bodyEnd--;
  return bodyEnd;
}

inline uint32_t rfidWriteArmDeadline(uint32_t startedAtMs) {
  return startedAtMs + RFID_WRITE_ARM_WINDOW_MS;
}

inline bool rfidArmStillValid(uint32_t nowMs, uint32_t deadlineMs) {
  return (int32_t)(deadlineMs - nowMs) > 0;
}

inline bool rfidSerialDestructiveArmValid(uint32_t nowMs, uint32_t deadlineMs,
                                          bool physicalCardPresent, bool armMatchesSelection) {
  return physicalCardPresent && armMatchesSelection && rfidArmStillValid(nowMs, deadlineMs);
}

inline bool rfidPersistFileAllowed(size_t bytes, size_t limit) {
  return bytes <= limit;
}

inline bool rfidPersistLineAllowed(size_t chars) {
  return chars <= RFID_MAX_PERSIST_LINE_CHARS;
}

inline bool rfidKeyCountAllowsInsert(size_t currentCount) {
  return currentCount < RFID_MAX_MIFARE_KEYS;
}
