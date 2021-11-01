/* This class has much the same interface as Adafruit_RA0 but provides a porting layer for:
 *
 * #ifdef _USE_FB0
 *   linux frame buffer
 *   draws to /dev/fb0 and looks through /dev/input/events* for mouse or touch screen.
 *   uses two supporting threads, one to update the fb and one to read the mouse.
 * #ifdef _USE_X11
 *   X11 client
 *   draws to an X11 window.
 *   uses one supporting thread to manage the X11 display connection and input.
 *
 * Both systems use a memory array named fb_canvas as a pixel-by-pixel rendering surface. This is
 * periodically copied to fb_stage on change. _USE_FB0 uses a third copy fb_cursor in which to draw cursor.
 * FB_X0 and FB_Y0 are the upper left coords on the hardware of drawing area FB_YRES x FB_XRES.
 *
 * Earth map pixels area mmap'd from local day and night files.
 * 
 * This class assumes the original ESP Arduino code was drawing onto a canvas 800w x 480h, set by APP_WIDTH
 * and APP_HEIGHT. If it weren't for fonts and the Earth map this could be scaled rather easily to any size.
 * Since resizing is not really possible, the display surface must be at least this large. Larger sizes
 * are accommodated by centering the scene with a black surround.
 * 
 */



#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <math.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/mman.h>

#include "Adafruit_RA8875.h"


#ifdef _USE_FB0

/* We make our own version of system which works better when we are running suid-root.
 * this is just used for ad-hoc solutions to issues encountered using /dev/fb0
 */

#include <stdarg.h>
#include <sys/wait.h>

/* fork/exec /bin/sh to run command
 */
static void ourSystem (const char *fmt, ...)
{
        // expand full command
        char cmd[1000];
        va_list ap;
        va_start (ap, fmt);
        vsnprintf (cmd, sizeof(cmd), fmt, ap);
        va_end (ap);

        printf ("Running: %s\n", cmd);

        // create pipe for parent to read from child
        int pipe_fd[2];
        if (pipe (pipe_fd) < 0) {
	    printf ("pipe(2) failed: %s", strerror(errno));
            return;
        }

        // start new process as clone of us
        int child_pid = fork();
        if (child_pid < 0) {
	    printf ("fork(2) failed: %s", strerror(errno));
            return;
        }

        // now two processes running concurrently

        if (child_pid == 0) {
            // child

            // engage full perm
            setuid (geteuid());

            // arrange stdout/err to write into pipe_fd[1] to parent
            dup2 (pipe_fd[1], 1);
            dup2 (pipe_fd[1], 2);

            // don't need read end of pipe
            close (pipe_fd[0]);

            // overlay with new image
            execl ("/bin/sh", "sh", "-c", cmd, NULL);

            printf ("Can not exec %s: %s\n", cmd, strerror(errno));
            _exit(1);
        }

        // parent arranges to read from pipe_fd[0] from child until EOF
        FILE *rsp_fp = fdopen (pipe_fd[0], "r");
        close (pipe_fd[1]);
	char rsp[1000];
        rsp[0] = 0;
        while (fgets (rsp, sizeof(rsp), rsp_fp))
	    printf ("%s", rsp);

        // finished with pipe
        fclose (rsp_fp);        // also closes(pipe_fd[0])

        // parent waits for child
        int wstatus;
        if (waitpid (child_pid, &wstatus, 0) < 0) {
	    printf ("waitpid(2) failed: %s", strerror(errno));
            return;
        }

        // finished, report any error status
	if (!WIFEXITED(wstatus) || WEXITSTATUS(wstatus) != 0) {
	    printf ("FAIL: %s", rsp);
	    return;
	}

        printf ("cmd ok\n");
}

#endif // _USE_FB0


/* not used, just here for compatibility
 */
uint32_t spi_speed;



Adafruit_RA8875::Adafruit_RA8875(uint8_t CS, uint8_t RST)
{
	// emulate a bug in the real RA8875 whereby the very first pixel read back is bogus
	read_first = true;

        // init the protected region flag
        pr_draw = false;

        // insure earth map pointers are NULL until set
        DEARTH_BIG = NULL;
        NEARTH_BIG = NULL;

        // not ready until proven
        ready = false;

        // reset until known
        screen_w = screen_h = 0;
}

void Adafruit_RA8875::setEarthPix (char *day_pixels, char *night_pixels)
{
        DEARTH_BIG = (uint16_t(*)[EARTH_BIG_H][EARTH_BIG_W]) day_pixels;
        NEARTH_BIG = (uint16_t(*)[EARTH_BIG_H][EARTH_BIG_W]) night_pixels;

}

bool Adafruit_RA8875::begin (int not_used)
{
        (void)not_used;

#ifdef _USE_X11

        // mostly in 2nd thread but a few queries from this one
        XInitThreads();

	// connect to X server
        display = XOpenDisplay(NULL);
	if (!display) {
	    printf ("Can not open X Windows display\n");
	    exit(1);
	}
	Screen *screen = XDefaultScreenOfDisplay (display);
        int screen_num = XScreenNumberOfScreen(screen);
        Window root = RootWindow(display,screen_num);
	unsigned long black_pixel = BlackPixelOfScreen (screen);

	// require TrueColor visual so we can use fb_canvas directly in img but try various depths
        XVisualInfo vinfo;
#if defined(_16BIT_FB)
        // only 16 will work
        if (!XMatchVisualInfo(display, screen_num, 16, TrueColor, &vinfo)) {
            printf ("16 bit TrueColor visual not found\n");
            exit(1);
        }
        visdepth = 16;
#else
        // try both 24 and 32
        if (XMatchVisualInfo(display, screen_num, 24, TrueColor, &vinfo)) {
            printf ("Found 24 bit TrueColor visual\n");
            visdepth = 24;
        } else if (XMatchVisualInfo(display, screen_num, 32, TrueColor, &vinfo)) {
            printf ("Found 32 bit TrueColor visual\n");
            visdepth = 32;
        } else {
            printf ("Neither 24 nor 32 bit TrueColor visual found\n");
            exit(1);
        }
#endif

        visual = vinfo.visual;

	// set initial scale to match, FB_X/Y0 can change to stay centered if window size changes
	fb_si.xres = FB_XRES;
	fb_si.yres = FB_YRES;
        SCALESZ = FB_XRES / APP_WIDTH;
        FB_CURSOR_SZ = FB_CURSOR_W*SCALESZ;
        FB_X0 = 0;
        FB_Y0 = 0;
        fb_nbytes = FB_XRES * FB_YRES * BYTESPFBPIX;

	// get memory for canvas where the drawing methods update their pixels
	fb_canvas = (fbpix_t *) malloc (fb_nbytes);
	if (!fb_canvas) {
	    printf ("Can not malloc(%d) for canvas\n", fb_nbytes);
	    exit(1);
	}
	memset (fb_canvas, 0, fb_nbytes);       // black

	// get memory for the staging area used to find dirty pixels
	fb_stage = (fbpix_t *) malloc (fb_nbytes);
	if (!fb_stage) {
	    printf ("Can not malloc(%d) for stage\n", fb_nbytes);
	    exit(1);
	}
	memset (fb_stage, 1, fb_nbytes);        // unlikely color

	// create XImage using staging area
	img = XCreateImage(display, visual, visdepth, ZPixmap, 0, (char*)fb_stage, FB_XRES, FB_YRES,
                BITSPFBPIX, 0);

	// create window with initial size, user might resize later
	XSetWindowAttributes wa;
	wa.bit_gravity = NorthWestGravity;
	wa.background_pixel = black_pixel;
	unsigned long value_mask = CWBitGravity | CWBackPixel;
        win = XCreateWindow(display, root, 0, 0, fb_si.xres, fb_si.yres, 0, visdepth, InputOutput,
                visual, value_mask, &wa);

	// create a black GC for this visual
	XGCValues gcv;
	gcv.foreground = black_pixel;
        black_gc = XCreateGC (display, win, GCForeground, &gcv);

	// create off-screen pixmap for smoother double-buffering
	pixmap = XCreatePixmap (display, win, FB_XRES, FB_YRES, visdepth);

	// init with black for first expose
	XFillRectangle (display, pixmap, black_gc, 0, 0, FB_XRES, FB_YRES);

	// set initial and min size
        XSizeHints* win_size_hints = XAllocSizeHints();
	win_size_hints->flags = PSize | PMinSize;
        win_size_hints->base_width = FB_XRES;
        win_size_hints->base_height = FB_YRES;
        win_size_hints->min_width = FB_XRES;
        win_size_hints->min_height = FB_YRES;
        XSetWMNormalHints(display, win, win_size_hints);
        XFree(win_size_hints);

	// set titles
        XTextProperty window_name_property;
        XTextProperty icon_name_property;
        char *window_name = (char*)"HamClock";
        XStringListToTextProperty(&window_name, 1, &window_name_property);
        XStringListToTextProperty(&window_name, 1, &icon_name_property);
        XSetWMName(display, win, &window_name_property);
        XSetWMIconName(display, win, &icon_name_property);

	// enable desired X11 events
        XSelectInput (display, win, KeyPressMask | KeyReleaseMask | PointerMotionMask | LeaveWindowMask
                | ButtonReleaseMask | ButtonPressMask | ExposureMask | StructureNotifyMask);

	// prep for mouse and keyboard info
	if (pthread_mutex_init (&mouse_lock, NULL)) {
	    printf ("mouse_lock: %s\n", strerror(errno));
	    exit(1);
	}
	mouse_downs = mouse_ups = 0;
	if (pthread_mutex_init (&kb_lock, NULL)) {
	    printf ("kb_lock: %s\n", strerror(errno));
	    exit(1);
	}
	kb_cqhead = kb_cqtail = 0;

	// set up a reentrantable lock for fb
	pthread_mutexattr_t fb_attr;
	pthread_mutexattr_init (&fb_attr);
	pthread_mutexattr_settype (&fb_attr, PTHREAD_MUTEX_RECURSIVE);
	if (pthread_mutex_init (&fb_lock, &fb_attr)) {
	    printf ("fb_lock: %s\n", strerror(errno));
	    exit(1);
	}

	// start with default font
	current_font = &Courier_Prime_Sans6pt7b;

	// start X11 thread
	pthread_t tid;
	int e = pthread_create (&tid, NULL, fbThreadHelper, this);
	if (e) {
	    printf ("fbThreadhelper: %s\n", strerror(e));
	    exit(1);
	}

	// everything is ready
	return (true);

#endif // _USE_X11

#ifdef _USE_FB0

	// try to disable some fb interference
	ourSystem ("sudo dmesg -n 1");

	// init for mouse thread
        mouse_fd = touch_fd = -1;

	// init mouse lock
	if (pthread_mutex_init (&mouse_lock, NULL)) {
	    printf ("mouse_lock: %s\n", strerror(errno));
	    exit(1);
	}
	mouse_downs = mouse_ups = 0;

	// start mouse thread
	pthread_t tid;
	int e = pthread_create (&tid, NULL, mouseThreadHelper, this);
	if (e) {
	    printf ("mouseThreadhelper: %s\n", strerror(e));
	    exit(1);
	}

	// init kb lock
	if (pthread_mutex_init (&kb_lock, NULL)) {
	    printf ("kb_lock: %s\n", strerror(errno));
	    exit(1);
	}
        kb_cqhead = kb_cqtail = 0;

	// init for kb thread
        kb_fd = -1;

        // start kb thread
        e = pthread_create (&tid, NULL, kbThreadHelper, this);
        if (e) {
            printf ("kbThreadhelper: %s\n", strerror(e));
            exit(1);
        }


	// connect to frame buffer
        const char fb_path[] = "/dev/fb0";
        fb_fd = open(fb_path, O_RDWR);
	if (fb_fd < 0) {
	    printf ("%s: %s\n", fb_path, strerror(errno));
	    close(fb_fd);
	    exit(1);
	}
        if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &fb_si) < 0) {
	    printf ("FBIOGET_VSCREENINFO: %s\n", strerror(errno));
	    close(fb_fd);
	    exit(1);
	}
	printf ("fb0 is %d x %d x %d\n", fb_si.xres, fb_si.yres, fb_si.bits_per_pixel);
	if (fb_si.xres < FB_XRES || fb_si.yres < FB_YRES || fb_si.bits_per_pixel != BITSPFBPIX) {
	    printf ("Sorry, frame buffer must be at least %u x %u with %u bits per pixel\n",
				FB_XRES, FB_YRES, BITSPFBPIX);
	    exit(1);
	}

	// set scale, borders and initial mouse
        SCALESZ = FB_XRES / APP_WIDTH;
        FB_CURSOR_SZ = FB_CURSOR_W*SCALESZ;
        FB_X0 = (fb_si.xres - FB_XRES)/2;
        FB_Y0 = (fb_si.yres - FB_YRES)/2;
	mouse_x = FB_X0;
	mouse_y = FB_Y0;

	// map fb to our address space
        size_t si_bytes = BYTESPFBPIX * fb_si.xres * fb_si.yres;
        fb_fb = (fbpix_t*) mmap (NULL, si_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
	if (!fb_fb) {
	    printf ("mmap(%u): %s\n", si_bytes, strerror(errno));
	    close (fb_fd);
	    exit(1);
	}

	// initial clear
	memset (fb_fb, 0, si_bytes);

	// make backing buffers
        fb_nbytes = FB_XRES * FB_YRES * sizeof(*fb_canvas);
	fb_canvas = (fbpix_t *) malloc (fb_nbytes);
	if (!fb_canvas) {
	    printf ("Can not malloc(%d) for canvas\n", fb_nbytes);
	    close(fb_fd);
	    exit(1);
	}
	memset (fb_canvas, 0, fb_nbytes);       // black
	fb_stage = (fbpix_t *) malloc (fb_nbytes);
	fb_cursor = (fbpix_t *) malloc (fb_nbytes);
	if (!fb_stage || !fb_cursor) {
	    printf ("Can not malloc(%d) for stage or cursor\n", fb_nbytes);
	    close(fb_fd);
	    exit(1);
	}
	memset (fb_stage, 1, fb_nbytes);        // unlikely color

	// set up a reentrantable lock
	pthread_mutexattr_t fb_attr;
	pthread_mutexattr_init (&fb_attr);
	pthread_mutexattr_settype (&fb_attr, PTHREAD_MUTEX_RECURSIVE);
	if (pthread_mutex_init (&fb_lock, &fb_attr)) {
	    printf ("fb_lock: %s\n", strerror(errno));
	    close(fb_fd);
	    exit(1);
	}

	// start with default font
	current_font = &Courier_Prime_Sans6pt7b;

	// start fb thread
	e = pthread_create (&tid, NULL, fbThreadHelper, this);
	if (e) {
	    printf ("fbThreadhelper: %s\n", strerror(e));
	    close(fb_fd);
	    exit(1);
	}

        // capture screen size
        screen_w = fb_si.xres;
        screen_h = fb_si.yres;

	// everything is ready
	return (true);

#endif // _USE_FB0
}

