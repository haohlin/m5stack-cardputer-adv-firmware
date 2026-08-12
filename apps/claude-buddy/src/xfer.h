#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include "ble_bridge.h"
#include <mbedtls/base64.h>
#include <ArduinoJson.h>
#include "safe_serial.h"
#include "security_utils.h"

static File     _xFile;
static uint32_t _xExpected = 0, _xWritten = 0;
static char     _xCharName[24] = "";
static bool     _xActive = false;
static bool     _xFailed = false;
static uint32_t _xTotal = 0, _xTotalWritten = 0;
static uint32_t _xLastActivityMs = 0;

// Ack goes to both streams — we don't track which one delivered the command,
// and writes to a clientless SerialBT just drop. The bridge listens on
// whichever port it opened.
static void _xAck(const char* what, bool ok, uint32_t n = 0) {
  char b[64];
  int len = snprintf(b, sizeof(b), "{\"ack\":\"%s\",\"ok\":%s,\"n\":%lu}\n", what, ok?"true":"false", (unsigned long)n);
  safeSerialWrite(b, len);
  bleWrite((const uint8_t*)b, len);
}

static uint32_t _xWipeDir(const char* dir) {
  File d = LittleFS.open(dir);
  if (!d || !d.isDirectory()) { LittleFS.mkdir(dir); return 0; }
  uint32_t freed = 0;
  File f = d.openNextFile();
  while (f) {
    freed += f.size();
    char p[80];
    snprintf(p, sizeof(p), "%s/%s", dir, f.name());
    f.close();
    LittleFS.remove(p);
    f = d.openNextFile();
  }
  d.close();
  return freed;
}

// Only one character lives on the device at a time. Installing a new one
// under a different name would otherwise leave the old one's files eating
// space. Wipe everything under /characters/, return total bytes reclaimed.
static uint32_t _xWipeAllChars() {
  File root = LittleFS.open("/characters");
  if (!root || !root.isDirectory()) { LittleFS.mkdir("/characters"); return 0; }
  uint32_t freed = 0;
  File sub = root.openNextFile();
  while (sub) {
    if (sub.isDirectory()) {
      char p[64];
      snprintf(p, sizeof(p), "/characters/%s", sub.name());
      sub.close();
      freed += _xWipeDir(p);
      LittleFS.rmdir(p);
    } else {
      sub.close();
    }
    sub = root.openNextFile();
  }
  root.close();
  return freed;
}

static void _xAbortTransfer() {
  if (_xFile) _xFile.close();
  _xFile = File();
  if (buddySafePathComponent(_xCharName, sizeof(_xCharName))) {
    char dir[48];
    snprintf(dir, sizeof(dir), "/characters/%s", _xCharName);
    _xWipeDir(dir);
    LittleFS.rmdir(dir);
  }
  _xActive = false;
  _xFailed = true;
}

// Called from data.h when incoming JSON has a "cmd" key. Returns true if
// it was a transfer command (caller should skip state-update parsing).
// Needs characterClose()/characterInit() declared before this include.
void characterClose();
bool characterInit(const char* name);
void petNameSet(const char* name);
const char* petName();
void ownerSet(const char* name);
const char* ownerName();
#include "stats.h"
#include "hal.h"

