/* manage most wifi uses.
 */

#include "HamClock.h"



// RSS info
#define RSS_INTERVAL    15                      // polling period, secs
static const char rss_page[] = "/ham/HamClock/RSS/web15rss.pl";
#define NRSS            15                      // max number RSS entries to cache

// kp historical and predicted info, new data posted every 3 hours
#define KP_INTERVAL     3500                    // polling period, secs
#define KP_COLOR        RA8875_YELLOW           // loading message text color
static const char kp_page[] = "/ham/HamClock/geomag/kindex.txt";

// xray info, new data posted every 10 minutes
#define XRAY_INTERVAL   600                     // polling interval, secs
#define XRAY_LCOLOR     RGB565(255,50,50)       // long wavelength plot color, reddish
#define XRAY_SCOLOR     RGB565(50,50,255)       // short wavelength plot color, blueish
static const char xray_page[] = "/ham/HamClock/xray/xray.txt";

// sunspot info, new data posted daily
#define SSPOT_INTERVAL  3400                    // polling interval, secs
#define SSPOT_COLOR     RA8875_CYAN             // loading message text color
static const char sspot_page[] = "/ham/HamClock/ssn/ssn-31.txt";

// solar flux info, new data posted three times a day
#define FLUX_INTERVAL   3300                    // polling interval, secs
#define FLUX_COLOR      RA8875_GREEN            // loading message text color
static const char sf_page[] = "/ham/HamClock/solar-flux/solarflux-99.txt";

// solar wind info, new data posted every few minutes
#define SWIND_INTERVAL  600                     // polling interval, secs
#define SWIND_COLOR     RA8875_MAGENTA          // loading message text color
static const char swind_page[] = "/ham/HamClock/solar-wind/swind-24hr.txt";

// STEREO A image and info, new data posted every few hours
#define STEREO_A_INTERVAL  3800                    // polling interval, secs
#define STEREO_A_COLOR     RA8875_BLUE             // loading message text color
static const char stereo_a_sep_page[] = "/ham/HamClock/STEREO/sepangle.txt";
static const char stereo_a_img_page[] = 
    #if defined(_CLOCK_1600x960) 
        "/ham/HamClock/STEREO/STEREO-A-195-320.bmp";
    #elif defined(_CLOCK_2400x1440)
        "/ham/HamClock/STEREO/STEREO-A-195-480.bmp";
    #elif defined(_CLOCK_3200x1920)
        "/ham/HamClock/STEREO/STEREO-A-195-640.bmp";
    #else
        "/ham/HamClock/STEREO/STEREO-A-195-160.bmp";
    #endif

// band conditions and map, models change each hour
#define BC_INTERVAL     2400                    // polling interval, secs
#define VOACAP_INTERVAL 2500                    // polling interval, secs
static const char bc_page[] = "/ham/HamClock/fetchBandConditions.pl";
static bool bc_reverting;                       // set while waiting for BC after WX
static int bc_hour, map_hour;                   // hour when valid
uint16_t bc_power;                              // VOACAP power setting
PropMapSetting prop_map = PROP_MAP_OFF;         // whether/how showing background prop map

// background map update intervals
#define DRAPMAP_INTERVAL     (5*60)             // polling interval, secs
#define OTHER_MAPS_INTERVAL  (60*60)            // polling interval, secs

// DRAP info, new data posted every few minutes
#define DRAPPLOT_INTERVAL    (DRAPMAP_INTERVAL+5) // polling interval, secs. N.B. avoid race with MAP
#define DRAPPLOT_COLOR  RA8875_RED              // loading message text color
static const char drap_page[] = "/ham/HamClock/drap/stats.txt";

// NOAA RSG space weather scales
#define NOAASWX_INTERVAL     3700               // polling interval, secs
static const char noaaswx_page[] = "/ham/HamClock/NOAASpaceWX/noaaswx.txt";

// geolocation web page
static const char locip_page[] = "/ham/HamClock/fetchIPGeoloc.pl";

// SDO images
#define SDO_INTERVAL    3200                    // polling interval, secs
#define SDO_COLOR       RA8875_MAGENTA          // loading message text color
// N.B. files must match order in plot_names[]
static const char *sdo_filename[4] = {
    #if defined(_CLOCK_1600x960) 
        "/ham/HamClock/SDO/f_211_193_171_340.bmp",
        "/ham/HamClock/SDO/latest_340_HMIIC.bmp",
        "/ham/HamClock/SDO/latest_340_HMIB.bmp",
        "/ham/HamClock/SDO/f_193_340.bmp",
    #elif defined(_CLOCK_2400x1440)
        "/ham/HamClock/SDO/f_211_193_171_510.bmp",
        "/ham/HamClock/SDO/latest_510_HMIIC.bmp",
        "/ham/HamClock/SDO/latest_510_HMIB.bmp",
        "/ham/HamClock/SDO/f_193_510.bmp",
    #elif defined(_CLOCK_3200x1920)
        "/ham/HamClock/SDO/f_211_193_171_680.bmp",
        "/ham/HamClock/SDO/latest_680_HMIIC.bmp",
        "/ham/HamClock/SDO/latest_680_HMIB.bmp",
        "/ham/HamClock/SDO/f_193_680.bmp",
    #else
        "/ham/HamClock/SDO/f_211_193_171_170.bmp",
        "/ham/HamClock/SDO/latest_170_HMIIC.bmp",
        "/ham/HamClock/SDO/latest_170_HMIB.bmp",
        "/ham/HamClock/SDO/f_193_170.bmp",
    #endif
};

// weather displays
#define DEWX_INTERVAL   1700                    // polling interval, secs
#define DXWX_INTERVAL   1600                    // polling interval, secs

// moon display
#define MOON_INTERVAL   30                      // update interval, secs
static bool moon_reverting;                     // flag for revertPlot1();


// list of default NTP servers unless user has set their own
static NTPServer ntp_list[] = {                 // init times to 0 insures all get tried initially
    {"pool.ntp.org", 0},
    {"time.google.com", 0},
    {"time.apple.com", 0},
    {"time.nist.gov", 0},
    {"europe.pool.ntp.org", 0},
    {"asia.pool.ntp.org", 0},
};
#define N_NTP NARRAY(ntp_list)                  // number of possible servers


// web site retry interval, secs
#define WIFI_RETRY      15

// pane auto rotation period in seconds -- most are the same but wx is longer
#define ROTATION_INTERVAL       30
#define ROTATION_WX_INTERVAL    60

// time of next attempts -- 0 will refresh immediately -- reset in initWiFiRetry()
static time_t next_flux;
static time_t next_ssn;
static time_t next_xray;
static time_t next_kp;
static time_t next_rss;
static time_t next_sdo_1;
static time_t next_sdo_2;
static time_t next_sdo_3;
static time_t next_sdo_4;
static time_t next_noaaswx;
static time_t next_dewx;
static time_t next_dxwx;
static time_t next_bc;
static time_t next_map;
static time_t next_moon;
static time_t next_gimbal;
static time_t next_dxcluster;
static time_t next_bme280_t;
static time_t next_bme280_p;
static time_t next_bme280_h;
static time_t next_bme280_d;
static time_t next_swind;
static time_t next_drap;
static time_t next_stereo_a;

// persisent space weather data and refresh time for use by getSpaceWeather()
static time_t ssn_update, xray_update, flux_update, kp_update, noaa_update, swind_update;
static time_t drap_update, path_update;
static float ssn_spw, xray_spw, flux_spw, kp_spw, swind_spw, drap_spw;
static float path_spw[PROP_MAP_N]; 
static NOAASpaceWx noaa_spw;

// local funcs
static bool updateKp(SBox &box);
static bool updateXRay(const SBox &box);
static bool updateSDO (const SBox &box, PlotChoice ch);
static bool updateSTEREO_A (const SBox &box);
static bool updateSunSpots(const SBox &box);
static bool updateSolarFlux(const SBox &box);
static bool updateBandConditions(const SBox &box);
static bool updateNOAASWx(const SBox &box);
static bool updateSolarWind(const SBox &box);
static bool updateDRAPPlot(const SBox &box);
static bool updateRSS (void);
static uint32_t crackBE32 (uint8_t bp[]);

/* return the next retry time_t.
 * retries are spaced out every WIFI_RETRY to avoid swamping the server
 */
static time_t nextWiFiRetry()
{
    static time_t prev_try;
    time_t t0 = now();
    time_t next_t0 = t0 + WIFI_RETRY;                   // interval after now
    time_t next_try = prev_try + WIFI_RETRY;            // interval after prev rot
    prev_try = next_t0 > next_try ? next_t0 : next_try; // use whichever is later
    return (prev_try);
}

/* return when next to rotate the given pane.
 * rotations are spaced out to avoid swamping the server or supporting service.
 */
static time_t nextPaneRotationTime(PlotPane pp, PlotChoice pc)
{
    time_t interval = (pc == PLOT_CH_DEWX || pc == PLOT_CH_DXWX) ? ROTATION_WX_INTERVAL : ROTATION_INTERVAL;
    time_t rot_time = now() + interval;

    // find soonest rot_time that is interval from all other active panes
    for (int i = 0; i < PANE_N*PANE_N; i++) {           // all permutations
        PlotPane ppi = (PlotPane) (i % PANE_N);
        if (ppi != pp && paneIsRotating((PlotPane)ppi)) {
            if ((rot_time >= next_rotationT[ppi] && rot_time - next_rotationT[ppi] < interval)
                  || (rot_time <= next_rotationT[ppi] && next_rotationT[ppi] - rot_time < interval))
                rot_time = next_rotationT[ppi] + interval;
        }
    }

    return (rot_time);
}

/* set de_ll.lat_d and de_ll.lng_d from the given ip else our public ip.
 * report status via tftMsg
 */
static void geolocateIP (const char *ip)
{
    WiFiClient iploc_client;                            // wifi client connection
    float lat, lng;
    char llline[80];
    char ipline[80];
    char credline[80];
    int nlines = 0;

    resetWatchdog();
    if (wifiOk() && iploc_client.connect(svr_host, HTTPPORT)) {

        // create proper query
        size_t l = snprintf (llline, sizeof(llline), "%s", locip_page);
        if (ip)
            l += snprintf (llline+l, sizeof(llline)-l, "?IP=%s", ip);
        Serial.println(llline);

        // send
        httpGET (iploc_client, svr_host, llline);
        if (!httpSkipHeader (iploc_client)) {
            Serial.println (F("geoIP header short"));
            goto out;
        }

        // expect 4 lines: LAT=, LNG=, IP= and CREDIT=, anything else first line is error message
        if (!getTCPLine (iploc_client, llline, sizeof(llline), NULL))
            goto out;
        nlines++;
        lat = atof (llline+4);
        if (!getTCPLine (iploc_client, llline, sizeof(llline), NULL))
            goto out;
        nlines++;
        lng = atof (llline+4);
        if (!getTCPLine (iploc_client, ipline, sizeof(ipline), NULL))
            goto out;
        nlines++;
        if (!getTCPLine (iploc_client, credline, sizeof(credline), NULL))
            goto out;
        nlines++;
    }

out:

    if (nlines == 4) {
        // ok

        tftMsg (true, 0, _FX("IP %s geolocation"), ipline+3);
        tftMsg (true, 0, _FX("  by %s"), credline+7);
        tftMsg (true, 0, _FX("  %.2f%c %.2f%c"), fabsf(lat), lat < 0 ? 'S' : 'N',
                                fabsf(lng), lng < 0 ? 'W' : 'E');

        de_ll.lat_d = lat;
        de_ll.lng_d = lng;
        normalizeLL (de_ll);
        NVWriteFloat (NV_DE_LAT, de_ll.lat_d);
        NVWriteFloat (NV_DE_LNG, de_ll.lng_d);
        setNVMaidenhead(NV_DE_GRID, de_ll);
        de_tz.tz_secs = getTZ (de_ll);
        NVWriteInt32(NV_DE_TZ, de_tz.tz_secs);


    } else {
        // trouble, error message if 1 line

        if (nlines == 1) {
            tftMsg (true, 0, _FX("IP geolocation err:"));
            tftMsg (true, 1000, _FX("  %s"), llline);
        } else
            tftMsg (true, 1000, _FX("IP geolocation failed"));
    }

    iploc_client.stop();
    resetWatchdog();
    printFreeHeap (F("geolocateIP"));
}

