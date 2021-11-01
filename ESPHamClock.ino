/* HamClock
 */


// glue
#include "HamClock.h"

// current version
const char *hc_version = HC_VERSION;

// default backend server
const char *svr_host = "clearskyinstitute.com";

// default web server port
int svr_port = SERVERPORT;

// clock and stopwatch box
SBox clock_b = { 0, 65, 230, 80};
SBox stopwatch_b = {149, 93, 38, 22};

// DE and DX map boxes
SBox dx_info_b;                         // dx info location
static SBox askdx_b;                    // dx lat/lng dialog
SCircle dx_marker_c;                    // DX symbol in DX info pane
SBox satname_b;                         // satellite name
SBox de_info_b;                         // de info location
static SBox askde_b;                    // de lat/lng dialog
SBox de_title_b;                        // de title location
SBox map_b;                             // entire map, pixels only, not border
SBox view_btn_b;                        // map View button
SBox view_pick_b;                       // map View pick box
SBox dx_maid_b;                         // dx maindenhead 
SBox de_maid_b;                         // de maindenhead 

// time zone boxes
TZInfo de_tz = {{85, 158, 50, 17}, DE_COLOR, 0};
TZInfo dx_tz = {{85, 307, 50, 17}, DX_COLOR, 0};

// NCDFX box
SBox NCDXF_b = {745, 0, 54, 147};

// brightness control and mode control
SBox brightness_b = {745, 30, 54, 147-30};      // lower portion of NCDXF_b
uint8_t brb_mode;                       // one of BRB_MODE

// common "skip" button
const SBox skip_b    = {730,10,55,35};
bool skip_skip;

// special options to force initing DE using our IP or given IP
bool init_iploc;
const char *init_locip;

// maidenhead label boxes
SBox maidlbltop_b;
SBox maidlblright_b;

// graphical button sizes
#define GB_W    14
#define GB_H    34

// satellite pass horizon
SCircle satpass_c;

// RSS banner box and state
SBox rss_bnr_b;
uint8_t rss_on;

// DRAP scale
SBox drap_b;

// whether to shade night or show place names
uint8_t night_on, names_on;

// Azimuthal-Mercator flag
uint8_t azm_on;

// grid style
uint8_t mapgrid_choice;

// info to display in the call sign GUI location -- the real call sign is always getCallsign()
CallsignInfo cs_info;
static const char on_air_msg[] = "ON THE AIR";  // used by setOnAir()
static SBox version_b;                          // show or check version, just below call
#define CSINFO_DROP     3                       // gap below cs_info

// de and dx sun rise/set boxes, dxsrss_b also used for DX prefix depending on dxsrss
SBox desrss_b, dxsrss_b;

// screen lock or demo mode control -- size mush match HC_RUNMAN_W/H base size
SBox lkscrn_b = {216, 117, 13, 20};

// WiFi touch control
TouchType wifi_tt;
SCoord wifi_tt_s;

// set up TFT display controller RA8875 instance on hardware SPI plus reset and chip select
#define RA8875_RESET    16
#define RA8875_CS       2
Adafruit_RA8875_R tft(RA8875_CS, RA8875_RESET);

// manage the great circle path through DE and DX points
#define MAX_GPATH               1500    // max number of points to draw in great circle path
static SCoord *gpath;                   // malloced path points
static uint16_t n_gpath;                // actual number in use
static uint32_t gpath_time;             // millis() when great path was drawn
#define GPATH_COLOR     RA8875_WHITE    // path color
static SBox prefix_b;                   // where to show DX prefix text

// manage using DX cluster prefix or one from nearestPrefix()
static bool dx_prefix_use_override;     // whether to use dx_override_prefixp[] or nearestPrefix()
static char dx_override_prefix[MAX_PREF_LEN];

// longest interval between calls to resetWatchdog(), ms
uint32_t max_wd_dt;

// whether flash crc is ok
uint8_t flash_crc_ok;

/* free gpath.
 */
void setDXPathInvalid()
{
    if (gpath) {
        free (gpath);
        gpath = NULL;
    }
    n_gpath = 0;
}

// these are needed for a normal C++ compiler
static void drawVersion(bool force);
static void checkTouch(void);
static void drawUptime(bool force);
static void drawWiFiInfo(void);
static void drawScreenLock(void);
static void toggleLockScreen(void);
static void toggleDETimeFormat(void);
static bool checkCallsignTouchFG (SCoord &b);
static bool checkCallsignTouchBG (SCoord &b);
static void eraseDXPath(void);
static void eraseDXMarker(void);
static void drawOneTimeDX(void);
static uint16_t getNextColor(uint16_t current);
static void drawRainbow (SBox &box);
static void drawDXCursorPrefix (void);
static void setDXPrefixOverride (const char *ovprefix);
static void unsetDXPrefixOverride (void);
static void shutdown(void);


/* on unix, be sure we restore screen brightness if dieing from receipt of signal
 */
#if defined(_IS_UNIX)
static void onSig (int signo)
{
    // try to save a message without calling printf
    // see https://wiki.sei.cmu.edu/confluence/display/c/SIG30-C.+Call+only+asynchronous-safe+functions+within+signal+handlers

    static const char msg[] = "Good bye from signal ";
    char sigstr[3];
    sigstr[0] = (signo/10)+'0';
    sigstr[1] = (signo%10)+'0';
    sigstr[2] = '\n';
    write (1, msg, sizeof(msg)-1);
    write (1, sigstr, 3);

    // try to insure screen is back on -- har!
    setFullBrightness();

    exit(0);
}
#endif // _IS_UNIX




// initial stack
char *stack_start;

