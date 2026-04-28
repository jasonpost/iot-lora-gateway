# IoT LoRa Gateway

Firmware for a Heltec WiFi LoRa 32 V4 that receives LoRa packets and republishes them to MQTT over Wi-Fi.

## What It Does

- Boots the onboard OLED and status LED.
- Configures the SX1262 LoRa radio for `915.0 MHz`.
- Connects to Wi-Fi using values from `src/secrets.h`.
- Connects to an MQTT broker.
- Publishes a startup status message.
- Publishes periodic heartbeat messages.
- Publishes received LoRa payloads.
- Shows current status and recent LoRa activity on the OLED.

## Hardware

- Heltec WiFi LoRa 32 V4
- USB connection for flashing and serial monitoring
- A reachable Wi-Fi network
- An MQTT broker reachable from that network

## Project Layout

- [platformio.ini](platformio.ini)
- [src/main.cpp](src/main.cpp)
- [src/secrets.example.h](src/secrets.example.h)

## Secrets Setup

This project keeps local credentials out of git.

1. Copy `src/secrets.example.h` to `src/secrets.h`.
2. Fill in your Wi-Fi and MQTT settings.
3. Keep `src/secrets.h` local only.

`src/secrets.h` is ignored by git and should not be committed.

Expected values in `src/secrets.h`:

```cpp
const char* WIFI_SSID = "...";
const char* WIFI_PASS = "...";
IPAddress MQTT_HOST(192, 168, 1, 10);
const uint16_t MQTT_PORT = 1883;
const char* MQTT_CLIENT_ID = "iot-lora-gateway";
const char* MQTT_USER = "...";
const char* MQTT_PASS = "...";
```

Non-secret gateway settings, including MQTT topics and publish intervals, live in
`src/config.h`.

Default MQTT topics:

- `site/lora-gateway/status`
- `site/lora-gateway/availability`
- `site/lora-gateway/health`
- `site/lora-gateway/rx`
- `site/lora-gateway/temperature`

## Build

This project uses PlatformIO with the `heltec_wifi_lora_32_v4` environment.

```powershell
pio run -e heltec_wifi_lora_32_v4
```

## Upload

```powershell
pio run -e heltec_wifi_lora_32_v4 --target upload
```

## Serial Monitor

```powershell
pio device monitor -b 115200
```

## Current Runtime Behavior

LoRa settings in the current firmware:

- Frequency: `915.0 MHz`
- Spreading factor: `9`
- Bandwidth: `125.0 kHz`
- Coding rate: `4/7`
- Sync word: `0x12`

MQTT behavior in the current firmware:

- Publishes `gateway bring-up online` after a successful MQTT connection
- Publishes `gateway alive` every 30 seconds while connected
- Publishes each received LoRa payload to `MQTT_TOPIC_RX`

Reconnect timing:

- Wi-Fi retry: every 15 seconds
- MQTT retry: every 5 seconds

## Dependencies

Declared in `platformio.ini`:

- `jgromes/RadioLib`
- `knolleary/PubSubClient`
- `adafruit/Adafruit SSD1306`
- `adafruit/Adafruit GFX Library`

## Notes

- The firmware currently acts as a receive-only LoRa-to-MQTT bridge.
- OLED status updates are intended for quick bring-up and troubleshooting.
- If credentials were ever committed previously, rotate them and clean git history separately.
