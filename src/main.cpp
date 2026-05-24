#include "header.h"

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

void setup() 
{
  Serial.begin(115200);
  delay(100); // Give serial monitor time to connect

  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);

  loadLoRaConfig();

#if ENABLE_BLUETOOTH
  char btName[32];
  snprintf(btName, sizeof(btName), "Nectar-RxStation-%02X%02X", mac[4], mac[5]);
  SerialBT.begin(btName);
  Serial.printf("[SYSTEM] Bluetooth Serial started: '%s'\n", btName);
#endif

  Serial.println("\n=========================================");
  Serial.printf("[SYSTEM] Firmware Version: %s\n", FW_VERSION);
  Serial.printf("[SYSTEM] Station ID (MAC): %02X:%02X:%02X:%02X:%02X:%02X\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.printf("[SYSTEM] Chip Model:      %s (Rev %d)\n", ESP.getChipModel(), ESP.getChipRevision());
  Serial.printf("[SYSTEM] Flash Size:      %.1f MB\n", ESP.getFlashChipSize() / (1024.0 * 1024.0));
  Serial.println("=========================================");

  SPI.begin(RADIO_SCLK_PIN, RADIO_MISO_PIN, RADIO_MOSI_PIN);
  Wire.begin(I2C_SDA, I2C_SCL);
  pinMode(BOARD_LED, OUTPUT);
  digitalWrite(BOARD_LED, LED_ON);
  pinMode(BATTERY_PIN, INPUT);

  *SDCard = false;
  ScreenText(u8g2);

  //SD card:
  SDCardDetection(u8g2, SDSPI, SDCard);
  if (*SDCard){
    int fileIndex = 0;
    do {
      snprintf(logFileName, sizeof(logFileName), "/log_%d.csv", fileIndex++);
    } while (SD.exists(logFileName));

    File log = SD.open(logFileName, FILE_WRITE);
    if (log) {
      log.println("Timestamp,RSSI,SNR,SSID,APID,RawFrame");
      log.close();
    }
    checkSDCardSpace(u8g2);
  }
  //Radio:
  RadioSettings(u8g2, &radio);

  // Premier affichage immédiat
  updateDisplay(u8g2, &radio);
}

