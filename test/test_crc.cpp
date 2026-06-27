#include <Arduino.h>
#include <unity.h>

// 1. Fonction de calcul du CRC16-CCITT sous test
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

// 2. Fonction de conversion Hexadécimale rapide sous test (SD Card logging)
void bytes_to_hex_string(const uint8_t *bytes, size_t len, char *hex_str) {
    static const char hexChars[] = "0123456789ABCDEF";
    for (size_t i = 0; i < len; i++) {
        hex_str[i * 2] = hexChars[(bytes[i] >> 4) & 0x0F];
        hex_str[i * 2 + 1] = hexChars[bytes[i] & 0x0F];
    }
    hex_str[len * 2] = '\0';
}

// 3. Fonction mathématique de conversion de tension de batterie ADC sous test
float convert_adc_to_voltage(uint32_t adc_val) {
    return (adc_val / 4095.0f) * 3.3f * 2.0f * 1.05f;
}

// ============================================================================
// Cas de Test
// ============================================================================

// Tests pour le CRC16
void test_calculate_crc16_ccitt_standard() {
    const uint8_t test_str[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    uint16_t result = calculate_crc16(test_str, 9);
    TEST_ASSERT_EQUAL_HEX16(0x29B1, result);
}

void test_calculate_crc16_zeros() {
    const uint8_t test_zeros[] = {0x00, 0x00, 0x00, 0x00};
    uint16_t result = calculate_crc16(test_zeros, 4);
    TEST_ASSERT_EQUAL_HEX16(0x84C0, result);
}

void test_calculate_crc16_single_byte() {
    const uint8_t test_byte[] = {0xA5};
    uint16_t result = calculate_crc16(test_byte, 1);
    TEST_ASSERT_EQUAL_HEX16(0x04BF, result);
}

// Test pour l'encodage et décodage bitwise de l'identifiant de mission
void test_mission_id_encoding_decoding() {
    uint8_t input_type = 2; // BALLOON
    uint8_t input_num = 99; // SSID_NUM
    uint8_t input_apid = 15; // APID

    // Encodage (identique à src/serial.cpp)
    uint16_t ssid = ((input_type & 0x03) << 8) | input_num;
    uint16_t id_mission = (ssid << 6) | (input_apid & 0x3F);

    // Décodage (identique à docs/app.js)
    uint16_t decoded_ssid = id_mission >> 6;
    uint8_t decoded_apid = id_mission & 0x3F;
    uint8_t decoded_type = (decoded_ssid >> 8) & 0x03;
    uint8_t decoded_num = decoded_ssid & 0xFF;

    TEST_ASSERT_EQUAL_UINT8(input_type, decoded_type);
    TEST_ASSERT_EQUAL_UINT8(input_num, decoded_num);
    TEST_ASSERT_EQUAL_UINT8(input_apid, decoded_apid);
}

// Test pour la conversion hexadécimale de la payload
void test_bytes_to_hex_conversion() {
    const uint8_t test_bytes[] = {0x00, 0x1A, 0xBC, 0xFF, 0x09};
    char result_str[11];
    bytes_to_hex_string(test_bytes, 5, result_str);
    TEST_ASSERT_EQUAL_STRING("001ABCFF09", result_str);
}

// Test pour la formule de conversion ADC vers tension physique de batterie
void test_convert_adc_to_voltage() {
    // 0 ADC -> 0V
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, convert_adc_to_voltage(0));
    // 2048 ADC -> ~3.466V
    TEST_ASSERT_FLOAT_WITHIN(0.005f, 3.4655f, convert_adc_to_voltage(2048));
    // 4095 ADC (Max) -> ~6.930V (USB connecté ou surtension factice)
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 6.930f, convert_adc_to_voltage(4095));
}

// handles pour les tâches de test
TaskHandle_t testRxTaskHandle = NULL;
TaskHandle_t testIOTaskHandle = NULL;
TaskHandle_t testPeriTaskHandle = NULL;
QueueHandle_t testQueue = NULL;

