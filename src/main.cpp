/**
 * @file main.cpp
 * @brief Point d'entrée principal du récepteur au sol Nectar-RxStation-LoRa32.
 * @author Paul Miailhe
 * @date 14/06/2023
 * 
 * Orchestre l'initialisation matérielle (SPI, I2C, OLED, Carte SD, Radio SX1276),
 * traite les interruptions LoRa, écrit les trames sur carte SD au format CSV
 * et les transmet en USB/Bluetooth au protocole binaire NectarMC.
 * Gère également la configuration dynamique par commandes AT.
 */

#include "header.h"

// ============================================================================
// Variables et instances globales statiques
// ============================================================================
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2Static(U8G2_R0, U8X8_PIN_NONE);
SPIClass SDSPIStatic(HSPI);
bool SDCardStatic = false;
uint8_t byteArrStatic[MAX_FRAME_SIZE];

U8G2_SSD1306_128X64_NONAME_F_HW_I2C *u8g2 = &u8g2Static;
SPIClass *SDSPI = &SDSPIStatic;
bool *SDCard = &SDCardStatic;
size_t receivedLen = 0;
char logFileName[32] = "/log.csv";
uint8_t *byteArr = byteArrStatic;
SX1276 radio = new Module(RADIO_CS_PIN, RADIO_DIO0_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);
LoRaConfig activeConfig;

// Descripteurs de tâches FreeRTOS pour surveillance
TaskHandle_t xRadioRxTaskHandle = NULL;
TaskHandle_t xIOProcessingTaskHandle = NULL;
TaskHandle_t xPeripheralTaskHandle = NULL;

/**
 * @brief Initialisation matérielle et logicielle du système.
 */
#ifndef UNIT_TEST
void setup() {
  Serial.begin(115200);
  delay(100); // Laisse le temps au moniteur série de s'ouvrir

  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);

  // Charger la configuration LoRa depuis la NVS (Flash non-volatile)
  loadLoRaConfig();

#if ENABLE_BLUETOOTH
  // Initialiser la pile Bluetooth classique avec un nom dérivé de l'adresse MAC
  char btName[32];
  snprintf(btName, sizeof(btName), "Nectar-RxStation-%02X%02X", mac[4], mac[5]);
  SerialBT.begin(btName);
  Serial.printf("[SYSTEM] Bluetooth Serial started: '%s'\n", btName);
