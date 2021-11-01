/* handle the touch screen
 */



#include "HamClock.h"
#include "calibrate.h"



#define TOUCH_HOLDT     1500            // hold time, millis()

static CAL_MATRIX touch_matrix;         // touch-to-display transformation


/* insert new_value into a[] in increasing order.
 * N.B. we do not check for overflowing a[]
 */
static void insertList (uint16_t a[], uint8_t n_used, uint16_t new_value)
{
    while (n_used > 0 && a[n_used-1] > new_value) {
        a[n_used] = a[n_used-1];
        n_used -= 1;
    }
    a[n_used] = new_value;
}

/* read the touch screen and return raw uncalibrated coordinates.
 */
static TouchType readRawTouch (uint16_t &x, uint16_t &y)
{
    // fast return if none
    if (!tft.touched())
        return (TT_NONE);

    // sums for means until released
    uint32_t xsum = 0, ysum = 0;
    uint16_t nsum = 0;

    // collect and determine duration until released
    uint32_t t0 = millis();
    while (tft.touched()) {
        uint16_t tx, ty;
        tft.touchRead (&tx, &ty);
        xsum += tx;
        ysum += ty;
        nsum++;
        wdDelay(10);
    }

    // set location from means
    x = xsum/nsum;
    y = ysum/nsum;
    
    // return hold or tap
    return (millis() - t0 >= TOUCH_HOLDT ? TT_HOLD : TT_TAP);
}

#if defined(_IS_ESP8266)

/* given values return from tft.touchRead(), return screen location.
 * N.B. assumes calibrateTouch() has already been called.
 */
static void touch2Screen (uint16_t tx, uint16_t ty, SCoord &s)
{
    CAL_POINT p, q;
    p.x = tx;
    p.y = ty;
    getDisplayPoint (&q, &p, &touch_matrix);
    s.x = q.x;
    s.y = q.y;
}

#endif

/* calibrate the touch screen.
 * use values from NVRAM if available unless force, and give op chance to redo.
 * goal is to set touch_matrix.
 */
