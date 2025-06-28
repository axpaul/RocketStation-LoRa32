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
      u8g2->drawStr(-1, 25, "ROCKET");
      u8g2->drawHLine(2, 35, 80);
      u8g2->drawHLine(3, 36, 80);
      u8g2->drawVLine(45, 32, 12);
      u8g2->drawVLine(46, 33, 12);
      u8g2->setFont(u8g2_font_fur17_tf);
      u8g2->drawStr(58, 60, "LoRa");
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
  u8g2->clearBuffer();

  if (!SD.begin(SDCARD_CS, *SDSPI)) {
    *SDCard = 0;
    do {
      u8g2->setCursor(0, 16);
      u8g2->println( "SDCard FAILED");
    } 
    while (u8g2->nextPage());
  }  
  else {
    *SDCard = 1;
    uint32_t cardSize = SD.cardSize() / (1024 * 1024);
    do {
      u8g2->setCursor(0, 16);
      u8g2->print( "SDCard:");;
      u8g2->print(cardSize / 1024.0);;
      u8g2->println(" GB");;
    } 
    while (u8g2->nextPage());    
  }
  
  u8g2->sendBuffer();
  delay(3000);
  u8g2->clearBuffer();

}

void checkSDCardSpace(U8G2_SSD1306_128X64_NONAME_F_HW_I2C* u8g2) {
  uint64_t totalBytes = SD.totalBytes();
  uint64_t usedBytes = SD.usedBytes();
  uint64_t freeBytes = totalBytes - usedBytes;

  double freeSpaceGB = freeBytes / (1024.0 * 1024.0 * 1024.0);
  double totalSpaceGB = totalBytes / (1024.0 * 1024.0 * 1024.0);

  u8g2->clearBuffer();

  u8g2->setCursor(0, 16);
  u8g2->print("Free space: ");
  u8g2->print(freeSpaceGB);
  u8g2->println(" GB");

  u8g2->setCursor(0, 32);
  u8g2->print("Total space: ");
  u8g2->print(totalSpaceGB);
  u8g2->println(" GB");

  do {
    // Draw the page contents on the buffer
  } while (u8g2->nextPage());

  u8g2->sendBuffer();
  delay(3000);
  u8g2->clearBuffer();
}

void writeFrameToFile(const uint8_t* frame, size_t length) {
  File log;
  log = SD.open("/log.txt", FILE_APPEND);
  if (log) {
    for (size_t i = 0; i < length; i++) {
      char hex[4];  // Inclure l'espace après l'octet hexadécimal
      sprintf(hex, "%02X ", frame[i]);
      log.print(hex);
    }

    log.println();  // Utiliser println() pour ajouter automatiquement une nouvelle ligne
    log.close();
  } 
}