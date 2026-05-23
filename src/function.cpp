#include "header.h"

void SDCardDetection(U8G2_SSD1306_128X64_NONAME_F_HW_I2C* u8g2, SPIClass* SDSPI, bool* SDCard) {
  pinMode(SDCARD_MISO, INPUT_PULLUP);
  SDSPI->begin(SDCARD_SCLK, SDCARD_MISO, SDCARD_MOSI, SDCARD_CS);

  if (!SD.begin(SDCARD_CS, *SDSPI)) {
    *SDCard = 0;
    Serial.println("[SD] Initialization FAILED!");
  }  
  else {
    *SDCard = 1;
    uint32_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("[SD] Initialization OK. Size: %.2f GB\n", cardSize / 1024.0);
  }
}

void checkSDCardSpace(U8G2_SSD1306_128X64_NONAME_F_HW_I2C* u8g2) {
  uint64_t totalBytes = SD.totalBytes();
  uint64_t usedBytes = SD.usedBytes();
  uint64_t freeBytes = totalBytes - usedBytes;

  double freeSpaceGB = freeBytes / (1024.0 * 1024.0 * 1024.0);
  double totalSpaceGB = totalBytes / (1024.0 * 1024.0 * 1024.0);

  Serial.printf("[SD] Free Space: %.2f GB / %.2f GB\n", freeSpaceGB, totalSpaceGB);
}

void writeFrameToFile(const char* filepath, const uint8_t* frame, size_t length, float rssi, float snr, const char* ssid_str, uint8_t apid) {
  File log = SD.open(filepath, FILE_APPEND);
  if (log) {
    // 1. Horodatage (HH:MM:SS) depuis l'horloge RTC
    char timeStr[16];
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d", rtc.getHour(true), rtc.getMinute(), rtc.getSecond());
    log.print(timeStr);
    log.print(",");

    // 2. Paramètres physiques (RSSI, SNR)
    log.print(rssi);
    log.print(",");
    log.print(snr);
    log.print(",");

    // 3. Paramètres de trame (SSID, APID)
    log.print(ssid_str);
    log.print(",");
    log.print(apid);
    log.print(",");

    // 4. Trame brute en Hexadécimal continu
    for (size_t i = 0; i < length; i++) {
      char hex[3];
      sprintf(hex, "%02X", frame[i]);
      log.print(hex);
    }

    log.println();
    log.close();
  } 
}