bool Adafruit_RA8875::displayReady()
{
        return (ready);
}

uint16_t Adafruit_RA8875::width(void)
{
	// size of original app
	return (APP_WIDTH);
}

uint16_t Adafruit_RA8875::height(void)
{
	// size of original app
	return (APP_HEIGHT);
}

void Adafruit_RA8875::fillScreen (uint16_t color16)
{
	fillRect(0, 0, width(), height(), color16);
}

void Adafruit_RA8875::setTextColor(uint16_t color16)
{
	text_color = RGB16TOFBPIX(color16);
}

void Adafruit_RA8875::setCursor(uint16_t x, uint16_t y)
{
	// convert app coords to fb
	cursor_x = SCALESZ*x;
	cursor_y = SCALESZ*y;
}

void Adafruit_RA8875::getTextBounds(char *string, int16_t x, int16_t y,
    int16_t *x1, int16_t *y1, uint16_t *w, uint16_t *h)
{
	uint16_t totw = 0;
	int16_t miny = 0, maxy = 0;
	char c;
	while ((c = *string++) != '\0') {
            if (c < current_font->first || c > current_font->last)
                continue;       // don't print so don't count
	    GFXglyph *gp = &current_font->glyph[c - current_font->first];
	    totw += gp->xAdvance;
	    if (gp->yOffset < miny)
		miny = gp->yOffset;
	    if (gp->yOffset + gp->height > maxy)
		maxy = gp->yOffset + gp->height;
	}

	*x1 = *y1 = 0;	// assume unused

	// return in app's coord system
	*w = totw/SCALESZ;
	*h = (maxy - miny)/SCALESZ;
}

void Adafruit_RA8875::print (char c)
{
	plotChar (c);
}

void Adafruit_RA8875::print (char *s)
{
	char c;
	while ((c = *s++) != '\0')
	    plotChar (c);
}

void Adafruit_RA8875::print (const char *s)
{
	char c;
	while ((c = *s++) != '\0')
	    plotChar (c);
}

void Adafruit_RA8875::print (int i, int b)
{
	char buf[32];
        const char *fmt = (b == 16 ? "%x" : "%d");
	int sl = snprintf (buf, sizeof(buf), fmt, i);
	for (int i = 0; i < sl; i++)
	    plotChar (buf[i]);
}

void Adafruit_RA8875::print (float f, int p)
{
	char buf[32];
	int sl = snprintf (buf, sizeof(buf), "%.*f", p, f);
	for (int i = 0; i < sl; i++)
	    plotChar (buf[i]);
}

void Adafruit_RA8875::print (long l)
{
	char buf[32];
	int sl = snprintf (buf, sizeof(buf), "%lu", l);
	for (int i = 0; i < sl; i++)
	    plotChar (buf[i]);
}

void Adafruit_RA8875::println (void)
{
	cursor_x = 0;
	cursor_y += current_font->yAdvance;
}

void Adafruit_RA8875::println (char *s)
{
	print(s);
	println();
}

void Adafruit_RA8875::println (const char *s)
{
	print(s);
	println();
}

void Adafruit_RA8875::println (int i, int b)
{
	print(i, b);
	println();
}

void Adafruit_RA8875::setXY (int16_t x, int16_t y)
{
	// save in RA8875 coords
	read_x = x;
	read_y = y;

	// start with MSB then alternate
	read_msb = true;
}

uint16_t Adafruit_RA8875::readData(void)
{
	// return the next pixel from RA8875; app is expecting full res.

	fbpix_t fbpix = fb_stage[read_y*FB_XRES + read_x];
	uint16_t p16 = FBPIXTORGB16(fbpix);
	if (read_msb) {
	    read_msb = false;
	    return (p16 >> 8);
	} else {
	    read_msb = true;
	    if (read_first)
		// supply bogus pixel on first read
		read_first = false;
	    else {
		if (++read_x == FB_XRES) {
		    read_x = 0;
		    read_y++;
		}
	    }
	    return (p16 & 0xff);
	}
}

void Adafruit_RA8875::setFont (const GFXfont *f)
{
	if (f)
	    current_font = f;
	else
	    current_font = &Courier_Prime_Sans6pt7b;
}

int16_t Adafruit_RA8875::getCursorX(void)
{
	// return in app's coord system
	return (cursor_x/SCALESZ);
}

int16_t Adafruit_RA8875::getCursorY(void)
{
	// return in app's coord system
	return (cursor_y/SCALESZ);
}

