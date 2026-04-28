#pragma once

const unsigned long WIFI_RETRY_MS = 15000;


const uint16_t MQTT_BUFFER_SIZE = 1536;
const unsigned long MQTT_HEARTBEAT_MS = 30000;
const unsigned long MQTT_HEALTH_PUBLISH_MS = 30000;
const unsigned long MQTT_TEMPERATURE_PUBLISH_MS = 60000;
const unsigned long MQTT_RETRY_MS = 5000;

const char* DEVICE_ID = "iot_lora_gateway";
const char* DEVICE_NAME = "IoT LoRa Gateway";

const char* GATEWAY_MQTT_BASE_TOPIC = "littlelodge/lora-gateway";
const char* GATEWAY_MQTT_TOPIC_STATUS = "littlelodge/lora-gateway/status";
const char* GATEWAY_MQTT_TOPIC_AVAILABILITY = "littlelodge/lora-gateway/availability";
const char* GATEWAY_MQTT_TOPIC_HEALTH = "littlelodge/lora-gateway/health";
const char* GATEWAY_MQTT_TOPIC_RX = "littlelodge/lora-gateway/rx";
const char* GATEWAY_MQTT_TOPIC_TEMPERATURE = "littlelodge/lora-gateway/temperature";

const char* GATEWAY_AVAILABILITY_ONLINE = "online";
const char* GATEWAY_AVAILABILITY_OFFLINE = "offline";

const char* HA_DISCOVERY_PREFIX = "homeassistant";

const bool TEMPERATURE_SENSOR_ENABLED = true;
const uint8_t BME280_ADDRESS_PRIMARY = 0x76;
const uint8_t BME280_ADDRESS_SECONDARY = 0x77;
