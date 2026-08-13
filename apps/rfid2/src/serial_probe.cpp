#include <Arduino.h>
#include <M5Unified.h>

#if ARDUINO_USB_MODE && !ARDUINO_USB_CDC_ON_BOOT
#define DEBUG_PORT USBSerial
#define DEBUG_PORT_NAME "USBSerial/HWCDC"
#else
#define DEBUG_PORT Serial
#if ARDUINO_USB_MODE
#define DEBUG_PORT_NAME "Serial/HWCDC"
#else
#define DEBUG_PORT_NAME "Serial/USBCDC"
#endif
#endif

uint32_t lastPrintMs = 0;
uint32_t lastDrawMs = 0;
String command;
String lastCommand = "-";

bool debugPortConnected() {
  return (bool)DEBUG_PORT;
}

void drawProbe() {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setCursor(0, 0);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextSize(1);
  M5.Display.println("Cardputer ADV serial probe");
  M5.Display.println();
  M5.Display.printf("uptime: %lu ms\n", (unsigned long)millis());
  M5.Display.printf("port: %s\n", DEBUG_PORT_NAME);
  M5.Display.printf("usb connected: %s\n", debugPortConnected() ? "yes" : "no");
  M5.Display.printf("USB_MODE=%d CDC_ON_BOOT=%d\n", ARDUINO_USB_MODE, ARDUINO_USB_CDC_ON_BOOT);
  M5.Display.printf("last command: %s\n", lastCommand.c_str());
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(1);
  M5.Display.setTextFont(&fonts::Font2);
  M5.Display.setTextSize(1);
  M5.Display.fillScreen(TFT_BLACK);

  DEBUG_PORT.begin(115200);
  DEBUG_PORT.setDebugOutput(true);
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
    DEBUG_PORT.printf(
        "{\"event\":\"serial_probe\",\"uptime_ms\":%lu,\"port\":\"%s\",\"connected\":%s,"
        "\"usb_mode\":%d,\"cdc_on_boot\":%d}\n",
        (unsigned long)now,
        DEBUG_PORT_NAME,
        debugPortConnected() ? "true" : "false",
        ARDUINO_USB_MODE,
        ARDUINO_USB_CDC_ON_BOOT);
    DEBUG_PORT.flush();
  }

  while (DEBUG_PORT.available() > 0) {
    const char c = (char)DEBUG_PORT.read();
    if (c == '\n' || c == '\r') {
      command.trim();
      lastCommand = command.length() ? command : "<blank>";
      DEBUG_PORT.printf("{\"event\":\"echo\",\"command\":\"%s\",\"uptime_ms\":%lu}\n", command.c_str(), (unsigned long)millis());
      DEBUG_PORT.flush();
      command = "";
    } else if (command.length() < 80) {
      command += c;
    }
  }
}