/* search ntp_list for the fastest so far, or rotate if all bad.
 * N.B. always return one of ntp_list, never NULL
 */
static NTPServer *findBestNTP()
{
    static uint8_t prev_fixed;

    NTPServer *best_ntp = &ntp_list[0];
    int rsp_min = ntp_list[0].rsp_time;

    for (unsigned i = 1; i < N_NTP; i++) {
        NTPServer *np = &ntp_list[i];
        if (np->rsp_time < rsp_min) {
            best_ntp = np;
            rsp_min = np->rsp_time;
        }
    }
    if (rsp_min == NTP_TOO_LONG) {
        prev_fixed = (prev_fixed+1) % N_NTP;
        best_ntp = &ntp_list[prev_fixed];
    }
    return (best_ntp);
}


/* init and connect, inform via tftMsg() if verbose.
 * non-verbose is used for automatic retries that should not clobber the display.
 */
static void initWiFi (bool verbose)
{
    // N.B. look at the usages and make sure this is "big enough"
    static const char dots[] = ".........................................";

    // probable mac when only localhost -- used to detect LAN but no WLAN
    const char *mac_lh = _FX("FF:FF:FF:FF:FF:FF");

    resetWatchdog();

    // begin
    // N.B. ESP seems to reconnect much faster if avoid begin() unless creds change
    // N.B. non-RPi UNIX systems return NULL from getWiFI*()
    WiFi.mode(WIFI_STA);
    const char *myssid = getWiFiSSID();
    const char *mypw = getWiFiPW();
    if (myssid && mypw && (strcmp (WiFi.SSID().c_str(), myssid) || strcmp (WiFi.psk().c_str(), mypw)))
        WiFi.begin ((char*)myssid, (char*)mypw);

    // prep
    resetWatchdog();
    uint32_t t0 = millis();
    uint32_t timeout = verbose ? 30000UL : 3000UL;      // dont wait nearly as long for a retry, millis
    uint16_t ndots = 0;                                 // progress counter
    char mac[30];
    strcpy (mac, WiFi.macAddress().c_str());
    tftMsg (verbose, 0, _FX("MAC addr: %s"), mac);

    // wait for connection
    resetWatchdog();
    if (myssid)
        tftMsg (verbose, 0, "\r");                      // init overwrite
    do {
        if (myssid)
            tftMsg (verbose, 0, _FX("Connecting to %s %.*s\r"), myssid, ndots, dots);
        Serial.printf (_FX("Trying network %d\n"), ndots);
        if (timesUp(&t0,timeout) || ndots == (sizeof(dots)-1)) {
            if (myssid)
                tftMsg (verbose, 1000, _FX("WiFi failed -- check credentials?"));
            else
                tftMsg (verbose, 1000, _FX("Network connection attempt failed"));
            return;
        }

        wdDelay(1000);
        ndots++;

        // WiFi.printDiag(Serial);

    } while (strcmp (mac, mac_lh) && (WiFi.status() != WL_CONNECTED));

    // init retry times
    initWiFiRetry();

    // report stats
    resetWatchdog();
    if (WiFi.status() == WL_CONNECTED) {
        IPAddress ip;
        ip = WiFi.localIP();
        tftMsg (verbose, 0, _FX("IP: %d.%d.%d.%d"), ip[0], ip[1], ip[2], ip[3]);
        ip = WiFi.subnetMask();
        tftMsg (verbose, 0, _FX("Mask: %d.%d.%d.%d"), ip[0], ip[1], ip[2], ip[3]);
        ip = WiFi.gatewayIP();
        tftMsg (verbose, 0, _FX("GW: %d.%d.%d.%d"), ip[0], ip[1], ip[2], ip[3]);
        ip = WiFi.dnsIP();
        tftMsg (verbose, 0, _FX("DNS: %d.%d.%d.%d"), ip[0], ip[1], ip[2], ip[3]);
        tftMsg (verbose, 0, _FX("Hostname: %s"), WiFi.hostname().c_str());
        if (WiFi.RSSI() < 10) {
            tftMsg (verbose, 0, _FX("Signal strength: %d dBm"), WiFi.RSSI());
            tftMsg (verbose, 0, _FX("Channel: %d"), WiFi.channel());
        }
        tftMsg (verbose, 0, _FX("S/N: %u"), ESP.getChipId());
    }

    // start web server for remote commands
    if (WiFi.status() == WL_CONNECTED || !strcmp (mac, mac_lh)) {
        tftMsg (verbose, 0, _FX("Start web server"));
        initWebServer();
    } else {
        tftMsg (verbose, 0, _FX("No web server"));
    }

    // retrieve cities
    readCities();
}

/* call exactly once to init wifi, maps and maybe time and location.
 * report on initial startup screen with tftMsg.
 */
void initSys()
{
    // start/check WLAN
    initWiFi(true);

    // init location if desired
    if (useGeoIP() || init_iploc || init_locip) {
        if (WiFi.status() == WL_CONNECTED)
            geolocateIP (init_locip);
        else
            tftMsg (true, 0, _FX("no network for geo IP"));
    } else if (useGPSD()) {
        LatLong lltmp;
        if (getGPSDLatLong(&lltmp)) {

            // good -- set de_ll
            de_ll = lltmp;
            normalizeLL (de_ll);
            NVWriteFloat (NV_DE_LAT, de_ll.lat_d);
            NVWriteFloat (NV_DE_LNG, de_ll.lng_d);
            setNVMaidenhead(NV_DE_GRID, de_ll);
            de_tz.tz_secs = getTZ (de_ll);
            NVWriteInt32(NV_DE_TZ, de_tz.tz_secs);

            tftMsg (true, 0, _FX("GPSD: %.2f%c %.2f%c"),
                                fabsf(de_ll.lat_d), de_ll.lat_d < 0 ? 'S' : 'N',
                                fabsf(de_ll.lng_d), de_ll.lng_d < 0 ? 'W' : 'E');

        } else
            tftMsg (true, 1000, _FX("GPSD: no Lat/Long"));
    }


    // skip box
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    drawStringInBox (_FX("Skip"), skip_b, false, RA8875_WHITE);
    bool skipped_here = false;


    // init time service as desired
    if (useGPSD()) {
        if (getGPSDUTC(&gpsd_server)) {
            tftMsg (true, 0, _FX("GPSD: time ok"));
            initTime();
        } else
            tftMsg (true, 1000, _FX("GPSD: no time"));

    } else if (WiFi.status() == WL_CONNECTED) {

        if (useLocalNTPHost()) {

            // test user choice
            const char *local_ntp = getLocalNTPHost();
            tftMsg (true, 0, _FX("NTP test %s ...\r"), local_ntp);
            if (getNTPUTC(&ntp_server))
                tftMsg (true, 0, _FX("NTP %s: ok\r"), local_ntp);
            else
                tftMsg (true, 0, _FX("NTP %s: fail\r"), local_ntp);
        } else {

            // try all the NTP servers to find the fastest (with sneaky way out)
            SCoord s;
            drainTouch();
            tftMsg (true, 0, _FX("Finding best NTP ..."));
            NTPServer *best_ntp = NULL;
            for (unsigned i = 0; i < N_NTP; i++) {
                NTPServer *np = &ntp_list[i];

                // measure the next. N.B. assumes we stay in sync
                if (getNTPUTC(&ntp_server) == 0)
                    tftMsg (true, 0, _FX("%s: err\r"), np->server);
                else {
                    tftMsg (true, 0, _FX("%s: %d ms\r"), np->server, np->rsp_time);
                    if (!best_ntp || np->rsp_time < best_ntp->rsp_time)
                        best_ntp = np;
                }

                // cancel scan if tapped and found at least one good
                if (best_ntp && (skip_skip || (readCalTouch(s) != TT_NONE && inBox (s, skip_b)))) {
                    drawStringInBox (_FX("Skip"), skip_b, true, RA8875_WHITE);
                    Serial.printf (_FX("NTP search cancelled with %s\n"), best_ntp->server);
                    skipped_here = true;
                    break;
                }
            }
            if (!skip_skip)
                wdDelay(800); // linger to show last time
            if (best_ntp)
                tftMsg (true, 0, _FX("Best NTP: %s %d ms\r"), best_ntp->server, best_ntp->rsp_time);
            else
                tftMsg (true, 0, _FX("No NTP\r"));
            drainTouch();
        }
        tftMsg (true, 0, NULL);   // next row

        // go
        initTime();

    } else {

        tftMsg (true, 0, _FX("No time"));
    }


    // init fs
    LittleFS.begin();
    LittleFS.setTimeCallback(now);

    // insure initial default map files are installed and ready to go
    if (!installBackgroundMaps (true, CM_NONE, NULL))
        tftMsg (true, 0, _FX("No map"));

    // offer time to peruse unless alreay opted to skip
    if (!skipped_here) {
        #define     TO_DS 100                               // timeout delay, decaseconds
        drawStringInBox (_FX("Skip"), skip_b, false, RA8875_WHITE);
        uint8_t s_left = TO_DS/10;                          // seconds remaining
        uint32_t t0 = millis();
        drainTouch();
        for (uint8_t ds_left = TO_DS; !skip_skip && ds_left > 0; --ds_left) {
            SCoord s;
            if (readCalTouch(s) != TT_NONE && inBox(s, skip_b)) {
                drawStringInBox (_FX("Skip"), skip_b, true, RA8875_WHITE);
                break;
            }
            if ((TO_DS - (millis() - t0)/100)/10 < s_left) {
                // just printing every ds_left/10 is too slow due to overhead
                char buf[30];
                sprintf (buf, _FX("Ready ... %d\r"), s_left--);
                tftMsg (true, 0, buf);
            }
            wdDelay(100);
        }
    }


    // handy place to init bc_power
    if (bc_power == 0 && !NVReadUInt16 (NV_BCPOWER, &bc_power))
        bc_power = 100;
}

/* update BandConditions pane in box b if needed or requested.
 */
void checkBandConditions (const SBox &b, bool force)
{
    // update if asked to or out of sync with map or time to refresh
    bool update_bc = force || (prop_map != PROP_MAP_OFF && bc_hour != map_hour) || (now() > next_bc);
    if (!update_bc)
        return;

    if (updateBandConditions(b)) {
        // worked ok so reschedule later
        next_bc = now() + BC_INTERVAL;
        bc_hour = hour(nowWO());
    } else {
        // retry
        next_bc = nextWiFiRetry();
    }
}

/* check if time to update VOACAP or core map.
 */
