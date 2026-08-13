#include "bridge_config.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Preferences.h>

namespace {
BridgeStoredConfig _cfg = {};
uint32_t _cfgVersion = 1;

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

bool parseEndpoint(const char* endpoint, char* host, size_t hostLen, uint16_t* port) {
  if (!endpoint || !endpoint[0]) return false;
  const char* p = strstr(endpoint, "://");
  p = p ? p + 3 : endpoint;
  const char* slash = strchr(p, '/');
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
    int parsed = atoi(colon + 1);
    if (parsed < 1 || parsed > 65535) return false;
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
  if (!cfg.token[0]) return fail("missing token");
  if (strstr(cfg.token, "paste-token") || cfg.token[0] == '<') return fail("bad token");
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
  if (!cfg.token[0]) return fail("missing token");
  if (strstr(cfg.token, "paste-token") || cfg.token[0] == '<') return fail("bad token");
  return true;
}

void persist(const BridgeStoredConfig& cfg) {
  Preferences prefs;
  prefs.begin("buddy", false);
  prefs.putString("br_ssid", cfg.ssid);
  prefs.putString("br_pass", cfg.pass);
  prefs.putString("br_host", cfg.host);
  prefs.putUShort("br_port", cfg.port);
  prefs.putString("br_tok", cfg.token);
  prefs.putBool("br_valid", cfg.valid);
  prefs.end();

  _cfg = cfg;
  bump();
}

}  // namespace

void bridgeConfigLoad() {
  Preferences prefs;
  memset(&_cfg, 0, sizeof(_cfg));
  _cfg.port = 17877;
  prefs.begin("buddy", true);
  prefs.getString("br_ssid", _cfg.ssid, sizeof(_cfg.ssid));
  prefs.getString("br_pass", _cfg.pass, sizeof(_cfg.pass));
  prefs.getString("br_host", _cfg.host, sizeof(_cfg.host));
  prefs.getString("br_tok", _cfg.token, sizeof(_cfg.token));
  _cfg.port = prefs.getUShort("br_port", 17877);
  _cfg.valid = prefs.getBool("br_valid", false);
  prefs.end();

  char error[32];
  if (_cfg.valid && !validate(_cfg, error, sizeof(error))) _cfg.valid = false;
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

  persist(next);
  return true;
}

bool bridgeConfigSaveJson(JsonDocument& doc, char* error, size_t errorLen) {
  auto fail = [&](const char* msg) {
    if (error && errorLen) copyField(error, errorLen, msg);
    return false;
  };

  BridgeStoredConfig next = _cfg;
  if (!next.port) next.port = 17877;

  JsonObject wifi = doc["wifi"].as<JsonObject>();
  const char* ssid = wifi["ssid"].as<const char*>();
  if (!ssid) ssid = doc["ssid"].as<const char*>();
  if (ssid) copyField(next.ssid, sizeof(next.ssid), ssid);

  if (!wifi["password"].isNull()) copyField(next.pass, sizeof(next.pass), wifi["password"].as<const char*>());
  else if (!wifi["pass"].isNull()) copyField(next.pass, sizeof(next.pass), wifi["pass"].as<const char*>());
  else if (!doc["password"].isNull()) copyField(next.pass, sizeof(next.pass), doc["password"].as<const char*>());

  const char* host = doc["host"].as<const char*>();
  if (host) copyField(next.host, sizeof(next.host), host);

  if (!doc["port"].isNull()) {
    uint16_t port = doc["port"] | 0;
    if (port) next.port = port;
  }

  const char* endpoint = doc["endpoint"].as<const char*>();
  if (endpoint) {
    uint16_t port = next.port ? next.port : 17877;
    if (!parseEndpoint(endpoint, next.host, sizeof(next.host), &port)) return fail("bad endpoint");
    next.port = port;
  }

  const char* token = doc["token"].as<const char*>();
  if (token) copyField(next.token, sizeof(next.token), token);

  if (!validateBridgeIdentity(next, error, errorLen)) return false;
  char readyError[32];
  next.valid = validate(next, readyError, sizeof(readyError));
  persist(next);
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
  if (!next.port) next.port = 17877;
  copyField(next.ssid, sizeof(next.ssid), ssid);
  copyField(next.pass, sizeof(next.pass), pass ? pass : "");

  char readyError[32];
  next.valid = validate(next, readyError, sizeof(readyError));
  persist(next);
  if (!next.valid) return fail(readyError[0] ? readyError : "bridge missing");
  return true;
}

void bridgeConfigClear() {
  Preferences prefs;
  prefs.begin("buddy", false);
  prefs.remove("br_ssid");
  prefs.remove("br_pass");
  prefs.remove("br_host");
  prefs.remove("br_port");
  prefs.remove("br_tok");
  prefs.putBool("br_valid", false);
  prefs.end();
  memset(&_cfg, 0, sizeof(_cfg));
  _cfg.port = 17877;
  bump();
}