void calibrateTouch (bool force)
{
#if !defined(_IS_ESP8266)
    return;
#endif


#   define N_TAPS       5               // number of taps to average for each point
#   define TAP_RAD      5               // symbol radius
#   define EDGE         50U             // distance from edge
#   define OP_WAIT      10              // seconds to wait for op to decide 
#   define N_CALPTS     3               // number of calibration points
#   define DBOUNCE      100             // millis() debounce

    CAL_POINT display[N_CALPTS] = {     // well-dispersed display coords
        {EDGE, EDGE},
        {tft.width()-EDGE, tft.height()/2U},
        {tft.width()/2U, tft.height()-EDGE},
    };
    CAL_POINT touch[N_CALPTS];          // corresponding touch coords
    uint16_t tx[N_TAPS], ty[N_TAPS];    // raw touch values
    uint16_t counter_x;

    // check whether values are already in NVRAM
    bool nvok = !force &&
                NVReadUInt32 (NV_TOUCH_CAL_A, &touch_matrix.An) &&
                NVReadUInt32 (NV_TOUCH_CAL_B, &touch_matrix.Bn) &&
                NVReadUInt32 (NV_TOUCH_CAL_C, &touch_matrix.Cn) &&
                NVReadUInt32 (NV_TOUCH_CAL_D, &touch_matrix.Dn) &&
                NVReadUInt32 (NV_TOUCH_CAL_E, &touch_matrix.En) &&
                NVReadUInt32 (NV_TOUCH_CAL_F, &touch_matrix.Fn) &&
                NVReadUInt32 (NV_TOUCH_CAL_DIV, &touch_matrix.Divider);

    // set up screen, give op chance to redo calibration if already in NVRAM
    tft.graphicsMode();
    eraseScreen();
    if (nvok) {
        selectFontStyle (LIGHT_FONT, SMALL_FONT);
        tft.setTextColor (RA8875_WHITE);

        // give a way out
        drawStringInBox ("Skip", skip_b, false, RA8875_WHITE);
        
        // appropriate prompt
        tft.setCursor (tft.width()/6, tft.height()/5);
#if defined(_IS_ESP8266)
        tft.print (F("Tap anywhere to recalibrate touch screen ... "));
#else
        tft.print (F("Click anywhere to recalibrate touch screen ... "));
#endif
        int16_t x = tft.getCursorX();
        int16_t y = tft.getCursorY();

        // spin until time out or tapped
        for (uint16_t s = OP_WAIT*10; !skip_skip && s > 0; --s) {
            resetWatchdog();
            if ((s+9)/10 != (s+10)/10) {
                tft.fillRect (x, y-30, 80, 40, RA8875_BLACK);
                tft.setCursor (x, y);
                tft.print((s+9)/10);
            }
            if (tft.touched()) {
                SCoord ss;
                (void) readCalTouch (ss);
                if (inBox(ss, skip_b))
                    return;
                goto proceed;
            }
            wdDelay(100);
        }
        return;
    }

  proceed:

    // collect each target
    eraseScreen();
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (RA8875_WHITE);
    tft.setCursor (tft.width()/6, tft.height()/3);
    tft.print (F("Tip: use a pencil or stylus, not your finger"));
    for (uint8_t i = 0; i < N_CALPTS; i++) {
        char buf[100];
        resetWatchdog();
        // appropriate prompt
#if defined(_IS_ESP8266)
        const char *prompt = "Tap";
#else
        const char *prompt = "Click";
#endif

        // display touch location and instructions
        tft.drawCircle (display[i].x, display[i].y, TAP_RAD, RA8875_WHITE);
        if (display[i].x < tft.width()/2) {
            tft.setCursor (display[i].x+20, display[i].y+10);
            sprintf (buf, _FX("< %s here carefully %d times "), prompt, N_TAPS);
            tft.print (buf);
            counter_x = display[i].x-35;
        } else {
            tft.setCursor (display[i].x-365, display[i].y+10);
            sprintf (buf, _FX("%s here carefully %d times > "), prompt, N_TAPS);
            tft.print (buf);
            counter_x = tft.getCursorX() + 35;
        }

        // collect N_TAPS
        for (uint8_t j = 0; j < N_TAPS; j++) {
            drainTouch();
            uint16_t x, y;
            uint32_t t0 = millis();
            while (readRawTouch (x, y) == TT_NONE || millis() - t0 < DBOUNCE)
                resetWatchdog();
            // Serial.printf(_FX("Raw Touch:\t%4d %4d\n"), x, y);
            tft.fillRect (counter_x, display[i].y-20, 20, 40, RA8875_BLACK);
            tft.setCursor (counter_x, display[i].y+10);
            tft.print(j+1);
            insertList (tx, j, x);
            insertList (ty, j, y);
        }

        // compute estimate as mean of center 3
        touch[i].x = (tx[N_TAPS/2-1] + tx[N_TAPS/2] + tx[N_TAPS/2+1])/3;
        touch[i].y = (ty[N_TAPS/2-1] + ty[N_TAPS/2] + ty[N_TAPS/2+1])/3;
    }

    // progress
    tft.setCursor (tft.width()/3, 2*tft.height()/3);
    tft.print('.');

    // find conversion mapping
    setCalibrationMatrix (display, touch, &touch_matrix);
    tft.print('.');

    // store in NVRAM
    NVWriteUInt32 (NV_TOUCH_CAL_A, touch_matrix.An);
    tft.print('.');
    NVWriteUInt32 (NV_TOUCH_CAL_B, touch_matrix.Bn);
    tft.print('.');
    NVWriteUInt32 (NV_TOUCH_CAL_C, touch_matrix.Cn);
    tft.print('.');
    NVWriteUInt32 (NV_TOUCH_CAL_D, touch_matrix.Dn);
    tft.print('.');
    NVWriteUInt32 (NV_TOUCH_CAL_E, touch_matrix.En);
    tft.print('.');
    NVWriteUInt32 (NV_TOUCH_CAL_F, touch_matrix.Fn);
    tft.print('.');
    NVWriteUInt32 (NV_TOUCH_CAL_DIV, touch_matrix.Divider);
}


/* read the touch screen and return calibrated screen coordinate.
 */
TouchType readCalTouch (SCoord &s)
{
    // fast return if none
    if (!tft.touched())
        return (TT_NONE);

    // read raw
    uint16_t x = 0, y = 0;
    TouchType tt = readRawTouch (x, y);

#if defined(_IS_ESP8266)

    // convert to screen coords via calibration matrix
    touch2Screen (x, y, s);

#else

    // no mapping required
    s.x = x;
    s.y = y;

#endif

    Serial.printf(_FX("Touch:\t%4d %4d\ttype %d\n"), s.x, s.y, (int)tt);
    
    // return hold or tap
    return (tt);
}

/* wait for no touch events, need time also since the resistance film seems to be sticky
 */
void drainTouch()
{
#if defined(_IS_ESP8266)
    resetWatchdog();
    uint32_t t0 = millis();
    bool touched = false;
    while (millis() - t0 < 100 || touched) {
        if ((touched = tft.touched()) == true) {
            uint16_t tx, ty;
            tft.touchRead (&tx, &ty);
        }
    }
    // Serial.println (F("Drain complete"));
    resetWatchdog();
#endif // defined(_IS_ESP8266)
}
