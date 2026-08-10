#include <Arduino.h>
#include <driver/usb_serial_jtag.h>

namespace {
uint32_t lastPrintMs = 0;
String command;

void usbWrite(const char* data) {
  if (!data) return;
  usb_serial_jtag_write_bytes(data, strlen(data), 0);
}

void usbPrintf(const char* fmt, ...) {
  char buffer[192];
  va_list args;
  va_start(args, fmt);
  const int n = vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  if (n <= 0) return;
  usb_serial_jtag_write_bytes(buffer, (size_t)min(n, (int)sizeof(buffer) - 1), 0);
}
}

void setup() {
  usb_serial_jtag_driver_config_t cfg = {};
  cfg.tx_buffer_size = 1024;
  cfg.rx_buffer_size = 1024;
  usb_serial_jtag_driver_install(&cfg);
  usbWrite("{\"event\":\"jtag_min_boot\"}\n");
}

void loop() {
  const uint32_t now = millis();
  if ((uint32_t)(now - lastPrintMs) >= 1000) {
    lastPrintMs = now;
    usbPrintf("{\"event\":\"jtag_min\",\"uptime_ms\":%lu}\n", (unsigned long)now);
  }

  uint8_t rx[64];
  const int got = usb_serial_jtag_read_bytes(rx, sizeof(rx), 0);
  for (int i = 0; i < got; ++i) {
    const char c = (char)rx[i];
    if (c == '\n' || c == '\r') {
      command.trim();
      usbPrintf("{\"event\":\"echo\",\"command\":\"%s\",\"uptime_ms\":%lu}\n",
                command.c_str(),
                (unsigned long)millis());
      command = "";
    } else if (command.length() < 80) {
      command += c;
    }
  }
  delay(1);
}