// called once
void setup()
{
    // init record of stack
    char stack;
    stack_start = &stack;

    // life
    pinMode(LIFE_LED, OUTPUT);
    digitalWrite (LIFE_LED, HIGH);

    // this just reset the soft timeout, the hard timeout is still 6 seconds
    ESP.wdtDisable();

    // start debug trace
    resetWatchdog();
    Serial.begin(115200);
    do {
        wdDelay(500);
    } while (!Serial);
    Serial.print(F("\nHamClock version "));
    Serial.println (hc_version);

    // record whether our FLASH CRC is correct -- takes about half a second
    flash_crc_ok = ESP.checkFlashCRC();
#ifdef _IS_ESP8266
    Serial.printf(_FX("FLASH crc ok: %d\n"), flash_crc_ok);
#endif

    // random seed, not critical
    randomSeed(micros());

    // Initialise the display
    if (!tft.begin(RA8875_800x480)) {
        Serial.println(_FX("RA8875 Not Found!"));
        while (1);
    }

    // must set rotation now from nvram setting .. too early to query setup options
    // N.B. don't save in EEPROM yet.
    uint8_t rot;
    if (!NVReadUInt8 (NV_ROTATE_SCRN, &rot))
        rot = 0;
    tft.setRotation(rot ? 2 : 0);

    // Adafruit assumed ESP8266 would run at 80 MHz, but we run it at 160
    extern uint32_t spi_speed;
    spi_speed *= 2;

    // turn display full on
    tft.displayOn(true);
    tft.GPIOX(true); 
    tft.PWM1config(true, RA8875_PWM_CLK_DIV1024); // PWM output for backlight
    initBrightness();

#if defined(_IS_UNIX)
    // connect signal handler
    signal (SIGTERM, onSig);
    signal (SIGILL, onSig);
    signal (SIGKILL, onSig);
    signal (SIGSEGV, onSig);
    signal (SIGBUS, onSig);
#endif // _IS_UNIX


    // now can save rot so if commit fails screen is up for fatalError()
    NVWriteUInt8 (NV_ROTATE_SCRN, rot);


// #define _GFX_COORD_TEST
#if defined(_GFX_COORD_TEST)
    // just used to confirm our posix porting layer graphics agree with Adafruit
    tft.fillScreen(RA8875_BLACK);
    tft.fillRect (100, 100, 6, 6, RA8875_RED);
    tft.drawRect (100, 100, 8, 8, RA8875_RED);
    tft.drawPixel (100,108,RA8875_RED);
    tft.drawPixel (102,108,RA8875_RED);
    tft.drawPixel (104,108,RA8875_RED);
    tft.drawPixel (106,108,RA8875_RED);
    tft.drawPixel (108,108,RA8875_RED);
    tft.drawCircle (100, 200, 1, RA8875_RED);
    tft.drawCircle (100, 200, 5, RA8875_RED);
    tft.fillCircle (110, 200, 3, RA8875_RED);
    tft.drawPixel (100,207,RA8875_RED);
    tft.drawPixel (100,208,RA8875_RED);
    tft.drawPixel (102,207,RA8875_RED);
    tft.drawPixel (104,207,RA8875_RED);
    tft.drawPixel (106,207,RA8875_RED);
    tft.drawPixel (108,207,RA8875_RED);
    tft.drawPixel (110,207,RA8875_RED);
    tft.drawPixel (110,208,RA8875_RED);
    tft.drawPixel (112,207,RA8875_RED);
    tft.drawPixel (114,207,RA8875_RED);
    tft.drawPixel (114,200,RA8875_RED);
    while(1)
        wdDelay(100);
#endif // _GFX_COORD_TEST

// #define _GFX_TEXTSZ
#if defined(_GFX_TEXTSZ)
    // just used to compare posix text port with Adafruit GRX 
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    char str[] = "scattered clouds";
    int sum = 0;
    for (char *s = str; *s; s++) {
        char s1 = s[1];
        s[1] = '\0';
        int l = getTextWidth(s);
        Serial.printf ("%c %d\n", *s, l);
        s[1] = s1;
        sum += l;
    }
    Serial.printf ("Net %d\n", getTextWidth(str));
    Serial.printf ("Sum %d\n", sum);
    while(1)
        wdDelay(100);
#endif // _GFX_TEXTSZ

    // enable touch screen system
    tft.touchEnable(true);

    // get info from user at full brighness, then commence with user's desired brightness
    clockSetup();
    setupBrightness();

    // do not display time until all set up
    hideClocks();

    // prep stopwatch
    initStopwatch();

    // draw initial callsign
    eraseScreen();
    free (cs_info.call);
    cs_info.call = strdup(getCallsign());
    cs_info.box.x = (tft.width()-512)/2;
    cs_info.box.y = 10;                 // coordinate with tftMsg()
    cs_info.box.w = 512;
    cs_info.box.h = 50;
    if (!NVReadUInt16 (NV_CALL_FG_COLOR, &cs_info.fg_color)) {
        cs_info.fg_color = RA8875_BLACK;
        NVWriteUInt16 (NV_CALL_FG_COLOR, cs_info.fg_color);
    }
    if (!NVReadUInt16 (NV_CALL_BG_COLOR, &cs_info.bg_color)) {
        cs_info.bg_color = RA8875_WHITE;
        NVWriteUInt16 (NV_CALL_BG_COLOR, cs_info.bg_color);
    }
    if (!NVReadUInt8 (NV_CALL_BG_RAINBOW, &cs_info.bg_rainbow)) {
        cs_info.bg_rainbow = 1;
        NVWriteUInt8 (NV_CALL_BG_RAINBOW, cs_info.bg_rainbow);
    }
    drawCallsign (true);

    // draw version just below
    tft.setTextColor (RA8875_WHITE);
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setCursor (cs_info.box.x+(cs_info.box.w-getTextWidth(hc_version))/2, cs_info.box.y+cs_info.box.h+10);
    tft.print (hc_version);

    // position the map box in lower right -- border is drawn outside
    map_b.w = EARTH_W;
    map_b.h = EARTH_H;
    map_b.x = tft.width() - map_b.w - 1;        // 1 in from edge for border
    map_b.y = tft.height() - map_b.h - 1;       // 1 in from edge for border

    // position View button in upper left
    view_btn_b.x = map_b.x;
    view_btn_b.y = map_b.y;                     // gets adjusted if showing grid label
    view_btn_b.w = VIEWBTN_W;
    view_btn_b.h = VIEWBTN_H;

    // pick box is wider and taller to avoid accidentally setting a close DX
    view_pick_b.x = view_btn_b.x;
    view_pick_b.y = view_btn_b.y;               // gets adjusted if showing grid label
    view_pick_b.w = 2*view_btn_b.w;
    view_pick_b.h = 3*view_btn_b.h;

    // redefine callsign for main screen
    cs_info.box.x = 0;
    cs_info.box.y = 0;
    cs_info.box.w = 230;
    cs_info.box.h = 52;

    // version box
    version_b.w = 46;
    version_b.x = cs_info.box.x+cs_info.box.w-version_b.w;
    version_b.y = cs_info.box.y+cs_info.box.h+CSINFO_DROP;
    version_b.h = 8;

    // start WiFi, maps and set de_ll.lat_d/de_ll.lng_d from geolocation or gpsd as desired -- uses tftMsg()
    initSys();

    // get from nvram even if set prior from setup, geolocate or gpsd
    NVReadInt32(NV_DE_TZ, &de_tz.tz_secs);
    NVReadFloat(NV_DE_LAT, &de_ll.lat_d);
    NVReadFloat(NV_DE_LNG, &de_ll.lng_d);
    normalizeLL(de_ll);

    // ask to update if new version available -- never returns if update succeeds
    if (!skip_skip) {
        char nv[50];
        if (newVersionIsAvailable (nv, sizeof(nv)) && askOTAupdate (nv))
            doOTAupdate(nv);
    }

    // init sensors
    initBME280();

    // read plot settings from NVnsure sane defaults 
    initPlotPanes();

    // init rest of de info
    de_info_b.x = 1;
    de_info_b.y = 185;                  // below DE: line
    de_info_b.w = map_b.x - 2;          // just inside border
    de_info_b.h = 110;
    uint16_t devspace = de_info_b.h/DE_INFO_ROWS;
    askde_b.x = de_info_b.x + 1;
    askde_b.y = de_info_b.y;
    askde_b.w = de_info_b.w - 2;
    askde_b.h = 3*devspace;
    desrss_b.x = de_info_b.x + de_info_b.w/2;
    desrss_b.y = de_info_b.y + 2*devspace;
    desrss_b.w = de_info_b.w/2;
    desrss_b.h = devspace;
    de_maid_b.x = de_info_b.x;
    de_maid_b.y = de_info_b.y+(DE_INFO_ROWS-1)*de_info_b.h/DE_INFO_ROWS;
    de_maid_b.w = de_info_b.w/2;
    de_maid_b.h = devspace;
    if (!NVReadUInt8(NV_DE_SRSS, &desrss)) {
        desrss = false;
        NVWriteUInt8(NV_DE_SRSS, desrss);
    }
    if (!NVReadUInt8(NV_DE_TIMEFMT, &de_time_fmt)) {
        de_time_fmt = DETIME_INFO;
        NVWriteUInt8(NV_DE_TIMEFMT, de_time_fmt);
    }
    sdelat = sinf(de_ll.lat);
    cdelat = cosf(de_ll.lat);
    antipode (deap_ll, de_ll);
    ll2s (de_ll, de_c.s, DE_R);
    ll2s (deap_ll, deap_c.s, DEAP_R);
    setSatObserver (de_ll.lat_d, de_ll.lng_d);
    de_title_b.x = de_info_b.x;
    de_title_b.y = de_tz.box.y-5;
    de_title_b.w = 30;
    de_title_b.h = 30;

    // init dx info
    if (!NVReadUInt8 (NV_DIST_KM, &show_km)) {
        if (!NVReadUInt8 (NV_METRIC_ON, &show_km))
            show_km = false;
        NVWriteUInt8 (NV_DIST_KM, show_km);
    }
    if (!NVReadUInt8 (NV_LP, &show_lp)) {
        show_lp = false;
        NVWriteUInt8 (NV_LP, show_lp);
    }
    dx_info_b.x = 1;
    dx_info_b.y = 295;
    dx_info_b.w = de_info_b.w;
    dx_info_b.h = 181;
    uint16_t dxvspace = dx_info_b.h/DX_INFO_ROWS;
    askdx_b.x = dx_info_b.x+1;
    askdx_b.y = dx_info_b.y + dxvspace;
    askdx_b.w = dx_info_b.w-2;
    askdx_b.h = 3*dxvspace;
    dx_marker_c.s.x = dx_info_b.x+62;
    dx_marker_c.s.y = dx_tz.box.y+8;
    dx_marker_c.r = dx_c.r;
    dxsrss_b.x = dx_info_b.x + dx_info_b.w/2;
    dxsrss_b.y = dx_info_b.y + 3*dxvspace;
    dxsrss_b.w = dx_info_b.w/2;
    dxsrss_b.h = dxvspace;
    dx_maid_b.x = dx_info_b.x;
    dx_maid_b.y = dx_info_b.y+(DX_INFO_ROWS-2)*dx_info_b.h/DX_INFO_ROWS;
    dx_maid_b.w = dx_info_b.w/2;
    dx_maid_b.h = dxvspace;
    if (!NVReadUInt8(NV_DX_SRSS, &dxsrss)) {
        dxsrss = DXSRSS_INAGO;
        NVWriteUInt8(NV_DX_SRSS, dxsrss);
    }
    if (!NVReadFloat(NV_DX_LAT,&dx_ll.lat_d) || !NVReadFloat(NV_DX_LNG,&dx_ll.lng_d)) {
        // if never set, default to 0/0
        dx_ll.lat_d = 0;
        dx_ll.lng_d = 0;
        NVWriteFloat (NV_DX_LAT, dx_ll.lat_d);
        NVWriteFloat (NV_DX_LNG, dx_ll.lng_d);
        setNVMaidenhead(NV_DX_GRID, dx_ll);
        dx_tz.tz_secs = getTZ (dx_ll);
        NVWriteInt32(NV_DX_TZ, dx_tz.tz_secs);
    }
    dx_ll.lat = deg2rad(dx_ll.lat_d);
    dx_ll.lng = deg2rad(dx_ll.lng_d);
    ll2s (dx_ll, dx_c.s, DX_R);
    if (!NVReadInt32(NV_DX_TZ, &dx_tz.tz_secs)) {
        dx_tz.tz_secs = getTZ (dx_ll);
        NVWriteInt32(NV_DX_TZ, dx_tz.tz_secs);
    }

    // sat pass circle
    satpass_c.r = dx_info_b.h/3 - 1;
    satpass_c.s.x = dx_info_b.x + dx_info_b.w/2;
    satpass_c.s.y = dx_info_b.y + dx_info_b.h - satpass_c.r - 1;


    // init portion of dx info used for satellite name
    satname_b.x = dx_info_b.x;
    satname_b.y = dx_info_b.y+1U;
    satname_b.w = dx_info_b.w;
    satname_b.h = dx_info_b.h/6;        // match FONT_H in earthsat.cpp

    // set up RSS state and banner box
    rss_bnr_b.x = map_b.x;
    rss_bnr_b.y = map_b.y + map_b.h - 2*GB_H;
    rss_bnr_b.w = map_b.w;
    rss_bnr_b.h = 2*GB_H;
    NVReadUInt8 (NV_RSS_ON, &rss_on);

    // set up az-merc state
    NVReadUInt8 (NV_AZIMUTHAL_ON, &azm_on);

    // get grid style state
    NVReadUInt8 (NV_LLGRID, &mapgrid_choice);

    // position the maiden label boxes
    maidlbltop_b.x = map_b.x;
    maidlbltop_b.y = map_b.y;
    maidlbltop_b.w = map_b.w;
    maidlbltop_b.h = MH_TR_H;
    maidlblright_b.x = map_b.x + map_b.w - MH_RC_W;
    maidlblright_b.y = map_b.y;
    maidlblright_b.w = MH_RC_W;
    maidlblright_b.h = map_b.h;

    // position the DRAP scale
    // N.B. drap_b.y is set dynamically in drawDRAPScale() depending on rss_on
    drap_b.x = map_b.x;
    drap_b.w = map_b.w;
    drap_b.h = map_b.h/20;
    drap_b.y = rss_on ? rss_bnr_b.y - drap_b.h: map_b.y + map_b.h - drap_b.h;

    // check for saved satellite
    dx_info_for_sat = initSatSelection();

    // perform inital screen layout
    initScreen();

    // now start checking repetative wd
    max_wd_dt = 0;
}

