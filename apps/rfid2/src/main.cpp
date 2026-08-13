#include <Arduino.h>
#include <M5Cardputer.h>
#include <MFRC522_I2C.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <math.h>
#include <set>

#include "security_utils.h"

namespace {
constexpr char kFwName[] = "m5stack-cardputer-adv-rfid2";
// kFwVersion is the human-readable release tag. The boot splash and serial
// output also read the runtime app description (esp_app_get_description())
// which is always accurate for the actually-running binary regardless of
// which OTA slot it was installed to.
constexpr char kFwVersion[] = "1.5.9";
constexpr uint8_t kRfidI2cAddress = 0x28;
constexpr int kRfidResetPin = -1;
constexpr int kGroveSda = 2;
constexpr int kGroveScl = 1;
constexpr uint32_t kI2cFrequency = 400000;
constexpr uint32_t kHeartbeatMs = 3000;
constexpr size_t kCommandMax = 96;
constexpr uint8_t kClassic1kBlocks = 64;
constexpr uint8_t kClassic1kSectors = 16;
constexpr uint8_t kClassicBlockSize = 16;
constexpr uint8_t kClassicKeySize = 6;
constexpr uint8_t kDumpSlotCount = 4;

// microSD lives on the Cardputer-Adv SPI bus (see M5Unified _pin_table_sd):
// CLK=40, CMD/MOSI=14, D0/MISO=39, D3/CS=12. No overlap with the RFID2 I2C
// Grove pins (SDA=G2, SCL=G1).
constexpr int kSdSckPin = 40;
constexpr int kSdMosiPin = 14;
constexpr int kSdMisoPin = 39;
constexpr int kSdCsPin = 12;
constexpr uint32_t kSdFrequencyHz = 4000000;
constexpr char kSlotDir[] = "/rfid";
constexpr char kKeyFilePath[] = "/rfid/keys.txt";

// ---- UI palette (RGB565) -------------------------------------------------
// "Tactical terminal" theme: near-black field, slate chrome, and a per-mode
// accent (READ=green, WRITE=amber, CLONE=red because it rewrites the UID).
constexpr uint16_t kColBg     = 0x0000;  // black field
constexpr uint16_t kColBar    = 0x2104;  // dark slate chrome
constexpr uint16_t kColText   = 0xFFFF;  // white
constexpr uint16_t kColDim     = 0x738E;  // muted gray
constexpr uint16_t kColRead    = 0x2FEC;  // green
constexpr uint16_t kColWrite   = 0xFD20;  // amber
constexpr uint16_t kColClone   = 0xF986;  // red/pink (destructive)
constexpr uint16_t kColArmed   = 0xFFE0;  // yellow (action armed)
constexpr uint16_t kColInfo    = 0x5D7F;  // cyan (data present)
constexpr uint16_t kColOk      = 0x2FEC;  // green (success)
constexpr int kGlyphW = 6;                // built-in font cell width at size 1

// Returns the version string baked into this build. kFwVersion IS the running
// version — it's a compile-time constant that is bumped on every release, and
// the deploy script always pushes the matching bin to the launcher SD so the
// installed binary is always the one with the correct tag.
static const char* runningVersion() { return kFwVersion; }

MFRC522_I2C rfid(kRfidI2cAddress, kRfidResetPin);
bool rfidReady = false;
uint32_t lastStatusMs = 0;
String lastI2cScan = "unknown";
String serialCommand;

struct LastCard {
  bool valid = false;
  String uid;
  uint8_t sak = 0;
  String typeName;
  uint32_t seenAtMs = 0;
};

LastCard lastCard;

struct StoredDump {
  bool valid = false;
  uint32_t version = 0;
  String sourceUid;
  String sourceType;
  uint32_t storedAtMs = 0;
  uint8_t sourceUidSize = 0;
  uint8_t blocksRead = 0;
  uint8_t sectorsRead = 0;
  uint8_t sectorsFailed = 0;
  bool readable[kClassic1kBlocks] = {};
  byte data[kClassic1kBlocks][kClassicBlockSize] = {};
  // Per-sector Key A that successfully authenticated during the read. Used so a
  // later write/clone can re-auth non-default-key cards and rebuild trailers.
  bool keyKnown[kClassic1kSectors] = {};
  byte keyA[kClassic1kSectors][kClassicKeySize] = {};
};

enum class UiMode : uint8_t {
  Read = 0,
  Write = 1,
  Clone = 2,
};
constexpr uint8_t kUiModeCount = 3;

enum class PendingAction : uint8_t {
  None = 0,
  ReadOverwrite,
  WriteSlot,
  CloneSlot,
  ClearSlot,
};

StoredDump storedDumps[kDumpSlotCount];
UiMode selectedMode = UiMode::Read;
uint8_t selectedSlot = 0;
// Which HUD row arrow keys steer: 0 = mode row (READ/WRITE/CLONE), 1 = slot row.
// Up/Down pick the row; Left/Right move within the focused row.
uint8_t focusRow = 0;
constexpr uint8_t kFocusModeRow = 0;
constexpr uint8_t kFocusSlotRow = 1;

// Options overlay (opened with M): screen brightness + sound (default OFF).
bool optionsOpen = false;
uint8_t optionIndex = 0;            // 0 = brightness, 1 = sound
constexpr uint8_t kOptionCount = 2;
constexpr uint8_t kBrightnessLevels = 5;
uint8_t brightnessLevel = 3;        // 0..4 -> dim..max
constexpr uint8_t kSoundMax = 5;    // 5 volume steps (shown as 5 segments)
uint8_t soundLevel = 0;             // 0 = OFF (default), 1..5 = quiet..loud
uint32_t nextDumpVersion = 1;
PendingAction pendingAction = PendingAction::None;
UiMode armedMode = UiMode::Read;
uint8_t armedSlot = 0;
uint32_t armedUntilMs = 0;
bool writeDone = false;

// Auto-trigger guard: tracks the UID of the last card that triggered an
// auto-read/auto-arm so holding the card steady doesn't re-trigger on every
// poll. Reset whenever the user navigates (mode/slot change) or removes the card.
String lastAutoTriggeredUid;

// Last-seen status bar values — if any change the bar redraws in real-time.
struct StatusCache {
  bool usbPlugged = false;
  bool usbCdc = false;
  int8_t batLevel = -2;   // -2 = uninitialised
  bool charging = false;
} statusCache;

// True while a full-screen result/notice (drawLines) is showing. The live card
// preview must not repaint the home HUD over it, or read/write results would
// vanish within ~50ms. Cleared as soon as the home HUD is drawn (any navigation
// keypress), which resumes the live preview.
bool resultScreenActive = false;

// Multi-key dictionary (Bruce-style): std::set<String> of uppercase 12-hex-char
// keys, auto-deduplicating and capped by RFID_MAX_MIFARE_KEYS. Seeded in
// setup(); persists to kKeyFilePath on SD.
std::set<String> mifareKeys;

// microSD presence; when false the firmware still runs fully with RAM-only slots.
bool sdReady = false;
// IMPORTANT: the Cardputer-Adv DISPLAY owns SPI3_HOST (Arduino HSPI) — see M5GFX
// autodetect (bus_cfg.spi_host = SPI3_HOST for board_M5CardputerADV). The microSD
// MUST therefore use the other host, FSPI (SPI2_HOST); using HSPI here collides
// with the panel bus and makes SD.begin() fail.
SPIClass sdSpi(FSPI);

// CLONE writes sector trailers (access bits + keys) when enabled. Off by default
// because, like the community MFRC522 cloners, trailer writes are risky: a bad
// access-bit pattern can permanently lock a sector. Reconstructed trailers reuse
// the known working Key A so the destination stays accessible.
bool cloneWriteTrailers = false;

// Picks a banner accent from result-screen keywords so success/failure/armed
// states read at a glance without the caller passing a color.
uint16_t bannerAccentFor(const String& title) {
  String t = title;
  t.toLowerCase();
  if (t.indexOf("fail") >= 0 || t.indexOf("block") >= 0 || t.indexOf("unsupported") >= 0 || t.indexOf("not ") >= 0) return kColClone;
  if (t.indexOf("armed") >= 0 || t.indexOf("has data") >= 0) return kColArmed;
  if (t.indexOf("clone") >= 0) return kColClone;
  if (t.indexOf("saved") >= 0 || t.indexOf("complete") >= 0 || t.indexOf("written") >= 0) return kColOk;
  return kColInfo;
}

void drawBanner(const String& title, uint16_t accent) {
  auto& d = M5.Display;
  d.fillRect(0, 0, d.width(), 16, accent);
  d.fillRect(0, 16, d.width(), 2, kColBg);
  d.setTextSize(1);
  d.setTextColor(kColBg, accent);
  d.setCursor(5, 4);
  d.print(title);
}

// Full-screen transient result/notice screen: accent banner + up to 3 body lines.
void drawLines(const char* title, const String& line1 = "", const String& line2 = "", const String& line3 = "") {
  auto& d = M5.Display;
  d.fillScreen(kColBg);
  resultScreenActive = true;
  drawBanner(title, bannerAccentFor(title));
  d.setTextSize(1);
  int y = 26;
  const String lines[3] = {line1, line2, line3};
  for (const String& line : lines) {
    if (line.length()) {
      d.setTextColor(kColText, kColBg);
      d.setCursor(6, y);
      d.print(line);
    }
    y += 13;
  }
}

const char* modeName(UiMode mode) {
  switch (mode) {
    case UiMode::Read: return "READ";
    case UiMode::Write: return "WRITE";
    case UiMode::Clone: return "CLONE";
  }
  return "READ";
}

uint16_t accentForMode(UiMode mode) {
  switch (mode) {
    case UiMode::Read: return kColRead;
    case UiMode::Write: return kColWrite;
    case UiMode::Clone: return kColClone;
  }
  return kColText;
}

String slotTitle(uint8_t slot) {
  return "Slot " + String(slot + 1);
}

String slotSummary(uint8_t slot) {
  const StoredDump& dump = storedDumps[slot];
  if (!dump.valid) {
    return slotTitle(slot) + ": empty";
  }
  return slotTitle(slot) + " v" + String(dump.version) + " " + String(dump.blocksRead) + " blocks";
}

String selectedSummary() {
  String summary = String(modeName(selectedMode)) + " " + slotTitle(selectedSlot);
  const StoredDump& dump = storedDumps[selectedSlot];
  if (dump.valid) {
    summary += " v" + String(dump.version);
  } else {
    summary += " empty";
  }
  return summary;
}

bool isArmedForSelection() {
  return pendingAction != PendingAction::None && armedMode == selectedMode && armedSlot == selectedSlot &&
         rfidArmStillValid(millis(), armedUntilMs);
}

bool isArmedAction(PendingAction action) {
  return pendingAction == action && armedMode == selectedMode && armedSlot == selectedSlot &&
         rfidArmStillValid(millis(), armedUntilMs);
}

void cancelArm() {
  pendingAction = PendingAction::None;
  armedUntilMs = 0;
}

void resetAutoTrigger() {
  lastAutoTriggeredUid = "";
}

void applyBrightness() {
  static const uint8_t lut[kBrightnessLevels] = {25, 70, 120, 180, 255};
  const uint8_t lvl = brightnessLevel < kBrightnessLevels ? brightnessLevel : kBrightnessLevels - 1;
  M5.Display.setBrightness(lut[lvl]);
}

// Short UI feedback tone at the configured volume level (0 = off, the default).
void beep(uint16_t freq = 2200, uint16_t ms = 20) {
  if (soundLevel == 0) return;
  static const uint8_t vol[kSoundMax + 1] = {0, 50, 100, 150, 205, 255};
  M5.Speaker.setVolume(vol[soundLevel <= kSoundMax ? soundLevel : kSoundMax]);
  M5.Speaker.tone(freq, ms);
}

// Top status bar: RFID link + SD/key state on the left; USB, charging, and a
// battery gauge on the right.
// Reads the current USB/battery/charging state and caches it so the live-update
// watcher in loop() knows whether a redraw is actually needed.
struct StatusSnapshot {
  bool usbPlugged;   // physical USB cable present (Serial.isPlugged)
  bool usbCdc;       // host terminal actively connected (Serial.isConnected)
  int8_t batLevel;   // 0-100 or -1
  bool charging;     // inferred: plugged AND batLevel not at 100
};

// Battery level tracker for charging-direction detection (Cardputer-Adv has no
// PMIC so isCharging() returns charge_unknown; charging is detected by observing
// the battery level rising between two readings 4 s apart).
namespace {
  int8_t _batPrev = -1;
  uint32_t _batPrevMs = 0;
  bool _inferredCharging = false;
}

StatusSnapshot readStatus() {
  StatusSnapshot s;
  s.usbPlugged  = Serial.isPlugged();
  s.usbCdc      = Serial.isConnected();
  s.batLevel    = (int8_t)M5.Power.getBatteryLevel();

  // Update the charging inference every ~4 s.
  const uint32_t now = millis();
  if (_batPrev < 0) {
    _batPrev = s.batLevel;
    _batPrevMs = now;
  } else if ((uint32_t)(now - _batPrevMs) >= 4000) {
    if (s.batLevel > _batPrev) {
      _inferredCharging = true;
    } else if (s.batLevel < _batPrev || !s.usbPlugged) {
      _inferredCharging = false;
    }
    _batPrev = s.batLevel;
    _batPrevMs = now;
  }
  // Also show charging if USB is plugged and battery < 100 (covers the initial
  // period before the first 4 s reading, and USB-power-bank type chargers).
  s.charging = _inferredCharging || (s.usbPlugged && s.batLevel >= 0 && s.batLevel < 100);
  return s;
}

void drawStatusBar() {
  auto& d = M5.Display;
  const int W = d.width();
  d.fillRect(0, 0, W, 13, kColBar);

  // Left: RFID link, SD/RAM, key count, trailer flag.
  d.setTextColor(rfidReady ? kColOk : kColClone, kColBar);
  d.setCursor(3, 3);
  d.print(rfidReady ? "RFID2" : "NO RF");
  d.setTextColor(sdReady ? kColInfo : kColDim, kColBar);
  d.setCursor(40, 3);
  d.print(sdReady ? "SD" : "RAM");
  d.setTextColor(kColText, kColBar);
  d.setCursor(60, 3);
  d.printf("K%u", (unsigned)mifareKeys.size());
  if (cloneWriteTrailers) {
    d.setTextColor(kColWrite, kColBar);
    d.setCursor(60 + (mifareKeys.size() >= 10 ? 24 : 18), 3);
    d.print("T");
  }

  // Right: battery gauge + ⚡ charge bolt + USB/CDC tag.
  // Serial.isPlugged() detects physical cable via USB-JTAG SOF interrupt —
  // true even when no terminal is open, and it updates in real-time in loop().
  const StatusSnapshot st = readStatus();
  statusCache.usbPlugged = st.usbPlugged;
  statusCache.usbCdc     = st.usbCdc;
  statusCache.batLevel   = st.batLevel;
  statusCache.charging   = st.charging;

  const int lvl = st.batLevel;
  const uint16_t bc = lvl < 0 ? kColDim : (lvl < 15 ? kColClone : (lvl < 40 ? kColWrite : kColOk));
  const int bx = W - 20;
  d.drawRect(bx, 3, 15, 8, bc);
  d.fillRect(bx + 15, 5, 2, 4, bc);  // battery nub
  if (lvl >= 0) {
    const int fw = (13 * lvl) / 100;
    if (fw > 0) d.fillRect(bx + 1, 4, fw, 6, st.charging ? kColArmed : bc);
  }
  String pct = lvl < 0 ? String("--") : String(lvl);
  int px = bx - (int)pct.length() * kGlyphW - 3;
  d.setTextColor(bc, kColBar);
  d.setCursor(px, 3);
  d.print(pct);
  if (st.charging) {  // ⚡ bolt: USB plugged + not full
    const int zx = px - 7;
    d.drawLine(zx + 3, 2, zx, 6, kColArmed);
    d.drawLine(zx, 6, zx + 4, 6, kColArmed);
    d.drawLine(zx + 4, 6, zx + 1, 11, kColArmed);
    px = zx;
  }
  // "CDC" = terminal open (more specific), "USB" = cable plugged but no terminal.
  if (st.usbCdc) {
    d.setTextColor(kColOk, kColBar);
    d.setCursor(px - 3 * kGlyphW - 4, 3);
    d.print("CDC");
  } else if (st.usbPlugged) {
    d.setTextColor(kColInfo, kColBar);
    d.setCursor(px - 3 * kGlyphW - 4, 3);
    d.print("USB");
  }
}

// Home HUD: status bar, mode tabs, slot strip, content panel, and a contextual
// action bar. Mode accent (green/amber/red) and the yellow "armed" bar make the
// current state and the next keypress obvious at a glance on the 240x135 screen.
void drawHome(const String& footer = "") {
  const bool armed = isArmedForSelection();
  String action;
  switch (selectedMode) {
    case UiMode::Read: action = "ENTER read into slot"; break;
    case UiMode::Write: action = "ENTER arm write"; break;
    case UiMode::Clone: action = "ENTER arm CLONE (UID)"; break;
  }
  if (pendingAction == PendingAction::ReadOverwrite && armed) {
    action = "ENTER overwrite  ESC keep";
  } else if (pendingAction == PendingAction::WriteSlot && armed) {
    action = "ENTER confirm write";
  } else if (pendingAction == PendingAction::CloneSlot && armed) {
    action = "ENTER CLONE incl UID!";
  } else if (pendingAction == PendingAction::ClearSlot && armed) {
    action = "DEL confirm clear";
  }

  auto& d = M5.Display;
  const int W = d.width();
  const int H = d.height();
  const uint16_t accent = accentForMode(selectedMode);
  d.fillScreen(kColBg);
  resultScreenActive = false;
  d.setTextSize(1);

  drawStatusBar();

  // Mode tabs: READ / WRITE / CLONE, active one filled with its accent.
  const int tabsY = 18, tabsH = 15;
  const int tabW = W / kUiModeCount;
  for (uint8_t i = 0; i < kUiModeCount; ++i) {
    const char* nm = modeName((UiMode)i);
    const uint16_t acc = accentForMode((UiMode)i);
    const int x = i * tabW;
    if ((uint8_t)selectedMode == i) {
      // Selected mode always keeps its accent colour; focus is shown by the left
      // caret (not by greying), so the active mode stays clearly readable.
      d.fillRect(x + 2, tabsY, tabW - 4, tabsH, acc);
      d.setTextColor(kColBg, acc);
    } else {
      d.drawRect(x + 2, tabsY, tabW - 4, tabsH, kColDim);
      d.setTextColor(acc, kColBg);
    }
    d.setCursor(x + (tabW - (int)strlen(nm) * kGlyphW) / 2, tabsY + 4);
    d.print(nm);
  }

  // Slot strip: filled = has data (cyan), empty = slate. Selected slot is boxed
  // in the mode accent.
  const int slotY = 38, slotH = 16;
  const int slotW = W / kDumpSlotCount;
  for (uint8_t s = 0; s < kDumpSlotCount; ++s) {
    const int x = s * slotW;
    const StoredDump& dd = storedDumps[s];
    const uint16_t fill = dd.valid ? kColInfo : kColBar;
    d.fillRect(x + 3, slotY, slotW - 6, slotH, fill);
    if (selectedSlot == s) {
      // Selected slot keeps its accent box regardless of focus; the caret marks
      // which row Left/Right steers.
      d.drawRect(x + 1, slotY - 2, slotW - 2, slotH + 4, accent);
      d.drawRect(x + 2, slotY - 1, slotW - 4, slotH + 2, accent);  // 2px = thicker
    }
    // Slot label: just the number. A filled dot shows the slot has data —
    // no version number (it's meaningless to the user).
    d.setTextColor(dd.valid ? kColBg : kColDim, fill);
    d.setCursor(x + (slotW - kGlyphW) / 2, slotY + 4);
    d.print(String(s + 1));
    if (dd.valid) {
      // Small filled dot bottom-right of the slot to indicate data present
      d.fillCircle(x + slotW - 7, slotY + slotH - 4, 2, kColBg);
    }
  }

  // Moving selection box: a bright white outline around the SELECTED item on the
  // currently-focused line. It hops between the mode line and the slot line as you
  // press Up/Down, so the active selection is always unambiguous (the per-line
  // accent fill/box stays, showing the selection each line will remember).
  if (focusRow == kFocusModeRow) {
    const int x = (uint8_t)selectedMode * tabW;
    d.drawRect(x + 1, tabsY - 1, tabW - 2, tabsH + 2, kColText);
    d.drawRect(x, tabsY - 2, tabW, tabsH + 4, kColText);
  } else {
    const int x = selectedSlot * slotW;
    d.drawRect(x + 1, slotY - 3, slotW - 2, slotH + 6, kColText);
    d.drawRect(x, slotY - 4, slotW, slotH + 8, kColText);
  }

  // Content panel: three rows below the slot strip.
  int y = 60;
  const StoredDump& cur = storedDumps[selectedSlot];

  if (lastCard.valid) {
    d.fillRect(0, y - 1, W, 13, 0x0420);
    d.setTextColor(kColOk, 0x0420);
    d.setCursor(4, y);
    d.print("ON  " + lastCard.uid);
  } else {
    d.setTextColor(kColDim, kColBg);
    d.setCursor(4, y);
    d.print("No card on reader");
  }
  y += 15;
  d.setCursor(4, y);
  if (cur.valid) {
    d.setTextColor(kColInfo, kColBg);
    d.printf("Slot %u: %ub %us/16  %s",
      (unsigned)(selectedSlot+1), (unsigned)cur.blocksRead,
      (unsigned)cur.sectorsRead, cur.sourceUid.substring(0,11).c_str());
  } else {
    d.setTextColor(kColDim, kColBg);
    d.printf("Slot %u: empty", (unsigned)(selectedSlot+1));
  }
  y += 13;
  d.setCursor(4, y);
  switch (selectedMode) {
    case UiMode::Read:
      d.setTextColor(kColDim, kColBg);
      d.print(cur.valid ? "Place card → auto-reads" : "Place card to read it");
      break;
    case UiMode::Write:
      if (cur.valid) { d.setTextColor(kColDim,  kColBg); d.print("Place dest card to write"); }
      else           { d.setTextColor(kColClone, kColBg); d.print("Slot empty — READ first"); }
      break;
    case UiMode::Clone:
      if (cur.valid) { d.setTextColor(kColDim,  kColBg); d.print("Place MAGIC card to clone"); }
      else           { d.setTextColor(kColClone, kColBg); d.print("Slot empty — READ first"); }
      break;
  }

  // Action bar: yellow when armed, otherwise the mode accent.
  const int barY = H - 13;
  const uint16_t hintCol = armed ? kColArmed : accent;
  d.fillRect(0, barY, W, 13, hintCol);
  d.setTextColor(kColBg, hintCol);
  String hint = footer.length() ? footer : action;
  const int maxChars = (W - 8) / kGlyphW;
  if ((int)hint.length() > maxChars) hint = hint.substring(0, maxChars);
  d.setCursor(4, barY + 3);
  d.print(hint);
}

// Options overlay: a focused settings card with a highlighted active row, a
// segmented brightness meter, and an ON/OFF sound pill. Same tactical-terminal
// language as the HUD (cyan chrome, accent fills, full-width action bar).
// Armed sub-page: shown while waiting for a card to confirm WRITE / CLONE /
// READ-overwrite. Re-drawn only when state actually changes so the display
// is stable even though pollCardPreview fires every 50 ms.
// Uses M5Canvas sprite → pushed atomically, zero flicker.
namespace {
  struct ArmedScreenCache {
    bool    cardValid = false;
    String  cardUid;
    bool    dirty     = true;  // true = must redraw on next call
    UiMode  mode      = UiMode::Read;
  } _armedCache;
}

void drawArmedScreen() {
  // Only redraw when card state or mode changes — no countdown means no
  // per-second redraws, so a held card produces a completely stable screen.
  if (!_armedCache.dirty            &&
      _armedCache.cardValid == lastCard.valid &&
      _armedCache.cardUid   == lastCard.uid   &&
      _armedCache.mode      == armedMode) {
    resultScreenActive = true;
    return;
  }
  _armedCache.cardValid = lastCard.valid;
  _armedCache.cardUid   = lastCard.uid;
  _armedCache.dirty     = false;
  _armedCache.mode      = armedMode;

  auto& disp = M5.Display;
  const int W = disp.width();
  const int H = disp.height();
  resultScreenActive = true;

  // Render into an off-screen sprite so the push is atomic (no flicker).
  // If allocation fails, draw directly to the display.
  M5Canvas cv(&disp);
  cv.setColorDepth(16);
  cv.createSprite(W, H);
  M5GFX& d = cv.width() ? (M5GFX&)cv : disp;
  d.fillScreen(kColBg);
  d.setTextSize(1);

  const uint16_t accent = accentForMode(armedMode);

  // Title bar
  d.fillRect(0, 0, W, 16, accent);
  d.setTextColor(kColBg, accent);
  d.setCursor(5, 4);
  d.print(armedMode == UiMode::Write ? "WRITE" :
          armedMode == UiMode::Clone ? "CLONE" : "READ overwrite");

  // Slot info
  const StoredDump& dump = storedDumps[armedSlot];
  d.setTextColor(kColInfo, kColBg);
  d.setCursor(4, 22);
  if (dump.valid)
    d.printf("Slot %u: %ub %us/16  %s",
             (unsigned)(armedSlot+1), (unsigned)dump.blocksRead,
             (unsigned)dump.sectorsRead, dump.sourceUid.substring(0,11).c_str());
  else
    d.printf("Slot %u: empty", (unsigned)(armedSlot+1));

  // Card area
  if (lastCard.valid) {
    d.fillRect(0, 36, W, 22, 0x0420);
    d.setTextColor(kColOk, 0x0420);
    d.setCursor(4, 40);
    d.printf("Card: %s", lastCard.uid.c_str());
    d.setTextColor(kColDim, 0x0420);
    d.setCursor(4, 51);
    d.print(lastCard.typeName.substring(0, 22));
    d.setTextColor(kColText, kColBg);
    d.setCursor(4, 62);
    if      (armedMode == UiMode::Clone) d.print("ENTER to clone  Esc to cancel");
    else if (armedMode == UiMode::Read)  d.print("ENTER to overwrite  Esc to cancel");
    else                                 d.print("ENTER to write  Esc to cancel");
  } else {
    d.setTextColor(kColDim, kColBg);
    d.setCursor(W/2 - 8*kGlyphW/2, 44);
    d.print("Waiting...");
    d.setCursor(4, 58);
    if      (armedMode == UiMode::Clone) d.print("Place MAGIC card on reader");
    else if (armedMode == UiMode::Read)  d.print("Place source card on reader");
    else                                 d.print("Place destination card on reader");
    d.setTextColor(kColDim, kColBg);
    d.setCursor(4, 72);
    d.print("Esc to cancel");
  }

  // Action bar: yellow when card present (ready to confirm), dim otherwise.
  const int barY = H - 13;
  const uint16_t barCol = lastCard.valid ? kColArmed : kColDim;
  d.fillRect(0, barY, W, 13, barCol);
  d.setTextColor(kColBg, barCol);
  d.setCursor(4, barY + 3);
  d.print(lastCard.valid ? "ENTER confirm  Esc cancel" : "Esc cancel");

  if (cv.width()) { cv.pushSprite(0, 0); cv.deleteSprite(); }
}

void drawOptions() {
  auto& d = M5.Display;
  const int W = d.width();
  const int H = d.height();
  d.fillScreen(kColBg);
  resultScreenActive = true;  // keep the preview poll from repainting the HUD
  d.setTextSize(1);

  // Title bar
  d.fillRect(0, 0, W, 16, kColInfo);
  d.setTextColor(kColBg, kColInfo);
  d.setCursor(5, 4);
  d.print("OPTIONS");
  String close = "M close";
  d.setCursor(W - (int)close.length() * kGlyphW - 4, 4);
  d.print(close);

  const int rowH = 20;
  const int labelX = 8;
  const int valX = 96;

  // Brightness row
  int y = 28;
  bool sel = optionIndex == 0;
  if (sel) d.fillRect(3, y - 3, W - 6, rowH, kColBar);
  d.setTextColor(sel ? kColText : kColDim, sel ? kColBar : kColBg);
  d.setCursor(labelX, y + 2);
  d.print("Brightness");
  const int segW = 14, segH = 11, segGap = 3;
  for (uint8_t i = 0; i < kBrightnessLevels; ++i) {
    const int sx = valX + i * (segW + segGap);
    if (i <= brightnessLevel) d.fillRect(sx, y, segW, segH, kColOk);
    else d.drawRect(sx, y, segW, segH, kColDim);
  }

  // Sound row — same 5-segment meter; level 0 (no segments) = OFF.
  y = 28 + rowH + 6;
  sel = optionIndex == 1;
  if (sel) d.fillRect(3, y - 3, W - 6, rowH, kColBar);
  d.setTextColor(sel ? kColText : kColDim, sel ? kColBar : kColBg);
  d.setCursor(labelX, y + 2);
  d.print("Sound");
  for (uint8_t i = 0; i < kSoundMax; ++i) {  // 5 segments = volume 1..5
    const int sx = valX + i * (segW + segGap);
    if (i < soundLevel) d.fillRect(sx, y, segW, segH, kColOk);
    else d.drawRect(sx, y, segW, segH, kColDim);
  }
  if (soundLevel == 0) {
    d.setTextColor(sel ? kColText : kColDim, sel ? kColBar : kColBg);
    d.setCursor(valX + kSoundMax * (segW + segGap) + 4, y + 2);
    d.print("OFF");
  }

  // Action bar
  const int barY = H - 13;
  d.fillRect(0, barY, W, 13, kColInfo);
  d.setTextColor(kColBg, kColInfo);
  d.setCursor(4, barY + 3);
  d.print("Up/Dn pick  L/R adjust  Esc/M close");
}

void setSelection(UiMode mode, uint8_t slot, const String& footer = "") {
  selectedMode = mode;
  selectedSlot = slot < kDumpSlotCount ? slot : 0;
  cancelArm();
  resetAutoTrigger();
  drawHome(footer);
}

void advanceSelection() {
  uint8_t index = selectedSlot * kUiModeCount + (uint8_t)selectedMode;
  index = (index + 1) % (kDumpSlotCount * kUiModeCount);
  selectedSlot = index / kUiModeCount;
  selectedMode = (UiMode)(index % kUiModeCount);
  cancelArm();
  drawHome();
}

void armSelection(PendingAction action) {
  pendingAction = action;
  armedMode = selectedMode;
  armedSlot = selectedSlot;
  armedUntilMs = rfidWriteArmDeadline(millis());
  _armedCache.dirty = true;  // force full redraw on first drawArmedScreen() call
}

void selectSlot(uint8_t slot) {
  selectedSlot = slot < kDumpSlotCount ? slot : 0;
  cancelArm();
  resetAutoTrigger();
  drawHome();
}

void moveSlot(int8_t delta) {
  selectedSlot = (uint8_t)((selectedSlot + kDumpSlotCount + delta) % kDumpSlotCount);
  cancelArm();
  resetAutoTrigger();
  drawHome();
}

void cycleMode(int8_t delta) {
  const int8_t next = ((int8_t)selectedMode + delta + kUiModeCount) % kUiModeCount;
  selectedMode = (UiMode)next;
  cancelArm();
  resetAutoTrigger();
  drawHome();
}

void setFocusRow(uint8_t row) {
  focusRow = row <= kFocusSlotRow ? row : kFocusModeRow;
  drawHome();  // focus change keeps the current selection (and any armed action)
}

String uidToString() {
  String uid;
  for (byte i = 0; i < rfid.uid.size; ++i) {
    if (i) uid += ' ';
    if (rfid.uid.uidByte[i] < 0x10) uid += '0';
    uid += String(rfid.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();
  return uid;
}

String bytesToHex(const byte* data, size_t len) {
  String out;
  out.reserve(len * 2);
  for (size_t i = 0; i < len; ++i) {
    if (data[i] < 0x10) out += '0';
    out += String(data[i], HEX);
  }
  out.toUpperCase();
  return out;
}

bool isSectorTrailerBlock(uint8_t block) {
  return (block % 4) == 3;
}

bool isCopyableClassicDataBlock(uint8_t block) {
  return block != 0 && !isSectorTrailerBlock(block);
}

int parseClassicBlockNumber(const String& value) {
  if (!value.length()) return -1;
  int block = 0;
  for (size_t i = 0; i < value.length(); ++i) {
    if (!isDigit(value[i])) return -1;
    block = block * 10 + (value[i] - '0');
    if (block >= kClassic1kBlocks) return -1;
  }
  return block;
}

int parseSlotNumber(String value) {
  value.trim();
  if (!value.length()) return -1;
  int slot = 0;
  for (size_t i = 0; i < value.length(); ++i) {
    if (!isDigit(value[i])) return -1;
    slot = slot * 10 + (value[i] - '0');
    if (slot > kDumpSlotCount) return -1;
  }
  if (slot < 1 || slot > kDumpSlotCount) return -1;
  return slot - 1;
}

int parseHexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return 10 + c - 'a';
  if (c >= 'A' && c <= 'F') return 10 + c - 'A';
  return -1;
}

bool parseHexBytes(const String& hex, byte* out, size_t len) {
  if (hex.length() != len * 2) return false;
  for (size_t i = 0; i < len; ++i) {
    const int hi = parseHexNibble(hex[i * 2]);
    const int lo = parseHexNibble(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = (byte)((hi << 4) | lo);
  }
  return true;
}

bool parseClassicBlockHex(String hex, byte out[kClassicBlockSize]) {
  hex.replace(" ", "");
  hex.replace(":", "");
  hex.replace("-", "");
  if (hex.length() != kClassicBlockSize * 2) return false;

  for (uint8_t i = 0; i < kClassicBlockSize; ++i) {
    const int hi = parseHexNibble(hex[i * 2]);
    const int lo = parseHexNibble(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = (byte)((hi << 4) | lo);
  }
  return true;
}

bool wakeAndSelectCard();  // defined below; needed by dictionary re-select

// ---- Bruce-style MifareKeysManager (adapted for our SD-only setup) ----------
// Validates a 12-char uppercase hex string (6 bytes = one MIFARE key).
static bool isValidHexKey(const String& key) {
  if (key.length() != 12) return false;
  for (char c : key) {
    if (!isxdigit((unsigned char)c)) return false;
  }
  return true;
}

// Converts a raw key byte array to the canonical 12-char uppercase hex string.
String keyToHex(const byte* key) {
  return bytesToHex(key, kClassicKeySize);
}

// Converts a canonical hex string key to raw bytes. Returns false if invalid.
static bool hexKeyToBytes(const String& hex, byte out[kClassicKeySize]) {
  if (!isValidHexKey(hex)) return false;
  for (uint8_t i = 0; i < kClassicKeySize; ++i) {
    const int hi = parseHexNibble(hex[i * 2]);
    const int lo = parseHexNibble(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = (byte)((hi << 4) | lo);
  }
  return true;
}

// Adds a key (uppercase hex string) — std::set guarantees dedup for free.
bool dictAddKey(const String& hexKey) {
  String k = hexKey;
  k.toUpperCase();
  k.trim();
  if (!isValidHexKey(k)) return false;
  if (mifareKeys.find(k) != mifareKeys.end()) return true;
  if (!rfidKeyCountAllowsInsert(mifareKeys.size())) return false;
  mifareKeys.insert(k);
  return true;
}

// Convenience overload accepting raw bytes.
bool dictAddKey(const byte* key) {
  return dictAddKey(keyToHex(key));
}

// Default well-known public MIFARE Classic keys — same set as Bruce firmware's
// RFIDInterface.h keys[] array plus a few extras.
void seedDefaultKeyDictionary() {
  static const char* kDefaults[] = {
    "FFFFFFFFFFFF",  // factory default — tried first
    "A0A1A2A3A4A5",  // MAD / common
    "D3F7D3F7D3F7",  // NDEF public
    "000000000000",  // all zeros
    "B0B1B2B3B4B5",
    "4D3A99C351DD",
    "1A982C7E459A",
    "AABBCCDDEEFF",
    "714C5C886E97",
    "587EE5F9350F",
    "A0478CC39091",
    "533CB6C723F6",
    "8FD0A4F256E9",
    "A6459AA77478",
    "26940B21FF5D",
  };
  mifareKeys.clear();
  for (const char* k : kDefaults) mifareKeys.insert(k);
}

// Tries every key in mifareKeys as Key A for this sector. Re-selects the card
// before each attempt (WS1850S / MFRC522-clone requires re-select after any
// failed/stopped auth — see fix notes). Leaves the card authenticated on
// success; caller must PCD_StopCrypto1() when done with the sector.
bool authenticateSectorWithDictionary(uint8_t firstBlock, byte outKey[kClassicKeySize]) {
  MFRC522_I2C::MIFARE_Key key;
  for (const auto& hexKey : mifareKeys) {
    if (!wakeAndSelectCard()) return false;  // card genuinely gone
    if (!hexKeyToBytes(hexKey, key.keyByte)) continue;
    if (rfid.PCD_Authenticate(MFRC522_I2C::PICC_CMD_MF_AUTH_KEY_A, firstBlock, &key, &rfid.uid) ==
        MFRC522_I2C::STATUS_OK) {
      memcpy(outKey, key.keyByte, kClassicKeySize);
      return true;
    }
    rfid.PCD_StopCrypto1();
  }
  return false;
}

String scanI2cBus() {
  String found;
  for (uint8_t addr = 1; addr < 127; ++addr) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      if (found.length()) found += ' ';
      found += "0x";
      if (addr < 0x10) found += '0';
      found += String(addr, HEX);
    }
  }
  found.toUpperCase();
  return found.length() ? found : "none";
}

bool hasRfid2OnBus() {
  Wire.beginTransmission(kRfidI2cAddress);
  return Wire.endTransmission() == 0;
}

// Wakes and selects whatever card is on the antenna, then leaves it ACTIVE and
// ready for authentication. Uses WUPA (PICC_WakeupA) instead of REQA
// (PICC_IsNewCardPresent) on purpose: the live preview poll halts the card with
// PICC_HaltA() after every detection, and a halted card ignores REQA. WUPA wakes
// cards in BOTH the IDLE and HALT states, so an explicit read/write right after a
// preview poll reliably re-selects the same held card instead of reporting "no
// card found".
bool wakeAndSelectCard() {
  if (!rfidReady) return false;
  // Retry a few times: right after the preview poll's PICC_HaltA(), the first
  // WUPA/Select can transiently fail before the PICC is ready. Without this the
  // correct key (e.g. default FF) may be skipped on a sector and the whole read
  // can fail intermittently even though the card is present and readable.
  for (uint8_t attempt = 0; attempt < 3; ++attempt) {
    byte atqa[2] = {};
    byte atqaSize = sizeof(atqa);
    const byte wake = rfid.PICC_WakeupA(atqa, &atqaSize);
    if ((wake == MFRC522_I2C::STATUS_OK || wake == MFRC522_I2C::STATUS_COLLISION) &&
        rfid.PICC_Select(&rfid.uid) == MFRC522_I2C::STATUS_OK) {
      return true;
    }
    rfid.PICC_HaltA();
    delay(5);
  }
  return false;
}

// ---------------------------------------------------------------------------
// microSD storage layer
// ---------------------------------------------------------------------------

String slotFilePath(uint8_t slot) {
  return String(kSlotDir) + "/slot" + String(slot + 1) + ".dump";
}

bool initSdStorage() {
  sdSpi.begin(kSdSckPin, kSdMisoPin, kSdMosiPin, kSdCsPin);
  pinMode(kSdCsPin, OUTPUT);
  digitalWrite(kSdCsPin, HIGH);
  // Some microSD cards need a slower first handshake; try the normal speed, then
  // fall back to a conservative rate before giving up.
  const uint32_t freqs[] = {kSdFrequencyHz, 1000000};
  for (uint8_t attempt = 0; attempt < 2; ++attempt) {
    for (uint32_t f : freqs) {
      if (SD.begin(kSdCsPin, sdSpi, f)) {
        if (!SD.exists(kSlotDir)) SD.mkdir(kSlotDir);
        sdReady = true;
        return true;
      }
      SD.end();
      delay(20);
    }
  }
  sdReady = false;
  return false;
}

// Persists one slot as a small text record. Key A per sector and every readable
// block (including trailers) are stored so a reload can write/clone identically.
bool saveSlotToSd(uint8_t slot) {
  if (!sdReady) return false;
  const StoredDump& dump = storedDumps[slot];
  const String path = slotFilePath(slot);
  if (!dump.valid) {
    SD.remove(path);
    return true;
  }

  File f = SD.open(path, FILE_WRITE);
  if (!f) return false;
  f.printf("v %u\n", dump.version);
  f.printf("uid %s\n", dump.sourceUid.c_str());
  f.printf("uidsize %u\n", dump.sourceUidSize);
  f.printf("type %s\n", dump.sourceType.c_str());
  f.printf("counts %u %u %u\n", dump.blocksRead, dump.sectorsRead, dump.sectorsFailed);
  for (uint8_t sector = 0; sector < kClassic1kSectors; ++sector) {
    if (dump.keyKnown[sector]) {
      f.printf("key %u %s\n", sector, bytesToHex(dump.keyA[sector], kClassicKeySize).c_str());
    }
  }
  for (uint8_t block = 0; block < kClassic1kBlocks; ++block) {
    if (dump.readable[block]) {
      f.printf("blk %u %s\n", block, bytesToHex(dump.data[block], kClassicBlockSize).c_str());
    }
  }
  f.close();
  return true;
}

bool loadSlotFromSd(uint8_t slot) {
  if (!sdReady) return false;
  const String path = slotFilePath(slot);
  if (!SD.exists(path)) return false;
  File f = SD.open(path, FILE_READ);
  if (!f) return false;
  if (!rfidPersistFileAllowed(f.size(), RFID_MAX_SLOT_FILE_BYTES)) {
    f.close();
    return false;
  }

  StoredDump dump;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    if (!rfidPersistLineAllowed(line.length())) {
      f.close();
      return false;
    }
    line.trim();
    if (!line.length()) continue;
    const int sp = line.indexOf(' ');
    const String tag = sp < 0 ? line : line.substring(0, sp);
    const String rest = sp < 0 ? "" : line.substring(sp + 1);
    if (tag == "v") {
      dump.version = (uint32_t)rest.toInt();
    } else if (tag == "uid") {
      dump.sourceUid = rest;
    } else if (tag == "uidsize") {
      dump.sourceUidSize = (uint8_t)rest.toInt();
    } else if (tag == "type") {
      dump.sourceType = rest;
    } else if (tag == "counts") {
      int a = 0, b = 0, c = 0;
      sscanf(rest.c_str(), "%d %d %d", &a, &b, &c);
      dump.blocksRead = (uint8_t)a;
      dump.sectorsRead = (uint8_t)b;
      dump.sectorsFailed = (uint8_t)c;
    } else if (tag == "key") {
      const int sp2 = rest.indexOf(' ');
      if (sp2 > 0) {
        const int sector = rest.substring(0, sp2).toInt();
        const String hex = rest.substring(sp2 + 1);
        if (sector >= 0 && sector < kClassic1kSectors && parseHexBytes(hex, dump.keyA[sector], kClassicKeySize)) {
          dump.keyKnown[sector] = true;
        }
      }
    } else if (tag == "blk") {
      const int sp2 = rest.indexOf(' ');
      if (sp2 > 0) {
        const int block = rest.substring(0, sp2).toInt();
        const String hex = rest.substring(sp2 + 1);
        if (block >= 0 && block < kClassic1kBlocks && parseHexBytes(hex, dump.data[block], kClassicBlockSize)) {
          dump.readable[block] = true;
        }
      }
    }
  }
  f.close();
  // Recompute counts from the actual parsed block data rather than trusting the
  // 'counts' scalar, so a missing/truncated/externally-edited 'counts' line can't
  // make a fully-parsed dump look empty (which would delete it on next save).
  uint8_t br = 0;
  for (uint8_t b = 0; b < kClassic1kBlocks; ++b) if (dump.readable[b]) br++;
  dump.blocksRead = br;
  uint8_t sr = 0, sf = 0;
  for (uint8_t s = 0; s < kClassic1kSectors; ++s) {
    bool any = false;
    for (uint8_t o = 0; o < 4; ++o) if (dump.readable[s * 4 + o]) { any = true; break; }
    if (any) sr++; else sf++;
  }
  dump.sectorsRead = sr;
  dump.sectorsFailed = sf;
  dump.valid = dump.blocksRead > 0;
  storedDumps[slot] = dump;
  if (dump.valid && dump.version >= nextDumpVersion) {
    nextDumpVersion = dump.version + 1;
  }
  return dump.valid;
}

void loadAllSlotsFromSd() {
  for (uint8_t slot = 0; slot < kDumpSlotCount; ++slot) {
    loadSlotFromSd(slot);
  }
}

// Saves the full mifareKeys set to SD. Bruce-style: one uppercase hex key per
// line, comment header. Overwrites the existing file atomically.
bool saveKeysToSd() {
  if (!sdReady) return false;
  File f = SD.open(kKeyFilePath, FILE_WRITE);
  if (!f) return false;
  f.println("// RFID2 Clone Station — MIFARE key dictionary");
  f.println("// One key per line, 12 uppercase hex chars (6 bytes).");
  for (const auto& k : mifareKeys) f.println(k);
  f.close();
  return true;
}

// Loads keys from SD into mifareKeys. Skips blank lines and comments (//),
// validates every line, auto-deduplicates via std::set. Never clears existing
// in-memory defaults — only adds what the file has on top.
bool loadKeysFromSd() {
  if (!sdReady || !SD.exists(kKeyFilePath)) return false;
  File f = SD.open(kKeyFilePath, FILE_READ);
  if (!f) return false;
  if (!rfidPersistFileAllowed(f.size(), RFID_MAX_KEY_FILE_BYTES)) {
    f.close();
    return false;
  }
  while (f.available()) {
    String line = f.readStringUntil('\n');
    if (!rfidPersistLineAllowed(line.length())) {
      f.close();
      return false;
    }
    line.trim();
    if (line.length() == 0 || line.startsWith("//")) continue;
    line.toUpperCase();
    if (isValidHexKey(line) && !dictAddKey(line)) {
      f.close();
      return false;
    }
  }
  f.close();
  return true;
}

void saveConfigToSd() {
  if (!sdReady) return;
  File f = SD.open("/rfid/config.txt", FILE_WRITE);
  if (!f) return;
  f.printf("brightness %u\n", brightnessLevel);
  f.printf("sound %u\n", soundLevel);
  f.close();
}

void loadConfigFromSd() {
  if (!sdReady || !SD.exists("/rfid/config.txt")) return;
  File f = SD.open("/rfid/config.txt", FILE_READ);
  if (!f) return;
  if (!rfidPersistFileAllowed(f.size(), RFID_MAX_SLOT_FILE_BYTES)) {
    f.close();
    return;
  }
  while (f.available()) {
    String line = f.readStringUntil('\n');
    if (!rfidPersistLineAllowed(line.length())) {
      f.close();
      return;
    }
    line.trim();
    const int sp = line.indexOf(' ');
    if (sp < 0) continue;
    const String tag = line.substring(0, sp);
    const long v = line.substring(sp + 1).toInt();
    if (tag == "brightness") brightnessLevel = (uint8_t)constrain(v, 0, kBrightnessLevels - 1);
    else if (tag == "sound") soundLevel = (uint8_t)constrain(v, 0, kSoundMax);
  }
  f.close();
}

void printJsonString(const String& value) {
  Serial.print('"');
  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    if (c == '"' || c == '\\') {
      Serial.print('\\');
      Serial.print(c);
    } else if (c == '\n') {
      Serial.print("\\n");
    } else if (c == '\r') {
      Serial.print("\\r");
    } else if (c == '\t') {
      Serial.print("\\t");
    } else if ((uint8_t)c < 0x20) {
      Serial.print(' ');
    } else {
      Serial.print(c);
    }
  }
  Serial.print('"');
}

void emitEventPrefix(const char* event) {
  Serial.print("{\"event\":");
  printJsonString(event);
  Serial.print(",\"fw\":");
  printJsonString(kFwName);
  Serial.print(",\"version\":");
  printJsonString(runningVersion());
  Serial.print(",\"build\":");
  printJsonString(String(__DATE__) + " " + __TIME__);
  Serial.print(",\"uptime_ms\":");
  Serial.print(millis());
}

void emitStatus(const char* reason) {
  emitEventPrefix("status");
  Serial.print(",\"reason\":");
  printJsonString(reason);
  Serial.print(",\"rfid_ready\":");
  Serial.print(rfidReady ? "true" : "false");
  Serial.print(",\"rfid_addr\":\"0x28\",\"sda\":");
  Serial.print(kGroveSda);
  Serial.print(",\"scl\":");
  Serial.print(kGroveScl);
  Serial.print(",\"i2c_scan\":");
  printJsonString(lastI2cScan);
  Serial.print(",\"last_card\":");
  if (lastCard.valid) {
    Serial.print("{\"uid\":");
    printJsonString(lastCard.uid);
    Serial.print(",\"sak\":\"0x");
    if (lastCard.sak < 0x10) Serial.print('0');
    Serial.print(lastCard.sak, HEX);
    Serial.print("\",\"type\":");
    printJsonString(lastCard.typeName);
    Serial.print(",\"seen_at_ms\":");
    Serial.print(lastCard.seenAtMs);
    Serial.print('}');
  } else {
    Serial.print("null");
  }
  Serial.print(",\"ui\":{\"mode\":");
  printJsonString(modeName(selectedMode));
  Serial.print(",\"slot\":");
  Serial.print(selectedSlot + 1);
  Serial.print(",\"armed\":");
  Serial.print(isArmedForSelection() ? "true" : "false");
  Serial.print(",\"pending_action\":");
  if (pendingAction == PendingAction::ReadOverwrite) {
    printJsonString("read_overwrite");
  } else if (pendingAction == PendingAction::WriteSlot) {
    printJsonString("write");
  } else if (pendingAction == PendingAction::CloneSlot) {
    printJsonString("clone");
  } else if (pendingAction == PendingAction::ClearSlot) {
    printJsonString("clear");
  } else {
    printJsonString("none");
  }
  Serial.print('}');
  Serial.print(",\"sd_ready\":");
  Serial.print(sdReady ? "true" : "false");
  Serial.print(",\"key_count\":");
  Serial.print(mifareKeys.size());
  Serial.print(",\"clone_trailers\":");
  Serial.print(cloneWriteTrailers ? "true" : "false");
  {
    const StatusSnapshot _ss = readStatus();
    Serial.print(",\"battery\":");
    Serial.print(_ss.batLevel);
    Serial.print(",\"usb_plugged\":");
    Serial.print(_ss.usbPlugged ? "true" : "false");
    Serial.print(",\"usb_cdc\":");
    Serial.print(_ss.usbCdc ? "true" : "false");
    Serial.print(",\"charging\":");
    Serial.print(_ss.charging ? "true" : "false");
  }
  Serial.print(",\"slots\":[");
  for (uint8_t slot = 0; slot < kDumpSlotCount; ++slot) {
    if (slot) Serial.print(',');
    const StoredDump& dump = storedDumps[slot];
    Serial.print("{\"slot\":");
    Serial.print(slot + 1);
    Serial.print(",\"valid\":");
    Serial.print(dump.valid ? "true" : "false");
    if (dump.valid) {
      Serial.print(",\"version\":");
      Serial.print(dump.version);
      Serial.print(",\"source_uid\":");
      printJsonString(dump.sourceUid);
      Serial.print(",\"source_type\":");
      printJsonString(dump.sourceType);
      Serial.print(",\"stored_at_ms\":");
      Serial.print(dump.storedAtMs);
      Serial.print(",\"blocks_read\":");
      Serial.print(dump.blocksRead);
      Serial.print(",\"sectors_read\":");
      Serial.print(dump.sectorsRead);
      Serial.print(",\"sectors_failed\":");
      Serial.print(dump.sectorsFailed);
    }
    Serial.print('}');
  }
  Serial.print(']');
  Serial.println('}');
  Serial.flush();
}

void emitMessage(const char* event, const String& message) {
  emitEventPrefix(event);
  Serial.print(",\"message\":");
  printJsonString(message);
  Serial.println('}');
  Serial.flush();
}

void refreshRfidStatus() {
  lastI2cScan = scanI2cBus();
  rfidReady = hasRfid2OnBus();
  if (rfidReady) {
    rfid.PCD_Init();
  }
}

bool selectPresentCard(const char* operation) {
  if (!rfidReady) {
    emitMessage("error", String(operation) + ": RFID2 is not initialized");
    drawLines("RFID2 not ready", "Check Grove cable", "then send reset-rfid");
    return false;
  }

  if (!wakeAndSelectCard()) {
    emitMessage("error", String(operation) + ": no card selected; lift and place card, then retry");
    drawLines(operation, "No card found", "Lift and place card", "then retry command");
    return false;
  }
  return true;
}

void emitStoredSummary(const char* event, uint8_t slot, const StoredDump& dump, const char* result, uint8_t blocksWritten = 0, uint8_t writeFailures = 0) {
  emitEventPrefix(event);
  Serial.print(",\"result\":");
  printJsonString(result);
  Serial.print(",\"slot\":");
  Serial.print(slot + 1);
  Serial.print(",\"version\":");
  Serial.print(dump.version);
  Serial.print(",\"source_uid\":");
  printJsonString(dump.sourceUid);
  Serial.print(",\"source_type\":");
  printJsonString(dump.sourceType);
  Serial.print(",\"blocks_read\":");
  Serial.print(dump.blocksRead);
  Serial.print(",\"sectors_read\":");
  Serial.print(dump.sectorsRead);
  Serial.print(",\"sectors_failed\":");
  Serial.print(dump.sectorsFailed);
  Serial.print(",\"blocks_written\":");
  Serial.print(blocksWritten);
  Serial.print(",\"write_failures\":");
  Serial.print(writeFailures);
  Serial.println('}');
  Serial.flush();
}

void emitSlots() {
  emitEventPrefix("slots");
  Serial.print(",\"selected_mode\":");
  printJsonString(modeName(selectedMode));
  Serial.print(",\"selected_slot\":");
  Serial.print(selectedSlot + 1);
  Serial.print(",\"slots\":[");
  for (uint8_t slot = 0; slot < kDumpSlotCount; ++slot) {
    if (slot) Serial.print(',');
    const StoredDump& dump = storedDumps[slot];
    Serial.print("{\"slot\":");
    Serial.print(slot + 1);
    Serial.print(",\"valid\":");
    Serial.print(dump.valid ? "true" : "false");
    if (dump.valid) {
      Serial.print(",\"version\":");
      Serial.print(dump.version);
      Serial.print(",\"source_uid\":");
      printJsonString(dump.sourceUid);
      Serial.print(",\"blocks_read\":");
      Serial.print(dump.blocksRead);
    }
    Serial.print('}');
  }
  Serial.println("]}");
  Serial.flush();
}

void emitStoredDump(uint8_t slot) {
  const StoredDump& dump = storedDumps[slot];
  if (!dump.valid) {
    emitMessage("error", slotTitle(slot) + " is empty; use read mode/store first");
    return;
  }

  emitEventPrefix("dump");
  Serial.print(",\"slot\":");
  Serial.print(slot + 1);
  Serial.print(",\"version\":");
  Serial.print(dump.version);
  Serial.print(",\"source_uid\":");
  printJsonString(dump.sourceUid);
  Serial.print(",\"source_type\":");
  printJsonString(dump.sourceType);
  Serial.print(",\"blocks\":[");
  bool first = true;
  for (uint8_t block = 0; block < kClassic1kBlocks; ++block) {
    if (!dump.readable[block]) continue;
    if (!first) Serial.print(',');
    first = false;
    Serial.print("{\"block\":");
    Serial.print(block);
    Serial.print(",\"copyable\":");
    Serial.print(isCopyableClassicDataBlock(block) ? "true" : "false");
    Serial.print(",\"data\":");
    printJsonString(bytesToHex(dump.data[block], kClassicBlockSize));
    Serial.print('}');
  }
  Serial.println("]}");
  Serial.flush();
}

void clearSlot(uint8_t slot, bool redraw = true) {
  storedDumps[slot] = StoredDump();
  writeDone = false;
  if (sdReady) SD.remove(slotFilePath(slot));
  if (pendingAction != PendingAction::None && armedSlot == slot) cancelArm();
  emitMessage("clear", slotTitle(slot) + " cleared");
  if (redraw) drawHome(slotTitle(slot) + " cleared");
}

void clearAllSlots() {
  for (uint8_t slot = 0; slot < kDumpSlotCount; ++slot) {
    storedDumps[slot] = StoredDump();
    if (sdReady) SD.remove(slotFilePath(slot));
  }
  writeDone = false;
  cancelArm();
  emitMessage("clear", "all slots cleared");
  drawHome("All slots cleared");
}

void emitBlockError(const char* operation, uint8_t sector, uint8_t block, byte status) {
  emitEventPrefix("block_error");
  Serial.print(",\"operation\":");
  printJsonString(operation);
  Serial.print(",\"sector\":");
  Serial.print(sector);
  Serial.print(",\"block\":");
  Serial.print(block);
  Serial.print(",\"status\":");
  Serial.print(status);
  Serial.println('}');
}

void storeSelectedClassic1k(uint8_t slot) {
  const uint8_t piccType = rfid.PICC_GetType(rfid.uid.sak);
  const String typeName = rfid.PICC_GetTypeName(piccType);
  const String sourceUid = uidToString();
  if (piccType != MFRC522_I2C::PICC_TYPE_MIFARE_1K) {
    emitMessage("error", "store supports MIFARE Classic 1K only; detected " + typeName);
    drawLines("Store unsupported", "Need MIFARE 1K", "Detected:", typeName);
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    return;
  }

  StoredDump nextDump;
  nextDump.version = nextDumpVersion++;
  nextDump.sourceUid = sourceUid;
  nextDump.sourceType = typeName;
  nextDump.storedAtMs = millis();
  nextDump.sourceUidSize = rfid.uid.size;

  // Show a live progress screen while reading — reading 16 sectors takes a few
  // seconds; without feedback the device looks frozen.
  auto drawReadProgress = [&](uint8_t sector) {
    auto& d = M5.Display;
    const int W = d.width(), H = d.height();
    d.fillScreen(kColBg);
    resultScreenActive = true;
    d.fillRect(0, 0, W, 16, kColRead);
    d.setTextColor(kColBg, kColRead);
    d.setTextSize(1);
    d.setCursor(5, 4);
    d.print("Reading...");
    d.setTextColor(kColText, kColBg);
    d.setCursor(4, 22);
    d.printf("Card: %s", sourceUid.c_str());
    d.setCursor(4, 36);
    d.printf("Sector %u / %u", (unsigned)sector + 1, (unsigned)kClassic1kSectors);
    // Progress bar
    const int pbX = 4, pbY = 52, pbW = W - 8, pbH = 8;
    d.drawRect(pbX, pbY, pbW, pbH, kColDim);
    d.fillRect(pbX + 1, pbY + 1, (pbW - 2) * (sector + 1) / kClassic1kSectors, pbH - 2, kColRead);
    d.setTextColor(kColDim, kColBg);
    d.setCursor(4, 68);
    d.printf("Blocks: %u  Failed: %u", (unsigned)nextDump.blocksRead, (unsigned)nextDump.sectorsFailed);
    M5Cardputer.update();  // keep keyboard responsive during long read
  };

  for (uint8_t sector = 0; sector < kClassic1kSectors; ++sector) {
    drawReadProgress(sector);
    const uint8_t firstBlock = sector * 4;
    byte sectorKey[kClassicKeySize];
    if (!authenticateSectorWithDictionary(firstBlock, sectorKey)) {
      nextDump.sectorsFailed++;
      emitBlockError("auth_read", sector, firstBlock, MFRC522_I2C::STATUS_ERROR);
      continue;
    }
    memcpy(nextDump.keyA[sector], sectorKey, kClassicKeySize);
    nextDump.keyKnown[sector] = true;

    bool sectorHadRead = false;
    // Read all four blocks including the sector trailer (offset 3) so writes and
    // clones can rebuild trailers (access bits + Key B) later.
    for (uint8_t offset = 0; offset < 4; ++offset) {
      const uint8_t block = firstBlock + offset;
      byte buffer[18] = {};
      byte byteCount = sizeof(buffer);
      byte status = rfid.MIFARE_Read(block, buffer, &byteCount);
      if (status == MFRC522_I2C::STATUS_OK) {
        memcpy(nextDump.data[block], buffer, kClassicBlockSize);
        nextDump.readable[block] = true;
        nextDump.blocksRead++;
        sectorHadRead = true;
      } else {
        emitBlockError("read", sector, block, status);
      }
    }
    if (sectorHadRead) {
      nextDump.sectorsRead++;
    }
    rfid.PCD_StopCrypto1();
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  nextDump.valid = nextDump.blocksRead > 0;
  storedDumps[slot] = nextDump;
  writeDone = false;
  const bool saved = nextDump.valid && saveSlotToSd(slot);
  emitStoredSummary("store", slot, storedDumps[slot], storedDumps[slot].valid ? "ok" : "no_blocks_read");
  if (storedDumps[slot].valid) {
    drawLines("Read saved",
              slotTitle(slot) + "  " + storedDumps[slot].sourceUid,
              String(storedDumps[slot].blocksRead) + " blocks  " + String(storedDumps[slot].sectorsRead) + "/16 sectors",
              sdReady ? (saved ? "Saved to SD" : "SD save failed") : "RAM only (no SD)");
  } else {
    drawLines("Read failed", "Auth failed " + String(nextDump.sectorsFailed) + "/16 sectors",
              "Card may use non-default keys", "Retry, or add keys (serial)");
  }
}

void storePresentClassic1k(uint8_t slot) {
  if (!selectPresentCard("store")) return;
  storeSelectedClassic1k(slot);
}

void writeStoredDumpToSelectedClassic1k(uint8_t slot) {
  StoredDump& dump = storedDumps[slot];
  if (!dump.valid) {
    emitMessage("error", slotTitle(slot) + " is empty; select/read a slot first");
    drawLines("Write blocked", slotTitle(slot) + " empty", "Select READ first");
    return;
  }

  const uint8_t piccType = rfid.PICC_GetType(rfid.uid.sak);
  const String typeName = rfid.PICC_GetTypeName(piccType);
  const String destUid = uidToString();
  if (piccType != MFRC522_I2C::PICC_TYPE_MIFARE_1K) {
    emitMessage("error", "write supports MIFARE Classic 1K only; detected " + typeName);
    drawLines("Write unsupported", "Need MIFARE 1K", "Detected:", typeName);
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    return;
  }

  if (destUid == dump.sourceUid) {
    emitMessage("error", "destination UID matches stored source UID; refusing to overwrite the source card");
    drawLines("Write blocked", "Destination matches", "stored source UID");
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    return;
  }

  uint8_t blocksWritten = 0;
  uint8_t writeFailures = 0;
  for (uint8_t sector = 0; sector < kClassic1kSectors; ++sector) {
    // Live progress
    {
      auto& d = M5.Display;
      const int W = d.width();
      d.fillScreen(kColBg);
      resultScreenActive = true;
      d.fillRect(0, 0, W, 16, kColWrite);
      d.setTextColor(kColBg, kColWrite); d.setTextSize(1);
      d.setCursor(5, 4); d.print("Writing...");
      d.setTextColor(kColText, kColBg);
      d.setCursor(4, 22); d.printf("Dest: %s", destUid.c_str());
      d.setCursor(4, 36); d.printf("Sector %u / %u", (unsigned)sector + 1, (unsigned)kClassic1kSectors);
      const int pbX = 4, pbY = 52, pbW = W - 8;
      d.drawRect(pbX, pbY, pbW, 8, kColDim);
      d.fillRect(pbX+1, pbY+1, (pbW-2)*(sector+1)/kClassic1kSectors, 6, kColWrite);
      d.setTextColor(kColDim, kColBg);
      d.setCursor(4, 68); d.printf("Written: %u  Errors: %u", (unsigned)blocksWritten, (unsigned)writeFailures);
      M5Cardputer.update();
    }
    const uint8_t firstBlock = sector * 4;
    bool hasWork = false;
    for (uint8_t offset = 0; offset < 3; ++offset) {
      const uint8_t block = firstBlock + offset;
      if (dump.readable[block] && isCopyableClassicDataBlock(block)) {
        hasWork = true;
        break;
      }
    }
    if (!hasWork) continue;

    // Authenticate the DESTINATION sector with the key dictionary (its current
    // key, not the source's), so non-default destination cards can be written.
    byte sectorKey[kClassicKeySize];
    if (!authenticateSectorWithDictionary(firstBlock, sectorKey)) {
      writeFailures += 3;
      emitBlockError("auth_write", sector, firstBlock, MFRC522_I2C::STATUS_ERROR);
      continue;
    }

    for (uint8_t offset = 0; offset < 3; ++offset) {
      const uint8_t block = firstBlock + offset;
      if (!dump.readable[block] || !isCopyableClassicDataBlock(block)) continue;
      byte status = rfid.MIFARE_Write(block, dump.data[block], kClassicBlockSize);
      if (status == MFRC522_I2C::STATUS_OK) {
        blocksWritten++;
      } else {
        writeFailures++;
        emitBlockError("write", sector, block, status);
      }
    }
    rfid.PCD_StopCrypto1();
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  emitStoredSummary("write", slot, dump, writeFailures == 0 ? "ok" : "partial", blocksWritten, writeFailures);
  writeDone = blocksWritten > 0;
  drawLines(writeDone ? "Write complete" : "Write failed", slotTitle(slot) + " v" + String(dump.version), "Written: " + String(blocksWritten), "Failures: " + String(writeFailures));
}

void writeStoredDumpToPresentClassic1k(uint8_t slot) {
  if (!selectPresentCard("write")) return;
  writeStoredDumpToSelectedClassic1k(slot);
}

// Builds a writable sector trailer from a stored dump: known Key A (the read key,
// since Key A always reads back as zeros), the source access bits verbatim, and
// the source Key B if it was readable (else fall back to the known Key A).
void buildTrailerForWrite(const StoredDump& dump, uint8_t sector, byte out[kClassicBlockSize]) {
  const uint8_t trailerBlock = sector * 4 + 3;
  memcpy(&out[0], dump.keyA[sector], kClassicKeySize);          // Key A (bytes 0-5)
  memcpy(&out[6], &dump.data[trailerBlock][6], 4);              // access bits + GPB (6-9)
  bool keyBReadable = false;
  for (uint8_t i = 10; i < 16; ++i) {
    if (dump.data[trailerBlock][i] != 0x00) { keyBReadable = true; break; }
  }
  if (keyBReadable) {
    memcpy(&out[10], &dump.data[trailerBlock][10], kClassicKeySize);  // Key B (10-15)
  } else {
    memcpy(&out[10], dump.keyA[sector], kClassicKeySize);            // fall back to Key A
  }
}

// Full clone of a stored dump onto the present (magic) card: data blocks, then
// the UID/block 0 via the library's gen1a backdoor (exact 16-byte copy) with a
// MIFARE_SetUid() fallback for gen2/CUID cards. Sector trailers are written only
// when cloneWriteTrailers is enabled. Block-0 write only works on magic cards;
// normal cards reject it (expected, reported as a failure).
void cloneStoredDumpToSelectedClassic1k(uint8_t slot) {
  StoredDump& dump = storedDumps[slot];
  if (!dump.valid) {
    emitMessage("error", slotTitle(slot) + " is empty; read a source card first");
    drawLines("Clone blocked", slotTitle(slot) + " empty", "Select READ first");
    return;
  }

  const uint8_t piccType = rfid.PICC_GetType(rfid.uid.sak);
  const String typeName = rfid.PICC_GetTypeName(piccType);
  const String destUid = uidToString();
  if (piccType != MFRC522_I2C::PICC_TYPE_MIFARE_1K) {
    emitMessage("error", "clone supports MIFARE Classic 1K only; detected " + typeName);
    drawLines("Clone unsupported", "Need MIFARE 1K", "Detected:", typeName);
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    return;
  }

  uint8_t blocksWritten = 0;
  uint8_t writeFailures = 0;
  uint8_t trailersWritten = 0;
  bool block0Written = false;
  bool magicGen1 = false;

  // 1) Data blocks (and optionally trailers), authenticating the destination
  //    with the key dictionary so non-default magic cards still work.
  for (uint8_t sector = 0; sector < kClassic1kSectors; ++sector) {
    // Live progress
    {
      auto& d = M5.Display;
      const int W = d.width();
      d.fillScreen(kColBg);
      resultScreenActive = true;
      d.fillRect(0, 0, W, 16, kColClone);
      d.setTextColor(kColBg, kColClone); d.setTextSize(1);
      d.setCursor(5, 4); d.print("Cloning...");
      d.setTextColor(kColText, kColBg);
      d.setCursor(4, 22); d.printf("Dest: %s", destUid.c_str());
      d.setCursor(4, 36); d.printf("Sector %u / %u", (unsigned)sector + 1, (unsigned)kClassic1kSectors);
      const int pbX = 4, pbY = 52, pbW = W - 8;
      d.drawRect(pbX, pbY, pbW, 8, kColDim);
      d.fillRect(pbX+1, pbY+1, (pbW-2)*(sector+1)/kClassic1kSectors, 6, kColClone);
      d.setTextColor(kColDim, kColBg);
      d.setCursor(4, 68); d.printf("Blocks: %u  UID: %s", (unsigned)blocksWritten, block0Written ? "done" : "pending");
      M5Cardputer.update();
    }
    const uint8_t firstBlock = sector * 4;
    bool hasWork = false;
    for (uint8_t offset = 0; offset < 3; ++offset) {
      const uint8_t block = firstBlock + offset;
      if (block != 0 && dump.readable[block]) { hasWork = true; break; }
    }
    if (cloneWriteTrailers && dump.readable[firstBlock + 3] && dump.keyKnown[sector]) hasWork = true;
    if (!hasWork) continue;

    byte sectorKey[kClassicKeySize];
    if (!authenticateSectorWithDictionary(firstBlock, sectorKey)) {
      writeFailures += 3;
      emitBlockError("auth_clone", sector, firstBlock, MFRC522_I2C::STATUS_ERROR);
      continue;
    }

    for (uint8_t offset = 0; offset < 3; ++offset) {
      const uint8_t block = firstBlock + offset;
      if (block == 0 || !dump.readable[block]) continue;  // block 0 handled separately
      const byte status = rfid.MIFARE_Write(block, dump.data[block], kClassicBlockSize);
      if (status == MFRC522_I2C::STATUS_OK) {
        blocksWritten++;
      } else {
        writeFailures++;
        emitBlockError("clone", sector, block, status);
      }
    }

    if (cloneWriteTrailers && dump.readable[firstBlock + 3] && dump.keyKnown[sector]) {
      byte trailer[kClassicBlockSize];
      buildTrailerForWrite(dump, sector, trailer);
      const byte status = rfid.MIFARE_Write(firstBlock + 3, trailer, kClassicBlockSize);
      if (status == MFRC522_I2C::STATUS_OK) {
        trailersWritten++;
      } else {
        writeFailures++;
        emitBlockError("clone_trailer", sector, firstBlock + 3, status);
      }
    }
    rfid.PCD_StopCrypto1();
  }

  // 2) UID / block 0 write — two attempts in this order:
  //
  // Try 1 — gen2/CUID direct write FIRST: authenticate with the dictionary
  //   key, then MIFARE_Write(block=0). CUID/gen2 cards accept this without
  //   any backdoor sequence. Critically, we try this BEFORE gen1a so we never
  //   send the 0x40 magic byte to a CUID card — some CUID cards enter a
  //   confused/bricked state when they receive the gen1a sequence.
  //
  // Try 2 — gen1a backdoor: halt → raw 0x40 (7-bit) + 0x43 → direct-write
  //   mode → MIFARE_Write(block=0). Only works on actual gen1a magic cards.
  //   Tried second so CUID cards are never exposed to the magic bytes.
  //
  // DO NOT use MIFARE_SetUid() — it calls MIFARE_OpenUidBackdoor() internally
  // making it gen1a-only, and will confuse/brick CUID cards.
  if (dump.readable[0]) {
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();

    // Attempt 1: gen2/CUID — direct authenticated write (safe for all card types)
    {
      byte sectorKey[kClassicKeySize];
      if (wakeAndSelectCard() && authenticateSectorWithDictionary(0, sectorKey)) {
        block0Written = rfid.MIFARE_Write((byte)0, dump.data[0], kClassicBlockSize) == MFRC522_I2C::STATUS_OK;
        if (block0Written) blocksWritten++;
        rfid.PCD_StopCrypto1();
      }
    }

    // Attempt 2: gen1a backdoor (only if gen2 failed — avoids confusing CUID cards)
    if (!block0Written) {
      rfid.PCD_StopCrypto1();
      if (wakeAndSelectCard() && rfid.MIFARE_OpenUidBackdoor(false)) {
        magicGen1 = true;
        block0Written = rfid.MIFARE_Write((byte)0, dump.data[0], kClassicBlockSize) == MFRC522_I2C::STATUS_OK;
        if (block0Written) blocksWritten++;
      }
    }

    if (!block0Written) {
      writeFailures++;
      emitBlockError("clone_block0", 0, 0, MFRC522_I2C::STATUS_ERROR);
    }
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  emitEventPrefix("clone");
  Serial.print(",\"result\":");
  printJsonString(writeFailures == 0 ? "ok" : "partial");
  Serial.print(",\"slot\":");
  Serial.print(slot + 1);
  Serial.print(",\"version\":");
  Serial.print(dump.version);
  Serial.print(",\"dest_uid\":");
  printJsonString(destUid);
  Serial.print(",\"magic_gen1\":");
  Serial.print(magicGen1 ? "true" : "false");
  Serial.print(",\"block0_written\":");
  Serial.print(block0Written ? "true" : "false");
  Serial.print(",\"blocks_written\":");
  Serial.print(blocksWritten);
  Serial.print(",\"trailers_written\":");
  Serial.print(trailersWritten);
  Serial.print(",\"write_failures\":");
  Serial.print(writeFailures);
  Serial.println('}');
  Serial.flush();

  writeDone = blocksWritten > 0;
  drawLines(block0Written ? "Clone complete" : "Clone partial",
            "UID " + String(block0Written ? (magicGen1 ? "copied" : "UID+BCC only") : "kept") + (magicGen1 ? " (gen1)" : ""),
            "Blocks: " + String(blocksWritten) + (cloneWriteTrailers ? "  Trl: " + String(trailersWritten) : ""),
            "Failures: " + String(writeFailures));
}

void cloneStoredDumpToPresentClassic1k(uint8_t slot) {
  if (!selectPresentCard("clone")) return;
  cloneStoredDumpToSelectedClassic1k(slot);
}

void writeLiteralDataBlock(const String& command) {
  const int firstSpace = command.indexOf(' ');
  const int secondSpace = firstSpace < 0 ? -1 : command.indexOf(' ', firstSpace + 1);
  if (firstSpace < 0 || secondSpace < 0) {
    emitMessage("error", "usage: write-block <block 1-62 non-trailer> <32 hex chars> confirm");
    drawLines("write-block usage", "write-block <block>", "<32 hex> confirm");
    return;
  }

  String blockToken = command.substring(firstSpace + 1, secondSpace);
  String dataToken = command.substring(secondSpace + 1);
  blockToken.trim();
  dataToken.trim();

  const int parsedBlock = parseClassicBlockNumber(blockToken);
  if (parsedBlock < 0) {
    emitMessage("error", "invalid MIFARE Classic 1K block: " + blockToken);
    drawLines("Write blocked", "Invalid block", blockToken);
    return;
  }

  const uint8_t block = (uint8_t)parsedBlock;
  if (!isCopyableClassicDataBlock(block)) {
    emitMessage("error", "refusing write-block for UID/block0 or sector trailer: block " + String(block));
    drawLines("Write blocked", "Block not editable", "Block: " + String(block));
    return;
  }

  byte payload[kClassicBlockSize] = {};
  if (!parseClassicBlockHex(dataToken, payload)) {
    emitMessage("error", "write-block data must be exactly 32 hex chars");
    drawLines("Write blocked", "Need 16 bytes", "32 hex chars");
    return;
  }

  if (!selectPresentCard("write-block")) return;

  const uint8_t piccType = rfid.PICC_GetType(rfid.uid.sak);
  const String typeName = rfid.PICC_GetTypeName(piccType);
  const String destUid = uidToString();
  if (piccType != MFRC522_I2C::PICC_TYPE_MIFARE_1K) {
    emitMessage("error", "write-block supports MIFARE Classic 1K only; detected " + typeName);
    drawLines("Write unsupported", "Need MIFARE 1K", "Detected:", typeName);
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    return;
  }

  const uint8_t sector = block / 4;
  const uint8_t firstBlock = sector * 4;
  byte sectorKey[kClassicKeySize];
  byte status = MFRC522_I2C::STATUS_ERROR;
  if (authenticateSectorWithDictionary(firstBlock, sectorKey)) {
    status = rfid.MIFARE_Write(block, payload, kClassicBlockSize);
  }

  emitEventPrefix("block_write");
  Serial.print(",\"uid\":");
  printJsonString(destUid);
  Serial.print(",\"block\":");
  Serial.print(block);
  Serial.print(",\"sector\":");
  Serial.print(sector);
  Serial.print(",\"result\":");
  printJsonString(status == MFRC522_I2C::STATUS_OK ? "ok" : "failed");
  Serial.print(",\"status\":");
  Serial.print(status);
  Serial.print(",\"data\":");
  printJsonString(bytesToHex(payload, kClassicBlockSize));
  Serial.println('}');
  Serial.flush();

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  if (status == MFRC522_I2C::STATUS_OK) {
    drawLines("Block written", "UID: " + destUid, "Block: " + String(block), bytesToHex(payload, 4) + "...");
  } else {
    drawLines("Write failed", "UID: " + destUid, "Block: " + String(block), "Status: " + String(status));
  }
}

String commandTail(const String& command, const char* verb) {
  String tail = command.substring(strlen(verb));
  tail.trim();
  return tail;
}

bool consumeConfirm(String& tail) {
  const size_t bodyLength = rfidConfirmedCommandLength(tail.c_str());
  if (!bodyLength) return false;
  tail = tail.substring(0, bodyLength);
  tail.trim();
  return true;
}

int parseOptionalSlot(String tail, bool& confirmed) {
  confirmed = consumeConfirm(tail);
  if (!tail.length()) return selectedSlot;
  return parseSlotNumber(tail);
}

bool serialDestructiveArmMatches(PendingAction action, UiMode mode, uint8_t slot) {
  const bool selectionMatches = selectedMode == mode && selectedSlot == slot &&
                                armedMode == mode && armedSlot == slot &&
                                pendingAction == action;
  return rfidSerialDestructiveArmValid(millis(), armedUntilMs, lastCard.valid, selectionMatches);
}

void serialArmRequired(const char* operation, uint8_t slot) {
  const String title = String(operation) + " blocked";
  emitMessage("error", title + ": arm matching " + slotTitle(slot) +
                         " on physical UI, keep card present, then resend within 8s");
  drawLines(title.c_str(), slotTitle(slot), "Arm on device UI", "Keep card present");
}

void executeSelectedAction() {
  switch (selectedMode) {
    case UiMode::Read: {
      // Reading into a slot that already holds data needs an explicit choice:
      // Enter again to overwrite, or Esc/backtick to keep the existing dump.
      if (storedDumps[selectedSlot].valid && !isArmedAction(PendingAction::ReadOverwrite)) {
        armSelection(PendingAction::ReadOverwrite);
        emitMessage("armed", "slot has data; Enter=overwrite, Esc=keep for " + slotTitle(selectedSlot));
        drawArmedScreen();
        return;
      }
      cancelArm();
      storePresentClassic1k(selectedSlot);
      return;
    }
    case UiMode::Write: {
      if (!storedDumps[selectedSlot].valid) {
        emitMessage("error", "write blocked: " + slotTitle(selectedSlot) + " is empty");
        drawLines("Write blocked", slotTitle(selectedSlot) + " empty", "Read a slot first");
        return;
      }
      if (!isArmedAction(PendingAction::WriteSlot)) {
        armSelection(PendingAction::WriteSlot);
        emitMessage("armed", "write armed for " + slotSummary(selectedSlot) + "; press Enter again within 8s");
        drawArmedScreen();
        return;
      }
      cancelArm();
      writeStoredDumpToPresentClassic1k(selectedSlot);
      return;
    }
    case UiMode::Clone: {
      if (!storedDumps[selectedSlot].valid) {
        emitMessage("error", "clone blocked: " + slotTitle(selectedSlot) + " is empty");
        drawLines("Clone blocked", slotTitle(selectedSlot) + " empty", "Read a slot first");
        return;
      }
      if (!isArmedAction(PendingAction::CloneSlot)) {
        armSelection(PendingAction::CloneSlot);
        emitMessage("armed", "CLONE armed for " + slotSummary(selectedSlot) + " (rewrites UID+block0); Enter again within 8s");
        drawArmedScreen();
        return;
      }
      cancelArm();
      cloneStoredDumpToPresentClassic1k(selectedSlot);
      return;
    }
  }
}

bool hasWord(const Keyboard_Class::KeysState& status, char a, char b = '\0') {
  for (char c : status.word) {
    if (c == a || (b && c == b)) return true;
  }
  return false;
}

void adjustOption(int8_t delta) {
  if (optionIndex == 0) {
    const int v = (int)brightnessLevel + delta;
    brightnessLevel = (uint8_t)constrain(v, 0, kBrightnessLevels - 1);
    applyBrightness();
  } else {
    const int v = (int)soundLevel + delta;
    soundLevel = (uint8_t)constrain(v, 0, kSoundMax);
    if (soundLevel > 0) beep();
  }
  drawOptions();
}

// Routes keys while the options overlay is open. Up/Down pick the row, Left/Right
// adjust, Enter toggles/steps, M or backtick saves and closes.
void handleOptionsKeys(const Keyboard_Class::KeysState& status) {
  // Close on Esc/backtick, the Back/Del key, or M. Settings save on close.
  if (status.del || hasWord(status, '`', '~') || hasWord(status, 'm', 'M')) {
    optionsOpen = false;
    saveConfigToSd();
    drawHome("Options saved");
    return;
  }
  if (hasWord(status, ';', ':')) { optionIndex = (optionIndex + kOptionCount - 1) % kOptionCount; drawOptions(); return; }
  if (hasWord(status, '.', '>')) { optionIndex = (optionIndex + 1) % kOptionCount; drawOptions(); return; }
  if (hasWord(status, ',', '<')) { adjustOption(-1); return; }
  if (hasWord(status, '/', '?')) { adjustOption(1); return; }
  if (status.enter) { adjustOption(1); return; }
}

void clearSelectedSlotAction() {
  if (!storedDumps[selectedSlot].valid) {
    drawHome(slotTitle(selectedSlot) + " already empty");
    emitMessage("clear", slotTitle(selectedSlot) + " already empty");
    return;
  }

  if (!isArmedAction(PendingAction::ClearSlot)) {
    armSelection(PendingAction::ClearSlot);
    emitMessage("armed", "clear armed for " + slotSummary(selectedSlot) + "; press Backspace again within 8s");
    drawLines("Clear armed", slotSummary(selectedSlot), "Back again", "backtick cancels");
    return;
  }

  clearSlot(selectedSlot);
}

void handleKeyboardUi() {
  // Options overlay owns the keyboard while open.
  if (optionsOpen) {
    if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
      handleOptionsKeys(M5Cardputer.Keyboard.keysState());
    }
    return;
  }

  if (pendingAction != PendingAction::None && !rfidArmStillValid(millis(), armedUntilMs)) {
    cancelArm();
    drawHome("Arm expired");
  }

  if (!M5Cardputer.Keyboard.isChange() || !M5Cardputer.Keyboard.isPressed()) {
    return;
  }

  Keyboard_Class::KeysState status = M5Cardputer.Keyboard.keysState();

  if (hasWord(status, '`', '~')) {
    cancelArm();
    drawHome("Cancelled");
    return;
  }

  if (hasWord(status, 'm', 'M')) {
    optionsOpen = true;
    optionIndex = 0;
    beep();
    drawOptions();
    return;
  }

  if (status.del) {
    clearSelectedSlotAction();
    return;
  }

  if (status.enter) {
    beep();
    executeSelectedAction();
    return;
  }

  // Up/Down pick which row the arrows steer; Left/Right move within that row.
  if (hasWord(status, ';', ':')) {  // Up -> mode row
    setFocusRow(kFocusModeRow);
    return;
  }

  if (hasWord(status, '.', '>')) {  // Down -> slot row
    setFocusRow(kFocusSlotRow);
    return;
  }

  if (hasWord(status, ',', '<')) {  // Left -> previous in focused row
    if (focusRow == kFocusModeRow) cycleMode(-1);
    else moveSlot(-1);
    return;
  }

  if (hasWord(status, '/', '?')) {  // Right -> next in focused row
    if (focusRow == kFocusModeRow) cycleMode(1);
    else moveSlot(1);
    return;
  }

  if (hasWord(status, 'r', 'R')) {
    focusRow = kFocusModeRow;
    setSelection(UiMode::Read, selectedSlot);
    return;
  }

  if (hasWord(status, 'w', 'W')) {
    focusRow = kFocusModeRow;
    setSelection(UiMode::Write, selectedSlot);
    return;
  }

  if (hasWord(status, 'c', 'C')) {
    focusRow = kFocusModeRow;
    setSelection(UiMode::Clone, selectedSlot);
    return;
  }

  for (uint8_t slot = 0; slot < kDumpSlotCount; ++slot) {
    const char key = (char)('1' + slot);
    if (hasWord(status, key)) {
      focusRow = kFocusSlotRow;
      selectSlot(slot);
      return;
    }
  }
}

void pollCardPreview() {
  if (!rfidReady) return;
  if (!wakeAndSelectCard()) {
    // Card lifted — reset auto-trigger so placing the same card again re-triggers.
    if (lastCard.valid) {
      lastCard.valid = false;
      resetAutoTrigger();
      if (isArmedForSelection()) drawArmedScreen();  // update armed sub-page: card gone
      else if (!resultScreenActive) drawHome();
    }
    return;
  }

  const uint32_t now = millis();
  const uint8_t piccType = rfid.PICC_GetType(rfid.uid.sak);
  const String uid = uidToString();
  const String typeName = rfid.PICC_GetTypeName(piccType);
  const bool isNewCard = !lastCard.valid || uid != lastCard.uid;
  const bool shouldReport = isNewCard || (uint32_t)(now - lastCard.seenAtMs) > 5000;

  lastCard.valid = true;
  lastCard.uid = uid;
  lastCard.sak = rfid.uid.sak;
  lastCard.typeName = typeName;
  lastCard.seenAtMs = now;

  if (shouldReport) {
    emitEventPrefix("card");
    Serial.print(",\"uid\":");
    printJsonString(uid);
    Serial.print(",\"sak\":\"0x");
    if (rfid.uid.sak < 0x10) Serial.print('0');
    Serial.print(rfid.uid.sak, HEX);
    Serial.print("\",\"type\":");
    printJsonString(typeName);
    Serial.println('}');
    Serial.flush();
  }

  // Auto-trigger: when a new card is placed, immediately start the appropriate
  // action without waiting for Enter — only prompt confirmation when it would
  // overwrite existing data. Guards against re-triggering while holding the card.
  const bool alreadyTriggered = (uid == lastAutoTriggeredUid);
  if (isNewCard && !alreadyTriggered && pendingAction == PendingAction::None) {
    lastAutoTriggeredUid = uid;

    if (selectedMode == UiMode::Read) {
      if (storedDumps[selectedSlot].valid) {
        // Slot occupied → arm overwrite prompt; user confirms with Enter or cancels.
        armSelection(PendingAction::ReadOverwrite);
        emitMessage("auto", "card detected; slot occupied — Enter=overwrite, Esc=keep");
        drawArmedScreen();
        // Card left selected; arm window ticks down; user presses Enter to read.
        rfid.PICC_HaltA();
        rfid.PCD_StopCrypto1();
        return;
      } else {
        // Slot empty → read immediately, no confirmation needed.
        emitMessage("auto", "card detected; reading into " + slotTitle(selectedSlot));
        // Card is already selected from wakeAndSelectCard(); storeSelectedClassic1k
        // operates on rfid.uid directly and handles HaltA internally.
        storeSelectedClassic1k(selectedSlot);
        return;
      }
    }

    if (selectedMode == UiMode::Write && storedDumps[selectedSlot].valid) {
      armSelection(PendingAction::WriteSlot);
      emitMessage("auto", "card detected; write armed for " + slotSummary(selectedSlot) + " — Enter to confirm");
      drawArmedScreen();
      rfid.PICC_HaltA();
      rfid.PCD_StopCrypto1();
      return;
    }

    if (selectedMode == UiMode::Clone && storedDumps[selectedSlot].valid) {
      armSelection(PendingAction::CloneSlot);
      emitMessage("auto", "card detected; clone armed for " + slotSummary(selectedSlot) + " — Enter to confirm");
      drawArmedScreen();
      rfid.PICC_HaltA();
      rfid.PCD_StopCrypto1();
      return;
    }
  }

  // If an action is armed, keep the armed sub-page live and updated with card info.
  if (isArmedForSelection()) drawArmedScreen();
  else if (!resultScreenActive) drawHome();
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

void processCommand(String command) {
  command.trim();
  command.toLowerCase();
  if (!command.length()) return;

  if (command == "status" || command == "?") {
    emitStatus("command");
  } else if (command == "slots") {
    emitSlots();
  } else if (command == "next") {
    advanceSelection();
    emitStatus("selection");
  } else if (command == "ui") {
    drawHome();
    emitStatus("ui");
  } else if (command == "mode") {
    emitMessage("mode", selectedSummary());
  } else if (command.startsWith("mode ")) {
    String mode = commandTail(command, "mode");
    if (mode == "read") {
      setSelection(UiMode::Read, selectedSlot);
      emitStatus("mode");
    } else if (mode == "write") {
      setSelection(UiMode::Write, selectedSlot);
      emitStatus("mode");
    } else if (mode == "clone") {
      setSelection(UiMode::Clone, selectedSlot);
      emitStatus("mode");
    } else {
      emitMessage("error", "usage: mode read|write|clone");
    }
  } else if (command == "slot") {
    emitSlots();
  } else if (command.startsWith("slot ")) {
    const int parsedSlot = parseSlotNumber(commandTail(command, "slot"));
    if (parsedSlot < 0) {
      emitMessage("error", "usage: slot 1-" + String(kDumpSlotCount));
    } else {
      setSelection(selectedMode, (uint8_t)parsedSlot);
      emitStatus("slot");
    }
  } else if (command == "scan") {
    lastI2cScan = scanI2cBus();
    rfidReady = hasRfid2OnBus();
    emitStatus("i2c_scan");
  } else if (command == "store" || command.startsWith("store ")) {
    bool confirmed = false;
    const int parsedSlot = parseOptionalSlot(commandTail(command, "store"), confirmed);
    if (parsedSlot < 0) {
      emitMessage("error", "usage: store [slot 1-" + String(kDumpSlotCount) + "] [confirm]");
    } else if (storedDumps[parsedSlot].valid && !confirmed) {
      emitMessage("error", slotTitle(parsedSlot) + " already has v" + String(storedDumps[parsedSlot].version) + "; use store " + String(parsedSlot + 1) + " confirm");
      drawLines("Read blocked", slotSummary(parsedSlot), "Use confirm to overwrite");
    } else {
      setSelection(UiMode::Read, (uint8_t)parsedSlot);
      storePresentClassic1k((uint8_t)parsedSlot);
    }
  } else if (command == "dump" || command.startsWith("dump ")) {
    bool confirmed = false;
    const int parsedSlot = parseOptionalSlot(commandTail(command, "dump"), confirmed);
    if (parsedSlot < 0) {
      emitMessage("error", "usage: dump [slot 1-" + String(kDumpSlotCount) + "]");
    } else {
      emitStoredDump((uint8_t)parsedSlot);
    }
  } else if (command == "write" || command.startsWith("write ")) {
    bool confirmed = false;
    const int parsedSlot = parseOptionalSlot(commandTail(command, "write"), confirmed);
    if (parsedSlot < 0) {
      emitMessage("error", "usage: write [slot 1-" + String(kDumpSlotCount) + "] confirm");
    } else if (!confirmed) {
      emitMessage("error", "write requires explicit confirm: write " + String(parsedSlot + 1) + " confirm");
      drawLines("Write blocked", slotSummary((uint8_t)parsedSlot), "Serial needs confirm");
    } else if (!serialDestructiveArmMatches(PendingAction::WriteSlot, UiMode::Write, (uint8_t)parsedSlot)) {
      serialArmRequired("write", (uint8_t)parsedSlot);
    } else {
      cancelArm();
      writeStoredDumpToPresentClassic1k((uint8_t)parsedSlot);
    }
  } else if (command.startsWith("write-block ")) {
    const size_t confirmedLength = rfidConfirmedCommandLength(command.c_str());
    if (!confirmedLength) {
      emitMessage("error", "write-block requires explicit confirm");
      drawLines("Write blocked", "Serial needs confirm", "write-block ... confirm");
    } else {
      emitMessage("error", "write-block is serial-disabled; use physical UI full-slot write");
      drawLines("Write blocked", "Serial literal writes disabled", "Use device UI");
    }
  } else if (command == "clone" || command.startsWith("clone ")) {
    bool confirmed = false;
    const int parsedSlot = parseOptionalSlot(commandTail(command, "clone"), confirmed);
    if (parsedSlot < 0) {
      emitMessage("error", "usage: clone [slot 1-" + String(kDumpSlotCount) + "] confirm");
    } else if (!confirmed) {
      emitMessage("error", "clone rewrites UID/block 0 and needs a MAGIC card; confirm: clone " + String(parsedSlot + 1) + " confirm");
      drawLines("Clone blocked", slotSummary((uint8_t)parsedSlot), "Serial needs confirm", "needs MAGIC card");
    } else if (!serialDestructiveArmMatches(PendingAction::CloneSlot, UiMode::Clone, (uint8_t)parsedSlot)) {
      serialArmRequired("clone", (uint8_t)parsedSlot);
    } else {
      cancelArm();
      cloneStoredDumpToPresentClassic1k((uint8_t)parsedSlot);
    }
  } else if (command == "keys") {
    emitEventPrefix("keys");
    Serial.print(",\"count\":");
    Serial.print(mifareKeys.size());
    Serial.print(",\"keys\":[");
    bool first = true;
    for (const auto& k : mifareKeys) {
      if (!first) Serial.print(',');
      first = false;
      printJsonString(k);
    }
    Serial.println("]}");
    Serial.flush();
  } else if (command.startsWith("key add ")) {
    String hex = commandTail(command, "key add");
    hex.replace(" ", ""); hex.replace(":", ""); hex.toUpperCase();
    if (!dictAddKey(hex)) {
      emitMessage("error", "key add needs 12 hex chars (6 bytes); got: " + hex);
    } else {
      saveKeysToSd();
      emitMessage("keys", "added " + hex + "; count=" + String(mifareKeys.size()));
    }
  } else if (command == "key clear") {
    mifareKeys.clear();
    saveKeysToSd();
    emitMessage("keys", "dictionary cleared");
  } else if (command == "key reset") {
    seedDefaultKeyDictionary();
    saveKeysToSd();
    emitMessage("keys", "dictionary reset to defaults; count=" + String(mifareKeys.size()));
  } else if (command == "trailers on") {
    cloneWriteTrailers = true;
    emitMessage("trailers", "clone will WRITE sector trailers (risky)");
  } else if (command == "trailers off") {
    cloneWriteTrailers = false;
    emitMessage("trailers", "clone will skip sector trailers (safe)");
  } else if (command == "trailers") {
    emitMessage("trailers", cloneWriteTrailers ? "on (clone writes trailers)" : "off (clone skips trailers)");
  } else if (command == "sd") {
    emitMessage("sd", sdReady ? "microSD mounted; slots persist" : "no microSD; slots are RAM-only");
  } else if (command == "clear" || command.startsWith("clear ")) {
    String tail = commandTail(command, "clear");
    const bool confirmed = consumeConfirm(tail);
    if (tail == "all") {
      if (confirmed) {
        clearAllSlots();
      } else {
        emitMessage("error", "clear all requires confirm: clear all confirm");
        drawLines("Clear blocked", "Use confirm for all", "clear all confirm");
      }
    } else {
      const int parsedSlot = tail.length() ? parseSlotNumber(tail) : selectedSlot;
      if (parsedSlot < 0) {
        emitMessage("error", "usage: clear [slot 1-" + String(kDumpSlotCount) + "] or clear all confirm");
      } else {
        clearSlot((uint8_t)parsedSlot);
      }
    }
  } else if (command == "reset-rfid") {
    refreshRfidStatus();
    drawHome();
    emitStatus("reset_rfid");
  } else if (command == "version") {
    emitMessage("version", String(kFwName) + " " + runningVersion());
  } else if (command == "help") {
    emitMessage("help", "commands: status, slots, next, ui, mode read|write|clone, slot <1-4>, scan, store [slot] [confirm], dump [slot], write [slot] confirm, clone [slot] confirm, write-block <block> <32hex> confirm, keys, key add <12hex>, key clear, key reset, trailers on|off, sd, clear [slot]|all confirm, reset-rfid, version, help");
  } else {
    emitMessage("error", "unknown command: " + command);
  }
}

void pollSerialCommands() {
  while (Serial.available() > 0) {
    const char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      processCommand(serialCommand);
      serialCommand = "";
    } else if (serialCommand.length() < kCommandMax) {
      serialCommand += c;
    } else {
      serialCommand = "";
      emitMessage("error", "command too long");
    }
  }
}
// Draws one expanding "ping" ring of the beacon as red side ARCS (gaps at top and
// bottom, like the product ((•)) mark) into an off-screen canvas.
void drawBeaconArc(M5Canvas& g, int cx, int cy, int r, uint16_t color) {
  for (int a = -48; a <= 48; a += 6) {
    const float e = (float)a * 0.01745329f;          // east arc
    const float w = (float)(a + 180) * 0.01745329f;  // west arc (mirror)
    g.fillCircle(cx + (int)(r * cosf(e)), cy + (int)(r * sinf(e)), 1, color);
    g.fillCircle(cx + (int)(r * cosf(w)), cy + (int)(r * sinf(w)), 1, color);
  }
}

// Animated boot splash: the original radar-ping look, recoloured RED — concentric
// red arc rings ping outward from a pulsing red centre dot, an Orbitron "RFID2"
// title, a red progress bar that becomes a gentle PRESS ANY KEY prompt, and a
// small haohanl/version credit. Rendered to an off-screen M5Canvas and pushed in
// one shot, so there is NO full-screen flicker. The ping keeps animating while
// waiting for a key; a serial byte also releases it.
void runBootSequence() {
  auto& disp = M5.Display;
  const int W = disp.width();
  const int H = disp.height();
  const int cx = W / 2;
  const uint16_t RED = 0xF800;
  const uint16_t REDmid = 0xC800;
  const uint16_t REDdim = 0x6000;
  const String credit = String("by haohanl  v") + runningVersion();

  M5Canvas cv(&disp);
  cv.setColorDepth(16);
  const bool buf = cv.createSprite(W, H);

  const uint32_t start = millis();
  for (;;) {
    M5Cardputer.update();
    const uint32_t t = millis() - start;
    const bool loaded = t > 1300;
    if (loaded && ((M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) ||
                   Serial.available() > 0)) {
      break;
    }
    if (!buf) { delay(30); continue; }  // no back-buffer: skip drawing, still wait for key

    cv.fillScreen(kColBg);
    cv.drawRoundRect(1, 1, W - 2, H - 2, 6, 0x2104);  // bezel frame

    // Status row: USB (left) + battery gauge (right).
    const int lvl = (int)M5.Power.getBatteryLevel();
    const bool charging = (int)M5.Power.isCharging() == 1;
    if ((bool)Serial) {
      cv.setTextColor(kColInfo, kColBg);
      cv.setCursor(6, 4);
      cv.print("USB");
    }
    {
      const uint16_t bcol = lvl < 0 ? kColDim : (lvl < 15 ? RED : (lvl < 40 ? kColWrite : kColOk));
      const int bx = W - 22;
      cv.drawRect(bx, 4, 15, 8, bcol);
      cv.fillRect(bx + 15, 6, 2, 4, bcol);
      if (lvl >= 0) {
        const int fw = (13 * lvl) / 100;
        if (fw > 0) cv.fillRect(bx + 1, 5, fw, 6, charging ? kColArmed : bcol);
      }
      String pct = (lvl < 0 ? String("--") : String(lvl)) + "%";
      cv.setTextColor(bcol, kColBg);
      cv.setCursor(bx - (int)pct.length() * kGlyphW - 2, 4);
      cv.print(pct);
    }

    // Title + subtitle (top, centred)
    cv.setFont(&fonts::Orbitron_Light_24);
    cv.setTextDatum(middle_center);
    cv.setTextColor(kColText, kColBg);
    cv.drawString("RFID2", cx, 20);
    cv.setFont(&fonts::Font0);
    cv.setTextDatum(top_left);
    cv.setTextSize(1);
    cv.setTextColor(kColInfo, kColBg);
    {
      const char* s = "CLONE STATION";
      cv.setCursor(cx - (int)strlen(s) * kGlyphW / 2, 34);
      cv.print(s);
    }

    // Beacon (left third): red side-arc rings standing well clear of the centre
    // dot (like the product mark) and pinging slowly outward. Time-based so the
    // motion stays calm regardless of frame rate (~2s cycle).
    const int bcx = 60, bcy = 74;
    for (int i = 0; i < 3; ++i) {
      const int ph = ((int)(t / 66) + i * 10) % 30;  // 0..29, ~2s loop
      const int r = 16 + ph;                          // 16..45: offset from dot, wider spread
      const uint16_t col = ph < 10 ? RED : (ph < 20 ? REDmid : REDdim);
      drawBeaconArc(cv, bcx, bcy, r, col);
    }
    cv.fillCircle(bcx, bcy, 3 + (((int)(t / 600)) & 1), RED);  // small, slow-pulsing core

    // Stage label / prompt above a full-width red progress bar.
    const int pbX = 12, pbW = W - 24, pbY = 118;
    cv.drawRect(pbX, pbY, pbW, 6, kColDim);
    if (!loaded) {
      cv.fillRect(pbX + 1, pbY + 1, (pbW - 2) * (int)t / 1300, 4, RED);
      cv.setTextColor(kColDim, kColBg);
      cv.setCursor(pbX, 110);
      cv.print("INITIALISING");
    } else {
      cv.fillRect(pbX + 1, pbY + 1, pbW - 2, 4, RED);
      if (((int)(t / 700)) & 1) {  // slow ~1.4s blink
        cv.setTextColor(kColText, kColBg);
        cv.setCursor(pbX, 110);
        cv.print("PRESS ANY KEY");
      }
    }

    // Credit + real version (bottom-left)
    cv.setTextColor(kColDim, kColBg);
    cv.setCursor(pbX, 126);
    cv.print(credit);

    cv.pushSprite(0, 0);
    delay(33);  // ~30fps: smooth but calm
  }
  if (buf) cv.deleteSprite();
  beep(2000, 20);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t serialStartMs = millis();
  while (!Serial && (uint32_t)(millis() - serialStartMs) < 1500) {
    delay(10);
  }

  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  cfg.fallback_board = m5::board_t::board_M5CardputerADV;
  M5Cardputer.begin(cfg, true);
  M5.Speaker.begin();
  M5.Speaker.setVolume(160);
  if (M5.Display.height() > M5.Display.width()) {
    M5.Display.setRotation(1);
  }

  Wire.end();
  Wire.begin(kGroveSda, kGroveScl, kI2cFrequency);
  delay(100);

  // GPIO5 is the LoRa Cap module's SPI CS pin. Both the SD card and the LoRa Cap
  // share the FSPI bus (SCK=40, MOSI=14, MISO=39). If GPIO5 is floating/LOW at
  // boot, the LoRa chip asserts itself on FSPI simultaneously with the SD, causing
  // bus contention and SD.begin() failure. Driving it HIGH deselects the LoRa chip
  // so the SD card has exclusive access to the bus. Same fix as the M5Stack
  // Launcher firmware (boards/m5stack-cardputer/interface.cpp line 84-86).
  pinMode(5, OUTPUT);
  digitalWrite(5, HIGH);

  // Key dictionary first, then microSD: load persisted slots, keys, and config so
  // they survive a power-cycle. Everything still works if no card is inserted.
  seedDefaultKeyDictionary();
  initSdStorage();
  if (sdReady) {
    loadKeysFromSd();
    loadAllSlotsFromSd();
    loadConfigFromSd();
  }
  applyBrightness();
  refreshRfidStatus();

  runBootSequence();

  if (rfidReady) {
    drawHome(String(sdReady ? "SD ok" : "no SD") + "  FW " + runningVersion());
  } else {
    drawLines("RFID2 not found", "Check Grove cable", "SDA=G2 SCL=G1", "I2C: " + lastI2cScan);
  }
  emitStatus("boot");
}

// Redraws just the 13px status bar strip and re-stamps it over the current HUD —
// called when USB or battery state changes between loop() iterations.
void refreshStatusBarLive() {
  if (resultScreenActive || optionsOpen) return;  // don't paint over result/options screens
  drawStatusBar();
}

void loop() {
  M5Cardputer.update();
  pollSerialCommands();
  handleKeyboardUi();

  // Live USB / battery / charge watcher: redraws the status bar immediately
  // whenever the cable is inserted or removed, a terminal connects/disconnects,
  // or the battery level crosses a threshold — no keypress needed.
  {
    const StatusSnapshot cur = readStatus();
    if (cur.usbPlugged != statusCache.usbPlugged ||
        cur.usbCdc     != statusCache.usbCdc     ||
        cur.charging   != statusCache.charging   ||
        (cur.batLevel  != statusCache.batLevel && cur.batLevel >= 0)) {
      refreshStatusBarLive();
    }
  }

  const uint32_t now = millis();
  if ((uint32_t)(now - lastStatusMs) >= kHeartbeatMs) {
    lastStatusMs = now;
    emitStatus("heartbeat");
  }

  if (!rfidReady) {
    delay(100);
    return;
  }

  pollCardPreview();
  delay(50);
}
