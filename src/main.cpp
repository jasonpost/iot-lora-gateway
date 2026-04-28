#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_BME280.h>
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
Adafruit_BME280 bme;
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
unsigned long lastHealthPublishMs = 0;
unsigned long lastTemperaturePublishMs = 0;
unsigned long lastWiFiAttemptMs = 0;
unsigned long lastMQTTAttemptMs = 0;
int lastLoRaRxState = RADIOLIB_ERR_NONE;
uint32_t loraPacketsReceived = 0;
uint32_t loraDecodeFailures = 0;
uint32_t loraRxFailures = 0;
uint32_t mqttPublishSuccesses = 0;
uint32_t mqttPublishFailures = 0;
float lastPacketRSSI = 0.0;
float lastPacketSNR = 0.0;
bool temperatureReady = false;
float lastTemperatureC = 0.0;
float lastHumidityPercent = 0.0;
float lastPressureHpa = 0.0;

String jsonEscape(const String& value) {
  String escaped;
  escaped.reserve(value.length() + 8);

  for (size_t i = 0; i < value.length(); ++i) {
    char c = value.charAt(i);
    switch (c) {
      case '"':
        escaped += "\\\"";
        break;
      case '\\':
        escaped += "\\\\";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        if (c < 0x20) {
          char encoded[7];
          snprintf(encoded, sizeof(encoded), "\\u%04x", static_cast<unsigned char>(c));
          escaped += encoded;
        } else {
          escaped += c;
        }
        break;
    }
  }

  return escaped;
}

bool publishMQTT(const char* topic, const char* payload, bool retained = false) {
  bool ok = mqtt.publish(topic, payload, retained);
  if (ok) {
    ++mqttPublishSuccesses;
  } else {
    ++mqttPublishFailures;
  }
  return ok;
}

bool publishMQTT(const char* topic, const String& payload, bool retained = false) {
  return publishMQTT(topic, payload.c_str(), retained);
}

String discoveryDeviceJson() {
  return String("\"device\":{\"identifiers\":[\"") + DEVICE_ID + "\"],\"name\":\"" + DEVICE_NAME + "\"}";
}

void publishSensorDiscovery(
  const char* objectId,
  const char* name,
  const char* stateTopic,
  const char* valueTemplate,
  const char* unit = "",
  const char* deviceClass = "",
  const char* stateClass = "") {

  String topic = String(HA_DISCOVERY_PREFIX) + "/sensor/" + DEVICE_ID + "/" + objectId + "/config";
  String payload = String("{\"name\":\"") + name +
                   "\",\"unique_id\":\"" + DEVICE_ID + "_" + objectId +
                   "\",\"state_topic\":\"" + stateTopic +
                   "\",\"value_template\":\"" + valueTemplate +
                   "\",\"availability_topic\":\"" + GATEWAY_MQTT_TOPIC_AVAILABILITY +
                   "\",\"payload_available\":\"" + GATEWAY_AVAILABILITY_ONLINE +
                   "\",\"payload_not_available\":\"" + GATEWAY_AVAILABILITY_OFFLINE + "\"";

  if (unit[0] != '\0') {
    payload += String(",\"unit_of_measurement\":\"") + unit + "\"";
  }
  if (deviceClass[0] != '\0') {
    payload += String(",\"device_class\":\"") + deviceClass + "\"";
  }
  if (stateClass[0] != '\0') {
    payload += String(",\"state_class\":\"") + stateClass + "\"";
  }

  payload += "," + discoveryDeviceJson() + "}";
  publishMQTT(topic.c_str(), payload, true);
}

void publishBinarySensorDiscovery(
  const char* objectId,
  const char* name,
  const char* valueTemplate,
  const char* deviceClass = "") {

  String topic = String(HA_DISCOVERY_PREFIX) + "/binary_sensor/" + DEVICE_ID + "/" + objectId + "/config";
  String payload = String("{\"name\":\"") + name +
                   "\",\"unique_id\":\"" + DEVICE_ID + "_" + objectId +
                   "\",\"state_topic\":\"" + GATEWAY_MQTT_TOPIC_HEALTH +
                   "\",\"value_template\":\"" + valueTemplate +
                   "\",\"payload_on\":\"ON\",\"payload_off\":\"OFF\"" +
                   ",\"availability_topic\":\"" + GATEWAY_MQTT_TOPIC_AVAILABILITY +
                   "\",\"payload_available\":\"" + GATEWAY_AVAILABILITY_ONLINE +
                   "\",\"payload_not_available\":\"" + GATEWAY_AVAILABILITY_OFFLINE + "\"";

  if (deviceClass[0] != '\0') {
    payload += String(",\"device_class\":\"") + deviceClass + "\"";
  }

  payload += "," + discoveryDeviceJson() + "}";
  publishMQTT(topic.c_str(), payload, true);
}

