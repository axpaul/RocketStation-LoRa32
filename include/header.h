// Author : Paul Miailhe
// Date : 14/06/2023

#ifndef HEADER_H
#define HEADER_H

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <SD.h>
#include <FS.h>
#include <ESP32Time.h>
#include <RadioLib.h>
#include <U8g2lib.h>

#define I2C_SDA                     21
#define I2C_SCL                     22

#define RADIO_SCLK_PIN              5
#define RADIO_MISO_PIN              19
#define RADIO_MOSI_PIN              27
#define RADIO_CS_PIN                18
#define RADIO_DIO0_PIN              26
#define RADIO_RST_PIN               23
#define RADIO_DIO1_PIN              33
#define RADIO_BUSY_PIN              32

#define SDCARD_MOSI                 15
#define SDCARD_MISO                 2
#define SDCARD_SCLK                 14
#define SDCARD_CS                   13

#define BOARD_LED                   25
#define LED_ON                      HIGH

#define ADC_PIN                     27

#define MAX_FRAME_SIZE 255
#define NECTAR_MAGIC 0xEB
#define FREQUENCY 869.525 

extern ESP32Time rtc;
extern char logFileName[32];

void ScreenText(U8G2_SSD1306_128X64_NONAME_F_HW_I2C* u8g2);
void SDCardDetection(U8G2_SSD1306_128X64_NONAME_F_HW_I2C* u8g2, SPIClass* SDSPI, bool* SDCard);
void checkSDCardSpace(U8G2_SSD1306_128X64_NONAME_F_HW_I2C* u8g2);
void writeFrameToFile(const char* filepath, const uint8_t* frame, size_t length, float rssi, float snr, const char* ssid_str, uint8_t apid);

void RadioSettings(U8G2_SSD1306_128X64_NONAME_F_HW_I2C* u8g2, SX1276 *radio);
size_t RadioReceive(U8G2_SSD1306_128X64_NONAME_F_HW_I2C* u8g2, SX1276 *radio, uint8_t* byteArr, size_t maxLen);
void updateDisplay(U8G2_SSD1306_128X64_NONAME_F_HW_I2C* u8g2, SX1276* radio);
void setFlag(void);
void RadioStartListen(SX1276 *radio);

uint16_t calculate_crc16(const uint8_t *data, size_t len);
void sendNectarFrame(uint8_t ssid_type, uint8_t ssid_num, uint8_t apid, const uint8_t *payload, size_t len);

#endif // HEADER_H
