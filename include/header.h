/**
 * @file header.h
 * @brief Fichier d'en-tête global pour le projet Nectar-RxStation-LoRa32.
 * @author Paul Miailhe
 * @date 14/06/2023
 * 
 * Contient les définitions des broches, les structures de données,
 * les paramètres de configuration native, et les déclarations de fonctions.
 */

#ifndef HEADER_H
#define HEADER_H

#define FW_VERSION "1.5.0"

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <SD.h>
#include <FS.h>
#include <ESP32Time.h>
#include <RadioLib.h>
#include <U8g2lib.h>

// ============================================================================
// Configuration du Bluetooth et de la télémétrie
// ============================================================================
#define ENABLE_BLUETOOTH 1

#if ENABLE_BLUETOOTH
#include "BluetoothSerial.h"
extern BluetoothSerial SerialBT;
#endif

// ============================================================================
// Définitions des broches (Pinout TTGO LoRa32 v2.1.6)
// ============================================================================
// Bus I2C pour l'afficheur OLED
#define I2C_SDA                     21
#define I2C_SCL                     22

// Connexions du module radio LoRa (SX1276)
#define RADIO_SCLK_PIN              5
#define RADIO_MISO_PIN              19
#define RADIO_MOSI_PIN              27
#define RADIO_CS_PIN                18
#define RADIO_DIO0_PIN              26
#define RADIO_RST_PIN               23
#define RADIO_DIO1_PIN              33
#define RADIO_BUSY_PIN              32

// Connexions du lecteur de carte SD (bus HSPI)
#define SDCARD_MOSI                 15
#define SDCARD_MISO                 2
#define SDCARD_SCLK                 14
#define SDCARD_CS                   13

// LED embarquée
#define BOARD_LED                   25
#define LED_ON                      HIGH

// Broche de lecture de la tension de batterie
#define BATTERY_PIN                 35

// ============================================================================
// Paramètres LoRa et protocoles de trames
// ============================================================================
#define MAX_FRAME_SIZE              255
#define NECTAR_MAGIC                0xEB

// Détermination de la bande radio native
#ifndef LORA_BAND_NATIVE
#define LORA_BAND_NATIVE            868
#endif

#if LORA_BAND_NATIVE == 868
  #define FREQ_MIN                  863.0f
  #define FREQ_MAX                  870.0f
  #define DEFAULT_FREQUENCY         869.525f
  #define DEFAULT_SF                8
  #define DEFAULT_BW                250.0f
#elif LORA_BAND_NATIVE == 433
  #define FREQ_MIN                  433.05f
  #define FREQ_MAX                  434.79f
  #define DEFAULT_FREQUENCY         433.500f
  #define DEFAULT_SF                8
  #define DEFAULT_BW                250.0f
#else
  #error "Configuration LORA_BAND_NATIVE invalide !"
#endif

// ============================================================================
// Structures et variables globales
// ============================================================================
struct LoRaConfig {
  float frequency;
  uint8_t spreadingFactor;
  float bandwidth;
  bool crcEnable;
  bool crcMode; // false = CCITT, true = IBM
};

extern LoRaConfig activeConfig;
extern ESP32Time rtc;
extern char logFileName[32];
extern char dispStatus[32];
extern bool *SDCard;
extern float dispRssi;
extern float dispSnr;
extern bool displayNeedsUpdate;

// ============================================================================
// Prototypes de fonctions
// ============================================================================

// Fonctions matérielles et de mesure
float readBatteryVoltage();

// Gestion de l'affichage OLED
void ScreenText(U8G2_SSD1306_128X64_NONAME_F_HW_I2C* u8g2);
void updateDisplay(U8G2_SSD1306_128X64_NONAME_F_HW_I2C* u8g2, SX1276* radio);

// Gestion de la carte SD
void SDCardDetection(U8G2_SSD1306_128X64_NONAME_F_HW_I2C* u8g2, SPIClass* SDSPI, bool* SDCard);
void checkSDCardSpace(U8G2_SSD1306_128X64_NONAME_F_HW_I2C* u8g2);
void writeFrameToFile(const char* filepath, const uint8_t* frame, size_t length, float rssi, float snr, const char* ssid_str, uint8_t apid);

// Gestion de la configuration non-volatile (NVS)
void loadLoRaConfig();
void saveLoRaConfig();
void resetLoRaConfig();

// Gestion du module radio SX1276
void RadioSettings(U8G2_SSD1306_128X64_NONAME_F_HW_I2C* u8g2, SX1276 *radio);
size_t RadioReceive(U8G2_SSD1306_128X64_NONAME_F_HW_I2C* u8g2, SX1276 *radio, uint8_t* byteArr, size_t maxLen);
void RadioStartListen(SX1276 *radio);
void IRAM_ATTR setFlag(void);

// Protocoles et communication NectarMC
uint16_t calculate_crc16(const uint8_t *data, size_t len);
void sendNectarFrame(uint8_t ssid_type, uint8_t ssid_num, uint8_t apid, const uint8_t *payload, size_t len, int8_t rssi, int8_t snr);

// Commandes de configuration AT (Série / Bluetooth)
void checkSerialCommands(SX1276 *radio);
void handleConfigCommand(const char* cmd, Stream& responseStream, SX1276 *radio);

#endif // HEADER_H
