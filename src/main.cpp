#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <stdarg.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_BME280.h>
#include "config.h"
#include "secrets.h"

static constexpr int SCREEN_WIDTH = 128;
static constexpr int SCREEN_HEIGHT = 64;
static constexpr size_t LORA_MQTT_PAYLOAD_BUFFER_SIZE = 512;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, PIN_OLED_RST);
Adafruit_BME280 bme;
SPIClass loraSPI(FSPI);
SX1262 radio = new Module(PIN_LORA_NSS, PIN_LORA_DIO1, PIN_LORA_RST, PIN_LORA_BUSY, loraSPI);
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

char GATEWAY_MQTT_BASE_TOPIC[MQTT_TOPIC_BUFFER_SIZE];
char GATEWAY_MQTT_TOPIC_STATUS[MQTT_TOPIC_BUFFER_SIZE];
char GATEWAY_MQTT_TOPIC_AVAILABILITY[MQTT_TOPIC_BUFFER_SIZE];
char GATEWAY_MQTT_TOPIC_HEALTH[MQTT_TOPIC_BUFFER_SIZE];
char GATEWAY_MQTT_TOPIC_RX[MQTT_TOPIC_BUFFER_SIZE];
char GATEWAY_MQTT_TOPIC_TEMPERATURE[MQTT_TOPIC_BUFFER_SIZE];

struct BinarySensorDiscoveryDef {
  const char* objectId;
  const char* name;
  const char* valueTemplate;
  const char* deviceClass;
};

struct SensorDiscoveryDef {
  const char* objectId;
  const char* name;
  const char* stateTopic;
  const char* valueTemplate;
  const char* unit;
  const char* deviceClass;
  const char* stateClass;
};

const BinarySensorDiscoveryDef BINARY_SENSOR_DISCOVERY[] = {
  {"wifi_connected", "Wi-Fi Connected", "{{ 'ON' if value_json.wifi_connected else 'OFF' }}", "connectivity"},
  {"mqtt_connected", "MQTT Connected", "{{ 'ON' if value_json.mqtt_connected else 'OFF' }}", "connectivity"},
  {"lora_ready", "LoRa Ready", "{{ 'ON' if value_json.lora_ready else 'OFF' }}", "connectivity"},
  {"temperature_available", "Temperature Sensor", "{{ 'ON' if value_json.temperature_available else 'OFF' }}", "connectivity"},
};

const SensorDiscoveryDef SENSOR_DISCOVERY[] = {
  {"uptime", "Uptime", GATEWAY_MQTT_TOPIC_HEALTH, "{{ value_json.uptime_s }}", "s", "duration", "total_increasing"},
  {"wifi_rssi", "Wi-Fi RSSI", GATEWAY_MQTT_TOPIC_HEALTH, "{{ value_json.wifi_rssi_dbm }}", "dBm", "signal_strength", "measurement"},
  {"packets_received", "LoRa Packets Received", GATEWAY_MQTT_TOPIC_HEALTH, "{{ value_json.packets_received }}", "", "", "total_increasing"},
  {"decode_failures", "LoRa Decode Failures", GATEWAY_MQTT_TOPIC_HEALTH, "{{ value_json.decode_failures }}", "", "", "total_increasing"},
  {"rx_failures", "LoRa RX Failures", GATEWAY_MQTT_TOPIC_HEALTH, "{{ value_json.rx_failures }}", "", "", "total_increasing"},
  {"mqtt_publish_failures", "MQTT Publish Failures", GATEWAY_MQTT_TOPIC_HEALTH, "{{ value_json.mqtt_publish_failures }}", "", "", "total_increasing"},
  {"last_packet_rssi", "Last LoRa RSSI", GATEWAY_MQTT_TOPIC_HEALTH, "{{ value_json.last_packet_rssi_dbm }}", "dBm", "signal_strength", "measurement"},
  {"last_packet_snr", "Last LoRa SNR", GATEWAY_MQTT_TOPIC_HEALTH, "{{ value_json.last_packet_snr_db }}", "dB", "", "measurement"},
  {"free_heap", "Free Heap", GATEWAY_MQTT_TOPIC_HEALTH, "{{ value_json.free_heap }}", "B", "data_size", "measurement"},
  {"temperature", "Attic Temperature", GATEWAY_MQTT_TOPIC_TEMPERATURE, "{{ value_json.temperature_c }}", "°C", "temperature", "measurement"},
  {"humidity", "Attic Humidity", GATEWAY_MQTT_TOPIC_TEMPERATURE, "{{ value_json.humidity_percent }}", "%", "humidity", "measurement"},
  {"pressure", "Attic Pressure", GATEWAY_MQTT_TOPIC_TEMPERATURE, "{{ value_json.pressure_hpa }}", "hPa", "pressure", "measurement"},
};