static void checkMap(void)
{
    // note whether BC is up
    PlotPane bc_pp = findPaneChoiceNow (PLOT_CH_BC);
    bool bc_up = bc_pp != PANE_NONE;

    // check VOACAP first
    if (prop_map != PROP_MAP_OFF) {

        // update if time or to stay in sync with BC if on
        if (now() > next_map || (bc_up && map_hour != bc_hour)) {

            // show pending if BC up
            if (bc_up)
                plotBandConditions (plot_b[bc_pp], 1, NULL, NULL);

            // update prop map
            bool ok = installPropMaps (propMap2MHz (prop_map));
            if (ok) {
                next_map = now() + VOACAP_INTERVAL;             // schedule normal refresh
                map_hour = hour(nowWO());                       // map is now current
                initEarthMap();

                // sync DRAP plot update if in use
                if (findPaneChoiceNow(PLOT_CH_DRAP) != PANE_NONE)
                    next_drap = now();
            } else {
                next_map = nextWiFiRetry();                     // schedule retry
                map_hour = bc_hour;                             // match bc to avoid immediate retry
            }

            // show result of effort unless no BC box
            if (bc_up)
                plotBandConditions (plot_b[bc_pp], ok ? 0 : -1, NULL, NULL);

            Serial.printf (_FX("Next VOACAP map check in %ld s at %ld\n"), next_map - now(), next_map);

        }

    } else if (core_map != CM_NONE) {

        if (now() > next_map) {

            bool downloaded;
            if (installBackgroundMaps (false, core_map, &downloaded)) {
                // schedule next refresh
                if (core_map == CM_DRAP) {
                    next_map = now() + DRAPMAP_INTERVAL;
                } else {
                    next_map = now() + OTHER_MAPS_INTERVAL;
                }
                if (downloaded)
                    initEarthMap();                             // avoid redraw if no change
            } else
                next_map = nextWiFiRetry();                     // schedule retry

            Serial.printf (_FX("Next %s map check in %ld s at %ld\n"), map_styles[core_map],
                                        next_map - now(), next_map);
        }
    }
}


/* check for tap at s known to be within BandConditions box b.
 * tapping a band loads prop map, tapping power cycles bc_power 1-1000 W.
 * return whether tap was useful for us.
 * N.B. coordinate tap positions with plotBandConditions()
 */
bool checkBCTouch (const SCoord &s, const SBox &b)
{
    // done if tap title
    if (s.y < b.y+b.h/5)
        return (false);

    // ll corner for power cycle
    SBox power_b;
    power_b.x = b.x + 2;
    power_b.y = b.y + 89*b.h/100;
    power_b.w = b.w/3;
    power_b.h = b.y + b.h - power_b.y - 1;
    // tft.drawRect (power_b.x, power_b.y, power_b.w, power_b.h, RA8875_WHITE);

    if (inBox (s, power_b)) {

        // show menu of available power choices
        #define N_POW 4
        MenuItem mitems[N_POW] = {
            {MENU_1OFN, bc_power == 1, 5, "1 watt"},
            {MENU_1OFN, bc_power == 10, 5, "10 watts"},
            {MENU_1OFN, bc_power == 100, 5, "100 watts"},
            {MENU_1OFN, bc_power == 1000, 5, "1000 watts"},
        };
        Menu menu = { 1, N_POW, N_POW, mitems };

        SBox menu_b;
        menu_b.x = b.x + 5;
        menu_b.y = b.y + b.h/2;
        menu_b.w = 100;
        menu_b.h = 0;   // set automatically

        // run menu, find selection
        SBox ok_b;
        uint16_t new_power = bc_power;
        if (runMenu (menu, b, menu_b, ok_b)) {
            for (int i = 0; i < N_POW; i++) {
                if (menu.items[i].set) {
                    new_power = powf (10, i);
                    break;
                }
            }
        }

        // always redo BC if nothing else to erase menu but only update voacap if power changed
        bool power_changed = new_power != bc_power;
        bc_power = new_power;
        NVWriteUInt16 (NV_BCPOWER, bc_power);
        checkBandConditions (b, true);
        if (power_changed)
            newVOACAPMap(prop_map);

    } else {

        // toggle band depending on position, if any.
        // N.B. coordinate this with plotBandConditions() layout
        PropMapSetting new_prop_map = prop_map;
        if (s.x < b.x + b.w/4) {
            int i = (b.y + b.h - 20 - s.y) / ((b.h - 47)/PROP_MAP_N);
            if (i == prop_map) {
                // tapped current VOACAP selection: toggle current setting
                new_prop_map = prop_map == PROP_MAP_OFF ? ((PropMapSetting)i) : PROP_MAP_OFF;
            } else if (i >= 0 && i < PROP_MAP_N) {
                // tapped a different VOACAP selection
                new_prop_map = (PropMapSetting)i;
            }
        }

        // update map if state change
        if (new_prop_map != prop_map) {
            if (new_prop_map == PROP_MAP_OFF) {
                prop_map = PROP_MAP_OFF;
                newCoreMap (core_map);
                plotBandConditions (b, 0, NULL, NULL);
            } else
                newVOACAPMap(new_prop_map);
        }

    }

    // ours just because tap was below title
    return (true);
}

/* arrange to resume PANE_1 after dt millis
 */
static void revertPlot1 (uint32_t dt)
{
    time_t revert_t = now() + dt/1000;

    // don't rotate until after revert_t
    if (paneIsRotating(PANE_1) && next_rotationT[PANE_1] < revert_t)
        next_rotationT[PANE_1] = revert_t;

    switch (plot_ch[PANE_1]) {
    case PLOT_CH_DXWX:
        next_dxwx = revert_t;
        break;
    case PLOT_CH_NOAASWX:
        next_noaaswx = revert_t;
        break;
    case PLOT_CH_SSN:
        next_ssn = revert_t;
        break;
    case PLOT_CH_XRAY:
        next_xray = revert_t;
        break;
    case PLOT_CH_FLUX:
        next_flux = revert_t;
        break;
    case PLOT_CH_KP:
        next_kp = revert_t;
        break;
    case PLOT_CH_BC:
        next_bc = revert_t;
        bc_reverting = true;
        break;
    case PLOT_CH_DEWX:
        next_dewx = revert_t;
        break;
    case PLOT_CH_MOON:
        next_moon = revert_t;
        moon_reverting = true;
        break;
    case PLOT_CH_DXCLUSTER:
        closeDXCluster();         // reopen after revert
        next_dxcluster = revert_t;
        break;
    case PLOT_CH_GIMBAL:
        next_gimbal = revert_t;
        break;
    case PLOT_CH_SDO_1:
        next_sdo_1 = revert_t;
        break;
    case PLOT_CH_SDO_2:
        next_sdo_2 = revert_t;
        break;
    case PLOT_CH_SDO_3:
        next_sdo_3 = revert_t;
        break;
    case PLOT_CH_SDO_4:
        next_sdo_4 = revert_t;
        break;
    case PLOT_CH_TEMPERATURE:
        next_bme280_t = revert_t;
        break;
    case PLOT_CH_PRESSURE:
        next_bme280_p = revert_t;
        break;
    case PLOT_CH_HUMIDITY:
        next_bme280_h = revert_t;
        break;
    case PLOT_CH_DEWPOINT:
        next_bme280_d = revert_t;
        break;
    case PLOT_CH_SOLWIND:
        next_swind = revert_t;
        break;
    case PLOT_CH_DRAP:
        next_drap = revert_t;
        break;
    case PLOT_CH_COUNTDOWN:
        // TODO?
        break;
    case PLOT_CH_STEREO_A:
        next_stereo_a = revert_t;
        break;
    default:
        fatalError(_FX("Bug! revertPlot1() choice %d"), plot_ch[PANE_1]);
        break;
    }
}

/* try to set the given pane to the given plot choice.
 * N.B. it's ok to set pane to same choice.
 * if ok then schedule for immediate refresh.
 * return whether successful
 * N.B. we might change plot_ch but we NEVER change plot_rotset here
 */
bool setPlotChoice (PlotPane pp, PlotChoice ch)
{
    // refuse if new choice is already in some other pane
    PlotPane pp_now = findPaneForChoice (ch);
    if (pp_now != PANE_NONE && pp_now != pp)
        return (false);

    // display box
    SBox &box = plot_b[pp];

    uint32_t sw_timer;

    switch (ch) {

    case PLOT_CH_BC:
        plot_ch[pp] = PLOT_CH_BC;
        next_bc = 0;
        break;

    case PLOT_CH_DEWX:
        plot_ch[pp] = PLOT_CH_DEWX;
        next_dewx = 0;
        break;

    case PLOT_CH_DXCLUSTER:
        if (!useDXCluster())
            return (false);
        plot_ch[pp] = PLOT_CH_DXCLUSTER;
        next_dxcluster = 0;
        break;

    case PLOT_CH_DXWX:
        plot_ch[pp] = PLOT_CH_DXWX;
        next_dxwx = 0;
        break;

    case PLOT_CH_FLUX:
        plot_ch[pp] = PLOT_CH_FLUX;
        next_flux = 0;
        break;

    case PLOT_CH_KP:
        plot_ch[pp] = PLOT_CH_KP;
        next_kp = 0;
        break;

    case PLOT_CH_MOON:
        plot_ch[pp] = PLOT_CH_MOON;
        next_moon = 0; 
        break;

    case PLOT_CH_NOAASWX:
        plot_ch[pp] = PLOT_CH_NOAASWX;
        next_noaaswx = 0;
        break;

    case PLOT_CH_SSN:
        plot_ch[pp] = PLOT_CH_SSN;
        next_ssn = 0;
        break;

    case PLOT_CH_XRAY:
        plot_ch[pp] = PLOT_CH_XRAY;
        next_xray = 0;
        break;

    case PLOT_CH_GIMBAL:
        // insure enabled and init if not already on this pane
        if (!haveGimbal())
            return (false);
        if (plot_ch[pp] != PLOT_CH_GIMBAL) {
            plot_ch[pp] = PLOT_CH_GIMBAL;
            initGimbalGUI(box);
        }
        break;

    case PLOT_CH_TEMPERATURE:
        if (getNBMEConnected() == 0)
            return (false);
        plot_ch[pp] = ch;
        drawOneBME280Pane (box, ch);
        next_bme280_t = 0;
        break;

    case PLOT_CH_PRESSURE:
        if (getNBMEConnected() == 0)
            return (false);
        plot_ch[pp] = ch;
        drawOneBME280Pane (box, ch);
        next_bme280_p = 0;
        break;

    case PLOT_CH_HUMIDITY:
        if (getNBMEConnected() == 0)
            return (false);
        plot_ch[pp] = ch;
        drawOneBME280Pane (box, ch);
        next_bme280_h = 0;
        break;

    case PLOT_CH_DEWPOINT:
        if (getNBMEConnected() == 0)
            return (false);
        plot_ch[pp] = ch;
        drawOneBME280Pane (box, ch);
        next_bme280_d = 0;
        break;

    case PLOT_CH_SDO_1:
        plot_ch[pp] = ch;
        next_sdo_1 = 0;
        break;

    case PLOT_CH_SDO_2:
        plot_ch[pp] = ch;
        next_sdo_2 = 0;
        break;

    case PLOT_CH_SDO_3:
        plot_ch[pp] = ch;
        next_sdo_3 = 0;
        break;

    case PLOT_CH_SDO_4:
        plot_ch[pp] = ch;
        next_sdo_4 = 0;
        break;

    case PLOT_CH_SOLWIND:
        plot_ch[pp] = ch;
        next_swind = 0;
        break;

    case PLOT_CH_DRAP:
        plot_ch[pp] = ch;
        next_drap = 0;
        break;

    case PLOT_CH_COUNTDOWN:
        if (getSWEngineState(sw_timer) != SWE_COUNTDOWN)
            return (false);
        plot_ch[pp] = ch;
        if (getSWDisplayState() == SWD_NONE)
            drawMainPageStopwatch(true);
        break;

    case PLOT_CH_STEREO_A:
        plot_ch[pp] = ch;
        next_stereo_a = 0;
        break;

    default:
        fatalError (_FX("setPlotChoice() PlotPane %d, PlotChoice %d"), (int)pp, (int)ch);
        break;

    }

    // insure DX and gimbal are off if not selected for display
    if (findPaneChoiceNow (PLOT_CH_DXCLUSTER) == PANE_NONE)
        closeDXCluster();
    if (findPaneChoiceNow (PLOT_CH_GIMBAL) == PANE_NONE)
        closeGimbal();

    // persist
    savePlotOps();

    // schedule next rotation if enabled
    if (paneIsRotating(pp))
        next_rotationT[pp] = nextPaneRotationTime(pp, ch);

    // ok!
    return (true);
}

