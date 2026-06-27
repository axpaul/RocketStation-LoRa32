/**
 * @file function.cpp
 * @brief Fonctions utilitaires, gestion de la carte SD, de l'OLED et de la configuration NVS.
 * @version 1.6.0
 * @author Paul Miailhe
 * @date 27/06/2026
 * 
 * Version 1.6.0 : Refactorisation multitâche dual-core (FreeRTOS) et écran OLED séparé.
 */

#include "header.h"
#include <Preferences.h>



/**
 * @brief Détecte, initialise et affiche l'état de la carte SD au démarrage.
 * @param u8g2 Pointeur vers l'écran OLED.
 * @param SDSPI Bus SPI dédié à la carte SD.
 * @param SDCard Variable de retour indiquant si la carte est active.
 * 
 * Configure le bus SPI et démarre le système de fichiers SD.
 * - En cas d'échec : Lance un avertissement visuel clignotant sur l'écran (triangle '⚠').
 * - En cas de réussite : Affiche un logo circulaire de validation '✓' et la taille de la carte.
 */
void SDCardDetection(U8G2_SSD1306_128X64_NONAME_F_HW_I2C* u8g2, SPIClass* SDSPI, bool* SDCard) {
  pinMode(SDCARD_MISO, INPUT_PULLUP);
  SDSPI->begin(SDCARD_SCLK, SDCARD_MISO, SDCARD_MOSI, SDCARD_CS);

  u8g2->setFont(u8g2_font_ncenB08_tr);
  if (!SD.begin(SDCARD_CS, *SDSPI, 20000000)) {
    *SDCard = false;
    Serial.println("[SD] Initialization FAILED!");
    
    // Alerte clignotante en cas d'erreur de carte SD (6 itérations de 300ms = 1.8s)
    for (int i = 0; i < 6; i++) {
      u8g2->clearBuffer();
      if (i % 2 == 0) {
        // Dessin du triangle d'avertissement
        u8g2->drawLine(64, 10, 46, 38);
        u8g2->drawLine(64, 10, 82, 38);
        u8g2->drawLine(46, 38, 82, 38);
        u8g2->drawLine(64, 11, 47, 37);
        u8g2->drawLine(64, 11, 81, 37);
        u8g2->drawLine(47, 37, 81, 37);
        
        // Point d'exclamation au centre
        u8g2->drawBox(63, 18, 2, 10);
        u8g2->drawBox(63, 31, 2, 2);
        
        // Libellé de l'erreur
        int w = u8g2->getStrWidth("SD CARD: FAILED!");
        u8g2->drawStr((128 - w) / 2, 56, "SD CARD: FAILED!");
      }
      u8g2->sendBuffer();
      delay(300);
    }
  }  
  else {
    *SDCard = true;
    uint32_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("[SD] Initialization OK. Size: %.2f GB\n", cardSize / 1024.0);

    // Écran de succès graphique
    u8g2->clearBuffer();
    
    // Cercle de validation
    u8g2->drawCircle(64, 26, 14);
    u8g2->drawCircle(64, 26, 13);
    
    // Symbole '✓' (coche) tracé manuellement
    u8g2->drawLine(55, 25, 61, 31);
    u8g2->drawLine(55, 26, 61, 32);
    u8g2->drawLine(55, 27, 61, 33);
    
    u8g2->drawLine(61, 31, 73, 19);
    u8g2->drawLine(61, 32, 73, 20);
    u8g2->drawLine(61, 33, 73, 21);

    // Affichage textuel de la capacité de stockage détectée
    char buf[64];
    snprintf(buf, sizeof(buf), "SD CARD: OK (%.1f GB)", cardSize / 1024.0);
    int w = u8g2->getStrWidth(buf);
    u8g2->drawStr((128 - w) / 2, 56, buf);
    
    u8g2->sendBuffer();
    delay(1500);
  }
}

/**
 * @brief Calcule et affiche l'espace disponible sur la carte SD via le port série.
 */
void checkSDCardSpace() {
  uint64_t totalBytes = SD.totalBytes();
  uint64_t usedBytes = SD.usedBytes();
  uint64_t freeBytes = totalBytes - usedBytes;

  double freeSpaceGB = freeBytes / (1024.0 * 1024.0 * 1024.0);
  double totalSpaceGB = totalBytes / (1024.0 * 1024.0 * 1024.0);

  Serial.printf("[SD] Free Space: %.2f GB / %.2f GB\n", freeSpaceGB, totalSpaceGB);
}

