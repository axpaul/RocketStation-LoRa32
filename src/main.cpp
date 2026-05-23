#include "header.h"

U8G2_SSD1306_128X64_NONAME_F_HW_I2C *u8g2 = nullptr;
SPIClass *SDSPI = nullptr;
bool *SDCard = nullptr;
size_t receivedLen = 0;
char logFileName[32] = "/log.csv";
uint8_t *byteArr = nullptr;
SX1276 radio = new Module(RADIO_CS_PIN, RADIO_DIO0_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);

void setup() 
{
  Serial.begin(115200);
  SPI.begin(RADIO_SCLK_PIN, RADIO_MISO_PIN, RADIO_MOSI_PIN);
  Wire.begin(I2C_SDA, I2C_SCL);
  pinMode(BOARD_LED, OUTPUT);
  digitalWrite(BOARD_LED, LED_ON);
  pinMode(BATTERY_PIN, INPUT);

  u8g2 = new U8G2_SSD1306_128X64_NONAME_F_HW_I2C(U8G2_R0, U8X8_PIN_NONE);
  SDSPI = new SPIClass(HSPI);
  SDCard = new bool;
  byteArr = new uint8_t[MAX_FRAME_SIZE];
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