/* check if it is time to update any info via wifi.
 * proceed even if no wifi to allow subsystems to update.
 */
void updateWiFi(void)
{
    resetWatchdog();

    // time now
    time_t t0 = now();

    // update each pane
    for (int i = PANE_1; i < PANE_N; i++) {

        SBox &box = plot_b[i];
        PlotChoice ch = plot_ch[i];
        PlotPane pp = (PlotPane)i;
        bool new_rot_ch = false;

        // rotate if this pane is rotating and it's time
        if (paneIsRotating(pp) && t0 >= next_rotationT[i]) {
            setPlotChoice (pp, getNextRotationChoice(pp, plot_ch[pp]));
            new_rot_ch = true;
            ch = plot_ch[pp];
        }

        switch (ch) {

        case PLOT_CH_BC:
            checkBandConditions (box, false);
            break;

        case PLOT_CH_DEWX:
            if (t0 >= next_dewx) {
                if (updateDEWX(box))
                    next_dewx = now() + DEWX_INTERVAL;
                else
                    next_dewx = nextWiFiRetry();
            }
            break;

        case PLOT_CH_DXCLUSTER:
            if (t0 >= next_dxcluster) {
                if (updateDXCluster(box))
                    next_dxcluster = 0;   // constant poll
                else
                    next_dxcluster = nextWiFiRetry();
            }
            break;

        case PLOT_CH_DXWX:
            if (t0 >= next_dxwx) {
                if (updateDXWX(box))
                    next_dxwx = now() + DXWX_INTERVAL;
                else
                    next_dxwx = nextWiFiRetry();
            }
            break;

        case PLOT_CH_FLUX:
            if (t0 >= next_flux) {
                if (updateSolarFlux(box))
                    next_flux = now() + FLUX_INTERVAL;
                else
                    next_flux = nextWiFiRetry();
            }
            break;

        case PLOT_CH_KP:
            if (t0 >= next_kp) {
                if (updateKp(box))
                    next_kp = now() + KP_INTERVAL;
                else
                    next_kp = nextWiFiRetry();
            }
            break;

        case PLOT_CH_MOON:
            if (t0 >= next_moon) {
                updateMoonPane(next_moon == 0 || moon_reverting);
                moon_reverting = false;
                next_moon = now() + MOON_INTERVAL;
            }
            break;

        case PLOT_CH_NOAASWX:
            if (t0 >= next_noaaswx) {
                if (updateNOAASWx(box))
                    next_noaaswx = now() + NOAASWX_INTERVAL;
                else
                    next_noaaswx = nextWiFiRetry();
            }
            break;

        case PLOT_CH_SSN:
            if (t0 >= next_ssn) {
                if (updateSunSpots(box))
                    next_ssn = now() + SSPOT_INTERVAL;
                else
                    next_ssn = nextWiFiRetry();
            }
            break;

        case PLOT_CH_XRAY:
            if (t0 >= next_xray) {
                if (updateXRay(box))
                    next_xray = now() + XRAY_INTERVAL;
                else
                    next_xray = nextWiFiRetry();
            }
            break;

        case PLOT_CH_GIMBAL:
            // update always unless standing by for revert
            if (next_gimbal == 0 || t0 >= next_gimbal) {
                updateGimbal();
                next_gimbal = 0;
            }
            break;

        case PLOT_CH_TEMPERATURE:
            // plot when pane reverts or new data is ready
            if ((next_bme280_t > 0 && t0 >= next_bme280_t) || (next_bme280_t == 0 && newBME280data(ch))) {
                drawOneBME280Pane (box, ch);
                next_bme280_t = 0;
            }
            break;

        case PLOT_CH_PRESSURE:
            // plot when pane reverts or new data is ready
            if ((next_bme280_p > 0 && t0 >= next_bme280_p) || (next_bme280_p == 0 && newBME280data(ch))) {
                drawOneBME280Pane (box, ch);
                next_bme280_p = 0;
            }
            break;

        case PLOT_CH_HUMIDITY:
            // plot when pane reverts or new data is ready
            if ((next_bme280_h > 0 && t0 >= next_bme280_h) || (next_bme280_h == 0 && newBME280data(ch))) {
                drawOneBME280Pane (box, ch);
                next_bme280_h = 0;
            }
            break;

        case PLOT_CH_DEWPOINT:
            // plot when pane reverts or new data is ready
            if ((next_bme280_d > 0 && t0 >= next_bme280_d) || (next_bme280_d == 0 && newBME280data(ch))) {
                drawOneBME280Pane (box, ch);
                next_bme280_d = 0;
            }
            break;


        case PLOT_CH_SDO_1:
            if (t0 >= next_sdo_1) {
                if (updateSDO(box, ch))
                    next_sdo_1 = now() + SDO_INTERVAL;
                else
                    next_sdo_1 = nextWiFiRetry();
            }
            break;

        case PLOT_CH_SDO_2:
            if (t0 >= next_sdo_2) {
                if (updateSDO(box, ch))
                    next_sdo_2 = now() + SDO_INTERVAL;
                else
                    next_sdo_2 = nextWiFiRetry();
            }
            break;

        case PLOT_CH_SDO_3:
            if (t0 >= next_sdo_3) {
                if (updateSDO(box, ch))
                    next_sdo_3 = now() + SDO_INTERVAL;
                else
                    next_sdo_3 = nextWiFiRetry();
            }
            break;

        case PLOT_CH_SDO_4:
            if (t0 >= next_sdo_4) {
                if (updateSDO(box, ch))
                    next_sdo_4 = now() + SDO_INTERVAL;
                else
                    next_sdo_4 = nextWiFiRetry();
            }
            break;

        case PLOT_CH_SOLWIND:
            if (t0 >= next_swind) {
                if (updateSolarWind(box))
                    next_swind = now() + SWIND_INTERVAL;
                else
                    next_swind = nextWiFiRetry();
            }
            break;

        case PLOT_CH_DRAP:
            if (t0 >= next_drap) {
                if (updateDRAPPlot(box))
                    next_drap = now() + DRAPPLOT_INTERVAL;
                else
                    next_drap = nextWiFiRetry();
            }
            break;

        case PLOT_CH_COUNTDOWN:
            // handled by stopwatch system
            break;

        case PLOT_CH_STEREO_A:
            if (t0 >= next_stereo_a) {
                if (updateSTEREO_A(box))
                    next_stereo_a = now() + STEREO_A_INTERVAL;
                else
                    next_stereo_a = nextWiFiRetry();
            }
            break;

        default:
            fatalError (_FX("Bug! updateWiFi() bad choice: %d"), ch);
            break;
        }

        // show immediately this is as new rotating pane
        if (new_rot_ch)
            showRotatingBorder (true, pp);
    }

    // check if time to update map
    checkMap();

    // freshen RSS
    if (t0 >= next_rss) {
        if (updateRSS())
            next_rss = now() + RSS_INTERVAL;
        else
            next_rss = nextWiFiRetry();
    }

    // check for server commands
    checkWebServer();
}


/* NTP time server query.
 * returns UNIX time and server used if ok, or 0 if trouble.
 * for good NTP packet description try
 *   http://www.cisco.com
 *      /c/en/us/about/press/internet-protocol-journal/back-issues/table-contents-58/154-ntp.html
 */
time_t getNTPUTC(const char **server)
{
    // NTP contents packet
    static const uint8_t timeReqA[] = { 0xE3, 0x00, 0x06, 0xEC };
    static const uint8_t timeReqB[] = { 0x31, 0x4E, 0x31, 0x34 };

    // N.B. do not call wifiOk: now() -> us -> wifiOk -> initWiFi -> initWiFiRetry which forces all 
    // if (!wifiOk())
    //    return (0);

    // create udp endpoint
    WiFiUDP ntp_udp;
    resetWatchdog();
    if (!ntp_udp.begin(1234)) {                                 // any local port
        Serial.println (F("NTP: UDP startup failed"));
        return (0);
    }

    // decide on server: user's else fastest 
    NTPServer *ntp_use = &ntp_list[0];                          // a place for rsp_time if useLocal
    const char *ntp_server;
    if (useLocalNTPHost()) {
        ntp_server = getLocalNTPHost();
    } else {
        ntp_use = findBestNTP();
        ntp_server = ntp_use->server;
    }

    // NTP buffer and timers
    uint8_t  buf[48];
    uint32_t tx_ms, rx_ms;

    // Assemble request packet
    memset(buf, 0, sizeof(buf));
    memcpy(buf, timeReqA, sizeof(timeReqA));
    memcpy(&buf[12], timeReqB, sizeof(timeReqB));

    // send
    Serial.printf(_FX("NTP: Issuing request to %s\n"), ntp_server);
    resetWatchdog();
    ntp_udp.beginPacket (ntp_server, 123);                      // NTP uses port 123
    ntp_udp.write(buf, sizeof(buf));
    tx_ms = millis();                                           // record when packet sent
    if (!ntp_udp.endPacket()) {
        Serial.println (F("NTP: UDP write failed"));
        ntp_use->rsp_time = NTP_TOO_LONG;                       // force different choice next time
        ntp_udp.stop();
        return (0UL);
    }
    // Serial.print (F("NTP: Sent 48 ... "));
    resetWatchdog();

    // receive response
    // Serial.print(F("NTP: Awaiting response ... "));
    memset(buf, 0, sizeof(buf));
    uint32_t t0 = millis();
    while (!ntp_udp.parsePacket()) {
        if (timesUp (&t0, NTP_TOO_LONG)) {
            Serial.println(F("NTP: UDP timed out"));
            ntp_use->rsp_time = NTP_TOO_LONG;                   // force different choice next time
            ntp_udp.stop();
            return (0UL);
        }
        resetWatchdog();
        wdDelay(10);
    }
    rx_ms = millis();                                           // record when packet arrived
    resetWatchdog();

    // record response time
    ntp_use->rsp_time = rx_ms - tx_ms;
    Serial.printf (_FX("NTP: %s replied after %d ms\n"), ntp_server, ntp_use->rsp_time);

    // read response
    if (ntp_udp.read (buf, sizeof(buf)) != sizeof(buf)) {
        Serial.println (F("NTP: UDP read failed"));
        ntp_use->rsp_time = NTP_TOO_LONG;                       // force different choice next time
        ntp_udp.stop();
        return (0UL);
    }
    // IPAddress from = ntp_udp.remoteIP();
    // Serial.printf (_FX("NTP: received 48 from %d.%d.%d.%d\n"), from[0], from[1], from[2], from[3]);

    // only accept server responses which are mode 4
    uint8_t mode = buf[0] & 0x7;
    if (mode != 4) {                                            // insure server packet
        Serial.print (F("NTP: RX mode should be 4 but it is ")); Serial.println (mode);
        ntp_udp.stop();
        return (0UL);
    }

    // crack and advance to next whole second
    time_t unix_s = crackBE32 (&buf[40]) - 2208988800UL;        // packet transmit time - (1970 - 1900)
    if ((uint32_t)unix_s > 0x7FFFFFFFUL) {                      // sanity check beyond unsigned value
        Serial.printf (_FX("NTP: crazy large UNIX time: %ld\n"), unix_s);
        ntp_udp.stop();
        return (0UL);
    }
    uint32_t fraction_more = crackBE32 (&buf[44]);              // x / 10^32 additional second
    uint16_t ms_more = 1000UL*(fraction_more>>22)/1024UL;       // 10 MSB to ms
    uint16_t transit_time = (rx_ms - tx_ms)/2;                  // transit = half the round-trip time
    ms_more += transit_time;                                    // with transit now = unix_s + ms_more
    uint16_t sec_more = ms_more/1000U+1U;                       // whole seconds behind rounded up
    wdDelay (sec_more*1000U - ms_more);                         // wait to next whole second
    unix_s += sec_more;                                         // account for delay
    // Serial.print (F("NTP: Fraction ")); Serial.print(ms_more);
    // Serial.print (F(", transit ")); Serial.print(transit_time);
    // Serial.print (F(", seconds ")); Serial.print(sec_more);
    // Serial.print (F(", UNIX ")); Serial.print (unix_s); Serial.println();
    resetWatchdog();

    // one more sanity check
    if (unix_s < 1577836800L) {          // Jan 1 2020
        Serial.printf (_FX("NTP: crazy small UNIX time: %ld\n"), unix_s);
        ntp_udp.stop();
        return (0UL);
    }

    ntp_udp.stop();
    *server = ntp_server;
    printFreeHeap (F("NTP"));
    return (unix_s);
}