inline bool xferCommand(JsonDocument& doc) {
  const char* cmd = doc["cmd"];
  if (!cmd) return false;

  if (strcmp(cmd, "name") == 0) {
    const char* n = doc["name"];
    if (n) petNameSet(n);
    _xAck("name", n != nullptr);
    return true;
  }

  if (strcmp(cmd, "species") == 0) {
    extern bool buddyMode, gifAvailable;
    extern void buddySetSpeciesIdx(uint8_t);
    uint8_t idx = doc["idx"] | 0xFF;
    speciesIdxSave(idx);
    buddyMode = !(gifAvailable && idx == 0xFF);
    if (buddyMode) buddySetSpeciesIdx(idx);
    _xAck("species", true);
    return true;
  }

  if (strcmp(cmd, "unpair") == 0) {
    bleClearBonds();
    _xAck("unpair", true);
    return true;
  }

  if (strcmp(cmd, "owner") == 0) {
    const char* n = doc["name"];
    if (n) ownerSet(n);
    _xAck("owner", n != nullptr);
    return true;
  }

  if (strcmp(cmd, "status") == 0) {
    // Dump everything the info screens show. Manual printf rather than
    // ArduinoJson serialize — less heap churn, and the shape is fixed.
    int vBat = (int)(halBatteryVolts() * 1000);
    int iBat = (int)halBatteryMilliAmps();
    int vBus = (int)(halVbusVolts() * 1000);
    int pct = (vBat - 3200) / 10;
    if (pct < 0) pct = 0; if (pct > 100) pct = 100;
    char b[320];
    int len = snprintf(b, sizeof(b),
      "{\"ack\":\"status\",\"ok\":true,\"n\":0,\"data\":{"
      "\"name\":\"%s\",\"owner\":\"%s\",\"sec\":%s,"
      "\"bat\":{\"pct\":%d,\"mV\":%d,\"mA\":%d,\"usb\":%s},"
      "\"sys\":{\"up\":%lu,\"heap\":%u,\"fsFree\":%lu,\"fsTotal\":%lu},"
      "\"stats\":{\"appr\":%u,\"deny\":%u,\"vel\":%u,\"nap\":%lu,\"lvl\":%u}"
      "}}\n",
      petName(), ownerName(), bleSecure() ? "true" : "false",
      pct, vBat, iBat, (vBus > 4000) ? "true" : "false",
      millis() / 1000, ESP.getFreeHeap(),
      (unsigned long)(LittleFS.totalBytes() - LittleFS.usedBytes()),
      (unsigned long)LittleFS.totalBytes(),
      stats().approvals, stats().denials, statsMedianVelocity(),
      (unsigned long)stats().napSeconds, stats().level
    );
    if (!buddyFormattedLengthFits(len, sizeof(b))) {
      _xAck("status", false);
      return true;
    }
    safeSerialWrite(b, len);
    bleWrite((const uint8_t*)b, len);
    return true;
  }

  if (strcmp(cmd, "char_begin") == 0) {
    if (_xActive) _xAbortTransfer();
    const char* name = doc["name"] | "pet";
    _xTotal = doc["total"] | 0;
    if (!buddySafePathComponent(name, sizeof(_xCharName))) {
      _xAck("char_begin", false);
      return true;
    }

    // Fit check: free space after wiping everything under /characters/.
    // Do the math before touching the filesystem so a failed check leaves
    // the current character intact.
    uint32_t free = LittleFS.totalBytes() - LittleFS.usedBytes();
    uint32_t reclaimable = 0;
    {
      File r = LittleFS.open("/characters");
      if (r && r.isDirectory()) {
        File s = r.openNextFile();
        while (s) {
          if (s.isDirectory()) {
            File f = s.openNextFile();
            while (f) { reclaimable += f.size(); f.close(); f = s.openNextFile(); }
          }
          s.close(); s = r.openNextFile();
        }
        r.close();
      }
    }
    // Headroom for LittleFS metadata overhead — it's not byte-for-byte.
    uint32_t available = free + reclaimable;
    if (!buddyTransferFits(_xTotal, available, 4096)) {
      char b[96];
      int len = snprintf(b, sizeof(b),
        "{\"ack\":\"char_begin\",\"ok\":false,\"n\":%lu,\"error\":\"need %luK, have %luK\"}\n",
        (unsigned long)available, (unsigned long)(_xTotal/1024), (unsigned long)(available/1024)
      );
      safeSerialWrite(b, len);
      bleWrite((const uint8_t*)b, len);
      return true;
    }

    snprintf(_xCharName, sizeof(_xCharName), "%s", name);
    characterClose();
    _xWipeAllChars();
    char dir[48]; snprintf(dir, sizeof(dir), "/characters/%s", _xCharName);
    LittleFS.mkdir(dir);
    _xTotalWritten = 0;
    _xExpected = 0;
    _xWritten = 0;
    _xFailed = false;
    _xActive = true;
    _xLastActivityMs = millis();
    _xAck("char_begin", true);
    return true;
  }

  if (!_xActive) return strcmp(cmd, "permission") != 0;  // permission cmd is not ours

  if (strcmp(cmd, "file") == 0) {
    const char* path = doc["path"];
    _xExpected = doc["size"] | 0;
    _xWritten = 0;
    if (!path || _xFailed || _xFile ||
        !buddySafePathComponent(path, 48) ||
        _xTotalWritten > _xTotal || _xExpected > _xTotal - _xTotalWritten) {
      _xAbortTransfer();
      _xAck("file", false);
      return true;
    }
    char full[80]; snprintf(full, sizeof(full), "/characters/%s/%s", _xCharName, path);
    _xFile = LittleFS.open(full, "w");
    if (!_xFile) _xAbortTransfer();
    else _xLastActivityMs = millis();
    _xAck("file", (bool)_xFile);
    return true;
  }

  if (strcmp(cmd, "chunk") == 0) {
    const char* b64 = doc["d"];
    if (!b64 || !_xFile) { _xAck("chunk", false); return true; }
    uint8_t buf[300];
    size_t outLen = 0;
    int rc = mbedtls_base64_decode(buf, sizeof(buf), &outLen,
                                   (const uint8_t*)b64, strlen(b64));
    if (rc != 0 || _xFailed ||
        !buddyChunkFits(_xExpected, _xWritten, _xTotal, _xTotalWritten, outLen)) {
      _xAbortTransfer();
      _xAck("chunk", false);
      return true;
    }
    size_t stored = _xFile.write(buf, outLen);
    if (stored != outLen) {
      _xAbortTransfer();
      _xAck("chunk", false);
      return true;
    }
    _xWritten += outLen;
    _xTotalWritten += outLen;
    _xLastActivityMs = millis();
    // Ack every chunk — LittleFS writes can block on flash erase and the
    // UART RX buffer is only ~256 bytes. Without this the sender overruns it.
    _xAck("chunk", true, _xWritten);
    return true;
  }

  if (strcmp(cmd, "file_end") == 0) {
    bool ok = !_xFailed && _xFile && _xWritten == _xExpected;
    if (_xFile) _xFile.close();
    _xFile = File();
    if (!ok) _xAbortTransfer();
    else _xLastActivityMs = millis();
    _xAck("file_end", ok, _xWritten);
    return true;
  }

  if (strcmp(cmd, "char_end") == 0) {
    bool ok = _xActive && !_xFailed && !_xFile && _xTotalWritten == _xTotal;
    _xActive = false;
    if (ok) ok = characterInit(_xCharName);
    if (!ok) _xAbortTransfer();
    extern bool buddyMode, gifAvailable;
    if (ok) { buddyMode = false; gifAvailable = true; speciesIdxSave(0xFF); }
    _xAck("char_end", ok);
    return true;
  }

  return false;
}

inline bool xferActive() { return _xActive; }
inline uint32_t xferProgress() { return _xTotalWritten; }
inline uint32_t xferTotal() { return _xTotal; }
inline void xferTick() {
  if (_xActive && millis() - _xLastActivityMs > 30000) _xAbortTransfer();
}
