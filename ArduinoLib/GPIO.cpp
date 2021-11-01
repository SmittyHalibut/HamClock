/* simple GPIO access class for linux or freebsd suitable for HamClock.
 *
 * The class constructor will only be called once because it is not public and there is only one instance
 * within a static context. This is the hallmark of a Meyers Singleton. Can not be instantiated,
 * use GPIO::getGPIO() to gain access. The class is thread-safe, although of course the functioning of
 * the connected devices may not be.
 */

#include "GPIO.h"



#if defined(_GPIO_LINUX)

/************************************************************************************
 *
 * linux implementation
 *
 ************************************************************************************/


#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>

#include <bcm_host.h>


/* constructor
 * _GPIO_LINUX
 */
GPIO::GPIO()
{
        // prepare gpio access
        char ynot[1024];
        ready = mapGPIOAddress(ynot);
        if (ready) {
            // init lock for safe threaded access
            pthread_mutexattr_t lock_attr;
            pthread_mutexattr_init (&lock_attr);
            pthread_mutexattr_settype (&lock_attr, PTHREAD_MUTEX_RECURSIVE);
            pthread_mutex_init (&lock, &lock_attr);
        } else {
            // note why not
            printf ("GPIO: %s\n", ynot);
        }

}

/* return reference to the one shared instance
 * _GPIO_LINUX
 */
GPIO& GPIO::getGPIO()
{
        static GPIO the_one;         // the only instance, hence only one call to constructor
        return the_one;
}

/* return whether GPIO is suitable for use
 * _GPIO_LINUX
 */
bool GPIO::isReady()
{
    return (ready);
}

/* _GPIO_LINUX
 */
void GPIO::setAsInput(uint8_t p)
{
        if (!ready)
            return;

        pthread_mutex_lock(&lock);

        gbase[p/10] &= ~GPIO_SEL_MASK(p,7);

        // enable pullup -- BCM2835
        gbase[37] = 2;
        gbase[38+p/32] = 1UL << (p%32);
        gbase[37] = 0;
        gbase[38+p/32] = 0;

        // enable pullup -- BCM2711
        gbase[57+p/16] = (gbase[57+p/16] & ~(3UL << 2*(p%16))) | (1UL << 2*(p%16));

        pthread_mutex_unlock(&lock);
}

/* _GPIO_LINUX
 */
void GPIO::setAsOutput(uint8_t p)
{
        if (!ready)
            return;

        pthread_mutex_lock(&lock);
        gbase[p/10] = (gbase[p/10] & ~GPIO_SEL_MASK(p,7)) | GPIO_SEL_MASK(p,1);
        pthread_mutex_unlock(&lock);
}

/* _GPIO_LINUX
 */
void GPIO::setHi(uint8_t p)
{
        if (!ready)
            return;

        pthread_mutex_lock(&lock);
        gbase[7+p/32] = 1UL << (p%32);
        pthread_mutex_unlock(&lock);
}

/* _GPIO_LINUX
 */
void GPIO::setLo(uint8_t p)
{
        if (!ready)
            return;

        pthread_mutex_lock(&lock);
        gbase[10+p/32] = 1UL << (p%32);
        pthread_mutex_unlock(&lock);
}

/* _GPIO_LINUX
 */
void GPIO::setHiLo (uint8_t p, bool hi)
{
        if (!ready)
            return;

        if (hi)
            setHi (p);
        else
            setLo (p);
}

/* _GPIO_LINUX
 */
bool GPIO::readPin (uint8_t p)
{
        if (!ready)
            return(false);

        pthread_mutex_lock(&lock);
        bool hi = (gbase[13+p/32] & (1UL<<(p%32))) != 0;
        pthread_mutex_unlock(&lock);
        return (hi);
}

/* set gbase so it points to the physical address of the GPIO controller.
 * return true if ok, else false with brief excuse in ynot[].
 * _GPIO_LINUX
 */
