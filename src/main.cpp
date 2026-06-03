#include <Arduino.h>
#include <M5Unified.h>
#include <MFRC522_I2C.h>
#include <Wire.h>

namespace {
constexpr char kFwName[] = "cardputer-rfid2-fw";
constexpr char kFwVersion[] = "0.4.0-manual-slots";
constexpr uint8_t kRfidI2cAddress = 0x28;
constexpr int kRfidResetPin = -1;
constexpr int kGroveSda = 2;
constexpr int kGroveScl = 1;
constexpr uint32_t kI2cFrequency = 400000;
constexpr uint32_t kHeartbeatMs = 3000;
constexpr uint32_t kWriteArmWindowMs = 8000;
constexpr size_t kCommandMax = 96;
constexpr uint8_t kClassic1kBlocks = 64;
constexpr uint8_t kClassic1kSectors = 16;
constexpr uint8_t kClassicBlockSize = 16;
constexpr uint8_t kDumpSlotCount = 4;

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
  uint8_t blocksRead = 0;
  uint8_t sectorsRead = 0;
  uint8_t sectorsFailed = 0;
  bool readable[kClassic1kBlocks] = {};
  byte data[kClassic1kBlocks][kClassicBlockSize] = {};
};

enum class UiMode : uint8_t {
  Read = 0,
  Write = 1,
};

StoredDump storedDumps[kDumpSlotCount];
UiMode selectedMode = UiMode::Read;
uint8_t selectedSlot = 0;
uint32_t nextDumpVersion = 1;
bool operationArmed = false;
UiMode armedMode = UiMode::Read;
uint8_t armedSlot = 0;
uint32_t armedUntilMs = 0;
bool writeDone = false;

void drawLines(const char* title, const String& line1 = "", const String& line2 = "", const String& line3 = "") {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setCursor(0, 0);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextSize(1);
  M5.Display.println(title);
  M5.Display.println();
  if (line1.length()) M5.Display.println(line1);
  if (line2.length()) M5.Display.println(line2);
  if (line3.length()) M5.Display.println(line3);
}

const char* modeName(UiMode mode) {
  return mode == UiMode::Read ? "READ" : "WRITE";
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
  return operationArmed && armedMode == selectedMode && armedSlot == selectedSlot &&
         (int32_t)(armedUntilMs - millis()) > 0;
}

void cancelArm() {
  operationArmed = false;
  armedUntilMs = 0;
}

void drawHome(const String& footer = "") {
  String action = selectedMode == UiMode::Read ? "Hold BtnA: read" : "Hold BtnA: arm";
  if (isArmedForSelection()) {
    action = selectedMode == UiMode::Read ? "Hold again: overwrite" : "Hold again: write";
  }
  drawLines("RFID2 manual mode", selectedSummary(), "Click BtnA: next", footer.length() ? footer : action);
}

void setSelection(UiMode mode, uint8_t slot, const String& footer = "") {
  selectedMode = mode;
  selectedSlot = slot < kDumpSlotCount ? slot : 0;
  cancelArm();
  drawHome(footer);
}

void advanceSelection() {
  uint8_t index = selectedSlot * 2 + (selectedMode == UiMode::Write ? 1 : 0);
  index = (index + 1) % (kDumpSlotCount * 2);
  selectedSlot = index / 2;
  selectedMode = (index % 2) ? UiMode::Write : UiMode::Read;
  cancelArm();
  drawHome();
}

