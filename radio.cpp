/* initial seed of radio control idea.
 */


#include "HamClock.h"


#if defined(_SUPPORT_KX3)

/* cleanup commands before changing freq:
 *
 *   SB0 = Set Sub Receiver or Dual Watch off
 *   FR0 = Cancel Split on K2, set RX vfo A
 *   FT0 = Set tx vfo A
 *   RT0 = Set RIT off
 *   XT0 = Set XIT off
 *   RC  = Set RIT / XIT to zero
 */
static char cleanup_cmds[] = ";SB0;FR0;FT0;RT0;XT0;RC;";

/* sprintf format to set new frequency, requires float in Hz
 */
static char setfreq_fmt[] = ";FA%011.0f;";


#if defined(_IS_ESP8266)


/**********************************************************************************
 *
 *
 *  send spot frequency to Elecraft radio on Huzzah pin 15.
 *
 *
 **********************************************************************************
 */
 


#define Elecraft_GPIO   15



/* send one bit @ getKX3Baud().
 * N.B. they want mark/sense inverted
 */
static void sendOneBit (uint8_t hi)
{
    uint32_t t0 = ESP.getCycleCount();
    digitalWrite (Elecraft_GPIO, !hi);
    uint32_t bit_time = ESP.getCpuFreqMHz()*1000000UL/getKX3Baud();
    while (ESP.getCycleCount()-t0 < bit_time)
        continue;
}

/* perform one-time preparation for sending commands
 */
static void prepIO()
{
    pinMode(Elecraft_GPIO, OUTPUT);
    sendOneBit (1);
}

/* send the given command.
 */
static void sendOneMessage (const char cmd[])
{
    Serial.printf ("Elecraft: %s\n", cmd);

    // send each char, 8N1, MSByte first
    char c;
    while ((c = *cmd++) != '\0') {

        // disable interrupts for a clean transmit
        cli();

        sendOneBit (0);                                 // start bit
        for (uint8_t bit = 0; bit < 8; bit++) {         // LSBit first
            sendOneBit (c & 1);
            c >>= 1;
        }
        sendOneBit (1);                                 // stop bit

        // resume interrupts
        sei();
    }
}




/* command radio to the given frequency.
 */
void setRadioSpot (float kHz)
{
    resetWatchdog();

    // ignore if not to use GPIO or baud 0
    if (!GPIOOk() || getKX3Baud() == 0)
        return;

    // one-time IO setup
    static bool ready;
    if (!ready) {
        prepIO();
        ready = true;
        Serial.println (F("Elecraft: ready"));
    }

    // send cleanup commands
    sendOneMessage (cleanup_cmds);

    // format and send command to change frequency
    char buf[30];
    (void) sprintf (buf, setfreq_fmt, kHz*1e3);
    sendOneMessage (buf);
}

#elif defined(_SUPPORT_GPIO)




/**********************************************************************************
 *
 *
 * hack to send spot frequency to Elecraft radio on RPi GPIO 14 (header pin 8).
 * can not use HW serial because Electraft wants inverted mark/space, thus
 * timing will not be very good.
 *
 *
 **********************************************************************************
 */
 



#include <time.h>
#include "GPIO.h"


/* send one bit @ getKX3Baud(), bit time multiplied by correction factor.
 * N.B. they want mark/sense inverted
 * N.B. this can be too long depending on kernel scheduling. Performance might be improved by
 *      assigning this process to a dedicated processor affinity and disable being scheduled using isolcpus.
 *      man pthread_setaffinity_np
 *      https://www.kernel.org/doc/html/v4.10/admin-guide/kernel-parameters.html
 */
