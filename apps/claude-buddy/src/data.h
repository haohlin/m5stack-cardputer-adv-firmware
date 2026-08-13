#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "ble_bridge.h"
#include "xfer.h"

constexpr uint8_t ASSISTANT_LINE_N = 8;
constexpr uint8_t ASSISTANT_LINE_W = 80;

struct TamaState {
  uint8_t  sessionsTotal;
  uint8_t  sessionsRunning;
  uint8_t  sessionsWaiting;
  bool     recentlyCompleted;
  uint32_t tokensToday;
  uint32_t lastUpdated;
  char     msg[24];
  bool     connected;
  char     lines[8][92];
  uint8_t  nLines;
  uint16_t lineGen;          // bumps when lines change — lets UI reset scroll
  char     assistantLines[ASSISTANT_LINE_N][ASSISTANT_LINE_W];
  uint8_t  nAssistantLines;
  uint16_t assistantGen;     // bumps when a completed assistant turn arrives
  bool     assistantTruncated;
  bool     assistantFromSummary;
  uint32_t assistantUpdated;
  char     promptId[40];     // pending permission request ID; empty = no prompt
  char     promptTool[20];
  char     promptHint[44];
};

// ---------------------------------------------------------------------------
// Three modes, checked in priority order:
//   demo   → auto-cycle fake scenarios every 8s, ignore live data
//   live   → JSON arrived in the last 10s over USB or BT
//   asleep → no data, all zeros, "No Claude connected"
// ---------------------------------------------------------------------------

static uint32_t _lastLiveMs = 0;
static uint32_t _lastBtByteMs = 0;   // hasClient() lies; track actual BT traffic
static bool     _demoMode   = false;
static uint8_t  _demoIdx    = 0;
static uint32_t _demoNext   = 0;

struct _Fake { const char* n; uint8_t t,r,w; bool c; uint32_t tok; };
static const _Fake _FAKES[] = {
  {"asleep",0,0,0,false,0}, {"one idle",1,0,0,false,12000},
  {"busy",4,3,0,false,89000}, {"attention",2,1,1,false,45000},
  {"completed",1,0,0,true,142000},
};

inline void dataSetDemo(bool on) {
  _demoMode = on;
  if (on) { _demoIdx = 0; _demoNext = millis(); }
}
inline bool dataDemo() { return _demoMode; }

inline bool dataConnected() {
  return _lastLiveMs != 0 && (millis() - _lastLiveMs) <= 30000;
}

inline bool dataBtActive() {
  // Desktop's idle keepalive is ~10s; give it 1.5x headroom.
  return _lastBtByteMs != 0 && (millis() - _lastBtByteMs) <= 15000;
}

inline const char* dataScenarioName() {
  if (_demoMode) return _FAKES[_demoIdx].n;
  if (dataConnected()) return dataBtActive() ? "bt" : "usb";
  return "none";
}

// Set true once the bridge sends a time sync — until then the RTC may
// hold whatever was on the coin cell (or 2000-01-01 if it lost power).
static bool _rtcValid = false;
inline bool dataRtcValid() { return _rtcValid; }

