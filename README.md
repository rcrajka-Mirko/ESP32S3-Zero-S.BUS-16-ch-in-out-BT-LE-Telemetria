# Ultra-Fast 16-Channel Wireless Trainer Link & Telemetry Bridge (Gen 2)

An advanced, high-performance, and zero-configuration wireless **Teacher-Student (Master-Slave) Trainer Link** for FrSky radios, running on two **Waveshare ESP32-S3 Zero** modules. This project acts as a bi-directional RF relay bypassing the severe 8-channel limitation of the stock FrSky PARA Bluetooth protocol by delivering full **16 SBUS channels** with native hardware-level speeds, coupled with an ultra-optimized **FrSky Smart Port (S.Port) to BLE telemetry bridge**.

---

## 🚀 Why Gen 2 Beats Everything Else (Including Stock FrSky PARA)

* **Full 16 Channels vs. FrSky 8-Ch Limit:** The built-in FrSky PARA Bluetooth LE trainer protocol severely restricts students to only 8 channels. This project delivers **all 16 independent SBUS channels** bi-directionally.
* **Faster Than Bluetooth PARA:** Utilizing Espressif's low-level **ESP-NOW** protocol (bypassing the heavy TCP/IP network stack), the control packets are shot over the 2.4 GHz spectrum with a native latency of just a few milliseconds. The student experiences a true "wired-like" instant response.
* **100% S.Port Line Saturation (Max Throughput):** Thanks to our newly engineered **1.5 ms micro-burst buffering**, S.Port data streams continuously at **~750 to 800 bytes per second** (approx. 84 BLE packets/s). This represents **100% of the absolute physical limits** of the FrSky Smart Port polling mechanism without a single dropped frame.
* **Zero-Configuration Plug-and-Play (Broadcast):** Gen 2 migrates from rigid MAC-address peering to a network-wide Broadcast (`FF:FF:FF:FF:FF:FF`). Both ESP32-S3 modules run **100% identical firmware**. You no longer need to read, hardcode, or track individual hardware MAC addresses.
* **Pro Hardware Thermal & Power Resilience:** Designed for grueling summer field use. The ESP32-S3 chips are shielded against thermal throttling via a custom aluminum heatsink layout, powered by a dedicated 5V regulated topology to handle heavy RF current spikes.

---

## 🛠️ Hardware Architecture & Assembly

The module is engineered to operate in the harsh RF environment of a transmitter's JR bay or internal tray. Because the ESP32-S3 draws massive current spikes (up to 300mA) during parallel ESP-NOW transmission and Bluetooth LE broadcasting, stock internal radio regulators could sag. 

### ⚡ Power Supply & Cooling Scheme
* **Regulated 5V Source:** An external dedicated 5V voltage regulator (7805 or high-efficiency step-down switching regulator) is mounted alongside the ESP32-S3 to drop the transmitter's raw battery voltage (7.2V - 8.4V) down to a stable 5.0V.
* **Filtering Stack:** Electrolytic smoothing capacitors are placed right at the input and output lines of the regulator to damp voltage ripples and suppress RF noise.
* **Thermal Management:** The ultra-compact Waveshare ESP32-S3 Zero is mounted onto a **small aluminum heatsink** to effectively dissipate heat during long, uninterrupted training sessions or hot summer days.

### 🔌 Pinout & Wiring (Identical for Both Modules)

| ESP32-S3 Zero Pin | Signal Direction | Connected To | Notes |
| :--- | :--- | :--- | :--- |
| **GPIO 4 (RX1)** | Input (From Radio) | Trainer Port SBUS OUT | Software inverted via `uart_set_line_inverse()` |
| **GPIO 5 (TX2)** | Output (To Radio) | Trainer Port SBUS IN | Software inverted via `uart_set_line_inverse()` |
| **GPIO 6 (TX0)** | Bi-directional | Smart Port (S.Port) | Half-duplex telemetry line (57600 baud) |
| **GPIO 21** | Output | Built-in WS2812 RGB LED | Status & Diagnostis display |
| **5V / GND** | Input | Dedicated 5V Regulator Block | Clean, decoupled power grid |

*No external hardware transistor inverters are needed! Signal inversion is handled directly inside the ESP32-S3 silicon.*

---

## 🚦 Smart Status LED Management (RGB NeoPixel)

The integrated WS2812 LED refreshes every 300 ms to provide crystal-clear physical diagnostic states at a glance:
* 🟨 **Yellow:** Core system startup, UART mapping, and BLE advertising initialization.
* 🟥 **Red:** **FAILSAFE ACTIVE** or complete signal loss. The wireless connection to the other radio has been dropped for more than 500 ms.
* 🟩 **Green:** ESP-NOW wireless link is securely established and operational, but no Bluetooth app is connected yet.
* 🟦 **Blue:** Elite State. Wireless link is fully active **AND** a mobile device/PC is connected via Bluetooth LE, receiving fast sensor telemetry.

