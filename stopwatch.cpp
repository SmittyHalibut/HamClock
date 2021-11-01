/* implement a simple stopwatch with lap timer, countdown timer, alarm clock and a pair of Big Clocks.
 *
 * we maintain two separate states:
 *   SWDisplayState: a display state that indicates which page we are showing, if any.
 *   SWEngineState:  an engine state that indicates what is running, if anything.
 */


#include "HamClock.h"


// set to show all boxes for debugging
// #define _SHOW_ALL


// countdown ranges, including flashing states
typedef enum {
    SWCDS_OFF,                          // idle or dark
    SWCDS_RUNOK,                        // more than SW_CD_WARNDT remaining
    SWCDS_WARN_ON,                      // > 0 but < SW_CD_WARNDT remaining
    SWCDS_TIMEOUT_ON,                   // timed out
} SWCDState;


/* only systems with GPIO can control LEDs and have start switch for countdown control.
 * ESP has a few GPIO but not enough for everything the Pi can do so this was left off.
 */
#if defined(_SUPPORT_GPIO) && defined(_IS_UNIX)

/* set the LEDs to indicate the given countdown range
 */
static void setLEDState (SWCDState cds)
{
    // ignore if not supposed to use GPIO
    if (!GPIOOk())
        return;

    // access pins
    GPIO& gpio = GPIO::getGPIO();
    if (!gpio.isReady())
        return;

    gpio.setAsOutput (SW_GRN_GPIO);
    gpio.setAsOutput (SW_RED_GPIO);

    switch (cds) {
    case SWCDS_RUNOK:              // fallthru
    case SWCDS_WARN_ON: 
        // green on
        gpio.setLo (SW_GRN_GPIO);
        gpio.setHi (SW_RED_GPIO);
        break;
    case SWCDS_OFF:
        // both off
        gpio.setHi (SW_GRN_GPIO);
        gpio.setHi (SW_RED_GPIO);
        break;
    case SWCDS_TIMEOUT_ON:
        // red on
        gpio.setHi (SW_GRN_GPIO);
        gpio.setLo (SW_RED_GPIO);
        break;
    }
}

/* return whether the countdown pin has toggled low,
 * ie, this is an edge-triggered state.
 */
static bool countdownPinIsTrue()
{
    // ignore if not supposed to use GPIO
    if (!GPIOOk())
        return (false);

    static bool prev_pin_state;
    static bool prev_pin_known;

    // read pin
    GPIO& gpio = GPIO::getGPIO();
    if (!gpio.isReady())
        return (false);
    gpio.setAsInput (SW_COUNTDOWN_GPIO);
    bool pin_state = gpio.readPin(SW_COUNTDOWN_GPIO);

    // init history if first time
    if (!prev_pin_known) {
        prev_pin_state = pin_state;
        prev_pin_known = true;
    }

    // return whether went low
    if (pin_state != prev_pin_state) {
        prev_pin_state = pin_state;
        return (!pin_state);
    } else
        return (false);
}

/* return state of alarm clock reset input pin.
 * N.B. pin is active-low
 */
static bool alarmPinIsSet(void)
{
    // ignore if not supposed to use GPIO
    if (!GPIOOk())
        return (false);

    GPIO& gpio = GPIO::getGPIO();
    if (!gpio.isReady())
        return (false);
    gpio.setAsInput (SW_ALARMOFF_GPIO);
    return (!gpio.readPin(SW_ALARMOFF_GPIO));
}

/* control the alarm clock output pin
 */
static void setAlarmPin (bool set)
{
    // ignore if not supposed to use GPIO
    if (!GPIOOk())
        return;

    GPIO& gpio = GPIO::getGPIO();
    gpio.setAsOutput (SW_ALARMOUT_GPIO);
    gpio.setHiLo (SW_ALARMOUT_GPIO, set);
}

#else // !_SUPPORT_GPIO

// dummies
static void setLEDState (SWCDState cds) { (void) cds; }
static bool countdownPinIsTrue() { return (false); }
static bool alarmPinIsSet(void) {return (false); }
static void setAlarmPin (bool set) { (void) set; }

#endif // _SUPPORT_GPIO




// stopwatch params
#define SW_NDIG         9                       // number of display digits
#define SW_BG           RA8875_BLACK            // bg color
#define SW_ND           8                       // number of digits
#define SW_DGAP         40                      // gap between digits
#define SW_Y0           190                     // upper left Y of all time digits
#define SW_DW           45                      // digit width
#define SW_DH           100                     // digit heigth
#define SW_X0           ((800-SW_ND*SW_DW-(SW_ND-1)*SW_DGAP)/2)   // x coord of left-most digit to center
#define SW_FLT          45                      // line thickness as fraction of SW_DW
#define SW_PUNCR        3                       // punctuation radius
#define SW_BAX          240                     // control button A x
#define SW_BBX          440                     // control button B x
#define SW_EXITX        670                     // exit button x
#define SW_EXITY        420                     // exit button y
#define SW_BCX          10                      // big-clock button x
#define SW_BCY          SW_EXITY                // big-clock button y
#define SW_BY           350                     // control button y
#define SW_BW           120                     // button width
#define SW_BH           40                      // button height
#define SW_CX           SW_BAX                  // color scale x
#define SW_CY           SW_EXITY                // color scale y
#define SW_CW           (SW_BBX+SW_BW-SW_CX)    // color scale width
#define SW_CH           SW_BH                   // color scale height
#define SW_HSV_S        200                     // color scale HSV saturation, 0..255
#define SW_HSV_V        255                     // color scale HSV value, 0..255
#define SW_BCDATEBIT    1                       // NV_BCFLAGS bit mask for showing bigclock date
#define SW_BCWXBIT      2                       // NV_BCFLAGS bit mask for showing bigclock weather
#define SW_BCDIGBIT     4                       // NV_BCFLAGS bit mask for whether big clock is digital
#define SW_DB12HBIT     8                       // NV_BCFLAGS bit mask for whether digital clock is 12 else 24
#define SW_ANOSHBIT     16                      // NV_BCFLAGS bit mask for whether no analog second hand

// alarm clock params
#define ALM_X0          180                     // alarm control button x
#define ALM_Y0          25                      // alarm control button y
#define ALM_W           200                     // alarm control button width
#define ALM_EX          420                     // alarm time display box x
#define ALM_EY          ALM_Y0                  // alarm time display box y
#define ALM_EW          SW_CDP_W                // alarm time display box w
#define ALM_TOVFLOW     (24U*60U)               // hrmn overflow value
#define ALM_RINGTO      30000                   // alarm clock ringing timeout, millis

// countdown params
#define SW_CD_X         ALM_X0                  // countdown button x
#define SW_CD_Y         (ALM_Y0+2*SW_BH)        // countdown button y
#define SW_CD_W         ALM_W                   // countdown button width
#define SW_CDP_X        ALM_EX                  // countdown period display box x
#define SW_CDP_W        ALM_W                   // countdown period display box width
#define SW_CD_WARNDT    60000                   // countdown warning time, ms

// big analog clock params
#define BAC_X0          400                     // x center
#define BAC_Y0          240                     // y center
#define BAC_MNR         210                     // minute hand radius
#define BAC_SCR         180                     // second hand radius
#define BAC_HRR         130                     // hour hand radius
#define BAC_FR          232                     // face radius
#define BAC_BEZR        238                     // bezel radius
#define BAC_HTR         12                      // hour tick radius
#define BAC_MTR         5                       // minute tick radius
#define BAC_DOTR        2                       // center dot radius
#define BAC_HRTH        deg2rad(15.0F)          // hour hand thickness half-angle, rads
#define BAC_MNTH        (BAC_HRTH*BAC_HRR/BAC_MNR) // minute hand thickness half-angle, rads
#define BAC_HTTH        deg2rad(0.6F)           // hour tick half-angle as seen from center, rads
#define BAC_FCOL        sw_col                  // face color
#define BAC_HRCOL       sw_col                  // hour hand color
#define BAC_MNCOL       sw_col                  // minute hand color
#define BAC_SCCOL       GRAY                    // second hand color
#define BAC_BKCOL       RA8875_BLUE             // Back button color
#define BAC_BEZCOL      GRAY                    // bezel color
#define BAC_DATEX       2                       // date box X -- just to anchor text
#define BAC_DATEY       2                       // date box Y -- just to anchor text
#define BAC_DATEW       200                     // date box width -- used just for tapping
#define BAC_DATEH       150                     // date box height -- used just for tapping
#define BAC_WXX         (800-PLOTBOX_W-1)       // weather box X
#define BAC_WXY         5                       // weather box Y
#define BAC_WXW         PLOTBOX_W               // weather box width
#define BAC_WXH         PLOTBOX_H               // weather box height
#define BAC_WXGDT       (30L*60*1000)           // weather update period when good, millis
#define BAC_WXFDT       (6*1000)                // weather update period when fail, millis

// big digital clock params
#define BDC_W           100                     // digit width
#define BDC_H           (2*BDC_W)               // digit height
#define BDC_X0          (400-3*BDC_W)           // left x
#define BDC_Y0          (BAC_WXY+BAC_WXH+20)    // top y
#define BDC_FLT         5                       // segment thickness as fraction of BDC_W
#define BDC_GAP         (BDC_W/2)               // gap between adjacent digits
#define BDC_CR          (BDC_W/BDC_FLT/2)       // colon radius

