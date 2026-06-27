/**
 * @file at_commands.cpp
 * @brief Gestion de l'analyse, du décodage et de l'application des commandes AT.
 * @author Paul Miailhe
 * @date 27/06/2026
 */

#include "header.h"

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
    if (xRadioRxTaskHandle != NULL) {
      responseStream.printf("RadioRxTask Stack Free: %u B\n", uxTaskGetStackHighWaterMark(xRadioRxTaskHandle) * 4);
    }
    if (xIOProcessingTaskHandle != NULL) {
      responseStream.printf("IOProcessingTask Stack Free: %u B\n", uxTaskGetStackHighWaterMark(xIOProcessingTaskHandle) * 4);
    }
    if (xPeripheralTaskHandle != NULL) {
      responseStream.printf("PeripheralTask Stack Free: %u B\n", uxTaskGetStackHighWaterMark(xPeripheralTaskHandle) * 4);
    }
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
