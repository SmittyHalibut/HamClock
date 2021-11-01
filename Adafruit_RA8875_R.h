/* this is the same interface as Adafruit_RA8875 with two differences:
 *   on ESP8266 it implements rotation correctly.
 *   on UNIX systems it can draw on X11 or RPi /dev/fb0.
 *
 * N.B. we only remimplented the functions we use, we don't claim this works with everything.
 */

#ifndef _Adafruit_RA8875_R_H
#define _Adafruit_RA8875_R_H

#include <stdarg.h>

#include <Adafruit_GFX.h>
#include <Adafruit_RA8875.h>

class Adafruit_RA8875_R : public Adafruit_RA8875 {

    public:

	Adafruit_RA8875_R(uint8_t CS, uint8_t RST) : Adafruit_RA8875::Adafruit_RA8875(CS, RST)
	{
	}

	void textSetCursor(uint16_t x, uint16_t y)
	{
	    if (rotation == 2) {
		x = width() - 1 - x;
		y = height() - 1 - y;
	    }
	    Adafruit_RA8875::textSetCursor(x, y);
	}

	void printf (const char *fmt, ...)
	{
	    char line[1024];
	    va_list ap;
	    va_start (ap, fmt);
	    vsnprintf (line, sizeof(line), fmt, ap);
	    va_end (ap);
	    Adafruit_RA8875::print(line);
	}

	void drawPixel(int16_t x, int16_t y, uint16_t color)
	{
	    if (rotation == 2) {
		x = width() - 1 - x;
		y = height() - 1 - y;
	    }
	    Adafruit_RA8875::drawPixel(x, y, color);
	}

	void drawPixels (uint16_t *p, uint32_t count, int16_t x, int16_t y)
	{
	    if (rotation == 2) {
		x = width() - 1 - x - (count-1);
		y = height() - 1 - y;
	    }
	    Adafruit_RA8875::drawPixels (p, count, x, y);
	}

	void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color)
	{
	    if (rotation == 2) {
		x0 = width()  - 1 - x0;
		y0 = height() - 1 - y0;
		x1 = width()  - 1 - x1;
		y1 = height() - 1 - y1;
	    }
	    Adafruit_RA8875::drawLine(x0, y0, x1, y1, color);
	}

        // non-standard
	void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t thickness, uint16_t color)
	{
	    if (rotation == 2) {
		x0 = width()  - 1 - x0;
		y0 = height() - 1 - y0;
		x1 = width()  - 1 - x1;
		y1 = height() - 1 - y1;
	    }
            #if defined (_IS_ESP8266)
                (void)thickness;
                Adafruit_RA8875::drawLine(x0, y0, x1, y1, color);
            #else
                Adafruit_RA8875::drawLine(x0, y0, x1, y1, thickness, color);
            #endif
	}

	void drawRect(int16_t x0, int16_t y0, int16_t w, int16_t h, uint16_t color)
	{
	    if (rotation == 2) {
		x0 = width()  - 1 - x0;
		y0 = height() - 1 - y0;
		w = -w;
		h = -h;
	    }
	    Adafruit_RA8875::drawRect(x0, y0, w, h, color);
	}

	void fillRect(int16_t x0, int16_t y0, int16_t w, int16_t h, uint16_t color)
	{
	    if (rotation == 2) {
		x0 = width()  - 1 - x0;
		y0 = height() - 1 - y0;
		w = -w;
		h = -h;
	    }
	    Adafruit_RA8875::fillRect(x0, y0, w, h, color);
	}

	void drawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color)
	{
	    if (rotation == 2) {
		x0 = width() - 1 - x0;
		y0 = height() - 1 - y0;
	    }
	    Adafruit_RA8875::drawCircle(x0, y0, r, color);
	}

	void fillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color)
	{
	    if (rotation == 2) {
		x0 = width() - 1 - x0;
		y0 = height() - 1 - y0;
	    }
	    Adafruit_RA8875::fillCircle(x0, y0, r, color);
	}

	void drawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color)
	{
	    if (rotation == 2) {
		x0 = width()  - 1 - x0;
		y0 = height() - 1 - y0;
		x1 = width()  - 1 - x1;
		y1 = height() - 1 - y1;
		x2 = width()  - 1 - x2;
		y2 = height() - 1 - y2;
	    }
	    Adafruit_RA8875::drawTriangle(x0, y0, x1, y1, x2, y2, color);
	}

	void fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color)
	{
	    if (rotation == 2) {
		x0 = width()  - 1 - x0;
		y0 = height() - 1 - y0;
		x1 = width()  - 1 - x1;
		y1 = height() - 1 - y1;
		x2 = width()  - 1 - x2;
		y2 = height() - 1 - y2;
	    }
	    Adafruit_RA8875::fillTriangle(x0, y0, x1, y1, x2, y2, color);
	}

        // non-standard
        bool getMouse (uint16_t *x, uint16_t *y)
        {
            #if defined (_IS_ESP8266)
                (void)x;
                (void)y;
                return (false);
            #else
                return (Adafruit_RA8875::getMouse(x,y));
            #endif
        }


#if defined (_IS_ESP8266)

        // stubs for ESP Arduino

        void setPR (uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
            (void)(x);
            (void)(y);
            (void)(w);
            (void)(h);
        }
        void drawPR(void) {}
        void X11OptionsEngageNow (bool fullscreen) {
            (void)(fullscreen);
        }
        void getScreenSize (int *w, int *h) {
            *w = 800;
            *h = 480;
        }
        bool displayReady() {
            return (true);
        }
        char getChar(void) {
            return (0);
        }
        int SCALESZ = 1;
	void drawSubPixel(int16_t x, int16_t y, uint16_t color) {
            drawPixel(x, y, color);
        }

#endif

};

#endif // _Adafruit_RA8875_R_H
