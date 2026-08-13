#pragma once
#ifdef BUDDY_XFER_HOST_TEST
#include "claude-buddy-xfer-host-stubs.h"
#else
#include <Arduino.h>
#include <LittleFS.h>
#include "ble_bridge.h"
#include <mbedtls/base64.h>
#include <ArduinoJson.h>
#include "safe_serial.h"
#include "bridge_config.h"
#endif

#ifndef CARDPUTER_FW_TAG
#define CARDPUTER_FW_TAG "adv-dev-bridge-serial"
#endif
#include "security_utils.h"

static File     _xFile;
static uint32_t _xExpected = 0, _xWritten = 0;
static char     _xCharName[24] = "";
static char     _xPreviousCharName[24] = "";
static bool     _xActive = false;
static bool     _xBridgeConfig = false;
static bool     _xFailed = false;
static uint32_t _xTotal = 0, _xTotalWritten = 0;
static uint32_t _xLastActivityMs = 0;
static constexpr uint32_t XFER_BRIDGE_CONFIG_MAX_BYTES = 4096;
static constexpr uint32_t XFER_FILESYSTEM_RESERVE_BYTES = 4096;
static constexpr const char* XFER_CHARACTER_STAGING_DIR = "/characters/incoming";
static constexpr const char* XFER_CHARACTER_ROLLBACK_DIR = "/characters/rollback";

// Ack goes to both streams — we don't track which one delivered the command,
// and writes to a clientless SerialBT just drop. The bridge listens on
// whichever port it opened.
static void _xAck(const char* what, bool ok, uint32_t n = 0) {
  char b[64];
  int len = snprintf(b, sizeof(b), "{\"ack\":\"%s\",\"ok\":%s,\"n\":%lu}\n", what, ok?"true":"false", (unsigned long)n);
  safeSerialWrite(b, len);
  bleWrite((const uint8_t*)b, len);
}