/* returns whether mouse is down, if so touchRead is called to capture coordinates.
 * must allow for multiple mouse events between each call and unwind one up/down at a time.
 * algorithm determined from several representative state sequences:
 *
 *     mouse            before touched()           touched()        after touched()
 *     event        mups             mdowns         action       mups'          mdowns'
 *     
 *                   0                 0              0           0             0
 *       v           0                 1              1           0             1
 *                   0                 1              1           0             1
 *                   0                 1              1           0             1
 *                   0                 1              1           -
 *       ^           1                 1              1 d--       1             0
 *                   1                 0              0 u--       0             0
 *     
 *     
 *                   0                 0              0           0             0
 *       v^          1                 1              1 d--       1             0
 *                   1                 0              0 u--       0             0
 *     
 *     
 *                   0                 0              0           0             0
 *       v           0                 1              1           0             1
 *                   0                 1              1           0             1
 *       ^v          1                 2              0 d-- u--   0             1
 *                   0                 1              1           0             1
 *                   0                 1              1           0             1
 *       ^           1                 1              1 d--       1             0
 *                   1                 0              0 u--       0             0
 *     
 *                   0                 0              0           0             0
 *       v           0                 1              1           0             1
 *                   0                 1              1           0             1
 *       ^v^         2                 2              1 d--       2             1
 *                   2                 1              0 u--       1             1
 *                   1                 1              1 d--       1             0
 *                   1                 0              0 u--       0             0
 *           
 *           
 *           
 * summary:
 *
 *               U  D     T 
 *               -  -     ---------
 *               0  0  -> 0
 *               0  1  -> 1
 *               1  0  -> 0 u--
 *               1  1  -> 1 d--
 *               1  2  -> 0 d-- u--
 *               2  1  -> 0 u--
 *               2  2  -> 1 d--
 *           
 * pseudo-code:
 *
 *               if (U > D)
 *                   u--
 *                   report = 0
 *               else if (U > 0)
 *                   if (U == D)
 *                       d--
 *                       report = 1
 *                   else
 *                       d--
 *                       u--
 *                       report = 0
 *               else
 *                   report = D
 *
 * implementation:
 *
 *       when touched() returns true: don't modify counters so they remain stable for multiple calls,
 *             update the counters in touchRead()
 *
 *       while touched returns false: modify counters immmediately because touchRead() is never called.
 *
 */
bool Adafruit_RA8875::touched(void)
{
	bool report_down;

	pthread_mutex_lock(&mouse_lock);

            if (mouse_ups > mouse_downs) {
                // absorb and report one up event
                mouse_ups--;
                report_down = false;
            } else if (mouse_ups > 0) {
                if (mouse_ups == mouse_downs) {
                    // report one down event, absorb in touchRead()
                    report_down = true;
                } else {
                    // absorb one of each that cancel out
                    report_down = false;
                    mouse_downs--;
                    mouse_ups--;
                }
            } else {
                // just follow hardware state
                report_down = mouse_downs > 0;
            }

            // if (report_down)
                // printf ("report %d  D %d U %d\n", report_down, mouse_downs, mouse_ups);

	pthread_mutex_unlock(&mouse_lock);

	return (report_down);
}

void Adafruit_RA8875::touchRead (uint16_t *x, uint16_t *y)
{
	// mouse is in fb_si coords return in app coords
	pthread_mutex_lock(&mouse_lock);
	    *x = (mouse_x-FB_X0)/SCALESZ;
	    *y = (mouse_y-FB_Y0)/SCALESZ;

            // printf ("touchRead D %d U %d -> ", mouse_downs, mouse_ups);

            if (mouse_ups > mouse_downs) {
                // absorbed one up event in touched()
            } else if (mouse_ups > 0) {
                if (mouse_ups == mouse_downs) {
                    // absorb one down event
                    mouse_downs--;
                } else {
                    // absorbed one of each that cancel out in touched()
                }
            } else {
                // retain hw state
            }

            // printf ("D %d U %d\n", mouse_downs, mouse_ups);

	pthread_mutex_unlock(&mouse_lock);
}

/* get mouse location in app coords.
 * return whether currently within app and cursor visible.
 */
bool Adafruit_RA8875::getMouse (uint16_t *x, uint16_t *y)
{
	pthread_mutex_lock(&mouse_lock);

            bool ok = mouse_idle <= MOUSE_FADE && mouse_x >= 0;
            if (ok) {
                *x = (mouse_x-FB_X0)/SCALESZ;
                *y = (mouse_y-FB_Y0)/SCALESZ;
            }

	pthread_mutex_unlock(&mouse_lock);

        return (ok);
}

void Adafruit_RA8875::drawPixel(int16_t x, int16_t y, uint16_t color16)
{
	fbpix_t fbpix = RGB16TOFBPIX(color16);
	x *= SCALESZ;
	y *= SCALESZ;
	pthread_mutex_lock(&fb_lock);
	    if (SCALESZ == 2) {
		plotfb (x, y, fbpix);
		plotfb (x, y+1, fbpix);
		plotfb (x+1, y, fbpix);
		plotfb (x+1, y+1, fbpix);
	    } else {
		for (uint8_t dx = 0; dx < SCALESZ; dx++)
		    for (uint8_t dy = 0; dy < SCALESZ; dy++)
			plotfb (x+dx, y+dy, fbpix);
	    }
	    fb_dirty = true;
	pthread_mutex_unlock (&fb_lock);
}

void Adafruit_RA8875::drawPixels (uint16_t * p, uint32_t count, int16_t x, int16_t y)
{
        while (count-- > 0)
            drawPixel (x++, y, *p++);
}

/* location is fb coord system
 */
void Adafruit_RA8875::drawSubPixel(int16_t x, int16_t y, uint16_t color16)
{
	fbpix_t fbpix = RGB16TOFBPIX(color16);
	pthread_mutex_lock(&fb_lock);
	    plotfb (x, y, fbpix);
	    fb_dirty = true;
	pthread_mutex_unlock (&fb_lock);
}

/* always draws 1-pixel wide in screen pixels
 */
void Adafruit_RA8875::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color16)
{
	fbpix_t fbpix = RGB16TOFBPIX(color16);
	x0 *= SCALESZ;
	y0 *= SCALESZ;
	x1 *= SCALESZ;
	y1 *= SCALESZ;
	pthread_mutex_lock(&fb_lock);
	    plotLine (x0, y0, x1, y1, fbpix);
	    fb_dirty = true;
	pthread_mutex_unlock (&fb_lock);
}

// non-standard -- width is in terms of app pixels, not screen pixels
void Adafruit_RA8875::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t thickness,
uint16_t color16)
{
	fbpix_t fbpix = RGB16TOFBPIX(color16);
	x0 *= SCALESZ;
	y0 *= SCALESZ;
	x1 *= SCALESZ;
	y1 *= SCALESZ;
        thickness *= SCALESZ;
	pthread_mutex_lock(&fb_lock);
            if (thickness == 1)
                plotLine (x0, y0, x1, y1, fbpix);
            else
                drawThickLine (x0, y0, x1, y1, thickness, fbpix);
	    fb_dirty = true;
	pthread_mutex_unlock (&fb_lock);
}

/* Adafruit's drawRect of width w draws from x0 through x0+w-1, ie, it draws w pixels wide and skips w-2
 */
void Adafruit_RA8875::drawRect(int16_t x0, int16_t y0, int16_t w, int16_t h, uint16_t color16)
{
	fbpix_t fbpix = RGB16TOFBPIX(color16);
	x0 *= SCALESZ;
	y0 *= SCALESZ;
	if (w == 0)
	    w = 1;
	if (h == 0)
	    h = 1;
        w -= 1;
        h -= 1;
	w *= SCALESZ;
	h *= SCALESZ;
	pthread_mutex_lock (&fb_lock);
	    plotLine (x0, y0, x0+w, y0, fbpix);
	    plotLine (x0+w, y0, x0+w, y0+h, fbpix);
	    plotLine (x0+w, y0+h, x0, y0+h, fbpix);
	    plotLine (x0, y0+h, x0, y0, fbpix);
	    fb_dirty = true;
	pthread_mutex_unlock (&fb_lock);
}

/* Adafruit's fillRect of width w draws from x0 through x0+w-1, ie, it draws w pixels wide
 */
void Adafruit_RA8875::fillRect(int16_t x0, int16_t y0, int16_t w, int16_t h, uint16_t color16)
{
	fbpix_t fbpix = RGB16TOFBPIX(color16);
	x0 *= SCALESZ;
	y0 *= SCALESZ;
	if (w == 0)
	    w = 1;
	if (h == 0)
	    h = 1;
	w *= SCALESZ;
	h *= SCALESZ;
	pthread_mutex_lock (&fb_lock);
	    for (uint16_t y = y0; y < y0+h; y++)
		for (uint16_t x = x0; x < x0+w; x++)
		    plotfb (x, y, fbpix);
	    fb_dirty = true;
	pthread_mutex_unlock (&fb_lock);
}

/* Adafruit's circle radius is counts beyond center, eg, radius 3 is 7 pixels wide
 */
void Adafruit_RA8875::drawCircle(int16_t x0, int16_t y0, int16_t r0, uint16_t color16)
{
	fbpix_t fbpix = RGB16TOFBPIX(color16);
	x0 *= SCALESZ;
	y0 *= SCALESZ;
	r0 *= SCALESZ;

        // scan a circle from radius r0-1/2 to r0+1/2 to include a whole pixel.
        // radius (r0+1/2)^2 = r0^2 + r0 + 1/4 so we use 2x everywhere to avoid floats
        uint32_t iradius2 = 4*r0*(r0 - 1) + 1;
        uint32_t oradius2 = 4*r0*(r0 + 1) + 1;
	pthread_mutex_lock (&fb_lock);
	    for (int32_t dy = -2*r0; dy <= 2*r0; dy += 2) {
                for (int32_t dx = -2*r0; dx <= 2*r0; dx += 2) {
                    uint32_t xy2 = dx*dx + dy*dy;
                    if (xy2 >= iradius2 && xy2 <= oradius2)
			plotfb (x0+dx/2, y0+dy/2, fbpix);
                }
            }
	    fb_dirty = true;
	pthread_mutex_unlock (&fb_lock);

}

/* Adafruit's circle radius is counts beyond center, eg, radius 3 is 7 pixels wide
 */
