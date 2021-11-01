#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "Arduino.h"
#include "ESP8266WiFi.h"
#include "ESP.h"

class ESP ESP;

ESP::ESP()
{
        sn = 0;
}

void ESP::restart(void)
{
        // log
        printf ("Restarting -- args will be:\n");
        for (int i = 0; our_argv[i] != NULL; i++)
            printf ("  argv[%d]: %s\n", i, our_argv[i]);
        printf ("see you there!\n\n");

        // close all but basic fd
        for (int i = 3; i < 100; i++)
            (void) ::close(i);

        // go
        execvp (our_argv[0], our_argv);

        printf ("%s has disappeared\n", our_argv[0]);
        exit(1);
}

/* try to get some sort of system serial number.
 * return 0xFFFFFFFF if unknown.
 */
uint32_t ESP::getChipId()
{
        // reuse once found
        if (sn)
            return (sn);

#if defined(_IS_LINUX)

        // try cpu serial number

        FILE *fp = popen ("awk -F: '/Serial/{print $2}' /proc/cpuinfo", "r");
        if (fp) {
            char buf[1024];
            while (fgets (buf, sizeof(buf), fp)) {
                int l = strlen(buf);                            // includes nl
                if (l >= 9) {                                   // 8 + nl
                    sn = strtoul (&buf[l-9], NULL, 16);         // 8 LSB
                    if (sn) {
                        printf ("Found ChipId '%.*s' -> 0x%X = %u\n", l-1, buf, sn, sn);
                        break;
                    }
                }
            }
            pclose (fp);
            if (sn)
                return (sn);
        }

#endif // _IS_LINUX

        // try MAC address

        std::string mac = WiFi.macAddress();
        unsigned int m1, m2, m3, m4, m5, m6;
        if (sscanf (mac.c_str(), "%x:%x:%x:%x:%x:%x", &m1, &m2, &m3, &m4, &m5, &m6) == 6) {
            sn = (m3<<24) + (m4<<16) + (m5<<8) + m6;
            printf ("Found ChipId from MAC '%s' -> 0x%x = %u\n", mac.c_str(), sn, sn);
        } else {
            printf ("No ChipId\n");
            sn = 0xFFFFFFFF;
        }

        return (sn);
}

void yield(void)
{
}