/* read next char from client.
 * return whether another character was in fact available.
 */
bool getChar (WiFiClient &client, char *cp)
{
    #define GET_TO 5000 // millis()

    resetWatchdog();

    // wait for char
    uint32_t t0 = millis();
    while (!client.available()) {
        resetWatchdog();
        if (!client.connected()) {
            // Serial.print (F("surprise getChar disconnect\n"));
            return (false);
        }
        if (timesUp(&t0,GET_TO)) {
            Serial.print (F("surprise getChar timeout\n"));
            return (false);
        }
        wdDelay(10);
    }

    // read, which has another way to indicate failure
    int c = client.read();
    if (c < 0) {
        Serial.print (F("bad getChar read\n"));
        return (false);
    }

    // got one
    *cp = (char)c;
    return (true);
}

/* send User-Agent to client
 */
void sendUserAgent (WiFiClient &client)
{
    StackMalloc ua_mem(200);
    char *ua = ua_mem.getMem();
    size_t ual = ua_mem.getSize();

    if (logUsageOk()) {

        uint32_t cd_timer;

        // encode full-screen with fb0
        int use_fb0 = 0;
        #if defined(_USE_FB0)
            use_fb0 = 1;
        #else
            if (getX11FullScreen())
                use_fb0 = 2;
        #endif

        // encode stopwatch if on else whether azm_on
        int main_page;
        switch (getSWDisplayState()) {
        default:
        case SWD_NONE:
            main_page = azm_on ? 1: 0;
            break;
        case SWD_MAIN:
            main_page = 2;
            break;
        case SWD_BCDIGITAL:
            main_page = 4;
            break;
        case SWD_BCANALOG:
            main_page = 3;
            break;
        }

        // alarm clock
        AlarmState as;
        uint16_t hr, mn;
        getAlarmState (as, hr, mn);

        // DOY
        uint8_t doy = 0;
        NVReadUInt8 (NV_DOY_ON, &doy);

        // encode plot options
        // prior to V2.67: value was either plot_ch or 99
        // since V2.67:    value is 100 + plot_ch
        int plotops[PANE_N];
        for (int i = 0; i < PANE_N; i++)
            plotops[i] = paneIsRotating((PlotPane)i) ? 100+(int)plot_ch[i] : (int)plot_ch[i];

        // prefix map style with N if not showing night
        char map_style[NV_MAPSTYLE_LEN+1];
        (void) getMapStyle(map_style);
        if (!night_on) {
            memmove (map_style+1, map_style, sizeof(map_style)-1);
            map_style[0] = 'N';
        }

        snprintf (ua, ual,
            _FX("User-Agent: %s/%s (id %u up %ld) crc %d LV5 %s %d %d %d %d %d %d %d %d %d %d %d %d %d %.2f %.2f %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\r\n"),
            platform, hc_version, ESP.getChipId(), getUptime(NULL,NULL,NULL,NULL), flash_crc_ok,
            map_style, main_page, mapgrid_choice, plotops[PANE_1], plotops[PANE_2], plotops[PANE_3],
            de_time_fmt, brb_mode, dx_info_for_sat, rss_on, useMetricUnits(),
            getNBMEConnected(), getKX3Baud(), found_phot, getBMETempCorr(BME_76), getBMEPresCorr(BME_76),
            desrss, dxsrss, BUILD_W, use_fb0,
            // new for LV5:
            (int)as, getCenterLng(), doy, names_on, getDemoMode(), (int)getSWEngineState(cd_timer), 
            (int)getBigClockBits(), utcOffset(), 0, 0, 0, 0);
    } else {
        snprintf (ua, ual, _FX("User-Agent: %s/%s (id %u up %ld) crc %d\r\n"),
            platform, hc_version, ESP.getChipId(), getUptime(NULL,NULL,NULL,NULL), flash_crc_ok);
    }

    // send
    client.print(ua);
}

/* issue an HTTP Get
 */
void httpGET (WiFiClient &client, const char *server, const char *page)
{
    resetWatchdog();

    FWIFIPR (client, F("GET ")); client.print(page); FWIFIPRLN (client, F(" HTTP/1.0"));
    FWIFIPR (client, F("Host: ")); client.println (server);
    sendUserAgent (client);
    FWIFIPRLN (client, F("Connection: close\r\n"));

    resetWatchdog();
}

/* given a standard 3-char abbreviation for month, set *monp to 1-12 and return true, else false
 * if nothing matches
 */
static bool crackMonth (const char *name, int *monp)
{
    for (int m = 1; m <= 12; m++) {
        if (strcmp (name, monthShortStr(m)) == 0) {
            *monp = m;
            return (true);
        }
    }

    return (false);
}

/* skip the given wifi client stream ahead to just after the first blank line, return whether ok.
 * this is often used so subsequent stop() on client doesn't slam door in client's face with RST.
 * Along the way, if lastmodp != NULL look for Last-Modified and set as a UNIX time, or 0 if not found.
 */
bool httpSkipHeader (WiFiClient &client, uint32_t *lastmodp)
{
    StackMalloc line_mem(150);
    char *line = line_mem.getMem();

    // assume no Last-Modified until found
    if (lastmodp)
        *lastmodp = 0;

    do {
        if (!getTCPLine (client, line, line_mem.getSize(), NULL))
            return (false);
        // Serial.println (line);
        
        // look for last-mod of the form: Last-Modified: Tue, 29 Sep 2020 22:55:02 GMT
        if (lastmodp) {
            char mstr[10];
            int dy, mo, yr, hr, mn, sc;
            if (sscanf (line, _FX("Last-Modified: %*[^,], %d %3s %d %d:%d:%d"), &dy, mstr, &yr, &hr, &mn, &sc)
                                                == 6 && crackMonth (mstr, &mo)) {
                tmElements_t tm;
                tm.Year = yr - 1970;
                tm.Month = mo;
                tm.Day = dy;
                tm.Hour = hr;
                tm.Minute = mn;
                tm.Second = sc;
                *lastmodp = makeTime (tm);
            }
        }

    } while (line[0] != '\0');  // getTCPLine absorbs \r\n so this tests for a blank line

    return (true);
}

/* same but when don't care about lastmod time
 */
bool httpSkipHeader (WiFiClient &client)
{
    return (httpSkipHeader (client, NULL));
}

/* retrieve and plot latest and predicted kp indices, return whether all ok
 */
static bool updateKp(SBox &box)
{
    // data are provided every 3 hours == 8/day. collect 7 days of history + 2 days of predictions
    #define     NKPPD           8                       // number of values per day
    #define     NHKPD           7                       // N historical days
    #define     NPKPD           2                       // N predicted days
    #define     NKP             ((NHKPD+NPKPD)*NKPPD)   // N total Kp values
    StackMalloc kp_mem(NKP*sizeof(float));
    float *kp = (float*)kp_mem.getMem();                // kp collection
    uint8_t kp_i = 0;                                   // next kp index to use
    char line[100];                                     // text line
    WiFiClient kp_client;                               // wifi client connection
    bool ok = false;                                    // set iff all ok


    Serial.println(kp_page);
    resetWatchdog();
    if (wifiOk() && kp_client.connect(svr_host, HTTPPORT)) {
        updateClocks(false);
        resetWatchdog();

        // query web page
        httpGET (kp_client, svr_host, kp_page);

        // skip response header
        if (!httpSkipHeader (kp_client)) {
            plotMessage (box, KP_COLOR, _FX("Kp header short"));
            goto out;
        }

        // read lines into kp array and build x
        StackMalloc kpx_mem(NKP*sizeof(float));
        float *kpx = (float *) kpx_mem.getMem();
        const int now_i = NHKPD*NKPPD-1;                        // last historic is now
        for (kp_i = 0; kp_i < NKP && getTCPLine (kp_client, line, sizeof(line), NULL); kp_i++) {
            kp[kp_i] = atof(line);
            kpx[kp_i] = (kp_i-now_i)/(float)NKPPD;
            // Serial.printf ("%2d%c: kp[%5.3f] = %g from \"%s\"\n", kp_i, kp_i == now_i ? '*' : ' ', kpx[kp_i], kp[kp_i], line);
        }

        // plot if all
        if (kp_i == NKP) {

            // save current (not last!) value for getSpaceWeather()
            kp_spw = kp[now_i];
            kp_update = now();

            // Kp value should be shown as int
            char value_str[10];
            snprintf (value_str, sizeof(value_str), "%d", (int)kp_spw);
            plotXYstr (box, kpx, kp, NKP, _FX("Days"), _FX("Planetary Kp"), KP_COLOR, 0, 9, value_str);

            ok = true;

        } else {
            Serial.printf (_FX("Kp read only %d of %d\n"), kp_i, NKP);
            plotMessage (box, KP_COLOR, _FX("Kp data short"));
        }

    } else {
        plotMessage (box, KP_COLOR, _FX("Kp connection failed"));
    }

    // clean up
out:
    kp_client.stop();
    resetWatchdog();
    printFreeHeap (F("updateKp"));
    return (ok);
}

/* given a GOES XRAY Flux value, return its event level designation in buf.
 */
static char *xrayLevel (float xray, char *buf)
{
    if (xray < 1e-8)
        strcpy (buf, _FX("A0.0"));
    else {
        static const char levels[] = "ABCMX";
        int power = floorf(log10f(xray));
        if (power > -4)
            power = -4;
        float mantissa = xray*powf(10.0F,-power);
        char alevel = levels[8+power];
        sprintf (buf, _FX("%c%.1f"), alevel, mantissa);
    }
    return (buf);
}