static void _assistantAppendSegment(TamaState* out, const char* start, size_t len) {
  while (len && (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r')) { start++; len--; }
  while (len && (start[len - 1] == ' ' || start[len - 1] == '\t' ||
                 start[len - 1] == '\n' || start[len - 1] == '\r')) len--;
  if (len == 0) return;

  while (len) {
    if (out->nAssistantLines >= ASSISTANT_LINE_N) {
      out->assistantTruncated = true;
      return;
    }

    const size_t maxTake = ASSISTANT_LINE_W - 1;
    size_t take = len > maxTake ? maxTake : len;
    if (len > maxTake) {
      for (size_t i = take; i > 28; --i) {
        if (start[i - 1] == ' ') { take = i - 1; break; }
      }
    }

    char* dst = out->assistantLines[out->nAssistantLines++];
    memcpy(dst, start, take);
    dst[take] = 0;

    start += take;
    len -= take;
    while (len && *start == ' ') { start++; len--; }
  }
}

static void _assistantAppendTextRange(TamaState* out, const char* text, size_t len) {
  const char* seg = text;
  const char* end = text + len;
  for (const char* p = text; p <= end; ++p) {
    if (p == end || *p == '\n' || *p == '\r') {
      _assistantAppendSegment(out, seg, (size_t)(p - seg));
      if (p == end) break;
      while (p + 1 < end && (p[1] == '\n' || p[1] == '\r')) ++p;
      seg = p + 1;
    }
  }
}

static void _assistantAppendText(TamaState* out, const char* text) {
  if (!text) return;
  _assistantAppendTextRange(out, text, strlen(text));
}

static bool _assistantAppendDeviceSummary(TamaState* out, const char* text) {
  if (!text) return false;
  const char* start = strstr(text, "<device_summary>");
  if (!start) return false;
  start += strlen("<device_summary>");
  const char* end = strstr(start, "</device_summary>");
  if (!end || end <= start) return false;
  _assistantAppendTextRange(out, start, (size_t)(end - start));
  return true;
}

static bool _applyTurnEvent(JsonDocument& doc, TamaState* out) {
  const char* evt = doc["evt"];
  if (!evt) return false;
  if (strcmp(evt, "turn") != 0) return true;

  const char* role = doc["role"];
  if (!role || strcmp(role, "assistant") != 0) return true;

  uint8_t oldN = out->nAssistantLines;
  bool oldSummary = out->assistantFromSummary;
  out->nAssistantLines = 0;
  out->assistantTruncated = false;
  out->assistantFromSummary = false;

  JsonVariant contentValue = doc["content"];
  const char* singleText = doc["text"];
  if (!singleText && contentValue.is<const char*>()) singleText = contentValue.as<const char*>();
  if (singleText) {
    bool usedSummary = _assistantAppendDeviceSummary(out, singleText);
    if (!usedSummary) _assistantAppendText(out, singleText);
    if (out->nAssistantLines == 0) {
      out->nAssistantLines = oldN;
      out->assistantFromSummary = oldSummary;
      return true;
    }
    out->assistantFromSummary = usedSummary;
    out->assistantGen++;
    out->assistantUpdated = millis();
    return true;
  }

  JsonArray content = contentValue.as<JsonArray>();
  if (content.isNull()) {
    out->nAssistantLines = oldN;
    out->assistantFromSummary = oldSummary;
    return true;
  }

  bool usedSummary = false;
  for (JsonVariant v : content) {
    JsonObject block = v.as<JsonObject>();
    const char* type = block["type"];
    const char* text = block["text"];
    if (!text) text = v.as<const char*>();
    if (!text || (type && strcmp(type, "text") != 0)) continue;
    if (_assistantAppendDeviceSummary(out, text)) usedSummary = true;
    if (out->assistantTruncated) break;
  }

  if (!usedSummary) {
    for (JsonVariant v : content) {
      JsonObject block = v.as<JsonObject>();
      const char* type = block["type"];
      const char* text = block["text"];
      if (!text) text = v.as<const char*>();
      if (!text || (type && strcmp(type, "text") != 0)) continue;
      _assistantAppendText(out, text);
      if (out->assistantTruncated) break;
    }
  }

  if (out->nAssistantLines == 0) {
    out->nAssistantLines = oldN;
    out->assistantFromSummary = oldSummary;
    return true;
  }

  out->assistantFromSummary = usedSummary;
  out->assistantGen++;
  out->assistantUpdated = millis();
  return true;
}

static void _applyJson(const char* line, TamaState* out) {
  JsonDocument doc;
  if (deserializeJson(doc, line)) return;
  if (xferCommand(doc)) { _lastLiveMs = millis(); return; }
  if (_applyTurnEvent(doc, out)) { _lastLiveMs = millis(); return; }

  // Bridge sends {"time":[epoch_sec, tz_offset_sec]}; gmtime_r on the
  // adjusted epoch yields local components including weekday.
  JsonArray t = doc["time"];
  if (!t.isNull() && t.size() == 2) {
    time_t local = (time_t)t[0].as<uint32_t>() + (int32_t)t[1];
    struct tm lt; gmtime_r(&local, &lt);
    RTC_TimeTypeDef tm = { (uint8_t)lt.tm_hour, (uint8_t)lt.tm_min, (uint8_t)lt.tm_sec };
    RTC_DateTypeDef dt = { (uint8_t)lt.tm_wday, (uint8_t)(lt.tm_mon + 1),
                           (uint8_t)lt.tm_mday, (uint16_t)(lt.tm_year + 1900) };
    halRtcSetTime(&tm);
    halRtcSetDate(&dt);
    extern uint32_t _clkLastRead;
    _clkLastRead = 0;   // force re-read so _clkDt and _rtcValid agree
    _rtcValid = true;
    _lastLiveMs = millis();
    return;
  }

  out->sessionsTotal     = doc["total"]     | out->sessionsTotal;
  out->sessionsRunning   = doc["running"]   | out->sessionsRunning;
  out->sessionsWaiting   = doc["waiting"]   | out->sessionsWaiting;
  out->recentlyCompleted = doc["completed"] | false;
  uint32_t bridgeTokens = doc["tokens"] | 0;
  if (doc["tokens"].is<uint32_t>()) statsOnBridgeTokens(bridgeTokens);
  out->tokensToday = doc["tokens_today"] | out->tokensToday;
  const char* m = doc["msg"];
  if (m) { strncpy(out->msg, m, sizeof(out->msg)-1); out->msg[sizeof(out->msg)-1]=0; }
  JsonArray la = doc["entries"];
  if (!la.isNull()) {
    uint8_t n = 0;
    for (JsonVariant v : la) {
      if (n >= 8) break;
      const char* s = v.as<const char*>();
      strncpy(out->lines[n], s ? s : "", 91); out->lines[n][91]=0;
      n++;
    }
    if (n != out->nLines || (n > 0 && strcmp(out->lines[n-1], out->msg) != 0)) {
      out->lineGen++;
    }
    out->nLines = n;
  }
  JsonObject pr = doc["prompt"];
  if (!pr.isNull()) {
    const char* pid = pr["id"]; const char* pt = pr["tool"]; const char* ph = pr["hint"];
    strncpy(out->promptId,   pid ? pid : "", sizeof(out->promptId)-1);   out->promptId[sizeof(out->promptId)-1]=0;
    strncpy(out->promptTool, pt  ? pt  : "", sizeof(out->promptTool)-1); out->promptTool[sizeof(out->promptTool)-1]=0;
    strncpy(out->promptHint, ph  ? ph  : "", sizeof(out->promptHint)-1); out->promptHint[sizeof(out->promptHint)-1]=0;
  } else {
    out->promptId[0] = 0; out->promptTool[0] = 0; out->promptHint[0] = 0;
  }
  out->lastUpdated = millis();
  _lastLiveMs = millis();
}

template<size_t N>
struct _LineBuf {
  char buf[N];
  uint16_t len = 0;
  bool overflow = false;
  void reset() {
    len = 0;
    overflow = false;
  }
  void feed(Stream& s, TamaState* out, uint16_t budget = 256) {
    while (s.available() && budget--) {
      char c = s.read();
      if (c == '\n' || c == '\r') {
        if (!overflow && len > 0) { buf[len]=0; if (buf[0]=='{') _applyJson(buf, out); }
        len = 0;
        overflow = false;
      } else if (len < N-1) {
        buf[len++] = c;
      } else {
        overflow = true;
      }
    }
  }
};

static _LineBuf<1024> _usbLine;
static _LineBuf<4352> _btLine;

inline void dataSetBleEnabled(bool enabled) {
  if (!enabled) _btLine.reset();
  bleSetEnabled(enabled);
  if (!enabled) _btLine.reset();
}

inline void dataPoll(TamaState* out) {
  uint32_t now = millis();
  xferTick();

  if (_demoMode) {
    if (now >= _demoNext) { _demoIdx = (_demoIdx + 1) % 5; _demoNext = now + 8000; }
    const _Fake& s = _FAKES[_demoIdx];
    out->sessionsTotal=s.t; out->sessionsRunning=s.r; out->sessionsWaiting=s.w;
    out->recentlyCompleted=s.c; out->tokensToday=s.tok; out->lastUpdated=now;
    out->connected = true;
    snprintf(out->msg, sizeof(out->msg), "demo: %s", s.n);
    return;
  }

  _usbLine.feed(Serial, out);
  // BLE ring buffer is drained manually since it's not a Stream.
  uint16_t bleBudget = 768;
  uint32_t bleStart = millis();
  while (bleAvailable() && bleBudget--) {
    int c = bleRead();
    if (c < 0) break;
    _lastBtByteMs = millis();
    if (c == '\n' || c == '\r') {
      if (!_btLine.overflow && _btLine.len > 0) {
        _btLine.buf[_btLine.len] = 0;
        if (_btLine.buf[0] == '{') _applyJson(_btLine.buf, out);
      }
      _btLine.len = 0;
      _btLine.overflow = false;
    } else if (_btLine.len < sizeof(_btLine.buf) - 1) {
      _btLine.buf[_btLine.len++] = (char)c;
    } else {
      _btLine.overflow = true;
    }
    if (millis() - bleStart > 8) break;
  }

  out->connected = dataConnected();
  if (!out->connected) {
    out->sessionsTotal=0; out->sessionsRunning=0; out->sessionsWaiting=0;
    out->recentlyCompleted=false; out->lastUpdated=now;
    strncpy(out->msg, "No Claude connected", sizeof(out->msg)-1);
    out->msg[sizeof(out->msg)-1]=0;
  }
}