void Adafruit_RA8875::fillCircle(int16_t x0, int16_t y0, int16_t r0, uint16_t color16)
{
	fbpix_t fbpix = RGB16TOFBPIX(color16);
	x0 *= SCALESZ;
	y0 *= SCALESZ;
	r0 *= SCALESZ;

        // scan a circle of radius r0+1/2 to include whole pixel.
        // radius (r0+1/2)^2 = r0^2 + r0 + 1/4 so we use 2x everywhere to avoid floats
        uint32_t radius2 = 4*r0*(r0 + 1) + 1;
	pthread_mutex_lock (&fb_lock);
	    for (int32_t dy = -2*r0; dy <= 2*r0; dy += 2) {
                for (int32_t dx = -2*r0; dx <= 2*r0; dx += 2) {
                    uint32_t xy2 = dx*dx + dy*dy;
                    if (xy2 <= radius2)
			plotfb (x0+dx/2, y0+dy/2, fbpix);
                }
            }
	    fb_dirty = true;
	pthread_mutex_unlock (&fb_lock);
}

void Adafruit_RA8875::drawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2,
    uint16_t color16)
{
	fbpix_t fbpix = RGB16TOFBPIX(color16);
	x0 *= SCALESZ;
	y0 *= SCALESZ;
	x1 *= SCALESZ;
	y1 *= SCALESZ;
	x2 *= SCALESZ;
	y2 *= SCALESZ;
	pthread_mutex_lock (&fb_lock);
	    plotLine (x0, y0, x1, y1, fbpix);
	    plotLine (x1, y1, x2, y2, fbpix);
	    plotLine (x2, y2, x0, y0, fbpix);
	    fb_dirty = true;
	pthread_mutex_unlock (&fb_lock);
}

void Adafruit_RA8875::fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2,
    uint16_t color16)
{
	fbpix_t fbpix = RGB16TOFBPIX(color16);
	x0 *= SCALESZ;
	y0 *= SCALESZ;
	x1 *= SCALESZ;
	y1 *= SCALESZ;
	x2 *= SCALESZ;
	y2 *= SCALESZ;

	// THIS ONLY WORKS FOR EQUALATORAL POINTED UP WITH x0/y0 top, x1/y1 left x2/y2 right.
	// google for raster triangle, eg
	//     http://www.gabrielgambetta.com/computer-graphics-from-scratch/filled-triangles.html
	// TODO
	int dy = y1 - y0;
	int dx = x2 - x0;
	pthread_mutex_lock (&fb_lock);
	    for (int y = y0; y <= y1; y++) {
		int xleft = x0 - dx*(y-y0)/dy;
		int xrite = x0 + dx*(y-y0)/dy;
		plotLine (xleft, y, xrite, y, fbpix);
	    }
	    fb_dirty = true;
	pthread_mutex_unlock (&fb_lock);
}

/********************************************************************************************************
 *
 * supporting methods
 *
 */



/*
 * thickLine.cpp
 * Draw a solid line with thickness using a modified Bresenhams algorithm.
 *
 * @date 25.03.2013
 * @author Armin Joachimsmeyer
 *      Email:   armin.joachimsmeyer@gmail.com
 * @copyright LGPL v3 (http://www.gnu.org/licenses/lgpl.html)
 * @version 1.5.0
 *
 * https://raw.githubusercontent.com/ArminJo/STMF3-Discovery-Demos/master/lib/graphics/src/thickLine.cpp
 */


/**
 * Modified Bresenham draw(line) with optional overlap. Required for drawThickLine().
 * Overlap draws additional pixel when changing minor direction. For standard bresenham overlap, choose LINE_OVERLAP_NONE (0).
 *
 *  Sample line:
 *
 *    00+
 *     -0000+
 *         -0000+
 *             -00
 *
 *  0 pixels are drawn for normal line without any overlap
 *  + pixels are drawn if LINE_OVERLAP_MAJOR
 *  - pixels are drawn if LINE_OVERLAP_MINOR
 */
void Adafruit_RA8875::drawLineOverlap (int16_t x0, int16_t y0, int16_t x1, int16_t y1, int8_t overlap,
fbpix_t color)
{
    int16_t tDeltaX, tDeltaY, tDeltaXTimes2, tDeltaYTimes2, tError, tStepX, tStepY;

    if ((x0 == x1) || (y0 == y1)) {
        //horizontal or vertical line
        plotLineRaw (x0, y0, x1, y1, color);
    } else {
        //calculate direction
        tDeltaX = x1 - x0;
        tDeltaY = y1 - y0;
        if (tDeltaX < 0) {
            tDeltaX = -tDeltaX;
            tStepX = -1;
        } else {
            tStepX = +1;
        }
        if (tDeltaY < 0) {
            tDeltaY = -tDeltaY;
            tStepY = -1;
        } else {
            tStepY = +1;
        }
        tDeltaXTimes2 = tDeltaX << 1;
        tDeltaYTimes2 = tDeltaY << 1;
        //draw start pixel
        plotfb(x0, y0, color);
        if (tDeltaX > tDeltaY) {
            // start value represents a half step in Y direction
            tError = tDeltaYTimes2 - tDeltaX;
            while (x0 != x1) {
                // step in main direction
                x0 += tStepX;
                if (tError >= 0) {
                    if (overlap & LINE_OVERLAP_MAJOR) {
                        // draw pixel in main direction before changing
                        plotfb(x0, y0, color);
                    }
                    // change Y
                    y0 += tStepY;
                    if (overlap & LINE_OVERLAP_MINOR) {
                        // draw pixel in minor direction before changing
                        plotfb(x0 - tStepX, y0, color);
                    }
                    tError -= tDeltaXTimes2;
                }
                tError += tDeltaYTimes2;
                plotfb(x0, y0, color);
            }
        } else {
            tError = tDeltaXTimes2 - tDeltaY;
            while (y0 != y1) {
                y0 += tStepY;
                if (tError >= 0) {
                    if (overlap & LINE_OVERLAP_MAJOR) {
                        // draw pixel in main direction before changing
                        plotfb(x0, y0, color);
                    }
                    x0 += tStepX;
                    if (overlap & LINE_OVERLAP_MINOR) {
                        // draw pixel in minor direction before changing
                        plotfb(x0, y0 - tStepY, color);
                    }
                    tError -= tDeltaYTimes2;
                }
                tError += tDeltaXTimes2;
                plotfb(x0, y0, color);
            }
        }
    }
}

/**
 * Bresenham with thickness
 * No pixel missed and every pixel only drawn once!
 */
void Adafruit_RA8875::drawThickLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
int16_t thick, fbpix_t color)
{
    int16_t i, tDeltaX, tDeltaY, tDeltaXTimes2, tDeltaYTimes2, tError, tStepX, tStepY;

    if (thick <= 1) {
        drawLineOverlap(x0, y0, x1, y1, LINE_OVERLAP_NONE, color); 
    }

    /**
     * For coordinate system with 0.0 top left
     * Swap X and Y delta and calculate clockwise (new delta X inverted)
     * or counterclockwise (new delta Y inverted) rectangular direction.
     * The right rectangular direction for LINE_OVERLAP_MAJOR toggles with each octant
     */
    tDeltaY = x1 - x0;
    tDeltaX = y1 - y0;
    // mirror 4 quadrants to one and adjust deltas and stepping direction
    bool tSwap = true; // count effective mirroring
    if (tDeltaX < 0) {
        tDeltaX = -tDeltaX;
        tStepX = -1;
        tSwap = !tSwap;
    } else {
        tStepX = +1;
    }
    if (tDeltaY < 0) {
        tDeltaY = -tDeltaY;
        tStepY = -1;
        tSwap = !tSwap;
    } else {
        tStepY = +1;
    }
    tDeltaXTimes2 = tDeltaX << 1;
    tDeltaYTimes2 = tDeltaY << 1;
    bool tOverlap;
    // adjust for right direction of thickness from line origin
    int tDrawStartAdjustCount = thick / 2;

    // which octant are we now
    if (tDeltaX >= tDeltaY) {
        if (tSwap) {
            tDrawStartAdjustCount = (thick - 1) - tDrawStartAdjustCount;
            tStepY = -tStepY;
        } else {
            tStepX = -tStepX;
        }
        /*
         * Vector for draw direction of start of lines is rectangular and counterclockwise to main line direction
         * Therefore no pixel will be missed if LINE_OVERLAP_MAJOR is used on change in minor rectangular direction
         */
        // adjust draw start point
        tError = tDeltaYTimes2 - tDeltaX;
        for (i = tDrawStartAdjustCount; i > 0; i--) {
            // change X (main direction here)
            x0 -= tStepX;
            x1 -= tStepX;
            if (tError >= 0) {
                // change Y
                y0 -= tStepY;
                y1 -= tStepY;
                tError -= tDeltaXTimes2;
            }
            tError += tDeltaYTimes2;
        }
        //draw start line
        plotLineRaw(x0, y0, x1, y1, color);
        // draw thick number of lines
        tError = tDeltaYTimes2 - tDeltaX;
        for (i = thick; i > 1; i--) {
            // change X (main direction here)
            x0 += tStepX;
            x1 += tStepX;
            tOverlap = LINE_OVERLAP_NONE;
            if (tError >= 0) {
                // change Y
                y0 += tStepY;
                y1 += tStepY;
                tError -= tDeltaXTimes2;
                /*
                 * Change minor direction reverse to line (main) direction
                 * because of choosing the right (counter)clockwise draw vector
                 * Use LINE_OVERLAP_MAJOR to fill all pixel
                 *
                 * EXAMPLE:
                 * 1,2 = Pixel of first 2 lines
                 * 3 = Pixel of third line in normal line mode
                 * - = Pixel which will additionally be drawn in LINE_OVERLAP_MAJOR mode
                 *           33
                 *       3333-22
                 *   3333-222211
                 * 33-22221111
                 *  221111                     /\
				 *  11                          Main direction of start of lines draw vector
                 *  -> Line main direction
                 *  <- Minor direction of counterclockwise of start of lines draw vector
                 */
                tOverlap = LINE_OVERLAP_MAJOR;
            }
            tError += tDeltaYTimes2;
            drawLineOverlap(x0, y0, x1, y1, tOverlap, color);
        }
    } else {
        // the other octant
        if (tSwap) {
            tStepX = -tStepX;
        } else {
            tDrawStartAdjustCount = (thick - 1) - tDrawStartAdjustCount;
            tStepY = -tStepY;
        }
        // adjust draw start point
        tError = tDeltaXTimes2 - tDeltaY;
        for (i = tDrawStartAdjustCount; i > 0; i--) {
            y0 -= tStepY;
            y1 -= tStepY;
            if (tError >= 0) {
                x0 -= tStepX;
                x1 -= tStepX;
                tError -= tDeltaYTimes2;
            }
            tError += tDeltaXTimes2;
        }
        //draw start line
        plotLineRaw(x0, y0, x1, y1, color);
        // draw thick number of lines
        tError = tDeltaXTimes2 - tDeltaY;
        for (i = thick; i > 1; i--) {
            y0 += tStepY;
            y1 += tStepY;
            tOverlap = LINE_OVERLAP_NONE;
            if (tError >= 0) {
                x0 += tStepX;
                x1 += tStepX;
                tError -= tDeltaYTimes2;
                tOverlap = LINE_OVERLAP_MAJOR;
            }
            tError += tDeltaXTimes2;
            drawLineOverlap(x0, y0, x1, y1, tOverlap, color);
        }
    }
}

