#ifndef _ARDUINO_H
#define _ARDUINO_H

/* Arduino.h over unix
 */



#if defined(__linux__)
  #define _IS_LINUX
#endif

#if defined(__FreeBSD__)
  #define _IS_FREEBSD
#endif

#if defined(_IS_ESP8266)
  #define _IIC_ESP
#elif defined(__has_include)
  #if defined(_IS_FREEBSD) && __has_include(<dev/iicbus/iic.h>) && __has_include("/dev/iic0")
    #define _IIC_FREEBSD
  #elif defined(_IS_LINUX) && (__has_include(<linux/i2c-dev.h>) || __has_include("linux/i2c-dev.h"))
    #define _IIC_LINUX
  #endif
#endif

#if defined(_IS_ESP8266)
  #define _GPIO_ESP
#elif defined(__has_include)
  #if defined(_IS_FREEBSD) && __has_include(<libgpio.h>) && __has_include("/dev/gpioc0")
    #define _GPIO_FREEBSD
  #elif defined(_IS_LINUX) && __has_include(<bcm_host.h>)
    #define _GPIO_LINUX
  #endif
#endif


#include <stdint.h>
#include <string>

#define	String std::string

#define	pinMode(x,y)
#define	digitalWrite(a,b)
#define	digitalRead(a)  a
#define	randomSeed(x)

#define	PROGMEM	
#define	F(X)	 X
#define	PSTR(X)	 X
#define FPSTR(X) X
#define PGM_P    const char *
#define	__FlashStringHelper char
#define strlen_P  strlen
#define strcpy_P  strcpy
#define strcmp_P  strcmp
#define strncmp_P  strncmp
#define strspn_P  strspn

#define LSBFIRST 0
#define MSBFIRST 1

// normally in cores/esp8266/flash_utils.h
#define FLASH_SECTOR_SIZE       4096

#define	OUTPUT	1
#define	HIGH	1
#define	A0	0
#define	pgm_read_byte(a)	(*(a))
#define	pgm_read_word(a)	(*(a))
#define	pgm_read_dword(a)	(*(a))
#define	pgm_read_float(a)	(*(a))

extern uint32_t millis(void);
extern long random(int max);
extern void delay (uint32_t ms);
extern uint16_t analogRead(int pin);
extern void setup(void);
extern void loop(void);
extern char **our_argv;
extern char our_make[];
extern std::string our_dir;

#include "ESP.h"
#include "Serial.h"


// glue with parent program
extern void setX11FullScreen (bool);
extern void setDemoMode(bool on);
extern void setCenterLng (int16_t l);
extern void fatalError (const char *fmt, ...);
extern const char *svr_host;
extern int svr_port;
extern bool skip_skip;
extern bool init_iploc;
extern const char *init_locip;



#endif // _ARDUINO_H