// contols common to both big clock styles
#define BC_ALM_X        2                       // x coord of alarm time box
#define BC_ALM_Y        380                     // y coord of alarm time box
#define BC_CDP_X        2                       // countdown period x
#define BC_CDP_Y        (BC_ALM_Y+SW_BH)        // countdown period y 
#define BC_BAD_W        200                     // bad time message width
#define BC_BAD_H        SW_BH                   // bad time message height
#define BC_BAD_X        (800-BC_BAD_W-2)        // x coord of bad time message
#define BC_BAD_Y        BC_CDP_Y                // y coord of bad time message
#define BC_EXIT_X       500                     // exit area x coord
#define BC_EXIT_Y       300                     // exit area y coord


// sanity checks
#if BDC_X0+BDC_W+BDC_GAP+BDC_W+2*BDC_GAP+BDC_W+BDC_GAP+BDC_W+BDC_GAP > 800
#error Big Digital Clock digits too wide
#endif
#if BDC_Y0+BDC_H > 480
#error Big Digital Clock digits too tall
#endif

// current state
static SWEngineState sws_engine;                // what is _running_
static SWDisplayState sws_display;              // what is _displaying_
static uint32_t countdown_period;               // count down from here, ms
static uint8_t swdigits[SW_NDIG];               // current digits
static uint32_t start_t, stop_dt;               // millis() at start, since stop
static uint16_t alarm_hrmn;                     // alarm time, hr*60 + min
static time_t alarm_ringtime;                   // now() when alarm started ringing
static AlarmState alarm_state;                  // whether off, armed or ringing

// button labels
static char cd_lbl[] = "Count down";
static char lap_lbl[] = "Lap";
static char reset_lbl[] = "Reset";
static char resume_lbl[] = "Resume";
static char run_lbl[] = "Run";
static char stop_lbl[] = "Stop";
static char exit_lbl[] = "Exit";
static char bigclock_lbl[] = "Big Clock";

// stopwatch controls
static SBox countdown_lbl_b = {SW_CD_X, SW_CD_Y, SW_CD_W, SW_BH};
static SBox cdtime_dsp_b = {SW_CDP_X, SW_CD_Y, SW_CDP_W, SW_BH};
static SBox cdtime_up_b = {SW_CDP_X, SW_CD_Y-SW_BH/2, SW_CDP_W, SW_BH};
static SBox cdtime_dw_b = {SW_CDP_X, SW_CD_Y+SW_BH/2, SW_CDP_W, SW_BH};
static SBox A_b = {SW_BAX, SW_BY, SW_BW, SW_BH};
static SBox B_b = {SW_BBX, SW_BY, SW_BW, SW_BH};
static SBox exit_b = {SW_EXITX, SW_EXITY, SW_BW, SW_BH};
static SBox bigclock_b = {SW_BCX, SW_BCY, SW_BW, SW_BH};
static SBox color_b = {SW_CX, SW_CY, SW_CW, SW_CH};
static uint8_t sw_hue;                          // hue 0..255
static uint16_t sw_col;                         // color pixel

// big clock controls
static SBox bcdate_b = {BAC_DATEX, BAC_DATEY, BAC_DATEW, BAC_DATEH};
static SBox bcwx_b = {BAC_WXX, BAC_WXY, BAC_WXW, BAC_WXH};      // weather
static SBox bccd_b = {BC_CDP_X, BC_CDP_Y, SW_BW, SW_BH};        // countdown remaining and control
static SBox bcalarm_b = {BC_ALM_X, BC_ALM_Y, SW_BW, SW_BH};     // alarm time and control
static uint16_t bc_bits;                        // see SWBCBits
static uint32_t bc_prev_wx;                     // time of prev drawn wx, millis
static uint32_t bc_wxdt = BAC_WXGDT;            // weather update interval, millis

// alarm clock controls on main sw page
static SBox alarm_lbl_b = {ALM_X0, ALM_Y0, ALM_W, SW_BH};
static SBox alarm_hrmn_b = {ALM_EX, ALM_EY, ALM_EW, SW_BH};
static SBox alarm_up_b = {ALM_EX, ALM_EY-SW_BH/2, ALM_EW, SW_BH};
static SBox alarm_dw_b = {ALM_EX, ALM_EY+SW_BH/2, ALM_EW, SW_BH};


/* save persistent state and log
 */
static void saveSWNV()
{
    NVWriteUInt16 (NV_BCFLAGS, bc_bits);
    NVWriteUInt32 (NV_CD_PERIOD, countdown_period);

    uint16_t acode = alarm_hrmn;
    if (alarm_state != ALMS_OFF)
        acode += ALM_TOVFLOW;
    NVWriteUInt16 (NV_ALARMCLOCK, acode);

    logState();
}

/* return ms countdown time remaining, if any
 */
static uint32_t getCountdownLeft()
{
    if (sws_engine == SWE_COUNTDOWN) {
        uint32_t since_start = millis() - start_t;
        if (since_start < countdown_period)
            return (countdown_period - since_start);
    }
    return (0);
}


/* set sw_col from sw_hue
 */
static void setSWColor()
{
    uint8_t r, g, b;
    hsvtorgb (&r, &g, &b, sw_hue, SW_HSV_S, SW_HSV_V);
    sw_col = RGB565 (r, g, b);
}

/* draw the current countdown_period if currently on the main SW page
 */
static void drawSWCDPeriod()
{
    if (sws_display == SWD_MAIN) {
        char buf[20];
        int mins = countdown_period/60000;
        snprintf (buf, sizeof(buf), "%d %s", mins, mins > 1 ? "mins" : "min");
        drawStringInBox (buf, cdtime_dsp_b, false, sw_col);
    }
}

/* draw the color control box
 */
static void drawColorScale()
{
    // erase to remove tick marks
    tft.fillRect (color_b.x, color_b.y, color_b.w, color_b.h, RA8875_BLACK);

    // rainbow
    for (uint16_t dx = 0; dx < color_b.w; dx++) {
        uint8_t r, g, b;
        uint8_t h = 255*dx/color_b.w;
        hsvtorgb (&r, &g, &b, h, SW_HSV_S, SW_HSV_V);
        uint16_t c = RGB565 (r, g, b);
        // tft.drawLine (color_b.x + dx, color_b.y, color_b.x + dx, color_b.y + color_b.h, c);
        tft.drawPixel (color_b.x + dx, color_b.y + color_b.h/2, c);
    }

    // mark it
    uint16_t hue_x = color_b.x + sw_hue*color_b.w/255;
    tft.drawLine (hue_x, color_b.y+3*color_b.h/8, hue_x, color_b.y+5*color_b.h/8, RA8875_WHITE);
}

/* draw the given digit in the given bounding box with lines the given fractional thickness of box width.
 */
static void drawDigit (const SBox &b, int digit, int frac_thick)
{
    uint16_t lt = b.w/frac_thick;
    uint16_t l2 = b.w/(2*frac_thick);

    // erase 
    tft.fillRect (b.x, b.y, b.w, b.h, SW_BG);

    // draw digit -- replace with drawRect to check boundaries
    switch (digit) {
    case 0:
        tft.fillRect (b.x, b.y, b.w, lt, sw_col);
        tft.fillRect (b.x, b.y+lt, lt, b.h-2*lt, sw_col);
        tft.fillRect (b.x, b.y+b.h-lt, b.w, lt, sw_col);
        tft.fillRect (b.x+b.w-lt, b.y+lt, lt, b.h-2*lt, sw_col);
        break;
    case 1:
        tft.fillRect (b.x+b.w/2-l2, b.y, lt, b.h, sw_col);      // center column
        break;
    case 2:
        tft.fillRect (b.x, b.y, b.w, lt, sw_col);
        tft.fillRect (b.x+b.w-lt, b.y+lt, lt, b.h/2-lt-l2, sw_col);
        tft.fillRect (b.x, b.y+b.h/2-l2, b.w, lt, sw_col);
        tft.fillRect (b.x, b.y+b.h/2+l2, lt, b.h/2-lt-l2, sw_col);
        tft.fillRect (b.x, b.y+b.h-lt, b.w, lt, sw_col);
        break;
    case 3:
        tft.fillRect (b.x, b.y, b.w, lt, sw_col);
        tft.fillRect (b.x+b.w-lt, b.y+lt, lt, b.h-2*lt, sw_col);
        tft.fillRect (b.x, b.y+b.h/2-l2, b.w-lt, lt, sw_col);
        tft.fillRect (b.x, b.y+b.h-lt, b.w, lt, sw_col);
        break;
    case 4:
        tft.fillRect (b.x, b.y, lt, b.h/2+l2, sw_col);
        tft.fillRect (b.x+lt, b.y+b.h/2-l2, b.w-2*lt, lt, sw_col);
        tft.fillRect (b.x+b.w-lt, b.y, lt, b.h, sw_col);
        break;
    case 5:
        tft.fillRect (b.x, b.y, b.w, lt, sw_col);
        tft.fillRect (b.x, b.y+lt, lt, b.h/2-lt-l2, sw_col);
        tft.fillRect (b.x, b.y+b.h/2-l2, b.w, lt, sw_col);
        tft.fillRect (b.x+b.w-lt, b.y+b.h/2+l2, lt, b.h/2-lt-l2, sw_col);
        tft.fillRect (b.x, b.y+b.h-lt, b.w, lt, sw_col);
        break;
    case 6:
        tft.fillRect (b.x, b.y, lt, b.h, sw_col);
        tft.fillRect (b.x+lt, b.y, b.w-lt, lt, sw_col);
        tft.fillRect (b.x+lt, b.y+b.h/2-l2, b.w-lt, lt, sw_col);
        tft.fillRect (b.x+b.w-lt, b.y+b.h/2+l2, lt, b.h/2-l2-lt, sw_col);
        tft.fillRect (b.x+lt, b.y+b.h-lt, b.w-lt, lt, sw_col);
        break;
    case 7:
        tft.fillRect (b.x, b.y, b.w, lt, sw_col);
        tft.fillRect (b.x+b.w-lt, b.y+lt, lt, b.h-lt, sw_col);
        break;
    case 8:
        tft.fillRect (b.x, b.y, lt, b.h, sw_col);
        tft.fillRect (b.x+b.w-lt, b.y, lt, b.h, sw_col);
        tft.fillRect (b.x+lt, b.y, b.w-2*lt, lt, sw_col);
        tft.fillRect (b.x+lt, b.y+b.h/2-l2, b.w-2*lt, lt, sw_col);
        tft.fillRect (b.x+lt, b.y+b.h-lt, b.w-2*lt, lt, sw_col);
        break;
    case 9:
        tft.fillRect (b.x, b.y, lt, b.h/2+l2, sw_col);
        tft.fillRect (b.x+b.w-lt, b.y, lt, b.h, sw_col);
        tft.fillRect (b.x+lt, b.y, b.w-2*lt, lt, sw_col);
        tft.fillRect (b.x+lt, b.y+b.h/2-l2, b.w-2*lt, lt, sw_col);
        break;
    default:
        Serial.printf (_FX("Bug! drawDigit %d\n"), digit);
        break;
    }
}