// retrieve and plot latest xray indices, return whether all ok
static bool updateXRay(const SBox &box)
{
    #define NXRAY 150                                   // n lines to collect = 25 hours @ 10 mins per line
    StackMalloc lxray_mem(NXRAY*sizeof(float));
    StackMalloc sxray_mem(NXRAY*sizeof(float));
    StackMalloc x_mem(NXRAY*sizeof(float));
    float *lxray = (float *) lxray_mem.getMem();        // long wavelength values
    float *sxray = (float *) sxray_mem.getMem();        // short wavelength values
    float *x = (float *) x_mem.getMem();                // x coords of plot
    uint8_t xray_i;                                     // next index to use
    WiFiClient xray_client;
    char line[100];
    uint16_t ll;
    bool ok = false;


    Serial.println(xray_page);
    resetWatchdog();
    if (wifiOk() && xray_client.connect(svr_host, HTTPPORT)) {
        updateClocks(false);

        // query web page
        httpGET (xray_client, svr_host, xray_page);

        // soak up remaining header
        if (!httpSkipHeader (xray_client)) {
            plotMessage (box, XRAY_LCOLOR, _FX("XRay header short"));
            goto out;
        }

        // collect content lines and extract both wavelength intensities
        xray_i = 0;
        float current_xray = 1;
        while (xray_i < NXRAY && getTCPLine (xray_client, line, sizeof(line), &ll)) {
            // Serial.println(line);
            if (line[0] == '2' && ll >= 56) {
                float s = atof(line+35);
                if (s <= 0)                     // missing values are set to -1.00e+05, also guard 0
                    s = 1e-9;
                sxray[xray_i] = log10f(s);
                float l = atof(line+47);
                if (l <= 0)                     // missing values are set to -1.00e+05, also guard 0
                    l = 1e-9;
                lxray[xray_i] = log10f(l);
                // Serial.print(l); Serial.print('\t'); Serial.println (s);
                // capture newest for display
                if (++xray_i == NXRAY)
                    current_xray = l;
            }
        }

        // proceed iff we found all
        if (xray_i == NXRAY) {
            resetWatchdog();

            // create x in hours back from 0
            for (int16_t i = 0; i < NXRAY; i++)
                x[i] = (i-NXRAY)/6.0;           // 6 entries per hour

            // overlay short over long
            ok = plotXYstr (box, x, lxray, NXRAY, _FX("Hours"), _FX("GOES 16 X-Ray"), XRAY_LCOLOR,
                                -9, -2, xrayLevel(current_xray, line))
                 && plotXY (box, x, sxray, NXRAY, NULL, NULL, XRAY_SCOLOR, -9, -2, 0.0);

            if (ok) {
                // capture
                xray_spw = current_xray;
                xray_update = now();
            }

        } else {
            Serial.printf (_FX("XRay only read %d of %d\n"), xray_i, NXRAY);
            plotMessage (box, XRAY_LCOLOR, _FX("XRay data short"));
        }
    } else {
        plotMessage (box, XRAY_LCOLOR, _FX("X-Ray connection failed"));
    }

out:

    xray_client.stop();
    resetWatchdog();
    printFreeHeap (F("updateXRay"));
    return (ok);
}


// retrieve and plot latest sun spot indices, return whether all ok
static bool updateSunSpots (const SBox &box)
{
    #define NSUNSPOT 31                         // go back 30 days, including 0
    StackMalloc x_sspot((NSUNSPOT)*sizeof(float));
    StackMalloc x_x((NSUNSPOT)*sizeof(float));
    StackMalloc x_line(100);
    float *sspot = (float*)x_sspot.getMem();
    float *x = (float*)x_x.getMem();
    char *line = x_line.getMem();
    WiFiClient ss_client;
    bool ok = false;


    Serial.println(sspot_page);
    resetWatchdog();
    if (wifiOk() && ss_client.connect(svr_host, HTTPPORT)) {
        updateClocks(false);

        // query web page
        httpGET (ss_client, svr_host, sspot_page);

        // skip response header
        if (!httpSkipHeader (ss_client)) {
            plotMessage (box, SSPOT_COLOR, _FX("Sunspot header short"));
            goto out;
        }

        // read lines into sspot array and build corresponding time value
        int8_t ssn_i;
        for (ssn_i = 0; ssn_i < NSUNSPOT && getTCPLine (ss_client, line, x_line.getSize(), NULL); ssn_i++) {
            // Serial.print(ssn_i); Serial.print("\t"); Serial.println(line);
            sspot[ssn_i] = atof(line+11);
            x[ssn_i] = 1-NSUNSPOT + ssn_i;
        }

        // plot if found all, display current value
        updateClocks(false);
        resetWatchdog();
        if (ssn_i == NSUNSPOT) {
            ok = plotXY (box, x, sspot, NSUNSPOT, _FX("Days"), _FX("Sunspot Number"),
                                        SSPOT_COLOR, 0, -1, sspot[NSUNSPOT-1]);
            if (ok) {
                // capture
                ssn_spw = sspot[NSUNSPOT-1];
                ssn_update = now();
            }
        }

    } else {
        plotMessage (box, SSPOT_COLOR, _FX("SSN connection failed"));
    }

    // clean up
out:
    ss_client.stop();
    resetWatchdog();
    printFreeHeap (F("updateSunspots"));
    return (ok);
}

/* retrieve and plot latest and predicted solar flux indices, return whether all ok.
 */
static bool updateSolarFlux(const SBox &box)
{
    // collect lines, three per day for 33 days
    #define     NSFLUX          99
    StackMalloc x_mem(NSFLUX*sizeof(float));
    StackMalloc flux_mem(NSFLUX*sizeof(float));
    StackMalloc line_mem(120);
    float *x = (float *) x_mem.getMem();
    float *flux = (float *) flux_mem.getMem();
    char *line = line_mem.getMem();
    WiFiClient sf_client;
    bool ok = false;


    Serial.println (sf_page);
    resetWatchdog();
    if (wifiOk() && sf_client.connect(svr_host, HTTPPORT)) {
        updateClocks(false);
        resetWatchdog();

        // query web page
        httpGET (sf_client, svr_host, sf_page);

        // skip response header
        if (!httpSkipHeader (sf_client)) {
            plotMessage (box, FLUX_COLOR, _FX("Flux header short"));
            goto out;
        }

        // read lines into flux array and build corresponding time value
        int8_t flux_i;
        for (flux_i = 0; flux_i < NSFLUX && getTCPLine (sf_client, line, line_mem.getSize(), NULL);flux_i++) {
            // Serial.print(flux_i); Serial.print("\t"); Serial.println(line);
            flux[flux_i] = atof(line);
            x[flux_i] = (flux_i - (NSFLUX-9-1))/3.0F;   // 3x(30 days history + 3 days predictions)
        }

        // plot if found all, display and capture current value
        updateClocks(false);
        resetWatchdog();
        if (flux_i == NSFLUX) {
            if (plotXY (box, x, flux, NSFLUX, _FX("Days"), _FX("10.7 cm Solar flux"),
                                                FLUX_COLOR, 0, 0, flux[NSFLUX-10])) {
                flux_spw = flux[NSFLUX-10];
                flux_update = now();
                ok = true;
            }
        } else
            plotMessage (box, FLUX_COLOR, _FX("Flux data missing"));

    } else {
        plotMessage (box, FLUX_COLOR, _FX("Flux connection failed"));
    }

    // clean up
out:
    sf_client.stop();
    resetWatchdog();
    printFreeHeap (F("updateSolarFlux"));
    return (ok);
}

/* retrieve and plot latest and predicted solar wind indices, return whether all ok.
 */
static bool updateSolarWind(const SBox &box)
{
    // max lines to collect, oldest first
    #define     SOLWINDDT       (10*60)         // interval, secs
    #define     SOLWINDP        (24*3600)       // period, secs
    #define     NSOLWIND        (SOLWINDP/SOLWINDDT)
    StackMalloc x_mem(NSOLWIND*sizeof(float));  // hours ago 
    StackMalloc y_mem(NSOLWIND*sizeof(float));  // wind
    float *x = (float *) x_mem.getMem();
    float *y = (float *) y_mem.getMem();
    WiFiClient swind_client;
    char line[80];
    bool ok = false;


    Serial.println (swind_page);
    resetWatchdog();
    if (wifiOk() && swind_client.connect(svr_host, HTTPPORT)) {
        updateClocks(false);
        resetWatchdog();

        // query web page
        httpGET (swind_client, svr_host, swind_page);

        // skip response header
        if (!httpSkipHeader (swind_client)) {
            plotMessage (box, SWIND_COLOR, _FX("Wind header short"));
            goto out;
        }

        // read lines into wind array and build corresponding x/y values
        time_t t0 = now();
        time_t start_t = t0 - SOLWINDP;
        time_t prev_unixs = 0;
        float max_y = 0;
        int nsw;
        for (nsw = 0; nsw < NSOLWIND && getTCPLine (swind_client, line, sizeof(line), NULL); ) {
            // Serial.printf (_FX("Swind %3d: %s\n"), nsw, line);
            long unixs;         // unix seconds
            float density;      // /cm^2
            float speed;        // km/s
            if (sscanf (line, "%ld %f %f", &unixs, &density, &speed) != 3) {
                plotMessage (box, SWIND_COLOR, _FX("Wind data garbled"));
                goto out;
            }

            // want y axis to be 10^12 /s /m^2
            float this_y = density * speed * 1e-3;

            // capture largest value in this period
            if (this_y > max_y)
                max_y = this_y;

            // skip until find within period and new interval or always included last
            if ((unixs < start_t || unixs - prev_unixs < SOLWINDDT) && nsw != NSOLWIND-1)
                continue;
            prev_unixs = unixs;

            // want x axis to be hours back from now
            x[nsw] = (t0 - unixs)/(-3600.0F);
            y[nsw] = max_y;
            // Serial.printf (_FX("Swind %3d %5.2f %5.2f\n"), nsw, x[nsw], y[nsw]);

            // good one
            max_y = 0;
            nsw++;
        }

        // plot if found at least a few
        updateClocks(false);
        resetWatchdog();
        if (nsw >= 10) {
            if (plotXY (box, x, y, nsw, _FX("Hours"), _FX("Solar wind"), SWIND_COLOR, 0, 0, y[nsw-1])) {
                swind_spw = y[nsw-1];
                swind_update = t0;
                ok = true;
            }
        } else
            plotMessage (box, SWIND_COLOR, _FX("Wind data missing"));

    } else {
        plotMessage (box, SWIND_COLOR, _FX("Wind connection failed"));
    }

    // clean up
out:
    swind_client.stop();
    resetWatchdog();
    printFreeHeap (F("updateSolarWind"));
    return (ok);
}

/* retrieve and plot latest DRAP frequencies, return whether all ok.
 */
