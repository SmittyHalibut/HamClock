#ifndef _SERIAL_H
#define _SERIAL_H

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#include "Arduino.h"

class Serial {

    public:

	void begin (int baud);
        void print (void);
	void print (char *s);
	void print (const char *s);
	void print (int i);
	void print (String s);
        void println (void);
	void println (char *s);
	void println (const char *s);
	void println (int i);
	int printf (const char *fmt, ...);
	operator bool();

};

extern class Serial Serial;

#endif // _SERIAL_H