/* draw the given stopwatch digit in the given position 0 .. SW_NDIG-1.
 * use swdigits[] to avoid erasing/redrawing the same digit again.
 */
static void drawSWDigit (uint8_t position, uint8_t digit)
{
    // check for no change
    if (swdigits[position] == digit)
        return;
    swdigits[position] = digit;

    // bounding box
    SBox b;
    b.x = SW_X0 + (SW_DW+SW_DGAP)*position;
    b.y = SW_Y0;
    b.w = SW_DW;
    b.h = SW_DH;

    // draw
    drawDigit (b, digit, SW_FLT);
}

/* display the given time value in millis()
 */
static void drawSWTime(uint32_t t)
{
    int ndig = 0;

    t %= (100UL*60UL*60UL*1000UL);                        // msec in SW_NDIG digits

    uint8_t tenhr = t / (10UL*3600UL*1000UL);
    t -= tenhr * (10UL*3600UL*1000UL);
    drawSWDigit (0, tenhr);
    ndig++;

    uint8_t onehr = t / (3600UL*1000UL);
    t -= onehr * (3600UL*1000UL);
    drawSWDigit (1, onehr);
    ndig++;

    uint8_t tenmn = t / (600UL*1000UL);
    t -= tenmn * (600UL*1000UL);
    drawSWDigit (2, tenmn);
    ndig++;

    uint8_t onemn = t / (60UL*1000UL);
    t -= onemn * (60UL*1000UL);
    drawSWDigit (3, onemn);
    ndig++;

    uint8_t tensec = t / (10UL*1000UL);
    t -= tensec * (10UL*1000UL);
    drawSWDigit (4, tensec);
    ndig++;

    uint8_t onesec = t / (1UL*1000UL);
    t -= onesec * (1UL*1000UL);
    drawSWDigit (5, onesec);
    ndig++;

    uint8_t tenthsec = t / (100UL);
    t -= tenthsec * (100UL);
    drawSWDigit (6, tenthsec);
    ndig++;

    uint8_t hundsec = t / (10UL);
    t -= hundsec * (10UL);
    drawSWDigit (7, hundsec);
    ndig++;

    if (ndig != SW_ND)
        fatalError (_FX("Bug! stopwatch %d != %d"), ndig, SW_ND);
}


/* given countdown time remaining, find range and button text color
 */
static void determineCDVisuals (uint32_t ms_left, SWCDState &cds, uint16_t &color)
{
    if (ms_left >= SW_CD_WARNDT) {
        cds = SWCDS_RUNOK;
        color = RA8875_GREEN;
        return;
    }

    bool flash_on = (millis()%500) < 250;               // flip at 2 Hz
    if (ms_left > 0) {
        if (flash_on) {
            cds = SWCDS_WARN_ON;
            color = DYELLOW;
        } else {
            cds = SWCDS_OFF;
            color = RA8875_BLACK;
        }
    } else {
        if (flash_on) {
            cds = SWCDS_TIMEOUT_ON;
            color = RA8875_RED;
        } else {
            cds = SWCDS_OFF;
            color = RA8875_BLACK;
        }
    }
}

/* draw alarm_hrmn, pin and label if requested in various ways depending on sws_display
 */
static void drawAlarmIndicator (bool label_too)
{
    // pin
    setAlarmPin (alarm_state == ALMS_RINGING);

    // prep
    char buf[30];
    selectFontStyle (BOLD_FONT, SMALL_FONT);
    uint16_t a_hr = alarm_hrmn/60;
    uint16_t a_mn = alarm_hrmn%60;

    if (sws_display == SWD_MAIN) {
        snprintf (buf, sizeof(buf), "%02d:%02d", a_hr, a_mn);
        drawStringInBox (buf, alarm_hrmn_b, false, sw_col);
        if (label_too) {
            const char *lbl = "?";
            switch (alarm_state) {
            case ALMS_OFF:     lbl = "Alarm off";   break;
            case ALMS_ARMED:   lbl = "Alarm armed"; break;
            case ALMS_RINGING: lbl = "Alarm!";      break;
            }
            drawStringInBox (lbl, alarm_lbl_b, alarm_state == ALMS_RINGING, sw_col);
        }
    } else if (sws_display == SWD_BCDIGITAL || sws_display == SWD_BCANALOG) {
        if (alarm_state == ALMS_OFF) {
            if (label_too) {
                // this is so web command set_alarm?off can actually erase the alarm box
                tft.fillRect (bcalarm_b.x, bcalarm_b.y, bcalarm_b.w, bcalarm_b.h, RA8875_BLACK);
            }
        } else if (alarm_state == ALMS_ARMED) {
            snprintf (buf, sizeof(buf), "A: %02d:%02d", a_hr, a_mn);
            drawStringInBox (buf, bcalarm_b, false, sw_col);
        } else if (alarm_state == ALMS_RINGING) {
            drawStringInBox ("Alarm!", bcalarm_b, true, sw_col);
        }

        #if defined(_SHOW_ALL)
            tft.drawRect (bcalarm_b.x, bcalarm_b.y, bcalarm_b.w, bcalarm_b.h, RA8875_WHITE);
        #endif
    }
}

/* return whether alarm has gone off since previous call.
 * N.B. we assume this will be called more than once per minute
 */
static bool checkAlarm()
{
    // get de time hrmn
    time_t de_t0 = nowWO() + de_tz.tz_secs;
    uint16_t de_hrmn = hour(de_t0)*60U + minute(de_t0);

    // wentoff unless still in same minute
    static uint16_t prev_de_hrmn = 24*60;       // init to impossible hrmn
    bool wentoff = de_hrmn == alarm_hrmn && de_hrmn != prev_de_hrmn;
    prev_de_hrmn = de_hrmn;

    return (wentoff);
}

/* draw remaining count down time and manage the state of the count down button and LED.
 * N.B. we handle all display states but assume sws_engine == SWE_COUNTDOWN 
 */
static void drawCDTimeRemaining(bool force)
{
    // sanity check: this function is only for countdown
    if (sws_engine != SWE_COUNTDOWN)
        return;

    // not crazy fast unless force
    static uint32_t gate;
    if (!force && !timesUp (&gate, 31))
        return;

    // get ms remaining 
    uint32_t ms_left = getCountdownLeft();

    // determine range and color
    SWCDState cds;
    uint16_t color;
    determineCDVisuals (ms_left, cds, color);

    // set LEDS
    setLEDState (cds);

    if (sws_display == SWD_MAIN) {

        // showing main stopwatch page at full ms resolution

        // show time using the 7-seg displays
        drawSWTime(ms_left);

        // determine whether to display inverted
        static bool prev_inv;
        bool inv = cds == SWCDS_RUNOK || cds == SWCDS_WARN_ON || cds == SWCDS_TIMEOUT_ON;

        // update the countdown button if different or force
        if (force || inv != prev_inv) {
            drawStringInBox (cd_lbl, countdown_lbl_b, inv, sw_col);
            prev_inv = inv;
        }

    } else {

        // the other display states share a common time format so build that first

        // break into H:M:S
        ms_left += 500U;                         // round to nearest second
        uint8_t hr = ms_left / 3600000U;
        ms_left -= hr * 3600000U;
        uint8_t mn = ms_left / 60000U;
        ms_left -= mn * 60000U;
        uint8_t sc = ms_left/1000U;

        // avoid repeating the same time and color
        static uint8_t prev_sc;
        static uint16_t prev_color;
        if (color == prev_color && sc == prev_sc && !force)
            return;

        // format
        char buf[32];
        if (hr == 0)
            sprintf (buf, "%d:%02d", mn, sc);
        else
            sprintf (buf, "%dh%02d", hr, mn);

        if (sws_display == SWD_NONE) {

            // main Hamclock page

            // overwrite stopwatch icon
            selectFontStyle (LIGHT_FONT, FAST_FONT);
            uint16_t cdw = getTextWidth(buf);
            tft.fillRect (stopwatch_b.x, stopwatch_b.y, stopwatch_b.w, stopwatch_b.h, RA8875_BLACK);
            tft.setTextColor (color);
            tft.setCursor (stopwatch_b.x + (stopwatch_b.w-cdw)/2, stopwatch_b.y+stopwatch_b.h/4);
            tft.print (buf);

            // draw pane if showing
            PlotPane cdp = findPaneChoiceNow(PLOT_CH_COUNTDOWN);
            if (cdp != PANE_NONE) {

                // find box
                SBox box = plot_b[cdp];

                // prep if force
                if (force) {
                    prepPlotBox (box);

                    // title
                    static const char title[] = "Countdown timer";
                    selectFontStyle (BOLD_FONT, FAST_FONT);
                    uint16_t w = getTextWidth(title);
                    tft.setCursor (box.x + (box.w - w)/2, box.y + 3);
                    tft.setTextColor (RA8875_GREEN);
                    tft.print (title);
                }

                // time remaining, don't blink
                static uint16_t prev_pane_color;
                uint16_t pane_color = color == RA8875_BLACK ? prev_pane_color : color;
                if (force || sc != prev_sc || pane_color != prev_pane_color) {
                    selectFontStyle (BOLD_FONT, LARGE_FONT);
                    uint16_t w = getTextWidth(buf);
                    tft.fillRect (box.x+10, box.y+box.h/3, box.w-20, box.h/3, RA8875_BLACK);
                    tft.setCursor (box.x + (box.w - w)/2, box.y + 2*box.h/3 - 5);
                    tft.setTextColor (pane_color);
                    tft.print(buf);
                    prev_pane_color = pane_color;
                }
            }

        } else if (sws_display == SWD_BCDIGITAL || sws_display == SWD_BCANALOG) {

            selectFontStyle (BOLD_FONT, SMALL_FONT);
            drawStringInBox (buf, bccd_b, false, color);

        }

        // remember
        prev_sc = sc;
        prev_color = color;
    }
}