static bool updateDRAPPlot(const SBox &box)
{
    // collect 24 hours of stats no closer than 5 minutes apart
    #define     DRAPDT          (10*60)                 // interval, seconds
    #define     NPLOTDRAP       (86400/DRAPDT)          // max number of values
    #define     MAXDRAPAGE      3600                    // must have data this new, secs
    StackMalloc x_mem(NPLOTDRAP*sizeof(float));         // hours ago 
    StackMalloc y_mem(NPLOTDRAP*sizeof(float));         // drap MHz
    float *x = (float *) x_mem.getMem();
    float *y = (float *) y_mem.getMem();
    WiFiClient drap_client;
    char line[80];
    bool ok = false;

    Serial.println (drap_page);
    resetWatchdog();
    if (wifiOk() && drap_client.connect(svr_host, HTTPPORT)) {
        updateClocks(false);
        resetWatchdog();

        // query web page
        httpGET (drap_client, svr_host, drap_page);

        // skip response header
        if (!httpSkipHeader (drap_client)) {
            plotMessage (box, DRAPPLOT_COLOR, _FX("DRAP short"));
            goto out;
        }

        // read lines and build corresponding x/y values
        time_t t0 = now();
        time_t oldest_drap = t0 - NPLOTDRAP*DRAPDT;
        time_t prev_unix = 0;
        float max_drapdt = 0;
        long unixs;
        float x_val = 0, min, max, mean;
        int ndrap = 0;
        while (getTCPLine (drap_client, line, sizeof(line), NULL)) {
            if (sscanf (line, "%ld : %f %f %f", &unixs, &min, &max, &mean) != 4) {
                plotMessage (box, DRAPPLOT_COLOR, _FX("DRAP data garbled"));
                goto out;
            }

            // hours ago
            x_val = (unixs - t0)/3600.0F;;

            // accept only if within current period and not same interval
            if (ndrap < NPLOTDRAP && unixs >= oldest_drap) {
                // capture max value in this interval
                if (max > max_drapdt)
                    max_drapdt = max;
                if (unixs - prev_unix >= DRAPDT) {
                    prev_unix = unixs;
                    x[ndrap] = x_val;
                    y[ndrap] = max_drapdt;
                    ndrap++;
                    max_drapdt = 0;
                    // Serial.printf (_FX("DRAP %3d: %g\n"), ndrap, max_drapdt);
                }
            }
        }

        // always capture latest as last entry
        if (ndrap == NPLOTDRAP)
            ndrap -= 1;
        x[ndrap] = x_val;
        y[ndrap] = max;
        ndrap++;

        // plot if found some recent
        updateClocks(false);
        resetWatchdog();
        if (ndrap > 10 && t0 - unixs < MAXDRAPAGE) {

            // always include the latest raw value even if must overwrite existing entry
            if (ndrap < NPLOTDRAP) {
                x[ndrap] = x_val;
                y[ndrap] = max;
                ndrap++;
            } else {
                x[NPLOTDRAP-1] = x_val;
                y[NPLOTDRAP-1] = max;
            }
            Serial.printf (_FX("DRAP found %d of %d, newest %g is %u s old\n"),
                        ndrap, NPLOTDRAP, max, t0 - unixs);

            if (plotXY (box,x,y,ndrap,_FX("Hours"),_FX("DRAP, max MHz"),DRAPPLOT_COLOR,0,0,y[ndrap-1])) {
                drap_spw = y[ndrap-1];
                drap_update = t0;
                ok = true;
            }
        } else {
            plotMessage (box, DRAPPLOT_COLOR, _FX("DRAP data missing"));
            Serial.printf (_FX("DRAP missing: found %d / %d, dt %ld / %d s\n"),
                        ndrap, NPLOTDRAP, t0 - unixs, MAXDRAPAGE);
        }

    } else {
        plotMessage (box, DRAPPLOT_COLOR, _FX("DRAP connection failed"));
    }

    // clean up
out:
    drap_client.stop();
    resetWatchdog();
    printFreeHeap (F("updateDRAPPlot"));
    return (ok);
}

/* retrieve and draw latest band conditions in the given box, return whether all ok.
 * N.B. reset bc_reverting
 */
static bool updateBandConditions(const SBox &box)
{
    StackMalloc response_mem(100);
    StackMalloc config_mem(100);
    char *response = (char *) response_mem.getMem();
    char *config = (char *) config_mem.getMem();
    WiFiClient bc_client;
    bool ok = false;


    // build query
    const size_t qsize = sizeof(bc_page)+200;
    StackMalloc query_mem (qsize);
    char *query = (char *) query_mem.getMem();
    time_t t = nowWO();
    snprintf (query, qsize,
                _FX("%s?YEAR=%d&MONTH=%d&RXLAT=%.3f&RXLNG=%.3f&TXLAT=%.3f&TXLNG=%.3f&UTC=%d&PATH=%d&POW=%d"),
                bc_page, year(t), month(t), dx_ll.lat_d, dx_ll.lng_d, de_ll.lat_d, de_ll.lng_d,
                hour(t), show_lp, bc_power);

    Serial.println (query);
    resetWatchdog();
    if (wifiOk() && bc_client.connect(svr_host, HTTPPORT)) {
        updateClocks(false);
        resetWatchdog();

        // query web page
        httpGET (bc_client, svr_host, query);

        // skip header
        if (!httpSkipHeader (bc_client)) {
            plotMessage (box, RA8875_RED, _FX("No BC header"));
            goto out;
        }

        // next line is CSV path reliability for the requested time between DX and DE, 9 bands 80-10m
        if (!getTCPLine (bc_client, response, response_mem.getSize(), NULL)) {
            plotMessage (box, RA8875_RED, _FX("No BC response"));
            goto out;
        }

        // next line is configuration summary
        if (!getTCPLine (bc_client, config, config_mem.getSize(), NULL)) {
            Serial.println(response);
            plotMessage (box, RA8875_RED, _FX("No BC config"));
            goto out;
        }

        Serial.printf (_FX("BC response: %s\n"), response);
        Serial.printf (_FX("BC config: %s\n"), config);

        // keep time fresh
        updateClocks(false);
        resetWatchdog();

        // get current utc hour for path_spw
        int t_hr = hour(t);

        // plot or show error
        // next 24 lines are reliability matrix.
        // N.B. col 1 is UTC but runs from 1 .. 24, 24 is really 0
        // lines include data for 9 bands, 80-10, but we drop 60 for BandMatrix
        float rel[PROP_MAP_N];          // value are path reliability 0 .. 1
        BandMatrix bm;
        for (int i = 0; i < BMTRX_ROWS; i++) {

            // read next row
            if (!getTCPLine (bc_client, response, response_mem.getSize(), NULL)) {
                Serial.println(response);
                plotMessage (box, RA8875_RED, _FX("No matrix"));
                goto out;
            }

            // crack next row, skipping 60 m
            int utc_hr;
            if (sscanf(response, _FX("%d %f,%*f,%f,%f,%f,%f,%f,%f,%f"), &utc_hr,
                        &rel[PROP_MAP_80M], &rel[PROP_MAP_40M], &rel[PROP_MAP_30M], &rel[PROP_MAP_20M],
                        &rel[PROP_MAP_17M], &rel[PROP_MAP_15M], &rel[PROP_MAP_12M], &rel[PROP_MAP_10M])
                            != BMTRX_COLS + 1) {
                Serial.println(response);
                plotMessage (box, RA8875_RED, _FX("Bad matrix"));
                goto out;
            }

            // correct utc
            utc_hr %= 24;

            // add to bm as integer percent
            for (int j = 0; j < BMTRX_COLS; j++)
                bm[utc_hr][j] = 100*rel[j];

            // copy to path_spw if correct time
            if (utc_hr == t_hr) {
                memcpy (path_spw, rel, PROP_MAP_N*sizeof(float));
                path_update = now();
            }
        }

        // #define _TEST_BAND_MATRIX
        #if defined(_TEST_BAND_MATRIX)
            for (int r = 0; r < BMTRX_ROWS; r++)                    // time 0 .. 23
                for (int c = 0; c < BMTRX_COLS; c++)                // band 80 .. 10
                    bm[r][c] = 100*r*c/BMTRX_ROWS/BMTRX_COLS;
                    // (*mp)[r][c] = (float)r/BMTRX_ROWS;
        #endif

        // ok!
        plotBandConditions (box, 0, &bm, config);
        ok = true;

    } else {
        plotMessage (box, RA8875_RED, _FX("VOACAP connection failed"));
    }

    // clean up
out:
    bc_reverting = false;
    bc_client.stop();
    resetWatchdog();
    printFreeHeap (F("updateBandConditions"));
    return (ok);
}

/* read given SDO image choice and display in the given box
 */
static bool updateSDO (const SBox &box, PlotChoice ch)
{
    // choose file
    const char *sdo_fn;
    switch (ch) {
    case PLOT_CH_SDO_1: sdo_fn = sdo_filename[0]; break;
    case PLOT_CH_SDO_2: sdo_fn = sdo_filename[1]; break;
    case PLOT_CH_SDO_3: sdo_fn = sdo_filename[2]; break;
    case PLOT_CH_SDO_4: sdo_fn = sdo_filename[3]; break;
    default:
        fatalError (_FX("Bug! updateSDO() bad choice: %d"), (int)ch);
        return (false);
    }

    bool ok = drawHTTPBMP (sdo_fn, box, SDO_COLOR);

    printFreeHeap(F("updateSDO"));
    return (ok);
}

/* read STEREO image and display in the given box
 */
static bool updateSTEREO_A (const SBox &box)
{
    WiFiClient client;
    bool sep_ok = false;
    bool file_ok = false;

    // get separation 
    float sep = 0;
    Serial.println(stereo_a_sep_page);
    resetWatchdog();
    if (wifiOk() && client.connect(svr_host, HTTPPORT)) {
        updateClocks(false);

        // query and skip header
        httpGET (client, svr_host, stereo_a_sep_page);

        char buf[20];
        if (httpSkipHeader(client) && getTCPLine (client, buf, sizeof(buf), NULL)) {
            sep = atof (buf);
            Serial.printf (_FX("STEREO_A ahead %g\n"), sep);
            sep_ok = true;
        } else {
            plotMessage (box, STEREO_A_COLOR, _FX("ahead failed"));
        }

        client.stop();
    }

    // read and display image if sep ok
    if (sep_ok)
        file_ok = drawHTTPBMP (stereo_a_img_page, box, STEREO_A_COLOR);

    // overlay rotation terminator
    if (file_ok) {
        #define SASEP_NSEGS 30          // number of line segments
        #define SASEP_COLOR RA8875_BLACK
        const float csep = cosf (deg2rad(sep));                 // cos angle past limb
        const uint16_t img_w = box.w - 10;                      // apparent image width
        const uint16_t img_r2 = img_w*img_w/4;                  // " sqr radius
        uint16_t prev_x = 0, prev_y = 0;
        bool prev_ok = false;
        for (int i = 0; i <= SASEP_NSEGS; i++) {                // inclusive
            int dy = img_w/2 - i*img_w/SASEP_NSEGS;             // pixels up from center
            if (dy < -box.h/2 || dy > box.h/2)                  // box is wider than high
                continue;
            int dx = csep * sqrtf (img_r2 - dy*dy);             // pixels right from center
            uint16_t y = box.y + box.h/2 - dy;
            uint16_t x = box.x + box.w/2 - dx;
            if (prev_ok)
                tft.drawLine (prev_x, prev_y, x, y, SASEP_COLOR);
            prev_x = x;
            prev_y = y;
            prev_ok = true;
        }

        // helpful arrow?
        #define SASEP_ARROWH 5          // half-height
        #define SASEP_ARROWL 25         // length
        #define SASEP_ARROWB 9          // back from tip
        #define SASEP_ARROWX0 (box.x+box.w/2-SASEP_ARROWL/2)
        #define SASEP_ARROWY0 (box.y+box.h/2)
        tft.drawLine (SASEP_ARROWX0, SASEP_ARROWY0, SASEP_ARROWX0+SASEP_ARROWL, SASEP_ARROWY0, SASEP_COLOR);
        tft.drawLine (SASEP_ARROWX0+SASEP_ARROWL, SASEP_ARROWY0, SASEP_ARROWX0+SASEP_ARROWL-SASEP_ARROWB,
                SASEP_ARROWY0-SASEP_ARROWH, SASEP_COLOR);
        tft.drawLine (SASEP_ARROWX0+SASEP_ARROWL, SASEP_ARROWY0, SASEP_ARROWX0+SASEP_ARROWL-SASEP_ARROWB,
                SASEP_ARROWY0+SASEP_ARROWH, SASEP_COLOR);
    }

    printFreeHeap(F("updateSTEREO_A"));
    return (sep_ok && file_ok);
}

/* get next line from client in line[] then return true, else nothing and return false.
 * line[] will have \r and \n removed and end with \0, optional line length in *ll will not include \0.
 * if line is longer than line_len it will be silently truncated.
 */
bool getTCPLine (WiFiClient &client, char line[], uint16_t line_len, uint16_t *ll)
{
    // decrement available length so there's always room to add '\0'
    line_len -= 1;

    // read until find \n or time out.
    uint16_t i = 0;
    while (true) {
        char c;
        if (!getChar (client, &c))
            return (false);
        if (c == '\r')
            continue;
        if (c == '\n') {
            line[i] = '\0';
            if (ll)
                *ll = i;
            // Serial.println(line);
            return (true);
        } else if (i < line_len)
            line[i++] = c;
    }
}

/* convert an array of 4 big-endian network-order bytes into a uint32_t
 */