---

## 🛡️ Advanced Safety FailSafe Guard

When dealing with a wireless link between radios, safety is paramount. Gen 2 features a robust hardware watchdog routine. 
If the wireless link between the Master and Student drops for **more than 300 ms** (due to extreme range or power loss on the student's end), the ESP32-S3 immediately flags a native **SBUS FailSafe condition** (`dataTX.failsafe = true;`) and forces the frame write into the Master radio. The Master radio instantly intercepts the drop, ignores the dead student stick inputs, and passes full aircraft recovery control back to the Instructor or triggers an auto-RTL.

---

## 📈 Real-Time Performance & Benchmarking

The firmware includes an ultra-lightweight, non-blocking telemetry counter routed to the native USB CDC port (**115200 baud**). Every single second, the module logs its throughput to ensure the link is running at peak capacity:

```text
=== S3 ZERO - HARNESS READY ===
[TELEMETRIA] Rychlost: 752 bajtov/s
[TELEMETRIA] Rychlost: 748 bajtov/s
[TELEMETRIA] Rychlost: 761 bajtov/s
```
*A steady readout between 740 and 780 bytes/s confirms that the Bluetooth buffer is flushing at 100% of the maximum theoretical polling speed of the FrSky Smart Port standard.*

---

## ⚙️ Protocol Blueprint

### SBUS Configuration
* **Baud Rate:** 100,000 baud
* **Frame Format:** 8E2 (8 Data bits, Even parity, 2 Stop bits)
* **Structure:** Inverted, fixed 25-byte packets. Tracks 16 channels formatted as 11-bit values packed into a optimized `sbus_packet_t` struct to save radio bandwidth.

### BLE GATT Specification (Reverse-Engineered PARA Protocol)
* **Device Name:** `X9D_TRAINER`
* **Connection Interval:** Automatically renegotiated upon connection to a blistering **7.5 ms – 15 ms range** (forcing the Android/iOS client to dump lag buffers).
* **Service UUID:** `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
* **TX Characteristic (Notify):** `6E400003-B5A3-F393-E0A9-E50E24DCCA9E`

---

## 📅 Chronology, Engineering Log & Milestones

The expansion of this project showcases the journey from a bare telemetric parser to a bulletproof, dual-link tactical trainer.

| Period / Phase | Milestone | Engineering & Architecture Notes |
| :--- | :--- | :--- |
| **September 2024** | Project Genesis | Initial concept architecture. Commencement of the custom Android Ground Control Station (GCS) app: *TelemetryView*. |
| **Oct–Nov 2024** | PARA Protocol Decoding | Exhaustive reverse engineering of the FrSky PARA BLE protocol using raw packet injection and sniffing via *nRF Connect*. |
| **Nov–Dec 2024** | First-Gen BLE Bridge | Writing the initial firmware using the `NimBLE-Arduino` stack. Successfully verified basic Bluetooth telemetry transmission. |
| **Jan–Feb 2025** | Telemetry Scaling | Integrated byte-stuffing and CRC checking routines for full FrSky S.Port decoding. Custom mapping of INAV 8.0.1 sensor IDs. |
| **Mar–Apr 2025** | Physical SBUS Relaying | Designing the Unicast peer-to-peer ESP-NOW matrix. Assembled the first hardware prototypes utilizing 7805 linear regulators and smoothing caps. Verified 16-channel passthrough. |
| **Mid 2025 – Early 2026**| Long-Term Field Testing | Validation flights. Discovery of data lag bottlenecks when sensors generated heavy burst traffic over standard BLE notification limits. |
| **June 2026 (Gen 2 Update)** | **The Performance Breakout** | **The Final Overhaul:** <br>• Ditched rigid MAC-peering for a **Zero-Config Broadcast** architecture, creating unified firmware for both modules.<br>• Replaced blocking `readBytes()` calls with a blazing fast **1.5 ms micro-burst buffer** loop.<br>• Achieved maximum theoretical S.Port saturation at **760+ bytes/s** over BLE.<br>• Forced **7.5ms BLE Connection Parameters** to eliminate telemetry jitter.<br>• Appended a **300ms Safety FailSafe watchdog** loop.<br>• Stabilized thermals via dedicated **aluminum cooling blocks** and a clean 5V isolated supply. |

---

## 👥 Acknowledgements

It wouldn't have been possible without beating my father. Thank you father.

**- Mirko**

---

## 📜 License
This project is open-source. Feel free to modify, build, and fly! Always test your FailSafe routines on the ground before taking off.