void armSelection() {
  operationArmed = true;
  armedMode = selectedMode;
  armedSlot = selectedSlot;
  armedUntilMs = millis() + kWriteArmWindowMs;
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

void setFactoryDefaultKey(MFRC522_I2C::MIFARE_Key& key) {
  for (byte& keyByte : key.keyByte) {
    keyByte = 0xFF;
  }
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
  printJsonString(kFwVersion);
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
  Serial.print('}');
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

  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
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
  if (operationArmed && armedSlot == slot) cancelArm();
  emitMessage("clear", slotTitle(slot) + " cleared");
  if (redraw) drawHome(slotTitle(slot) + " cleared");
}

void clearAllSlots() {
  for (uint8_t slot = 0; slot < kDumpSlotCount; ++slot) {
    storedDumps[slot] = StoredDump();
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

  MFRC522_I2C::MIFARE_Key key;
  setFactoryDefaultKey(key);

  for (uint8_t sector = 0; sector < kClassic1kSectors; ++sector) {
    const uint8_t firstBlock = sector * 4;
    byte status = rfid.PCD_Authenticate(MFRC522_I2C::PICC_CMD_MF_AUTH_KEY_A, firstBlock, &key, &rfid.uid);
    if (status != MFRC522_I2C::STATUS_OK) {
      nextDump.sectorsFailed++;
      emitBlockError("auth_read", sector, firstBlock, status);
      continue;
    }

    bool sectorHadRead = false;
    for (uint8_t offset = 0; offset < 3; ++offset) {
      const uint8_t block = firstBlock + offset;
      byte buffer[18] = {};
      byte byteCount = sizeof(buffer);
      status = rfid.MIFARE_Read(block, buffer, &byteCount);
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
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  nextDump.valid = nextDump.blocksRead > 0;
  storedDumps[slot] = nextDump;
  writeDone = false;
  emitStoredSummary("store", slot, storedDumps[slot], storedDumps[slot].valid ? "ok" : "no_blocks_read");
  if (storedDumps[slot].valid) {
    drawLines("Read saved", slotTitle(slot) + " v" + String(storedDumps[slot].version), "Blocks: " + String(storedDumps[slot].blocksRead), "Click next / hold run");
  } else {
    drawLines("Read failed", slotTitle(slot), "No readable blocks", "Default key only");
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

  MFRC522_I2C::MIFARE_Key key;
  setFactoryDefaultKey(key);

  uint8_t blocksWritten = 0;
  uint8_t writeFailures = 0;
  for (uint8_t sector = 0; sector < kClassic1kSectors; ++sector) {
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

    byte status = rfid.PCD_Authenticate(MFRC522_I2C::PICC_CMD_MF_AUTH_KEY_A, firstBlock, &key, &rfid.uid);
    if (status != MFRC522_I2C::STATUS_OK) {
      writeFailures += 3;
      emitBlockError("auth_write", sector, firstBlock, status);
      continue;
    }

    for (uint8_t offset = 0; offset < 3; ++offset) {
      const uint8_t block = firstBlock + offset;
      if (!dump.readable[block] || !isCopyableClassicDataBlock(block)) continue;
      status = rfid.MIFARE_Write(block, dump.data[block], kClassicBlockSize);
      if (status == MFRC522_I2C::STATUS_OK) {
        blocksWritten++;
      } else {
        writeFailures++;
        emitBlockError("write", sector, block, status);
      }
    }
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

void writeLiteralDataBlock(const String& command) {
  const int firstSpace = command.indexOf(' ');
  const int secondSpace = firstSpace < 0 ? -1 : command.indexOf(' ', firstSpace + 1);
  if (firstSpace < 0 || secondSpace < 0) {
    emitMessage("error", "usage: write-block <block 1-62 non-trailer> <32 hex chars>");
    drawLines("write-block usage", "write-block <block>", "<32 hex chars>");
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

  MFRC522_I2C::MIFARE_Key key;
  setFactoryDefaultKey(key);

  const uint8_t sector = block / 4;
  const uint8_t firstBlock = sector * 4;
  byte status = rfid.PCD_Authenticate(MFRC522_I2C::PICC_CMD_MF_AUTH_KEY_A, firstBlock, &key, &rfid.uid);
  if (status == MFRC522_I2C::STATUS_OK) {
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
  const bool confirmed = tail.indexOf("confirm") >= 0;
  tail.replace("confirm", "");
  tail.trim();
  return confirmed;
}

int parseOptionalSlot(String tail, bool& confirmed) {
  confirmed = consumeConfirm(tail);
  if (!tail.length()) return selectedSlot;
  return parseSlotNumber(tail);
}

void executeSelectedAction() {
  if (selectedMode == UiMode::Read) {
    if (storedDumps[selectedSlot].valid && !isArmedForSelection()) {
      armSelection();
      emitMessage("armed", "overwrite armed for " + slotTitle(selectedSlot) + "; hold BtnA again within 8s");
      drawLines("Overwrite armed", slotSummary(selectedSlot), "Hold BtnA again", "or click to cancel");
      return;
    }
    cancelArm();
    storePresentClassic1k(selectedSlot);
    return;
  }

  if (!storedDumps[selectedSlot].valid) {
    emitMessage("error", "write blocked: " + slotTitle(selectedSlot) + " is empty");
    drawLines("Write blocked", slotTitle(selectedSlot) + " empty", "Select READ first");
    return;
  }

  if (!isArmedForSelection()) {
    armSelection();
    emitMessage("armed", "write armed for " + slotSummary(selectedSlot) + "; hold BtnA again within 8s");
    drawLines("Write armed", slotSummary(selectedSlot), "Hold BtnA again", "or click to cancel");
    return;
  }

  cancelArm();
  writeStoredDumpToPresentClassic1k(selectedSlot);
}

void handleButtonUi() {
  if (operationArmed && (int32_t)(armedUntilMs - millis()) <= 0) {
    cancelArm();
    drawHome("Arm expired");
  }

  if (M5.BtnA.wasHold()) {
    executeSelectedAction();
  } else if (M5.BtnA.wasClicked()) {
    advanceSelection();
  }
}

void pollCardPreview() {
  if (!rfidReady) return;
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return;

  const uint32_t now = millis();
  const uint8_t piccType = rfid.PICC_GetType(rfid.uid.sak);
  const String uid = uidToString();
  const String typeName = rfid.PICC_GetTypeName(piccType);
  const bool shouldReport = !lastCard.valid || uid != lastCard.uid || (uint32_t)(now - lastCard.seenAtMs) > 5000;

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

    if (isArmedForSelection()) {
      drawLines("Card detected", "UID: " + uid, selectedSummary(), "Hold BtnA to run");
    } else {
      drawHome("Card: " + uid);
    }
  }

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
    } else {
      emitMessage("error", "usage: mode read|write");
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
    } else {
      setSelection(UiMode::Write, (uint8_t)parsedSlot);
      writeStoredDumpToPresentClassic1k((uint8_t)parsedSlot);
    }
  } else if (command.startsWith("write-block ")) {
    writeLiteralDataBlock(command);
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
    emitMessage("version", String(kFwName) + " " + kFwVersion);
  } else if (command == "help") {
    emitMessage("help", "commands: status, slots, next, ui, mode read|write, slot <1-4>, scan, store [slot] [confirm], dump [slot], write [slot] confirm, write-block <block> <32hex>, clear [slot]|all confirm, reset-rfid, version, help");
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
  M5.begin(cfg);
  if (M5.Display.height() > M5.Display.width()) {
    M5.Display.setRotation(1);
  }

  Wire.end();
  Wire.begin(kGroveSda, kGroveScl, kI2cFrequency);
  delay(100);

  refreshRfidStatus();
  if (rfidReady) {
    drawHome("FW " + String(kFwVersion));
  } else {
    drawLines("RFID2 not found", "Check Grove cable", "SDA=G2 SCL=G1", "I2C: " + lastI2cScan);
  }
  emitStatus("boot");
}

void loop() {
  M5.update();
  pollSerialCommands();
  handleButtonUi();

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
