/* simple Serial.cpp
 */

#include "Arduino.h"
#include "Serial.h"


void Serial::begin (int baud)
{
    (void) baud;
}

void Serial::print (void)
{
}

void Serial::print (char *s)
{
    printf ("%s", s);
}

void Serial::print (const char *s)
{
    printf ("%s", s);
}

void Serial::print (int i)
{
    printf ("%d", i);
}

void Serial::print (String s)
{
    printf ("%s", s.c_str());
}

void Serial::println (void)
{
    printf ("\n");
}

void Serial::println (char *s)
{
    printf ("%s\n", s);
}

void Serial::println (const char *s)
{
    printf ("%s\n", s);
}

void Serial::println (int i)
{
    printf ("%d\n", i);
}

int Serial::printf (const char *fmt, ...)
{
    va_list ap;
    va_start (ap, fmt);
    int n = vprintf (fmt, ap);
    va_end (ap);
    return (n);
}

Serial::operator bool()
{
    return (true);
}



class Serial Serial;
