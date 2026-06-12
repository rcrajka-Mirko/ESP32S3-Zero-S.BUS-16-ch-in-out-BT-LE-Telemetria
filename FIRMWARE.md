# Firmware Documentation & Source Code (Gen 2)

This document contains the complete, production-ready source code and compilation instructions for the universal ESP32-S3 Zero wireless trainer bridge.

## 📦 Dependency & Libraries
To compile this project successfully, ensure you have the following libraries installed in your environment (PlatformIO or Arduino IDE Library Manager):
- **sbus** by *bolderflight* (For robust SBUS parsing and frame writing)
- **Adafruit NeoPixel** by *Adafruit* (For status RGB LED driving)
- **WiFi** and **esp_now** (Built-in Espressif core libraries)
- **BLEDevice**, **BLEServer**, **BLE2902** (Built-in C++ BLE stack)

---

## 💻 Full Source Code (`main.cpp` / `sketch.ino`)

The firmware features an autonomous **Zero-Configuration Broadcast** design. Flash this exact code onto **both** ESP32-S3 Zero modules without changing any parameters.

```cpp
/**
 * @file ESP32S3_X9D_Wireless_Trainer_Broadcast_FailSafe.ino
 * @brief Universal High-Speed Wireless Trainer Bridge for FrSky Taranis X9D
 * @version 2.2.0
 * @date 2026-06-12
 */

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <sbus.h>
#include <Adafruit_NeoPixel.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include "driver/uart.h"

// Hardware Pin Configuration for ESP32-S3 Zero
#define SBUS_RX_PIN 4
#define SBUS_TX_PIN 5
#define SPORT_PIN 6     
#define RGB_PIN 21      

Adafruit_NeoPixel pixel(1, RGB_PIN, NEO_GRB + NEO_KHZ800);
BLECharacteristic *pTxCharacteristic;
bool deviceConnected = false;

// Network Broadcast Address - Allows immediate hardware swaps without tracking MACs
static const uint8_t BROADCAST_MAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; 

bfs::SbusRx sbus_rx(&Serial1, SBUS_RX_PIN, -1, false);
bfs::SbusTx sbus_tx(&Serial2, -1, SBUS_TX_PIN, false);
HardwareSerial SportSerial(0); 

struct __attribute__((packed)) sbus_packet_t {
    uint16_t kanaly[16]; 
};
sbus_packet_t outPacket;
bfs::SbusData dataTX;

uint32_t count_ble_bytes = 0;   
uint32_t last_recv_ms = 0;
uint32_t last_debug_ms = 0;
bool is_failsafe_active = false;

class MyCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) { 
        deviceConnected = true; 
        pServer->updateConnParams(pServer->getConnId(), 0x0006, 0x000C, 0, 100); // Enforce 7.5ms - 15ms link window
    }
    void onDisconnect(BLEServer* pServer) { 
        deviceConnected = false; 
        BLEDevice::startAdvertising(); 
    }
};

void on_data_recv(const esp_now_recv_info_t *info, const uint8_t *incoming_data, int len) {
    if (len != sizeof(sbus_packet_t)) return;
    last_recv_ms = millis();
    is_failsafe_active = false; 
    
    const sbus_packet_t *in = (sbus_packet_t *)incoming_data;
    for (int i = 0; i < 16; i++) dataTX.ch[i] = in->kanaly[i];
    
    dataTX.failsafe = false;
    dataTX.ch17 = false;
    dataTX.ch18 = false;
    
    sbus_tx.data(dataTX);
    sbus_tx.Write(); 
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n=== UNIVERSAL MODULE - PRODUCTION READY ===");

    pixel.begin();
    pixel.setPixelColor(0, pixel.Color(20, 20, 0)); pixel.show(); 

    // Initialize BLE Stack
    BLEDevice::init("X9D_TRAINER");
    BLEServer *pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyCallbacks());
    BLEService *pService = pServer->createService("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
    pTxCharacteristic = pService->createCharacteristic("6E400003-B5A3-F393-E0A9-E50E24DCCA9E", BLECharacteristic::PROPERTY_NOTIFY);
    pTxCharacteristic->addDescriptor(new BLE2902());
    pService->start();
    BLEDevice::startAdvertising();

    delay(500);

    // Initialize Hardware UARTS
    SportSerial.begin(57600, SERIAL_8N1, SPORT_PIN, -1);
    Serial1.begin(100000, SERIAL_8E2, SBUS_RX_PIN, -1);
    Serial2.begin(100000, SERIAL_8E2, -1, SBUS_TX_PIN);
    
    delay(200);
    // Invert signals directly in ESP32-S3 silicon registers
    uart_set_line_inverse(UART_NUM_0, UART_SIGNAL_RXD_INV); 
    uart_set_line_inverse(UART_NUM_1, UART_SIGNAL_RXD_INV); 
    uart_set_line_inverse(UART_NUM_2, UART_SIGNAL_TXD_INV); 

    // Initialize ESP-NOW Broadcast RF Link
    WiFi.mode(WIFI_STA);
    if (esp_now_init() == ESP_OK) {
        esp_now_register_recv_cb(on_data_recv);
        
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, BROADCAST_MAC, 6);
        peer.channel = 1;
        peer.encrypt = false;
        esp_now_add_peer(&peer);
    }

    sbus_rx.Begin();
    sbus_tx.Begin();
}

void loop() {
    // 1. Core SBUS Passthrough Loop
    if (sbus_rx.Read()) {
        bfs::SbusData localData = sbus_rx.data();
        for (int i = 0; i < 16; i++) outPacket.kanaly[i] = localData.ch[i];
        esp_now_send(BROADCAST_MAC, (uint8_t *)&outPacket, sizeof(outPacket));
    }

    // 2. Optimized 1.5ms Micro-Burst S.Port -> BLE Buffer Flush
    if (deviceConnected && SportSerial.available()) {
        uint32_t wait_start = micros();
        while (SportSerial.available() < 20 && (micros() - wait_start < 1500)) {
            delayMicroseconds(10);
        }

        uint8_t buf[20];
        int n = 0;
        while (SportSerial.available() > 0 && n < 20) {
            buf[n++] = SportSerial.read();
        }

        if (n > 0) {
            pTxCharacteristic->setValue(buf, n);
            pTxCharacteristic->notify();
            count_ble_bytes += n; 
        }
    }

    // 3. Hardware FailSafe Watchdog (300 ms trigger window)
    if (!is_failsafe_active && (millis() - last_recv_ms > 300)) {
        is_failsafe_active = true;
        dataTX.failsafe = true; 
        sbus_tx.data(dataTX);
        sbus_tx.Write(); 
    }

    // 4. USB Serial Telemetry Line Diagnostics
    if (millis() - last_debug_ms > 1000) {
        last_debug_ms = millis();
        Serial.printf("[TELEMETRIA] Rychlost: %d bajtov/s %s\n", count_ble_bytes, is_failsafe_active ? "[FAILSAFE ACTIVE]" : "");
        count_ble_bytes = 0;
    }

    // 5. Non-blocking RGB Diagnostic LED Scheduler
    static uint32_t last_led = 0;
    if (millis() - last_led > 300) {
        last_led = millis();
        if (is_failsafe_active || (millis() - last_recv_ms > 500)) {
            pixel.setPixelColor(0, pixel.Color(30, 0, 0)); // Red: Connection Lost
        } else {
            pixel.setPixelColor(0, deviceConnected ? pixel.Color(0, 0, 30) : pixel.Color(0, 15, 0)); // Blue: BLE OK | Green: RF Link OK
        }
        pixel.show();
    }
}
```

## 🛠️ Critical IDE Build Options (ESP32-S3 Core)
To ensure the native hardware USB interface and execution loops function properly on the Waveshare S3 Zero board, apply these exact target build flags in your config:
* **USB CDC On Boot:** `Enabled` (Maps the runtime log stream to the native type-C COM port)
* **Upload Mode:** `Internal USB`
* **Core Debug Level:** `None` (For raw production execution speeds)