void dummyRxTask(void *param) {
    uint8_t dummyBuf[256] = {0};
    for (;;) {
        uint16_t crc = calculate_crc16(dummyBuf, 100);
        xQueueSend(testQueue, &crc, 10);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void dummyIOTask(void *param) {
    uint16_t val;
    for (;;) {
        if (xQueueReceive(testQueue, &val, 10) == pdTRUE) {
            char hex[32];
            bytes_to_hex_string((uint8_t*)&val, 2, hex);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void dummyPeriTask(void *param) {
    for (;;) {
        // Simulation de traitement de commandes AT (lecture, parsing, formattage de chaîne)
        char cmd[] = "AT+FREQ=869.525";
        float val = atof(cmd + 8);
        char response[64];
        snprintf(response, sizeof(response), "+FREQ: %.3f\n", val);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void test_task_stacks_high_water_mark() {
    testQueue = xQueueCreate(5, sizeof(uint16_t));
    TEST_ASSERT_NOT_NULL(testQueue);

    // Démarrage des tâches de test avec les mêmes tailles de pile que le firmware réel
    // 4096 octets pour RX, 8192 octets pour IO et 4096 octets pour Peripheral
    xTaskCreatePinnedToCore(dummyRxTask, "TestRxTask", 4096, NULL, 3, &testRxTaskHandle, 1);
    xTaskCreatePinnedToCore(dummyIOTask, "TestIOTask", 8192, NULL, 1, &testIOTaskHandle, 0);
    xTaskCreatePinnedToCore(dummyPeriTask, "TestPeriTask", 4096, NULL, 1, &testPeriTaskHandle, 0);

    // Laisser tourner les tâches un court instant
    delay(500);

    // Récupération de l'espace mémoire libre minimal rencontré (en mots de 32 bits = 4 octets)
    UBaseType_t rxWaterMark = uxTaskGetStackHighWaterMark(testRxTaskHandle);
    UBaseType_t ioWaterMark = uxTaskGetStackHighWaterMark(testIOTaskHandle);
    UBaseType_t periWaterMark = uxTaskGetStackHighWaterMark(testPeriTaskHandle);

    // Affichage des informations de débogage sur le port série
    Serial.printf("[TEST] TestRxTask stack high water mark: %u words (%u bytes free)\n", rxWaterMark, rxWaterMark * 4);
    Serial.printf("[TEST] TestIOTask stack high water mark: %u words (%u bytes free)\n", ioWaterMark, ioWaterMark * 4);
    Serial.printf("[TEST] TestPeriTask stack high water mark: %u words (%u bytes free)\n", periWaterMark, periWaterMark * 4);

    // Nettoyage impératif avant les assertions pour éviter les fuites ou blocages en cas d'échec
    vTaskDelete(testRxTaskHandle);
    vTaskDelete(testIOTaskHandle);
    vTaskDelete(testPeriTaskHandle);
    vQueueDelete(testQueue);

    // Vérification qu'il reste au moins 10% d'espace libre de sécurité (400 octets pour RX, 800 octets pour IO, 400 octets pour Peripheral)
    TEST_ASSERT_TRUE_MESSAGE(rxWaterMark * 4 > 400, "TestRxTask stack is critically low!");
    TEST_ASSERT_TRUE_MESSAGE(ioWaterMark * 4 > 800, "TestIOTask stack is critically low!");
    TEST_ASSERT_TRUE_MESSAGE(periWaterMark * 4 > 400, "TestPeriTask stack is critically low!");
}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    
    // Suite CRC16
    RUN_TEST(test_calculate_crc16_ccitt_standard);
    RUN_TEST(test_calculate_crc16_zeros);
    RUN_TEST(test_calculate_crc16_single_byte);
    
    // Suite Logique binaire & Formats
    RUN_TEST(test_mission_id_encoding_decoding);
    RUN_TEST(test_bytes_to_hex_conversion);
    
    // Suite Tension & Mathématiques
    RUN_TEST(test_convert_adc_to_voltage);

    // Suite Multitâche FreeRTOS (Stabilité des piles)
    RUN_TEST(test_task_stacks_high_water_mark);
    
    UNITY_END();
}

void loop() {
}
