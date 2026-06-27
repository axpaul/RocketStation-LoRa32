/**
 * @file radio.cpp
 * @brief Contrôle du module radio SX1276 et gestion de l'affichage OLED dynamique.
 * @author Paul Miailhe
 * @date 14/06/2023
 */

#include "header.h"

// Variables d'état d'interruption et d'horloge
volatile bool enableInterrupt = true;
ESP32Time rtc;

// Descripteurs globaux FreeRTOS définis pour la synchronisation inter-cœur
SemaphoreHandle_t rxSemaphore = NULL;
SemaphoreHandle_t radioMutex = NULL;
QueueHandle_t rxQueue = NULL;

// Métadonnées de la dernière trame pour l'affichage OLED
char dispStatus[32] = "RX:0";
static uint32_t rxCount = 0;
static uint32_t errCount = 0;
char dispSsidApid[32] = "No Frame";
float dispRssi = 0.0;
float dispSnr = 0.0;
bool dispHasFrame = false;
bool displayNeedsUpdate = false;

// Variables de statistiques globales pour l'OLED
unsigned long lastTrackerPacketTime[256] = {0};
uint32_t bytesReceivedThisSecond = 0;

// Spinlock pour la protection des variables d'affichage partagées entre cœurs
portMUX_TYPE dispMux = portMUX_INITIALIZER_UNLOCKED;

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
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (rxSemaphore != NULL) {
        xSemaphoreGiveFromISR(rxSemaphore, &xHigherPriorityTaskWoken);
        if (xHigherPriorityTaskWoken == pdTRUE) {
            portYIELD_FROM_ISR();
        }
    }
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
 * @brief Extrait la charge utile brute et les paramètres physiques d'une trame LoRa reçue.
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
size_t RadioReceive(SX1276 *radio, uint8_t* byteArr, size_t maxLen) {
  // Désactiver les interruptions le temps de traiter le paquet
  enableInterrupt = false;

  // Obtenir la taille du paquet reçu
  size_t length = radio->getPacketLength();
  if (length > maxLen) {
    length = maxLen;
  }

  int state = radio->readData(byteArr, length);

  if (state == RADIOLIB_ERR_NONE) {
    // Si le CRC matériel est désactivé (Option B), on vérifie le CRC logiciel avant de valider la trame
    if (!activeConfig.crcEnable) {
      if (length < 5) {
        errCount++;
        taskENTER_CRITICAL(&dispMux);
        snprintf(dispStatus, sizeof(dispStatus), "RX:%d E:%d", rxCount % 1000, errCount % 1000);
        strcpy(dispSsidApid, "Invalid size (<5B)");
        displayNeedsUpdate = true;
        taskEXIT_CRITICAL(&dispMux);
        RadioStartListen(radio);
        return 0;
      }

      uint16_t receivedCrc = byteArr[length - 2] | (byteArr[length - 1] << 8);
      uint16_t calculatedCrc = calculate_crc16(byteArr, length - 2);

      if (calculatedCrc != receivedCrc) {
        errCount++;
        taskENTER_CRITICAL(&dispMux);
        snprintf(dispStatus, sizeof(dispStatus), "RX:%d E:%d", rxCount % 1000, errCount % 1000);
        strcpy(dispSsidApid, "CRC Error (Soft)");
        displayNeedsUpdate = true;
        taskEXIT_CRITICAL(&dispMux);
        RadioStartListen(radio);
        return 0;
      }

      // Retirer les 2 octets de CRC
      length -= 2;
    }

    rxCount++;
    // Formatage compact sans espace et limitation à 3 chiffres (0-999) pour conserver de l'espace
    uint32_t dispRx = rxCount % 1000;
    uint32_t dispErr = errCount % 1000;
    taskENTER_CRITICAL(&dispMux);
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

    displayNeedsUpdate = true;
    taskEXIT_CRITICAL(&dispMux);
    RadioStartListen(radio);
    return length;
  } else if (state == RADIOLIB_ERR_CRC_MISMATCH) {
    errCount++;
    taskENTER_CRITICAL(&dispMux);
    snprintf(dispStatus, sizeof(dispStatus), "RX:%d E:%d", rxCount % 1000, errCount % 1000);
    dispRssi = radio->getRSSI();
    dispSnr = radio->getSNR();
    
    displayNeedsUpdate = true;
    taskEXIT_CRITICAL(&dispMux);
    RadioStartListen(radio);
    return 0;
  } else {
    errCount++;
    taskENTER_CRITICAL(&dispMux);
    snprintf(dispStatus, sizeof(dispStatus), "RX:%d E:%d", rxCount % 1000, errCount % 1000);
    dispRssi = radio->getRSSI();
    dispSnr = radio->getSNR();
    
    displayNeedsUpdate = true;
    taskEXIT_CRITICAL(&dispMux);
    RadioStartListen(radio);
    return 0;
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