// called repeatedly forever
void loop()
{
    // update stopwatch exclusively, if active
    if (runStopwatch()) {
        // these can run even while stopwatch is up
        followBrightness();
        readBME280();
        return;
    }

    // check on wifi and plots
    updateWiFi();

    // update clocks
    updateClocks(false);

    // update sat pass (this is just the pass; the path is recomputed before each map sweep)
    updateSatPass();

    // update NCFDX beacons, don't erase if holding path
    updateBeacons(!waiting4DXPath(), false, false);

    // display more of earth map
    drawMoreEarth();

    // other goodies
    drawUptime(false);
    drawWiFiInfo();
    drawVersion(false);
    followBrightness();
    checkOnAir();
    readBME280();
    runNextDemoCommand();

    // check for touch events
    checkTouch();
}


/* assuming basic hw init is complete setup everything for the screen.
 * called once at startup and after each time returning from other full-screen options.
 * The overall layout is establihed by setting the various SBox values.
 * Some are initialized statically in setup() some are then set relative to these.
 */
void initScreen()
{
    resetWatchdog();

    // erase entire screen
    eraseScreen();

    // set protected region
    tft.setPR (map_b.x, map_b.y, map_b.w, map_b.h);

    // us
    drawVersion(true);
    drawCallsign(true);

    // DE info
    selectFontStyle (BOLD_FONT, SMALL_FONT);
    tft.setTextColor(DE_COLOR);
    tft.setCursor(de_info_b.x, de_tz.box.y+18);
    tft.print(F("DE:"));
    de_c.s.x = de_info_b.x+62;
    de_c.s.y = de_tz.box.y+8;
    drawDEMarker(true);
    drawDEInfo();

    // DX info
    drawOneTimeDX();
    drawDXInfo();

    resetWatchdog();

    // draw section borders
    tft.drawLine (0, map_b.y-1, tft.width()-1, map_b.y-1, GRAY);                        // top
    tft.drawLine (0, tft.height()-1, tft.width()-1, tft.height()-1, GRAY);              // bottom
    tft.drawLine (0, map_b.y-1, 0, tft.height()-1, GRAY);                               // left
    tft.drawLine (tft.width()-1, map_b.y-1, tft.width()-1, tft.height()-1, GRAY);       // right
    tft.drawLine (map_b.x-1, map_b.y-1, map_b.x-1, tft.height()-1, GRAY);               // left of map
    tft.drawLine (0, dx_info_b.y, map_b.x-1, dx_info_b.y, GRAY);                        // de/dx divider

    // init each pane
    for (int i = 0; i < PANE_N; i++)
        setPlotChoice ((PlotPane)i, plot_ch[i]);

    // set up beacon box
    resetWatchdog();
    drawBeaconBox();

    // draw all the clocks
    showClocks();
    updateClocks(true);
    drawMainPageStopwatch(true);

    // start 
    initEarthMap();
    initWiFiRetry();
    drawBME280Panes();
    displaySatInfo();
    drawUptime(true);
    drawScreenLock();

    // flush any stale touchs
    drainTouch();
}

/* draw the one-time portion of dx_info either because we just booted or because
 * we are transitioning back from being in sat mode
 */
static void drawOneTimeDX()
{
    if (dx_info_for_sat)
        return;

    tft.fillRect (dx_info_b.x, dx_info_b.y+1, dx_info_b.w, dx_info_b.h-1, RA8875_BLACK);

    selectFontStyle (BOLD_FONT, SMALL_FONT);
    tft.setTextColor(DX_COLOR);
    tft.setCursor(dx_info_b.x, dx_tz.box.y+18);
    tft.print(F("DX:"));

    // save/restore dx_c so it can be used
    SCircle dx_c_save = dx_c;
    dx_c = dx_marker_c;
    drawDXMarker(true);
    dx_c = dx_c_save;
}

/* round to whole degrees.
 * used to remove spurious fractions
 */
static void roundLL (LatLong &ll)
{
    ll.lat_d = roundf (ll.lat_d);
    ll.lng_d = roundf (ll.lng_d);
    normalizeLL(ll);
}


/* monitor for touch events
 */
static void checkTouch()
{
    resetWatchdog();

    TouchType tt;
    SCoord s;

    // check for remote and local touch, else get out fast
    if (wifi_tt != TT_NONE) {
        s = wifi_tt_s;
        tt = wifi_tt;
        wifi_tt = TT_NONE;
        // remote touches never turn on brightness
    } else {
        tt = readCalTouch (s);
        if (tt == TT_NONE)
            return;
        // don't do anything else if this tap just turned on brightness
        if (brightnessOn()) {
            drainTouch();
            return;
        }
    }


    // check for lock screen first
    if (inBox (s, lkscrn_b)) {
        if (getDemoMode()) {
            // turn off, draw padlock
            setDemoMode (false);
            drawScreenLock();
        } else {
            // check for shutdown or toggle lock
            if (tt == TT_HOLD) {
                if (!screenIsLocked())
                    shutdown();
            } else {
                toggleLockScreen();
                drawScreenLock();
            }
        }
        return;
    }
    if (screenIsLocked())
        return;

    drainTouch();

    // check all touch locations, ones that can be over map checked first
    LatLong ll;
    if (inBox (s, view_pick_b)) {
        // set flag to draw map menu at next opportunity
        mapmenu_pending = true;
    } else if (checkSatMapTouch (s)) {
        dx_info_for_sat = true;
        displaySatInfo();
        eraseDXPath();                  // declutter to emphasize sat track
        drawAllSymbols(false);
    } else if (s2ll (s, ll)) {
        // subpixel values are meaningless
        roundLL (ll);
        if (tt == TT_HOLD) {
            // map hold: update DE
            // assume op wants city, if showing
            if (names_on)
                (void) getNearestCity (ll, ll);
            newDE (ll, NULL);
        } else {
            // just an ordinary map location: update DX
            // assume op wants city, if showing
            if (names_on)
                (void) getNearestCity (ll, ll);
            newDX (ll, NULL, NULL);
        }
    } else if (inBox (s, de_title_b)) {
        toggleDETimeFormat();
    } else if (inBox (s, stopwatch_b)) {
        // check this before checkClockTouch
        checkStopwatchTouch(tt);
    } else if (checkClockTouch(s, tt)) {
        updateClocks(true);
    } else if (checkTZTouch (s, de_tz, de_ll)) {
        NVWriteInt32 (NV_DE_TZ, de_tz.tz_secs);
        drawTZ (de_tz);
        drawDEInfo();
        updateMoonPane(true);
        newBC();
    } else if (!dx_info_for_sat && checkTZTouch (s, dx_tz, dx_ll)) {
        // N.B. don't even call checkTZTouch if !dx_info_for_sat
        NVWriteInt32 (NV_DX_TZ, dx_tz.tz_secs);
        drawTZ (dx_tz);
        drawDXInfo();
    } else if (checkCallsignTouchFG(s)) {
        NVWriteUInt16 (NV_CALL_FG_COLOR, cs_info.fg_color);
        drawCallsign (false);   // just foreground
    } else if (checkCallsignTouchBG(s)) {
        NVWriteUInt16 (NV_CALL_BG_COLOR, cs_info.bg_color);
        NVWriteUInt8 (NV_CALL_BG_RAINBOW, cs_info.bg_rainbow);
        drawCallsign (true);    // fg and bg
    } else if (!dx_info_for_sat && checkDistTouch(s)) {
        show_km = !show_km;
        NVWriteUInt8 (NV_DIST_KM, show_km);
        drawDXInfo ();
    } else if (!dx_info_for_sat && checkPathDirTouch(s)) {
        show_lp = !show_lp;
        NVWriteUInt8 (NV_LP, show_lp);
        drawDXInfo ();
        newBC();
        newVOACAPMap(prop_map);
    } else if (inBox (s, desrss_b)) {
        // N.B. this must come before askde_b because it overlaps
        if (de_time_fmt == DETIME_INFO) {
            desrss = !desrss;
            NVWriteUInt8 (NV_DE_SRSS, desrss);
            drawDEInfo();
        }
    } else if (!dx_info_for_sat && inBox (s, dxsrss_b)) {
        // N.B. this must come before askdx_b because it overlaps
        dxsrss = (dxsrss+1)%DXSRSS_N;
        NVWriteUInt8 (NV_DX_SRSS, dxsrss);
        drawDXInfo();
    } else if (de_time_fmt == DETIME_INFO && inBox (s, askde_b)) {
        char maid[MAID_CHARLEN];
        getNVMaidenhead (NV_DE_GRID, maid);
        if (askNewPos (askde_b, ll = de_ll, maid))
            newDE (ll, maid);
        else
            drawDEInfo();
    } else if (!dx_info_for_sat && inBox (s, askdx_b)) {
        char maid[MAID_CHARLEN];
        getNVMaidenhead (NV_DX_GRID, maid);
        if (askNewPos (askdx_b, ll = dx_ll, maid))
            newDX (ll, maid, NULL);
        else
            drawDXInfo();
    } else if (!dx_info_for_sat && inCircle(s, dx_marker_c)) {
        newDX (dx_ll, NULL, NULL);
    } else if (checkPlotTouch(s, PANE_1, tt)) {
        updateWiFi();
    } else if (checkPlotTouch(s, PANE_2, tt)) {
        updateWiFi();
    } else if (checkPlotTouch(s, PANE_3, tt)) {
        updateWiFi();
    } else if (inBox (s, brightness_b)) {
        changeBrightness(s);
    } else if (checkBeaconTouch(s)) {
        drawBeaconBox();
        updateBeacons(true, true, true);
    } else if (checkSatNameTouch(s)) {
        closeDXCluster();       // prevent inbound msgs from clogging network
        closeGimbal();          // avoid dangling connection
        hideClocks();
        dx_info_for_sat = querySatSelection();
        initScreen();
    } else if (dx_info_for_sat && inCircle (s, satpass_c)) {
        showNextSatEvents ();
    } else if (inBox (s, version_b)) {
        char nv[50];
        if (newVersionIsAvailable(nv, sizeof(nv))) {
            if (askOTAupdate (nv))
                doOTAupdate(nv);
        } else {
            eraseScreen();
            tft.setTextColor (RA8875_WHITE);
            tft.setCursor (tft.width()/8, tft.height()/3);
            selectFontStyle (BOLD_FONT, SMALL_FONT);
            tft.print (F("You're up to date!"));        // match webserver response
            wdDelay(3000);
        }
        initScreen();
    }
}

