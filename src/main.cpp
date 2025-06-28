#include "header.h"

U8G2_SSD1306_128X64_NONAME_F_HW_I2C *u8g2 = nullptr;
SPIClass *SDSPI = nullptr;
bool *SDCard = nullptr;
uint8_t receivedFlagMain = false;
uint8_t *byteArr = nullptr;
SX1276 radio = new Module(RADIO_CS_PIN, RADIO_DIO0_PIN, RADIO_RST_PIN, RADIO_BUSY_PIN);

void setup() 
{
  Serial.begin(115200);
  SPI.begin(RADIO_SCLK_PIN, RADIO_MISO_PIN, RADIO_MOSI_PIN);
  Wire.begin(I2C_SDA, I2C_SCL);
  pinMode(BOARD_LED, OUTPUT);
  digitalWrite(BOARD_LED, LED_ON);

  u8g2 = new U8G2_SSD1306_128X64_NONAME_F_HW_I2C(U8G2_R0, U8X8_PIN_NONE);
  SDSPI = new SPIClass(HSPI);
  SDCard = new bool;
  byteArr = new uint8_t[NbTrame];
  *SDCard = false;
  ScreenText(u8g2);

  //SD card:
  SDCardDetection(u8g2, SDSPI, SDCard);
  if (*SDCard){
    checkSDCardSpace(u8g2);
  }
  //Radio:
  RadioSettings(u8g2, &radio);
}

void loop() {
  // put your main code here, to run repeatedly:
  receivedFlagMain = RadioReceive(u8g2, &radio, byteArr);
  
  if(receivedFlagMain == true){
    sendWithCRC(byteArr, NbTrame);
    if (*SDCard){
      writeFrameToFile(byteArr, NbTrame);  
    }
  }
}
