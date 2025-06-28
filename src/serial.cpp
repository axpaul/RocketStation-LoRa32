#include "header.h"

uint8_t calculate_crc8(uint8_t *data, size_t len) {
    uint8_t crc = 0;
    while (len--) {
        uint8_t extract = *data++;
        for (uint8_t tempI = 8; tempI; tempI--) {
            uint8_t sum = (crc ^ extract) & 0x01;
            crc >>= 1;
            if (sum) {
                crc ^= CRC8_DPOLY;
            }
            extract >>= 1;
        }
    }
    return crc;
}

void sendWithCRC(uint8_t *data, size_t len) {
    // Calculer le CRC
    uint8_t crc = calculate_crc8(data, len);

    // Envoyer le début de la trame, le CRC, les données, puis la fin de la trame
    Serial.write(0xEE);  // Start of Header (SOH)
    Serial.write(crc);   // Checksum
    for (size_t i = 0; i < len; ++i) {
        Serial.write(data[i]);  // Data
    }
    Serial.write('\n');  // End of Transmission (EOT)
}