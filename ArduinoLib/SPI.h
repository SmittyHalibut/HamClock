#ifndef _SPI_H
#define _SPI_H

/* Arduino SPI.h implemented over raspberry pi.
 * TODO
 */

#include "Arduino.h"

const uint8_t SPI_MODE0 = 0x00; ///<  CPOL: 0  CPHA: 0
const uint8_t SPI_MODE1 = 0x01; ///<  CPOL: 0  CPHA: 1
const uint8_t SPI_MODE2 = 0x10; ///<  CPOL: 1  CPHA: 0
const uint8_t SPI_MODE3 = 0x11; ///<  CPOL: 1  CPHA: 1

class SPISettings {
public:
  SPISettings() :_clock(1000000), _bitOrder(LSBFIRST), _dataMode(SPI_MODE0){}
  SPISettings(uint32_t clock, uint8_t bitOrder, uint8_t dataMode) :_clock(clock), _bitOrder(bitOrder), _dataMode(dataMode){}
  uint32_t _clock;
  uint8_t  _bitOrder;
  uint8_t  _dataMode;
};

class SPIClass {
    public:

        SPIClass();

        // stubs
        void begin(void){}
        uint8_t transfer(uint8_t data) { return 0; }
        void beginTransaction(SPISettings settings) { }
        void endTransaction(void) { }


};

extern SPIClass SPI;

#endif // _SPI_H