void Adafruit_RA8875::plotLineLow(int16_t x0, int16_t y0, int16_t x1, int16_t y1, fbpix_t color)
{
        int16_t dx = x1 - x0;
        int16_t dy = y1 - y0;
        int16_t yi = 1;

        if (dy < 0) {
            yi = -1;
            dy = -dy;
        }
        int16_t D = 2*dy - dx;
        int16_t y = y0;

        for (int16_t x = x0; x <= x1; x++) {
            plotfb(x,y,color);
            if (D > 0) {
                y = y + yi;
                D = D - 2*dx;
            }
            D = D + 2*dy;
        }
}

void Adafruit_RA8875::plotLineHigh(int16_t x0, int16_t y0, int16_t x1, int16_t y1, fbpix_t color)
{
        int16_t dx = x1 - x0;
        int16_t dy = y1 - y0;
        int16_t xi = 1;

        if (dx < 0) {
            xi = -1;
            dx = -dx;
        }
        int16_t D = 2*dx - dy;
        int16_t x = x0;

        for (int16_t y = y0; y <= y1; y++) {
            plotfb(x,y,color);
            if (D > 0) {
                x = x + xi;
                D = D - 2*dy;
            }
            D = D + 2*dx;
        }
}

/* plot line using Bresenham's algorithm.
 * https://en.wikipedia.org/wiki/Bresenham%27s_line_algorithm
 */
void Adafruit_RA8875::plotLineRaw(int16_t x0, int16_t y0, int16_t x1, int16_t y1, fbpix_t color)
{
        if (abs(y1 - y0) < abs(x1 - x0)) {
            if (x0 > x1)
                plotLineLow(x1, y1, x0, y0, color);
            else
                plotLineLow(x0, y0, x1, y1, color);
        } else {
            if (y0 > y1)
                plotLineHigh(x1, y1, x0, y0, color);
            else
                plotLineHigh(x0, y0, x1, y1, color);
        }
}


void Adafruit_RA8875::plotLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, fbpix_t color)
{
        plotLineRaw (x0, y0, x1, y1, color);
}

void Adafruit_RA8875::plotfb (int16_t x, int16_t y, fbpix_t color)
{
        fb_canvas[y*FB_XRES + x] = color;
}

/* plot hi res earth lat0,lng0 at app's screen location x0,y0.
 * we interpolate this to SCALESZxSCALESZ, knowing dlat and dlng going one full step right and down.
 * frac_day is 1 for all DEARTH, 0 for all NEARTH else blend
 */
void Adafruit_RA8875::plotEarth (uint16_t x0, uint16_t y0, float lat0, float lng0,
float dlatr, float dlngr, float dlatd, float dlngd, float fract_day)
{
        // beware of no map files
        if (!DEARTH_BIG || !NEARTH_BIG)
            return;

        // beware lng wrap across date line
        if (dlngr < -180) dlngr += 360;
        if (dlngd < -180) dlngd += 360;
        if (dlngr >  180) dlngr -= 360;
        if (dlngd >  180) dlngd -= 360;

        // scale app step size to our step size
        dlatr /= SCALESZ;
        dlngr /= SCALESZ;
        dlatd /= SCALESZ;
        dlngd /= SCALESZ;

        // ditto starting loc
	x0 *= SCALESZ;
	y0 *= SCALESZ;

	for (int r = 0; r < SCALESZ; r++) {
	    fbpix_t *frow = &fb_canvas[(y0+r)*FB_XRES + x0];
	    for (int c = 0; c < SCALESZ; c++) {
                float lat = lat0 + dlatr*c + dlatd*r;
                float lng = lng0 + dlngr*c + dlngd*r;
                int ex = (int)((lng+180)*EARTH_BIG_W/360 + EARTH_BIG_W + 0.5F);
                int ey = (int)((90-lat)*EARTH_BIG_H/180 + EARTH_BIG_H + 0.5F);
                ex = (ex + EARTH_BIG_W) % EARTH_BIG_W;
                ey = (ey + EARTH_BIG_H) % EARTH_BIG_H;
		uint16_t c16; 
		if (fract_day == 0) {
		    c16 = (*NEARTH_BIG)[ey][ex];
		} else if (fract_day == 1) {
		    c16 = (*DEARTH_BIG)[ey][ex];
		} else {
		    // blend from day to night
		    uint16_t day_pix = (*DEARTH_BIG)[ey][ex];
		    uint16_t night_pix = (*NEARTH_BIG)[ey][ex];
		    uint8_t day_r = RGB565_R(day_pix);
		    uint8_t day_g = RGB565_G(day_pix);
		    uint8_t day_b = RGB565_B(day_pix);
		    uint8_t night_r = RGB565_R(night_pix);
		    uint8_t night_g = RGB565_G(night_pix);
		    uint8_t night_b = RGB565_B(night_pix);
		    float fract_night = 1 - fract_day;
		    uint8_t twi_r = (fract_day*day_r + fract_night*night_r);
		    uint8_t twi_g = (fract_day*day_g + fract_night*night_g);
		    uint8_t twi_b = (fract_day*day_b + fract_night*night_b);
		    c16 = RGB565 (twi_r, twi_g, twi_b);
		}
		*frow++ = RGB16TOFBPIX(c16);
	    }
	}
}

void Adafruit_RA8875::plotChar (char ch)
{
	if (ch < current_font->first || ch > current_font->last)
	    return;     // don't print if don't count length
	GFXglyph *gp = &current_font->glyph[ch-current_font->first];
	uint8_t *bp = &current_font->bitmap[gp->bitmapOffset];
	int16_t x = cursor_x + gp->xOffset;
	int16_t y = cursor_y + gp->yOffset;
	uint16_t bitn = 0;
	pthread_mutex_lock (&fb_lock);
	    for (uint16_t r = 0; r < gp->height; r++) {
		for (uint16_t c = 0; c < gp->width; c++) {
		    uint8_t bit = bp[bitn/8] & (1 << (7-(bitn%8)));
		    if (bit)
			plotfb (x+c, y+r, text_color);
		    bitn++;
		}
	    }
	    fb_dirty = true;
	pthread_mutex_unlock (&fb_lock);

	cursor_x += gp->xAdvance;
}

/* store the desired protect drawing region
 * we silently enforce it being wholy within FB_XRES x FB_YRES
 */
void Adafruit_RA8875::setPR (uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
        if (x >= 0 && y >= 0 && x + w <= FB_XRES && y + h <= FB_YRES) {
            pr_x = x*SCALESZ;
            pr_y = y*SCALESZ;
            pr_w = w*SCALESZ;
            pr_h = h*SCALESZ;
        }
}

/* draw the protected region synchronously
 */
void Adafruit_RA8875::drawPR(void)
{
        // set flag to inform the drawing thread to draw the pr region, wait until finished.
        // if you know of a better way with mutexes etc let me know
        pr_draw = true;
        while (pr_draw)
            usleep (1000);
}


/* return a typed character, else 0
 */
char Adafruit_RA8875::getChar()
{
    char c = 0;
    pthread_mutex_lock (&kb_lock);
        if (kb_cqhead != kb_cqtail) {
            c = kb_cq[kb_cqhead++];
            if (kb_cqhead == sizeof(kb_cq))
                kb_cqhead = 0;
        }
    pthread_mutex_unlock (&kb_lock);
    return (c);
}




/********************************************************************************************************
 *
 * event handling and rendering threads
 *
 */


#ifdef _USE_X11

// _USE_X11
void *Adafruit_RA8875::fbThreadHelper(void *me)
{
	// kludge to allow using a method as a thread function.
	pthread_detach(pthread_self());
	((Adafruit_RA8875*)me)->fbThread();
	return (NULL);
}

/* display fb_canvas
 * N.B. we assume fb_lock is held
 */
