// secrets.h
#pragma once

// -------------------- Wi-Fi credentials --------------------
#define WIFI_SSID "DummySSID"
#define WIFI_PASSWORD "DummyPassword"

// -------------------- Server configuration --------------------
#define SERVER_URL "dummyserver.com/api/wake"  // Replace with your actual server URL

// -------------------- Target PC configuration --------------------
#define TARGET_MAC "dummymacaddress"      // Ubuntu PC MAC address
#define BROADCAST_IP 192, 168, 0, 255      // Match your network subnet
#define PC_SHUTDOWN_URL "http://192.168.0.109:8080/shutdown"
