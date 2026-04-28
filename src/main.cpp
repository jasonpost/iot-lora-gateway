#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "config.h"
#include "secrets.h"

static constexpr int PIN_LED = 35;
static constexpr int PIN_VEXT = 36;
static constexpr int PIN_OLED_RST = 21;
static constexpr int PIN_OLED_SDA = 17;
static constexpr int PIN_OLED_SCL = 18;
static constexpr uint8_t OLED_ADDRESS = 0x3C;
static constexpr int PIN_LORA_NSS = 8;
static constexpr int PIN_LORA_SCK = 9;
static constexpr int PIN_LORA_MOSI = 10;
static constexpr int PIN_LORA_MISO = 11;
static constexpr int PIN_LORA_RST = 12;
static constexpr int PIN_LORA_BUSY = 13;
static constexpr int PIN_LORA_DIO1 = 14;

static constexpr int SCREEN_WIDTH = 128;
static constexpr int SCREEN_HEIGHT = 64;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, PIN_OLED_RST);
SPIClass loraSPI(FSPI);
SX1262 radio = new Module(PIN_LORA_NSS, PIN_LORA_DIO1, PIN_LORA_RST, PIN_LORA_BUSY, loraSPI);
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

bool loraReady = false;
int loraInitCode = 0;
bool wifiReady = false;
bool mqttReady = false;
String lastLoRaMessage;
unsigned long lastHeartbeatMs = 0;
unsigned long lastWiFiAttemptMs = 0;
unsigned long lastMQTTAttemptMs = 0;
int lastLoRaRxState = RADIOLIB_ERR_NONE;

void showStatus(const char* line1, const char* line2 = "", const char* line3 = "") {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println(line1);
  if (line2[0] != '\0') display.println(line2);
  if (line3[0] != '\0') display.println(line3);
  display.display();
}

void setupLoRa() {
  loraSPI.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_NSS);

  loraInitCode = radio.begin(915.0);
  if (loraInitCode == RADIOLIB_ERR_NONE) {
    radio.setOutputPower(14);
    radio.setSpreadingFactor(9);
    radio.setBandwidth(125.0);
    radio.setCodingRate(7);
    radio.setSyncWord(0x12);
    loraReady = true;
    Serial.println("LoRa OK");
    Serial.println("LoRa RX cfg: 915.0 MHz, SF9, BW125, CR 4/7, SW 0x12, CRC on");
  } else {
    Serial.print("LoRa init failed: ");
    Serial.println(loraInitCode);
  }
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  lastWiFiAttemptMs = millis();
  Serial.println("WiFi connect started");
}

void connectMQTT() {
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  lastMQTTAttemptMs = millis();

  mqttReady = false;
  if (!wifiReady || WiFi.status() != WL_CONNECTED) {
    Serial.println("MQTT skipped: WiFi not connected");
    return;
  }

  Serial.print("Connecting MQTT...");
  if (mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS)) {
    mqttReady = true;
    Serial.println("OK");
    mqtt.publish(MQTT_TOPIC_STATUS, "gateway bring-up online");
    lastHeartbeatMs = millis();
  } else {
    Serial.print("FAIL rc=");
    Serial.println(mqtt.state());
  }
}

void serviceConnectivity(unsigned long now) {
  wl_status_t wifiStatus = WiFi.status();
  bool connectedNow = (wifiStatus == WL_CONNECTED);

  if (connectedNow && !wifiReady) {
    wifiReady = true;
    Serial.print("WiFi OK: ");
    Serial.println(WiFi.localIP());
  } else if (!connectedNow && wifiReady) {
    wifiReady = false;
    mqttReady = false;
    Serial.println("WiFi disconnected");
  }

  if (!connectedNow && (now - lastWiFiAttemptMs >= WIFI_RETRY_MS || lastWiFiAttemptMs == 0)) {
    connectWiFi();
  }

  if (wifiReady) {
    if (!mqtt.connected() && (now - lastMQTTAttemptMs >= MQTT_RETRY_MS || lastMQTTAttemptMs == 0)) {
      connectMQTT();
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  pinMode(PIN_VEXT, OUTPUT);
  digitalWrite(PIN_VEXT, LOW);

  pinMode(PIN_OLED_RST, OUTPUT);
  digitalWrite(PIN_OLED_RST, LOW);
  delay(50);
  digitalWrite(PIN_OLED_RST, HIGH);
  delay(50);

  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);

  Serial.println();
  Serial.println("Heltec V4 bring-up");
  Serial.println("Serial OK");

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("OLED init failed");
    for (;;) {
      digitalWrite(PIN_LED, HIGH);
      delay(150);
      digitalWrite(PIN_LED, LOW);
      delay(150);
    }
  }

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  Serial.println("OLED OK");
  setupLoRa();
  connectWiFi();

  if (mqttReady) {
    showStatus("Gateway Ready", "MQTT OK", WiFi.localIP().toString().c_str());
  } else if (loraReady && wifiReady) {
    showStatus("Gateway Ready", "LoRa OK", WiFi.localIP().toString().c_str());
  } else if (loraReady) {
    showStatus("Gateway Ready", "LoRa OK", "WiFi FAIL");
  } else if (wifiReady) {
    showStatus("Gateway Ready", "LoRa FAIL", WiFi.localIP().toString().c_str());
  } else {
    showStatus("Gateway Ready", "LoRa FAIL", "WiFi FAIL");
  }

  for (int i = 0; i < 6; ++i) {
    digitalWrite(PIN_LED, HIGH);
    delay(120);
    digitalWrite(PIN_LED, LOW);
    delay(120);
  }
}

