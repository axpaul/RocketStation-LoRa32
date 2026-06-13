/**
 * @file radio.cpp
 * @brief Contrôle du module radio SX1276 et gestion de l'affichage OLED dynamique.
 * @author Paul Miailhe
 * @date 14/06/2023
 */

#include "header.h"

// Variables d'état d'interruption et d'horloge
volatile bool receivedFlag = false;
volatile bool enableInterrupt = true;
ESP32Time rtc;

// Métadonnées de la dernière trame pour l'affichage OLED
char dispStatus[32] = "RX:0";
static uint32_t rxCount = 0;
static uint32_t errCount = 0;
char dispSsidApid[32] = "No Frame";
float dispRssi = 0.0;
float dispSnr = 0.0;
bool dispHasFrame = false;

// Variables de statistiques pour le second écran OLED (Rotation d'affichage)
static unsigned long lastTrackerPacketTime[256] = {0};
static uint32_t activeTrackersCount = 0;
static uint32_t bytesReceivedThisSecond = 0;
static uint32_t dataRateBps = 0;
static unsigned long lastPacketTime = 0;
static bool hasReceivedAny = false;
static unsigned long lastRateCalculation = 0;

static int dispMode = 0; // 0 = Infos Trame, 1 = Statistiques réseau
static unsigned long lastModeChange = 0;

/**
 * @brief Routine de Service d'Interruption (ISR) déclenchée à la réception d'un paquet LoRa.
 * 
 * Doit impérativement être placée en mémoire IRAM (attribut IRAM_ATTR) pour éviter
 * tout blocage système lors de l'accès simultané à la carte SD.
 */
void IRAM_ATTR setFlag(void) {
    if (!enableInterrupt) {
        return;
    }
    receivedFlag = true;
}

/**
 * @brief Initialise et configure le module LoRa SX1276 avec les réglages actifs.
 * @param u8g2 Pointeur vers l'afficheur OLED.
 * @param radio Pointeur vers l'instance de l'émetteur-récepteur SX1276 de RadioLib.
 * 
 * Configure la fréquence, la bande passante, le spreading factor, la limite de courant,
 * active le CRC matériel et attache la routine d'interruption de réception sur DIO0.
 * Bloque l'exécution avec un message d'erreur sur l'OLED en cas de panne matérielle.
 */
void RadioSettings(U8G2_SSD1306_128X64_NONAME_F_HW_I2C* u8g2, SX1276 *radio) {
  Serial.println("[SX1276] Initializing ...");
  rtc.setTime(0); // Démarrage à l'Epoch 0 (1er janvier 1970)
 
  int state = radio->begin(activeConfig.frequency);

  if (state == RADIOLIB_ERR_NONE) {
    radio->setOutputPower(17);
    radio->setBandwidth(activeConfig.bandwidth);
    radio->setCurrentLimit(120);
    radio->setSpreadingFactor(activeConfig.spreadingFactor);
    radio->setCRC(activeConfig.crcEnable, activeConfig.crcMode);
    radio->setDio0Action(setFlag, RISING);
    state = radio->startReceive();
    
    Serial.println("[SX1276] Complete!");
  } else {
    Serial.printf("[SX1276] begin FAILED, code: %d\n", state);
    u8g2->clearBuffer();
    u8g2->drawStr(0, 12, "Initializing radio: FAIL!");
    u8g2->sendBuffer();
    while (true) { delay(100); }
  }

  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("[SX1276] startReceive FAILED, code: %d\n", state);
    u8g2->clearBuffer();
    u8g2->drawStr(0, 12, "Initializing radio: FAIL!");
    u8g2->sendBuffer();
    while (true) { delay(100); }    
  }
}

/**
 * @brief Lit et calcule la tension de la batterie.
 * @return La tension de la batterie filtrée en volts.
 * 
 * Effectue un suréchantillonnage (moyenne sur 8 mesures) sur la broche ADC BATTERY_PIN.
 * Valide si la tension dépasse 4.4V pour détecter l'alimentation directe via USB.
 */
