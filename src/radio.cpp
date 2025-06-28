#include "header.h"

volatile bool receivedFlag = false;
volatile bool enableInterrupt = true;
ESP32Time rtc;

void setFlag(void)
{
    // check if the interrupt is enabled
    if (!enableInterrupt) {
        return;
    }

    // we got a packet, set the flag
    receivedFlag = true;
}

// initialize SX1276 with default settings
void RadioSettings(U8G2_SSD1306_128X64_NONAME_F_HW_I2C* u8g2, SX1276 *radio){

  u8g2->clearBuffer();
  u8g2->setCursor(0, 16);
  u8g2->println( "[SX1276] Initializing ...");
  u8g2->sendBuffer();
  delay(500);

  rtc.setTime(0, 0, 0, 0, 0, 2023);
 
  int state = radio->begin(FREQUENCY);

  if (state == RADIOLIB_ERR_NONE) {
    
    radio->setOutputPower(17);
    radio->setBandwidth(250);
    radio->setCurrentLimit(120);
    radio->setSpreadingFactor(8);
    radio->setCRC(true, false);
    radio->setDio0Action(setFlag, RISING);
    state = radio->startReceive();

      do {
      u8g2->setCursor(0, 32);
      u8g2->println( "[SX1276] Complete !");
      u8g2->setCursor(0, 48);
      u8g2->println( "Waiting to receive data");
      u8g2->sendBuffer();
    } while (u8g2->nextPage());

    u8g2->sendBuffer();
    delay(1000);
    u8g2->clearBuffer();

  } 
  else {
        while (true);
    }

  if (state != RADIOLIB_ERR_NONE) {
    u8g2->clearBuffer();
    u8g2->drawStr(0, 12, "Initializing radio: FAIL!");
    u8g2->sendBuffer();
    while (1);    
  }
}

uint8_t RadioReceive(U8G2_SSD1306_128X64_NONAME_F_HW_I2C* u8g2, SX1276 *radio, uint8_t* byteArr){
  
  if (receivedFlag) {
    // disable the interrupt service routine while
    // processing the data
    enableInterrupt = false;

    // reset flag
    receivedFlag = false;

    //char time[8] = rtc.getTime();
    int state = radio->readData(byteArr, NbTrame);

    if (state == RADIOLIB_ERR_NONE) {
      u8g2->clearBuffer();

      char buf[256];
      u8g2->drawStr(0, 12, "Received OK!");
      snprintf(buf, sizeof(buf),"Last Trame Time %d:%d:%d", rtc.getHour(true), rtc.getMinute(), rtc.getSecond());
      u8g2->drawStr(0, 26, buf);
      snprintf(buf, sizeof(buf), "RSSI:%.2f", radio->getRSSI());
      u8g2->drawStr(0, 40, buf);
      snprintf(buf, sizeof(buf), "SNR:%.2f", radio->getSNR());
      u8g2->drawStr(0, 54, buf);
      u8g2->sendBuffer();

      RadioStartListen(radio);
      return 1;
    }
    else if (state == RADIOLIB_ERR_CRC_MISMATCH) {
      // packet was received, but is malformed
      char buf[256];
      u8g2->drawStr(0, 12, "CRC error !");
      snprintf(buf, sizeof(buf),"Last Trame Time %d:%d:%d", rtc.getHour(true), rtc.getMinute(), rtc.getSecond());
      u8g2->drawStr(0, 26, buf);
      snprintf(buf, sizeof(buf), "RSSI:%.2f", radio->getRSSI());
      u8g2->drawStr(0, 40, buf);
      snprintf(buf, sizeof(buf), "SNR:%.2f", radio->getSNR());
      u8g2->drawStr(0, 54, buf);
      u8g2->sendBuffer();

      RadioStartListen(radio);
      return 0;
    } 
    else {
      // some other error occurred
      char buf[256];
      snprintf(buf, sizeof(buf), "Failed, code %d", state);
      u8g2->drawStr(0, 12, buf);
      snprintf(buf, sizeof(buf),"Last Trame Time %d:%d:%d", rtc.getHour(true), rtc.getMinute(), rtc.getSecond());
      u8g2->drawStr(0, 26, buf);
      snprintf(buf, sizeof(buf), "RSSI:%.2f", radio->getRSSI());
      u8g2->drawStr(0, 40, buf);
      snprintf(buf, sizeof(buf), "SNR:%.2f", radio->getSNR());
      u8g2->drawStr(0, 54, buf);
      u8g2->sendBuffer();

      RadioStartListen(radio);
      return 0;
    }
  }
  else{
    // no interrupt
    return 0;
  }
}

void RadioStartListen(SX1276 *radio){
  // put module back to listen mode
  radio->startReceive();
  // we're ready to receive more packets,
  // enable interrupt service routine
  enableInterrupt = true;
}