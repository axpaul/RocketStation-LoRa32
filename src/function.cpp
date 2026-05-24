#include "header.h"
#include <Preferences.h>

void ScreenText(U8G2_SSD1306_128X64_NONAME_F_HW_I2C* u8g2) {
  Wire.beginTransmission(0x3C);

  if (Wire.endTransmission() == 0) 
  { 
    u8g2->begin();
    u8g2->setFlipMode(0);
    u8g2->setFontMode(1);
    u8g2->setDrawColor(1);
    u8g2->setFontDirection(0);

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char idBuf[32];
    snprintf(idBuf, sizeof(idBuf), "ID: %02X%02X  v%s", mac[4], mac[5], FW_VERSION);

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
      
      // Info footer
      u8g2->setFont(u8g2_font_5x7_tr);
      u8g2->drawStr(5, 62, idBuf);
      
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
    
    // Blinking visual alert at boot (6 toggles, 300ms each = 1.8s)
    for (int i = 0; i < 6; i++) {
      u8g2->clearBuffer();
      if (i % 2 == 0) {
        // Draw warning triangle
        u8g2->drawLine(64, 10, 46, 38);
        u8g2->drawLine(64, 10, 82, 38);
        u8g2->drawLine(46, 38, 82, 38);
        u8g2->drawLine(64, 11, 47, 37);
        u8g2->drawLine(64, 11, 81, 37);
        u8g2->drawLine(47, 37, 81, 37);
        
        // Exclamation point
        u8g2->drawBox(63, 18, 2, 10);
        u8g2->drawBox(63, 31, 2, 2);
        
        // Text
        int w = u8g2->getStrWidth("SD CARD: FAILED!");
        u8g2->drawStr((128 - w) / 2, 56, "SD CARD: FAILED!");
      }
      u8g2->sendBuffer();
      delay(300);
    }
  }  
  else {
    *SDCard = 1;
    uint32_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("[SD] Initialization OK. Size: %.2f GB\n", cardSize / 1024.0);

    // Success display with a circular checkmark
    u8g2->clearBuffer();
    
    // Draw thick circle
    u8g2->drawCircle(64, 26, 14);
    u8g2->drawCircle(64, 26, 13);
    
    // Draw checkmark
    u8g2->drawLine(55, 25, 61, 31);
    u8g2->drawLine(55, 26, 61, 32);
    u8g2->drawLine(55, 27, 61, 33);
    
    u8g2->drawLine(61, 31, 73, 19);
    u8g2->drawLine(61, 32, 73, 20);
    u8g2->drawLine(61, 33, 73, 21);

    // Success text
    char buf[64];
    snprintf(buf, sizeof(buf), "SD CARD: OK (%.1f GB)", cardSize / 1024.0);
    int w = u8g2->getStrWidth(buf);
    u8g2->drawStr((128 - w) / 2, 56, buf);
    
    u8g2->sendBuffer();
    delay(1500);
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
  } else {
    Serial.println("[SD] Error: Failed to open log file. SD card marked as OFFLINE.");
    *SDCard = false;
  }
}

// Charge la configuration radio depuis la mémoire Flash non volatile (NVS) de l'ESP32
void loadLoRaConfig() {
  Preferences prefs;
  
  // Ouvre l'espace NVS de nom "loracfg" en mode Lecture/Écriture (false = non-lecture seule)
  prefs.begin("loracfg", false);
  
  // Lit les clés. Si elles n'existent pas (1er démarrage), la valeur de secours par défaut est renvoyée
  activeConfig.frequency = prefs.getFloat("freq", DEFAULT_FREQUENCY);
  activeConfig.spreadingFactor = prefs.getUChar("sf", DEFAULT_SF);
  activeConfig.bandwidth = prefs.getFloat("bw", DEFAULT_BW);
  
  prefs.end(); // Ferme proprement l'accès NVS
  
  Serial.printf("[CONFIG] Loaded from NVS: Freq=%.3f MHz, SF=%d, BW=%.1f kHz\n", 
                activeConfig.frequency, activeConfig.spreadingFactor, activeConfig.bandwidth);
}

// Enregistre les réglages actifs dans la mémoire Flash non volatile (NVS)
void saveLoRaConfig() {
  Preferences prefs;
  prefs.begin("loracfg", false);
  
  // Écrit les clés / valeurs
  prefs.putFloat("freq", activeConfig.frequency);
  prefs.putUChar("sf", activeConfig.spreadingFactor);
  prefs.putFloat("bw", activeConfig.bandwidth);
  
  prefs.end();
  
  Serial.println("[CONFIG] Saved current config to NVS.");
}

// Efface tous les enregistrements NVS sous le nom "loracfg" pour revenir aux valeurs usines
void resetLoRaConfig() {
  Preferences prefs;
  prefs.begin("loracfg", false);
  
  prefs.clear(); // Efface toutes les clés de ce namespace
  
  prefs.end();
  
  Serial.println("[CONFIG] NVS configuration cleared.");
}