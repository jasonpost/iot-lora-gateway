# Main Feature/Objective
The objective of this solution is to create a LoRa Gateway. 
It will receive LoRa messages from various sensors on the property.
It will take these messages and send them to Home Assistant via MQTT.
This LoRa Gateway will reside in the attic of a house.
A router will also reside with the gateway.

# Secondary Features
The controller will also have the extra following features:
1. Monitor the attic temperature.
2. Publish gateway health/status information to Home Assistant.
3. Support safe power control and power monitoring for attic equipment.

# Power Control Approach
Mains power switching should be handled by a Shelly 2PM Power Metering Switch, or an equivalent listed smart relay/power meter, instead of directly switching 120V mains from the LoRa gateway controller.

The Shelly 2PM will be responsible for:
1. Switching power to the router outlet.
2. Switching power to a second controlled outlet, if needed.
3. Reporting power usage for each controlled channel.
4. Integrating with Home Assistant for manual control, automation, and monitoring.

The LoRa gateway firmware should remain focused on:
1. Receiving LoRa sensor messages.
2. Publishing LoRa messages to MQTT.
3. Publishing gateway status/health data.
4. Monitoring attic temperature.

The Shelly outputs should be configured to turn ON after power is restored. This keeps the router and gateway-supporting equipment in a default powered-on state after outages.

Avoid placing the LoRa gateway's own power behind a switch that only the gateway can control, unless there is an independent recovery path. This prevents the gateway from turning itself off and being unable to turn itself back on.

# Home Assistant Integration
Home Assistant should receive:
1. LoRa sensor messages through MQTT.
2. Gateway online/offline status.
3. Gateway health metrics such as uptime, Wi-Fi signal, MQTT status, packet counts, and temperature.
4. Shelly power state and power usage.

MQTT discovery should be considered so gateway sensors and status values appear automatically in Home Assistant.

# Safety Notes
Any mains-voltage hardware should be installed in a proper enclosure with appropriate strain relief, conductor sizing, heat management, and electrical protection.

For mains switching, prefer listed/certified devices such as the Shelly 2PM over bare relay modules whenever possible.
