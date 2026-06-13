#include <Arduino.h>
#include <unity.h>

// Copie locale de la fonction à tester pour éviter les bugs de dépendances SCons de PlatformIO
uint16_t calculate_crc16(const uint8_t *data, size_t len) {
    if (data == nullptr || len == 0) {
        return 0xFFFF;
    }
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

void test_calculate_crc16_ccitt_standard() {
    // Vecteur de test standard : la chaîne "123456789" (ASCII)
    // Le CRC16-CCITT standard (poly=0x1021, init=0xFFFF) pour cette chaîne est 0x29B1
    const uint8_t test_str[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    uint16_t result = calculate_crc16(test_str, 9);
    TEST_ASSERT_EQUAL_HEX16(0x29B1, result);
}

void test_calculate_crc16_zeros() {
    const uint8_t test_zeros[] = {0x00, 0x00, 0x00, 0x00};
    // Le CRC16-CCITT pour 4 octets à 0x00 est 0x84C0
    uint16_t result = calculate_crc16(test_zeros, 4);
    TEST_ASSERT_EQUAL_HEX16(0x84C0, result);
}

void test_calculate_crc16_single_byte() {
    const uint8_t test_byte[] = {0xA5};
    // Le CRC16-CCITT pour un seul octet 0xA5 est 0x5D38
    uint16_t result = calculate_crc16(test_byte, 1);
    TEST_ASSERT_EQUAL_HEX16(0x04BF, result);
}

void setup() {
    // Attendre l'initialisation de la liaison série de test
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_calculate_crc16_ccitt_standard);
    RUN_TEST(test_calculate_crc16_zeros);
    RUN_TEST(test_calculate_crc16_single_byte);
    UNITY_END();
}

void loop() {
    // Rien à faire ici
}
