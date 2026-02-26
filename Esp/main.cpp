#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include "secrets.h" 


const unsigned long POLL_INTERVAL = 5000;          
const unsigned long DEBUG_INTERVAL = 10000;        
const unsigned long SHUTDOWN_RETRY_INTERVAL = 10000; 

unsigned long lastPollTime = 0;
unsigned long lastDebugTime = 0;
unsigned long lastShutdownAttempt = 0;
String lastProcessedRequestId = "";
bool shutdownMode = false;

WiFiUDP udp;

void setup() {
  Serial.begin(115200);
  connectWiFi();

  udp.begin(9); // WOL uses UDP port 9
}

void loop() {
  unsigned long currentMillis = millis();

  // Poll the server for wake/shutdown requests
  if (currentMillis - lastPollTime >= POLL_INTERVAL) {
    checkForWakeRequest();
    lastPollTime = currentMillis;
  }

  // Retry shutdown if in shutdown mode
  if (shutdownMode && currentMillis - lastShutdownAttempt >= SHUTDOWN_RETRY_INTERVAL) {
    Serial.println("Retrying shutdown command...");
    sendShutdownCommand();
    lastShutdownAttempt = currentMillis;
  }

  // Periodic Wi-Fi debug info
  // if (WiFi.status() == WL_CONNECTED && currentMillis - lastDebugTime >= DEBUG_INTERVAL) {
  //   Serial.println("WiFi connected. IP: " + WiFi.localIP().toString());
  //   if (shutdownMode) {
  //     Serial.println("Currently in SHUTDOWN mode - will retry until wake request is received");
  //   }
  //   lastDebugTime = currentMillis;
  // }

  delay(100);
}

// Wi-Fi connection 
void connectWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("WiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

// -------------------- Poll server --------------------
void checkForWakeRequest() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected, reconnecting...");
    WiFi.reconnect();
    return;
  }

  HTTPClient http;
  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "application/json");

  int httpResponseCode = http.GET();
  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println("Server response: " + response);

    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, response);

    if (error) {
      Serial.println("JSON parsing failed: " + String(error.c_str()));
      http.end();
      return;
    }

    bool shouldWake = doc["wake"];
    bool shouldShutdown = doc["shutdown"];
    String requestId = doc["id"] | "";
    
    // Wake request
    if (shouldWake && requestId != "" && requestId != lastProcessedRequestId) {
      Serial.println("New wake request received! ID: " + requestId);
      if (shutdownMode) {
        Serial.println("Exiting shutdown mode - wake request received");
        shutdownMode = false;
      }
      sendWOL();
      lastProcessedRequestId = requestId;
      acknowledgeRequest(requestId, "wake");
    }
    // Shutdown request
    else if (shouldShutdown && requestId != "" && requestId != lastProcessedRequestId) {
      Serial.println("New shutdown request received! ID: " + requestId);
      shutdownMode = true;
      sendShutdownCommand();
      lastShutdownAttempt = millis();
      lastProcessedRequestId = requestId;
      acknowledgeRequest(requestId, "shutdown");
    }
    else if ((shouldWake || shouldShutdown) && requestId == lastProcessedRequestId) {
      Serial.println("Duplicate request ignored: " + requestId);
    }
    else if (!shutdownMode) {
      Serial.println("No wake/shutdown request pending.");
    }

  } else {
    Serial.println("HTTP Error: " + String(httpResponseCode));
  }

  http.end();
}

//  Send Wake-on-LAN 
void sendWOL() {
  Serial.println("Sending Wake-on-LAN packet...");

  uint8_t macBytes[6];
  if (!parseMACAddress(TARGET_MAC, macBytes)) {
    Serial.println("Invalid MAC address format!");
    return;
  }

  uint8_t magicPacket[102];
  // 6 bytes of 0xFF
  for (int i = 0; i < 6; i++) magicPacket[i] = 0xFF;
  // Repeat MAC 16 times
  for (int i = 0; i < 16; i++)
    for (int j = 0; j < 6; j++)
      magicPacket[6 + i * 6 + j] = macBytes[j];

  udp.beginPacket(IPAddress(BROADCAST_IP), 9);
  udp.write(magicPacket, 102);
  if (udp.endPacket()) {
    Serial.println("WOL packet sent successfully!");
  } else {
    Serial.println("Failed to send WOL packet!");
  }
}

// Send Shutdown Command 
void sendShutdownCommand() {
  Serial.println("Sending shutdown command to PC...");

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, cannot send shutdown command!");
    return;
  }

  HTTPClient http;
  http.begin(PC_SHUTDOWN_URL);
  http.addHeader("Content-Type", "application/json");
  int httpResponseCode = http.POST("{}");

  if (httpResponseCode > 0) {
    Serial.println("Shutdown command sent successfully! Response code: " + String(httpResponseCode));
  } else {
    Serial.println("Failed to send shutdown command! Error code: " + String(httpResponseCode));
  }

  http.end();
}

// Parse MAC address 
bool parseMACAddress(const char* macStr, uint8_t* macBytes) {
  int values[6];
  if (sscanf(macStr, "%x:%x:%x:%x:%x:%x", 
             &values[0], &values[1], &values[2], 
             &values[3], &values[4], &values[5]) == 6) {
    for (int i = 0; i < 6; i++) macBytes[i] = (uint8_t)values[i];
    return true;
  }
  return false;
}

//  Acknowledge request 
void acknowledgeRequest(String requestId, String actionType) {
  HTTPClient http;
  http.begin(String(SERVER_URL) + "/ack");
  http.addHeader("Content-Type", "application/json");

  DynamicJsonDocument doc(256);
  doc["id"] = requestId;
  doc["status"] = "sent";
  doc["action"] = actionType;

  String payload;
  serializeJson(doc, payload);
  int httpResponseCode = http.POST(payload);

  if (httpResponseCode > 0) {
    Serial.println("Acknowledgment sent successfully. Code: " + String(httpResponseCode));
  } else {
    Serial.println("Failed to send acknowledgment! Error: " + String(httpResponseCode));
  }

  http.end();
}