# LoRa Implementation Design

This document defines the intended LoRa-to-MQTT architecture for the property
sensor network. It captures the target design, not just the gateway behavior
that exists today.

## Goals

- Allow many low-power LoRa transmitters to report through one gateway.
- Preserve a raw receive stream for debugging.
- Publish clean per-transmitter MQTT state topics for Home Assistant.
- Let Home Assistant see each transmitter as its own MQTT device when discovery
  support is added.
- Support future MQTT-to-LoRa commands without requiring battery devices to stay
  awake continuously.

## Current Gateway Behavior

The current firmware is receive-only. It receives a LoRa packet as text and
publishes a JSON MQTT envelope to:

```text
<MQTT_TOPIC_PREFIX>/lora-gateway/rx
```

Current MQTT payload shape:

```json
{
  "payload": "sensor payload",
  "rssi_dbm": -78.5,
  "snr_db": 8.2,
  "packet_count": 42
}
```

The current gateway does not parse the transmitter payload as JSON. If a
transmitter sends JSON text today, that JSON is preserved as an escaped string
inside the `payload` field.

## Target Transmitter Payload Standard

Transmitters should send compact JSON over LoRa. Every uplink message must
include a stable `node_id`.

Example:

```json
{
  "node_id": "barn-01",
  "type": "state",
  "seq": 44,
  "rx_window_ms": 1000,
  "battery_v": 3.88,
  "temperature_c": 21.7,
  "humidity_pct": 48.2
}
```

Required fields:

- `node_id`: Stable transmitter identifier. Use lowercase letters, numbers, and
  hyphens, such as `barn-01` or `well-house`.
- `type`: Message type. Use `state` for normal sensor readings.

Recommended fields:

- `seq`: Per-node sequence number, useful for detecting missed messages.
- `rx_window_ms`: How long the node will listen after sending this packet.
- `battery_v`: Battery voltage when available.

Sensor fields are flexible. A node may include one reading or many readings in
the same packet. For example, one transmitter can report temperature, humidity,
battery voltage, and door state together.

## Target MQTT Topic Layout

The gateway should keep the raw receive topic:

```text
<MQTT_TOPIC_PREFIX>/lora-gateway/rx
```

The gateway should also parse `node_id` and publish each node to its own state
topic:

```text
<MQTT_TOPIC_PREFIX>/lora/<node_id>/state
```

Examples:

```text
littlelodge/lora/barn-01/state
littlelodge/lora/well-house/state
littlelodge/lora/soil-03/state
```

The per-node MQTT state payload should keep the original transmitter fields and
add gateway radio metadata:

```json
{
  "node_id": "barn-01",
  "type": "state",
  "seq": 44,
  "rx_window_ms": 1000,
  "battery_v": 3.88,
  "temperature_c": 21.7,
  "humidity_pct": 48.2,
  "rssi_dbm": -78.5,
  "snr_db": 8.2,
  "gateway_packet_count": 42
}
```

Keep `node_id` in the payload even though it is also present in the topic. This
makes logs, retained messages, automations, and exports self-describing.

## Home Assistant Discovery

Home Assistant can trigger automations from a shared raw topic by checking
`node_id`, but per-node topics are cleaner for real HA entities and devices.

For discovery, each LoRa transmitter should become its own HA device. Discovery
entities for `barn-01` should point at:

```text
<MQTT_TOPIC_PREFIX>/lora/barn-01/state
```

Example discovery payload for a temperature entity:

```json
{
  "name": "Temperature",
  "unique_id": "lora_barn_01_temperature",
  "state_topic": "littlelodge/lora/barn-01/state",
  "value_template": "{{ value_json.temperature_c }}",
  "unit_of_measurement": "C",
  "device_class": "temperature",
  "state_class": "measurement",
  "device": {
    "identifiers": ["lora_barn_01"],
    "name": "Barn 01",
    "manufacturer": "Little Lodge LoRa"
  }
}
```

Discovery can be implemented in phases:

1. Publish per-node state topics with no HA discovery.
2. Add discovery for known/common fields such as `battery_v`, `temperature_c`,
   `humidity_pct`, `door_open`, and `water_detected`.
3. Add optional node metadata if transmitters include fields such as
   `device_name` or `model`.

## Future Downlink Commands

Battery-powered LoRa nodes should not stay awake waiting for commands. They
should periodically wake, send state, listen briefly, then sleep.

Expected node cycle:

```text
wake
read sensors
send state packet
listen for rx_window_ms
apply command if one is received
sleep
```

MQTT command topic:

```text
<MQTT_TOPIC_PREFIX>/lora/<node_id>/command
```

Example MQTT command:

```json
{
  "command": "set_interval",
  "interval_s": 300,
  "request_id": "abc123",
  "expires_s": 3600
}
```

The gateway stores the command in a small pending queue. When that node next
checks in and advertises a receive window, the gateway transmits the command
over LoRa:

```json
{
  "to": "barn-01",
  "from": "gateway",
  "type": "command",
  "command": "set_interval",
  "interval_s": 300,
  "request_id": "abc123"
}
```

The node should acknowledge the command immediately if power budget allows, or
on its next uplink:

```json
{
  "node_id": "barn-01",
  "type": "ack",
  "request_id": "abc123",
  "ok": true
}
```

Potential MQTT ack topic:

```text
<MQTT_TOPIC_PREFIX>/lora/<node_id>/ack
```

## Command Queue Guidance

The Heltec ESP32-S3 gateway can hold a modest command queue. Commands are small,
so the first implementation should use RAM and intentionally small limits.

Suggested first limits:

- Maximum command JSON size: 256 bytes.
- Maximum pending commands total: 20.
- Maximum pending commands per node: 1 to 3.
- Default expiration: 1 hour.
- Drop or replace expired commands.

Start with a RAM queue. A later version can use flash/NVS only if commands must
survive gateway reboots. The gateway should not try to become a general-purpose
durable message broker.

## Gateway Responsibilities

Receive path:

1. Receive LoRa packet.
2. Publish raw envelope to `<MQTT_TOPIC_PREFIX>/lora-gateway/rx`.
3. Parse transmitter JSON.
4. Validate and sanitize `node_id`.
5. Publish enriched state to `<MQTT_TOPIC_PREFIX>/lora/<node_id>/state`.
6. Optionally publish HA discovery configs for known fields.

Future command path:

1. Subscribe to `<MQTT_TOPIC_PREFIX>/lora/+/command`.
2. Validate command topic and payload.
3. Store pending command for the target node.
4. Deliver pending command during that node's next receive window.
5. Publish ack/status when the node acknowledges or when the command expires.

## Node ID Rules

Use a conservative node ID format so it is safe in MQTT topics and HA entity
names:

- Lowercase letters `a-z`.
- Numbers `0-9`.
- Hyphen `-`.
- Length between 1 and 32 characters.

Examples:

- `barn-01`
- `well-house`
- `soil-03`
- `driveway-gate`

Avoid spaces, slashes, underscores, uppercase letters, and punctuation.

## Implementation Phases

1. Document and standardize transmitter JSON with required `node_id`.
2. Add gateway JSON parsing and per-node MQTT state publishing.
3. Keep the raw gateway receive topic for debugging.
4. Add Home Assistant discovery for common fields.
5. Add MQTT command subscription and an in-memory pending command queue.
6. Add LoRa downlink delivery during node receive windows.
7. Add command acknowledgements and expiration reporting.
