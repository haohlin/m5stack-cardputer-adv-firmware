#pragma once

// Legacy compile-time fallback. Prefer provisioning runtime settings with:
//   CARDPUTER_WIFI_SSID="..." CARDPUTER_WIFI_PASS="..." ./scripts/write_bridge_config_folder.sh
//
// src/bridge_config.local.h is ignored because it contains your Wi-Fi
// password and bridge pairing token. Runtime NVS settings override this file.

#define CARDPUTER_BRIDGE_WIFI_SSID "YourWiFi"
#define CARDPUTER_BRIDGE_WIFI_PASS "YourWiFiPassword"
#define CARDPUTER_BRIDGE_HOST "192.168.1.10"
#define CARDPUTER_BRIDGE_PORT 17878
#define CARDPUTER_BRIDGE_TOKEN "paste-token-from-bridge-config-json"
#define CARDPUTER_BRIDGE_CA "-----BEGIN CERTIFICATE-----\npublic-ca-pem\n-----END CERTIFICATE-----\n"
#define CARDPUTER_BRIDGE_FW_LABEL "cardputer-adv-bridge-dev"
