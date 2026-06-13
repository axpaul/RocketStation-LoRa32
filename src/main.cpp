/**
 * @file main.cpp
 * @brief Point d'entrée principal du récepteur au sol RocketStation-LoRa32.
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

  // Premier affichage immédiat des données et du statut de l'écran
  updateDisplay(u8g2, &radio);
}

/**
 * @brief Boucle d'exécution principale (Orchestrateur).
 */
void loop() {
  // Scruter et traiter les commandes AT provenant de l'USB et du Bluetooth
  checkSerialCommands(&radio);
  
  // Lecture de la trame radio LoRa reçue par le module
  receivedLen = RadioReceive(u8g2, &radio, byteArr, MAX_FRAME_SIZE);
  
  if (receivedLen > 0) {
    // La trame minimale valide doit contenir au moins 3 octets : [SSID_NUM] [APID] [SSID_TYPE]
    if (receivedLen >= 3) {
      uint8_t ssid_num  = byteArr[0];
      uint8_t apid      = byteArr[1];
      uint8_t ssid_type = byteArr[2];
      
      const uint8_t* payload = &byteArr[3];
      size_t payload_len     = receivedLen - 3;
      
      // Extraction des métriques de la liaison radio
      int8_t rssi_val = (int8_t)radio.getRSSI();
      int8_t snr_val  = (int8_t)radio.getSNR();
      
      // Transmission de la trame binaire NectarMC (USB et Bluetooth)
      sendNectarFrame(ssid_type, ssid_num, apid, payload, payload_len, rssi_val, snr_val);
      
      // Enregistrement de la trame sur carte SD si elle est disponible
      if (*SDCard) {
        const char* ssid_prefix = "OTHER";
        if (ssid_type == 0) ssid_prefix = "FX";
        else if (ssid_type == 1) ssid_prefix = "MF";
        else if (ssid_type == 2) ssid_prefix = "BALLOON";
        else if (ssid_type == 3) ssid_prefix = "OTHER";

        char ssid_str[32];
        snprintf(ssid_str, sizeof(ssid_str), "%s%d", ssid_prefix, ssid_num);

        writeFrameToFile(logFileName, byteArr, receivedLen, radio.getRSSI(), radio.getSNR(), ssid_str, apid);  
      }
    }
  }

  // Mettre à jour l'horloge en temps réel sur l'écran toutes les secondes
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate >= 1000) {
    lastUpdate = millis();
    updateDisplay(u8g2, &radio);
  }
}

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
 * - AT+CFG         : Affiche un résumé de la configuration et de l'état matériel.
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
      int state = radio->setFrequency(val);
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

  // AT+SF=<6-12> ou AT+SF?
  else if (strncmp(cmd, "AT+SF=", 6) == 0) {
    int val = atoi(cmd + 6);
    if (val >= 6 && val <= 12) {
      activeConfig.spreadingFactor = val;
      int state = radio->setSpreadingFactor(val);
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
      int state = radio->setBandwidth(val);
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

  // AT+CFG ou AT+STATUS
  else if (strcmp(cmd, "AT+CFG") == 0 || strcmp(cmd, "AT+STATUS") == 0) {
    responseStream.println("--- RocketStation Configuration ---");
    responseStream.printf("Firmware Version  : %s\n", FW_VERSION);
    responseStream.printf("Native Band Limit : %d MHz\n", LORA_BAND_NATIVE);
    responseStream.printf("Allowed Range     : [%.1f - %.1f] MHz\n", FREQ_MIN, FREQ_MAX);
    responseStream.printf("Frequency (Active): %.3f MHz\n", activeConfig.frequency);
    responseStream.printf("Spreading Factor  : %d\n", activeConfig.spreadingFactor);
    responseStream.printf("Bandwidth         : %.1f kHz\n", activeConfig.bandwidth);
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

  // Cas d'erreur : commande non reconnue
  else {
    responseStream.printf("ERROR: Unknown AT command '%s'\n", cmd);
  }
}