void publishHomeAssistantDiscovery() {
  publishBinarySensorDiscovery("wifi_connected", "Wi-Fi Connected", "{{ 'ON' if value_json.wifi_connected else 'OFF' }}", "connectivity");
  publishBinarySensorDiscovery("mqtt_connected", "MQTT Connected", "{{ 'ON' if value_json.mqtt_connected else 'OFF' }}", "connectivity");
  publishBinarySensorDiscovery("lora_ready", "LoRa Ready", "{{ 'ON' if value_json.lora_ready else 'OFF' }}", "connectivity");
  publishBinarySensorDiscovery("temperature_available", "Temperature Sensor", "{{ 'ON' if value_json.temperature_available else 'OFF' }}", "connectivity");
  publishSensorDiscovery("uptime", "Uptime", GATEWAY_MQTT_TOPIC_HEALTH, "{{ value_json.uptime_s }}", "s", "duration", "total_increasing");
  publishSensorDiscovery("wifi_rssi", "Wi-Fi RSSI", GATEWAY_MQTT_TOPIC_HEALTH, "{{ value_json.wifi_rssi_dbm }}", "dBm", "signal_strength", "measurement");
  publishSensorDiscovery("packets_received", "LoRa Packets Received", GATEWAY_MQTT_TOPIC_HEALTH, "{{ value_json.packets_received }}", "", "", "total_increasing");
  publishSensorDiscovery("decode_failures", "LoRa Decode Failures", GATEWAY_MQTT_TOPIC_HEALTH, "{{ value_json.decode_failures }}", "", "", "total_increasing");
  publishSensorDiscovery("rx_failures", "LoRa RX Failures", GATEWAY_MQTT_TOPIC_HEALTH, "{{ value_json.rx_failures }}", "", "", "total_increasing");
  publishSensorDiscovery("mqtt_publish_failures", "MQTT Publish Failures", GATEWAY_MQTT_TOPIC_HEALTH, "{{ value_json.mqtt_publish_failures }}", "", "", "total_increasing");
  publishSensorDiscovery("last_packet_rssi", "Last LoRa RSSI", GATEWAY_MQTT_TOPIC_HEALTH, "{{ value_json.last_packet_rssi_dbm }}", "dBm", "signal_strength", "measurement");
  publishSensorDiscovery("last_packet_snr", "Last LoRa SNR", GATEWAY_MQTT_TOPIC_HEALTH, "{{ value_json.last_packet_snr_db }}", "dB", "", "measurement");
  publishSensorDiscovery("free_heap", "Free Heap", GATEWAY_MQTT_TOPIC_HEALTH, "{{ value_json.free_heap }}", "B", "data_size", "measurement");
  publishSensorDiscovery("temperature", "Attic Temperature", GATEWAY_MQTT_TOPIC_TEMPERATURE, "{{ value_json.temperature_c }}", "°C", "temperature", "measurement");
  publishSensorDiscovery("humidity", "Attic Humidity", GATEWAY_MQTT_TOPIC_TEMPERATURE, "{{ value_json.humidity_percent }}", "%", "humidity", "measurement");
  publishSensorDiscovery("pressure", "Attic Pressure", GATEWAY_MQTT_TOPIC_TEMPERATURE, "{{ value_json.pressure_hpa }}", "hPa", "pressure", "measurement");
  Serial.println("Home Assistant discovery published");
}

bool readTemperatureSensor() {
  if (!temperatureReady) {
    return false;
  }

  lastTemperatureC = bme.readTemperature();
  lastHumidityPercent = bme.readHumidity();
  lastPressureHpa = bme.readPressure() / 100.0F;

  return !isnan(lastTemperatureC);
}

void publishTemperature() {
  if (!mqttReady || !readTemperatureSensor()) {
    return;
  }

  char payload[96];
  snprintf(
    payload,
    sizeof(payload),
    "{\"temperature_c\":%.2f,\"humidity_percent\":%.2f,\"pressure_hpa\":%.2f}",
    lastTemperatureC,
    lastHumidityPercent,
    lastPressureHpa);

  if (publishMQTT(GATEWAY_MQTT_TOPIC_TEMPERATURE, payload, true)) {
    Serial.println("MQTT temperature OK");
  } else {
    Serial.println("MQTT temperature FAIL");
  }
}