// _USE_X11
void Adafruit_RA8875::drawCanvas()
{
        // send one block containing the rectangular bounding box of the changed pixels. smaller
        // transations would send fewer pixels but each transaction is expensive.. found no happy medium.

        bool any_change = false;

        // bounding box
        int bb_x0 = 0, bb_y0 = 0, bb_x1 = 0, bb_y1 = 0;

        for (int y = 0; y < FB_YRES; y++) {

            // we assume protected region is at lower right
            int max_x = pr_draw || pr_w == 0 || y < pr_y ? FB_XRES : pr_x;

            // handy start of this row
            fbpix_t *stage_p = &fb_stage[y*FB_XRES];
            fbpix_t *canvas_p = &fb_canvas[y*FB_XRES];

            for (int x = 0; x < FB_XRES; x++) {

                int max_y = pr_draw || pr_h == 0 || x < pr_x ? FB_YRES : pr_y;
                if (x >= max_x && y >= max_y)
                    continue;

                if (stage_p[x] != canvas_p[x]) {

                    // update pixel
                    stage_p[x] = canvas_p[x];

                    // update bounding box
                    if (!any_change) {
                        bb_x0 = bb_x1 = x;
                        bb_y0 = bb_y1 = y;
                    }
                    if (x < bb_x0)
                        bb_x0 = x;
                    if (x > bb_x1)
                        bb_x1 = x;
                    if (y > bb_y1)
                        bb_y1 = y;
                    // y can't get any smaller

                    // found something to do
                    any_change = true;
                }
            }
        }

        if (any_change) {

            // update bounding box (inclusive of both edges)
            int nx = bb_x1-bb_x0+1;
            int ny = bb_y1-bb_y0+1;
            XPutImage(display, pixmap, black_gc, img, bb_x0, bb_y0, bb_x0, bb_y0, nx, ny);
            XCopyArea(display, pixmap, win, black_gc, bb_x0, bb_y0, nx, ny, FB_X0+bb_x0, FB_Y0+bb_y0);

            // struct timeval tv;
            // gettimeofday(&tv, NULL);
            // printf ("XCopyArea %ld.%06ld [%6d, %6d] %6d x %6d = %6d\n", tv.tv_sec, tv.tv_usec, bb_x0, bb_y0, nx, ny, nx*ny);

            // let server catch up before next loop
            XSync (display, false);
        }
}

// _USE_X11
void Adafruit_RA8875::X11OptionsEngageNow (bool fs)
{
        // save
        options_fullscreen = fs;

        // trigger
        options_engage = true;

        // wait here
        while (options_engage)
            usleep (1000);
}

/* thread that runs forever reacting to X11 events and painting fb_canvas whenever it changes
 */
// _USE_X11
void Adafruit_RA8875::fbThread ()
{
        // application and transparent cursors
        Cursor app_cursor, off_cursor;

        // each event
	XEvent event;

        // record keypress time
        struct timeval kp0;

        // init cursor timeout off soon
        gettimeofday (&mouse_tv, NULL);
        bool cursor_on = true;

        // create red application cursor
        char mask_data[FB_CURSOR_SZ*FB_CURSOR_SZ/8];            // bitmask of active pixels forming arrow
        memset (mask_data, 0, FB_CURSOR_SZ*FB_CURSOR_SZ/8);
        char cur_data[FB_CURSOR_SZ*FB_CURSOR_SZ/8];             // bitmask of fg pixels else bg color
        memset (cur_data, 0, FB_CURSOR_SZ*FB_CURSOR_SZ/8);    
        // fill top half sans border
        for (uint16_t r = 0; r < FB_CURSOR_SZ/2; r++) {
            for (uint16_t c = r/2+1; c < 2*r-1; c++) {
                mask_data[(r*FB_CURSOR_SZ+c)/8] |= 1 << ((r*FB_CURSOR_SZ+c)%8);
                cur_data[(r*FB_CURSOR_SZ+c)/8] |= 1 << ((r*FB_CURSOR_SZ+c)%8);
            }
        }
        // fill bottom half sans border
        for (uint16_t r = FB_CURSOR_SZ/2; r < FB_CURSOR_SZ; r++) {
            for (uint16_t c = r/2+1; c < 3*FB_CURSOR_SZ/2-r-1; c++) {
                mask_data[(r*FB_CURSOR_SZ+c)/8] |= 1 << ((r*FB_CURSOR_SZ+c)%8);
                cur_data[(r*FB_CURSOR_SZ+c)/8] |= 1 << ((r*FB_CURSOR_SZ+c)%8);
            }
        }
        // extend mask 1 to form bg border
        for (uint16_t i = 0; i < FB_CURSOR_SZ/2; i++) {
            mask_data[((i)*FB_CURSOR_SZ + (2*i))/8] |= 1 << (((i)*FB_CURSOR_SZ + 2*(i))%8);
            mask_data[((i)*FB_CURSOR_SZ + (2*i+1))/8] |= 1 << (((i)*FB_CURSOR_SZ + (2*i+1))%8);
            mask_data[((2*i)*FB_CURSOR_SZ + (i))/8] |= 1 << (((2*i)*FB_CURSOR_SZ + (i))%8);
            mask_data[((2*i+1)*FB_CURSOR_SZ + (i))/8] |= 1 << (((2*i+1)*FB_CURSOR_SZ + (i))%8);
            mask_data[((FB_CURSOR_SZ-i-1)*FB_CURSOR_SZ + (i+FB_CURSOR_SZ/2))/8]
                        |= 1 << (((FB_CURSOR_SZ-i-1)*FB_CURSOR_SZ + (i+FB_CURSOR_SZ/2))%8);
        }

        // #define _DUMP_CURSOR_MASK
        #ifdef _DUMP_CURSOR_MASK
        for (uint16_t r = 0; r < FB_CURSOR_SZ; r++) {
            for (uint16_t c = 0; c < FB_CURSOR_SZ; c++)
                printf (" %d", (mask_data[(r*FB_CURSOR_SZ+c)/8] >> (r*FB_CURSOR_SZ+c)%8) & 1);
            printf ("\n");
        }
        #endif // _DUMP_CURSOR_MASK

        Pixmap mask_pm = XCreateBitmapFromData(display, win, mask_data, FB_CURSOR_SZ, FB_CURSOR_SZ);
        Pixmap cur_pm = XCreateBitmapFromData(display, win, cur_data, FB_CURSOR_SZ, FB_CURSOR_SZ);
        XColor fg, bg;
        fg.red = 0xFF<<8; fg.green = 0x22<<8; fg.blue = 0x22<<8;
        bg.red = 0; bg.green = 0; bg.blue = 0;
        app_cursor = XCreatePixmapCursor(display, cur_pm, mask_pm, &fg, &bg, 0, 0);
        XFreePixmap(display, mask_pm);

        // create transparent cursor
        memset (mask_data, 0, FB_CURSOR_SZ*FB_CURSOR_SZ/8);     // bitmask all off, ignores data
        mask_pm = XCreateBitmapFromData(display, win, mask_data, FB_CURSOR_SZ, FB_CURSOR_SZ);
        off_cursor = XCreatePixmapCursor(display, cur_pm, mask_pm, &fg, &bg, 0, 0);
        XFreePixmap(display, cur_pm);
        XFreePixmap(display, mask_pm);

	// first display!
        XMapWindow(display,win);
        XDefineCursor (display, win, app_cursor);

        for(;;)
        {

            // all set
            ready = true;

            // get mouse idle time
            struct timeval tv;
            gettimeofday (&tv, NULL);
            mouse_idle = (tv.tv_sec - mouse_tv.tv_sec)*1000 + (tv.tv_usec - mouse_tv.tv_usec)/1000;

            // turn off cursor if no mouse action
            if (mouse_idle <= MOUSE_FADE) {

                // want cursor on
                if (!cursor_on) {
                    XDefineCursor (display, win, app_cursor);
                    cursor_on = true;
                }
            } else {

                // want cursor off
                if (cursor_on) {
                    XDefineCursor (display, win, off_cursor);
                    cursor_on = false;
                }
            }

            // X11 options are deferred until explicitly enabled; reset options_engage after each use.
            if (options_engage) {

                printf ("options_engage: %d\n", options_fullscreen);

                // add or remove _NET_WM_STATE_FULLSCREEN from _NET_WM_STATE
                // see https://specifications.freedesktop.org/wm-spec
                // inspired by Joeri van Dooren, ure@on3ure.be
                Atom wm_state = XInternAtom(display, "_NET_WM_STATE", False);
                Atom fullscreen = XInternAtom(display, "_NET_WM_STATE_FULLSCREEN", False);
                memset(&event, 0, sizeof(event));
                event.type = ClientMessage;
                event.xclient.window = win;
                event.xclient.message_type = wm_state;
                event.xclient.format = 32;
                event.xclient.data.l[0] = options_fullscreen ? 1 : 0; // _NET_WM_STATE_ADD or _NET_WM_STATE_REMOVE
                event.xclient.data.l[1] = fullscreen;
                event.xclient.data.l[2] = 0;
                XSendEvent(display, DefaultRootWindow(display), False,
                                SubstructureNotifyMask | SubstructureRedirectMask, &event);

                // done until trigger again
                options_engage = false;
            }

	    // handle events but don't block if none
	    while (XPending (display) > 0) {

		XNextEvent(display, &event);

		switch (event.type) {

		case Expose:
		    // printf ("Expose: [%d, %d]  %d x %d \n", event.xexpose.x, event.xexpose.y, event.xexpose.width, event.xexpose.height);
		    XCopyArea(display, pixmap, win, black_gc,
		    		event.xexpose.x-FB_X0, event.xexpose.y-FB_Y0,
				event.xexpose.width, event.xexpose.height, event.xexpose.x, event.xexpose.y);
		    break;

                case KeyPress:
                    // just record time to start repeating
                    gettimeofday (&kp0, NULL);
                    break;

                case KeyRelease:
                    {
                        char buf[10];
                        if (XLookupString ((XKeyEvent*)&event, buf, sizeof(buf), NULL, NULL) > 0) {
                            pthread_mutex_lock (&kb_lock);
                                kb_cq[kb_cqtail++] = buf[0];
                                if (kb_cqtail == sizeof(kb_cq))
                                    kb_cqtail = 0;
                            pthread_mutex_unlock (&kb_lock);
                        }
                    }
		    break;

		case ButtonPress:
		    pthread_mutex_lock (&mouse_lock);
			mouse_x = event.xbutton.x;
			mouse_y = event.xbutton.y;
			mouse_downs++;
		    pthread_mutex_unlock (&mouse_lock);

                    // record time of mouse situation change for cursor fade
                    gettimeofday (&mouse_tv, NULL);

                    // printf ("press   %ld.%06ld\n", mouse_tv.tv_sec, mouse_tv.tv_usec);

		    break;

		case ButtonRelease:
		    pthread_mutex_lock (&mouse_lock);
			mouse_x = event.xbutton.x;
			mouse_y = event.xbutton.y;
			mouse_ups++;
		    pthread_mutex_unlock (&mouse_lock);

                    // record time of mouse situation change for cursor fade
                    gettimeofday (&mouse_tv, NULL);

                    // printf ("release %ld.%06ld\n", mouse_tv.tv_sec, mouse_tv.tv_usec);

		    break;

                case LeaveNotify:

                    // indicate mouse not valid
		    pthread_mutex_lock (&mouse_lock);
			mouse_x = -1;
		    pthread_mutex_unlock (&mouse_lock);

                    break;


                case MotionNotify:

		    pthread_mutex_lock (&mouse_lock);
			mouse_x = event.xbutton.x;
			mouse_y = event.xbutton.y;
		    pthread_mutex_unlock (&mouse_lock);

                    // record time of mouse situation change for cursor fade
                    gettimeofday (&mouse_tv, NULL);

		    break;

		case ConfigureNotify:
		    // printf ("ConfigureNotify: %dx%d+%d+%d\n", event.xconfigure.width, event.xconfigure.height, event.xconfigure.x, event.xconfigure.y);
		    fb_si.xres = event.xconfigure.width;
		    fb_si.yres = event.xconfigure.height;
		    FB_X0 = (fb_si.xres - FB_XRES)/2;
		    FB_Y0 = (fb_si.yres - FB_YRES)/2;
		    // fill in unused border
		    XFillRectangle (display, win, black_gc, 0, 0, fb_si.xres, FB_Y0);
		    XFillRectangle (display, win, black_gc, 0, FB_Y0, FB_X0, FB_YRES);
		    XFillRectangle (display, win, black_gc, FB_X0 + FB_XRES, FB_Y0, FB_X0+1, FB_YRES);
		    XFillRectangle (display, win, black_gc, 0, FB_Y0 + FB_YRES, fb_si.xres, FB_Y0+1);
                    // invalidate staging area to get a full refresh
                    memset (fb_stage, ~0, fb_nbytes);
		    break;
		}
	    }

	    // show any changes
            pthread_mutex_lock (&fb_lock);
                if (fb_dirty || pr_draw) {
                    drawCanvas();
                    fb_dirty = false;
                    pr_draw = false;
                }
            pthread_mutex_unlock (&fb_lock);

            // generate another char if key still pressed
            if (event.type == KeyPress) {
                struct timeval tv0;
                gettimeofday (&tv0, NULL);
                int dt_ms = (tv0.tv_sec - kp0.tv_sec)*1000 + (tv0.tv_usec - kp0.tv_usec)/1000;
                if (dt_ms > 400) {
                    char buf[10];
                    if (XLookupString ((XKeyEvent*)&event, buf, sizeof(buf), NULL, NULL) > 0) {
                        pthread_mutex_lock (&kb_lock);
                            kb_cq[kb_cqtail++] = buf[0];
                            if (kb_cqtail == sizeof(kb_cq))
                                kb_cqtail = 0;
                        pthread_mutex_unlock (&kb_lock);
                    }
                    kp0 = tv0;
                }
            }

            // let scene build a while before next update
            usleep (50000);

        }

}

