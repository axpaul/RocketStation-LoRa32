#include "header.h"

void ScreenText(U8G2_SSD1306_128X64_NONAME_F_HW_I2C* u8g2) {
  Wire.beginTransmission(0x3C);

  if (Wire.endTransmission() == 0) 
  { 
    u8g2->begin();
    u8g2->setFlipMode(0);
    u8g2->setFontMode(1);
    u8g2->setDrawColor(1);
    u8g2->setFontDirection(0);

    // Dynamic 3-second startup animation (15 frames of 200ms)
    for (int frame = 0; frame < 15; frame++) {
      u8g2->clearBuffer();
      
      // Draw Title and Subtitle on the left
      u8g2->setFont(u8g2_font_fur17_tf);
      u8g2->drawStr(5, 26, "NECTAR");
      u8g2->drawHLine(2, 33, 85);
      u8g2->drawHLine(2, 34, 85);
      u8g2->setFont(u8g2_font_fur11_tf);
      u8g2->drawStr(5, 52, "RX STATION");
      
      // Draw Antenna Tower on the right (centered at x = 110)
      // Base legs
      u8g2->drawLine(110, 35, 100, 58);
      u8g2->drawLine(110, 35, 120, 58);
      u8g2->drawHLine(97, 58, 27);
      // Cross lattice
      u8g2->drawLine(103, 51, 110, 43);
      u8g2->drawLine(117, 51, 110, 43);
      // Vertical mast
      u8g2->drawVLine(110, 22, 13);
      // Tip disc
      u8g2->drawDisc(110, 22, 2);

      // Animate transmitting waves (left quadrants, propagating outward)
      int waveStage = frame % 5; // Cycle: 0 -> 1 -> 2 -> 3 -> 4
      if (waveStage >= 1) {
        u8g2->drawCircle(110, 22, 7, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_LOWER_LEFT);
      }
      if (waveStage >= 2) {
        u8g2->drawCircle(110, 22, 14, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_LOWER_LEFT);
      }
      if (waveStage >= 3) {
        u8g2->drawCircle(110, 22, 21, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_LOWER_LEFT);
      }
      if (waveStage >= 4) {
        u8g2->drawCircle(110, 22, 28, U8G2_DRAW_UPPER_LEFT | U8G2_DRAW_LOWER_LEFT);
      }

      u8g2->sendBuffer();
      delay(200);
    }
    u8g2->setFont(u8g2_font_fur11_tf);
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