bool loraReady = false;
int loraInitCode = 0;
bool wifiReady = false;
bool mqttReady = false;
bool mqttTopicsReady = false;
String lastLoRaMessage;
volatile bool loraPacketReceived = false;
unsigned long lastHeartbeatMs = 0;
unsigned long lastHealthPublishMs = 0;
unsigned long lastTemperaturePublishMs = 0;
unsigned long lastDiscoveryAttemptMs = 0;
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
bool homeAssistantDiscoveryPublished = false;
bool temperatureReadOk = false;
float lastTemperatureC = 0.0;
float lastHumidityPercent = 0.0;
float lastPressureHpa = 0.0;

#if defined(ESP8266) || defined(ESP32)
ICACHE_RAM_ATTR
#endif
void onLoRaPacketReceived() {
  loraPacketReceived = true;
}

bool appendChar(char* buffer, size_t bufferSize, size_t& offset, char value) {
  if (offset + 1 >= bufferSize) {
    return false;
  }

  buffer[offset++] = value;
  buffer[offset] = '\0';
  return true;
}

bool appendFormatted(char* buffer, size_t bufferSize, size_t& offset, const char* format, ...) {
  if (offset >= bufferSize) {
    return false;
  }

  va_list args;
  va_start(args, format);
  int written = vsnprintf(buffer + offset, bufferSize - offset, format, args);
  va_end(args);

  if (written < 0 || static_cast<size_t>(written) >= bufferSize - offset) {
    buffer[bufferSize - 1] = '\0';
    return false;
  }

  offset += static_cast<size_t>(written);
  return true;
}

bool appendJsonEscapedContent(char* buffer, size_t bufferSize, size_t& offset, const String& value) {
  for (size_t i = 0; i < value.length(); ++i) {
    unsigned char c = static_cast<unsigned char>(value.charAt(i));

    switch (c) {
      case '"':
        if (!appendFormatted(buffer, bufferSize, offset, "\\\"")) return false;
        break;
      case '\\':
        if (!appendFormatted(buffer, bufferSize, offset, "\\\\")) return false;
        break;
      case '\n':
        if (!appendFormatted(buffer, bufferSize, offset, "\\n")) return false;
        break;
      case '\r':
        if (!appendFormatted(buffer, bufferSize, offset, "\\r")) return false;
        break;
      case '\t':
        if (!appendFormatted(buffer, bufferSize, offset, "\\t")) return false;
        break;
      default:
        if (c < 0x20) {
          if (!appendFormatted(buffer, bufferSize, offset, "\\u%04x", c)) return false;
        } else if (!appendChar(buffer, bufferSize, offset, static_cast<char>(c))) {
          return false;
        }
        break;
    }
  }

  return true;
}

bool buildLoRaPayloadJson(char* buffer, size_t bufferSize, const String& incoming) {
  size_t offset = 0;
  buffer[0] = '\0';

  return appendFormatted(buffer, bufferSize, offset, "{\"payload\":\"") &&
         appendJsonEscapedContent(buffer, bufferSize, offset, incoming) &&
         appendFormatted(
           buffer,
           bufferSize,
           offset,
           "\",\"rssi_dbm\":%.1f,\"snr_db\":%.1f,\"packet_count\":%lu}",
           lastPacketRSSI,
           lastPacketSNR,
           static_cast<unsigned long>(loraPacketsReceived));
}