/* given the degree members:
 *   clamp lat to [-90,90];
 *   modulo lng to [-180,180).
 * then fill in the radian members.
 */
void normalizeLL (LatLong &ll)
{
    ll.lat_d = fminf(fmaxf(ll.lat_d,-90),90);           // clamp
    ll.lat = deg2rad(ll.lat_d);

    ll.lng_d = fmodf(ll.lng_d+(2*360+180),360)-180;     // wrap
    ll.lng = deg2rad(ll.lng_d);
}

/* set new DX location from ll in dx_info.
 * use the given grid, else look up from ll.
 * also set override prefix unless NULL
 */
void newDX (LatLong &ll, const char *grid, const char *ovprefix)
{
    resetWatchdog();

    // disable the sat info 
    if (dx_info_for_sat) {
        dx_info_for_sat = false;
        drawOneTimeDX();
    }

    // set grid and TZ
    normalizeLL (ll);
    if (grid)
        NVWriteString (NV_DX_GRID, grid);
    else
        setNVMaidenhead (NV_DX_GRID, ll);
    dx_tz.tz_secs = getTZ (ll);
    NVWriteInt32(NV_DX_TZ, dx_tz.tz_secs);

    // erase previous DX info
    eraseDXPath ();
    eraseDXMarker ();

    // set new location
    dx_ll = ll;
    ll2s (dx_ll, dx_c.s, DX_R);

    // set DX prefix
    if (ovprefix)
        setDXPrefixOverride (ovprefix);
    else
        unsetDXPrefixOverride();

    // draw in new location and update info
    drawDXPath ();
    gpath_time = millis();
    drawDXInfo ();
    drawAllSymbols(false);

    // don't draw if prefix from cluster because it draws also
    if (!dx_prefix_use_override)
        drawDXCursorPrefix();

    // show DX weather and update band conditions if showing
    showDXWX();
    newBC();

    // persist
    NVWriteFloat (NV_DX_LAT, dx_ll.lat_d);
    NVWriteFloat (NV_DX_LNG, dx_ll.lng_d);
}

/* set new DE location, and optional grid
 */
void newDE (LatLong &ll, const char *grid)
{
    resetWatchdog();

    // set grid and TZ
    normalizeLL (ll);
    if (grid)
        NVWriteString (NV_DE_GRID, grid);
    else
        setNVMaidenhead (NV_DE_GRID, ll);
    de_tz.tz_secs = getTZ (ll);
    NVWriteInt32(NV_DE_TZ, de_tz.tz_secs);

    // sat path will change, stop gimbal and require op to start
    stopGimbalNow();

    if (azm_on) {

        // must start over because everything moves to keep new DE centered

        drawDEMarker(false);
        de_ll = ll;

        // prep for all new map
        initEarthMap();

    } else {

        // for mercator we try harder to just update the minimum .. lazy way is just call initScreen

        // erase current markers
        eraseDXPath();
        eraseDEMarker();
        eraseDEAPMarker();
        eraseDXMarker();

        // set new
        de_ll = ll;
        sdelat = sinf(de_ll.lat);
        cdelat = cosf(de_ll.lat);
        ll2s (de_ll, de_c.s, DE_R);
        antipode (deap_ll, de_ll);
        ll2s (deap_ll, deap_c.s, DEAP_R);

        // draw in new location and update info
        drawDEInfo();
        drawDXInfo();   // heading changes
        drawAllSymbols(false);
        
        // update lunar info
        getLunarCir (nowWO(), de_ll, lunar_cir);
    }

    // persist
    NVWriteFloat (NV_DE_LAT, de_ll.lat_d);
    NVWriteFloat (NV_DE_LNG, de_ll.lng_d);

    // more updates that depend on DE regardless of projection
    showDEWX();                         // schedules moon update after linger if assigned to plot1
    if (findPaneChoiceNow(PLOT_CH_MOON) != PANE_NONE)
        updateMoonPane(true);           // only if not scheduled by showDEWX
    newBC();
    newVOACAPMap(prop_map);
    sendDXClusterDELLGrid();
    setSatObserver (de_ll.lat_d, de_ll.lng_d);
    displaySatInfo();
}
 
/* given a touch location check if Op wants to change callsign fg.
 * if so then update cs_info and return true else false.
 */
static bool checkCallsignTouchFG (SCoord &b)
{
    SBox left_half = cs_info.box;
    left_half.w /=2;

    if (inBox (b, left_half)) {
        // change fg
        do {
            cs_info.fg_color = getNextColor (cs_info.fg_color);
        } while (!cs_info.bg_rainbow && cs_info.fg_color == cs_info.bg_color);
        return (true);
    }
    return (false);
}


/* given a touch location check if Op wants to change callsign bg.
 * if so then update cs_info and return true else false.
 */
static bool checkCallsignTouchBG (SCoord &b)
{
    SBox right_half = cs_info.box;
    right_half.w /=2;
    right_half.x += right_half.w;

    if (inBox (b, right_half)) {
        // change bg cycling through rainbow when current color is white
        if (cs_info.bg_rainbow) {
            cs_info.bg_rainbow = false;
            do {
                cs_info.bg_color = getNextColor (cs_info.bg_color);
            } while (cs_info.bg_color == cs_info.fg_color);
        } else {
            // TODO: can never turn on Rainbow if FG is white because BG will never be set to white
            if (cs_info.bg_color == RA8875_WHITE) {
                cs_info.bg_rainbow = true;
            } else {
                do {
                    cs_info.bg_color = getNextColor (cs_info.bg_color);
                } while (cs_info.bg_color == cs_info.fg_color);
            }
        }
        return (true);
    }
    return (false);
}

/* return next color in basic set of primary colors.
 */
static uint16_t getNextColor(uint16_t current)
{
    static uint16_t colors[] = {
        RA8875_RED, RA8875_GREEN, RA8875_BLUE, RA8875_CYAN,
        RA8875_MAGENTA, RA8875_YELLOW, RA8875_WHITE, RA8875_BLACK
    };
    #define NCOLORS NARRAY(colors)

    for (uint8_t i = 0; i < NCOLORS; i++)
        if (colors[i] == current)
            return (colors[(i+1)%NCOLORS]);
    return (colors[0]);         // default if current is unknown
}

/* erase the DX marker by restoring map
 */
static void eraseDXMarker()
{
    eraseSCircle (dx_c);

    // restore sat name in case hit
    for (int16_t dy = -dx_c.r; dy <= dx_c.r; dy++)
        drawSatNameOnRow (dx_c.s.y+dy);
}

/* erase great circle through DE and DX by restoring map at each entry in gpath[] then forget.
 * we also erase the box used to display the prefix
 */