/* draw either BigClock state awareness message as needed
 */
static void drawBCAwareness (bool force)
{
    // whether time was ok last iteration
    static bool time_was_ok;

    // get current state
    bool clock_ok = clockTimeOk();
    bool ut_zero = utcOffset() == 0;
    bool time_ok_now = clock_ok && ut_zero;

    // update if force or new state
    if (time_ok_now) {
        if (force || !time_was_ok) {
            // erase
            tft.fillRect (BC_BAD_X, BC_BAD_Y, BC_BAD_W, BC_BAD_H, RA8875_BLACK);
            Serial.print (F("SW: time ok now\n"));
        }
    } else {
        if (force || time_was_ok) {
            selectFontStyle (BOLD_FONT, SMALL_FONT);
            tft.setCursor (BC_BAD_X, BC_BAD_Y+27);
            tft.setTextColor (RA8875_RED);
            if (clock_ok) {
                static const char msg[] = "Time is offset";
                tft.print (msg);
                Serial.printf ("SW: %s\n", msg);
            } else {
                static const char msg[] = "Time unlocked";
                tft.print (msg);
                Serial.printf ("SW: %s\n", msg);
            }
        }
    }

    // persist
    time_was_ok = time_ok_now;
}


/* refresh detailed date info in bcdate_b.
 * N.B. we never erase because Wednesday overlays clock
 */
static void drawBCDate (int hr, int dy, int wd, int mo)
{
    // day
    selectFontStyle (BOLD_FONT, LARGE_FONT);
    tft.setTextColor (BAC_FCOL);
    tft.setCursor (bcdate_b.x, bcdate_b.y + 50);
    tft.print (dayStr(wd));

    // month
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setCursor (bcdate_b.x, bcdate_b.y + 90);
    if (useMetricUnits())
        tft.printf (_FX("%d %s"), dy, monthStr(mo));
    else
        tft.printf (_FX("%s %d"), monthStr(mo), dy);

    // AM/PM only for analog or 12 hour digital
    if (sws_display == SWD_BCANALOG || (bc_bits & SW_DB12HBIT)) {
        tft.setCursor (bcdate_b.x, bcdate_b.y + 125);
        tft.print (hr < 12 ? "AM" : "PM");
    }

    #if defined(_SHOW_ALL)
        tft.drawRect (bcdate_b.x, bcdate_b.y, bcdate_b.w, bcdate_b.h, RA8875_WHITE);
    #endif
}

/* refresh DE weather in bcwx_b, return whether successful
 */
static bool drawBCWx(void)
{
    WXInfo wi;
    char ynot[100];
    bool ok = getCurrentWX (de_ll, true, &wi, ynot);
    if (ok)
        plotWX (bcwx_b, BAC_FCOL, wi);
    else
        plotMessage (bcwx_b, RA8875_RED, ynot);

    // undo border
    tft.drawRect (bcwx_b.x, bcwx_b.y, bcwx_b.w, bcwx_b.h, RA8875_BLACK);

    return (ok);
}

/* draw the given big digital clock digit in the given position 0 .. 3
 */
static void drawBDCDigit (uint8_t position, uint8_t digit)
{
    // bounding box
    SBox b;
    b.x = BDC_X0 + (BDC_W+BDC_GAP)*position + (position >= 2)*BDC_GAP;
    b.y = BDC_Y0;
    b.w = BDC_W;
    b.h = BDC_H;

    // draw
    drawDigit (b, digit, BDC_FLT);
}

/* draw the digital Big Clock 
 */
static void drawDigitalBigClock (bool all)
{
    // persist to avoid drawing the same digits again
    static time_t prev_t0;                              // previous report time
    static uint8_t prev_mnten, prev_mnmn;               // previous mins tens and unit
    static uint8_t prev_hr, prev_mo, prev_dy;           // previous drawn date info

    // get local time now, including any user offset
    time_t t0 = nowWO() + de_tz.tz_secs;

    // done if same second unless all
    if (!all && t0 == prev_t0)
        return;

    // crack open
    uint8_t hr = hour(t0);
    uint8_t mn = minute(t0);
    int dy = day(t0);
    int mo = month(t0);
    uint8_t mnmn = mn%10;

    // initial erase or showing date and it's a new day
    if (all || ((bc_bits & SW_BCDATEBIT) && (dy != prev_dy || mo != prev_mo))) {
        eraseScreen();
        all = true;     // insure everything gets redrawn
        // date
        if (bc_bits & SW_BCDATEBIT) {
            drawBCDate (hr, dy, weekday(t0), mo);
            prev_dy = dy;
            prev_mo = mo;
        }
    }

    // toggle punctuation every second
    if (all || t0 != prev_t0) {
        uint16_t x = BDC_X0 + BDC_W + BDC_GAP + BDC_W + BDC_GAP;
        uint16_t color = all || (t0&1) ? sw_col : SW_BG;
        tft.fillCircle (x, BDC_Y0 + BDC_H/3,   BDC_CR, color);
        tft.fillCircle (x, BDC_Y0 + 2*BDC_H/3, BDC_CR, color);
        prev_t0 = t0;
    }

    // update minutes every minute
    if (all || mnmn != prev_mnmn) {

        // minute for sure
        drawBDCDigit (3, mnmn);
        prev_mnmn = mnmn;

        // and tens of minutes too if changed
        uint8_t mnten = mn/10;
        if (all || mnten != prev_mnten) {
            drawBDCDigit (2, mnten);
            prev_mnten = mnten;
        }
    }

    // update hour every hour
    if (all || hr != prev_hr) {
        prev_hr = hr;
        if (bc_bits & SW_DB12HBIT) {
            uint8_t hr12 = hr%12;
            if (hr12 == 0)
                hr12 = 12;
            if (hr12 >= 10)
                drawBDCDigit (0, hr12/10);
            drawBDCDigit (1, hr12%10);
        } else {
            drawBDCDigit (0, hr/10);
            drawBDCDigit (1, hr%10);
        }
    }

    // update awareness
    drawBCAwareness (all);

    // init countdown if first call
    if (all) {
        drawCDTimeRemaining(true);
        drawAlarmIndicator(false);
    }

    // update weather if desired and all or new
    if ((bc_bits & SW_BCWXBIT) && (timesUp(&bc_prev_wx, bc_wxdt) || all))
        bc_wxdt = drawBCWx() ? BAC_WXGDT : BAC_WXFDT;

    #if defined(_SHOW_ALL)
        tft.drawRect (bccd_b.x, bccd_b.y, bccd_b.w, bccd_b.h, RA8875_WHITE);
    #endif

    // immediate
    tft.drawPR();
}


/* draw analog Big Clock
 */
