#pragma once
#include <Arduino.h>

// ESP32-S3 USB CDC writes can stall the main loop if the host has opened the
// port but is not draining it. Protocol echoes/acks to Serial are therefore
// best-effort so BLE traffic cannot starve keyboard scanning.
inline size_t safeSerialWrite(const char* data, size_t len) {
  if (!data || len == 0) return 0;
  size_t written = 0;
  uint32_t start = millis();
  while (written < len) {
    int room = Serial.availableForWrite();
    if (room <= 0) {
      if (millis() - start > 8) break;
      delay(0);
      continue;
    }
    size_t n = len - written;
    if (n > (size_t)room) n = (size_t)room;
    written += Serial.write((const uint8_t*)data + written, n);
  }
  return written;
}

inline size_t safeSerialWrite(const uint8_t* data, size_t len) {
  return safeSerialWrite((const char*)data, len);
}
