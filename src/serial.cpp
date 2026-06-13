/**
 * @file serial.cpp
 * @brief Gestion de la liaison série USB et Bluetooth pour l'envoi des trames NectarMC.
 * @author Paul Miailhe
 * @date 14/06/2023
 */

#include "header.h"

#if ENABLE_BLUETOOTH
BluetoothSerial SerialBT;
#endif

/**
 * @brief Calcule le CRC16-CCITT d'un tableau d'octets.
 * @param data Pointeur vers le tableau de données.
 * @param len Longueur du tableau de données.
 * @return Valeur du CRC16 calculé.
 * 
 * Utilise le polynôme standard 0x1021 avec une valeur initiale de 0xFFFF.
 */
uint16_t calculate_crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= (data[i] << 8);
        for (int j = 0; j < 8; ++j) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

/**
 * @brief Construit, valide et émet une trame conforme au protocole NectarMC.
 * @param ssid_type Type de tracker (0: FX, 1: MF, 2: BALLOON, 3: OTHER).
 * @param ssid_num Numéro unique de tracker.
 * @param apid Identifiant d'application (Application Process Identifier).
 * @param payload Pointeur vers les données utiles de la trame.
 * @param len Longueur des données utiles en octets.
 * 
 * Cette fonction assemble :
 * - Le header NectarMC (octet magic 0xEB, identifiant de mission encodé sur 16 bits en Little-Endian, taille de payload).
 * - La charge utile (payload).
 * - Le CRC16-CCITT calculé sur le header et la payload.
 * La trame finale est émise sur USB (Serial) et Bluetooth (SerialBT) si un appareil est appairé.
 * Un caractère '\n' est ajouté en fin de trame pour en faciliter l'enregistrement.
 */
void sendNectarFrame(uint8_t ssid_type, uint8_t ssid_num, uint8_t apid, const uint8_t *payload, size_t len, int8_t rssi, int8_t snr) {
    // 1. Limiter la longueur brute de LoRa à 253 octets max pour laisser de la place au RSSI (1B) et SNR (1B)
    if (len > 253) {
        len = 253;
    }

    // 2. Calculer le SSID (10 bits) et l'Id_mission (16 bits)
    uint16_t ssid = ((ssid_type & 0x03) << 8) | ssid_num;
    uint16_t id_mission = (ssid << 6) | (apid & 0x3F);

    // 3. Préparer le Header NectarMC (4 octets)
    // La taille de la payload transmise en série est la longueur LoRa + 2 octets (RSSI + SNR)
    uint8_t header[4];
    header[0] = NECTAR_MAGIC;
    header[1] = id_mission & 0xFF;              // Encodage en Little-Endian (partie basse)
    header[2] = (id_mission >> 8) & 0xFF;       // Encodage en Little-Endian (partie haute)
    header[3] = (uint8_t)((len + 2) & 0xFF);    // Taille de la payload série (données + RSSI + SNR)

    // 4. Assembler le header, le payload LoRa et les métriques RSSI/SNR dans la trame
    uint8_t frame[265];
    memcpy(frame, header, 4);
    if (len > 0 && payload != nullptr) {
        memcpy(frame + 4, payload, len);
    }
    frame[4 + len] = (uint8_t)rssi;
    frame[4 + len + 1] = (uint8_t)snr;

    // 5. Calculer le CRC16 sur l'ensemble [Header + Payload (avec RSSI + SNR)]
    uint16_t crc = calculate_crc16(frame, 4 + len + 2);

    // 6. Émettre la trame complète sur la liaison série USB (Serial)
    Serial.write(frame, 4 + len + 2);
    Serial.write(crc & 0xFF);         // CRC16 Little-Endian (partie basse)
    Serial.write((crc >> 8) & 0xFF);  // CRC16 Little-Endian (partie haute)
    Serial.write('\n');               // Retour chariot pour le debug dans les terminaux série

#if ENABLE_BLUETOOTH
    // 7. Émettre également en Bluetooth si un client est connecté.
    if (SerialBT.connected()) {
        SerialBT.write(frame, 4 + len + 2);
        SerialBT.write(crc & 0xFF);
        SerialBT.write((crc >> 8) & 0xFF);
        SerialBT.write('\n');         // Retour chariot pour la liaison Bluetooth
    }
#endif
}