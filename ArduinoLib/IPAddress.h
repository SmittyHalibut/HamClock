#ifndef _IPADDRESS_H
#define _IPADDRESS_H

#include "Arduino.h"

class IPAddress {

    private:

        union {
	    uint8_t bytes[4];  // IPv4 address
	    uint32_t dword;
	} _address;

    public:

        // construct unintialized
        IPAddress(void) { }

        // construct with given octets
        IPAddress (uint8_t a, uint8_t b, uint8_t c, uint8_t d)
        {
            _address.bytes[0] = a;
            _address.bytes[1] = b;
            _address.bytes[2] = c;
            _address.bytes[3] = d;
        }

        // Overloaded index operator to allow getting and setting individual octets of the address
        uint8_t operator[](int index) const {
            return _address.bytes[index];
        }
        uint8_t& operator[](int index) {
            return _address.bytes[index];
        }


};

#endif // _IPADDRESS_H