static void eraseDXPath()
{
    // get out fast if nothing to do
    if (!n_gpath)
        return;

    // erase the prefix box
    for (uint16_t dy = 0; dy < prefix_b.h; dy++)
        for (uint16_t dx = 0; dx < prefix_b.w; dx++)
            drawMapCoord (prefix_b.x + dx, prefix_b.y + dy);

    // erase the great path
    for (uint16_t i = 0; i < n_gpath; i++) {
        drawMapCoord (gpath[i]);                        // draws x and x+1, y
        drawMapCoord (gpath[i].x, gpath[i].y+1);        //        "       , y+1
    }

    // mark no longer active
    setDXPathInvalid();
}

/* find long- else short-path angular distance and east-of-north bearing from DE to ll, both in radians
 * in range 0..2pi.
 */
void propDEDXPath (bool long_path, LatLong &ll, float *distp, float *bearp)
{
    // cdist will be cos of short-path anglar separation in radians, so acos is 0..pi
    // *bearp will be short-path from DE to ll east-to-north in radians, -pi..pi
    float cdist;
    solveSphere (ll.lng-de_ll.lng, M_PI_2F-ll.lat, sdelat, cdelat, &cdist, bearp);

    if (long_path) {
        *distp = 2*M_PIF - acosf(cdist);              // long path can be anywhere 0..2pi
        *bearp = fmodf (*bearp + 3*M_PIF, 2*M_PIF);   // +180 then clamp to 0..2pi
    } else {
        *distp = acosf(cdist);                        // short part always 0..pi
        *bearp = fmodf (*bearp + 2*M_PIF, 2*M_PIF);   // shift -pi..pi to 0..2pi
    }
}

/* draw great circle through DE and DX.
 * save screen coords in gpath[]
 */
void drawDXPath ()
{
    resetWatchdog();

    // find short-path bearing and distance from DE to DX
    float dist, bear;
    propDEDXPath (false, dx_ll, &dist, &bear);

    // start with max nnumber of points, then reduce
    gpath = (SCoord *) realloc (gpath, MAX_GPATH * sizeof(SCoord));
    if (!gpath) {
        Serial.println (F("Failed to malloc gpath"));
        reboot();
    }

    // walk great circle path from DE through DX, storing each point
    float ca, B;
    SCoord s;
    n_gpath = 0;
    for (float b = 0; b < 2*M_PIF; b += 2*M_PIF/MAX_GPATH) {
        solveSphere (bear, b, sdelat, cdelat, &ca, &B);
        ll2s (asinf(ca), fmodf(de_ll.lng+B+5*M_PIF,2*M_PIF)-M_PIF, s, 1);
        if (overMap(s) && (n_gpath == 0 || memcmp (&s, &gpath[n_gpath-1], sizeof(SCoord)))) {
            uint16_t c = b < dist ? getShortPathColor() : getLongPathColor();
            gpath[n_gpath++] = s;

            // beware drawing the fat pixel off the edge of earth because eraseDXPath won't erase it
            LatLong ll;
            tft.drawPixel (s.x, s.y, c);
            if (s2ll(s.x+1, s.y, ll))
                tft.drawPixel (s.x, s.y, c);
            if (s2ll(s.x+1, s.y+1, ll))
                tft.drawPixel (s.x, s.y, c);
            if (s2ll(s.x, s.y+1, ll))
                tft.drawPixel (s.x, s.y, c);
        }
    }

    // reduce to actual number of points used
    // Serial.printf (_FX("n_gpath %u -> %u\n"), MAX_GPATH, n_gpath);
    gpath = (SCoord *) realloc (gpath, n_gpath * sizeof(SCoord));
    if (!gpath) {
        Serial.println (F("Failed to realloc gpath"));
        reboot();
    }

    // printFreeHeap (F("drawDXPath"));
}

/* return whether we are waiting for a DX path to linger.
 * also erase path if its linger time just expired.
 */
bool waiting4DXPath()
{
    if (n_gpath == 0)
        return (false);

    if (timesUp (&gpath_time, DXPATH_LINGER)) {
        eraseDXPath();          // sets no longer valid
        drawAllSymbols(false);
        return (false);
    }

    return (true);
}

void drawDXMarker (bool force)
{
    // test for over visible map unless force, eg might be under RSS now
    if (!force && !overMap(dx_c.s))
        return;

    tft.fillCircle (dx_c.s.x, dx_c.s.y, DX_R, DX_COLOR);
    tft.drawCircle (dx_c.s.x, dx_c.s.y, DX_R, RA8875_BLACK);
    tft.fillCircle (dx_c.s.x, dx_c.s.y, 2, RA8875_BLACK);
}

/* return the bounding box of the given string in the current font.
 */
void getTextBounds (const char str[], uint16_t *wp, uint16_t *hp)
{
    int16_t x, y;
    tft.getTextBounds ((char*)str, 0, 0, &x, &y, wp, hp);
}


/* return width in pixels of the given string in the current font
 */
uint16_t getTextWidth (const char str[])
{
    uint16_t w, h;
    getTextBounds (str, &w, &h);
    return (w);
}


/* set ON AIR else normal call sign
 */
void setOnAir (bool on)
{
    free (cs_info.call);
    cs_info.call = strdup (on ? on_air_msg : getCallsign());
    drawCallsign (true);
}

/* change call sign to ON AIR as long as GPIO21 is low
 */
bool checkOnAir()
{

#if defined(_SUPPORT_GPIO) && defined(_IS_UNIX)

    // ignore if not supposed to use GPIO
    if (!GPIOOk())
        return (false);

    // only draw when changes
    static bool prev_on;

    GPIO& gpio = GPIO::getGPIO();
    gpio.setAsInput (ONAIR_GPIO);
    bool on = gpio.isReady() && !gpio.readPin(ONAIR_GPIO);

    if (on && !prev_on)
        setOnAir(true);
    else if (!on && prev_on)
        setOnAir(false);

    prev_on = on;

    return (on);

#else

    return (false);

#endif // _SUPPORT_GPIO && _IS_UNIX
}


/* draw callsign using cs_info.
 * draw everything if all, else just fg text.
 */
void drawCallsign (bool all)
{
    tft.graphicsMode();

    if (all) {
        if (cs_info.bg_rainbow)
            drawRainbow (cs_info.box);
        else
            tft.fillRect (cs_info.box.x, cs_info.box.y, cs_info.box.w, cs_info.box.h, cs_info.bg_color);
    }

    // make copy in order to replace each 0 with del which is modified to be a slashed-0
    StackMalloc call_cpy(cs_info.call);
    char *call = call_cpy.getMem();
    for (char *z = call; *z != '\0' ; z++) {
        if (*z == '0')
            *z = 127;   // del
    }

    // keep shrinking font until fits
    uint16_t cw, ch;
    selectFontStyle (BOLD_FONT, LARGE_FONT);
    getTextBounds (call, &cw, &ch);

    if (cw >= cs_info.box.w) {
        selectFontStyle (BOLD_FONT, SMALL_FONT);
        getTextBounds (call, &cw, &ch);
        if (cw >= cs_info.box.w) {
            selectFontStyle (LIGHT_FONT, SMALL_FONT);
            getTextBounds (call, &cw, &ch);
        }
    }

    tft.setTextColor (cs_info.fg_color);
    int cx = cs_info.box.x + (cs_info.box.w-cw)/2;
    int cy = cs_info.box.y + ch + (cs_info.box.h-ch)/2 - 3;
    tft.setCursor (cx, cy);
    tft.print(call);
}

/* draw full spectrum in the given box.
 */
static void drawRainbow (SBox &box)
{
    uint8_t h, s = 255, v = 255;
    uint8_t r, g, b;
    uint8_t x0 = random(box.w);

    tft.graphicsMode();
    for (uint16_t x = box.x; x < box.x+box.w; x++) {
        h = 255*((x+x0-box.x)%box.w)/box.w;
        hsvtorgb (&r, &g, &b, h, s, v);
        uint16_t c = RGB565(r,g,b);
        tft.fillRect (x, box.y, 1, box.h, c);
    }
}

/* draw version just below cs_info if time to recheck or force
 */
static void drawVersion(bool force)
{
    // how often to check for new version
    #define VER_CHECK_DT    (6*3600*1000UL)         // millis

    // get out fast if nothing to do yet
    static uint32_t check_t;
    if (!timesUp (&check_t, VER_CHECK_DT) && !force)
        return;

    // check for new version and reset timeout
    char line[50];
    bool new_avail = newVersionIsAvailable (line, sizeof(line));
    check_t = millis();

    // show current version, but highlight if new version is available
    uint16_t col = new_avail ? RA8875_RED : GRAY;
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    snprintf (line, sizeof(line), _FX("Ver %s"), hc_version);
    uint16_t vw = getTextWidth (line);
    tft.setTextColor (col);
    tft.setCursor (version_b.x+version_b.w-vw, version_b.y);        // right justify
    tft.print (line);
}

/* draw wifi signal strength or IP occasionally below cs_info
 */
