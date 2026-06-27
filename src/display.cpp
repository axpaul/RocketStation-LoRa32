/**
 * @file display.cpp
 * @brief Gestion de l'affichage OLED dynamique (écrans de démarrage et de statut).
 * @author Paul Miailhe
 * @date 27/06/2026
 */

#include "header.h"

// Spinlock partagé avec radio.cpp pour les variables d'affichage inter-cœur
extern portMUX_TYPE dispMux;

// Variables d'état locales pour la rotation et le calcul du débit d'affichage
static uint32_t activeTrackersCount = 0;
static uint32_t dataRateBps = 0;
static unsigned long lastRateCalculation = 0;

static int dispMode = 0; // 0 = Infos Trame, 1 = Statistiques réseau
static unsigned long lastModeChange = 0;

/**
 * @brief Affiche l'écran de démarrage graphique avec une animation d'ondes radio.
 * @param u8g2 Pointeur vers l'instance de l'écran OLED.
 */
void ScreenText(U8G2_SSD1306_128X64_NONAME_F_HW_I2C* u8g2) {
  Wire.beginTransmission(0x3C);

  if (Wire.endTransmission() == 0) { 
    u8g2->begin();
    u8g2->setFlipMode(0);
    u8g2->setFontMode(1);
    u8g2->setDrawColor(1);
    u8g2->setFontDirection(0);

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char idBuf[32];
    snprintf(idBuf, sizeof(idBuf), "ID: %02X%02X  v%s", mac[4], mac[5], FW_VERSION);

    // Animation de démarrage dynamique (15 trames de 200ms = 3 secondes)
    for (int frame = 0; frame < 15; frame++) {
      u8g2->clearBuffer();
      
      // Dessin du titre et sous-titre de la station
      u8g2->setFont(u8g2_font_fur17_tf);
      u8g2->drawStr(5, 26, "NECTAR");
      u8g2->drawHLine(2, 33, 85);
      u8g2->drawHLine(2, 34, 85);
      u8g2->setFont(u8g2_font_fur11_tf);
      u8g2->drawStr(5, 52, "RX STATION");
      
      // Pied de page d'informations
      u8g2->setFont(u8g2_font_5x7_tr);
      u8g2->drawStr(5, 62, idBuf);
      
      // Dessin du pylône de l'antenne radio (centré en x = 110)
      u8g2->drawLine(110, 35, 100, 58);
      u8g2->drawLine(110, 35, 120, 58);
      u8g2->drawHLine(97, 58, 27);
      
      // Diagonales métalliques
      u8g2->drawLine(103, 51, 110, 43);
      u8g2->drawLine(117, 51, 110, 43);
      
      // Mât d'antenne vertical
      u8g2->drawVLine(110, 22, 13);
      
      // Bulbe émetteur au sommet
      u8g2->drawDisc(110, 22, 2);

      // Animation de propagation des ondes (quadrants gauches)
      int waveStage = frame % 5;
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

/**
 * @brief Met à jour l'écran OLED avec le statut de réception, la batterie, l'heure et la rotation d'écrans.
 */
void updateDisplay(U8G2_SSD1306_128X64_NONAME_F_HW_I2C* u8g2, SX1276* radio) {
  u8g2->clearBuffer();

  // Copie locale thread-safe des variables partagées avec le Cœur 1
  char localStatus[32];
  char localSsidApid[32];
  float localRssi;
  float localSnr;
  bool localHasFrame;
  uint32_t localBytesReceived;
  unsigned long localTrackerTimes[256];

  taskENTER_CRITICAL(&dispMux);
  memcpy(localStatus, dispStatus, sizeof(localStatus));
  memcpy(localSsidApid, dispSsidApid, sizeof(localSsidApid));
  localRssi = dispRssi;
  localSnr = dispSnr;
  localHasFrame = dispHasFrame;
  localBytesReceived = bytesReceivedThisSecond;
  bytesReceivedThisSecond = 0;
  memcpy(localTrackerTimes, lastTrackerPacketTime, sizeof(localTrackerTimes));
  taskEXIT_CRITICAL(&dispMux);

  u8g2->setFont(u8g2_font_ncenB08_tr); // Police grasse principale

  char buf[64];
  char timeBuf[16];
  
  // 1. En-tête : Affichage du statut RX
  u8g2->drawStr(0, 12, localStatus);
  
  // En-tête : Affichage de l'heure
  snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", rtc.getHour(true), rtc.getMinute(), rtc.getSecond());
  int clockWidth = u8g2->getStrWidth(timeBuf);
  int clockX = 128 - clockWidth;
  u8g2->drawStr(clockX, 12, timeBuf);

  // 2. Gestion de la batterie et de l'icône de liaison sans fil
  float vbat = readBatteryVoltage();
  char batBuf[16] = "";
  bool isUsb = (vbat > 4.4);
  if (isUsb) {
    strcpy(batBuf, "USB");
  } else {
    snprintf(batBuf, sizeof(batBuf), "%.1fV", vbat);
  }

  int statusWidth = u8g2->getStrWidth(localStatus);
  int availableSpace = clockX - statusWidth;

  // Affichage du symbole Bluetooth si connecté (prioritaire sur la tension)
  int btSpace = 0;
#if ENABLE_BLUETOOTH
  if (SerialBT.connected()) {
    btSpace = 10;
    int bx = clockX - 6;
    int by = 2;
    u8g2->drawLine(bx, by, bx, by + 10);
    u8g2->drawLine(bx - 3, by + 3, bx + 3, by + 7);
    u8g2->drawLine(bx - 3, by + 7, bx + 3, by + 3);
    u8g2->drawLine(bx + 3, by + 3, bx, by);
    u8g2->drawLine(bx + 3, by + 7, bx, by + 10);
  }
#endif

  availableSpace -= btSpace;
  int batTextWidth = u8g2->getStrWidth(batBuf);
  int batWidth = 11 + 2 + batTextWidth;

  if (availableSpace >= batWidth) {
    int blockX = statusWidth + (availableSpace - batWidth) / 2;
    u8g2->drawFrame(blockX, 5, 10, 6);
    u8g2->drawBox(blockX + 10, 7, 1, 2);
    
    if (!isUsb) {
      int pct = (int)((vbat - 3.2) * 100.0);
      if (pct < 0) pct = 0;
      if (pct > 100) pct = 100;
      int fillWidth = (pct * 8) / 100;
      if (fillWidth > 0) {
        u8g2->drawBox(blockX + 1, 6, fillWidth, 4);
      }
    } else {
      u8g2->drawBox(blockX + 1, 6, 8, 4);
    }
    u8g2->drawStr(blockX + 11 + 2, 12, batBuf);
  } else if (availableSpace >= 15) {
    int blockX = statusWidth + (availableSpace - 11) / 2;
    u8g2->drawFrame(blockX, 5, 10, 6);
    u8g2->drawBox(blockX + 10, 7, 1, 2);
    if (!isUsb) {
      int pct = (int)((vbat - 3.2) * 100.0);
      if (pct < 0) pct = 0;
      if (pct > 100) pct = 100;
      int fillWidth = (pct * 8) / 100;
      if (fillWidth > 0) {
        u8g2->drawBox(blockX + 1, 6, fillWidth, 4);
      }
    } else {
      u8g2->drawBox(blockX + 1, 6, 8, 4);
    }
  }

  // Ligne de délimitation
  u8g2->drawHLine(0, 15, 128);
  u8g2->setFont(u8g2_font_ncenB08_tr);

  // 3. Rotation des modes d'affichage OLED (4 secondes par écran)
  if (millis() - lastModeChange >= 4000) {
    lastModeChange = millis();
    dispMode = (dispMode + 1) % 2;
  }

  // Calcul du débit de données instantané toutes les secondes
  unsigned long elapsed = millis() - lastRateCalculation;
  if (elapsed >= 1000) {
    dataRateBps = (localBytesReceived * 1000) / elapsed;
    lastRateCalculation = millis();
  }

  // Recalcul du nombre de trackers actifs (ceux reçus durant les 10 dernières secondes)
  uint32_t tempActiveCount = 0;
  unsigned long now = millis();
  for (int i = 0; i < 256; i++) {
    if (localTrackerTimes[i] != 0 && (now - localTrackerTimes[i] <= 10000)) {
      tempActiveCount++;
    }
  }
  activeTrackersCount = tempActiveCount;

  // 4. Dessin de la zone d'affichage alternée
  if (dispMode == 0) {
    u8g2->drawStr(0, 30, localSsidApid);

    if (localHasFrame) {
      snprintf(buf, sizeof(buf), "RSSI: %.2f dBm", localRssi);
    } else {
      snprintf(buf, sizeof(buf), "RSSI: -- dBm");
    }
    u8g2->drawStr(0, 44, buf);

    if (localHasFrame) {
      snprintf(buf, sizeof(buf), "SNR: %.2f dB", localSnr);
    } else {
      snprintf(buf, sizeof(buf), "SNR: -- dB");
    }
    u8g2->drawStr(0, 58, buf);
  } else {
    snprintf(buf, sizeof(buf), "LoRa @ %.3f MHz", activeConfig.frequency);
    u8g2->drawStr(0, 30, buf);

    snprintf(buf, sizeof(buf), "SF: %d  |  BW: %.1f kHz", activeConfig.spreadingFactor, activeConfig.bandwidth);
    u8g2->drawStr(0, 44, buf);
    
    snprintf(buf, sizeof(buf), "Trackers: %d  |  %d B/s", activeTrackersCount, dataRateBps);
    u8g2->drawStr(0, 58, buf);
  }

  u8g2->sendBuffer();
}
