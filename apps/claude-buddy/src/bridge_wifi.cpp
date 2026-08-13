#include "bridge_wifi.h"
#include "bridge_config.h"

#if defined(CARDPUTER_ADV) && __has_include("bridge_config.local.h")
#include "bridge_config.local.h"
#endif

#if defined(CARDPUTER_ADV)

#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <esp_system.h>

#ifndef CARDPUTER_BRIDGE_WIFI_PASS
#define CARDPUTER_BRIDGE_WIFI_PASS ""
#endif

#ifndef CARDPUTER_BRIDGE_PORT
#define CARDPUTER_BRIDGE_PORT 17878
#endif

#ifndef CARDPUTER_BRIDGE_FW_LABEL
#define CARDPUTER_BRIDGE_FW_LABEL "cardputer-adv-dev"
#endif

namespace {
enum WifiDiagPhase : uint8_t {
  WIFI_DIAG_NONE = 0,
  WIFI_DIAG_BOOT,
  WIFI_DIAG_OFF,
  WIFI_DIAG_NO_CONFIG,
  WIFI_DIAG_CONFIG_NEW,
  WIFI_DIAG_STOP_BEFORE,
  WIFI_DIAG_STOP_AFTER,
  WIFI_DIAG_MODE_BEFORE,
  WIFI_DIAG_MODE_AFTER,
  WIFI_DIAG_BEGIN_BEFORE,
  WIFI_DIAG_BEGIN_AFTER,
  WIFI_DIAG_WAIT_IP,
  WIFI_DIAG_GOT_IP,
  WIFI_DIAG_WS_BEGIN,
  WIFI_DIAG_WS_ONLINE,
  WIFI_DIAG_WS_TEXT,
};

RTC_DATA_ATTR uint32_t _diagMagic = 0;
RTC_DATA_ATTR uint32_t _diagBootCount = 0;
RTC_DATA_ATTR uint8_t _diagRtcPhase = WIFI_DIAG_NONE;
RTC_DATA_ATTR uint32_t _diagRtcPhaseMs = 0;

WebSocketsClient _ws;
bool _wifiStarted = false;
bool _wsStarted = false;
bool _wsOnline = false;
bool _eventsRegistered = false;
uint32_t _lastWifiBeginMs = 0;
uint32_t _nextStateMs = 0;
uint32_t _seenConfigVersion = 0;
uint8_t _bootPhase = WIFI_DIAG_NONE;
uint8_t _resetReason = 0;
uint8_t _lastDisconnectReason = 0;
char _deviceName[24] = "Claude";
char _status[24] = "wifi off";
char _lastMessage[96] = "";
char _lastEvent[20] = "-";
char _lastIp[16] = "-";
char _authHeader[128] = "";

const char* phaseName(uint8_t phase) {
  switch (phase) {
    case WIFI_DIAG_BOOT: return "boot";
    case WIFI_DIAG_OFF: return "off";
    case WIFI_DIAG_NO_CONFIG: return "no-cfg";
    case WIFI_DIAG_CONFIG_NEW: return "cfg-new";
    case WIFI_DIAG_STOP_BEFORE: return "stop>";
    case WIFI_DIAG_STOP_AFTER: return "stop-ok";
    case WIFI_DIAG_MODE_BEFORE: return "mode>";
    case WIFI_DIAG_MODE_AFTER: return "mode-ok";
    case WIFI_DIAG_BEGIN_BEFORE: return "begin>";
    case WIFI_DIAG_BEGIN_AFTER: return "begin-ok";
    case WIFI_DIAG_WAIT_IP: return "wait-ip";
    case WIFI_DIAG_GOT_IP: return "got-ip";
    case WIFI_DIAG_WS_BEGIN: return "ws>";
    case WIFI_DIAG_WS_ONLINE: return "ws-ok";
    case WIFI_DIAG_WS_TEXT: return "ws-text";
    default: return "-";
  }
}

const char* resetName(uint8_t reason) {
  switch ((esp_reset_reason_t)reason) {
    case ESP_RST_POWERON: return "power";
    case ESP_RST_EXT: return "ext";
    case ESP_RST_SW: return "sw";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "int-wdt";
    case ESP_RST_TASK_WDT: return "task-wdt";
    case ESP_RST_WDT: return "wdt";
    case ESP_RST_DEEPSLEEP: return "sleep";
    case ESP_RST_BROWNOUT: return "brown";
    case ESP_RST_SDIO: return "sdio";
    default: return "unknown";
  }
}

void markPhase(WifiDiagPhase phase) {
  _diagRtcPhase = phase;
  _diagRtcPhaseMs = millis();
}

void setStatus(const char* s) {
  strncpy(_status, s, sizeof(_status) - 1);
  _status[sizeof(_status) - 1] = 0;
}

void setLast(const char* s) {
  strncpy(_lastMessage, s, sizeof(_lastMessage) - 1);
  _lastMessage[sizeof(_lastMessage) - 1] = 0;
}

void setEvent(const char* s) {
  strncpy(_lastEvent, s ? s : "-", sizeof(_lastEvent) - 1);
  _lastEvent[sizeof(_lastEvent) - 1] = 0;
}

void sendHello() {
  char msg[160];
  snprintf(msg, sizeof(msg),
           "{\"v\":1,\"type\":\"hello\",\"device\":\"%s\",\"fw\":\"%s\"}",
           _deviceName, CARDPUTER_BRIDGE_FW_LABEL);
  _ws.sendTXT(msg);
}

void handleText(uint8_t* payload, size_t length) {
  JsonDocument doc;
  if (deserializeJson(doc, payload, length)) return;

  const char* type = doc["type"] | "";
  if (strcmp(type, "bridge.status") == 0) {
    bool connected = doc["connected"] | false;
    setStatus(connected ? "bridge ok" : "bridge wait");
    setLast(connected ? "bridge connected" : "bridge waiting");
    return;
  }

  if (strcmp(type, "display.summary") == 0) {
    const char* title = doc["title"] | "Claude";
    const char* text = doc["text"] | "";
    char line[96];
    snprintf(line, sizeof(line), "%.16s: %.70s", title, text);
    setLast(line);
    return;
  }

  if (strcmp(type, "prompt.result") == 0) {
    const char* status = doc["status"] | "queued";
    char line[96];
    snprintf(line, sizeof(line), "prompt %s", status);
    setLast(line);
    return;
  }

  if (strcmp(type, "question.request") == 0) {
    setLast("question from bridge");
  }
}

void webSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      _wsOnline = true;
      markPhase(WIFI_DIAG_WS_ONLINE);
      setStatus("bridge ok");
      setLast("bridge connected");
      sendHello();
      break;
    case WStype_DISCONNECTED:
      _wsOnline = false;
      setStatus(WiFi.status() == WL_CONNECTED ? "bridge wait" : "wifi wait");
      break;
    case WStype_TEXT:
      markPhase(WIFI_DIAG_WS_TEXT);
      handleText(payload, length);
      break;
    default:
      break;
  }
}