#endif

  // Logs système sur le démarrage
  Serial.println("\n=========================================");
  Serial.printf("[SYSTEM] Firmware Version: %s\n", FW_VERSION);
  Serial.printf("[SYSTEM] Station ID (MAC): %02X:%02X:%02X:%02X:%02X:%02X\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.printf("[SYSTEM] Chip Model:      %s (Rev %d)\n", ESP.getChipModel(), ESP.getChipRevision());
  Serial.printf("[SYSTEM] Flash Size:      %.1f MB\n", ESP.getFlashChipSize() / (1024.0 * 1024.0));
  Serial.println("=========================================");

  // Configuration des bus de communication et broches E/S
  SPI.begin(RADIO_SCLK_PIN, RADIO_MISO_PIN, RADIO_MOSI_PIN);
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000); // Activer Fast I2C (400 kHz)
  pinMode(BOARD_LED, OUTPUT);
  digitalWrite(BOARD_LED, LED_ON);
  pinMode(BATTERY_PIN, INPUT);

  *SDCard = false;
  
  // Affiche l'écran de chargement avec animation
  ScreenText(u8g2);

  // Initialisation et détection de la carte SD
  SDCardDetection(u8g2, SDSPI, SDCard);
  if (*SDCard) {
    int fileIndex = 0;
    // Recherche d'un index de fichier libre /log_X.csv pour éviter d'écraser les anciens enregistrements
    do {
      snprintf(logFileName, sizeof(logFileName), "/log_%d.csv", fileIndex++);
    } while (SD.exists(logFileName));

    // Création du fichier log et écriture de l'en-tête CSV
    File log = SD.open(logFileName, FILE_WRITE);
    if (log) {
      log.println("Index,Timestamp,Length,RSSI,SNR,SSID,APID,RawFrame");
      log.close();
    }
    checkSDCardSpace(u8g2);
  }

  // Configuration et démarrage initial du module radio LoRa
  RadioSettings(u8g2, &radio);

  // Création des objets de synchronisation FreeRTOS
  rxSemaphore = xSemaphoreCreateBinary();
  radioMutex = xSemaphoreCreateMutex();
  rxQueue = xQueueCreate(10, sizeof(LoRaPacket));

  if (rxSemaphore == NULL || radioMutex == NULL || rxQueue == NULL) {
    Serial.println("[SYSTEM] FAILED to create FreeRTOS sync objects!");
    while (true) { delay(100); }
  }

  // Création des tâches FreeRTOS et liaison aux cœurs de l'ESP32
  // Tâche de réception LoRa : Haute priorité, sur le cœur 1 (cœur par défaut de la stack RF)
  xTaskCreatePinnedToCore(
    vRadioRxTask,           /* Fonction de la tâche */
    "RadioRxTask",          /* Nom de la tâche */
    4096,                   /* Taille de pile (Stack size in bytes) */
    NULL,                   /* Paramètre d'entrée */
    3,                      /* Priorité élevée pour réagir à l'interruption */
    &xRadioRxTaskHandle,    /* Descripteur de tâche */
    1                       /* Épinglé sur le Cœur 1 */
  );

  // Tâche de traitement E/S (SD, BT, Série) : Priorité basse, sur le cœur 0
  xTaskCreatePinnedToCore(
    vIOProcessingTask,      /* Fonction de la tâche */
    "IOProcessingTask",     /* Nom de la tâche */
    8192,                   /* Stack large (8 Ko) indispensable pour pile Bluetooth */
    NULL,                   /* Paramètre d'entrée */
    1,                      /* Priorité normale */
    &xIOProcessingTaskHandle, /* Descripteur de tâche */
    0                       /* Épinglé sur le Cœur 0 */
  );

  // Tâche périphérique (OLED, AT, Batterie) : Priorité normale, sur le cœur 0
  xTaskCreatePinnedToCore(
    vPeripheralTask,        /* Fonction de la tâche */
    "PeripheralTask",       /* Nom de la tâche */
    4096,                   /* Taille de pile (4 Ko) */
    NULL,                   /* Paramètre d'entrée */
    1,                      /* Priorité normale */
    &xPeripheralTaskHandle, /* Descripteur de tâche */
    0                       /* Épinglé sur le Cœur 0 */
  );

  Serial.println("[SYSTEM] FreeRTOS Multitasking initialized successfully!");

  // Premier affichage immédiat des données et du statut de l'écran
  updateDisplay(u8g2, &radio);
}

/**
 * @brief Boucle d'exécution principale (Orchestrateur).
 * Gère l'OLED, le diagnostic AT et les mesures secondaires.
 */
void loop() {
  // Le Cœur 1 ne fait rien d'autre que d'attendre la radio.
  // loop() est mis en sommeil perpétuel.
  vTaskDelay(portMAX_DELAY);
}

/**
 * @brief Tâche de réception radio haute priorité (Cœur 1).
 * Attend le sémaphore émis par l'ISR de la radio, extrait le paquet sous Mutex,
 * puis pousse la structure de paquet dans la file d'attente.
 */