static void drawAnalogBigClock (bool all)
{
    // points 1 and 2 are the fat positions part way out, point 3 is the far tip, "point" 0 is the center

    // persistent time measures
    static time_t prev_t0;                              // detect change of secs
    static uint8_t prev_mo, prev_dy;                    // previously drawn date info

    // previous hand positions for motion detection and exact erasing
    static int16_t prev_hrdx1, prev_hrdx2, prev_hrdx3, prev_hrdy1, prev_hrdy2, prev_hrdy3;
    static int16_t prev_mndx1, prev_mndx2, prev_mndx3, prev_mndy1, prev_mndy2, prev_mndy3;
    static int16_t prev_scdx3, prev_scdy3;

    // get local time now, including any user offset
    time_t t0 = nowWO() + de_tz.tz_secs;

    // wait for second to change unless all
    if (!all && t0 == prev_t0)
        return;
    prev_t0 = t0;

    // crack open
    int hr = hour(t0);
    int mn = minute(t0);
    int sc = second(t0);
    int dy = day(t0);
    int mo = month(t0);

    // refresh if desired or new date (since we never erase the date)
    if (all || ((bc_bits & SW_BCDATEBIT) && (dy != prev_dy || mo != prev_mo))) {

        // fresh face
        eraseScreen();

        // face perimeter
      #if defined (_IS_ESP8266)
        // avoids bright flash of circle filling but doesn't fill at higher display sizes
        for (uint16_t r = BAC_FR+1; r <= BAC_BEZR; r++)
            tft.drawCircle (BAC_X0, BAC_Y0, r, BAC_BEZCOL);
      #else
        tft.fillCircle (BAC_X0, BAC_Y0, BAC_BEZR, BAC_BEZCOL);
        tft.fillCircle (BAC_X0, BAC_Y0, BAC_FR, RA8875_BLACK);
      #endif
        tft.drawCircle (BAC_X0, BAC_Y0, BAC_FR, BAC_FCOL);

        // hour points
        for (int i = 0; i < 12; i++) {
            float a = deg2rad(360.0F*i/12.0F);
            uint16_t x0 = roundf(BAC_X0 + (BAC_FR-BAC_HTR) * cosf(a));
            uint16_t y0 = roundf(BAC_Y0 + (BAC_FR-BAC_HTR) * sinf(a));
            uint16_t x1 = roundf(BAC_X0 + BAC_FR * cosf(a-BAC_HTTH));
            uint16_t y1 = roundf(BAC_Y0 + BAC_FR * sinf(a-BAC_HTTH));
            uint16_t x2 = roundf(BAC_X0 + BAC_FR * cosf(a+BAC_HTTH));
            uint16_t y2 = roundf(BAC_Y0 + BAC_FR * sinf(a+BAC_HTTH));
            tft.drawLine (x0, y0, x1, y1, 1, BAC_FCOL);
            tft.drawLine (x0, y0, x2, y2, 1, BAC_FCOL);
        }

        // minute ticks
        for (int i = 0; i < 60; i++) {
            if ((i % 5) == 0)
                continue;                               // don't overdraw hour marks
            float a = deg2rad(360.0F*i/60.0F);
            uint16_t x0 = roundf(BAC_X0 + BAC_FR * cosf(a));
            uint16_t y0 = roundf(BAC_Y0 + BAC_FR * sinf(a));
            uint16_t x1 = roundf(BAC_X0 + (BAC_FR-BAC_MTR) * cosf(a));
            uint16_t y1 = roundf(BAC_Y0 + (BAC_FR-BAC_MTR) * sinf(a));
            tft.drawLine (x0, y0, x1, y1, 1, BAC_FCOL);
        }

        // init all locations bogus but inside face and not 0
        prev_hrdx1 = prev_hrdy1 = 10;
        prev_mndx1 = prev_mndy1 = 10;
        prev_hrdx2 = prev_hrdy2 = 20;
        prev_mndx2 = prev_mndy2 = 20;
        prev_hrdx3 = prev_hrdy3 = 30;
        prev_mndx3 = prev_mndy3 = 30;
        prev_scdx3 = prev_scdy3 = 30;

        // date
        if ((bc_bits & SW_BCDATEBIT)) {
            drawBCDate (hr, dy, weekday(t0), mo);
            prev_dy = dy;
            prev_mo = mo;
        }
    }

    // find central angle and far tip location of each hand
    float hr_angle = deg2rad(30*(3-(((hr+24)%12) + mn/60.0F)));
    float mn_angle = deg2rad(6*(15-(mn+sc/60.0F)));
    float sc_angle = deg2rad(6*(15-sc));
    int16_t hrdx3 = roundf(BAC_HRR * cosf(hr_angle));
    int16_t hrdy3 = roundf(BAC_HRR * sinf(hr_angle));
    int16_t mndx3 = roundf(BAC_MNR * cosf(mn_angle));
    int16_t mndy3 = roundf(BAC_MNR * sinf(mn_angle));
    int16_t scdx3 = roundf(BAC_SCR * cosf(sc_angle));
    int16_t scdy3 = roundf(BAC_SCR * sinf(sc_angle));

    // erase and update hand position if far tip moved
    bool hr_moved = hrdx3 != prev_hrdx3 || hrdy3 != prev_hrdy3;
    bool mn_moved = mndx3 != prev_mndx3 || mndy3 != prev_mndy3;
    bool sc_moved = scdx3 != prev_scdx3 || scdy3 != prev_scdy3;
    if (hr_moved) {
        tft.drawLine (BAC_X0, BAC_Y0, BAC_X0+prev_hrdx1, BAC_Y0-prev_hrdy1, 1, RA8875_BLACK);
        tft.drawLine (BAC_X0, BAC_Y0, BAC_X0+prev_hrdx2, BAC_Y0-prev_hrdy2, 1, RA8875_BLACK);
        tft.drawLine (BAC_X0+prev_hrdx1,BAC_Y0-prev_hrdy1,BAC_X0+prev_hrdx3,BAC_Y0-prev_hrdy3,1,RA8875_BLACK);
        tft.drawLine (BAC_X0+prev_hrdx2,BAC_Y0-prev_hrdy2,BAC_X0+prev_hrdx3,BAC_Y0-prev_hrdy3,1,RA8875_BLACK);
        prev_hrdx1 = roundf(BAC_HRR/3.0F * cosf(hr_angle-BAC_HRTH));
        prev_hrdy1 = roundf(BAC_HRR/3.0F * sinf(hr_angle-BAC_HRTH));
        prev_hrdx2 = roundf(BAC_HRR/3.0F * cosf(hr_angle+BAC_HRTH));
        prev_hrdy2 = roundf(BAC_HRR/3.0F * sinf(hr_angle+BAC_HRTH));
        prev_hrdx3 = hrdx3;
        prev_hrdy3 = hrdy3;
    }
    if (mn_moved) {
        tft.drawLine (BAC_X0, BAC_Y0, BAC_X0+prev_mndx1, BAC_Y0-prev_mndy1, 1, RA8875_BLACK);
        tft.drawLine (BAC_X0, BAC_Y0, BAC_X0+prev_mndx2, BAC_Y0-prev_mndy2, 1, RA8875_BLACK);
        tft.drawLine (BAC_X0+prev_mndx1,BAC_Y0-prev_mndy1,BAC_X0+prev_mndx3,BAC_Y0-prev_mndy3,1,RA8875_BLACK);
        tft.drawLine (BAC_X0+prev_mndx2,BAC_Y0-prev_mndy2,BAC_X0+prev_mndx3,BAC_Y0-prev_mndy3,1,RA8875_BLACK);
        prev_mndx1 = roundf(BAC_MNR/3.0F * cosf(mn_angle-BAC_MNTH));
        prev_mndy1 = roundf(BAC_MNR/3.0F * sinf(mn_angle-BAC_MNTH));
        prev_mndx2 = roundf(BAC_MNR/3.0F * cosf(mn_angle+BAC_MNTH));
        prev_mndy2 = roundf(BAC_MNR/3.0F * sinf(mn_angle+BAC_MNTH));
        prev_mndx3 = mndx3;
        prev_mndy3 = mndy3;
    }
    if (sc_moved) {
        tft.drawLine (BAC_X0, BAC_Y0, BAC_X0+prev_scdx3, BAC_Y0-prev_scdy3, 1, RA8875_BLACK);
        prev_scdx3 = scdx3;
        prev_scdy3 = scdy3;
    }

    // draw hand if moved or likely clobbered by another hand erasure
    float hr_sc_angle = fabsf(hr_angle - sc_angle);
    float hr_mn_angle = fabsf(hr_angle - mn_angle);
    float mn_sc_angle = fabsf(mn_angle - sc_angle);
    bool hrsc_hit = hr_sc_angle < 2*BAC_HRTH || hr_sc_angle > 2*M_PIF - 2*BAC_HRTH;    // fudge fat bottom
    bool hrmn_hit = hr_mn_angle < 2*BAC_HRTH || hr_mn_angle > 2*M_PIF - 2*BAC_HRTH;
    bool mnsc_hit = mn_sc_angle < 2*BAC_MNTH || mn_sc_angle > 2*M_PIF - 2*BAC_MNTH;
    if (hr_moved || hrsc_hit || hrmn_hit) {
        tft.drawLine (BAC_X0, BAC_Y0, BAC_X0+prev_hrdx1, BAC_Y0-prev_hrdy1, 1, BAC_HRCOL);
        tft.drawLine (BAC_X0, BAC_Y0, BAC_X0+prev_hrdx2, BAC_Y0-prev_hrdy2, 1, BAC_HRCOL);
        tft.drawLine (BAC_X0+prev_hrdx1,BAC_Y0-prev_hrdy1,BAC_X0+prev_hrdx3,BAC_Y0-prev_hrdy3,1,BAC_HRCOL);
        tft.drawLine (BAC_X0+prev_hrdx2,BAC_Y0-prev_hrdy2,BAC_X0+prev_hrdx3,BAC_Y0-prev_hrdy3,1,BAC_HRCOL);
    }
    if (mn_moved || hrmn_hit || mnsc_hit) {
        tft.drawLine (BAC_X0, BAC_Y0, BAC_X0+prev_mndx1, BAC_Y0-prev_mndy1, 1, BAC_MNCOL);
        tft.drawLine (BAC_X0, BAC_Y0, BAC_X0+prev_mndx2, BAC_Y0-prev_mndy2, 1, BAC_MNCOL);
        tft.drawLine (BAC_X0+prev_mndx1,BAC_Y0-prev_mndy1,BAC_X0+prev_mndx3,BAC_Y0-prev_mndy3,1,BAC_MNCOL);
        tft.drawLine (BAC_X0+prev_mndx2,BAC_Y0-prev_mndy2,BAC_X0+prev_mndx3,BAC_Y0-prev_mndy3,1,BAC_MNCOL);
    }
    if (sc_moved || hrsc_hit || mnsc_hit) {
        if ((bc_bits & SW_ANOSHBIT) == 0)
            tft.drawLine (BAC_X0, BAC_Y0, BAC_X0+prev_scdx3, BAC_Y0-prev_scdy3, 1, BAC_SCCOL);
    }

    // center dot
    tft.fillCircle (BAC_X0, BAC_Y0, BAC_DOTR, BAC_BEZCOL);

    // update awareness
    drawBCAwareness (all);

    // init countdown if first call
    if (all) {
        drawCDTimeRemaining(true);
        drawAlarmIndicator(false);
    }

    #if defined(_SHOW_ALL)
        tft.drawRect (bccd_b.x, bccd_b.y, bccd_b.w, bccd_b.h, RA8875_WHITE);
    #endif

    // immediate
    tft.drawPR();

    // update weather if desired and all or new
    if ((bc_bits & SW_BCWXBIT) && (timesUp(&bc_prev_wx, bc_wxdt) || all))
        bc_wxdt = drawBCWx() ? BAC_WXGDT : BAC_WXFDT;

}

