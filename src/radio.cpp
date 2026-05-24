#include "header.h"

volatile bool receivedFlag = false;
volatile bool enableInterrupt = true;
ESP32Time rtc;

char dispStatus[32] = "RX: 0";
static uint32_t rxCount = 0;
static uint32_t errCount = 0;
char dispSsidApid[32] = "No Frame";
float dispRssi = 0.0;
float dispSnr = 0.0;
bool dispHasFrame = false;

// Statistiques de performance réseau
static bool seenTrackers[256] = {false};
static uint32_t activeTrackersCount = 0;
static uint32_t bytesReceivedThisSecond = 0;
static uint32_t dataRateBps = 0;
static unsigned long lastPacketTime = 0;
static bool hasReceivedAny = false;
static unsigned long lastRateCalculation = 0;

void setFlag(void)
{
    // check if the interrupt is enabled
    if (!enableInterrupt) {
        return;
    }

    // we got a packet, set the flag
    receivedFlag = true;
}

// initialize SX1276 with default settings
void RadioSettings(U8G2_SSD1306_128X64_NONAME_F_HW_I2C* u8g2, SX1276 *radio){
  Serial.println("[SX1276] Initializing ...");
  rtc.setTime(0, 0, 0, 0, 0, 2023);
 
  int state = radio->begin(FREQUENCY);

  if (state == RADIOLIB_ERR_NONE) {
    radio->setOutputPower(17);
    radio->setBandwidth(250);
    radio->setCurrentLimit(120);
    radio->setSpreadingFactor(8);
    radio->setCRC(true, false);
    radio->setDio0Action(setFlag, RISING);
    state = radio->startReceive();
    
    Serial.println("[SX1276] Complete!");
  } 
  else {
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

static int dispMode = 0; // 0 = Packet Info, 1 = Network Stats
static unsigned long lastModeChange = 0;

float readBatteryVoltage() {
  uint32_t sum = 0;
  for (int i = 0; i < 8; i++) {
    sum += analogRead(BATTERY_PIN);
    delayMicroseconds(10);
  }
  float avgAdc = sum / 8.0;
  // Standard conversion for LilyGO LoRa32 boards using internal ESP32 reference (3.3V)
  // and division by 2 (100k/100k resistors). A small 1.05 correction factor accounts for typical ADC offsets.
  float voltage = (avgAdc / 4095.0) * 3.3 * 2.0 * 1.05;
  return voltage;
}

void updateDisplay(U8G2_SSD1306_128X64_NONAME_F_HW_I2C* u8g2, SX1276* radio) {
  u8g2->clearBuffer();
  u8g2->setFont(u8g2_font_ncenB08_tr);

  char buf[64];
  char timeBuf[16];
  
  // Ligne 1 : Statut et Horloge en temps réel (toujours visible)
  u8g2->drawStr(0, 12, dispStatus);
  
  // Real-time clock aligned to the right
  snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", rtc.getHour(true), rtc.getMinute(), rtc.getSecond());
  int clockWidth = u8g2->getStrWidth(timeBuf);
  int clockX = 128 - clockWidth;
  u8g2->drawStr(clockX, 12, timeBuf);

  // Dynamic battery info display centered in the header
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
  int batTextWidth = u8g2->getStrWidth(batBuf);
  int batWidth = 13 + 4 + batTextWidth; // Icon (13px) + spacing (4px) + text width

  if (availableSpace >= batWidth + 10) {
    // Show both the battery icon and the voltage / USB text
    int blockX = statusWidth + (availableSpace - batWidth) / 2;
    
    // Draw battery outline & tip
    u8g2->drawFrame(blockX, 5, 12, 6);
    u8g2->drawBox(blockX + 12, 7, 1, 2);
    
    if (!isUsb) {
      int pct = (int)((vbat - 3.2) * 100.0);
      if (pct < 0) pct = 0;
      if (pct > 100) pct = 100;
      int fillWidth = (pct * 10) / 100;
      if (fillWidth > 0) {
        u8g2->drawBox(blockX + 1, 6, fillWidth, 4);
      }
    } else {
      // Full fill when on USB power
      u8g2->drawBox(blockX + 1, 6, 10, 4);
    }
    
    // Print battery voltage/USB text
    u8g2->drawStr(blockX + 13 + 4, 12, batBuf);
  }
  else if (availableSpace >= 20) {
    // Show battery icon only to prevent overlap
    int blockX = statusWidth + (availableSpace - 13) / 2;
    u8g2->drawFrame(blockX, 5, 12, 6);
    u8g2->drawBox(blockX + 12, 7, 1, 2);
    if (!isUsb) {
      int pct = (int)((vbat - 3.2) * 100.0);
      if (pct < 0) pct = 0;
      if (pct > 100) pct = 100;
      int fillWidth = (pct * 10) / 100;
      if (fillWidth > 0) {
        u8g2->drawBox(blockX + 1, 6, fillWidth, 4);
      }
    } else {
      u8g2->drawBox(blockX + 1, 6, 10, 4);
    }
  }

  u8g2->drawHLine(0, 15, 128);

  // Gérer la rotation des écrans toutes les 4 secondes
  if (millis() - lastModeChange >= 4000) {
    lastModeChange = millis();
    dispMode = (dispMode + 1) % 2;
  }

  // Calcul du débit de données toutes les secondes (normalisé au temps réel écoulé)
  unsigned long elapsed = millis() - lastRateCalculation;
  if (elapsed >= 1000) {
    dataRateBps = (bytesReceivedThisSecond * 1000) / elapsed;
    bytesReceivedThisSecond = 0;
    lastRateCalculation = millis();
  }

  if (dispMode == 0) {
    // Écran 1 : Infos de la dernière trame reçue
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
  } 
  else {
    // Écran 2 : Configuration radio et statistiques de flux
    u8g2->drawStr(0, 30, "LoRa @ 869.525 MHz");
    u8g2->drawStr(0, 44, "SF: 8  |  BW: 250 kHz");
    
    snprintf(buf, sizeof(buf), "Trackers: %d  |  %d B/s", activeTrackersCount, dataRateBps);
    u8g2->drawStr(0, 58, buf);
  }

  u8g2->sendBuffer();
}

size_t RadioReceive(U8G2_SSD1306_128X64_NONAME_F_HW_I2C* u8g2, SX1276 *radio, uint8_t* byteArr, size_t maxLen){
  
  if (receivedFlag) {
    // disable the interrupt service routine while
    // processing the data
    enableInterrupt = false;

    // reset flag
    receivedFlag = false;

    // Obtenir la taille réelle du paquet reçu
    size_t length = radio->getPacketLength();
    if (length > maxLen) {
      length = maxLen;
    }

    int state = radio->readData(byteArr, length);

    if (state == RADIOLIB_ERR_NONE) {
      rxCount++;
      if (errCount == 0) {
        snprintf(dispStatus, sizeof(dispStatus), "RX: %d", rxCount);
      } else {
        snprintf(dispStatus, sizeof(dispStatus), "RX:%d E:%d", rxCount, errCount);
      }
      
      // Décode le SSID et l'APID de la trame
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
        
        // Enregistrer l'émetteur comme actif
        if (!seenTrackers[ssid_num]) {
          seenTrackers[ssid_num] = true;
          activeTrackersCount++;
        }
      } else {
        strcpy(dispSsidApid, "Invalid frame (<3B)");
      }
      
      dispRssi = radio->getRSSI();
      dispSnr = radio->getSNR();
      dispHasFrame = true;

      // Actualiser le débit et le temps de réception
      bytesReceivedThisSecond += length;
      lastPacketTime = millis();
      hasReceivedAny = true;

      updateDisplay(u8g2, radio);

      RadioStartListen(radio);
      return length;
    }
    else if (state == RADIOLIB_ERR_CRC_MISMATCH) {
      errCount++;
      snprintf(dispStatus, sizeof(dispStatus), "RX:%d E:%d", rxCount, errCount);
      dispRssi = radio->getRSSI();
      dispSnr = radio->getSNR();
      
      updateDisplay(u8g2, radio);

      RadioStartListen(radio);
      return 0;
    } 
    else {
      errCount++;
      snprintf(dispStatus, sizeof(dispStatus), "RX:%d E:%d", rxCount, errCount);
      dispRssi = radio->getRSSI();
      dispSnr = radio->getSNR();
      
      updateDisplay(u8g2, radio);

      RadioStartListen(radio);
      return 0;
    }
  }
  else{
    // no interrupt
    return 0;
  }
}

void RadioStartListen(SX1276 *radio){
  // put module back to listen mode
  radio->startReceive();
  // we're ready to receive more packets,
  // enable interrupt service routine
  enableInterrupt = true;
}