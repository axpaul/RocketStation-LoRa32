#include "header.h"

void ScreenText(U8G2_SSD1306_128X64_NONAME_F_HW_I2C* u8g2) {
  Wire.beginTransmission(0x3C);

  if (Wire.endTransmission() == 0) 
  { 
    u8g2->begin();
    u8g2->clearBuffer();
    u8g2->setFlipMode(0);
    u8g2->setFontMode(1);
    u8g2->setDrawColor(1);
    u8g2->setFontDirection(0);
    u8g2->firstPage();

    do 
    {
      u8g2->setFont(u8g2_font_fur17_tf);
      u8g2->drawStr(12, 26, "NECTAR");
      u8g2->drawHLine(2, 35, 124);
      u8g2->drawHLine(2, 36, 124);
      u8g2->setFont(u8g2_font_fur11_tf);
      u8g2->drawStr(14, 58, "RX STATION");
    } while (u8g2->nextPage());
    u8g2->sendBuffer();
    u8g2->setFont(u8g2_font_fur11_tf);
    delay(3000);
  }
}

void SDCardDetection(U8G2_SSD1306_128X64_NONAME_F_HW_I2C* u8g2, SPIClass* SDSPI, bool* SDCard) {
  pinMode(SDCARD_MISO, INPUT_PULLUP);
  SDSPI->begin(SDCARD_SCLK, SDCARD_MISO, SDCARD_MOSI, SDCARD_CS);

  u8g2->setFont(u8g2_font_ncenB08_tr);
  if (!SD.begin(SDCARD_CS, *SDSPI)) {
    *SDCard = 0;
    Serial.println("[SD] Initialization FAILED!");
    
    // Alerte visuelle rapide au boot
    u8g2->clearBuffer();
    u8g2->drawStr(0, 32, "SD Card: FAILED!");
    u8g2->sendBuffer();
    delay(1500);
  }  
  else {
    *SDCard = 1;
    uint32_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("[SD] Initialization OK. Size: %.2f GB\n", cardSize / 1024.0);

    // Notification rapide de succès
    u8g2->clearBuffer();
    char buf[64];
    snprintf(buf, sizeof(buf), "SD Card: OK (%.1f GB)", cardSize / 1024.0);
    u8g2->drawStr(0, 32, buf);
    u8g2->sendBuffer();
    delay(1000);
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