static uint32_t crackBE32 (uint8_t bp[])
{
    union {
        uint32_t be;
        uint8_t ba[4];
    } be4;

    be4.ba[3] = bp[0];
    be4.ba[2] = bp[1];
    be4.ba[1] = bp[2];
    be4.ba[0] = bp[3];

    return (be4.be);
}

/* called when RSS has just been turned on: update now and restart refresh cycle
 */
void scheduleRSSNow()
{
    next_rss = 0;
}

/* display next RSS feed item if on, return whether ok
 */
static bool updateRSS ()
{
    // persistent list of malloced titles
    static char *titles[NRSS];
    static uint8_t n_titles, title_i;

    // skip and clear cache if off
    if (!rss_on) {
        while (n_titles > 0) {
            free (titles[--n_titles]);
            titles[n_titles] = NULL;
        }
        return (true);
    }

    // reserve mem
    StackMalloc line_mem(150);
    char *line = line_mem.getMem();

    // prepare background
    tft.fillRect (rss_bnr_b.x, rss_bnr_b.y, rss_bnr_b.w, rss_bnr_b.h, RSS_BG_COLOR);
    tft.drawLine (rss_bnr_b.x, rss_bnr_b.y, rss_bnr_b.x+rss_bnr_b.w, rss_bnr_b.y, GRAY);

    // fill titles[] if empty
    if (title_i >= n_titles) {

        // reset count and index
        n_titles = title_i = 0;

        // TCP client
        WiFiClient rss_client;
        
        Serial.println(rss_page);
        resetWatchdog();
        if (wifiOk() && rss_client.connect(svr_host, HTTPPORT)) {

            resetWatchdog();
            updateClocks(false);

            // fetch feed page
            httpGET (rss_client, svr_host, rss_page);

            // skip response header
            if (!httpSkipHeader (rss_client)) {
                Serial.println (F("RSS header short"));
                goto out;
            }

            // get up to NRSS more titles[]
            for (n_titles = 0; n_titles < NRSS; n_titles++) {
                if (!getTCPLine (rss_client, line, line_mem.getSize(), NULL))
                    goto out;
                if (titles[n_titles])
                    free (titles[n_titles]);
                titles[n_titles] = strdup (line);
                // Serial.printf (_FX("RSS[%d] len= %d\n"), n_titles, strlen(titles[n_titles]));
            }
        }

      out:
        rss_client.stop();

        // real trouble if still no titles
        if (n_titles == 0) {
            // report error 
            selectFontStyle (LIGHT_FONT, SMALL_FONT);
            tft.setTextColor (RSS_FG_COLOR);
            tft.setCursor (rss_bnr_b.x + rss_bnr_b.w/2-100, rss_bnr_b.y + 2*rss_bnr_b.h/3-1);
            tft.print (F("RSS network error"));
            Serial.println (F("RSS failed"));
            return (false);
        }
        printFreeHeap (F("updateRSS"));
    }

    // draw next title
    char *title = titles[title_i];
    size_t ll = strlen(title);

    // usable banner drawing x and width
    uint16_t ubx = rss_bnr_b.x + 5;
    uint16_t ubw = rss_bnr_b.w - 10;

    resetWatchdog();

    // find pixel width of title
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (RSS_FG_COLOR);
    uint16_t bw = getTextWidth (title);

    // draw as 1 or 2 lines to fit within ubw
    if (bw < ubw) {
        // title fits on one row, draw centered horizontally and vertically
        tft.setCursor (ubx + (ubw-bw)/2, rss_bnr_b.y + 2*rss_bnr_b.h/3-1);
        tft.print (title);
    } else {
        // title too long, split near center
        char *row2 = strchr (title+ll/2, ' ');
        if (!row2)
            row2 = title+ll/2;          // no blanks! just split in half?
        *row2++ = '\0';                 // replace with EOS and move to start of row 2
        uint16_t r1w, r2w;              // row 1 and 2 pixel widths
        r1w = getTextWidth (title);
        r2w = getTextWidth (row2);

        // draw if fits
        if (r1w <= ubw && r2w <= ubw) {
            tft.setCursor (ubx + (ubw-r1w)/2, rss_bnr_b.y + rss_bnr_b.h/2 - 8);
            tft.print (title);
            tft.setCursor (ubx + (ubw-r2w)/2, rss_bnr_b.y + rss_bnr_b.h - 9);
            tft.print (row2);
        } else {
            Serial.printf (_FX("RSS not fit: '%s' '%s'\n"), title, row2);
        }
    }

    // remove from list and advance to next title
    free (titles[title_i]);
    titles[title_i++] = NULL;
 
    // too often: printFreeHeap (F("updateRSS"));
    resetWatchdog();
    return (true);
}

/* display the RSG NOAA solar environment scale values.
 */
static bool updateNOAASWx(const SBox &box)
{
    // expecting 3 reply lines of the form, anything else is an error message
    //  R  0 0 0 0
    //  S  0 0 0 0
    //  G  0 0 0 0
    char line[100];
    bool ok = false;

    // TCP client
    WiFiClient noaaswx_client;

    // starting msg
        
    // read scales
    Serial.println(noaaswx_page);
    resetWatchdog();
    if (wifiOk() && noaaswx_client.connect(svr_host, HTTPPORT)) {

        resetWatchdog();
        updateClocks(false);

        // fetch page
        httpGET (noaaswx_client, svr_host, noaaswx_page);

        // skip header then read the data lines
        if (httpSkipHeader (noaaswx_client)) {

            for (int i = 0; i < N_NOAASW_C; i++) {

                // read next line
                if (!getTCPLine (noaaswx_client, line, sizeof(line), NULL)) {
                    plotMessage (box, RA8875_RED, _FX("NOAASW missing data"));
                    goto out;
                }
                // Serial.printf ("NOAA: %d %s\n", i, line);

                // parse
                // sprintf (line, "%c 1 2 3 4", 'A'+i);  // test line
                if (sscanf (line, "%c %d %d %d %d", &noaa_spw.cat[i], &noaa_spw.val[i][0],
                                &noaa_spw.val[i][1], &noaa_spw.val[i][2], &noaa_spw.val[i][3]) != 5) {
                    plotMessage (box, RA8875_RED, line);
                    goto out;
                }
            }

            // all ok, display
            noaa_update = now();
            plotNOAASWx (box, noaa_spw);
            ok = true;

        } else {
            plotMessage (box, RA8875_RED, _FX("NOAASW header short"));
            goto out;
        }
    } else
        plotMessage (box, RA8875_RED, _FX("NOAASW connection failed"));

out:

    // finished with connection
    noaaswx_client.stop();

    printFreeHeap (F("updateNOAASWx"));
    return (ok);
}

/* it is MUCH faster to print F() strings in a String than using them directly.
 * see esp8266/2.3.0/cores/esp8266/Print.cpp::print(const __FlashStringHelper *ifsh) to see why.
 */
void FWIFIPR (WiFiClient &client, const __FlashStringHelper *str)
{
    String _sp(str);
    client.print(_sp);
}

void FWIFIPRLN (WiFiClient &client, const __FlashStringHelper *str)
{
    String _sp(str);
    client.println(_sp);
}

// handy wifi health check
bool wifiOk()
{
    if (WiFi.status() == WL_CONNECTED)
        return (true);

    // retry occasionally
    static uint32_t last_wifi;
    if (timesUp (&last_wifi, WIFI_RETRY*1000)) {
        initWiFi(false);
        return (WiFi.status() == WL_CONNECTED);
    } else
        return (false);
}

/* reset the wifi retry flags so all that are in use are updated
 */
void initWiFiRetry()
{
    next_flux = 0;
    next_ssn = 0;
    next_xray = 0;
    next_kp = 0;
    next_rss = 0;
    next_sdo_1 = 0;
    next_sdo_2 = 0;
    next_sdo_3 = 0;
    next_sdo_4 = 0;
    next_noaaswx = 0;
    next_dewx = 0;
    next_dxwx = 0;
    next_bc = 0;
    next_moon = 0;
    next_gimbal = 0;
    next_dxcluster = 0;
    next_bme280_t = 0;
    next_bme280_p = 0;
    next_bme280_d = 0;
    next_bme280_h = 0;
    next_swind = 0;
    next_drap = 0;
    next_stereo_a = 0;

    // map is in memory
    // next_map = 0;
}

/* called to schedule an update to the band conditions pane if up.
 * if BC is on PANE_1 wait for a revert in progress otherwie update immediately.
 */
void newBC()
{
    PlotPane bc_pp = findPaneChoiceNow (PLOT_CH_BC);
    if (bc_pp != PANE_NONE && (bc_pp != PANE_1 || !bc_reverting))
        next_bc = 0;
}

/* called to schedule an immediate update of the given VOACAP map, unless being turned off.
 */
void newVOACAPMap(PropMapSetting pm)
{
    prop_map = pm;
    if (pm != PROP_MAP_OFF)
        next_map = 0;
}

/* called to schedule an immediate update of the give core map, unless being turned off
 */
void newCoreMap(CoreMaps cm)
{
    core_map = cm;
    if (cm != CM_NONE)
        next_map = 0;
}

/* display the current DE weather.
 * if already assigned to any pane just update now, else show in PANE_1 then arrange to linger
 */
void showDEWX()
{
    PlotPane dewx_pp = findPaneChoiceNow (PLOT_CH_DEWX);
    if (dewx_pp != PANE_NONE)
        next_dewx = 0;
    else {
        // revert whether worked or not
        (void) updateDEWX (plot_b[PANE_1]);
        revertPlot1 (DXPATH_LINGER);
    }
}

/* display the current DX weather.
 * if already assigned to any pane just update now, else show in PANE_1 then arrange to linger
 */
void showDXWX()
{
    PlotPane dxwx_pp = findPaneChoiceNow (PLOT_CH_DXWX);
    if (dxwx_pp != PANE_NONE)
        next_dxwx = 0;
    else {
        // revert whether worked or not
        (void) updateDXWX (plot_b[PANE_1]);
        revertPlot1 (DXPATH_LINGER);
    }
}

/* return most recent space weather info and its age. values never read will be zero and ancient.
 * most are value+age but xray is a string and pathrel is an array of PROP_MAP_N.
 */
void getSpaceWeather (SPWxValue &ssn, SPWxValue &flux, SPWxValue &kp, SPWxValue &swind, SPWxValue &drap,
NOAASpaceWx &noaaspw, time_t &noaaspw_age, char xray[], time_t &xray_age,
float pathrel[PROP_MAP_N], time_t &pathrel_age)
{
    // time now for ages
    time_t t0 = now();

    // these are easy scalars
    ssn.value = ssn_spw;
    ssn.age = t0 - ssn_update;
    flux.value = flux_spw;
    flux.age = t0 - flux_update;
    kp.value = kp_spw;
    kp.age = t0 - kp_update;
    swind.value = swind_spw;
    swind.age = t0 - swind_update;
    drap.value = drap_spw;
    drap.age = t0 - drap_update;

    // easy struct but beware never set yet
    if (!noaa_spw.cat[0]) {
        noaa_spw.cat[0] = 'R';
        noaa_spw.cat[1] = 'S';
        noaa_spw.cat[2] = 'G';
    }
    noaaspw = noaa_spw;
    noaaspw_age = t0 - noaa_update;

    // xray is a string
    (void) xrayLevel (xray_spw, xray);  // handles 0 ok
    xray_age = t0 - xray_update;

    // VOACAP path reliability is an array
    for (int i = 0; i < PROP_MAP_N; i++)
        pathrel[i] = path_spw[i];
    pathrel_age = t0 - path_update;
}

/* return current NTP response time list.
 * N.B. this is the real data, caller must not modify.
 */
int getNTPServers (const NTPServer **listp)
{
    *listp = ntp_list;
    return (N_NTP);
}