// return actual display dimensions
// _USE_X11
void Adafruit_RA8875::getScreenSize (int *w, int *h)
{
        int snum = DefaultScreen(display);
        *w = DisplayWidth(display, snum);
        *h = DisplayHeight(display, snum);
}

#endif	// _USE_X11

#ifdef _USE_FB0

// unused on FB_0 side
// _USE_FB0
void Adafruit_RA8875::X11OptionsEngageNow (bool fs)
{
        (void)(fs);
}

/* open keyboard on kb_fd if not already.
 */
// _USE_FB0
void Adafruit_RA8875::findKeyboard()
{
        // skip if already ok
        if (kb_fd >= 0)
            return;

	// connect to kb on VT 1: still experimental
	const char kb_dev[] = "/dev/tty1";
	kb_fd = open (kb_dev, O_RDWR);
	if (kb_fd < 0) {
	    printf ("KB: %s: %s\n", kb_dev, strerror(errno));
            // continue since kb not essential
	} else {
            // turn off cursor blinking and login on tty1
            ourSystem ("echo 0 > /sys/class/graphics/fbcon/cursor_blink");
            ourSystem ("systemctl stop getty@tty1.service");

            // turn off VT drawing
            // https://unix.stackexchange.com/questions/173712/best-practice-for-hiding-virtual-console-while-rendering-video-to-framebuffer
            printf ("turning off VT\n");
            if (ioctl (kb_fd, KDSETMODE, KD_GRAPHICS) < 0)
                printf ("KDSETMODE KD_GRAPHICS: %s\n", strerror(errno));

            // change tty to raw after open so it sticks
            ourSystem ("stty -F /dev/tty1 min 1 -icanon");

            printf ("KB: found kb at %s\n", kb_dev);
        }
}

/* look through all /dev/input/events* for a mouse and/or touchscreen.
 * set mouse_fd and/or touch_rd if found but ignore if already >= 0.
 * builds heavily from https://elinux.org/images/9/93/Evtest.c
 * exit if grave trouble.
 */
// _USE_FB0
void Adafruit_RA8875::findMouse()
{
        // open events directory
        char dirname[] = "/dev/input";
        DIR *dp = opendir (dirname);
        if (!dp) {
            printf ("%s: %s\n", dirname, strerror(errno));
            exit(1);
        }

        // scan directory for events*
        struct dirent *de;
        while ((de = readdir (dp)) != NULL) {

            // only consider events* files
            if (strncmp (de->d_name, "event", 5))
                continue;

            // open events file
            char fullevpath[512];
            snprintf (fullevpath, sizeof(fullevpath), "%s/%s", dirname, de->d_name);
            // printf ("POINTER: checking %s\n", fullevpath);
            int evfd = open (fullevpath, O_RDONLY);
            if (evfd < 0) {
                printf ("%s: %s\n", fullevpath, strerror(errno));
                continue;
            }

            // try to get capabilities
            #define BITS_PER_LONG (sizeof(long) * 8)
            #define NBITS(x) ((((x)-1)/BITS_PER_LONG)+1)
            #define OFF(x)  ((x)%BITS_PER_LONG)
            #define BIT(x)  (1UL<<OFF(x))
            #define LONG(x) ((x)/BITS_PER_LONG)
            #define test_bit(bit, array)    ((array[LONG(bit)] >> OFF(bit)) & 1)
            unsigned long bit[EV_MAX][NBITS(KEY_MAX)];
            memset(bit, 0, sizeof(bit));
            if (ioctl (evfd, EVIOCGBIT(0, EV_MAX), bit[0]) < 0) {
                // if can't get this the whole strategy is busted
                printf ("%s: EVIOCGBIT(%d) failed: %s\n", fullevpath, 0, strerror(errno));
                exit(1);
            }

            // decide based on events found:
            //    touchscreen: type EV_ABS with codes ABS_X and ABS_Y and EV_KEY with BTN_TOUCH
            //    mouse: type EV_REL with codes REL_X and REL_Y and EV_KEY with BTN_LEFT

            bool evfd_used = false;
            if (touch_fd < 0 && test_bit (EV_ABS, bit[0])
                             && ioctl (evfd, EVIOCGBIT(EV_ABS, KEY_MAX), bit[EV_ABS]) >= 0
                             && test_bit (ABS_X, bit[EV_ABS])
                             && test_bit (ABS_Y, bit[EV_ABS])
                             && ioctl (evfd, EVIOCGBIT(EV_KEY, KEY_MAX), bit[EV_KEY]) >= 0
                             && test_bit (BTN_TOUCH, bit[EV_KEY])) {
                printf ("POINTER: found touch screen at %s\n", fullevpath);
                touch_fd = evfd;
                evfd_used = true;
            }

            if (mouse_fd < 0 && test_bit (EV_REL, bit[0])
                             && ioctl (evfd, EVIOCGBIT(EV_REL, KEY_MAX), bit[EV_REL]) >= 0
                             && test_bit (REL_X, bit[EV_REL])
                             && test_bit (REL_Y, bit[EV_REL])
                             && ioctl (evfd, EVIOCGBIT(EV_KEY, KEY_MAX), bit[EV_KEY]) >= 0
                             && test_bit (BTN_LEFT, bit[EV_KEY])) {
                printf ("POINTER: found mouse at %s\n", fullevpath);
                mouse_fd = evfd;
                evfd_used = true;
            }

            // close unless used
            if (!evfd_used)
                close (evfd);
        }

        // finished with directory
        closedir (dp);
}

// _USE_FB0
void *Adafruit_RA8875::mouseThreadHelper(void *me)
{
	// kludge to allow using a method as a thread function.
	pthread_detach(pthread_self());
	((Adafruit_RA8875*)me)->mouseThread();
	return (NULL);
}

/* thread to monitor RPi mouse and touch screen input.
 * if there is a touch screen we never expect it to disappear but we do allow a mouse to come and go.
 */