void vRadioRxTask(void *pvParameters) {
  (void)pvParameters;
  uint8_t rxBuffer[MAX_FRAME_SIZE];

  for (;;) {
    // Attendre le signal d'interruption LoRa (DIO0)
    if (xSemaphoreTake(rxSemaphore, portMAX_DELAY) == pdTRUE) {
      size_t len = 0;
      int8_t rssi_val = 0;
      int8_t snr_val = 0;

      // Prendre le mutex radio pour accès exclusif au module SX1276 sur le bus SPI
      if (xSemaphoreTake(radioMutex, portMAX_DELAY) == pdTRUE) {
        len = RadioReceive(u8g2, &radio, rxBuffer, MAX_FRAME_SIZE);
        if (len > 0) {
          rssi_val = (int8_t)dispRssi;
          snr_val = (int8_t)(dispSnr * 4.0f);
        }
        xSemaphoreGive(radioMutex);
      }

      // Si le paquet est valide (trame minimale de 3 octets : SSID, APID, Type)
      if (len >= 3) {
        LoRaPacket packet;
        memcpy(packet.data, rxBuffer, len);
        packet.length = len;
        packet.rssi = rssi_val;
        packet.snr = snr_val;
        packet.timestamp = rtc.getEpoch();
        packet.ssid_num = rxBuffer[0];
        packet.apid = rxBuffer[1];
        packet.ssid_type = rxBuffer[2];

        // Envoyer le paquet dans la file d'attente pour traitement par le cœur 0
        if (xQueueSend(rxQueue, &packet, 0) != pdTRUE) {
          Serial.println("[SYSTEM] rxQueue is FULL! Packet dropped.");
        }
      }
    }
  }
}

/**
 * @brief Tâche E/S de traitement de paquets (Cœur 0).
 * Attend les paquets dans la file d'attente, les formate, les envoie
 * sur port série/Bluetooth et les enregistre sur carte SD (bus SPI HSPI indépendant).
 */
void vIOProcessingTask(void *pvParameters) {
  (void)pvParameters;
  LoRaPacket packet;

  for (;;) {
    // Bloquer jusqu'à ce qu'un paquet LoRa soit disponible
    if (xQueueReceive(rxQueue, &packet, portMAX_DELAY) == pdTRUE) {
      
      // 1. Transmission binaire NectarMC (USB et Bluetooth)
      const uint8_t* payload = &packet.data[3];
      size_t payload_len = packet.length - 3;
      sendNectarFrame(packet.ssid_type, packet.ssid_num, packet.apid, payload, payload_len, packet.rssi, packet.snr);

      // 2. Enregistrement sur la carte SD si elle est disponible
      if (*SDCard) {
        const char* ssid_prefix = "OTHER";
        if (packet.ssid_type == 0) ssid_prefix = "FX";
        else if (packet.ssid_type == 1) ssid_prefix = "MF";
        else if (packet.ssid_type == 2) ssid_prefix = "BALLOON";
        else if (packet.ssid_type == 3) ssid_prefix = "OTHER";

        char ssid_str[32];
        snprintf(ssid_str, sizeof(ssid_str), "%s%d", ssid_prefix, packet.ssid_num);

        writeFrameToFile(logFileName, packet.data, packet.length, (float)packet.rssi, (float)packet.snr / 4.0f, ssid_str, packet.apid);
      }
    }
  }
}

/**
 * @brief Tâche de gestion des périphériques (Cœur 0).
 * Scrute les commandes AT, rafraîchit l'OLED périodiquement.
 */
void vPeripheralTask(void *pvParameters) {
  (void)pvParameters;
  
  static unsigned long lastUpdate = 0;
  static unsigned long lastDisplayRefresh = 0;
  
  for (;;) {
    // Scruter et traiter les commandes AT provenant de l'USB et du Bluetooth
    checkSerialCommands(&radio);

    // Mettre à jour l'horloge ou le contenu de l'écran de manière asynchrone
    if (millis() - lastUpdate >= 1000) {
      lastUpdate = millis();
      lastDisplayRefresh = millis();
      updateDisplay(u8g2, &radio);
      displayNeedsUpdate = false;
    } else if (displayNeedsUpdate && (millis() - lastDisplayRefresh >= 250)) {
      lastDisplayRefresh = millis();
      updateDisplay(u8g2, &radio);
      displayNeedsUpdate = false;
    }

    vTaskDelay(pdMS_TO_TICKS(50)); // Attente de 50 ms pour relâcher le CPU
  }
}
#endif