void stopBridge() {
  if (_wsStarted) {
    _ws.disconnect();
    _wsStarted = false;
  }
  _wsOnline = false;
}

void wifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_START:
      setEvent("sta-start");
      break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      setEvent("connected");
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP: {
      markPhase(WIFI_DIAG_GOT_IP);
      setEvent("got-ip");
      IPAddress ip(info.got_ip.ip_info.ip.addr);
      snprintf(_lastIp, sizeof(_lastIp), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
      break;
    }
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      _lastDisconnectReason = info.wifi_sta_disconnected.reason;
      setEvent("disc");
      _lastIp[0] = '-';
      _lastIp[1] = 0;
      break;
    case ARDUINO_EVENT_WIFI_STA_STOP:
      setEvent("sta-stop");
      break;
    case ARDUINO_EVENT_WIFI_STA_LOST_IP:
      setEvent("lost-ip");
      _lastIp[0] = '-';
      _lastIp[1] = 0;
      break;
    default:
      break;
  }
}

bool runtimeConfig(BridgeStoredConfig* out) {
  if (bridgeConfigValid()) {
    if (out) *out = bridgeConfig();
    return true;
  }
#if defined(CARDPUTER_BRIDGE_WIFI_SSID) && defined(CARDPUTER_BRIDGE_CA)
  if (out) {
    memset(out, 0, sizeof(*out));
    out->valid = true;
    strncpy(out->ssid, CARDPUTER_BRIDGE_WIFI_SSID, sizeof(out->ssid) - 1);
    strncpy(out->pass, CARDPUTER_BRIDGE_WIFI_PASS, sizeof(out->pass) - 1);
    strncpy(out->host, CARDPUTER_BRIDGE_HOST, sizeof(out->host) - 1);
    out->port = CARDPUTER_BRIDGE_PORT;
    strncpy(out->token, CARDPUTER_BRIDGE_TOKEN, sizeof(out->token) - 1);
    strncpy(out->ca, CARDPUTER_BRIDGE_CA, sizeof(out->ca) - 1);
  }
  return true;
#else
  return false;
#endif
}
}  // namespace

void bridgeWifiInit(const char* deviceName) {
  constexpr uint32_t MAGIC = 0xCADA7102;
  if (_diagMagic != MAGIC) {
    _diagMagic = MAGIC;
    _diagBootCount = 0;
    _diagRtcPhase = WIFI_DIAG_NONE;
    _diagRtcPhaseMs = 0;
  }
  _bootPhase = _diagRtcPhase;
  _resetReason = (uint8_t)esp_reset_reason();
  _diagBootCount++;
  markPhase(WIFI_DIAG_BOOT);
  bridgeConfigLoad();
  if (deviceName && deviceName[0]) {
    strncpy(_deviceName, deviceName, sizeof(_deviceName) - 1);
    _deviceName[sizeof(_deviceName) - 1] = 0;
  }
  if (!_eventsRegistered) {
    WiFi.onEvent(wifiEvent);
    _eventsRegistered = true;
  }
}

