#ifndef _GPIO_H
#define _GPIO_H

/* Class to read and write the GPIO pins on linux or freebsd.
 * N.B. methods take GPIO number, not header pin number.
 * This is a Meyers Singleton, it can not be instantiated, use GPIO::getGPIO() to gain access.
 *
 * data sheet: https://www.raspberrypi.org/documentation/hardware/raspberrypi/bcm2835/BCM2835-ARM-Peripherals.pdf
 * errata: https://elinux.org/BCM2835_datasheet_errata
 */

#include <stdint.h>
#include <pthread.h>

#include "Arduino.h"

#if defined(_GPIO_FREEBSD)
#include <sys/types.h>
#include <libgpio.h>
#endif

class GPIO {

    public:

        static GPIO& getGPIO(void);
        bool isReady(void);
        void setAsInput(uint8_t p);
        void setAsOutput(uint8_t p);
        void setHi(uint8_t p);
        void setHiLo (uint8_t p, bool hi);
        void setLo(uint8_t p);
        bool readPin (uint8_t p);

#if __cplusplus > 199711L

        // enforce no copy or move, only possible in c++11
        GPIO(const GPIO&) = delete;             // Copy ctor
        GPIO(GPIO&&) = delete;                  // Move ctor
        GPIO& operator=(const GPIO&) = delete;  // Copy assignment
        GPIO& operator=(GPIO&&) = delete;       // Move assignment

#endif


    private:

        GPIO(void);

#if defined(_GPIO_LINUX)

        bool ready;
        volatile uint32_t *gbase;
        inline uint32_t GPIO_SEL_MASK (uint8_t p, uint32_t m) {
            return (m<<(3*(p%10)));
        }
        pthread_mutex_t lock;
        bool mapGPIOAddress(char ynot[]);

#elif defined(_GPIO_FREEBSD)

        bool ready;
        pthread_mutex_t lock;
        gpio_handle_t handle;

#endif

};

#endif // _GPIO_H
