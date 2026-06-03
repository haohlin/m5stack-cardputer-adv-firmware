#include <Arduino.h>
#include <M5Unified.h>
#include <driver/usb_serial_jtag.h>

namespace {
uint32_t lastPrintMs = 0;
uint32_t lastDrawMs = 0;
String command;
String lastCommand = "-";

void usbWrite(const char* data) {
  if (!data) return;
  usb_serial_jtag_write_bytes(data, strlen(data), 0);
}

void usbPrintf(const char* fmt, ...) {
  char buffer[256];
  va_list args;
  va_start(args, fmt);
  const int n = vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  if (n <= 0) return;
  const size_t len = (size_t)min(n, (int)sizeof(buffer) - 1);
  usb_serial_jtag_write_bytes(buffer, len, 0);
}

void drawProbe() {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setCursor(0, 0);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextSize(1);
  M5.Display.println("USB-JTAG driver probe");
  M5.Display.println();
  M5.Display.printf("uptime: %lu ms\n", (unsigned long)millis());
  M5.Display.println("port: usb_serial_jtag");
  M5.Display.printf("last command: %s\n", lastCommand.c_str());
  M5.Display.println();
  M5.Display.println("Host should see JSON");
  M5.Display.println("over /dev/cu.usbmodem*");
}
}

void setup() {
  auto cfg = M5.config();
  cfg.fallback_board = m5::board_t::board_M5CardputerADV;
  M5.begin(cfg);
  M5.Display.setRotation(1);
  M5.Display.setFont(&fonts::Font2);
  M5.Display.fillScreen(TFT_BLACK);

  usb_serial_jtag_driver_config_t usbConfig = {};
  usbConfig.tx_buffer_size = 1024;
  usbConfig.rx_buffer_size = 1024;
  usb_serial_jtag_driver_install(&usbConfig);
  usbWrite("{\"event\":\"jtag_probe_boot\"}\n");
  drawProbe();
}

void loop() {
  M5.update();
  const uint32_t now = millis();
  if ((uint32_t)(now - lastDrawMs) >= 250) {
    lastDrawMs = now;
    drawProbe();
  }

  if ((uint32_t)(now - lastPrintMs) >= 1000) {
    lastPrintMs = now;
    usbPrintf("{\"event\":\"jtag_probe\",\"uptime_ms\":%lu}\n", (unsigned long)now);
  }

  uint8_t rx[64];
  const int got = usb_serial_jtag_read_bytes(rx, sizeof(rx), 0);
  for (int i = 0; i < got; ++i) {
    const char c = (char)rx[i];
    if (c == '\n' || c == '\r') {
      command.trim();
      lastCommand = command.length() ? command : "<blank>";
      usbPrintf("{\"event\":\"echo\",\"command\":\"%s\",\"uptime_ms\":%lu}\n",
                command.c_str(),
                (unsigned long)millis());
      command = "";
    } else if (command.length() < 80) {
      command += c;
    }
  }
}