static void sendOneBit (int hi, float correction)
{
    // get time now
    struct timespec t0, t1;
    clock_gettime (CLOCK_MONOTONIC, &t0);

    // set bit (remember: Elecraft wants inverted mark/sense)
    GPIO& gpio = GPIO::getGPIO();
    gpio.setHiLo (Elecraft_GPIO, !hi);

    // wait for one bit duration with modified correction including nominal correction
    uint32_t baud = getKX3Baud();
    float overhead = 1.0F - 0.08F*baud/38400;          // measured on pi 4
    unsigned long bit_ns = 1000000000UL/baud*overhead*correction;
    unsigned long dt_ns;
    do {
        clock_gettime (CLOCK_MONOTONIC, &t1);
        dt_ns = 1000000000UL*(t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec);
    } while (dt_ns < bit_ns);
}

/* perform one-time preparation for sending commands
 */
static void prepIO()
{
    // init Elecraft pin
    GPIO& gpio = GPIO::getGPIO();
    gpio.setAsOutput(Elecraft_GPIO);
    sendOneBit (1, 1.0F);
}


/* send the given string with the given time correction factor.
 * return total nsecs.
 */
static uint32_t sendOneString (float correction, const char str[])
{
    // get current scheduler and priority
    int orig_sched = sched_getscheduler(0);
    struct sched_param orig_param;
    sched_getparam (0, &orig_param);

    // attempt setting high priority
    struct sched_param hi_param;
    hi_param.sched_priority = sched_get_priority_max(SCHED_FIFO);
    bool hipri_ok = sched_setscheduler (0, SCHED_FIFO, &hi_param) == 0;
    if (!hipri_ok)
        printf ("Failed to set new prioity %d: %s\n", hi_param.sched_priority, strerror(errno));

    // get starting time
    struct timespec t0, t1;
    clock_gettime (CLOCK_MONOTONIC, &t0);

    // send each char, 8N1, MSByte first
    char c;
    while ((c = *str++) != '\0') {
        sendOneBit (0, correction);                         // start bit
        for (uint8_t bit = 0; bit < 8; bit++) {             // LSBit first
            sendOneBit (c & 1, correction);                 // data bit
            c >>= 1;
        }
        sendOneBit (1, correction);                         // stop bit
    }

    // record finish time
    clock_gettime (CLOCK_MONOTONIC, &t1);

    // restore original priority
    if (hipri_ok)
        sched_setscheduler (0, orig_sched, &orig_param);

    // return duration in nsec
    return (1000000000UL*(t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec));
}

/* send the given command.
 */
static void sendOneMessage (const char cmd[])
{
    // len
    size_t cmd_l = strlen(cmd);

    // compute ideal time to send command
    uint32_t bit_ns = 1000000000UL/getKX3Baud();           // ns per bit
    uint32_t cmd_ns = cmd_l*10*bit_ns;                     // start + 8N1
  
    // send with no speed correction
    uint32_t ns0 = sendOneString (1.0F, cmd);

    // compute measured correction factor
    float correction = (float)cmd_ns/ns0;

    // repeat if correction more than 1 percent
    uint32_t ns1 = 0;
    if (correction < 0.99F || correction > 1.01F) {
        usleep (500000);    // don't pummel
        ns1 = sendOneString (correction, cmd);
    }

    printf ("Elecraft: correction= %g cmd= %u ns0= %u ns1= %u ns\n", correction, cmd_ns, ns0, ns1);

}


/* command radio to the given frequency.
 */
void setRadioSpot (float kHz)
{
    resetWatchdog();

    // ignore if not to use GPIO or baud 0
    if (!GPIOOk() || getKX3Baud() == 0)
        return;

    // one-time IO setup
    static bool ready;
    if (!ready) {
        prepIO();
        ready = true;
        Serial.println (F("Elecraft: ready"));
    }

    // send cleanup commands
    sendOneMessage (cleanup_cmds);

    // format and send command to change frequency
    char buf[30];
    (void) sprintf (buf, setfreq_fmt, kHz*1e3);
    sendOneMessage (buf);
}

#endif // _SUPPORT_GPIO

#else  // !_SUPPORT_KX3


/* dummy for unsupported platforms
 */

void setRadioSpot (float kHz)
{
    (void) kHz;
}

#endif // _SUPPORT_KX3