bool buildTopic(char* buffer, size_t bufferSize, const char* suffix = nullptr) {
  int written;
  if (suffix == nullptr || suffix[0] == '\0') {
    written = snprintf(buffer, bufferSize, "%s/%s", MQTT_TOPIC_PREFIX, GATEWAY_MQTT_TOPIC_NODE);
  } else {
    written = snprintf(buffer, bufferSize, "%s/%s/%s", MQTT_TOPIC_PREFIX, GATEWAY_MQTT_TOPIC_NODE, suffix);
  }

  return written >= 0 && static_cast<size_t>(written) < bufferSize;
}

bool setupMQTTTopics() {
  bool ok = buildTopic(GATEWAY_MQTT_BASE_TOPIC, sizeof(GATEWAY_MQTT_BASE_TOPIC)) &&
            buildTopic(GATEWAY_MQTT_TOPIC_STATUS, sizeof(GATEWAY_MQTT_TOPIC_STATUS), "status") &&
            buildTopic(GATEWAY_MQTT_TOPIC_AVAILABILITY, sizeof(GATEWAY_MQTT_TOPIC_AVAILABILITY), "availability") &&
            buildTopic(GATEWAY_MQTT_TOPIC_HEALTH, sizeof(GATEWAY_MQTT_TOPIC_HEALTH), "health") &&
            buildTopic(GATEWAY_MQTT_TOPIC_RX, sizeof(GATEWAY_MQTT_TOPIC_RX), "rx") &&
            buildTopic(GATEWAY_MQTT_TOPIC_TEMPERATURE, sizeof(GATEWAY_MQTT_TOPIC_TEMPERATURE), "temperature");

  if (!ok) {
    Serial.println("MQTT topic setup failed: topic prefix too long");
  }

  return ok;
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

bool publishSensorDiscovery(
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
  return publishMQTT(topic.c_str(), payload, true);
}

bool publishBinarySensorDiscovery(
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
  return publishMQTT(topic.c_str(), payload, true);
}

bool publishHomeAssistantDiscovery() {
  bool ok = true;

  for (const BinarySensorDiscoveryDef& sensor : BINARY_SENSOR_DISCOVERY) {
    ok &= publishBinarySensorDiscovery(sensor.objectId, sensor.name, sensor.valueTemplate, sensor.deviceClass);
  }

  for (const SensorDiscoveryDef& sensor : SENSOR_DISCOVERY) {
    ok &= publishSensorDiscovery(
      sensor.objectId,
      sensor.name,
      sensor.stateTopic,
      sensor.valueTemplate,
      sensor.unit,
      sensor.deviceClass,
      sensor.stateClass);
  }

  Serial.println(ok ? "Home Assistant discovery published" : "Home Assistant discovery publish incomplete");
  return ok;
}

bool readTemperatureSensor() {
  if (!temperatureReady) {
    temperatureReadOk = false;
    return false;
  }

  lastTemperatureC = bme.readTemperature();
  lastHumidityPercent = bme.readHumidity();
  lastPressureHpa = bme.readPressure() / 100.0F;
  temperatureReadOk = !isnan(lastTemperatureC) &&
                      !isnan(lastHumidityPercent) &&
                      !isnan(lastPressureHpa);

  return temperatureReadOk;
}

void publishTemperature() {
  if (!mqttReady || !readTemperatureSensor()) {
    return;
  }

  char payload[96];
  int written = snprintf(
    payload,
    sizeof(payload),
    "{\"temperature_c\":%.2f,\"humidity_percent\":%.2f,\"pressure_hpa\":%.2f}",
    lastTemperatureC,
    lastHumidityPercent,
    lastPressureHpa);

  if (written < 0 || static_cast<size_t>(written) >= sizeof(payload)) {
    ++mqttPublishFailures;
    Serial.println("MQTT temperature skipped: payload JSON too large");
    return;
  }

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
  bool currentTemperatureOk = temperatureReady && readTemperatureSensor();

  if (currentTemperatureOk) {
    snprintf(temperatureJson, sizeof(temperatureJson), "%.2f", lastTemperatureC);
  } else {
    snprintf(temperatureJson, sizeof(temperatureJson), "null");
  }

  int written = snprintf(
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
    currentTemperatureOk ? "true" : "false",
    temperatureJson,
    static_cast<unsigned long>(ESP.getFreeHeap()));

  if (written < 0 || static_cast<size_t>(written) >= sizeof(payload)) {
    ++mqttPublishFailures;
    Serial.println("MQTT health skipped: payload JSON too large");
    return;
  }

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

  loraInitCode = radio.begin(LORA_FREQUENCY_MHZ);
  if (loraInitCode == RADIOLIB_ERR_NONE) {
    radio.setOutputPower(LORA_OUTPUT_POWER_DBM);
    radio.setSpreadingFactor(LORA_SPREADING_FACTOR);
    radio.setBandwidth(LORA_BANDWIDTH_KHZ);
    radio.setCodingRate(LORA_CODING_RATE);
    radio.setSyncWord(LORA_SYNC_WORD);
    radio.setPacketReceivedAction(onLoRaPacketReceived);

    int receiveState = radio.startReceive();
    if (receiveState == RADIOLIB_ERR_NONE) {
      loraReady = true;
      Serial.println("LoRa OK");
      Serial.println("LoRa RX cfg: 915.0 MHz, SF9, BW125, CR 4/7, SW 0x12, CRC on");
    } else {
      loraInitCode = receiveState;
      Serial.print("LoRa RX start failed: ");
      Serial.println(receiveState);
    }
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
  if (!mqttTopicsReady) {
    Serial.println("MQTT skipped: topics not configured");
    return;
  }

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
    lastDiscoveryAttemptMs = millis();
    homeAssistantDiscoveryPublished = publishHomeAssistantDiscovery();
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
    homeAssistantDiscoveryPublished = false;
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
  mqttTopicsReady = setupMQTTTopics();

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
  showStatus("Gateway Ready", loraReady ? "LoRa OK" : "LoRa FAIL", "WiFi connecting");

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

  serviceConnectivity(now);
  mqttReady = mqtt.connected();
  if (mqttReady) {
    mqtt.loop();
  }

  if (mqttReady) {
    if (!homeAssistantDiscoveryPublished &&
        (lastDiscoveryAttemptMs == 0 || now - lastDiscoveryAttemptMs >= MQTT_DISCOVERY_RETRY_MS)) {
      lastDiscoveryAttemptMs = now;
      homeAssistantDiscoveryPublished = publishHomeAssistantDiscovery();
    }

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

  if (loraReady && loraPacketReceived) {
    loraPacketReceived = false;
    String incoming;
    int state = radio.readData(incoming);

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
        char payload[LORA_MQTT_PAYLOAD_BUFFER_SIZE];
        bool payloadReady = buildLoRaPayloadJson(payload, sizeof(payload), incoming);

        if (!payloadReady) {
          ++mqttPublishFailures;
          Serial.println("MQTT publish skipped: LoRa payload JSON too large");
          showStatus("LoRa RX", incoming.substring(0, 20).c_str(), "Payload too large");
        } else if (publishMQTT(GATEWAY_MQTT_TOPIC_RX, payload)) {
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

    int receiveState = radio.startReceive();
    if (receiveState != RADIOLIB_ERR_NONE) {
      lastLoRaRxState = receiveState;
      ++loraRxFailures;
      Serial.print("LoRa RX restart failed: ");
      Serial.println(receiveState);
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