/* draw stopwatch in any possible display state
 */
static void drawSWState()
{
    switch (sws_display) {
    case SWD_MAIN:

        switch (sws_engine) {
        case SWE_RESET:
            drawSWTime(0);
            drawStringInBox (cd_lbl, countdown_lbl_b, false, sw_col);
            drawStringInBox (run_lbl, A_b, false, sw_col);
            drawStringInBox (reset_lbl, B_b, false, sw_col);
            drawSWCDPeriod();
            setLEDState (SWCDS_OFF);
            break;
        case SWE_RUN:
            drawStringInBox (cd_lbl, countdown_lbl_b, false, sw_col);
            drawStringInBox (stop_lbl, A_b, false, sw_col);
            drawStringInBox (lap_lbl, B_b, false, sw_col);
            drawSWCDPeriod();
            setLEDState (SWCDS_OFF);
            break;
        case SWE_STOP:
            drawSWTime(stop_dt);        // show stopped time
            drawStringInBox (cd_lbl, countdown_lbl_b, false, sw_col);
            drawStringInBox (run_lbl, A_b, false, sw_col);
            drawStringInBox (reset_lbl, B_b, false, sw_col);
            drawSWCDPeriod();
            setLEDState (SWCDS_OFF);
            break;
        case SWE_LAP:
            drawSWTime(stop_dt);        // show lap hold time
            drawStringInBox (cd_lbl, countdown_lbl_b, false, sw_col);
            drawStringInBox (reset_lbl, A_b, false, sw_col);
            drawStringInBox (resume_lbl, B_b, false, sw_col);
            drawSWCDPeriod();
            setLEDState (SWCDS_OFF);
            break;
        case SWE_COUNTDOWN:
            drawStringInBox (cd_lbl, countdown_lbl_b, true, sw_col);
            drawStringInBox (reset_lbl, A_b, false, sw_col);
            drawStringInBox (reset_lbl, B_b, false, sw_col);
            drawSWCDPeriod();
            drawCDTimeRemaining(true);
            break;
        }

        drawAlarmIndicator  (true);

        #if defined(_SHOW_ALL)
            tft.drawRect (alarm_up_b.x, alarm_up_b.y, alarm_up_b.w, alarm_up_b.h, RA8875_WHITE);
            tft.drawRect (alarm_dw_b.x, alarm_dw_b.y, alarm_dw_b.w, alarm_dw_b.h, RA8875_WHITE);
            tft.drawRect (cdtime_up_b.x, cdtime_up_b.y, cdtime_up_b.w, cdtime_up_b.h, RA8875_WHITE);
            tft.drawRect (cdtime_dw_b.x, cdtime_dw_b.y, cdtime_dw_b.w, cdtime_dw_b.h, RA8875_WHITE);
        #endif

        break;


    case SWD_BCDIGITAL:
        drawDigitalBigClock (true);
        break;

    case SWD_BCANALOG:
        drawAnalogBigClock (true);
        break;

    case SWD_NONE:
        drawMainPageStopwatch(true);
        break;
    }
}


/* draw the appropriate Big Clock
 */
static void drawBigClock (bool all)
{
    if (sws_display == SWD_BCDIGITAL)
        drawDigitalBigClock (all);
    else
        drawAnalogBigClock (all);
}


/* draw the main stopwatch page controls.
 * N.B. we do not erase screen, leave that to caller
 */
static void drawSWMainPage()
{
    // get last color, else set default
    if (!NVReadUInt8 (NV_SWHUE, &sw_hue)) {
        sw_hue = 85;    // green
        NVWriteUInt8 (NV_SWHUE, sw_hue);
    }
    setSWColor();

    // buttons
    selectFontStyle (BOLD_FONT, SMALL_FONT);
    drawStringInBox (exit_lbl, exit_b, false, sw_col);
    drawStringInBox (bigclock_lbl, bigclock_b, false, sw_col);

    // state
    sws_display = SWD_MAIN;

    // log with server
    logState();

    // init sw digits all illegal so they all get drawn first time
    memset (swdigits, 255, sizeof(swdigits));

    // draw punctuation
    tft.fillCircle (SW_X0 + 2*SW_DW + SW_DGAP + SW_DGAP/2,   SW_Y0 + SW_DH/3,   SW_PUNCR, sw_col);
    tft.fillCircle (SW_X0 + 2*SW_DW + SW_DGAP + SW_DGAP/2,   SW_Y0 + 2*SW_DH/3, SW_PUNCR, sw_col);
    tft.fillCircle (SW_X0 + 4*SW_DW + 3*SW_DGAP + SW_DGAP/2, SW_Y0 + SW_DH/3,   SW_PUNCR, sw_col);
    tft.fillCircle (SW_X0 + 4*SW_DW + 3*SW_DGAP + SW_DGAP/2, SW_Y0 + 2*SW_DH/3, SW_PUNCR, sw_col);
    tft.fillCircle (SW_X0 + 6*SW_DW + 5*SW_DGAP + SW_DGAP/2, SW_Y0 + SW_DH,     SW_PUNCR, sw_col);

    // draw buttons from state and color scale
    drawSWState();
    drawColorScale();
}

/* function just for waitForTap to detect whether external pin or web server command turned alarm off.
 */
static bool checkExternalTurnOff()
{
    return (alarmPinIsSet() || alarm_state != ALMS_RINGING);
}

/* called to indicate the alarm has gone off.
 *   always set the alarm pin.
 *   if showing the main hamclock map, overwrite a pane with message, wait for dismiss, restore then return;
 *   if showing the main stopwatch page show alarm label active and return immediately;
 *   if showing a bigclock page show alarm time highlighted and return immediately.
 */
static void showAlarmRinging()
{
    setAlarmPin(true);

    if (sws_display == SWD_NONE) {

        // show icon
        drawMainPageStopwatch (true);

        // overwrite pane, wait here until dismiss, refresh pane
        const PlotPane alarm_pane = PANE_2;
        SBox &b = plot_b[alarm_pane];

        // close down other network systems if using this pane
        if (findPaneChoiceNow(PLOT_CH_DXCLUSTER) == alarm_pane)
            closeDXCluster();
        if (findPaneChoiceNow(PLOT_CH_GIMBAL) == alarm_pane)
            closeGimbal();

        // prep
        prepPlotBox (b);

        // alarm!
        const char *astr = "Alarm!";
        selectFontStyle (BOLD_FONT, SMALL_FONT);
        tft.setCursor (b.x + (b.w-getTextWidth(astr))/2, b.y + b.h/3);
        tft.setTextColor (RA8875_RED);
        tft.print (astr);

        // show a dismiss button
        SBox dismiss_b;
        dismiss_b.x = b.x + 30;
        dismiss_b.y = b.y + 2*b.h/3;
        dismiss_b.w = b.w - 60;
        dismiss_b.h = 35;
        selectFontStyle (LIGHT_FONT, SMALL_FONT);
        drawStringInBox (" Cancel ", dismiss_b, false, BRGRAY);

        // wait for tap or timeout
        SCoord s;
        (void) waitForTap (dismiss_b, b, checkExternalTurnOff, ALM_RINGTO, s);

        // off
        alarm_state = ALMS_ARMED;
        drawMainPageStopwatch (true);
        logState();

        // restart -- init doesn't include our own countdown pane
        if (findPaneChoiceNow(PLOT_CH_COUNTDOWN) == alarm_pane)
            drawCDTimeRemaining(true);
        else
            initWiFiRetry();

    } else {

        drawAlarmIndicator(true);
    }
}


/* check our touch controls, update state.
 * works for all stopwatch pages: main and either big clock
 */