// _USE_FB0
void Adafruit_RA8875::mouseThread (void)
{
        // poll for mouse occasionally whenever not found
        time_t mouse_poll = 0;

	// forever: get next mouse or touchscreen message, update mouse info in fb coords.
        // N.B. limit motion to app's border.
	for(;;) {

            // look for mouse occasionaly if none, we assume touch never disappears
            if (mouse_fd < 0) {
                time_t t = time(NULL);
                if (t - mouse_poll > 1) {
                    // printf ("POINTER: check for mouse\n");
                    mouse_poll = t;
                    findMouse();
                }
            }

            // fill rfd with active input fd's and find max
            int max_fd = 0;
            fd_set rfd;
            FD_ZERO (&rfd);
            if (mouse_fd >= 0) {
                FD_SET (mouse_fd, &rfd);
                if (mouse_fd > max_fd)
                    max_fd = mouse_fd;
            }
            if (touch_fd >= 0) {
                FD_SET (touch_fd, &rfd);
                if (touch_fd > max_fd)
                    max_fd = touch_fd;
            }
            if (max_fd == 0) {
                // printf ("POINTER: no mouse or touch screen\n");
                usleep (1000000);       // try again leisurely
                continue;
            }

            // wait for any ready, don't wait forever so we can check again for mouse if needed
            struct timeval tv;
            tv.tv_sec = 1;
            tv.tv_usec = 0;
            int ns = select (max_fd+1, &rfd, NULL, NULL, &tv);
            if (ns == 0)
                continue;               // timed out
            if (ns < 0) {
                printf ("select(2) error: %s\n", strerror(errno));
                exit(1);
            }

            // pick one to read, get other next time
            int ready_fd;
            if (FD_ISSET (mouse_fd, &rfd))
                ready_fd = mouse_fd;
            else if (FD_ISSET (touch_fd, &rfd))
                ready_fd = touch_fd;
            else {
                printf ("bug! select(2) returned %d but nothing ready\n", ns);
                exit(1);
            }

            // check for input or EOF from ready_fd
            struct input_event iev;
            if (read (ready_fd, &iev, sizeof(iev)) == sizeof(iev)) {

		pthread_mutex_lock (&mouse_lock);

                    if (iev.type == EV_ABS && iev.code == ABS_X) {
                        mouse_x = iev.value;
                        fb_dirty = true;
                    } else if (iev.type == EV_ABS && iev.code == ABS_Y) {
                        mouse_y = iev.value;
                        fb_dirty = true;
                    } else if (iev.type == EV_REL && iev.code == REL_X) {
                        mouse_x += iev.value;
                        fb_dirty = true;
                    } else if (iev.type == EV_REL && iev.code == REL_Y) {
                        mouse_y += iev.value;
                        fb_dirty = true;
                    } else if (iev.type == EV_KEY && (iev.code == BTN_TOUCH || iev.code == BTN_LEFT)) {
                        if (iev.value > 0)
                            mouse_downs++;
                        else
                            mouse_ups++;
                        fb_dirty = true;
                    }

                    if (fb_dirty) {
                        // insure in range
                        if (mouse_x < FB_X0)
                            mouse_x = FB_X0;
                        if (mouse_x >= FB_X0 + SCALESZ*width())
                            mouse_x = FB_X0 + SCALESZ*width()-1;
                        if (mouse_y < FB_Y0)
                            mouse_y = FB_Y0;
                        if (mouse_y >= FB_Y0 + SCALESZ*height())
                            mouse_y = FB_Y0 + SCALESZ*height()-1;

                        // record time of mouse situation change for cursor drawing
                        gettimeofday (&mouse_tv, NULL);
                    }

		pthread_mutex_unlock (&mouse_lock);

            } else {

                // close and rety later if disappeared
                if (ready_fd == touch_fd) {
                    printf ("POINTER: touch screen disappeared\n");
                    close (touch_fd);
                    touch_fd = -1;
                } else if (ready_fd == mouse_fd) {
                    printf ("POINTER: mouse disappeared\n");
                    close (mouse_fd);
                    mouse_fd = -1;
                }
            }
        }
}

// _USE_FB0
void *Adafruit_RA8875::kbThreadHelper(void *me)
{
	// kludge to allow using a method as a thread function.
	pthread_detach(pthread_self());
	((Adafruit_RA8875*)me)->kbThread();
	return (NULL);
}

// _USE_FB0
void Adafruit_RA8875::kbThread ()
{
	int8_t buf[1];

        // first try immediately
        findKeyboard();

	// block until get kb char, then store in kb_cq
	for(;;) {

            // look for kb occasionaly if none
            if (kb_fd < 0) {
                // printf ("KB: check for kb\n");
                usleep (1000000);       // try again leisurely
                findKeyboard();
                continue;
            }

            // read next char, beware trouble
	    int nr = read (kb_fd, buf, 1);
	    if (nr == 1) {
		pthread_mutex_lock (&kb_lock);
                    kb_cq[kb_cqtail++] = buf[0];
                    if (kb_cqtail == sizeof(kb_cq))
                        kb_cqtail = 0;
		    fb_dirty = true;
		pthread_mutex_unlock (&kb_lock);
                // printf ("KB: %d %c\n", buf[0], buf[0]);
	    } else {
                if (nr < 0)
                    printf ("KB: %s\n", strerror(errno));
                else
                    printf ("KB: EOF\n");
                close(kb_fd);
                kb_fd = -1;
            }
	}
}

// _USE_FB0
void *Adafruit_RA8875::fbThreadHelper(void *me)
{
	// kludge to allow using a method as a thread function.
	pthread_detach(pthread_self());
	((Adafruit_RA8875*)me)->fbThread();
	return (NULL);
}

/* cursor drawing helper:
 * given location of cursor shape relative to 0,0 at hw mouse coord, set fb_cursor to color if fits within.
 */
// _USE_FB0
void Adafruit_RA8875::setCursorIfVis (uint16_t row, uint16_t col, fbpix_t color)
{
        // rely on unsigned wrap detect negative values

        row += mouse_y - FB_Y0;
        col += mouse_x - FB_X0;
        if (row < FB_YRES && col < FB_XRES)
            fb_cursor[row*FB_XRES + col] = color;
}

/* display fb_canvas.
 * N.B. we assume fb_lock is held
 */
// _USE_FB0
void Adafruit_RA8875::drawCanvas()
{
        // put only the unproteced region unless pr_draw is set
        if (pr_draw) {
            // draw everything
            memcpy (fb_stage, fb_canvas, fb_nbytes);
        } else {
            // draw only around the protected area
            uint16_t bw = FB_XRES*BYTESPFBPIX;                                  // bytes wide
            uint16_t pr_r = pr_x + pr_w;                                        // right of PR
            uint16_t pr_b = pr_y + pr_h;                                        // bottom of PR
            fbpix_t *s_row = fb_stage;                                          // next stage row
            fbpix_t *c_row = fb_canvas;                                         // next canvas row
            for (uint16_t y = 0; y < FB_YRES; y++, c_row += FB_XRES, s_row += FB_XRES) {
                if (y < pr_y) {
                    memcpy (s_row, c_row, bw);                                  // above
                } else if (y < pr_b) {
                    memcpy (s_row, c_row, pr_x*BYTESPFBPIX);                    // left
                    memcpy (s_row+pr_r, c_row+pr_r, (FB_XRES-pr_r)*BYTESPFBPIX);// right
                } else {
                    memcpy (s_row, c_row, bw);                                  // below
                }
            }
        }
}

/* thread that runs forever to update display buffer whenever fb_canvas changes
 */
// _USE_FB0
void Adafruit_RA8875::fbThread ()
{
        // init cursor timeout off soon
        gettimeofday (&mouse_tv, NULL);

        // update screen periodically
	for (;;) {

            // all set
            ready = true;

	    // get stable copy of canvas into staging area
	    pthread_mutex_lock (&fb_lock);
		bool is_new = fb_dirty || pr_draw;
		if (is_new) {
                    drawCanvas();
		    fb_dirty = false;
                    pr_draw = false;
		}
	    pthread_mutex_unlock (&fb_lock);

            // get mouse idle time
            struct timeval tv;
            gettimeofday (&tv, NULL);
            mouse_idle = (tv.tv_sec - mouse_tv.tv_sec)*1000 + (tv.tv_usec - mouse_tv.tv_usec)/1000;

            // copy fb_stage to hardware display if new or mouse moved
            if (is_new || mouse_idle < MOUSE_FADE) {

                // copy to cursor layer
                memcpy (fb_cursor, fb_stage, fb_nbytes);

                // add cursor if moved
                // N.B.: CAN NOT use the nice drawing tools because they use fb_canvas
                if (mouse_idle < MOUSE_FADE) {
                    const fbpix_t fgcolor = RGB16TOFBPIX(RGB565(0,0,0));
                    const fbpix_t bgcolor = RGB16TOFBPIX(RGB565(0xFF,0x22,0x22));
                    // fill top half
                    for (uint16_t r = 0; r < FB_CURSOR_SZ/2; r++)
                        for (uint16_t c = r/2+1; c < 2*r-1; c++)
                            setCursorIfVis (r, c, bgcolor);
                    // fill bottom half
                    for (uint16_t r = FB_CURSOR_SZ/2; r < FB_CURSOR_SZ; r++)
                        for (uint16_t c = r/2+1; c < 3*FB_CURSOR_SZ/2-r-1; c++)
                            setCursorIfVis (r, c, bgcolor);
                    // draw border
                    for (uint16_t i = 0; i < FB_CURSOR_SZ/2; i++) {
                        setCursorIfVis(i, 2*i, fgcolor);
                        setCursorIfVis(i, 2*i+1, fgcolor);
                        setCursorIfVis(2*i, i, fgcolor);
                        setCursorIfVis(2*i+1, i, fgcolor);
                        setCursorIfVis(FB_CURSOR_SZ-i-1, i+FB_CURSOR_SZ/2, fgcolor);
                    }
                }

                // wait for vertical sync TODO
                // int zero = 0;
                // if (ioctl(fb_fd, FBIO_WAITFORVSYNC, &zero) < 0)
                    // printf ("FBIO_WAITFORVSYNC: %s\n", strerror(errno));

                // black top border
                const uint32_t fb_rowbytes = fb_si.xres*BYTESPFBPIX;
                memset (fb_fb, 0, FB_Y0*fb_rowbytes);

                // copy cursor area to screen, and blacken left and right borders
                for (int y = 0; y < FB_YRES; y++) {
                    fbpix_t *fb_row0 = fb_fb + (FB_Y0+y)*fb_si.xres;
                    memset (fb_row0, 0, FB_X0*BITSPFBPIX);              // left border
                    memcpy (fb_row0+FB_X0, fb_cursor+y*FB_XRES, FB_XRES*BYTESPFBPIX);
                    memset (fb_row0+FB_X0+FB_XRES, 0, FB_X0*2);         // right border
                }

                // black bottom border
                memset (fb_fb+(FB_Y0+FB_YRES)*fb_si.xres, 0, FB_Y0*fb_rowbytes);
            }

	    // no need to go crazy
            usleep (20000);
	}
}


// return actual display dimensions
// _USE_FB0
void Adafruit_RA8875::getScreenSize (int *w, int *h)
{
        *w = FB_XRES;
        *h = FB_YRES;
}

#endif // _USE_FB0
