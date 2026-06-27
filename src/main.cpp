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
    NULL,                   /* Descripteur de tâche */
    1                       /* Épinglé sur le Cœur 1 */
  );

  // Tâche de traitement E/S (SD, BT, Série) : Priorité basse, sur le cœur 0
  xTaskCreatePinnedToCore(
    vIOProcessingTask,      /* Fonction de la tâche */
    "IOProcessingTask",     /* Nom de la tâche */
    8192,                   /* Stack large (8 Ko) indispensable pour pile Bluetooth */
    NULL,                   /* Paramètre d'entrée */
    1,                      /* Priorité normale */
    NULL,                   /* Descripteur de tâche */
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
  // Scruter et traiter les commandes AT provenant de l'USB et du Bluetooth
  checkSerialCommands(&radio);

  // Mettre à jour l'horloge ou le contenu de l'écran de manière asynchrone non bloquante
  static unsigned long lastUpdate = 0;
  static unsigned long lastDisplayRefresh = 0;
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

  // Laisser du temps aux tâches FreeRTOS de priorité égale ou inférieure
  delay(10);
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
#endif

/**
 * @brief Écoute et accumule les commandes série sur l'USB et le Bluetooth.
 * @param radio Pointeur vers le module radio SX1276.
 */
void checkSerialCommands(SX1276 *radio) {
  static char serialBuf[64];
  static size_t serialIdx = 0;
  
  // Lecture des commandes sur le port USB
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialIdx > 0) {
        serialBuf[serialIdx] = '\0';
        handleConfigCommand(serialBuf, Serial, radio);
        serialIdx = 0;
      }
    } else if (serialIdx < sizeof(serialBuf) - 1) {
      serialBuf[serialIdx++] = c;
    }
  }

#if ENABLE_BLUETOOTH
  static char btBuf[64];
  static size_t btIdx = 0;
  
  // Lecture des commandes sur le port Bluetooth SPP
  while (SerialBT.available() > 0) {
    char c = SerialBT.read();
    if (c == '\n' || c == '\r') {
      if (btIdx > 0) {
        btBuf[btIdx] = '\0';
        handleConfigCommand(btBuf, SerialBT, radio);
        btIdx = 0;
      }
    } else if (btIdx < sizeof(btBuf) - 1) {
      btBuf[btIdx++] = c;
    }
  }
#endif
}

/**
 * @brief Analyse, décode et applique les commandes de configuration au format AT.
 * @param cmd Tampon de la ligne de commande reçue.
 * @param responseStream Flux de réponse (Serial ou SerialBT).
 * @param radio Pointeur vers le module radio SX1276.
 * 
 * Commandes supportées :
 * - AT             : Commande de test de liaison. Répond "OK".
 * - AT+FREQ=<mhz>  : Applique la fréquence LoRa à chaud. (Vérifie les limites physiques).
 * - AT+FREQ?       : Renvoie la fréquence active.
 * - AT+SF=<6-12>   : Applique le Spreading Factor LoRa.
 * - AT+SF?         : Renvoie le Spreading Factor actif.
 * - AT+BW=<khz>    : Applique la bande passante LoRa.
 * - AT+BW?         : Renvoie la bande passante active.
 * - AT+CRC=<0|1>   : Active (1) ou désactive (0) le CRC matériel du module radio.
 * - AT+CRC?        : Renvoie l'état du CRC matériel actif (0 ou 1).
 * - AT+TIME=<epoch>: Synchronise l'heure RTC de la station (Epoch Unix).
 * - AT+TIME?       : Renvoie l'époque Unix de l'horloge RTC active.
 * - AT+RSSI?       : Renvoie le RSSI du dernier paquet reçu en dBm.
 * - AT+SNR?        : Renvoie le SNR du dernier paquet reçu en dB.
 * - AT+SIG?        : Renvoie à la fois le RSSI et le SNR du dernier paquet reçu.
 * - AT+CFG         : Affiche un résumé de la configuration (version, freq, sf, bw, crc, etc.) et de l'état.
 * - AT+SAVE        : Sauvegarde définitivement les paramètres radio actifs dans la NVS.
 * - AT+RESET       : Efface la configuration personnalisée de la NVS et redémarre la carte.
 */
