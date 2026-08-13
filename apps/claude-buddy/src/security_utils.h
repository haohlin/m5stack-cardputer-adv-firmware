#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

inline bool buddySafePathComponent(const char* value, size_t capacity) {
  if (!value || !value[0] || capacity < 2) return false;
  const size_t len = strlen(value);
  if (len >= capacity || strcmp(value, ".") == 0 || strcmp(value, "..") == 0) return false;
  for (size_t i = 0; i < len; ++i) {
    const unsigned char c = (unsigned char)value[i];
    const bool alphaNumeric = (c >= 'a' && c <= 'z') ||
                              (c >= 'A' && c <= 'Z') ||
                              (c >= '0' && c <= '9');
    if (!alphaNumeric && c != '-' && c != '_' && c != '.') return false;
  }
  const unsigned char first = (unsigned char)value[0];
  return (first >= 'a' && first <= 'z') ||
         (first >= 'A' && first <= 'Z') ||
         (first >= '0' && first <= '9');
}

template <size_t Count, size_t Width>
inline bool buddyAppendPath(char (&paths)[Count][Width], uint8_t& used, const char* value) {
  if (used >= Count || !buddySafePathComponent(value, Width)) return false;
  snprintf(paths[used], Width, "%s", value);
  ++used;
  return true;
}

inline bool buddyAppendPath(char (*paths)[32], uint8_t& used, const char* value) {
  if (!paths || used >= 32 || !buddySafePathComponent(value, 32)) return false;
  snprintf(paths[used], 32, "%s", value);
  ++used;
  return true;
}

inline bool buddyFormattedLengthFits(int length, size_t capacity) {
  return length >= 0 && (size_t)length < capacity;
}

inline bool buddyTransferFits(uint32_t total, uint32_t available, uint32_t reserve) {
  return total > 0 && total <= available && reserve <= available - total;
}

inline bool buddyChunkFits(uint32_t fileExpected, uint32_t fileWritten,
                           uint32_t totalExpected, uint32_t totalWritten,
                           uint32_t chunk) {
  if (fileWritten > fileExpected || totalWritten > totalExpected) return false;
  return chunk <= fileExpected - fileWritten && chunk <= totalExpected - totalWritten;
}

inline uint32_t buddyPairingPasskey(uint32_t randomValue) {
  return 100000U + (randomValue % 900000U);
}
