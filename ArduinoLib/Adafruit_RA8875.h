/* this is the same as Adafruit_RA8875 but runs on Rasp Pi using /dev/fb0 or any UNIX using X Windows.
 * N.B. we only remimplented the functions we use, we may have missed a few.
 */

#ifndef _Adafruit_RA8875_H
#define _Adafruit_RA8875_H

#define	PROGMEM

#include <stdint.h>
#include <pthread.h>

#ifdef _USE_X11

#include <sys/time.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

// simplest to just recreate the same fb structure
struct fb_var_screeninfo {
    int xres, yres;
};

#endif // _USE_X11

#ifdef _USE_FB0

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <linux/input.h>

#endif	// _USE_FB0

#include "gfxfont.h"
extern const GFXfont Courier_Prime_Sans6pt7b;


#ifndef RGB565
#define RGB565(R,G,B)   ((((uint16_t)(R) & 0xF8) << 8) | (((uint16_t)(G) & 0xFC) << 3) | ((uint16_t)(B) >> 3))
#endif

#define RGB565_R(c)     (((c) & 0xF800) >> 8)
#define RGB565_G(c)     (((c) & 0x07E0) >> 3)
#define RGB565_B(c)     (((c) & 0x001F) << 3)

#define	RGB1632(C16)	((((uint32_t)(C16)&0xF800)<<8) | (((uint32_t)(C16)&0x07E0)<<5) | (((C16)&0x001F)<<3))
#define	RGB3216(C32)	RGB565(((C32)>>16)&0xFF, ((C32)>>8)&0xFF, ((C32)&0xFF))

#define	RA8875_BLACK	RGB565(0,0,0)
#define	RA8875_WHITE	RGB565(255,255,255)
#define RA8875_RED	RGB565(255,0,0)
#define	RA8875_GREEN	RGB565(0,255,0)
#define	RA8875_BLUE	RGB565(0,0,255)
#define	RA8875_CYAN	RGB565(0,255,255)
#define	RA8875_MAGENTA	RGB565(255,0,255)
#define	RA8875_YELLOW	RGB565(255,255,0)

#define	RA8875_800x480 1
#define RA8875_PWM_CLK_DIV1024 1
#define	RA8875_MRWC 1


// choose 16 or 32 bit hw frame buffer
#if defined(_16BIT_FB)
typedef uint16_t fbpix_t;
#define BYTESPFBPIX     2
#define BITSPFBPIX      16
#define RGB16TOFBPIX(x) x
#define FBPIXTORGB16(x) x
#else
typedef uint32_t fbpix_t;
#define BYTESPFBPIX     4
#define BITSPFBPIX      32
#define RGB16TOFBPIX(x) RGB1632(x)
#define FBPIXTORGB16(x) RGB3216(x)
#endif

class Adafruit_RA8875 {

    public:

	Adafruit_RA8875(uint8_t CS, uint8_t RST);

	void displayOn (int o)
	{
	}

	void GPIOX (int x)
	{
	}

	void PWM1config(bool t, int x)
	{
	}

	void graphicsMode(void)
	{
	}


	void writeCommand (uint8_t c)
	{
	}

	void setRotation (int r)
	{
	    rotation = r;
	}

	void textSetCursor(uint16_t x, uint16_t y)
	{
	}

	void PWM1out(uint16_t bpwm)
	{
	}

	void touchEnable (bool b)
	{
	}

	bool begin (int x);
	uint16_t width(void);
	uint16_t height(void);
	void fillScreen (uint16_t color16);
	void setTextColor(uint16_t color16);
	void setCursor(uint16_t x, uint16_t y);
	void getTextBounds(char *string, int16_t x, int16_t y,
		int16_t *x1, int16_t *y1, uint16_t *w, uint16_t *h);
	void print (char c);
	void print (char *s);
	void print (const char *s);
	void print (int i, int base = 10);
	void print (float f, int p = 2);
	void print (long l);
	void println (void);
	void println (char *s);
	void println (const char *s);
	void println (int i, int base = 10);
	void setXY (int16_t x, int16_t y);
	uint16_t readData(void);
	void setFont (const GFXfont *f);
	int16_t getCursorX(void);
	int16_t getCursorY(void);
	bool touched(void);
	void touchRead (uint16_t *x, uint16_t *y);
	void drawPixel(int16_t x, int16_t y, uint16_t color16);
        void drawPixels(uint16_t * p, uint32_t count, int16_t x, int16_t y);
	void drawSubPixel(int16_t x, int16_t y, uint16_t color16);
	void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color16);
	void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t thickness, uint16_t color16);
	void drawRect(int16_t x0, int16_t y0, int16_t w, int16_t h, uint16_t color16);
	void fillRect(int16_t x0, int16_t y0, int16_t w, int16_t h, uint16_t color16);
	void drawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color16);
	void fillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color16);
	void drawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2,
	    uint16_t color16);
	void fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2,
	    uint16_t color16);

	// special method to draw hi res earth pixel
	void plotEarth (uint16_t x0, uint16_t y0, float lat0, float lng0,
            float dlatr, float dlngr, float dlatd, float dlngd, float fract_day);

        // methods to implement a protected rectangle drawn only with drawPR()
        void setPR (uint16_t x, uint16_t y, uint16_t w, uint16_t h);
        void drawPR(void);
        uint16_t pr_x, pr_y, pr_w, pr_h;
        volatile bool pr_draw;
	void drawCanvas(void);

	// real/app display size
	int SCALESZ;

        // get next keyboard character
        char getChar(void);

        // get current mouse position
        bool getMouse (uint16_t *x, uint16_t *y);

        void setEarthPix (char *day_pixels, char *night_pixels);

        // used to engage/disengage X11 fullscreen
        void X11OptionsEngageNow (bool fullscreen);

        // used to get total screen size
        void getScreenSize (int *w, int *h);

        // use to learn whether display is ready
        bool displayReady(void);

    protected:

	// 0: normal 2: 180 degs
	int rotation;

        // set when display is definitely up and running
        volatile bool ready;

	#define FB_CURSOR_W 16          // APP units
	int FB_CURSOR_SZ;

    private:

