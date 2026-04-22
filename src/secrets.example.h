#pragma once

#include <Arduino.h>
#include <WiFi.h>

const char* WIFI_SSID = "your-wifi-ssid";
const char* WIFI_PASS = "your-wifi-password";
IPAddress MQTT_HOST(192, 168, 1, 10);
const uint16_t MQTT_PORT = 1883;
const char* MQTT_CLIENT_ID = "iot-lora-gateway";
const char* MQTT_USER = "your-mqtt-username";
const char* MQTT_PASS = "your-mqtt-password";
const char* MQTT_TOPIC_STATUS = "your/site/lora/status";
const char* MQTT_TOPIC_RX = "your/site/lora/data";