void handleConfigCommand(const char* cmd, Stream& responseStream, SX1276 *radio) {
  // Ignorer silencieusement si la commande ne commence pas par "AT"
  if (strncmp(cmd, "AT", 2) != 0) {
    return;
  }

  // AT
  if (strcmp(cmd, "AT") == 0) {
    responseStream.println("OK");
    return;
  }

  // AT+FREQ=<mhz> ou AT+FREQ?
  if (strncmp(cmd, "AT+FREQ=", 8) == 0) {
    float val = atof(cmd + 8);
    if (val >= FREQ_MIN && val <= FREQ_MAX) {
      activeConfig.frequency = val;
      int state = RADIOLIB_ERR_UNKNOWN;
      if (xSemaphoreTake(radioMutex, portMAX_DELAY) == pdTRUE) {
        state = radio->setFrequency(val);
        radio->startReceive();
        xSemaphoreGive(radioMutex);
      }
      if (state == RADIOLIB_ERR_NONE) {
        responseStream.println("OK");
      } else {
        responseStream.printf("ERROR: %d\n", state);
      }
    } else {
      responseStream.printf("ERROR: Out of limits [%.1f - %.1f] MHz\n", FREQ_MIN, FREQ_MAX);
    }
  } else if (strcmp(cmd, "AT+FREQ?") == 0) {
    responseStream.printf("+FREQ: %.3f\n", activeConfig.frequency);
    responseStream.println("OK");
  }

  // AT+TIME=<epoch> ou AT+TIME?
  else if (strncmp(cmd, "AT+TIME=", 8) == 0) {
    uint32_t epoch = strtoul(cmd + 8, NULL, 10);
    rtc.setTime(epoch);
    responseStream.println("OK");
  } else if (strcmp(cmd, "AT+TIME?") == 0) {
    responseStream.printf("+TIME: %lu\n", rtc.getEpoch());
    responseStream.println("OK");
  }

  // AT+RSSI?
  else if (strcmp(cmd, "AT+RSSI?") == 0) {
    responseStream.printf("+RSSI: %.1f\n", dispRssi);
    responseStream.println("OK");
  }

  // AT+SNR?
  else if (strcmp(cmd, "AT+SNR?") == 0) {
    responseStream.printf("+SNR: %.1f\n", dispSnr);
    responseStream.println("OK");
  }

  // AT+SIG?
  else if (strcmp(cmd, "AT+SIG?") == 0) {
    responseStream.printf("+SIG: RSSI=%.1f, SNR=%.1f\n", dispRssi, dispSnr);
    responseStream.println("OK");
  }

  // AT+SF=<6-12> ou AT+SF?
  else if (strncmp(cmd, "AT+SF=", 6) == 0) {
    int val = atoi(cmd + 6);
    if (val >= 6 && val <= 12) {
      activeConfig.spreadingFactor = val;
      int state = RADIOLIB_ERR_UNKNOWN;
      if (xSemaphoreTake(radioMutex, portMAX_DELAY) == pdTRUE) {
        state = radio->setSpreadingFactor(val);
        radio->startReceive();
        xSemaphoreGive(radioMutex);
      }
      if (state == RADIOLIB_ERR_NONE) {
        responseStream.println("OK");
      } else {
        responseStream.printf("ERROR: %d\n", state);
      }
    } else {
      responseStream.println("ERROR: SF must be between 6 and 12");
    }
  } else if (strcmp(cmd, "AT+SF?") == 0) {
    responseStream.printf("+SF: %d\n", activeConfig.spreadingFactor);
    responseStream.println("OK");
  }

  // AT+BW=<khz> ou AT+BW?
  else if (strncmp(cmd, "AT+BW=", 6) == 0) {
    float val = atof(cmd + 6);
    if (val > 0.0f) {
      activeConfig.bandwidth = val;
      int state = RADIOLIB_ERR_UNKNOWN;
      if (xSemaphoreTake(radioMutex, portMAX_DELAY) == pdTRUE) {
        state = radio->setBandwidth(val);
        radio->startReceive();
        xSemaphoreGive(radioMutex);
      }
      if (state == RADIOLIB_ERR_NONE) {
        responseStream.println("OK");
      } else {
        responseStream.printf("ERROR: %d\n", state);
      }
    } else {
      responseStream.println("ERROR: Bandwidth must be greater than 0");
    }
  } else if (strcmp(cmd, "AT+BW?") == 0) {
    responseStream.printf("+BW: %.1f\n", activeConfig.bandwidth);
    responseStream.println("OK");
  }

  // AT+CRC=0 ou AT+CRC=1 ou AT+CRC?
  else if (strncmp(cmd, "AT+CRC=", 7) == 0) {
    int enable = -1;
    int mode = 0; // Default to CCITT
    int numArgs = sscanf(cmd + 7, "%d,%d", &enable, &mode);
    if (numArgs >= 1 && (enable == 0 || enable == 1)) {
      if (numArgs == 2 && mode != 0 && mode != 1) {
        responseStream.println("ERROR: CRC mode must be 0 (CCITT) or 1 (IBM)");
      } else {
        activeConfig.crcEnable = (enable == 1);
        if (numArgs == 2) {
          activeConfig.crcMode = (mode == 1);
        }
        int state = RADIOLIB_ERR_UNKNOWN;
        if (xSemaphoreTake(radioMutex, portMAX_DELAY) == pdTRUE) {
          state = radio->setCRC(activeConfig.crcEnable, activeConfig.crcMode);
          radio->startReceive();
          xSemaphoreGive(radioMutex);
        }
        if (state == RADIOLIB_ERR_NONE) {
          responseStream.println("OK");
        } else {
          responseStream.printf("ERROR: %d\n", state);
        }
      }
    } else {
      responseStream.println("ERROR: CRC must be 0 (Disabled) or 1 (Enabled) with optional mode: AT+CRC=<0|1>[,0|1]");
    }
  } else if (strcmp(cmd, "AT+CRC?") == 0) {
    responseStream.printf("+CRC: %d,%d\n", activeConfig.crcEnable ? 1 : 0, activeConfig.crcMode ? 1 : 0);
    responseStream.println("OK");
  }

  // AT+CFG ou AT+STATUS
  else if (strcmp(cmd, "AT+CFG") == 0 || strcmp(cmd, "AT+STATUS") == 0) {
    responseStream.println("--- Nectar RxStation Configuration ---");
    responseStream.printf("Firmware Version  : %s\n", FW_VERSION);
    responseStream.printf("Native Band Limit : %d MHz\n", LORA_BAND_NATIVE);
    responseStream.printf("Allowed Range     : [%.1f - %.1f] MHz\n", FREQ_MIN, FREQ_MAX);
    responseStream.printf("Frequency (Active): %.3f MHz\n", activeConfig.frequency);
    responseStream.printf("Spreading Factor  : %d\n", activeConfig.spreadingFactor);
    responseStream.printf("Bandwidth         : %.1f kHz\n", activeConfig.bandwidth);
    if (activeConfig.crcEnable) {
      responseStream.printf("Hardware CRC      : ON (%s)\n", activeConfig.crcMode ? "IBM" : "CCITT");
    } else {
      responseStream.println("Hardware CRC      : OFF");
    }
    responseStream.printf("SD Card Connected : %s\n", *SDCard ? "Yes" : "No");
#if ENABLE_BLUETOOTH
    responseStream.printf("Bluetooth Client  : %s\n", SerialBT.connected() ? "Connected" : "Disconnected");
#endif
    responseStream.println("-----------------------------------");
    responseStream.println("OK");
  }

  // AT+SAVE
  else if (strcmp(cmd, "AT+SAVE") == 0) {
    saveLoRaConfig();
    responseStream.println("OK");
  }

  // AT+RESET
  else if (strcmp(cmd, "AT+RESET") == 0) {
    resetLoRaConfig();
    responseStream.println("OK");
    delay(1000);
    ESP.restart();
  }

  // AT+HELP ou AT?
  else if (strcmp(cmd, "AT+HELP") == 0 || strcmp(cmd, "AT?") == 0) {
    responseStream.println("--- Available AT Commands ---");
    responseStream.println("AT             : Test link");
    responseStream.println("AT+HELP or AT? : Print this help menu");
    responseStream.println("AT+INFO or AT+VER : Print station identification");
    responseStream.println("AT+FREQ=<mhz>  : Set active frequency (e.g. 869.525)");
    responseStream.println("AT+FREQ?       : Get active frequency");
    responseStream.println("AT+SF=<6-12>   : Set active Spreading Factor");
    responseStream.println("AT+SF?         : Get active Spreading Factor");
    responseStream.println("AT+BW=<khz>    : Set active Bandwidth");
    responseStream.println("AT+BW?         : Get active Bandwidth");
    responseStream.println("AT+CRC=<0|1>   : Set Hardware CRC (0=OFF, 1=ON)");
    responseStream.println("AT+CRC?        : Get Hardware CRC status");
    responseStream.println("AT+TIME=<epoch>: Set RTC time (Unix Epoch)");
    responseStream.println("AT+TIME?       : Get RTC time (Unix Epoch)");
    responseStream.println("AT+RSSI?       : Get last received packet RSSI");
    responseStream.println("AT+SNR?        : Get last received packet SNR");
    responseStream.println("AT+SIG?        : Get RSSI and SNR of last packet");
    responseStream.println("AT+CFG         : Get detailed configuration");
    responseStream.println("AT+LIST        : List CSV log files on SD card");
    responseStream.println("AT+DUMP=<file> : Dump CSV log file contents");
    responseStream.println("AT+SAVE        : Save current config to NVS");
    responseStream.println("AT+RESET       : Reset config to factory & reboot");
    responseStream.println("-----------------------------");
    responseStream.println("OK");
  }

  // AT+INFO ou AT+VER
  else if (strcmp(cmd, "AT+INFO") == 0 || strcmp(cmd, "AT+VER") == 0) {
    responseStream.printf("+INFO: NECTAR RX STATION,FW=%s,Band=%d\n", FW_VERSION, LORA_BAND_NATIVE);
    responseStream.println("OK");
  }

  // AT+LIST
  else if (strcmp(cmd, "AT+LIST") == 0) {
    if (!*SDCard) {
      responseStream.println("ERROR: SD card offline");
      return;
    }
    File root = SD.open("/");
    if (!root) {
      responseStream.println("ERROR: Failed to open root");
      return;
    }
    File file = root.openNextFile();
    while (file) {
      if (!file.isDirectory()) {
        const char* name = file.name();
        if (strstr(name, "log") != NULL && strstr(name, ".csv") != NULL) {
          responseStream.printf("+LIST: %s,%d\n", name, file.size());
        }
      }
      file = root.openNextFile();
    }
    root.close();
    responseStream.println("OK");
  }

  // AT+DUMP=<file>
  else if (strncmp(cmd, "AT+DUMP=", 8) == 0) {
    if (!*SDCard) {
      responseStream.println("ERROR: SD card offline");
      return;
    }
    const char* filename = cmd + 8;
    char fullPath[64];
    if (filename[0] != '/') {
      snprintf(fullPath, sizeof(fullPath), "/%s", filename);
    } else {
      snprintf(fullPath, sizeof(fullPath), "%s", filename);
    }
    if (!SD.exists(fullPath)) {
      responseStream.printf("ERROR: File %s not found\n", fullPath);
      return;
    }
    File file = SD.open(fullPath, FILE_READ);
    if (!file) {
      responseStream.println("ERROR: Failed to open file");
      return;
    }
    responseStream.println("+DUMP: START");
    uint8_t buf[64];
    while (file.available()) {
      int len = file.read(buf, sizeof(buf));
      responseStream.write(buf, len);
    }
    file.close();
    responseStream.println(); // ensure clean final newline
    responseStream.println("+DUMP: END");
    responseStream.println("OK");
  }

  // Cas d'erreur : commande non reconnue
  else {
    responseStream.printf("ERROR: Unknown AT command '%s'\n", cmd);
  }
}