float readBatteryVoltage() {
  uint32_t sum = 0;
  for (int i = 0; i < 8; i++) {
    sum += analogRead(BATTERY_PIN);
    delayMicroseconds(10);
  }
  float avgAdc = sum / 8.0;
  // Conversion pour cartes LilyGO LoRa32 avec pont diviseur interne 100k/100k
  float voltage = (avgAdc / 4095.0) * 3.3 * 2.0 * 1.05;
  return voltage;
}

/**
 * @brief Met à jour l'écran OLED avec le statut de réception, la batterie, l'heure et la rotation d'écrans.
 * @param u8g2 Pointeur vers l'écran OLED.
 * @param radio Pointeur vers le module radio SX1276.
 * 
 * L'écran contient :
 * - Ligne d'en-tête (Fixe) : Le compteur RX/erreurs, l'heure RTC, le statut de batterie ou
 *   l'icône Bluetooth (SPP) si une connexion série sans fil est active.
 * - Zone principale (Alternée toutes les 4s) :
 *     - Écran 0 : Infos de la dernière trame (SSID, APID, RSSI, SNR).
 *     - Écran 1 : Fréquence active, paramètres radio (SF, BW) et débit/compteur de trackers actifs.
 */
void updateDisplay(U8G2_SSD1306_128X64_NONAME_F_HW_I2C* u8g2, SX1276* radio) {
  u8g2->clearBuffer();
  u8g2->setFont(u8g2_font_ncenB08_tr); // Police grasse principale

  char buf[64];
  char timeBuf[16];
  
  // 1. En-tête : Affichage du statut RX
  u8g2->drawStr(0, 12, dispStatus);
  
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

  int statusWidth = u8g2->getStrWidth(dispStatus);
  int availableSpace = clockX - statusWidth;

  // Affichage du symbole Bluetooth si connecté (prioritaire sur la tension)
  int btSpace = 0;
#if ENABLE_BLUETOOTH
  if (SerialBT.connected()) {
    btSpace = 10; // Espace réservé pour l'icône BT (7px de largeur + marge)
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
  int batWidth = 11 + 2 + batTextWidth; // Structure icône + texte

  if (availableSpace >= batWidth) {
    // Tension de batterie + Icône
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
    // Icône de batterie seule pour éviter le chevauchement
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
    dataRateBps = (bytesReceivedThisSecond * 1000) / elapsed;
    bytesReceivedThisSecond = 0;
    lastRateCalculation = millis();
  }

  // Recalcul du nombre de trackers actifs (ceux reçus durant les 10 dernières secondes)
  uint32_t tempActiveCount = 0;
  unsigned long now = millis();
  for (int i = 0; i < 256; i++) {
    if (lastTrackerPacketTime[i] != 0 && (now - lastTrackerPacketTime[i] <= 10000)) {
      tempActiveCount++;
    }
  }
  activeTrackersCount = tempActiveCount;

  // 4. Dessin de la zone d'affichage alternée
  if (dispMode == 0) {
    // Écran 1 : Télémétrie de la dernière trame reçue
    u8g2->drawStr(0, 30, dispSsidApid);

    if (dispHasFrame) {
      snprintf(buf, sizeof(buf), "RSSI: %.2f dBm", dispRssi);
    } else {
      snprintf(buf, sizeof(buf), "RSSI: -- dBm");
    }
    u8g2->drawStr(0, 44, buf);

    if (dispHasFrame) {
      snprintf(buf, sizeof(buf), "SNR: %.2f dB", dispSnr);
    } else {
      snprintf(buf, sizeof(buf), "SNR: -- dB");
    }
    u8g2->drawStr(0, 58, buf);
  } else {
    // Écran 2 : Configuration RF et Débit du récepteur
    snprintf(buf, sizeof(buf), "LoRa @ %.3f MHz", activeConfig.frequency);
    u8g2->drawStr(0, 30, buf);

    snprintf(buf, sizeof(buf), "SF: %d  |  BW: %.1f kHz", activeConfig.spreadingFactor, activeConfig.bandwidth);
    u8g2->drawStr(0, 44, buf);
    
    snprintf(buf, sizeof(buf), "Trackers: %d  |  %d B/s", activeTrackersCount, dataRateBps);
    u8g2->drawStr(0, 58, buf);
  }

  u8g2->sendBuffer();
}

/**
 * @brief Extrait la charge utile brute et les paramètres physiques d'une trame LoRa reçue.
 * @param u8g2 Pointeur vers l'écran OLED.
 * @param radio Pointeur vers le composant SX1276.
 * @param byteArr Tableau recevant la charge utile décodée.
 * @param maxLen Capacité maximale du tableau de réception.
 * @return Taille du paquet reçu en octets, ou 0 en cas d'absence de paquet ou d'erreur.
 * 
 * Cette fonction vérifie si le drapeau d'interruption a été activé. Elle désactive les interrupts,
 * lit les données reçues, décode le SSID et l'APID de la trame (si sa taille >= 3 octets),
 * met à jour les indicateurs physiques de signal (RSSI, SNR), actualise l'afficheur OLED,
 * puis réactive l'écoute continue du composant radio.
 */
size_t RadioReceive(U8G2_SSD1306_128X64_NONAME_F_HW_I2C* u8g2, SX1276 *radio, uint8_t* byteArr, size_t maxLen) {
  if (receivedFlag) {
    // Désactiver les interruptions le temps de traiter le paquet
    enableInterrupt = false;
    receivedFlag = false;

    // Obtenir la taille du paquet reçu
    size_t length = radio->getPacketLength();
    if (length > maxLen) {
      length = maxLen;
    }

    int state = radio->readData(byteArr, length);

    if (state == RADIOLIB_ERR_NONE) {
      rxCount++;
      // Formatage compact sans espace et limitation à 3 chiffres (0-999) pour conserver de l'espace
      uint32_t dispRx = rxCount % 1000;
      uint32_t dispErr = errCount % 1000;
      if (errCount == 0) {
        snprintf(dispStatus, sizeof(dispStatus), "RX:%d", dispRx);
      } else {
        snprintf(dispStatus, sizeof(dispStatus), "RX:%d E:%d", dispRx, dispErr);
      }
      
      // Décodage du couple SSID et APID (Trame minimale valide = 3 octets)
      if (length >= 3) {
        uint8_t ssid_num  = byteArr[0];
        uint8_t apid      = byteArr[1];
        uint8_t ssid_type = byteArr[2];

        const char* ssid_prefix = "OTHER";
        if (ssid_type == 0) ssid_prefix = "FX";
        else if (ssid_type == 1) ssid_prefix = "MF";
        else if (ssid_type == 2) ssid_prefix = "BALLOON";
        else if (ssid_type == 3) ssid_prefix = "OTHER";

        snprintf(dispSsidApid, sizeof(dispSsidApid), "%s%d (APID:%d)", ssid_prefix, ssid_num, apid);
        
        // Horodatage de présence du tracker
        lastTrackerPacketTime[ssid_num] = millis();
      } else {
        strcpy(dispSsidApid, "Invalid frame (<3B)");
      }
      
      dispRssi = radio->getRSSI();
      dispSnr = radio->getSNR();
      dispHasFrame = true;

      // Statistiques de débit de données
      bytesReceivedThisSecond += length;
      lastPacketTime = millis();
      hasReceivedAny = true;

      updateDisplay(u8g2, radio);
      RadioStartListen(radio);
      return length;
    } else if (state == RADIOLIB_ERR_CRC_MISMATCH) {
      errCount++;
      snprintf(dispStatus, sizeof(dispStatus), "RX:%d E:%d", rxCount % 1000, errCount % 1000);
      dispRssi = radio->getRSSI();
      dispSnr = radio->getSNR();
      
      updateDisplay(u8g2, radio);
      RadioStartListen(radio);
      return 0;
    } else {
      errCount++;
      snprintf(dispStatus, sizeof(dispStatus), "RX:%d E:%d", rxCount % 1000, errCount % 1000);
      dispRssi = radio->getRSSI();
      dispSnr = radio->getSNR();
      
      updateDisplay(u8g2, radio);
      RadioStartListen(radio);
      return 0;
    }
  } else {
    return 0; // Pas d'interruption
  }
}

/**
 * @brief Repasse le composant RF en écoute de réception et réactive sa routine d'interruption.
 * @param radio Pointeur vers le composant SX1276.
 */
void RadioStartListen(SX1276 *radio) {
  radio->startReceive();
  enableInterrupt = true;
}