void loop() {
  checkSerialCommands(&radio);
  
  // put your main code here, to run repeatedly:
  receivedLen = RadioReceive(u8g2, &radio, byteArr, MAX_FRAME_SIZE);
  
  if (receivedLen > 0){
    // La trame minimale doit contenir au moins 3 octets : [SSID_NUM] [APID] [SSID_TYPE]
    if (receivedLen >= 3) {
      uint8_t ssid_num  = byteArr[0];
      uint8_t apid      = byteArr[1];
      uint8_t ssid_type = byteArr[2];
      
      const uint8_t* payload = &byteArr[3];
      size_t payload_len     = receivedLen - 3;
      
      sendNectarFrame(ssid_type, ssid_num, apid, payload, payload_len);
      
      if (*SDCard){
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

// Cette fonction écoute et accumule les caractères reçus sur les ports série (USB et Bluetooth)
// pour assembler des lignes complètes de commandes.
void checkSerialCommands(SX1276 *radio) {
  // Les variables 'static' conservent leur état entre chaque appel de loop()
  static char serialBuf[64];
  static size_t serialIdx = 0;
  
  // Lecture du port série USB
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') { // Fin de ligne détectée : on traite la commande
      if (serialIdx > 0) {
        serialBuf[serialIdx] = '\0'; // Ajout du caractère de fin de chaîne
        handleConfigCommand(serialBuf, Serial, radio); // Traitement et réponse sur le port USB
        serialIdx = 0; // Réinitialisation de l'index du buffer
      }
    } else if (serialIdx < sizeof(serialBuf) - 1) {
      serialBuf[serialIdx++] = c; // Accumulation du caractère
    }
  }

#if ENABLE_BLUETOOTH
  // Même logique pour la connexion Bluetooth Serial
  static char btBuf[64];
  static size_t btIdx = 0;
  
  while (SerialBT.available() > 0) {
    char c = SerialBT.read();
    if (c == '\n' || c == '\r') { // Fin de ligne détectée : on traite la commande
      if (btIdx > 0) {
        btBuf[btIdx] = '\0'; // Fin de chaîne
        handleConfigCommand(btBuf, SerialBT, radio); // Traitement et réponse sur la liaison Bluetooth
        btIdx = 0; // Réinitialisation de l'index
      }
    } else if (btIdx < sizeof(btBuf) - 1) {
      btBuf[btIdx++] = c; // Accumulation
    }
  }
#endif
}

// Cette fonction décode et exécute la commande reçue, puis renvoie le résultat 
// sur le flux de communication concerné (USB Serial ou Bluetooth SerialBT).
void handleConfigCommand(const char* cmd, Stream& responseStream, SX1276 *radio) {
  
  // Commande "SET FREQ <valeur>" : Modifie la fréquence radio active
  if (strncmp(cmd, "SET FREQ ", 9) == 0) {
    float val = atof(cmd + 9); // Récupère la valeur numérique en virgule flottante
    // Vérification des limites physiques de la carte (868 ou 433) définies dans header.h
    if (val >= FREQ_MIN && val <= FREQ_MAX) {
      activeConfig.frequency = val;
      int state = radio->setFrequency(val); // Applique le changement à chaud sur le module SX1276
      if (state == RADIOLIB_ERR_NONE) {
        responseStream.printf("OK: Frequency set to %.3f MHz\n", val);
      } else {
        responseStream.printf("ERROR: Failed to set frequency, code %d\n", state);
      }
    } else {
      // Rejet de la commande si hors limites physiques de l'antenne / de la bande native
      responseStream.printf("ERROR: Frequency %.3f out of native band limits [%.1f - %.1f] MHz\n", val, FREQ_MIN, FREQ_MAX);
    }
  }
  
  // Commande "SET SF <valeur>" : Modifie le Spreading Factor (Facteur d'étalement LoRa)
  else if (strncmp(cmd, "SET SF ", 7) == 0) {
    int val = atoi(cmd + 7); // Récupère la valeur entière
    if (val >= 6 && val <= 12) { // Limite légale standard pour LoRa
      activeConfig.spreadingFactor = val;
      int state = radio->setSpreadingFactor(val); // Applique le changement à chaud
      if (state == RADIOLIB_ERR_NONE) {
        responseStream.printf("OK: Spreading Factor set to %d\n", val);
      } else {
        responseStream.printf("ERROR: Failed to set SF, code %d\n", state);
      }
    } else {
      responseStream.println("ERROR: SF must be between 6 and 12");
    }
  }
  
  // Commande "SET BW <valeur>" : Modifie la Bande Passante LoRa (en kHz)
  else if (strncmp(cmd, "SET BW ", 7) == 0) {
    float val = atof(cmd + 7);
    if (val > 0.0f) {
      activeConfig.bandwidth = val;
      int state = radio->setBandwidth(val); // Applique le changement de bande passante à chaud
      if (state == RADIOLIB_ERR_NONE) {
        responseStream.printf("OK: Bandwidth set to %.1f kHz\n", val);
      } else {
        responseStream.printf("ERROR: Failed to set BW, code %d\n", state);
      }
    } else {
      responseStream.println("ERROR: Bandwidth must be greater than 0");
    }
  }
  
  // Commande "GET CFG" ou "STATUS" : Affiche l'état complet et les limites
  else if (strcmp(cmd, "GET CFG") == 0 || strcmp(cmd, "STATUS") == 0) {
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
  }
  
  // Commande "SAVE" : Enregistre la configuration active dans la mémoire Flash NVS
  else if (strcmp(cmd, "SAVE") == 0) {
    saveLoRaConfig(); // Enregistrement permanent
    responseStream.println("OK: Configuration saved to NVS.");
  }
  
  // Commande "RESET" : Efface la configuration personnalisée NVS et redémarre l'ESP32
  else if (strcmp(cmd, "RESET") == 0) {
    resetLoRaConfig(); // Efface la mémoire
    responseStream.println("OK: NVS config cleared. Rebooting device...");
    delay(1000);
    ESP.restart(); // Redémarrage matériel
  }
  
  // Cas d'erreur pour les commandes non reconnues
  else {
    responseStream.printf("ERROR: Unknown command '%s'\n", cmd);
  }
}