static void drawWiFiInfo()
{
    resetWatchdog();

    // show in red if wifi signal strength less than this -- not a hard cutoff but -70 is definitely dead.
    #define MIN_WIFI_RSSI (-60)

    // just once every few seconds, wary about overhead calling RSSI()
    static uint32_t prev_ms;
    if (!timesUp(&prev_ms, 5000))
        return;

    // toggle rssi and ip, or just ip if no wifi
    static bool rssi_ip_toggle;
    char str[30];
    if (rssi_ip_toggle) {
        // show RSSI, if working
        int16_t rssi = WiFi.RSSI();
        if (rssi < 10) {
            sprintf (str, _FX("  WiFi %4d dBm"), rssi);
            tft.setTextColor (rssi < MIN_WIFI_RSSI ? RA8875_RED : GRAY);
        } else {
            // no wifi, just leave IP showing
            str[0] = '\0';
        }
    } else {
        // show IP
        IPAddress ip = WiFi.localIP();
        bool net_ok = ip[0] != '\0';
        if (net_ok) {
            sprintf (str, _FX("IP %d.%d.%d.%d"), ip[0], ip[1], ip[2], ip[3]);
            tft.setTextColor (GRAY);
        } else {
            strcpy (str, "    No Network");
            tft.setTextColor (RA8875_RED);
        }
    }

    if (str[0]) {
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        int16_t x = cs_info.box.x + 68;         // a little past Uptime
        int16_t y = cs_info.box.y+cs_info.box.h+CSINFO_DROP;
        uint16_t w = cs_info.box.w - version_b.w - 68 - 2;
        tft.fillRect (x, y-1, w, 11, RA8875_BLACK);
        tft.setCursor (x, y);
        tft.print(str);
    }

    // toggle
    rssi_ip_toggle = !rssi_ip_toggle;

}

static void prepUptime()
{
    resetWatchdog();

    const uint16_t x = cs_info.box.x;
    const uint16_t y = cs_info.box.y+cs_info.box.h+CSINFO_DROP;
    tft.fillRect (x+11, y-1, 50, 11, RA8875_BLACK);
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (GRAY);
    tft.setCursor (x, y);
    tft.print (F("Up "));
}

/* return uptime in seconds, 0 if not ready yet.
 * already break into components if not NULL
 */
time_t getUptime (uint16_t *days, uint8_t *hrs, uint8_t *mins, uint8_t *secs)
{
    // "up" is elapsed time since first good value
    static time_t start_s;

    // get time now from NTP
    time_t now_s = now();
    if (now_s < 1490000000L)            // March 2017
        return (0);                     // not ready yet

    // get secs since starts_s unless first call or time ran backwards?!
    if (start_s == 0 || now_s < start_s)
        start_s = now_s;
    time_t up0 = now_s - start_s;

    // break out if interested
    if (days && hrs && mins && secs) {
        time_t up = up0;
        *days = up/SECSPERDAY;
        up -= *days*SECSPERDAY;
        *hrs = up/3600;
        up -= *hrs*3600;
        *mins = up/60;
        up -= *mins*60;
        *secs = up;
    }

    // return up secs
    return (up0);

}

/* draw time since boot just below cs_info.
 * keep drawing to a minimum and get out fast if no change unless force.
 */
static void drawUptime(bool force)
{
    // only do the real work once per second
    static uint32_t prev_ms;
    if (!timesUp(&prev_ms, 1000))
        return;

    // only redraw if significant chars change
    static uint8_t prev_m = 99, prev_h = 99;

    // get uptime, bail if not ready yet.
    uint16_t days; uint8_t hrs, mins, secs;
    time_t upsecs = getUptime (&days, &hrs, &mins, &secs);
    if (!upsecs)
        return;

    resetWatchdog();

    // draw two most significant units if change
    if (upsecs < 60) {
        prepUptime();
        tft.print(upsecs); tft.print(F("s "));
    } else if (upsecs < 3600) {
        prepUptime();
        tft.print(mins); tft.print(F("m "));
        tft.print(secs); tft.print(F("s "));
    } else if (upsecs < SECSPERDAY) {
        if (mins != prev_m || force) {
            prepUptime();
            tft.print(hrs); tft.print(F("h "));
            tft.print(mins); tft.print(F("m "));
            prev_m = mins;
        }
    } else {
        if (hrs != prev_h || force) {
            prepUptime();
            tft.print(days); tft.print(F("d "));
            tft.print(hrs); tft.print(F("h "));
            prev_h = hrs;
        }
    }
}


/* return whether coordinate s is over the maidenhead key around the edges
 */
static bool overMaidKey (const SCoord &s)
{
    return (!azm_on && mapgrid_choice == MAPGRID_MAID       
                        && (inBox(s,maidlbltop_b) || inBox(s,maidlblright_b)) );
}

/* return whether coordinate s is over an active DRAP scale
 */
static bool overDRAPScale (const SCoord &s)
{
    return (DRAPScaleIsUp() && inBox(s,drap_b));
}

/* return whether coordinate s is over a usable map location
 */
bool overMap (const SCoord &s)
{
    return (inBox(s, map_b) && !overRSS(s) && !inBox(s,view_btn_b) && !overMaidKey(s) && !overDRAPScale(s));
}

/* return whether coordinate s is over any symbol
 */
bool overAnySymbol (const SCoord &s)
{
    return (inCircle(s, de_c) || inCircle(s, dx_c) || inCircle(s, deap_c)
                || inCircle (s, sun_c) || inCircle (s, moon_c) || overAnyBeacon(s)
                || overAnyDXClusterSpots(s) || inBox(s,santa_b)
                || overDRAPScale(s));
}

/* return whether the DRAP scale is (or should be) visible now
 */
bool DRAPScaleIsUp(void)
{
    return (core_map == CM_DRAP && prop_map == PROP_MAP_OFF);
}


/* draw all symbols, order establishes layering priority
 * N.B. called by updateBeacons(): beware recursion
 */
void drawAllSymbols(bool erase_too)
{
    updateClocks(false);
    resetWatchdog();

    if (DRAPScaleIsUp())
        drawDRAPScale();
    if (!overRSS(sun_c.s))
        drawSun();
    if (!overRSS(moon_c.s))
        drawMoon();
    updateBeacons(erase_too, true, false);
    drawDEMarker(false);
    drawDXMarker(false);
    if (!overRSS(deap_c.s))
        drawDEAPMarker();
    drawDXClusterSpotsOnMap();
    drawSanta ();

    updateClocks(false);
}

/* return whether coordinate s is over an active RSS region.
 */
bool overRSS (const SCoord &s)
{
        return (rss_on && inBox (s, rss_bnr_b));
}

/* return whether box b is over an active RSS banner
 */
bool overRSS (const SBox &b)
{
    if (!rss_on)
        return (false);

    if (b.x >= rss_bnr_b.x+rss_bnr_b.w)
        return (false);
    if (b.y >= rss_bnr_b.y+rss_bnr_b.h)
        return (false);
    if (b.x + b.w <= rss_bnr_b.x)
        return (false);
    if (b.y + b.h <= rss_bnr_b.y)
        return (false);

    return (true);
}

/* write another line to the initial screen if verbose, else just log.
 * advance screen row unless this and previous line ended with \r, furthermore
 *    NULL fmt forces return to normal row advancing for next call.
 * we now implement 2 columns.
 */
void tftMsg (bool verbose, uint32_t dwell_ms, const char *fmt, ...)
{
    // setup
    #define MSG_NROWS   11              // actually 1- because we preincrement row
    #define MSG_ROWH    35
    #define MSG_COL1_X  10
    #define MSG_COL2_X  (tft.width()/2)
    #define MSG_ROW1_Y  100             // coordinate with _initial_ cs_info
    static uint8_t row, col;            // counters, not pixels
    static bool prev_hold;              // whether previous message stayed on row
    StackMalloc msg_buf(300);
    char *buf = msg_buf.getMem();

    // NULL fmt signals return to normal row advancing
    if (!fmt) {
        prev_hold = false;
        return;
    }

    // format msg
    va_list ap;
    va_start(ap, fmt);
    int l = vsnprintf (buf, msg_buf.getSize(), fmt, ap);
    va_end(ap);

    // note whether \r
    bool hold = buf[l-1] == '\r';
    // Serial.printf ("tftMsg hold= %d %s\n", hold, buf);

    // rm any \n and \r
    if (buf[l-1] == '\n' || buf[l-1] == '\r')
        buf[--l] = '\0';

    // log
    Serial.println(buf);

    // done unless verbose
    if (!verbose)
        return;


    // advance unless this and prev hold
    if (!(hold && prev_hold)) {
        if (++row == MSG_NROWS) {
            row = 1;
            col++;
        }
    }

    // set location
    uint16_t x = col ? MSG_COL2_X : MSG_COL1_X;
    uint16_t y = MSG_ROW1_Y + row*MSG_ROWH;

    // erase if this is another non-advance
    if (hold && prev_hold)
        tft.fillRect (x, y-(MSG_ROWH-9), tft.width()/2, MSG_ROWH, RA8875_BLACK);

    // draw
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor(RA8875_WHITE);
    tft.setCursor (x, y);
    tft.print(buf);

    if (dwell_ms)
        wdDelay (dwell_ms);

    // record
    prev_hold = hold;
}

/* toggle the NV_LKSCRN_ON value
 */
static void toggleLockScreen()
{
    uint8_t lock_on = !screenIsLocked();
    Serial.printf (_FX("Screen lock is now %s\n"), lock_on ? "On" : "Off");
    NVWriteUInt8 (NV_LKSCRN_ON, lock_on);
}

/* draw the lock screen symbol according to demo and/or NV_LKSCRN_ON.
 */
