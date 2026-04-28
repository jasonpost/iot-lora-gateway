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
- Optional I2C BME280 sensor for attic temperature, humidity, and pressure
- USB connection for flashing and serial monitoring
- A reachable Wi-Fi network
- An MQTT broker reachable from that network
- Shelly 2PM, or equivalent listed smart relay/power meter, for attic outlet control and power monitoring

## Power Control

The gateway firmware does not switch mains power directly. Use a Shelly 2PM, or an
equivalent listed smart relay/power meter, for router and attic equipment power
control.

Recommended Shelly responsibilities:

- Control the router outlet.
- Optionally control a second attic equipment outlet.
- Report power state and power usage directly to Home Assistant.
- Restore controlled outputs to ON after power loss.

Do not power the LoRa gateway from an outlet that only the gateway can turn back
on. Keep an independent recovery path so the gateway cannot strand itself
powered off.

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

- `littlelodge/lora-gateway/status`
- `littlelodge/lora-gateway/availability`
- `littlelodge/lora-gateway/health`
- `littlelodge/lora-gateway/rx`
- `littlelodge/lora-gateway/temperature`

Home Assistant discovery uses the default prefix `homeassistant`.

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
- Publishes retained availability of `online` after MQTT connects
- Registers an MQTT Last Will of retained `offline`
- Publishes retained Home Assistant MQTT discovery configs after MQTT connects
- Publishes `gateway alive` every 30 seconds while connected
- Publishes retained gateway health JSON every 30 seconds
- Publishes retained BME280 temperature JSON every 60 seconds when the sensor is present
- Publishes each received LoRa payload with RSSI, SNR, and packet count metadata

Example LoRa receive payload:

```json
{"payload":"sensor payload","rssi_dbm":-78.5,"snr_db":8.2,"packet_count":42}
```

Example gateway health fields:

- `uptime_s`
- `wifi_connected`
- `wifi_rssi_dbm`
- `ip`
- `mqtt_connected`
- `lora_ready`
- `last_lora_state`
- `packets_received`
- `decode_failures`
- `rx_failures`
- `mqtt_publish_successes`
- `mqtt_publish_failures`
- `last_packet_rssi_dbm`
- `last_packet_snr_db`
- `temperature_available`
- `temperature_c`
- `free_heap`

Temperature behavior:

- Uses an optional I2C BME280 sensor at `0x76` or `0x77`
- Publishes to `littlelodge/lora-gateway/temperature`
- Continues running if the BME280 is not found

Reconnect timing:

- Wi-Fi retry: every 15 seconds
- MQTT retry: every 5 seconds

## Dependencies

Declared in `platformio.ini`:

- `jgromes/RadioLib`
- `knolleary/PubSubClient`
- `adafruit/Adafruit SSD1306`
- `adafruit/Adafruit GFX Library`
- `adafruit/Adafruit BME280 Library`

## Notes

- The firmware currently acts as a receive-only LoRa-to-MQTT bridge.
- OLED status updates are intended for quick bring-up and troubleshooting.
- If credentials were ever committed previously, rotate them and clean git history separately.
