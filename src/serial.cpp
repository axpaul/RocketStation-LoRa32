#include "header.h"

#if ENABLE_BLUETOOTH
BluetoothSerial SerialBT;
#endif

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

void sendNectarFrame(uint8_t ssid_type, uint8_t ssid_num, uint8_t apid, const uint8_t *payload, size_t len) {
    // 1. Limiter la longueur pour éviter tout débordement de buffer
    if (len > 255) {
        len = 255;
    }

    // 2. Calculer le SSID (10 bits) et l'Id_mission (16 bits)
    uint16_t ssid = ((ssid_type & 0x03) << 8) | ssid_num;
    uint16_t id_mission = (ssid << 6) | (apid & 0x3F);

    // 3. Préparer le Header NectarMC (4 octets)
    uint8_t header[4];
    header[0] = NECTAR_MAGIC;
    header[1] = id_mission & 0xFF;         // Encodage en Little-Endian (partie basse)
    header[2] = (id_mission >> 8) & 0xFF;  // Encodage en Little-Endian (partie haute)
    header[3] = (uint8_t)(len & 0xFF);     // payload_size

    // 4. Assembler l'en-tête et la payload dans un buffer temporaire pour le calcul du CRC
    // La taille max de la payload NectarMC est 255. La trame fait au max 4 + 255 = 259 octets.
    uint8_t frame[265];
    memcpy(frame, header, 4);
    if (len > 0 && payload != nullptr) {
        memcpy(frame + 4, payload, len);
    }

    // 5. Calculer le CRC16 sur Header + Payload
    uint16_t crc = calculate_crc16(frame, 4 + len);

    // 6. Émettre la trame complète sur le port série USB
    Serial.write(frame, 4 + len);
    Serial.write(crc & 0xFF);         // CRC16 Little-Endian (partie basse)
    Serial.write((crc >> 8) & 0xFF);  // CRC16 Little-Endian (partie haute)
    Serial.write('\n');               // Retour chariot pour faciliter la lecture/log dans un terminal série

#if ENABLE_BLUETOOTH
    // 7. Émettre également en Bluetooth si un appareil (PC/téléphone) est connecté.
    // SerialBT fonctionne de la même manière que Serial, les paquets binaires NectarMC
    // y sont écrits séquentiellement.
    if (SerialBT.connected()) {
        SerialBT.write(frame, 4 + len);
        SerialBT.write(crc & 0xFF);
        SerialBT.write((crc >> 8) & 0xFF);
        SerialBT.write('\n');         // Retour chariot pour la liaison Bluetooth
    }
#endif
}