void publishHealth() {
  if (!mqttReady) {
    return;
  }

  char payload[512];
  char temperatureJson[16];
  String ip = (wifiReady && WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "";
  int wifiRSSI = (wifiReady && WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;

  if (temperatureReady && readTemperatureSensor()) {
    snprintf(temperatureJson, sizeof(temperatureJson), "%.2f", lastTemperatureC);
  } else {
    snprintf(temperatureJson, sizeof(temperatureJson), "null");
  }

  snprintf(
    payload,
    sizeof(payload),
    "{\"device_id\":\"%s\",\"uptime_s\":%lu,\"wifi_connected\":%s,\"wifi_rssi_dbm\":%d,\"ip\":\"%s\",\"mqtt_connected\":%s,\"lora_ready\":%s,\"last_lora_state\":%d,\"packets_received\":%lu,\"decode_failures\":%lu,\"rx_failures\":%lu,\"mqtt_publish_successes\":%lu,\"mqtt_publish_failures\":%lu,\"last_packet_rssi_dbm\":%.1f,\"last_packet_snr_db\":%.1f,\"temperature_available\":%s,\"temperature_c\":%s,\"free_heap\":%lu}",
    DEVICE_ID,
    static_cast<unsigned long>(millis() / 1000UL),
    (wifiReady && WiFi.status() == WL_CONNECTED) ? "true" : "false",
    wifiRSSI,
    ip.c_str(),
    mqttReady ? "true" : "false",
    loraReady ? "true" : "false",
    lastLoRaRxState,
    static_cast<unsigned long>(loraPacketsReceived),
    static_cast<unsigned long>(loraDecodeFailures),
    static_cast<unsigned long>(loraRxFailures),
    static_cast<unsigned long>(mqttPublishSuccesses),
    static_cast<unsigned long>(mqttPublishFailures),
    lastPacketRSSI,
    lastPacketSNR,
    temperatureReady ? "true" : "false",
    temperatureJson,
    static_cast<unsigned long>(ESP.getFreeHeap()));

  if (publishMQTT(GATEWAY_MQTT_TOPIC_HEALTH, payload, true)) {
    Serial.println("MQTT health OK");
  } else {
    Serial.println("MQTT health FAIL");
  }
}

void setupTemperatureSensor() {
  if (!TEMPERATURE_SENSOR_ENABLED) {
    Serial.println("Temperature sensor disabled");
    return;
  }

  temperatureReady = bme.begin(BME280_ADDRESS_PRIMARY, &Wire);
  if (!temperatureReady) {
    temperatureReady = bme.begin(BME280_ADDRESS_SECONDARY, &Wire);
  }

  if (temperatureReady) {
    readTemperatureSensor();
    Serial.println("BME280 OK");
  } else {
    Serial.println("BME280 not found");
  }
}

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
  if (mqtt.connect(
        MQTT_CLIENT_ID,
        MQTT_USER,
        MQTT_PASS,
        GATEWAY_MQTT_TOPIC_AVAILABILITY,
        1,
        true,
        GATEWAY_AVAILABILITY_OFFLINE)) {
    mqttReady = true;
    Serial.println("OK");
    publishMQTT(GATEWAY_MQTT_TOPIC_AVAILABILITY, GATEWAY_AVAILABILITY_ONLINE, true);
    publishMQTT(GATEWAY_MQTT_TOPIC_STATUS, "gateway bring-up online");
    publishHomeAssistantDiscovery();
    lastHeartbeatMs = millis();
    lastHealthPublishMs = 0;
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
  mqtt.setBufferSize(MQTT_BUFFER_SIZE);

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
  setupTemperatureSensor();
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
      if (publishMQTT(GATEWAY_MQTT_TOPIC_STATUS, "gateway alive")) {
        Serial.println("MQTT heartbeat OK");
      } else {
        Serial.println("MQTT heartbeat FAIL");
      }
    }

    if (lastHealthPublishMs == 0 || now - lastHealthPublishMs >= MQTT_HEALTH_PUBLISH_MS) {
      lastHealthPublishMs = now;
      publishHealth();
    }

    if (temperatureReady && (lastTemperaturePublishMs == 0 || now - lastTemperaturePublishMs >= MQTT_TEMPERATURE_PUBLISH_MS)) {
      lastTemperaturePublishMs = now;
      publishTemperature();
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
      ++loraPacketsReceived;
      lastPacketRSSI = radio.getRSSI();
      lastPacketSNR = radio.getSNR();
      Serial.print("LoRa RX: ");
      Serial.println(incoming);
      Serial.print("RSSI: ");
      Serial.print(lastPacketRSSI);
      Serial.print(" dBm | SNR: ");
      Serial.println(lastPacketSNR);

      if (mqttReady) {
        String payload = "{\"payload\":\"" + jsonEscape(incoming) +
                         "\",\"rssi_dbm\":" + String(lastPacketRSSI, 1) +
                         ",\"snr_db\":" + String(lastPacketSNR, 1) +
                         ",\"packet_count\":" + String(loraPacketsReceived) + "}";

        if (publishMQTT(GATEWAY_MQTT_TOPIC_RX, payload)) {
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
      ++loraDecodeFailures;
      Serial.print("LoRa heard packet but could not decode it, state=");
      Serial.println(state);
    } else if (state != RADIOLIB_ERR_RX_TIMEOUT) {
      lastLoRaRxState = state;
      ++loraRxFailures;
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
      } else if (temperatureReady && readTemperatureSensor()) {
        String tempLine = "Temp " + String(lastTemperatureC, 1) + " C";
        showStatus("Gateway Ready", "MQTT OK", tempLine.c_str());
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