static void drawScreenLock()
{
    tft.fillRect (lkscrn_b.x, lkscrn_b.y, lkscrn_b.w, lkscrn_b.h, RA8875_BLACK);

    if (getDemoMode()) {

        // runner icon
        const uint16_t rx = tft.SCALESZ*lkscrn_b.x;
        const uint16_t ry = tft.SCALESZ*lkscrn_b.y;
        for (uint16_t dy = 0; dy < HC_RUNNER_H; dy++)
            for (uint16_t dx = 0; dx < HC_RUNNER_W; dx++)
                tft.drawSubPixel (rx+dx, ry+dy, pgm_read_word(&runner[dy*HC_RUNNER_W + dx]));

    } else {

        uint16_t hh = lkscrn_b.h/2;
        uint16_t hw = lkscrn_b.w/2;

        tft.fillRect (lkscrn_b.x, lkscrn_b.y+hh, lkscrn_b.w, hh, RA8875_WHITE);
        tft.drawLine (lkscrn_b.x+hw, lkscrn_b.y+hh+2, lkscrn_b.x+hw, lkscrn_b.y+hh+hh/2, RA8875_BLACK);
        tft.drawCircle (lkscrn_b.x+hw, lkscrn_b.y+hh, hw, RA8875_WHITE);

        if (!screenIsLocked())
            tft.fillRect (lkscrn_b.x+hw, lkscrn_b.y, hw+1, hh, RA8875_BLACK);
    }
}

static void toggleDETimeFormat()
{
    // cycle and persist
    de_time_fmt = (de_time_fmt + 1) % DETIME_N;
    NVWriteUInt8(NV_DE_TIMEFMT, de_time_fmt);

    // draw new state
    drawDEInfo();
}

/* resume using nearestPrefix
 */
static void unsetDXPrefixOverride ()
{
    dx_prefix_use_override = false;
}

/* set an override prefix for getDXPrefix() to use instead of using nearestPrefix()
 */
static void setDXPrefixOverride (const char *ovprefix)
{
    // extract
    call2Prefix (ovprefix, dx_override_prefix);

    // flag ready
    dx_prefix_use_override = true;
}

/* extract the prefix from the given call sign
 */
void call2Prefix (const char *call, char prefix[MAX_PREF_LEN])
{
    // init
    memset (prefix, 0, MAX_PREF_LEN);

    // copy call into prefix; if contains / usually use the shorter side
    const char *slash = strchr (call, '/');
    if (slash) {
        const char *right = slash+1;
        size_t llen = slash - call;
        size_t rlen = strlen (right);
        const char *slash2 = strchr (right, '/');
        if (slash2)
            rlen = slash2 - right;              // don't count past 2nd slash

        if (rlen <= 1 || llen <= rlen || !strcmp(right,"MM") || !strcmp(right,"AM")
                        || !strncmp (right, "QRP", 3) || strspn(right,"0123456789") == rlen)
            memcpy (prefix, call, llen >= MAX_PREF_LEN ? MAX_PREF_LEN-1 : llen); 
        else
            memcpy (prefix, right, rlen >= MAX_PREF_LEN ? MAX_PREF_LEN-1 : rlen); 
    } else
        memcpy (prefix, call, MAX_PREF_LEN-1);

    // truncate after right-most digit
    for (int i = MAX_PREF_LEN-1; --i >= 0; ) {
        if (isdigit(prefix[i])) {
            prefix[i+1] = '\0';
            break;
        }
    }

}

/* return the override prefix else nearest one based on ll, if any
 */
bool getDXPrefix (char p[MAX_PREF_LEN+1])
{
    if (dx_prefix_use_override) {
        memcpy (p, dx_override_prefix, MAX_PREF_LEN);
        p[MAX_PREF_LEN] = '\0';
        return (true);
    } else {
        return (nearestPrefix (dx_ll, p));
    }
}

/* display the DX prefix at dx_c 
 */
static void drawDXCursorPrefix()
{
    char p[MAX_PREF_LEN+1];
    if (getDXPrefix(p)) {
        setMapTagBox (p, dx_c.s, dx_c.r+5, prefix_b);
        drawMapTag (p, prefix_b);
    }
}

/* set box to position a string with nominal center at s over the map taking care not to obscure
 * the given region nor go outside the map. r is encircling radius, if any.
 */
void setMapTagBox (const char *tag, const SCoord &s, uint16_t r, SBox &box)
{
    // get text size
    uint16_t cw, ch;
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    getTextBounds (tag, &cw, &ch);

    // define initial position
    box.x = s.x - cw/2-2;
    box.y = s.y - ch/2 + r;
    box.w = cw + 4;
    box.h = ch + 1;

    // insure entirely over map
    SCoord rss_test;
    rss_test.x = map_b.x + map_b.w/2U;
    rss_test.y = box.y + box.h;
    if (overRSS(rss_test) || box.y + box.h >= map_b.y + map_b.h) {
        // move label on top of symbol if near bottom 
        box.y = s.y - r - box.h - 2;
    } else if (azm_on) {
        // check bottom corners against hemisphere edge if in lower half, top half should be ok
        uint16_t center_y = map_b.y + map_b.h/2;
        if (s.y > center_y) {
            uint32_t r2 = (uint32_t)map_b.h/2 * (uint32_t)map_b.w/4;
            uint32_t dy = box.y + box.h - center_y;
            uint16_t max_dx = sqrtf(r2 - dy*dy);
            uint16_t center_x = map_b.x + ((s.x < map_b.x + map_b.w/2) ? map_b.w/4 : 3*map_b.w/4);
            if (box.x < center_x - max_dx) {
                box.x = s.x + r + 2;
                box.y = s.y - box.h/2;
            } else if (box.x + box.w >= center_x + max_dx) {
                box.x = s.x - r - box.w - 2;
                box.y = s.y - box.h/2;
            }
        }
    } else {
        // check left and right edges
        if (box.x < map_b.x)
            box.x = map_b.x + 2;
        else if (box.x + box.w >= map_b.x + map_b.w)
            box.x = map_b.x + map_b.w - box.w - 2;
    }
}

/* draw a small string within the given box set by setMapTagBox
 */
void drawMapTag (const char *tag, SBox &box)
{
    // draw
    tft.fillRect (box.x, box.y, box.w, box.h, RA8875_BLACK);
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setCursor (box.x+2, box.y);
    tft.setTextColor (RA8875_WHITE);
    tft.print((char*)tag);
}

/* return whether screen is currently locked
 */
bool screenIsLocked()
{
    uint8_t lock_on;
    if (!NVReadUInt8 (NV_LKSCRN_ON, &lock_on)) {
        lock_on = 0;
        NVWriteUInt8 (NV_LKSCRN_ON, lock_on);
    }
    return (lock_on != 0);
}

/* return whether SCoord is within SBox
 */
bool inBox (const SCoord &s, const SBox &b)
{
    return (s.x >= b.x && s.x < b.x+b.w && s.y >= b.y && s.y < b.y+b.h);
}

/* return whether SCoord is within SCircle.
 * N.B. must match Adafruit_RA8875::fillCircle()
 */
bool inCircle (const SCoord &s, const SCircle &c)
{
    // beware unsigned subtraction
    uint16_t dx = (s.x >= c.s.x) ? s.x - c.s.x : c.s.x - s.x;
    uint16_t dy = (s.y >= c.s.y) ? s.y - c.s.y : c.s.y - s.y;
    return (4*dx*dx + 4*dy*dy <= 4*c.r*(c.r+1)+1);
}

/* erase the given SCircle
 */
void eraseSCircle (const SCircle &c)
{
    // scan a circle of radius r+1/2 to include whole pixel.
    // radius (r+1/2)^2 = r^2 + r + 1/4 so we use 2x everywhere to avoid floats
    uint16_t radius2 = 4*c.r*(c.r + 1) + 1;
    for (int16_t dy = -2*c.r; dy <= 2*c.r; dy += 2) {
        for (int16_t dx = -2*c.r; dx <= 2*c.r; dx += 2) {
            int16_t xy2 = dx*dx + dy*dy;
            if (xy2 <= radius2)
                drawMapCoord (c.s.x+dx/2, c.s.y+dy/2);
        }
    }
}

/* erase entire screen
 */
void eraseScreen()
{
    tft.setPR (0, 0, 0, 0);
    tft.fillScreen(RA8875_BLACK);
    tft.drawPR();
}

void resetWatchdog()
{
    // record longest wd feed interval so far in max_wd_dt
    static uint32_t prev_ms;
    uint32_t ms = millis();
    uint32_t dt = ms - prev_ms;
    if (dt < 100)
        return;         // ignore crazy fast
    if ((dt > max_wd_dt || dt > 5000) && prev_ms > 0) {
        // Serial.printf (_FX("max WD %u\n"), dt);
        max_wd_dt = dt;
    }
    prev_ms = ms;

    ESP.wdtFeed();
    yield();
}

/* like delay() but breaks into small chunks so we can call resetWatchdog()
 */
void wdDelay(int ms)
{
    #define WD_DELAY_DT   50
    uint32_t t0 = millis();
    int dt;
    while ((dt = millis() - t0) < ms) {
        resetWatchdog();
        if (dt < WD_DELAY_DT)
            delay (dt);
        else
            delay (WD_DELAY_DT);
    }
}

/* handy utility to return whether now is dt later than prev.
 * if so, update *prev and return true, else return false.
 */
