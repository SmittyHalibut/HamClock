/* implement EEPROM class using a local file.
 * format is %08X %02X\n for each address/byte pair.
 * updates of existing address are performed in place.
 */

#include <string>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/file.h>

#include "Arduino.h"
#include "EEPROM.h"

class EEPROM EEPROM;

EEPROM::EEPROM()
{
        fp = NULL;
        filename = NULL;
}

void EEPROM::begin (int s)
{
        // establish file name
	if (!filename) {

            // new file name
            std::string newfn = our_dir + "eeprom";

            // preserve old file if found
	    char oldfn[1024];
	    snprintf (oldfn, sizeof(oldfn), "%s/.rpihamclock_eeprom", getenv("HOME"));
            rename (oldfn, newfn.c_str());

	    filename = strdup (newfn.c_str());
	}

        // start over if called again
        if (fp) {
            fclose(fp);
            fp = NULL;
        }
        if (data_array) {
            free (data_array);
            data_array = NULL;
        }

        // open RW, create if new owned by real user
	fp = fopen (filename, "r+");
        if (fp) {
            printf ("EEPROM %s: open ok\n", filename);
        } else {
            fp = fopen (filename, "w+");
            if (fp)
                printf ("EEPROM %s: create ok\n", filename);
            else {
                fatalError ("EEPROM %s:\ncreate failed:\n%s\n", filename, strerror(errno));
                // never returns
            }
        }
        fchown (fileno(fp), getuid(), getgid());

        // check lock
        if (flock (fileno(fp), LOCK_EX|LOCK_NB) < 0)
            fatalError ("Another instance of HamClock has been detected.\n"
                        "Only one at a time is allowed or use the -d argument to give each\n"
                        "a separate working directory.");

        // malloc memory, init as zeros
        n_data_array = s;
        data_array = (uint8_t *) calloc (n_data_array, sizeof(uint8_t));

        // init data_array from file .. support old version of random memory locations
	char line[64];
	unsigned int a, b;
	while (fp && fgets (line, sizeof(line), fp)) {
	    // sscanf (line, "%x %x", &a, &b); printf ("R: %08X %02X\n", a, b);
	    if (sscanf (line, "%x %x", &a, &b) == 2 && a < n_data_array)
                data_array[a] = b;
        }
}

bool EEPROM::commit(void)
{
        // (over)write entire data_array array
        fseek (fp, 0L, SEEK_SET);
        for (unsigned a = 0; a < n_data_array; a++)
            fprintf (fp, "%08X %02X\n", a, data_array[a]);
        fflush (fp);

        // return whether io ok
        return (!feof(fp) && !ferror(fp));
}

void EEPROM::write (uint32_t address, uint8_t byte)
{
        // set array if available and address is in bounds
        if (data_array && address < n_data_array)
            data_array[address] = byte;
}

uint8_t EEPROM::read (uint32_t address)
{
        // use array if available and address is in bounds
        if (data_array && address < n_data_array)
            return (data_array[address]);

        // not found
        return (0);
}
