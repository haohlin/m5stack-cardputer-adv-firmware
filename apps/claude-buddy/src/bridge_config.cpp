#include "bridge_config.h"
#include "bridge_config_storage.h"
#include "security_utils.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Preferences.h>

namespace {
BridgeStoredConfig _cfg = {};
uint32_t _cfgVersion = 1;
constexpr char kBridgeRecordKey[] = "br_cfg";
using BridgeRecord = BuddyBridgeConfigRecord<BridgeStoredConfig>;

void bump() {
  _cfgVersion++;
  if (_cfgVersion == 0) _cfgVersion = 1;
}

void copyField(char* dst, size_t dstLen, const char* src) {
  if (!dst || dstLen == 0) return;
  if (!src) src = "";
  strncpy(dst, src, dstLen - 1);
  dst[dstLen - 1] = 0;
}

bool hasBadHostChars(const char* host) {
  if (!host || !host[0]) return true;
  if (host[0] == '<') return true;
  for (const char* p = host; *p; ++p) {
    char c = *p;
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '-';
    if (!ok) return true;
  }
  return false;
}

bool parseSecureEndpoint(const char* endpoint, char* host, size_t hostLen, uint16_t* port) {
  constexpr char kScheme[] = "wss://";
  if (!endpoint || strncmp(endpoint, kScheme, sizeof(kScheme) - 1) != 0) return false;
  const char* p = endpoint + sizeof(kScheme) - 1;
  const char* slash = strchr(p, '/');
  if (!slash || strcmp(slash, "/device") != 0) return false;
  const char* end = slash ? slash : p + strlen(p);
  const char* colon = nullptr;
  for (const char* q = p; q < end; ++q) {
    if (*q == ':') colon = q;
  }

  const char* hostEnd = colon ? colon : end;
  size_t n = (size_t)(hostEnd - p);
  if (n == 0 || n >= hostLen) return false;
  memcpy(host, p, n);
  host[n] = 0;
  if (hasBadHostChars(host)) return false;

  if (colon) {
    const char* value = colon + 1;
    if (value == end) return false;
    uint32_t parsed = 0;
    for (const char* q = value; q < end; ++q) {
      if (*q < '0' || *q > '9') return false;
      parsed = parsed * 10 + (uint32_t)(*q - '0');
      if (parsed > 65535) return false;
    }
    if (parsed == 0) return false;
    *port = (uint16_t)parsed;
  }
  return true;
}

bool validate(const BridgeStoredConfig& cfg, char* error, size_t errorLen) {
  auto fail = [&](const char* msg) {
    if (error && errorLen) copyField(error, errorLen, msg);
    return false;
  };
  if (!cfg.ssid[0]) return fail("missing ssid");
  if (hasBadHostChars(cfg.host)) return fail("bad host");
  if (cfg.port == 0) return fail("bad port");
  if (!buddyBridgeTokenAllowed(cfg.token)) return fail("weak token");
  if (!strstr(cfg.ca, "-----BEGIN CERTIFICATE-----") ||
      !strstr(cfg.ca, "-----END CERTIFICATE-----")) return fail("missing ca");
  return true;
}

bool validateBridgeIdentity(const BridgeStoredConfig& cfg, char* error, size_t errorLen) {
  auto fail = [&](const char* msg) {
    if (error && errorLen) copyField(error, errorLen, msg);
    return false;
  };
  if (hasBadHostChars(cfg.host)) {
    if (error && errorLen) {
      snprintf(error, errorLen, "bad host %.18s", cfg.host[0] ? cfg.host : "(empty)");
    }
    return false;
  }
  if (cfg.port == 0) return fail("bad port");
  if (!buddyBridgeTokenAllowed(cfg.token)) return fail("weak token");
  if (!strstr(cfg.ca, "-----BEGIN CERTIFICATE-----") ||
      !strstr(cfg.ca, "-----END CERTIFICATE-----")) return fail("missing ca");
  return true;
}

void removeLegacyBridgeKeys(Preferences& prefs) {
  prefs.remove("br_ssid");
  prefs.remove("br_pass");
  prefs.remove("br_host");
  prefs.remove("br_port");
  prefs.remove("br_tok");
  prefs.remove("br_ca");
  prefs.remove("br_valid");
}

bool persist(const BridgeStoredConfig& cfg) {
  const BridgeRecord record = buddyBridgeConfigRecordMake(cfg);
  Preferences prefs;
  if (!prefs.begin("buddy", false)) return false;
  const size_t stored = prefs.putBytes(kBridgeRecordKey, &record, sizeof(record));
  const bool committed = stored == sizeof(record);
  if (committed) removeLegacyBridgeKeys(prefs);
  prefs.end();

  if (!committed) return false;

  _cfg = cfg;
  bump();
  return true;
}

}  // namespace

