#ifndef _EEPROM_H
#define _EEPROM_H

#include <stdio.h>
#include <stdint.h>

/* EEPROM class that uses a local file
 */

class EEPROM
{
    public:

        EEPROM(void);
	bool commit(void);
	void begin(int s);
	void write (uint32_t address, uint8_t byte);
	uint8_t read (uint32_t address);

    private:

        FILE *fp;
	char *filename;
        uint8_t *data_array;
        size_t n_data_array;
};

extern class EEPROM EEPROM;

#endif // _EEPROM_H