/**
 * @brief Enregistre les métadonnées et la charge utile brute d'une trame dans le fichier log CSV.
 * @param filepath Chemin absolu du fichier log (ex: "/log_0.csv").
 * @param frame Pointeur vers le tableau de données reçues.
 * @param length Taille en octets de la trame brute.
 * @param rssi Valeur RSSI (puissance du signal radio reçu).
 * @param snr Rapport signal/bruit (SNR).
 * @param ssid_str Chaîne de caractères décrivant l'identifiant du tracker.
 * @param apid Code de l'APID du tracker.
 * 
 * Cette fonction est optimisée pour minimiser le temps de traitement dans la boucle
 * principale. Elle écrit une ligne au format CSV :
 * Index,Timestamp,Length,RSSI,SNR,SSID,APID,RawFrame
 * La trame est écrite en hexadécimal continu rapide grâce à une table statique,
 * ce qui évite les conversions lentes basées sur 'sprintf'.
 */
void writeFrameToFile(const char* filepath, const uint8_t* frame, size_t length, float rssi, float snr, const char* ssid_str, uint8_t apid) {
  static uint32_t logPacketCount = 0;
  logPacketCount++;

  File log = SD.open(filepath, FILE_APPEND);
  if (log) {
    // 1. Index du message reçu pour cette session
    log.print(logPacketCount);
    log.print(",");

    // 2. Horodatage (HH:MM:SS) depuis l'horloge RTC
    char timeStr[16];
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d", rtc.getHour(true), rtc.getMinute(), rtc.getSecond());
    log.print(timeStr);
    log.print(",");

    // 3. Nombre d'octets de la trame
    log.print(length);
    log.print(",");

    // 4. Paramètres physiques radio
    log.print(rssi);
    log.print(",");
    log.print(snr);
    log.print(",");

    // 5. SSID décodé et APID
    log.print(ssid_str);
    log.print(",");
    log.print(apid);
    log.print(",");

    // 6. Charge utile brute convertie en Hexadécimal continu (sans sprintf)
    static const char hexChars[] = "0123456789ABCDEF";
    for (size_t i = 0; i < length; i++) {
      char hex[2];
      hex[0] = hexChars[(frame[i] >> 4) & 0x0F];
      hex[1] = hexChars[frame[i] & 0x0F];
      log.write((const uint8_t*)hex, 2);
    }

    log.println();
    log.close();
  } else {
    Serial.println("[SD] Error: Failed to open log file. SD card marked as OFFLINE.");
    *SDCard = false;
  }
}

/**
 * @brief Récupère les configurations LoRa enregistrées en mémoire non-volatile (NVS).
 * 
 * Si aucune configuration personnalisée n'est stockée, applique les valeurs
 * par défaut issues de la bande native de la carte de développement.
 */
void loadLoRaConfig() {
  Preferences prefs;
  
  // Ouvre le namespace "loracfg" en mode lecture/écriture
  prefs.begin("loracfg", false);
  
  // Lecture ou application des valeurs par défaut si non trouvées
  activeConfig.frequency = prefs.getFloat("freq", DEFAULT_FREQUENCY);
  activeConfig.spreadingFactor = prefs.getUChar("sf", DEFAULT_SF);
  activeConfig.bandwidth = prefs.getFloat("bw", DEFAULT_BW);
  activeConfig.crcEnable = prefs.getBool("crc", true);
  activeConfig.crcMode = prefs.getBool("crcMode", false);
  
  prefs.end(); // Fermeture propre de la NVS
  
  Serial.printf("[CONFIG] Loaded from NVS: Freq=%.3f MHz, SF=%d, BW=%.1f kHz, CRC=%s (Mode=%s)\n", 
                activeConfig.frequency, activeConfig.spreadingFactor, activeConfig.bandwidth, 
                activeConfig.crcEnable ? "ON" : "OFF", activeConfig.crcMode ? "IBM" : "CCITT");
}

/**
 * @brief Enregistre définitivement la configuration radio actuelle dans la NVS.
 */
void saveLoRaConfig() {
  Preferences prefs;
  prefs.begin("loracfg", false);
  
  prefs.putFloat("freq", activeConfig.frequency);
  prefs.putUChar("sf", activeConfig.spreadingFactor);
  prefs.putFloat("bw", activeConfig.bandwidth);
  prefs.putBool("crc", activeConfig.crcEnable);
  prefs.putBool("crcMode", activeConfig.crcMode);
  
  prefs.end();
  
  Serial.println("[CONFIG] Saved current config to NVS.");
}

/**
 * @brief Supprime l'intégralité des configurations stockées dans la NVS.
 * 
 * Permet de restaurer les paramètres d'usine lors d'un appel ultérieur ou d'un reboot.
 */
void resetLoRaConfig() {
  Preferences prefs;
  prefs.begin("loracfg", false);
  
  prefs.clear(); // Supprime toutes les clés du namespace
  
  prefs.end();
  
  Serial.println("[CONFIG] NVS configuration cleared.");
}