void bridgeConfigLoad() {
  Preferences prefs;
  memset(&_cfg, 0, sizeof(_cfg));
  _cfg.port = 17878;
  BridgeRecord record = {};
  size_t loaded = 0;
  if (prefs.begin("buddy", true)) {
    if (prefs.getBytesLength(kBridgeRecordKey) == sizeof(record)) {
      loaded = prefs.getBytes(kBridgeRecordKey, &record, sizeof(record));
    }
    prefs.end();
  }

  BridgeStoredConfig candidate = {};
  if (loaded == sizeof(record) && buddyBridgeConfigRecordExtract(record, &candidate)) {
    char error[32];
    candidate.valid = validate(candidate, error, sizeof(error));
    _cfg = candidate;
  }
  bump();
}

const BridgeStoredConfig& bridgeConfig() { return _cfg; }
uint32_t bridgeConfigVersion() { return _cfgVersion; }
bool bridgeConfigValid() { return _cfg.valid; }

bool bridgeConfigSave(const BridgeStoredConfig& cfg) {
  char error[32];
  BridgeStoredConfig next = cfg;
  next.valid = validate(next, error, sizeof(error));
  if (!next.valid) return false;

  return persist(next);
}

bool bridgeConfigSaveJson(JsonDocument& doc, char* error, size_t errorLen) {
  auto fail = [&](const char* msg) {
    if (error && errorLen) copyField(error, errorLen, msg);
    return false;
  };

  // Endpoint authority and bearer credential must be a complete replacement.
  // Keeping an old token while accepting a caller-provided host enables a
  // trusted transport peer to redirect the existing credential.
  const char* endpoint = doc["endpoint"].as<const char*>();
  const char* token = doc["token"].as<const char*>();
  const char* ca = doc["ca"].as<const char*>();
  if (!endpoint || !token || !ca) return fail("endpoint token ca required");
  if (strlen(token) >= sizeof(_cfg.token)) return fail("token too long");
  if (strlen(ca) >= sizeof(_cfg.ca)) return fail("ca too long");

  BridgeStoredConfig next = _cfg;
  next.port = 17878;

  JsonObject wifi = doc["wifi"].as<JsonObject>();
  const char* ssid = wifi["ssid"].as<const char*>();
  if (!ssid) ssid = doc["ssid"].as<const char*>();
  if (ssid) copyField(next.ssid, sizeof(next.ssid), ssid);

  if (!wifi["password"].isNull()) copyField(next.pass, sizeof(next.pass), wifi["password"].as<const char*>());
  else if (!wifi["pass"].isNull()) copyField(next.pass, sizeof(next.pass), wifi["pass"].as<const char*>());
  else if (!doc["password"].isNull()) copyField(next.pass, sizeof(next.pass), doc["password"].as<const char*>());

  uint16_t port = 17878;
  if (!parseSecureEndpoint(endpoint, next.host, sizeof(next.host), &port)) return fail("secure endpoint required");
  next.port = port;
  copyField(next.token, sizeof(next.token), token);
  copyField(next.ca, sizeof(next.ca), ca);

  if (!validateBridgeIdentity(next, error, errorLen)) return false;
  char readyError[32];
  next.valid = validate(next, readyError, sizeof(readyError));
  if (!persist(next)) return fail("NVS write failed");
  return true;
}

bool bridgeConfigSaveFromFile(const char* path, char* error, size_t errorLen) {
  auto fail = [&](const char* msg) {
    if (error && errorLen) copyField(error, errorLen, msg);
    return false;
  };

  File f = LittleFS.open(path, "r");
  if (!f) return fail("open failed");

  JsonDocument doc;
  DeserializationError rc = deserializeJson(doc, f);
  f.close();
  if (rc) return fail("bad json");

  return bridgeConfigSaveJson(doc, error, errorLen);
}

bool bridgeConfigSaveWifi(const char* ssid, const char* pass, char* error, size_t errorLen) {
  auto fail = [&](const char* msg) {
    if (error && errorLen) copyField(error, errorLen, msg);
    return false;
  };
  if (!ssid || !ssid[0]) return fail("missing ssid");

  BridgeStoredConfig next = _cfg;
  if (!next.port) next.port = 17878;
  copyField(next.ssid, sizeof(next.ssid), ssid);
  copyField(next.pass, sizeof(next.pass), pass ? pass : "");

  char readyError[32];
  next.valid = validate(next, readyError, sizeof(readyError));
  if (!persist(next)) return fail("NVS write failed");
  if (!next.valid) return fail(readyError[0] ? readyError : "bridge missing");
  return true;
}

bool bridgeConfigClear() {
  Preferences prefs;
  if (!prefs.begin("buddy", false)) return false;
  const bool hadRecord = prefs.isKey(kBridgeRecordKey);
  const bool invalidated = !hadRecord || prefs.remove(kBridgeRecordKey);
  if (!invalidated) {
    prefs.end();
    return false;
  }
  removeLegacyBridgeKeys(prefs);
  prefs.end();
  memset(&_cfg, 0, sizeof(_cfg));
  _cfg.port = 17878;
  bump();
  return true;
}
