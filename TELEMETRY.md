# Telemetry Protocol Specification & BLE Mapping

This document provides a technical breakdown of how FrSky Smart Port (S.Port) telemetry frames are collected, packaged, and streamed over Bluetooth LE (GATT) to the companion Ground Control Station app.

---

## 📡 FrSky Smart Port (S.Port) Stream
Smart Port is a half-duplex serial protocol operating at **57600 baud (8N1)** using an inverted signal. The Master device (receiver/radio) polls individual sensor IDs on a fixed schedule roughly every 11–12 ms, generating approximately 83–90 polls per second.

### Data Frame Format
Each standard S.Port packet payload consists of exactly **8 bytes**:
1. `0x10` (Data Frame Header)
2. `Value ID` (2 bytes, Little Endian — e.g., GPS, Altitude)
3. `Data Value` (4 bytes, Little Endian — the actual telemetry value)
4. `Checksum` (1 byte)

*Byte stuffing is automatically applied to escape standard header bytes (`0x7E` and `0x7D`), reducing the raw bytes/sec capacity down to the practical limit of ~750 bytes/s achieved by this firmware.*

---

## 📱 Bluetooth LE GATT Architecture
The ESP32-S3 firmware emulates a custom BLE UART server designed to coexist smoothly alongside FrSky's internal transmitters. To enforce high-frequency data flushing, the module automatically negotiates a rapid connection parameter structure upon client connection.

### BLE Link Parameters
- **Connection Interval:** 7.5 ms (Min) to 15.0 ms (Max)
- **Slave Latency:** 0
- **Supervision Timeout:** 1000 ms

### GATT UUID Map
The bridge utilizes a highly stable 128-bit Nordic UART-like topology to dispatch raw telemetric streams:

| Entity | UUID | Properties | Description |
| :--- | :--- | :--- | :--- |
| **Service** | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` | — | Main Telemetry Service |
| **TX Characteristic** | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` | `NOTIFY` | Outbound stream (S.Port -> Android) |

---

## 📊 INAV 8.0.1 / EdgeTX Sensor ID Mapping
The following standard FrSky sensor IDs are extracted from the telemetry line by the companion Android app (*TelemetryView*) and parsed into real-time variables:

| Sensor Type | S.Port ID (Hex) | Data Size | Parsing / Scaling Logic |
| :--- | :--- | :--- | :--- |
| **Baro Altitude** | `0x0100` / `0x010F` | 4 Bytes | Signed integer in centimeters (divided by 100 for meters) |
| **Variometer** | `0x0110` / `0x011F` | 4 Bytes | Vertical speed in cm/s (divided by 100 for m/s) |
| **GPS Coordinates** | `0x0800` | 4 Bytes | Encoded Latitude/Longitude dms formats |
| **GPS Speed** | `0x0830` | 4 Bytes | Speed in knots/kmh depending on scale configuration |
| **GPS Heading** | `0x0840` | 4 Bytes | Heading angle in degrees (scaled by 100) |
| **Pitch / Roll** | `0x0440` | 4 Bytes | Accelerometer attitude angles (scaled by 10) |
| **Battery Voltage** | `0x0210` | 4 Bytes | Main flight pack voltage tracking |
| **RSSI** | `0xF101` | 4 Bytes | Rx link quality factor monitoring |

---

## ⚡ 1.5ms Micro-Burst Buffer Optimization
Standard BLE notifications are limited to an MTU payload of 20 bytes for base compatibility. 
The firmware loop runs a micro-second precision watchdog:

1. When a byte enters the hardware UART buffer, a timer (`micros()`) captures the timestamp.
2. The loop waits up to **1500 microseconds (1.5 ms)** for additional incoming bytes to arrive.
3. Once 20 bytes are queued or the 1.5 ms window closes, the chunk is compiled into a single `.notify()` call.

This keeps wireless network transaction overhead minimal while hitting a perfect throughput averaging **750 bytes/s**, creating zero lag on the *ArtificialHorizonView* HUD layout within the Android application.