static void checkSWPageTouch()
{
    // check for touch at all
    SCoord s;
    if (readCalTouchWS(s) == TT_NONE)
        return;

    // update idle timer, ignore if this tap is restoring full brightness
    if (brightnessOn())
        return;

    // check each box depending on which page is up

    if (sws_display == SWD_MAIN) {

        // main stopwatch boxes

        if (inBox (s, countdown_lbl_b)) {

            // start countdown timer regardless of current state
            setSWEngineState (SWE_COUNTDOWN, countdown_period);

        } else if (inBox (s, cdtime_up_b)) {

            // increment countdown period
            countdown_period += 60000;
            countdown_period -= (countdown_period % 60000);             // insure whole minute
            saveSWNV();
            if (sws_engine == SWE_COUNTDOWN)
                setSWEngineState (sws_engine, countdown_period);         // engage new value immediately
            else
                drawSWCDPeriod();                                       // just display new value

        } else if (inBox (s, cdtime_dw_b)) {

            // decrement countdown period
            if (countdown_period >= 2*60000) {                          // 1 minute minimum
                countdown_period -= 60000;
                countdown_period -= (countdown_period % 60000);         // insure whole minute
                saveSWNV();
                if (sws_engine == SWE_COUNTDOWN)
                    setSWEngineState (sws_engine, countdown_period);     // engage new value immediately
                else
                    drawSWCDPeriod();                                   // just display new value
            }

        } else if (inBox (s, alarm_up_b)) {

            // increase alarm hour or minute
            uint16_t a_hr = alarm_hrmn/60;
            uint16_t a_mn = alarm_hrmn%60;
            if (s.x < alarm_up_b.x + alarm_up_b.w/2) {
                a_hr = (a_hr + 1) % 24;
            } else {
                if (++a_mn == 60) {
                    if (++a_hr == 24)
                        a_hr = 0;
                    a_mn = 0;
                }
            }
            alarm_hrmn = a_hr*60 + a_mn;
            saveSWNV();
            drawAlarmIndicator (false);

        } else if (inBox (s, alarm_dw_b)) {

            // decresae alarm hour or minute
            uint16_t a_hr = alarm_hrmn/60;
            uint16_t a_mn = alarm_hrmn%60;
            if (s.x < alarm_up_b.x + alarm_up_b.w/2) {
                a_hr = (a_hr + 23) % 24;
            } else {
                if (a_mn == 0) {
                    a_mn = 59;
                    a_hr = (a_hr + 23) % 24;
                } else {
                    a_mn -= 1;
                }
            }
            alarm_hrmn = a_hr*60 + a_mn;
            saveSWNV();
            drawAlarmIndicator (false);

        } else if (inBox (s, alarm_lbl_b)) {

            // control alarm clock mode
            switch (alarm_state) {
            case ALMS_OFF:
                alarm_state = ALMS_ARMED;
                break;
            case ALMS_ARMED:
                alarm_state = ALMS_OFF;
                break;
            case ALMS_RINGING:
                alarm_state = ALMS_ARMED;
                break;
            }
            drawAlarmIndicator  (true);
            saveSWNV();
        
        } else if (inBox (s, A_b)) {

            // box action depends on current engine state
            SWEngineState new_sws;
            switch (sws_engine) {
            case SWE_RESET:
                // clicked Run
                new_sws = SWE_RUN;
                break;
            case SWE_RUN:
                // clicked Stop
                new_sws = SWE_STOP;
                break;
            case SWE_STOP:
                // clicked Run
                new_sws = SWE_RUN;
                break;
            case SWE_LAP:
                // clicked Reset
                new_sws = SWE_RESET;
                break;
            case SWE_COUNTDOWN:
                // clicked Reset
                new_sws = SWE_RESET;
                break;
            default:
                new_sws = SWE_RESET;
                break;
            }

            // update state and GUI
            setSWEngineState (new_sws, countdown_period);

        } else if (inBox (s, B_b)) {

            // box action depends on current engine state
            SWEngineState new_sws;
            switch (sws_engine) {
            case SWE_RESET:
                // clicked Reset
                new_sws = SWE_RESET;
                break;
            case SWE_RUN:
                // clicked Lap
                new_sws = SWE_LAP;
                break;
            case SWE_STOP:
                // clicked Reset
                new_sws = SWE_RESET;
                break;
            case SWE_LAP:
                // clicked Resume
                new_sws = SWE_RUN;
                break;
            case SWE_COUNTDOWN:
                // clicked Reset
                new_sws = SWE_RESET;
                break;
            default:
                new_sws = SWE_RESET;
                break;
            }

            // update state and GUI
            setSWEngineState (new_sws, countdown_period);

        } else if (inBox (s, exit_b)) {

            // done
            sws_display = SWD_NONE;

        } else if (inBox (s, color_b)) {

            // change color and redraw
            sw_hue = 255*(s.x - color_b.x)/color_b.w;
            NVWriteUInt8 (NV_SWHUE, sw_hue);
            drawSWMainPage();

        } else if (inBox (s, bigclock_b)) {

            // start desired big clock
            Serial.println(F("SW: BigClock enter"));
            sws_display = (bc_bits & SW_BCDIGBIT) ? SWD_BCDIGITAL : SWD_BCANALOG;
            drawBigClock (true);
            logState();
        }

    } else if (sws_display == SWD_BCDIGITAL || sws_display == SWD_BCANALOG) {

        // bigclock boxes

        // toggle analog/digital if tap near center
        SBox center_b;
        center_b.w = 100;
        center_b.h = 100;
        center_b.x = (800-center_b.w)/2;
        center_b.y = (480-center_b.h)/2;

        // toggle digital 12/24 if tap over hours digits
        SBox hr12_b;
        hr12_b.w = 2*BDC_W;
        hr12_b.h = BDC_H;
        hr12_b.x = BDC_X0;
        hr12_b.y = BDC_Y0;
        
        // toggle analog second hand if towards the right side
        SBox sec_hand_b;
        sec_hand_b.w = 100;
        sec_hand_b.h = 100;
        sec_hand_b.x = 600;
        sec_hand_b.y = (480-center_b.h)/2;

        #if defined(_SHOW_ALL)
            tft.drawRect (center_b.x, center_b.y, center_b.w, center_b.h, RA8875_WHITE);
            tft.drawRect (hr12_b.x, hr12_b.y, hr12_b.w, hr12_b.h, RA8875_WHITE);
            tft.drawRect (sec_hand_b.x, sec_hand_b.y, sec_hand_b.w, sec_hand_b.h, RA8875_WHITE);
            tft.drawRect (bcwx_b.x, bcwx_b.y, bcwx_b.w, bcwx_b.h, RA8875_WHITE);
            tft.drawRect (BC_EXIT_X, BC_EXIT_Y, 800-BC_EXIT_X, 480-BC_EXIT_Y, RA8875_WHITE);
            tft.drawRect (BC_BAD_X, BC_BAD_Y, BC_BAD_W, BC_BAD_H, RA8875_RED);
        #endif

        if (inBox (s, bcdate_b)) {
            // toggle showing date
            bc_bits ^= SW_BCDATEBIT;
            drawBigClock (true);
            saveSWNV();
        } else if (inBox (s, bcwx_b)) {
            // toggle showing weather
            bc_bits ^= SW_BCWXBIT;
            drawBigClock (true);
            saveSWNV();
        } else if (sws_display == SWD_BCDIGITAL && inBox (s, hr12_b)) {
            // toggle 12/24 hour
            bc_bits ^= SW_DB12HBIT;
            drawBigClock (true);
            saveSWNV();
        } else if (sws_display == SWD_BCANALOG && inBox (s, sec_hand_b)) {
            // toggle sec hand
            bc_bits ^= SW_ANOSHBIT;
            saveSWNV();
        } else if (sws_engine == SWE_COUNTDOWN && inBox (s, bccd_b)) {
            // reset cd time but stay in SWE_COUNTDOWN state
            start_t = millis();
        } else if (inBox (s, center_b)) {
            // toggle digital/analog
            if (sws_display == SWD_BCDIGITAL) {
                sws_display = SWD_BCANALOG;
                bc_bits &= ~SW_BCDIGBIT;
            } else {
                sws_display = SWD_BCDIGITAL;
                bc_bits |= SW_BCDIGBIT;
            }
            saveSWNV();
            drawBigClock(true);
        } else if (inBox (s, bcalarm_b)) {
            if (alarm_state == ALMS_RINGING) {
                alarm_state = ALMS_ARMED;
                drawAlarmIndicator(false);
                logState();
            }
        } else if (s.x > BC_EXIT_X && s.y > BC_EXIT_Y) {
            // tap anywhere in lower right to return to main stopwatch
            Serial.println(F("SW: BigClock exit"));
            sws_display = SWD_MAIN;
            eraseScreen();
            drawSWMainPage();
        }

    }
}

/* init one-time prep
 */
void initStopwatch()
{
    // read values from NV
    if (!NVReadUInt16 (NV_BCFLAGS, &bc_bits)) {
        bc_bits = SW_BCDATEBIT | SW_BCWXBIT;
        NVWriteUInt16 (NV_BCFLAGS, bc_bits);
    }
    if (!NVReadUInt32 (NV_CD_PERIOD, &countdown_period)) {
        countdown_period = 600000;     // 10 mins default
        NVWriteUInt32 (NV_CD_PERIOD, countdown_period);
    }

    // read and unpack alarm time and whether active
    if (!NVReadUInt16 (NV_ALARMCLOCK, &alarm_hrmn)) {
        alarm_hrmn = 0;
        alarm_state = ALMS_OFF;
        NVWriteUInt16 (NV_ALARMCLOCK, 0);
    }
    if (alarm_hrmn >= ALM_TOVFLOW) {
        alarm_hrmn = alarm_hrmn % ALM_TOVFLOW;
        alarm_state = ALMS_ARMED;
    }

    // insure output pins are off
    setLEDState (SWCDS_OFF);
    setAlarmPin (false);
}