bool GPIO::mapGPIOAddress(char ynot[])
{
        // access kernel physical address
        const char gpiofilefile[] = "/dev/gpiomem";
        int fd = open (gpiofilefile, O_RDWR|O_SYNC);
        if (fd < 0) {
            sprintf (ynot, "%s: %s", gpiofilefile, strerror(errno));
            return (false);
        }

        /* mmap access */
        gbase = (uint32_t *) mmap(NULL, 0xB4, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);

        // fd not needed after setting up mmap
        close(fd);

        // check for error, leave gbase 0 if so
        if (gbase == MAP_FAILED) {
            gbase = NULL;
            sprintf (ynot, "mmap: %s", strerror(errno));
            return (false);
        }

        // worked
        return (true);
}


#elif defined(_GPIO_FREEBSD)


/************************************************************************************
 *
 * freebsd implementation
 *
 ************************************************************************************/



/* prepare class, set ready if useable.
 * _GPIO_FREEBSD
 */
GPIO::GPIO()
{
        handle = gpio_open(0);

        if (handle == GPIO_INVALID_HANDLE) {
            printf ("GPIO open failed\n");
            ready = false;
        } else {
            ready = true;
            // init lock for safe threaded access
            pthread_mutexattr_t lock_attr;
            pthread_mutexattr_init (&lock_attr);
            pthread_mutexattr_settype (&lock_attr, PTHREAD_MUTEX_RECURSIVE);
            pthread_mutex_init (&lock, &lock_attr);
        }
}

/* return reference to the one shared instance
 * _GPIO_FREEBSD
 */
GPIO& GPIO::getGPIO()
{
        static GPIO the_one;         // the only instance, hence only one call to constructor

    // #define _GPIO_LIST_PINS
    #ifdef _GPIO_LIST_PINS
        gpio_config_t *config;
        int n_pins = gpio_pin_list (the_one.handle, &config);
        for (int i = 0; i < n_pins; i++)
            printf ("pin %d flags %x\n", config[i].g_pin, config[i].g_flags);
        free (config);
    #endif // _TEST_GPIO

        return the_one;
}

/* return whether GPIO is suitable for use
 * _GPIO_FREEBSD
 */
bool GPIO::isReady()
{
        return (ready);
}

/* _GPIO_FREEBSD
 */
void GPIO::setAsInput (uint8_t p)
{
        pthread_mutex_lock(&lock);

        // must set input and pullup atomically
        gpio_config_t cfg;
        memset (&cfg, 0, sizeof(cfg));
        cfg.g_pin = p;
        cfg.g_flags = GPIO_PIN_INPUT | GPIO_PIN_PULLUP;
        gpio_pin_set_flags (handle, &cfg);

        pthread_mutex_unlock(&lock);
}

void GPIO::setAsOutput (uint8_t p)
{
        pthread_mutex_lock(&lock);
        gpio_pin_output (handle, p);
        pthread_mutex_unlock(&lock);
}

void GPIO::setHi (uint8_t p)
{
        pthread_mutex_lock(&lock);
        gpio_pin_high (handle, p);
        pthread_mutex_unlock(&lock);
}

void GPIO::setLo(uint8_t p)
{
        pthread_mutex_lock(&lock);
        gpio_pin_low (handle, p);
        pthread_mutex_unlock(&lock);
}

void GPIO::setHiLo (uint8_t p, bool hi)
{
        pthread_mutex_lock(&lock);
        gpio_pin_set (handle, p, hi != 0);
        pthread_mutex_unlock(&lock);
}

bool GPIO::readPin (uint8_t p)
{
        pthread_mutex_lock(&lock);
        bool s = gpio_pin_get (handle, p) == GPIO_VALUE_HIGH;
        pthread_mutex_unlock(&lock);
        return (s);
}


#else


// dummy placeholders

GPIO::GPIO() { }
GPIO& GPIO::getGPIO()
{
        static GPIO the_one;         // the only instance, hence only one call to constructor
        return the_one;
}
void GPIO::setAsInput(uint8_t p) { } 
void GPIO::setAsOutput(uint8_t p) { }
void GPIO::setHi(uint8_t p) { }
void GPIO::setLo(uint8_t p) { }
void GPIO::setHiLo (uint8_t p, bool hi) { }
bool GPIO::readPin (uint8_t p) { return false; }

#endif
