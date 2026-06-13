/**
 * @file main.cpp
 * @brief Universal High-Speed Wireless Trainer Bridge (Gen 3 - Matrix Auto-Inversion)
 * @version 3.0.0
 * @date 2026-06-14
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

// Network Broadcast Address
static const uint8_t BROADCAST_MAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; 

// Výstupný parser pre bezpečné generovanie korektného SBUS signálu do rádia
bfs::SbusTx sbus_tx(&Serial2, -1, SBUS_TX_PIN, false);
HardwareSerial SportSerial(0); 

#define SBUS_PACKET_SIZE 25
struct __attribute__((packed)) sbus_raw_packet_t {
    uint8_t raw_bytes[SBUS_PACKET_SIZE]; 
};
sbus_raw_packet_t outPacket;

struct __attribute__((packed)) sbus_channels_packet_t {
    uint16_t kanaly[16]; 
};
sbus_channels_packet_t channelsPacket;
bfs::SbusData dataTX;

uint32_t count_ble_bytes = 0;   
uint32_t last_recv_ms = 0;
uint32_t last_debug_ms = 0;

// Globálne premenné pre riadenie inteligentnej autodetekcie polarity
bool isInvertedDetected = true;
uint32_t last_valid_frame_ms = 0;

class MyCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) { 
        deviceConnected = true; 
        pServer->updateConnParams(pServer->getConnId(), 0x0006, 0x000C, 0, 100);
    }
    void onDisconnect(BLEServer* pServer) { 
        deviceConnected = false; 
        BLEDevice::startAdvertising(); 
    }
};

// RECEIVER: Spustí sa hneď, ako sú doručené bezdrôtové dáta druhej strane
void on_data_recv(const esp_now_recv_info_t *info, const uint8_t *incoming_data, int len) {
    if (len != sizeof(sbus_channels_packet_t)) return;
    last_recv_ms = millis();
    
    const sbus_channels_packet_t *in = (sbus_channels_packet_t *)incoming_data;
    for (int i = 0; i < 16; i++) dataTX.ch[i] = in->kanaly[i];
    
    // ČISTÝ BYPASS: Vypnutie akéhokoľvek vnútorného softvérového safe/failsafe režimu
    dataTX.failsafe = false;
    dataTX.ch17 = false;
    dataTX.ch18 = false;
    
    sbus_tx.data(dataTX);
    sbus_tx.Write(); 
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n=== UNIVERSAL MODULE - AUTO-DETECTION MATRIX ACTIVE ===");

    pixel.begin();
    pixel.setPixelColor(0, pixel.Color(20, 20, 0)); pixel.show(); 

    BLEDevice::init("X9D_TRAINER");
    BLEServer *pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyCallbacks());
    BLEService *pService = pServer->createService("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
    pTxCharacteristic = pService->createCharacteristic("6E400003-B5A3-F393-E0A9-E50E24DCCA9E", BLECharacteristic::PROPERTY_NOTIFY);
    pTxCharacteristic->addDescriptor(new BLE2902());
    pService->start();
    BLEDevice::startAdvertising();

    delay(500);

    SportSerial.begin(57600, SERIAL_8N1, SPORT_PIN, -1);
    Serial1.begin(100000, SERIAL_8E2, SBUS_RX_PIN, -1); // SBUS IN (Prichádzajúce rádio)
    Serial2.begin(100000, SERIAL_8E2, -1, SBUS_TX_PIN); // SBUS OUT (Odchádzajúce rádio)
    
    delay(200);
    
    // Predvolené počiatočné nastavenie: Invertovaný stav (Pre moduly typu X9D)
    uart_set_line_inverse(UART_NUM_0, UART_SIGNAL_RXD_INV); 
    uart_set_line_inverse(UART_NUM_1, UART_SIGNAL_RXD_INV); 
    uart_set_line_inverse(UART_NUM_2, UART_SIGNAL_TXD_INV); 

    WiFi.mode(WIFI_STA);
    if (esp_now_init() == ESP_OK) {
        esp_now_register_recv_cb(on_data_recv);
        
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, BROADCAST_MAC, 6);
        peer.channel = 1;
        peer.encrypt = false;
        esp_now_add_peer(&peer);
    }

    sbus_tx.Begin();
}

void loop() {
    uint32_t nyni = millis();

    // 1. INTELIGENTNÝ AUTO-INVERZNÝ SKENER (Riešenie pre mix X20 a X9D)
    // Ak modul nedostane validný SBUS balík viac ako 350 ms, hardvérovo otočí polaritu UARTu za behu!
    if (nyni - last_valid_frame_ms > 350) {
        last_valid_frame_ms = nyni; 
        isInvertedDetected = !isInvertedDetected; 
        
        if (isInvertedDetected) {
            uart_set_line_inverse(UART_NUM_1, UART_SIGNAL_RXD_INV); // Zapnutie inverzie (Pre X9D)
            Serial.println("[MATRIX] Polarita zmenená: INVERTOVANÝ MÓD (X9D/Taranis)");
        } else {
            uart_set_line_inverse(UART_NUM_1, UART_SIGNAL_INV_DISABLE); // Vypnutie inverzie (Pre X20/ETHOS)
            Serial.println("[MATRIX] Polarita zmenená: PRIAMY MÓD (X20/FrSky Native)");
        }
        while(Serial1.available() > 0) Serial1.read(); // Vyčistenie chybného bufferu
    }

    // 2. VYSIELAČ: Spracovanie prúdu bajtov (Funguje nonstop aj v pokoji pák)
    while (Serial1.available() >= SBUS_PACKET_SIZE) {
        if (Serial1.peek() != 0x0F) {
            Serial1.read(); 
        } else {
            Serial1.readBytes(outPacket.raw_bytes, SBUS_PACKET_SIZE);
            
            // Overenie koncového bajtu SBUS protokolu (musí byť 0x00)
            if (outPacket.raw_bytes[24] == 0x00) {
                last_valid_frame_ms = nyni; // ZÁMOK: Našli sme správnu polaritu, skener sa zastaví
                
                // Bitový rozklad 25 surových bajtov priamo na kanály (Úplná eliminácia závislosti na pohybe pák)
                channelsPacket.kanaly[0]  = ((outPacket.raw_bytes[1]       | outPacket.raw_bytes[2] << 8) & 0x07FF);
                channelsPacket.kanaly[1]  = ((outPacket.raw_bytes[2] >> 3  | outPacket.raw_bytes[3] << 5) & 0x07FF);
                channelsPacket.kanaly[2]  = ((outPacket.raw_bytes[3] >> 6  | outPacket.raw_bytes[4] << 2 | outPacket.raw_bytes[5] << 10) & 0x07FF);
                channelsPacket.kanaly[3]  = ((outPacket.raw_bytes[5] >> 1  | outPacket.raw_bytes[6] << 7) & 0x07FF);
                channelsPacket.kanaly[4]  = ((outPacket.raw_bytes[6] >> 4  | outPacket.raw_bytes[7] << 4) & 0x07FF);
                channelsPacket.kanaly[5]  = ((outPacket.raw_bytes[7] >> 7  | outPacket.raw_bytes[8] << 1 | outPacket.raw_bytes[9] << 9) & 0x07FF);
                channelsPacket.kanaly[6]  = ((outPacket.raw_bytes[9] >> 2  | outPacket.raw_bytes[10] << 6) & 0x07FF);
                channelsPacket.kanaly[7]  = ((outPacket.raw_bytes[10] >> 5 | outPacket.raw_bytes[11] << 3) & 0x07FF);
                channelsPacket.kanaly[8]  = ((outPacket.raw_bytes[12]      | outPacket.raw_bytes[13] << 8) & 0x07FF);
                channelsPacket.kanaly[9]  = ((outPacket.raw_bytes[13] >> 3 | outPacket.raw_bytes[14] << 5) & 0x07FF);
                channelsPacket.kanaly[10] = ((outPacket.raw_bytes[14] >> 6 | outPacket.raw_bytes[15] << 2 | outPacket.raw_bytes[16] << 10) & 0x07FF);
                channelsPacket.kanaly[11] = ((outPacket.raw_bytes[16] >> 1 | outPacket.raw_bytes[17] << 7) & 0x07FF);
                channelsPacket.kanaly[12] = ((outPacket.raw_bytes[17] >> 4 | outPacket.raw_bytes[18] << 4) & 0x07FF);
                channelsPacket.kanaly[13] = ((outPacket.raw_bytes[18] >> 7 | outPacket.raw_bytes[19] << 1 | outPacket.raw_bytes[20] << 9) & 0x07FF);
                channelsPacket.kanaly[14] = ((outPacket.raw_bytes[20] >> 2 | outPacket.raw_bytes[21] << 6) & 0x07FF);
                channelsPacket.kanaly[15] = ((outPacket.raw_bytes[21] >> 5 | outPacket.raw_bytes[22] << 3) & 0x07FF);

                // Odoslanie očisteného poľa kanálov cez ESP-NOW
                esp_now_send(BROADCAST_MAC, (uint8_t *)&channelsPacket, sizeof(channelsPacket));
            }
            break;
        }
    }

    // 3. TELEMETRIA (S.Port -> BLE)
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

    // LOCAL SAFE REŽIM (FAILSAFE) KOMPLETNE VYREZANÝ PRE PLYNULÝ STREAM

    // 4. DIAGNOSTICKÝ VÝPIS DO PC
    if (millis() - last_debug_ms > 1000) {
        last_debug_ms = millis();
        bool is_link_dead = (millis() - last_recv_ms > 400);
        Serial.printf("[SYSTEM] BLE Rýchlosť: %d B/s | Linka: %s | Polarita RX: %s\n", 
                      count_ble_bytes, is_link_dead ? "DEAD" : "LIVE_OK", isInvertedDetected ? "INVERT" : "DIRECT");
        count_ble_bytes = 0;
    }

    // 5. STAVOVÁ INDIKÁCIA (RGB LED)
    static uint32_t last_led = 0;
    if (millis() - last_led > 300) {
        last_led = millis();
        if (millis() - last_recv_ms > 400) {
            pixel.setPixelColor(0, pixel.Color(35, 0, 0)); 
        } else {
            pixel.setPixelColor(0, deviceConnected ? pixel.Color(0, 0, 35) : pixel.Color(0, 20, 0)); 
        }
        pixel.show();
    }
}