static void _xAckError(const char* what, const char* error) {
  char b[128];
  int len = snprintf(b, sizeof(b), "{\"ack\":\"%s\",\"ok\":false,\"n\":0,\"error\":\"%.64s\"}\n",
                     what, error ? error : "error");
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

static void _xWipeBridgeStaging() {
  _xWipeDir("/bridge");
  LittleFS.rmdir("/bridge");
}

static bool _xLooksBridgeConfigName(const char* name) {
  return name && (strstr(name, "bridge-config") ||
                  strstr(name, "claude-cardputer-bridge") ||
                  strstr(name, "cardputer-bridge"));
}

static const char* _xBaseName(const char* path) {
  if (!path) return "";
  const char* slash = strrchr(path, '/');
  return slash ? slash + 1 : path;
}

static bool _xIsBridgeJsonPath(const char* path) {
  return strcmp(_xBaseName(path), "bridge.json") == 0;
}

// Only one character lives on the device at a time. Installing a new one
// under a different name would otherwise leave the old one's files eating
// space. Wipe everything under /characters/, return total bytes reclaimed.
static bool _xIsReservedCharacterDir(const char* name) {
  return name && (strcmp(name, "incoming") == 0 || strcmp(name, "rollback") == 0);
}

static void _xWipeAllCharsExcept(const char* keep) {
  File root = LittleFS.open("/characters");
  if (!root || !root.isDirectory()) { LittleFS.mkdir("/characters"); return; }
  File sub = root.openNextFile();
  while (sub) {
    if (sub.isDirectory()) {
      const char* name = _xBaseName(sub.name());
      if (keep && strcmp(name, keep) == 0) { sub.close(); sub = root.openNextFile(); continue; }
      char p[64];
      snprintf(p, sizeof(p), "/characters/%s", name);
      sub.close();
      _xWipeDir(p);
      LittleFS.rmdir(p);
    } else {
      sub.close();
    }
    sub = root.openNextFile();
  }
  root.close();
}

static bool _xFindExistingCharacter(char* out, size_t outLen) {
  if (!out || outLen == 0) return false;
  out[0] = 0;
  File root = LittleFS.open("/characters");
  if (!root || !root.isDirectory()) return false;
  File sub = root.openNextFile();
  while (sub) {
    if (sub.isDirectory()) {
      const char* name = _xBaseName(sub.name());
      if (!_xIsReservedCharacterDir(name) && buddySafePathComponent(name, outLen)) {
        snprintf(out, outLen, "%s", name);
        sub.close();
        root.close();
        return true;
      }
    }
    sub.close();
    sub = root.openNextFile();
  }
  root.close();
  return false;
}

static void _xAbortTransfer() {
  if (_xFile) _xFile.close();
  _xFile = File();
  if (_xBridgeConfig) {
    _xWipeBridgeStaging();
  } else if (buddySafePathComponent(_xCharName, sizeof(_xCharName))) {
    _xWipeDir(XFER_CHARACTER_STAGING_DIR);
    LittleFS.rmdir(XFER_CHARACTER_STAGING_DIR);
  }
  _xActive = false;
  _xBridgeConfig = false;
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
#ifndef BUDDY_XFER_HOST_TEST
#include "stats.h"
#include "hal.h"
#endif

static bool _xSaveBridgeConfigFile(const char* path, char* err, size_t errLen) {
  // A new authority/config set must never race a live Wi-Fi connection.
  settings().wifi = false;
  settingsSave();
  bool ok = bridgeConfigSaveFromFile(path, err, errLen);
  return ok;
}

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

  if (strcmp(cmd, "bridge_forget") == 0) {
    bridgeConfigClear();
    settings().wifi = false;
    settingsSave();
    _xAck("bridge_forget", true);
    return true;
  }

  if (strcmp(cmd, "bridge_config") == 0) {
    char err[48] = "";
    settings().wifi = false;
    settingsSave();
    bool ok = bridgeConfigSaveJson(doc, err, sizeof(err));
    if (ok) {
      _xAck("bridge_config", true);
    } else {
      _xAckError("bridge_config", err[0] ? err : "bad bridge config");
    }
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
    char b[384];
    int len = snprintf(b, sizeof(b),
      "{\"ack\":\"status\",\"ok\":true,\"n\":0,\"data\":{"
      "\"name\":\"%s\",\"owner\":\"%s\",\"fw\":\"%s\",\"sec\":%s,"
      "\"bat\":{\"pct\":%d,\"mV\":%d,\"mA\":%d,\"usb\":%s},"
      "\"sys\":{\"up\":%lu,\"heap\":%u,\"fsFree\":%lu,\"fsTotal\":%lu},"
      "\"stats\":{\"appr\":%u,\"deny\":%u,\"vel\":%u,\"nap\":%lu,\"lvl\":%u}"
      "}}\n",
      petName(), ownerName(), CARDPUTER_FW_TAG, bleSecure() ? "true" : "false",
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
    if (!buddySafePathComponent(name, sizeof(_xCharName)) ||
        strcmp(name, "incoming") == 0 || strcmp(name, "rollback") == 0) {
      _xAck("char_begin", false);
      return true;
    }
    _xBridgeConfig = _xLooksBridgeConfigName(name);

    if (_xBridgeConfig) {
      const uint32_t totalBytes = LittleFS.totalBytes();
      const uint32_t usedBytes = LittleFS.usedBytes();
      const uint32_t available = usedBytes <= totalBytes ? totalBytes - usedBytes : 0;
      if (_xTotal > XFER_BRIDGE_CONFIG_MAX_BYTES ||
          !buddyTransferFits(_xTotal, available, XFER_FILESYSTEM_RESERVE_BYTES)) {
        _xBridgeConfig = false;
        _xAck("char_begin", false);
        return true;
      }
      snprintf(_xCharName, sizeof(_xCharName), "%s", "bridge-config");
      _xWipeDir("/bridge");
      LittleFS.mkdir("/bridge");
      _xTotalWritten = 0;
      _xExpected = 0;
      _xWritten = 0;
      _xFailed = false;
      _xActive = true;
      _xLastActivityMs = millis();
      _xAck("char_begin", true);
      return true;
    }

    // Replacement stages beside the current character. Do not count the old
    // pack as available: it remains intact until the staged pack has completed
    // and parsed successfully.
    uint32_t free = LittleFS.totalBytes() - LittleFS.usedBytes();
    // Headroom for LittleFS metadata overhead — it's not byte-for-byte.
    uint32_t available = free;
    if (!buddyTransferFits(_xTotal, available, XFER_FILESYSTEM_RESERVE_BYTES)) {
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
    _xFindExistingCharacter(_xPreviousCharName, sizeof(_xPreviousCharName));
    _xWipeDir(XFER_CHARACTER_STAGING_DIR);
    LittleFS.rmdir(XFER_CHARACTER_STAGING_DIR);
    LittleFS.mkdir("/characters");
    LittleFS.mkdir(XFER_CHARACTER_STAGING_DIR);
    _xTotalWritten = 0;
    _xExpected = 0;
    _xWritten = 0;
    _xFailed = false;
    _xActive = true;
    _xBridgeConfig = false;
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
    char full[80];
    if (!_xBridgeConfig && _xIsBridgeJsonPath(path)) {
      if (_xTotal > XFER_BRIDGE_CONFIG_MAX_BYTES) {
        _xAbortTransfer();
        _xAck("file", false);
        return true;
      }
      // A sender may identify a bridge transfer by file name rather than the
      // character name. Discard the empty character staging area before
      // switching destinations so it cannot be mistaken for a character later.
      _xWipeDir(XFER_CHARACTER_STAGING_DIR);
      LittleFS.rmdir(XFER_CHARACTER_STAGING_DIR);
      _xBridgeConfig = true;
      _xWipeDir("/bridge");
      LittleFS.mkdir("/bridge");
    }
    if (_xBridgeConfig) snprintf(full, sizeof(full), "/bridge/%s", path);
    else snprintf(full, sizeof(full), "%s/%s", XFER_CHARACTER_STAGING_DIR, path);
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
    if (_xBridgeConfig) {
      if (!ok) {
        _xAbortTransfer();
        _xAck("char_end", false);
        return true;
      }
      _xActive = false;
      char err[48] = "";
      ok = _xSaveBridgeConfigFile("/bridge/bridge.json", err, sizeof(err));
      if (ok) {
        _xBridgeConfig = false;
        _xAck("char_end", true);
      } else {
        _xWipeBridgeStaging();
        _xBridgeConfig = false;
        _xFailed = true;
        _xAckError("char_end", err[0] ? err : "bad bridge config");
      }
      return true;
    }
    _xActive = false;
    char bridgePath[96];
    snprintf(bridgePath, sizeof(bridgePath), "%s/bridge.json", XFER_CHARACTER_STAGING_DIR);
    if (LittleFS.exists(bridgePath)) {
      char err[48] = "";
      if (ok) ok = _xSaveBridgeConfigFile(bridgePath, err, sizeof(err));
      _xWipeDir(XFER_CHARACTER_STAGING_DIR);
      LittleFS.rmdir(XFER_CHARACTER_STAGING_DIR);
      if (ok) {
        _xAck("char_end", true);
      } else {
        _xAckError("char_end", err[0] ? err : "bad bridge config");
      }
      return true;
    }
    // Validate staged files before modifying current character paths.
    if (ok) {
      characterClose();
      ok = characterInit("incoming");
    }
    char target[64];
    snprintf(target, sizeof(target), "/characters/%s", _xCharName);
    char rollback[64];
    snprintf(rollback, sizeof(rollback), "%s", XFER_CHARACTER_ROLLBACK_DIR);
    bool movedOld = false;
    if (ok && LittleFS.exists(target)) {
      _xWipeDir(rollback);
      LittleFS.rmdir(rollback);
      movedOld = LittleFS.rename(target, rollback);
      ok = movedOld;
    }
    if (ok) ok = LittleFS.rename(XFER_CHARACTER_STAGING_DIR, target);
    if (ok) ok = characterInit(_xCharName);
    if (!ok) {
      if (LittleFS.exists(target)) {
        _xWipeDir(XFER_CHARACTER_STAGING_DIR);
        LittleFS.rmdir(XFER_CHARACTER_STAGING_DIR);
        LittleFS.rename(target, XFER_CHARACTER_STAGING_DIR);
      }
      if (movedOld) LittleFS.rename(rollback, target);
      if (_xPreviousCharName[0]) characterInit(_xPreviousCharName);
      _xAbortTransfer();
    } else {
      _xWipeAllCharsExcept(_xCharName);
    }
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
