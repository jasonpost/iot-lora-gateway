#pragma once

const int PIN_LED = 35;
const int PIN_VEXT = 36;
const int PIN_OLED_RST = 21;
const int PIN_OLED_SDA = 17;
const int PIN_OLED_SCL = 18;
const uint8_t OLED_ADDRESS = 0x3C;

const int PIN_LORA_NSS = 8;
const int PIN_LORA_SCK = 9;
const int PIN_LORA_MOSI = 10;
const int PIN_LORA_MISO = 11;
const int PIN_LORA_RST = 12;
const int PIN_LORA_BUSY = 13;
const int PIN_LORA_DIO1 = 14;

const float LORA_FREQUENCY_MHZ = 915.0;
const int LORA_OUTPUT_POWER_DBM = 14;
const int LORA_SPREADING_FACTOR = 9;
const float LORA_BANDWIDTH_KHZ = 125.0;
const int LORA_CODING_RATE = 7;
const uint8_t LORA_SYNC_WORD = 0x12;

const unsigned long WIFI_RETRY_MS = 15000;


const uint16_t MQTT_BUFFER_SIZE = 1536;
const uint16_t MQTT_SOCKET_TIMEOUT_S = 2;
const unsigned long MQTT_HEARTBEAT_MS = 30000;
const unsigned long MQTT_HEALTH_PUBLISH_MS = 30000;
const unsigned long MQTT_TEMPERATURE_PUBLISH_MS = 60000;
const unsigned long MQTT_DISCOVERY_RETRY_MS = 30000;
const unsigned long MQTT_RETRY_MS = 5000;
const unsigned long LORA_RECOVERY_RETRY_MS = 10000;

const char* DEVICE_ID = "iot_lora_gateway";
const char* DEVICE_NAME = "IoT LoRa Gateway";

const char* GATEWAY_MQTT_TOPIC_NODE = "lora-gateway";
const size_t MQTT_TOPIC_BUFFER_SIZE = 96;

const char* GATEWAY_AVAILABILITY_ONLINE = "online";
const char* GATEWAY_AVAILABILITY_OFFLINE = "offline";

const char* HA_DISCOVERY_PREFIX = "homeassistant";

const bool TEMPERATURE_SENSOR_ENABLED = true;
const uint8_t BME280_ADDRESS_PRIMARY = 0x76;
const uint8_t BME280_ADDRESS_SECONDARY = 0x77;