#if defined(_CLOCK_1600x960)

	#define FB_XRES 1600
	#define FB_YRES 960
	#define EARTH_BIG_W 1320
	#define EARTH_BIG_H 660

#elif defined(_CLOCK_2400x1440)

	#define FB_XRES 2400
	#define FB_YRES 1440
	#define EARTH_BIG_W 1980
	#define EARTH_BIG_H 990

#elif defined(_CLOCK_3200x1920)

	#define FB_XRES 3200
	#define FB_YRES 1920
	#define EARTH_BIG_W 2640
	#define EARTH_BIG_H 1320

#else   // original size

	#define FB_XRES 800
	#define FB_YRES 480
	#define EARTH_BIG_W 660
	#define EARTH_BIG_H 330

#endif

#ifdef _USE_X11

	Display *display;
	Window win;
        Visual *visual;
        int visdepth;
	GC black_gc;
	XImage *img;
	Pixmap pixmap;

        // used by X11OptionsEngageNow
        volatile bool options_engage, options_fullscreen;


#endif // _USE_X11

#ifdef _USE_FB0

	// mouse and/or touch screen is read in separate thread protected by mouse_lock
	static void *mouseThreadHelper(void *me);
	void mouseThread (void);
        void findMouse(void);
	int mouse_fd, touch_fd;

	// kb is read in separate thread protected by kb_lock
	static void *kbThreadHelper(void *me);
	void kbThread ();
        void findKeyboard(void);
        int kb_fd;

        void setCursorIfVis (uint16_t row, uint16_t col, fbpix_t color);

	int fb_fd;                      // frame buffer mmap file descriptor
	fbpix_t *fb_fb;                 // pointer to mmap fb
	fbpix_t *fb_cursor;             // temp image buffer for cursor overlay

#endif	// _USE_FB0

	pthread_mutex_t mouse_lock;
	volatile int16_t mouse_x, mouse_y;
	volatile int mouse_ups, mouse_downs;
	pthread_mutex_t kb_lock;
        char kb_cq[20];
        int kb_cqhead, kb_cqtail;

        struct timeval mouse_tv;
        int mouse_idle;
        #define MOUSE_FADE 5000         // ms

        // total display size
        volatile int screen_w, screen_h;

        /* for drawLineOverlap:
         * Overlap means drawing additional pixel when changing minor direction
         * Needed for drawThickLine, otherwise some pixels will be missing in the thick line
         */
        typedef enum {
            LINE_OVERLAP_NONE,  // No line overlap, like in standard Bresenham
            LINE_OVERLAP_MAJOR, // Overlap - first go major then minor direction. Pixel is drawn as extension after actual line
            LINE_OVERLAP_MINOR, // Overlap - first go minor then major direction. Pixel is drawn as extension before next line
            LINE_OVERLAP_BOTH   // Overlap - both
        } DLOverlap;

	// frame buffer is drawn in separate thread protected by fb_lock
        static void *fbThreadHelper(void *me);
        #define APP_WIDTH  800
        #define APP_HEIGHT 480
	void fbThread ();
	pthread_mutex_t fb_lock;
	struct fb_var_screeninfo fb_si;
	volatile bool fb_dirty;
	fbpix_t *fb_canvas;             // main drawing image buffer
	fbpix_t *fb_stage;              // temp image during staging to fb hw
	int fb_nbytes;                  // bytes in each in-memory image buffer
        void drawLineOverlap (int16_t x0, int16_t y0, int16_t x1, int16_t y1, int8_t overlap, fbpix_t aColor);
        void drawThickLine (int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t thick, fbpix_t aColor);
	void plotLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, fbpix_t color);
        void plotLineLow(int16_t x0, int16_t y0, int16_t x1, int16_t y1, fbpix_t color);
        void plotLineHigh(int16_t x0, int16_t y0, int16_t x1, int16_t y1, fbpix_t color);
        void plotLineRaw(int16_t x0, int16_t y0, int16_t x1, int16_t y1, fbpix_t color);
	void plotfb (int16_t x, int16_t y, fbpix_t color);
	void plotChar (char c);
	fbpix_t text_color;
	uint16_t cursor_x, cursor_y;
	uint16_t read_x, read_y;
	bool read_msb, read_first;
	const GFXfont *current_font;
	int FB_X0;
	int FB_Y0;

	// big earth mmap'd maps
        uint16_t (*DEARTH_BIG)[EARTH_BIG_H][EARTH_BIG_W];
        uint16_t (*NEARTH_BIG)[EARTH_BIG_H][EARTH_BIG_W];

};

#endif // _Adafruit_RA8875_H