bool timesUp (uint32_t *prev, uint32_t dt)
{
    uint32_t ms = millis();
    if (ms < *prev || ms - *prev < dt)          // allow for ms < prev
        return (false);
    *prev = ms;
    return (true);
}

/* log our state, if ok with user
 */
void logState()
{
    if (logUsageOk()) {
        // just use anything to cause a web transaction
        char ver[50];
        (void) newVersionIsAvailable (ver, sizeof(ver));
    }
}


/* shutdown helper that asks RU Sure in the given box.
 * return answer.
 */
static bool shutdown_rus (const SBox &b, uint16_t color)
{
    // cursor y
    uint16_t cur_y = b.y + 7*b.h/10;

    // erase and print query
    tft.fillRect (b.x+1, b.y+1, b.w-2, b.h-2, RA8875_BLACK);    // erase but avoid boundary
    tft.setCursor (b.x+50, cur_y);
    tft.setTextColor(color);
    tft.print ("Are you sure? ");

    // define yes/no boxes
    SBox y_b;
    y_b.x = tft.getCursorX();
    y_b.y = b.y;
    y_b.w = 50;
    y_b.h = b.h;
    tft.setCursor (y_b.x+8, cur_y);
    tft.print ("Yes");

    SBox n_b = y_b;
    n_b.x += y_b.w+30;
    tft.setCursor (n_b.x+10, cur_y);
    tft.print ("No");

    // wait for one to be tapped
    for (;;) {
        SCoord s;
        TouchType tt = readCalTouchWS (s);
        if (tt != TT_NONE) {
            if (inBox (s, y_b))
                return (true);
            if (inBox (s, n_b))
                return (false);
        }
    }
}

/* offer power down, restart etc depending on platform.
 */
static void shutdown(void)
{
    eraseScreen();

    // prep
    selectFontStyle (BOLD_FONT, SMALL_FONT);
    const uint16_t x0 = tft.width()/4;
    const uint16_t w = tft.width()/2;
    const uint16_t h = 50;
    uint16_t y = tft.height()/4;

    // define each possibility -- set depends on platform
    typedef struct {
        const char *prompt;
        SBox box;
        uint16_t color;
    } ShutDownCtrl;
    enum {
        _SDC_RESUME,
        _SDC_RESTART,
        _SDC_EXIT,
        _SDC_REBOOT,
        _SDC_SHUTDOWN
    };
    ShutDownCtrl sdc[] = {              // N.B. must be in order of _SDC_* enum
        {"Never mind -- resume",        {x0, y,                 w, h}, RA8875_GREEN},
        {"Restart HamClock",            {x0, (uint16_t)(y+h),   w, h}, RA8875_YELLOW},
        #if defined(_IS_UNIX)
            {"Exit HamClock",           {x0, (uint16_t)(y+2*h), w, h}, RA8875_MAGENTA},
            {"Reboot",                  {x0, (uint16_t)(y+3*h), w, h}, RA8875_RED},
            {"Shutdown",                {x0, (uint16_t)(y+4*h), w, h}, RGB565(255,125,0)},
        #endif // _IS_UNIX
    };

    // number of options
    #if defined(_IS_UNIX)
        // os control only when full screen
        unsigned n_sdc = getX11FullScreen() ? NARRAY(sdc) : NARRAY(sdc)-2;
    #else
        // only restart makes sense
        unsigned n_sdc = NARRAY(sdc);
    #endif

    // main loop that displays choices until one is confirmed
    int selection = -1;
    do {

        // display all
        for (unsigned i = 0; i < n_sdc; i++) {
            ShutDownCtrl *sdp = &sdc[i];
            drawStringInBox (sdp->prompt, sdp->box, false, sdp->color);
        }

        // wait for a selection
        for (selection = -1; selection < 0; ) {
            SCoord s;
            TouchType tt = readCalTouchWS (s);
            if (tt != TT_NONE) {
                for (unsigned i = 0; i < n_sdc; i++) {
                    if (inBox (s, sdc[i].box)) {
                        selection = i;
                        break;
                    }
                }
            }
        }

    } while (selection > 0 && !shutdown_rus (sdc[selection].box, sdc[selection].color));

    // engage selection action
    switch (selection) {
    case _SDC_RESUME:
        initScreen();
        return;
    case _SDC_RESTART:
        Serial.print (_FX("Restarting\n"));
        eraseScreen();  // fast touch feedback
        reboot();
        break;
 #if defined(_IS_UNIX)
    case _SDC_EXIT:
        Serial.print (_FX("Exiting\n"));
        setFullBrightness();
        eraseScreen();
        wdDelay(200);
        exit(0);
    case _SDC_REBOOT:
        drawStringInBox ("Rebooting...", sdc[3].box, true, RA8875_RED);
        tft.drawPR();            // forces immediate effect
        Serial.print (_FX("Rebooting\n"));
        system ("sudo reboot");
        for(;;);
    case _SDC_SHUTDOWN:
        drawStringInBox ("Shutting down...", sdc[4].box, true, RA8875_RED);
        tft.drawPR();            // forces immediate effect
        Serial.print (_FX("Shutting down\n"));
        system ("sudo halt");
        for(;;);
 #endif // _IS_UNIX
    default:
        fatalError ("Shutdown choice bug: %d", selection);
        return;

    }
}


/* reboot
 */
void reboot()
{
    setFullBrightness();
    ESP.restart();
    for(;;);
}

/* call to display one final message, never returns
 */
void fatalError (const char *fmt, ...)
{
    #define FE_LINEH    35              // line height
    // format message
    StackMalloc msg_buf(2000);
    char *msg = msg_buf.getMem();
    va_list ap;
    va_start(ap, fmt);
    vsnprintf (msg, msg_buf.getSize(), fmt, ap);
    va_end(ap);

    // log for sure
    Serial.printf ("Fatal: %s\n", msg);

    // it may still be very early so wait a short while for display
    for (int i = 0; i < 20; i++) {
        if (tft.displayReady())
            break;
        wdDelay(100);
    }

    // nothing more unless display
    if (tft.displayReady()) {

        // fresh screen
        eraseScreen();

        // prep display
        uint16_t y = 0;
        selectFontStyle (LIGHT_FONT, SMALL_FONT);
        tft.setTextColor (RA8875_WHITE);

        // display msg, scroll if contains newlines, beware long lines
        char *mp = msg;
        do {
            char *nl = strchr (mp, '\n');
            if (nl)
                *nl = '\0';
            size_t line_l = strlen(mp);
            size_t chars_printed = 0;
            while (chars_printed < line_l) {
                char *line_cpy = strdup (mp+chars_printed);             // copy because ...
                (void) maxStringW (line_cpy, tft.width()-10);           // ... may shorten in-place
                tft.setCursor (0, y += FE_LINEH);
                tft.print(line_cpy);
                chars_printed += strlen(line_cpy);
                free (line_cpy);
            }
            mp += line_l;
            if (nl)
                mp++;
        } while (strlen(mp) > 0 && y < 480-FE_LINEH);

        // draw box and wait for click
        SBox x_b = {350, 400, 100, 50};
    #if defined(_IS_UNIX)
        const char x_msg[] = "Exit";
    #else
        const char x_msg[] = "Restart";
    #endif
        drawStringInBox (x_msg, x_b, false, RA8875_WHITE);
        drainTouch();
        SCoord s;
        while (readCalTouchWS (s) == TT_NONE || !inBox (s, x_b))
            wdDelay(20);

        drawStringInBox (x_msg, x_b, true, RA8875_WHITE);
        wdDelay(100);
    }

    // outta here
    #if defined(_IS_UNIX)
        exit(1);
    #else
        reboot();
    #endif
}


/* return the worst offending heap and stack
 */
static int worst_heap = 900000000;
static int worst_stack;
void getWorstMem (int *heap, int *stack)
{
    *heap = worst_heap;
    *stack = worst_stack;
}

#if defined (_IS_ESP8266)

/* call with FLASH string, return pointer to RAM heap string.
 * used to save a lot of RAM for calls that only accept RAM strings, eg, *printf format 
 * N.B. we accommodate nesting these pointers only a few deep then they are reused.
 */
const char *_FX_helper(const __FlashStringHelper *flash_string)
{
    #define N_PTRS 3
    static char *ram_string[N_PTRS];
    static uint8_t nxt_i;

    // get next available pointer
    char **sp = &ram_string[nxt_i];
    nxt_i = (nxt_i + 1) % N_PTRS;

    // convert and copy 
    String _s(flash_string);
    strcpy (*sp = (char *) realloc (*sp, strlen(_s.c_str())+1), _s.c_str());
    return (*sp);
}

#endif


/* log current heap and stack usage, record worst offenders
 */
void printFreeHeap (const __FlashStringHelper *label)
{
    // compute sizes
    char stack_here;
    int free_heap = ESP.getFreeHeap();
    int stack_used = stack_start - &stack_here;

    // log..
    // getFreeHeap() is close to binary search of max malloc
    Serial.printf (_FX("Up %lu s: "), millis()/1000U); // N.B. do not use getUptime here, it loops NTP
    Serial.print (label);
    Serial.printf (_FX("(), free heap %d, stack size %d\n"), free_heap, stack_used);

    // record worst
    if (free_heap < worst_heap)
        worst_heap = free_heap;
    if (stack_used > worst_stack)
        worst_stack = stack_used;
}
