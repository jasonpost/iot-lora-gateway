#pragma once

const char* DEVICE_ID = "iot_lora_gateway";
const char* DEVICE_NAME = "IoT LoRa Gateway";

const char* MQTT_BASE_TOPIC = "site/lora-gateway";
const char* MQTT_TOPIC_STATUS = "site/lora-gateway/status";
const char* MQTT_TOPIC_AVAILABILITY = "site/lora-gateway/availability";
const char* MQTT_TOPIC_HEALTH = "site/lora-gateway/health";
const char* MQTT_TOPIC_RX = "site/lora-gateway/rx";
const char* MQTT_TOPIC_TEMPERATURE = "site/lora-gateway/temperature";

const char* HA_DISCOVERY_PREFIX = "homeassistant";

const unsigned long MQTT_HEARTBEAT_MS = 30000;
const unsigned long WIFI_RETRY_MS = 15000;
const unsigned long MQTT_RETRY_MS = 5000;