void bridgeWifiPoll(bool enabled, bool bleLinked, const char* pageName, uint8_t batteryPct) {
  uint32_t now = millis();
  BridgeStoredConfig cfg;
  if (!enabled) {
    stopBridge();
    if (_wifiStarted) {
      markPhase(WIFI_DIAG_STOP_BEFORE);
      WiFi.disconnect(true, false);
      WiFi.mode(WIFI_OFF);
      markPhase(WIFI_DIAG_STOP_AFTER);
      _wifiStarted = false;
    }
    markPhase(WIFI_DIAG_OFF);
    setStatus("wifi off");
    return;
  }

  if (!runtimeConfig(&cfg)) {
    stopBridge();
    if (_wifiStarted) {
      markPhase(WIFI_DIAG_STOP_BEFORE);
      WiFi.disconnect(true, false);
      WiFi.mode(WIFI_OFF);
      markPhase(WIFI_DIAG_STOP_AFTER);
      _wifiStarted = false;
    }
    markPhase(WIFI_DIAG_NO_CONFIG);
    setStatus("no config");
    return;
  }

  uint32_t cfgVersion = bridgeConfigVersion();
  if (_seenConfigVersion != cfgVersion) {
    stopBridge();
    if (_wifiStarted) {
      markPhase(WIFI_DIAG_STOP_BEFORE);
      WiFi.disconnect(true, false);
      markPhase(WIFI_DIAG_STOP_AFTER);
      _wifiStarted = false;
    }
    _seenConfigVersion = cfgVersion;
    markPhase(WIFI_DIAG_CONFIG_NEW);
    setStatus("config new");
  }

  if (WiFi.status() != WL_CONNECTED) {
    stopBridge();
    _wsOnline = false;
    if (!_wifiStarted || now - _lastWifiBeginMs > 10000) {
      markPhase(WIFI_DIAG_MODE_BEFORE);
      WiFi.mode(WIFI_STA);
      markPhase(WIFI_DIAG_MODE_AFTER);
      markPhase(WIFI_DIAG_BEGIN_BEFORE);
      WiFi.begin(cfg.ssid, cfg.pass);
      markPhase(WIFI_DIAG_BEGIN_AFTER);
      _wifiStarted = true;
      _lastWifiBeginMs = now;
      setStatus("wifi join");
    } else {
      markPhase(WIFI_DIAG_WAIT_IP);
      setStatus("wifi wait");
    }
    return;
  }

  if (!_wsStarted) {
    // `beginSSL` without a CA deliberately calls insecure TLS in this
    // dependency. Only the CA-validated variant is allowed for Wi-Fi bridge.
    snprintf(_authHeader, sizeof(_authHeader), "Authorization: Bearer %s\r\n", cfg.token);
    markPhase(WIFI_DIAG_WS_BEGIN);
    _ws.setExtraHeaders(_authHeader);
    _ws.beginSslWithCA(cfg.host, cfg.port, "/device", cfg.ca);
    _ws.onEvent(webSocketEvent);
    _ws.setReconnectInterval(5000);
    _wsStarted = true;
    _wsOnline = false;
    _nextStateMs = 0;
    setStatus("bridge dial");
  }

  _ws.loop();

  if (_wsOnline && (int32_t)(now - _nextStateMs) >= 0) {
    char msg[192];
    snprintf(msg, sizeof(msg),
             "{\"v\":1,\"type\":\"state\",\"battery\":%u,\"ble\":%s,\"page\":\"%s\",\"heap\":%u}",
             batteryPct,
             bleLinked ? "true" : "false",
             pageName ? pageName : "home",
             ESP.getFreeHeap());
    _ws.sendTXT(msg);
    _nextStateMs = now + 5000;
  }
}

bool bridgeWifiConfigured() { return runtimeConfig(nullptr); }
bool bridgeWifiConnected() { return _wsOnline; }
const char* bridgeWifiStatus() { return _status; }
const char* bridgeWifiLastMessage() { return _lastMessage; }
const char* bridgeWifiDiagPhase() { return phaseName(_diagRtcPhase); }
const char* bridgeWifiDiagBootPhase() { return phaseName(_bootPhase); }
const char* bridgeWifiDiagReset() { return resetName(_resetReason); }
const char* bridgeWifiDiagEvent() { return _lastEvent; }
const char* bridgeWifiDiagIp() { return _lastIp; }
uint8_t bridgeWifiDiagDisconnectReason() { return _lastDisconnectReason; }
uint32_t bridgeWifiDiagBootCount() { return _diagBootCount; }

#else

void bridgeWifiInit(const char*) {}
void bridgeWifiPoll(bool, bool, const char*, uint8_t) {}
bool bridgeWifiConfigured() { return false; }
bool bridgeWifiConnected() { return false; }
const char* bridgeWifiStatus() { return "no config"; }
const char* bridgeWifiLastMessage() { return ""; }
const char* bridgeWifiDiagPhase() { return "-"; }
const char* bridgeWifiDiagBootPhase() { return "-"; }
const char* bridgeWifiDiagReset() { return "-"; }
const char* bridgeWifiDiagEvent() { return "-"; }
const char* bridgeWifiDiagIp() { return "-"; }
uint8_t bridgeWifiDiagDisconnectReason() { return 0; }
uint32_t bridgeWifiDiagBootCount() { return 0; }

#endif
