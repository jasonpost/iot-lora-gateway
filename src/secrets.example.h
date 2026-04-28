#pragma once

#include <Arduino.h>
#include <WiFi.h>

const char* WIFI_SSID = "your-wifi-ssid";
const char* WIFI_PASS = "your-wifi-password";
IPAddress MQTT_HOST(192, 168, 1, 10);
// Plain MQTT is intended only for a trusted local network. Use TLS-capable
// firmware and broker configuration if this broker is exposed beyond the LAN.
const uint16_t MQTT_PORT = 1883;
const char* MQTT_CLIENT_ID = "iot-lora-gateway";
const char* MQTT_USER = "your-mqtt-username";
const char* MQTT_PASS = "your-mqtt-password";
const char* MQTT_TOPIC_PREFIX = "your-private-topic-prefix";