void loop() {
  static unsigned long lastToggleMs = 0;
  static bool ledOn = false;
  static unsigned long lastStatusMs = 0;
  const unsigned long now = millis();

  if (mqttReady) {
    if (now - lastHeartbeatMs >= MQTT_HEARTBEAT_MS) {
      lastHeartbeatMs = now;
      if (mqtt.publish(MQTT_TOPIC_STATUS, "gateway alive")) {
        Serial.println("MQTT heartbeat OK");
      } else {
        Serial.println("MQTT heartbeat FAIL");
      }
    }
  }
  serviceConnectivity(now);
  mqttReady = mqtt.connected();
  mqtt.loop();

  if (loraReady) {
    String incoming;
    int state = radio.receive(incoming, 0, 100);

    if (state == RADIOLIB_ERR_NONE) {
      lastLoRaRxState = state;
      lastLoRaMessage = incoming;
      Serial.print("LoRa RX: ");
      Serial.println(incoming);
      Serial.print("RSSI: ");
      Serial.print(radio.getRSSI());
      Serial.print(" dBm | SNR: ");
      Serial.println(radio.getSNR());

      if (mqttReady) {
        if (mqtt.publish(MQTT_TOPIC_RX, incoming.c_str())) {
          Serial.println("MQTT publish OK");
          showStatus("LoRa RX", incoming.substring(0, 20).c_str(), "Published MQTT");
        } else {
          Serial.println("MQTT publish FAIL");
          showStatus("LoRa RX", incoming.substring(0, 20).c_str(), "Publish FAIL");
        }
      } else {
        Serial.println("MQTT unavailable for publish");
        showStatus("LoRa RX", incoming.substring(0, 20).c_str(), "MQTT offline");
      }
    } else if (state == RADIOLIB_ERR_CRC_MISMATCH || state == RADIOLIB_ERR_LORA_HEADER_DAMAGED) {
      lastLoRaRxState = state;
      Serial.print("LoRa heard packet but could not decode it, state=");
      Serial.println(state);
    } else if (state != RADIOLIB_ERR_RX_TIMEOUT) {
      lastLoRaRxState = state;
      Serial.print("LoRa RX failed: ");
      Serial.println(state);
    }
  }

  if (now - lastToggleMs >= 500) {
    lastToggleMs = now;
    ledOn = !ledOn;
    digitalWrite(PIN_LED, ledOn ? HIGH : LOW);
  }

  if (now - lastStatusMs >= 3000) {
    lastStatusMs = now;
    Serial.print(ledOn ? "Heartbeat ON" : "Heartbeat OFF");
    Serial.print(" | LoRa=");
    if (loraReady) {
      Serial.print("OK");
      if (lastLoRaRxState != RADIOLIB_ERR_NONE) {
        Serial.print(" state=");
        Serial.print(lastLoRaRxState);
      }
    } else {
      Serial.print("FAIL(");
      Serial.print(loraInitCode);
      Serial.print(")");
    }

    Serial.print(" | WiFi=");
    if (wifiReady && WiFi.status() == WL_CONNECTED) {
      Serial.print(WiFi.localIP());
    } else {
      wifiReady = false;
      Serial.print("FAIL");
    }

    Serial.print(" | MQTT=");
    if (mqttReady) {
      Serial.println("OK");
      if (lastLoRaMessage.length() > 0) {
        showStatus("Gateway Ready", "MQTT OK", lastLoRaMessage.substring(0, 20).c_str());
      } else {
        showStatus("Gateway Ready", "MQTT OK", WiFi.localIP().toString().c_str());
      }
    } else {
      Serial.println("FAIL");
      if (wifiReady && WiFi.status() == WL_CONNECTED) {
        showStatus("Gateway Ready", loraReady ? "LoRa OK" : "LoRa FAIL", "MQTT FAIL");
      } else {
        showStatus("Gateway Ready", loraReady ? "LoRa OK" : "LoRa FAIL", "WiFi FAIL");
      }
    }
  }
}