/* draw the main HamClock page stopwatch icon or count down time remaining or alarm is set in stopwatch_b
 *   and/or pane if showing, all depending on sws_engine.
 */
void drawMainPageStopwatch (bool force)
{
    if (sws_engine == SWE_COUNTDOWN) {

        drawCDTimeRemaining(force);

    } else if (force) {

        // draw icon

        // erase
        tft.fillRect (stopwatch_b.x, stopwatch_b.y, stopwatch_b.w, stopwatch_b.h, RA8875_BLACK);
        #if defined(_SHOW_ALL)
            tft.drawRect (stopwatch_b.x, stopwatch_b.y, stopwatch_b.w, stopwatch_b.h, RA8875_WHITE);
        #endif

        // body radius and step for stems
        uint16_t br = 3*stopwatch_b.h/8;
        uint16_t xc = stopwatch_b.x + stopwatch_b.w/2;
        uint16_t yc = stopwatch_b.y + stopwatch_b.h/2;
        uint16_t dx = roundf(br*cosf(deg2rad(45)));

        // body color depends on whether alarm is armed
        uint16_t body_c = alarm_state != ALMS_OFF ? RA8875_GREEN : GRAY;

        // watch
        tft.fillCircle (xc, yc, br, body_c);

        // top stem
        tft.fillRect (xc-1, yc-br-3, 3, 4, body_c);

        // 2 side stems
        tft.fillCircle (xc-dx, yc-dx-1, 1, body_c);
        tft.fillCircle (xc+dx, yc-dx-1, 1, body_c);

        // face
        tft.drawCircle (xc, yc, 3*br/4, RA8875_BLACK);

        // hands
        tft.drawLine (xc, yc, xc, yc-3*br/4, RA8875_WHITE);
        tft.drawLine (xc, yc, xc+3*br/6, yc, RA8875_WHITE);

        // add "vibration" if ringing
        if (alarm_state == ALMS_RINGING) {
            float vr = 1.4F*br;
            uint16_t vdx1 = roundf(vr*cosf(deg2rad(5)));
            uint16_t vdy1 = roundf(vr*sinf(deg2rad(5)));
            uint16_t vdx2 = roundf(vr*cosf(deg2rad(30)));
            uint16_t vdy2 = roundf(vr*sinf(deg2rad(30)));
            tft.drawLine (xc+vdx1, yc-vdy1, xc+vdx2, yc-vdy2, body_c);
            tft.drawLine (xc-vdx1, yc-vdy1, xc-vdx2, yc-vdy2, body_c);
            vr = 1.8F*br;
            vdx1 = roundf(vr*cosf(deg2rad(5)));
            vdy1 = roundf(vr*sinf(deg2rad(5)));
            vdx2 = roundf(vr*cosf(deg2rad(30)));
            vdy2 = roundf(vr*sinf(deg2rad(30)));
            tft.drawLine (xc+vdx1, yc-vdy1, xc+vdx2, yc-vdy2, body_c);
            tft.drawLine (xc-vdx1, yc-vdy1, xc-vdx2, yc-vdy2, body_c);
        }
    }
}


/* stopwatch_b has been touched from HamClock Main page:
 * if tapped while counting down just reset and continue main HamClock page, else start main SW page.
 */
void checkStopwatchTouch(TouchType tt)
{
    // if tapped the stop watch while counting down, just restart
    if (sws_engine == SWE_COUNTDOWN && tt == TT_TAP) {
        setSWEngineState (SWE_COUNTDOWN, countdown_period);
        return;
    }

    Serial.println(F("SW: main enter"));

    // close down other systems
    closeDXCluster();           // prevent inbound msgs from clogging network
    closeGimbal();              // avoid dangling connection
    hideClocks();

    // fresh start
    eraseScreen();
    drawSWMainPage();
}

/* called by main loop to run another iteration of the stop watch.
 * we may or may not be running ("engine" state) and may or may not be visible ("display" state).
 * return whether any stopwatch page is visible now.
 */
bool runStopwatch()
{
    // always honor countdown switch regardless of display state
    if (countdownPinIsTrue())
        setSWEngineState (SWE_COUNTDOWN, countdown_period);

    // always check alarm clock regardless of display state
    if (alarm_state == ALMS_ARMED && checkAlarm()) {
        // record time and indicate alarm has just gone off
        alarm_ringtime = now();
        alarm_state = ALMS_RINGING;
        logState();
        showAlarmRinging();
    }
    if (alarm_state == ALMS_RINGING) {
        if (alarmPinIsSet() || now() - alarm_ringtime >= ALM_RINGTO/1000) {
            // op hit the cancel pin or timed out
            alarm_state = ALMS_ARMED;
            logState();
            if (sws_display == SWD_NONE)
                drawMainPageStopwatch (true);
            else
                drawAlarmIndicator (true);
        }
    }

    if (sws_display != SWD_NONE) {

        // one of the stopwatch pages is up

        // check for our button taps.
        // N.B. this may update sws_display so check again afterwards
        checkSWPageTouch();

        switch (sws_display) {

        case SWD_NONE:
            Serial.println(F("SW: main exit"));
            insureCountdownPaneSensible();
            initScreen();
            return (false);

        case SWD_MAIN:
            // show timer if running but not so often as to overload the graphics
            if (sws_engine == SWE_RUN) {
                static uint32_t main_time_gate;
                if (timesUp (&main_time_gate, 41))      // prime number insures all digits change
                    drawSWTime(millis() - start_t);
            }
            break;

        case SWD_BCDIGITAL:
            drawDigitalBigClock (false);
            break;

        case SWD_BCANALOG:
            drawAnalogBigClock (false);
            break;
        }

        // update countdown if running
        if (sws_engine == SWE_COUNTDOWN)
            drawCDTimeRemaining(false);

        // stopwatch is up
        return (true);

    } else {

        // main hamclock page is up, update count if counting down
        if (sws_engine == SWE_COUNTDOWN)
            drawMainPageStopwatch (false);

        // not up
        return (false);
    }
}

/* change stopwatch engine state and appearance.
 * also set countdown to ms if changing to SWE_COUNTDOWN.
 * return whether requested state is valid now.
 */
bool setSWEngineState (SWEngineState new_sws, uint32_t ms)
{
    switch (new_sws) {
    case SWE_RESET:
        if (sws_engine == SWE_RESET)
            return (true);                      // ignore if no change
        sws_engine = SWE_RESET;
        break;
    case SWE_RUN:
        if (sws_engine == SWE_RUN)
            return (true);                      // ignore if no change
        if (sws_engine == SWE_COUNTDOWN)
            break;                              // just continue running countdown
        if (sws_engine == SWE_STOP)
            start_t = millis() - stop_dt;       // resume after stop: reinstate delta
        else if (sws_engine != SWE_LAP)           // resume after lap: just keep going
            start_t = millis();                 // start fresh
        sws_engine = SWE_RUN;
        break;
    case SWE_STOP:
        if (sws_engine == SWE_STOP)
            return (true);                      // ignore if no change
        if (sws_engine == SWE_COUNTDOWN)
            return (false);                     // stop not implemented for countdown
        stop_dt = millis() - start_t;           // capture delta
        sws_engine = SWE_STOP;
        break;
    case SWE_LAP:
        if (sws_engine == SWE_LAP)
            return (true);                      // ignore if no change
        if (sws_engine == SWE_COUNTDOWN || sws_engine == SWE_STOP)
            return (false);                     // lap not implemented for countdown or stop
        stop_dt = millis() - start_t;           // capture delta
        sws_engine = SWE_LAP;
        break;
    case SWE_COUNTDOWN:
        countdown_period = ms;
        saveSWNV();
        start_t = millis();
        sws_engine = SWE_COUNTDOWN;
        break;
    default:
        return (false);
    }

    // draw new state appearance
    drawSWState();

    // log
    logState();

    return (true);
}

/* retrieve current engine state and associated ms timer value
 */
SWEngineState getSWEngineState (uint32_t &sw_timer)
{
    switch (sws_engine) {
    case SWE_RESET:
        sw_timer = 0;
        break;
    case SWE_RUN:
        sw_timer = millis() - start_t;
        break;
    case SWE_STOP:
        sw_timer = stop_dt;
        break;
    case SWE_LAP:
        sw_timer = stop_dt;
        break;
    case SWE_COUNTDOWN:
        sw_timer = getCountdownLeft();
        break;
    }

    return (sws_engine);
}

/* retrieve current stopwatch display state
 */
SWDisplayState getSWDisplayState()
{
    return (sws_display);
}

/* return alarm state and time 
 */
void getAlarmState (AlarmState &as, uint16_t &hr, uint16_t &mn)
{
    as = alarm_state;
    hr = alarm_hrmn / 60;
    mn = alarm_hrmn % 60;
}

/* set a new alarm state from a web command.
 * N.B. we do no error checking
 */
void setAlarmState (const AlarmState &as, uint16_t hr, uint16_t mn)
{
    if (as == ALMS_OFF) {
        // minimal state downgrade, leave time unchanged
        alarm_state = alarm_state == ALMS_RINGING ? ALMS_ARMED : ALMS_OFF;
    } else {
        // set new state and time
        alarm_state = as;
        alarm_hrmn = hr*60U + mn;
    }
    saveSWNV();

    // update display
    if (sws_display == SWD_NONE)
        drawMainPageStopwatch (true);
    else
        drawAlarmIndicator (true);
}

SWBCBits getBigClockBits(void)
{
    return ((SWBCBits)bc_bits);
}
