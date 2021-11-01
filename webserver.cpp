/* service the external web server or internal demo.
 *
 */

#include "HamClock.h"



// platform
#if defined (_IS_ESP8266)
const char platform[] = "ESPHamClock";
#elif defined(_IS_LINUX_RPI)
const char platform[] = "HamClock-rpi";
#elif defined(_IS_LINUX)
const char platform[] = "HamClock-linux";
#elif defined (__APPLE__)
const char platform[] = "HamClock-apple";
#elif defined (_IS_FREEBSD)
const char platform[] = "HamClock-FreeBSD";
#else
const char platform[] = "HamClock-UNIX";
#endif


// persistent server for listening for remote connections
static WiFiServer *remoteServer;

// handy default messages
static const char garbcmd[] = "Garbled command";
static const char notsupp[] = "Not supported";

/* replace all "%20" with blank, IN PLACE
 */
static void replaceBlankEntity (char *from)
{
    char *to = from;
    while (*from) {
        if (strncmp (from, "%20", 3) == 0) {
            *to++ = ' ';
            from += 3;
        } else
            *to++ = *from++;
    }
    *to = '\0';
}

/* send initial response indicating body will be plain text
 */
static void startPlainText (WiFiClient client)
{
    resetWatchdog();

    FWIFIPRLN (client, F("HTTP/1.0 200 OK"));
    sendUserAgent (client);
    FWIFIPRLN (client, F("Content-Type: text/plain; charset=us-ascii"));
    FWIFIPRLN (client, F("Connection: close\r\n"));             // include extra blank line

    resetWatchdog();
}

/* send the given message as HTTP error 400 Bad request.
 */
static void sendHTTPError (WiFiClient client, const char *errmsg)
{
    resetWatchdog();

    // preserve locally
    Serial.println (errmsg);

    // send to client
    FWIFIPRLN (client, F("HTTP/1.0 400 Bad request"));
    sendUserAgent (client);
    FWIFIPRLN (client, F("Content-Type: text/plain; charset=us-ascii"));
    FWIFIPRLN (client, F("Connection: close\r\n"));             // include extra blank line
    client.println (errmsg);

    resetWatchdog();
}

/* report all choices for the given pane to client
 */
static void reportPaneChoices (WiFiClient *clientp, PlotPane pp)
{
    // which pane
    char buf[50];
    snprintf (buf, sizeof(buf), "Pane%d     ", (int)pp+1);
    clientp->print(buf);

    // always start with current then any others in rotset
    PlotChoice pc_now = plot_ch[pp];
    clientp->print(plot_names[pc_now]);
    if (pc_now == PLOT_CH_BC) {
        snprintf (buf, sizeof(buf), "/%dW", bc_power);
        clientp->print(buf);
    }
    if (paneIsRotating(pp)) {
        time_t t0 = now();
        PlotChoice pc_next = pc_now;
        while ((pc_next = getNextRotationChoice (pp, pc_next)) != pc_now) {
            size_t l = snprintf (buf, sizeof(buf), ",%s", plot_names[pc_next]);
            if (pc_next == PLOT_CH_BC)
                snprintf (buf+l, sizeof(buf)-l, "/%dW", bc_power);
            clientp->print(buf);
        }
        int sleft = next_rotationT[pp] - t0;
        snprintf (buf, sizeof(buf), " rotating in %02d:%02d", sleft/60, sleft%60);
        clientp->print(buf);
    }
    clientp->println();
}

/* send screen capture
 */
static bool getWiFiScreenCapture(WiFiClient *clientp, char *line)
{
    (void)(line);

    #define CORESZ 14                           // always 14 bytes at front
    #define HDRVER 108                          // BITMAPV4HEADER, also n bytes in subheader
    #define BHDRSZ (CORESZ+HDRVER)              // total header size
    uint8_t buf[300];                           // any modest size ge BHDRSZ and mult of 2

    uint32_t nrows = tft.SCALESZ*tft.height();
    uint32_t ncols = tft.SCALESZ*tft.width();

    resetWatchdog();

    // build BMP header 
    uint32_t npix = nrows*ncols;                // n pixels
    uint32_t nbytes = npix*2;                   // n bytes of image data

    // 14 byte header common to all formats
    buf[0] = 'B';                               // id
    buf[1] = 'M';                               // id
    *((uint32_t*)(buf+ 2)) = BHDRSZ+nbytes;     // total file size: header + pixels
    *((uint16_t*)(buf+ 6)) = 0;                 // reserved 0
    *((uint16_t*)(buf+ 8)) = 0;                 // reserved 0
    *((uint32_t*)(buf+10)) = BHDRSZ;            // offset to start of pixels

    // we use BITMAPV4INFOHEADER which supports RGB565
    *((uint32_t*)(buf+14)) = HDRVER;            // subheader type
    *((uint32_t*)(buf+18)) = ncols;             // width
    *((uint32_t*)(buf+22)) = -nrows;            // height, neg means starting at the top row
    *((uint16_t*)(buf+26)) = 1;                 // n planes
    *((uint16_t*)(buf+28)) = 16;                // bits per pixel -- 16 RGB565 
    *((uint32_t*)(buf+30)) = 3;                 // BI_BITFIELDS to indicate RGB bitmasks are present
    *((uint32_t*)(buf+34)) = nbytes;            // image size in bytes
    *((uint32_t*)(buf+38)) = 0;                 // X pixels per meter -- 0 is don't care
    *((uint32_t*)(buf+42)) = 0;                 // Y pixels per meter -- 0 is don't care
    *((uint32_t*)(buf+46)) = 0;                 // colors in table
    *((uint32_t*)(buf+50)) = 0;                 // important colors
    *((uint32_t*)(buf+54)) = 0xF800;            // red mask
    *((uint32_t*)(buf+58)) = 0x07E0;            // green mask
    *((uint32_t*)(buf+62)) = 0x001F;            // blue mask
    *((uint32_t*)(buf+66)) = 0;                 // alpha mask
    *((uint32_t*)(buf+70)) = 1;                 // CSType: 1 means ignore all the remaining fields!
    *((uint32_t*)(buf+74)) = 0;                 // RedX
    *((uint32_t*)(buf+78)) = 0;                 // RedY
    *((uint32_t*)(buf+82)) = 0;                 // RedZ
    *((uint32_t*)(buf+86)) = 0;                 // GreenX
    *((uint32_t*)(buf+90)) = 0;                 // GreenY
    *((uint32_t*)(buf+94)) = 0;                 // GreenZ
    *((uint32_t*)(buf+99)) = 0;                 // BlueX
    *((uint32_t*)(buf+102)) = 0;                // BlueY
    *((uint32_t*)(buf+106)) = 0;                // BlueZ
    *((uint32_t*)(buf+110)) = 0;                // GammaRed
    *((uint32_t*)(buf+114)) = 0;                // GammaGreen
    *((uint32_t*)(buf+118)) = 0;                // GammaBlue

    // send the web page header
    resetWatchdog();
    FWIFIPRLN (*clientp, F("HTTP/1.0 200 OK"));
    sendUserAgent (*clientp);
    FWIFIPRLN (*clientp, F("Content-Type: image/bmp"));
    FWIFIPR (*clientp, F("Content-Length: ")); clientp->println (BHDRSZ+nbytes);
    FWIFIPRLN (*clientp, F("Connection: close\r\n"));
    // Serial.println(F("web header sent"));

    // send the image header
    clientp->write ((uint8_t*)buf, BHDRSZ);
    // Serial.println(F("img header sent"));

    // send the pixels
    resetWatchdog();
    tft.graphicsMode();
    tft.setXY(0,0);
    tft.writeCommand(RA8875_MRWC);
    static bool first = true;
    if (first) {
        // skip first pixel first time
        tft.readData();
        tft.readData();
        first = false;
    }
    uint16_t bufl = 0;
    for (uint32_t i = 0; i < npix; i++) {
        if ((i % tft.width()) == 0)
            resetWatchdog();

        // swap bytes
        buf[bufl+1] = tft.readData();
        buf[bufl+0] = tft.readData();
        bufl += 2;

        if (bufl == sizeof(buf) || i == npix-1) {

            #if defined (_IS_ESP8266)
                // ESP outgoing data can deadlock if incoming buffer fills, so freshen any pending arrivals.
                if (isDXClusterConnected()) {
                    PlotPane pp = findPaneChoiceNow (PLOT_CH_DXCLUSTER);
                    if (pp != PANE_NONE)
                        updateDXCluster(plot_b[pp]);
                }
            #endif

            clientp->write ((uint8_t*)buf, bufl);
            bufl = 0;
            resetWatchdog();
        }
    }
    // Serial.println(F("pixels sent"));

    // never fails
    return (true);
}

/* remote command to report the current stopwatch timer value, in seconds
 */
static bool getWiFiStopwatch (WiFiClient *clientp, char *unused)
{
    (void) unused;

    startPlainText(*clientp);

    char buf[50];

    // get current state and time
    uint32_t ms;
    SWEngineState sws = getSWEngineState(ms);

    // format time
    int hr = ms/(1000*3600);
    ms -= hr*(1000*3600);
    int mn = ms/(1000*60);
    ms -= mn*(1000*60);
    int sc = ms/1000;
    ms -= sc*1000;
    char timebuf[30];
    snprintf (timebuf, sizeof(timebuf), _FX("%02d:%02d:%02d.%03d"), hr, mn, sc, (int)ms);

    // report
    switch (sws) {
    case SWE_RESET:
        snprintf (buf, sizeof(buf), _FX("Reset %s\n"), timebuf);
        break;
    case SWE_RUN:
        snprintf (buf, sizeof(buf), _FX("Running %s\n"), timebuf);
        break;
    case SWE_STOP:
        snprintf (buf, sizeof(buf), _FX("Stopped %s\n"), timebuf);
        break;
    case SWE_LAP:
        snprintf (buf, sizeof(buf), _FX("Lap %s\n"), timebuf);
        break;
    case SWE_COUNTDOWN:
        snprintf (buf, sizeof(buf), _FX("Countdown %s\n"), timebuf);
        break;
    }

    clientp->print (buf);

    return (true);
}

/* helper to report DE or DX info which are very similar
 */
static bool getWiFiDEDXInfo_helper (WiFiClient *clientp, char *unused, bool want_de)
{
    (void) unused;

    char buf[100];

    // handy which
    TZInfo &tz  =        want_de ? dx_tz : de_tz;
    LatLong &ll =        want_de ? dx_ll : de_ll;
    const char *prefix = want_de ? "DX_" : "DE_";
    NV_Name nv_grid =    want_de ? NV_DX_GRID : NV_DE_GRID;

    // start response
    startPlainText(*clientp);


    // report prefix and path if dx else de call
    if (want_de) {
        // show prefix
        char prefix[MAX_PREF_LEN+1];
        if (getDXPrefix(prefix)) {
            snprintf (buf, sizeof(buf), "DX_prefix     %s\n", prefix);
            clientp->print(buf);
        }

        // show short path info
        float dist, B;
        propDEDXPath (false, dx_ll, &dist, &B);
        dist *= ERAD_M;                             // radians to miles
        B *= 180/M_PIF;                             // radians to degrees
        if (show_km)
            dist *= 1.609344F;                      // mi - > km
        FWIFIPR (*clientp, F("DX_path_SP    "));
        snprintf (buf, sizeof(buf), _FX("%.0f %s @ %.0f deg\n"), dist, show_km ? "km" : "mi", B);
        clientp->print (buf);

        // show long path info
        propDEDXPath (true, dx_ll, &dist, &B);
        dist *= ERAD_M;                             // radians to miles
        B *= 180/M_PIF;                             // radians to degrees
        if (show_km)
            dist *= 1.609344F;                      // mi - > km
        FWIFIPR (*clientp, F("DX_path_LP    "));
        snprintf (buf, sizeof(buf), _FX("%.0f %s @ %.0f deg\n"), dist, show_km ? "km" : "mi", B);
        clientp->print (buf);

    } else {
        snprintf (buf, sizeof(buf), _FX("Call          %s\n"), getCallsign());
        clientp->print (buf);
    }

    // report local time
    time_t t = nowWO();
    time_t local = t + tz.tz_secs;
    int yr = year (local);
    int mo = month(local);
    int dy = day(local);
    int hr = hour (local);
    int mn = minute (local);
    int sc = second (local);
    snprintf (buf, sizeof(buf), _FX("%stime       %d-%02d-%02dT%02d:%02d:%02d\n"), prefix,yr,mo,dy,hr,mn,sc);
    clientp->print (buf);

    // report timezone
    snprintf (buf, sizeof(buf), _FX("%stz         UTC%+g\n"), prefix, tz.tz_secs/3600.0);
    clientp->print (buf);

    // report lat
    snprintf (buf, sizeof(buf), _FX("%slat        %.2f deg\n"), prefix, ll.lat_d);
    clientp->print (buf);

    // report lng
    snprintf (buf, sizeof(buf), _FX("%slng        %.2f deg\n"), prefix, ll.lng_d);
    clientp->print (buf);

    // report grid
    char maid[MAID_CHARLEN];
    getNVMaidenhead (nv_grid, maid);
    snprintf (buf, sizeof(buf), _FX("%sgrid       %s\n"), prefix, maid);
    clientp->print (buf);



    // get moon info
    time_t rise, set;
    AstroCir cir;
    getLunarRS (t, ll, &rise, &set);
    getLunarCir (t, ll, cir);

    // report moon rise
    snprintf (buf, sizeof(buf), _FX("%sMoonAz     %.1f deg\n"), prefix, rad2deg(cir.az));
    clientp->print(buf);
    snprintf (buf, sizeof(buf), _FX("%sMoonEl     %.1f deg\n"), prefix, rad2deg(cir.el));
    clientp->print(buf);
    if (rise > 0) {
        snprintf (buf, sizeof(buf), _FX("%sMoonRise   %02d:%02d\n"), prefix,
                                hour(rise+tz.tz_secs), minute (rise+tz.tz_secs));
    } else {
        snprintf (buf, sizeof(buf), _FX("%sMoonRise   none\n"), prefix);
    }
    clientp->print(buf);

    // report moon set
    if (set > 0) {
        snprintf (buf, sizeof(buf), _FX("%sMoonSet    %02d:%02d\n"), prefix,
                                hour(set+tz.tz_secs), minute (set+tz.tz_secs));
    } else {
        snprintf (buf, sizeof(buf), _FX("%sMoonSet    none\n"), prefix);
    }
    clientp->print(buf);

    // print moon velocity
    snprintf (buf, sizeof(buf), _FX("%sMoonVel    %.1f m/s\n"), prefix, cir.vel);
    clientp->print(buf);



    // get sun info
    getSolarRS (t, ll, &rise, &set);
    getSolarCir (t, ll, cir);

    // report sun rise
    snprintf (buf, sizeof(buf), _FX("%sSunAz      %.1f deg\n"), prefix, rad2deg(cir.az));
    clientp->print(buf);
    snprintf (buf, sizeof(buf), _FX("%sSunEl      %.1f deg\n"), prefix, rad2deg(cir.el));
    clientp->print(buf);
    if (rise > 0) {
        snprintf (buf, sizeof(buf), _FX("%sSunRise    %02d:%02d\n"), prefix,
                                hour(rise+tz.tz_secs), minute (rise+tz.tz_secs));
    } else {
        snprintf (buf, sizeof(buf), _FX("%sSunRise    none\n"), prefix);
    }
    clientp->print(buf);

    // report sun set
    if (set > 0) {
        snprintf (buf, sizeof(buf), _FX("%sSunSet     %02d:%02d\n"), prefix,
                                hour(set+tz.tz_secs), minute (set+tz.tz_secs));
    } else {
        snprintf (buf, sizeof(buf), _FX("%sSunSet     none\n"), prefix);
    }
    clientp->print(buf);


    // get weather
    StackMalloc wxi_mem(sizeof(WXInfo));
    WXInfo *wip = (WXInfo *) wxi_mem.getMem();
    if (getCurrentWX (ll, want_de, wip, buf)) {
        float x = useMetricUnits() ? wip->temperature_c : 9*wip->temperature_c/5+32;
        snprintf (buf, sizeof(buf), _FX("%sWxTemp     %.1f %c\n"), prefix, x, useMetricUnits() ? 'C' : 'F');
        clientp->print(buf);
        snprintf (buf, sizeof(buf), _FX("%sWxHumidity %.1f %%\n"), prefix, wip->humidity_percent);
        clientp->print(buf);
        x = (useMetricUnits() ? 3.6 : 2.237) * wip->wind_speed_mps; // kph or mph
        snprintf (buf, sizeof(buf), _FX("%sWxWindSpd  %.1f %s\n"), prefix, x, useMetricUnits()?"kph":"mph");
        clientp->print(buf);
        snprintf (buf, sizeof(buf), _FX("%sWxWindDir  %s\n"), prefix, wip->wind_dir_name);
        clientp->print(buf);
        snprintf (buf, sizeof(buf), _FX("%sWxClouds   %s\n"), prefix, wip->clouds);
        clientp->print(buf);
        snprintf (buf, sizeof(buf), _FX("%sWxCondx    %s\n"), prefix, wip->conditions);
        clientp->print(buf);
        snprintf (buf, sizeof(buf), _FX("%sWxFrom     %s\n"), prefix, wip->attribution);
        clientp->print(buf);
    } else {
        clientp->print(prefix);
        clientp->print(F("WxErr      "));
        clientp->println(buf);
    }

    printFreeHeap (F("getWiFiDEDXInfo_helper"));

    // ok
    return (true);
}

/* remote report DE info
 */
static bool getWiFiDEInfo (WiFiClient *clientp, char *line)
{
    return (getWiFiDEDXInfo_helper (clientp, line, false));
}

/* remote report DX info
 */
static bool getWiFiDXInfo (WiFiClient *clientp, char *line)
{
    return (getWiFiDEDXInfo_helper (clientp, line, true));
}

/* remote report current set of DX spots
 */
static bool getWiFiDXSpots (WiFiClient *clientp, char *line)
{
    // retrieve spots, if available
    DXClusterSpot *spots;
    uint8_t nspots;
    if (!getDXClusterSpots (&spots, &nspots)) {
        strcpy (line, _FX("No cluster"));
        return (false);
    }

    // start reply, even if none
    startPlainText (*clientp);

    // print each row, similar to drawDXSpot()
    FWIFIPR (*clientp, F("#  kHz   Call        UTC  Grid    Lat     Lng       Dist   Bear\n"));
    float sdelat = sinf(de_ll.lat);
    float cdelat = cosf(de_ll.lat);
    for (uint8_t i = 0; i < nspots; i++) {
        DXClusterSpot *sp = &spots[i];
        char line[100];

        // pretty freq, fixed 8 chars
        const char *f_fmt = sp->freq < 1e6 ? "%8.1f" : "%8.0f";
        (void) sprintf (line, f_fmt, sp->freq);

        // cdist will be cos of short-path anglar separation in radians, so acos is 0..pi
        // bear will be bearing from DE to spot east-to-north in radians, -pi..pi
        float cdist, bear;
        solveSphere (sp->ll.lng-de_ll.lng, M_PI_2F-sp->ll.lat, sdelat, cdelat, &cdist, &bear);
        float dist = acosf(cdist);                      // short path angle always 0..pi
        bear = fmodf (bear + 2*M_PIF, 2*M_PIF);         // shift -pi..pi to 0..2pi
        if (show_lp) {                                  // match DX display
            bear = fmodf (bear + 3*M_PIF, 2*M_PIF);     // +180 then 0..2pi
            dist = 2*M_PIF - dist;                      // cocircular angle
        }
        dist *= ERAD_M;                                 // angle to miles
        bear *= 180/M_PIF;                              // rad -> degrees
        if (show_km)                                    // match DX display
            dist *= 1.609344F;                          // miles -> km

        // print together
        snprintf (line+8, sizeof(line)-8, _FX(" %-*s %04u %s   %6.2f %7.2f   %6.0f   %4.0f\n"),
                MAX_SPOTCALL_LEN-1, sp->call, sp->uts, sp->grid, sp->ll.lat_d, sp->ll.lng_d, dist, bear);
        clientp->print(line);
    }

    // ok
    return (true);
}

/* remote report some basic clock configuration
 */
static bool getWiFiConfig (WiFiClient *clientp, char *unused)
{
    (void) unused;

    const __FlashStringHelper *not_sup = F("N/A");
    char buf[100];

    // start reply
    startPlainText (*clientp);

    // report whether screen is locked
    FWIFIPR (*clientp, F("Screen    "));
    if (screenIsLocked())
        FWIFIPRLN (*clientp, F("locked"));
    else
        FWIFIPRLN (*clientp, F("unlocked"));

    // report map style
    FWIFIPR (*clientp, F("MapStyle  "));
    clientp->print(getMapStyle(buf));
    if (!night_on)
        clientp->print (F(" without night"));
    clientp->println();

    // report map projection
    FWIFIPR (*clientp, F("MapProj   "));
    if (azm_on)
        FWIFIPRLN (*clientp, F("Azimuthal"));
    else
        FWIFIPRLN (*clientp, F("Mercator"));

    // report grid overlay
    FWIFIPR (*clientp, F("MapGrid   "));
    switch (mapgrid_choice) {
    case MAPGRID_OFF:       FWIFIPRLN (*clientp, F("None")); break;
    case MAPGRID_TROPICS:   FWIFIPRLN (*clientp, F("Tropics")); break;
    case MAPGRID_LATLNG:    FWIFIPRLN (*clientp, F("Lat/Long")); break;
    case MAPGRID_MAID:      FWIFIPRLN (*clientp, F("Maidenhead")); break;
    default:                FWIFIPRLN (*clientp, F("unknown")); break;
    }

    // report panes
    for (int pp = 0; pp < PANE_N; pp++)
        reportPaneChoices (clientp, (PlotPane)pp);

    // report NCDXF beacon box state
    FWIFIPR (*clientp, F("NCDXF     "));
    switch (brb_mode) {
    case BRB_SHOW_BEACONS:  FWIFIPRLN (*clientp, F("Beacons")); break;
    case BRB_SHOW_ONOFF:    FWIFIPRLN (*clientp, F("OnOff_timers")); break;
    case BRB_SHOW_PHOT:     FWIFIPRLN (*clientp, F("Photocell")); break;
    case BRB_SHOW_BR:       FWIFIPRLN (*clientp, F("Brightness")); break;
    default:                FWIFIPRLN (*clientp, F("Off")); break;
    }

    // report display brightness and timers
    uint16_t pcon, t_idle, t_idle_left;
    FWIFIPR (*clientp, F("Bright    "));
    if (getDisplayInfo (pcon, t_idle, t_idle_left)) {

        // display brightness info
        if (brControlOk())
            snprintf (buf, sizeof(buf), _FX("%d <= %d <= %d%%\n"), getBrMin(), pcon, getBrMax());
        else
            snprintf (buf, sizeof(buf), _FX("%d%%\n"), pcon);
        clientp->print (buf);

        // display dimming info
        FWIFIPR (*clientp, F("AutoDim   "));
        if (t_idle == 0)
            strcpy (buf, _FX("off"));
        else {
            size_t nbuf = snprintf (buf, sizeof(buf), _FX("%d mins, off "), t_idle);
            if (t_idle_left > 0) {
                int mn = t_idle_left/60;
                int sc = t_idle_left - mn*60;
                snprintf (buf+nbuf, sizeof(buf)-nbuf, _FX("in %02d:%02d"), mn, sc);
            } else {
                snprintf (buf+nbuf, sizeof(buf)-nbuf, _FX("now"));
            }
        }
        clientp->println (buf);

        // display on/off time for each day
        for (int dow = 1; dow <= DAYSPERWEEK; dow++) {
            uint16_t on_mins, off_mins;
            if (getDisplayOnOffTimes (dow, on_mins, off_mins)) {
                snprintf (buf, sizeof(buf), "OnOff_%s %02d:%02d %02d:%02d\n", dayShortStr(dow),
                                    on_mins/60, on_mins%60, off_mins/60, off_mins%60);
                clientp->print (buf);
            } else
                break;
        }

    } else
        FWIFIPRLN (*clientp, not_sup);


    // report alarm
    FWIFIPR (*clientp, F("Alarm     "));
    AlarmState as;
    uint16_t hr, mn;
    getAlarmState (as, hr, mn);
    switch (as) {
    case ALMS_OFF:
        snprintf (buf, sizeof(buf), "Off (%02d:%02d)\n", hr, mn);
        break;
    case ALMS_ARMED:
        snprintf (buf, sizeof(buf), "Armed for %02d:%02d\n", hr, mn);
        break;
    case ALMS_RINGING:
        snprintf (buf, sizeof(buf), "Ringing since %02d:%02d\n", hr, mn);
        break;
    }
    clientp->print (buf);


    // time source
    FWIFIPR (*clientp, F("TimeFrom  "));
    if (gpsd_server)
        snprintf (buf, sizeof(buf), _FX("GPSD %s\n"), gpsd_server);
    else if (ntp_server)
        snprintf (buf, sizeof(buf), _FX("NTP %s\n"), ntp_server);
    else
        strcpy (buf, _FX("Error\n"));
    clientp->print(buf);


    // report what DE pane is being used for
    FWIFIPR (*clientp, F("DEPane    "));
    switch (de_time_fmt) {
    case DETIME_INFO:           FWIFIPR (*clientp, F("Info ")); break;
    case DETIME_ANALOG:         FWIFIPR (*clientp, F("Analog ")); break;
    case DETIME_CAL:            FWIFIPR (*clientp, F("Calendar ")); break;
    case DETIME_ANALOG_DTTM:    FWIFIPR (*clientp, F("Analog+DtTm ")); break;
    }
    snprintf (buf, sizeof(buf), _FX("TZ=UTC%+g "), de_tz.tz_secs/3600.0);
    clientp->print(buf);
    if (de_time_fmt == DETIME_INFO) {
        if (desrss)
            FWIFIPR (*clientp, F("RSAtAt"));
        else
            FWIFIPR (*clientp, F("RSInAgo"));
    }
    clientp->println();

    // report what DX pane is being used for
    FWIFIPR (*clientp, F("DXPane    "));
    if (dx_info_for_sat)
        FWIFIPRLN (*clientp, F("sat"));
    else {
        FWIFIPR (*clientp, F("DX "));
        snprintf (buf, sizeof(buf), "TZ=UTC%+g ", dx_tz.tz_secs/3600.0);
        clientp->print(buf);
        switch (dxsrss) {
        case DXSRSS_INAGO:  FWIFIPR (*clientp, F("RSInAgo")); break;
        case DXSRSS_ATAT:   FWIFIPR (*clientp, F("RSAtAt"));  break;
        case DXSRSS_PREFIX: FWIFIPR (*clientp, F("Prefix"));  break;
        }
        clientp->println();
    }

    // report rss
    if (rss_on)
        FWIFIPRLN (*clientp, F("RSS       on"));
    else
        FWIFIPRLN (*clientp, F("RSS       off"));

    // report dxcluster state
    FWIFIPR (*clientp, F("DXCluster "));
    if (useDXCluster()) {
        snprintf (buf, sizeof(buf), _FX("%s:%d %sconnected\n"), getDXClusterHost(), getDXClusterPort(),
                                        isDXClusterConnected() ? "" : "dis");
        clientp->print (buf);
    } else
        FWIFIPRLN (*clientp, F("off"));

    // report units
    FWIFIPR (*clientp, F("Units     "));
    if (useMetricUnits())
        FWIFIPRLN (*clientp, F("metric"));
    else
        FWIFIPRLN (*clientp, F("imperial"));

    // report BME info
    FWIFIPR (*clientp, F("BME280    "));
    #if defined(_SUPPORT_ENVSENSOR)
        size_t bl = 0;
        for (int i = 0; i < MAX_N_BME; i++) {
            const BMEData *dp = getBMEData(i);
            if (dp)
                bl += snprintf (buf+bl,sizeof(buf)-bl, _FX("dTemp@%x= %g dPres@%x= %g "),
                                        dp->i2c, getBMETempCorr(i), dp->i2c, getBMEPresCorr(i));
        }
        if (bl > 0)
            clientp->println (buf);
        else
            FWIFIPRLN (*clientp, F("none"));
    #else
        FWIFIPRLN (*clientp, not_sup);
    #endif // _SUPPORT_ENVSENSOR

    // report KX3 info
    FWIFIPR (*clientp, F("KX3       "));
    #if defined(_SUPPORT_KX3)
        uint32_t baud = getKX3Baud();
        if (baud > 0) {
            snprintf (buf, sizeof(buf), _FX("%d baud\n"), baud);
            clientp->print (buf);
        } else
            FWIFIPRLN (*clientp, F("off"));
    #else
        FWIFIPRLN (*clientp, not_sup);
    #endif // _SUPPORT_KX3

    // report GPIO info
    FWIFIPR (*clientp, F("GPIO      "));
    #if defined(_SUPPORT_GPIO)
        if (GPIOOk()) {
            FWIFIPRLN (*clientp, F("on"));
            #if defined(_IS_UNIX)
                FWIFIPR (*clientp, F("ONAIR     "));
                if (checkOnAir())
                    FWIFIPRLN (*clientp, F("on"));
                else
                    FWIFIPRLN (*clientp, F("off"));
            #endif
        } else {
            FWIFIPRLN (*clientp, F("off"));
        }
    #else
        FWIFIPRLN (*clientp, not_sup);
    #endif // _SUPPORT_GPIO

    // report photosensor info
    FWIFIPR (*clientp, F("Photocell "));
    #if defined(_SUPPORT_PHOT)
        if (found_phot)
            FWIFIPRLN (*clientp, F("connected"));
        else
            FWIFIPRLN (*clientp, F("disconnected"));
    #else
        FWIFIPRLN (*clientp, not_sup);
    #endif // _SUPPORT_PHOT

    // report call sign colors
    FWIFIPR (*clientp, F("Call_FG   "));
    snprintf (buf, sizeof(buf), _FX("%d,%d,%d\n"), RGB565_R(cs_info.fg_color),
                RGB565_G(cs_info.fg_color), RGB565_B(cs_info.fg_color)); 
    clientp->print(buf);
    FWIFIPR (*clientp, F("Call_BG   "));
    if (cs_info.bg_rainbow)
        snprintf (buf, sizeof(buf), _FX("Rainbow\n"));
    else
        snprintf (buf, sizeof(buf), _FX("%d,%d,%d\n"), RGB565_R(cs_info.bg_color),
                RGB565_G(cs_info.bg_color), RGB565_B(cs_info.bg_color)); 
    clientp->print(buf);


    // done
    return (true);
}

/* report current satellite info to the given WiFi connection, or none.
 * always return true
 */
static bool getWiFiSatellite (WiFiClient *clientp, char *unused)
{
    (void) unused;

    // start reply
    startPlainText (*clientp);

    // get name and current position
    float az, el, range, rate, raz, saz, rhrs, shrs;
    char name[NV_SATNAME_LEN];
    if (!getSatAzElNow (name, &az, &el, &range, &rate, &raz, &saz, &rhrs, &shrs)) {
        FWIFIPRLN (*clientp, F("none"));
        return (true);
    }

    FWIFIPR (*clientp, F("Name  ")); clientp->println (name);
    FWIFIPR (*clientp, F("Alt   ")); clientp->print (el); FWIFIPRLN(*clientp, F(" deg"));
    FWIFIPR (*clientp, F("Az    ")); clientp->print (az); FWIFIPRLN(*clientp, F(" deg"));
    FWIFIPR (*clientp, F("Range ")); clientp->print (range); FWIFIPRLN(*clientp, F(" km"));
    FWIFIPR (*clientp, F("Rate  ")); clientp->print (rate); FWIFIPRLN(*clientp, F(" m/s"));
    FWIFIPR (*clientp, F("144MHzDoppler ")); clientp->print (-rate*144000/3e8); FWIFIPRLN(*clientp,F(" kHz"));
    FWIFIPR (*clientp, F("440MHzDoppler ")); clientp->print (-rate*440000/3e8); FWIFIPRLN(*clientp,F(" kHz"));

    // add table of next several events, if any
    time_t *rises, *sets;
    int n_times;
    if (raz == SAT_NOAZ || saz == SAT_NOAZ || (n_times = nextSatRSEvents (&rises, &sets)) == 0) {
        FWIFIPR (*clientp, F("No rise or set\n"));
    } else {
        // print heading
        FWIFIPR (*clientp, F("  Upcoming DE Passes\n"));
        FWIFIPR (*clientp, F("Day  Rise    Set    Up\n"));
        // snprintf (line, sizeof(line), "%.3s  %02dh%02d  %02dh%02d  %02d:%02d\n"

        // print table
        for (int i = 0; i < n_times; i++) {
            char line[60];

            // DE timezone
            time_t rt = rises[i] + de_tz.tz_secs;
            time_t st = sets[i] + de_tz.tz_secs;
            int up = st - rt;

            // detect crossing midnight by comparing weekday
            int rt_wd = weekday(rt);
            int st_wd = weekday(st);

            // start with rise day and time for sure
            size_t l = snprintf (line, sizeof(line), "%.3s  %02dh%02d", dayShortStr(rt_wd),
                                                                                hour(rt), minute(rt));

            // if set time is tomorrow start new line with set day and blank rise
            if (rt_wd != st_wd)
                l += snprintf (line+l, sizeof(line)-l, "\n%s  %s", dayShortStr(st_wd), "     ");

            // show set time
            l += snprintf (line+l, sizeof(line)-l, "  %02dh%02d  ", hour(st), minute(st));

            // show up time, beware longer than 1 hour (moon!)
            if (up >= 3600)
                l += snprintf (line+l, sizeof(line)-l, "%02dh%02d\n", up/3600, (up-3600*(up/3600))/60);
            else
                l += snprintf (line+l, sizeof(line)-l, "%02d:%02d\n", up/60, up-60*(up/60));

            // done!
            clientp->print (line);
        }

        // clean up
        free ((void*)rises);
        free ((void*)sets);
    }

    return (true);
}


/* report all available satellites.
 */
static bool getWiFiAllSatellites (WiFiClient *clientp, char *line)
{
    // get names and elements
    const char **all_names = getAllSatNames();
    if (all_names == NULL) {
        strcpy (line, _FX("No sats"));
        return (false);
    }

    // list and free
    startPlainText(*clientp);
    const char **all_names_0 = all_names;
    const char *name;
    while ((name = *all_names++) != NULL) {
        clientp->println (name);
        free ((void*)name);
    }
    free ((void*)all_names_0);

    // ok
    return (true);
}


/* send the current collection of sensor data to client in tabular format.
 */
static bool getWiFiSensorData (WiFiClient *clientp, char *line)
{
    if (getNBMEConnected() == 0) {
        strcpy (line, _FX("No sensors"));
        return (false);
    }

    // send html header
    startPlainText(*clientp);

    // send content header
    if (useMetricUnits())
        FWIFIPR (*clientp, F("#   UTC ISO 8601      UNIX secs I2C  Temp,C   P,hPa   Hum,%  DewP,C\n"));
    else
        FWIFIPR (*clientp, F("#   UTC ISO 8601      UNIX secs I2C  Temp,F  P,inHg   Hum,%  DewP,F\n"));

    // send data for each connected sensor
    resetWatchdog();
    for (int i = 0; i < MAX_N_BME; i++) {
        const BMEData *dp = getBMEData(i);
        if (dp) {
            // head points to oldest
            for (int j = 0; j < N_BME_READINGS; j++) {
                uint8_t q = (dp->q_head+j)%N_BME_READINGS;
                time_t u = dp->u[q];
                if (u) {
                    char buf[100];
                    snprintf (buf, sizeof(buf),
                                _FX("%4d-%02d-%02dT%02d:%02d:%02dZ %lu  %02x %7.2f %7.2f %7.2f %7.2f\n"),
                                year(u), month(u), day(u), hour(u), minute(u), second(u), u,
                                dp->i2c, dp->t[q], dp->p[q], dp->h[q], dewPoint (dp->t[q], dp->h[q]));
                    clientp->print (buf);
                    q = (q+1)%N_BME_READINGS;
                }
            }
            clientp->print ("\n");
        }
    }

    return (true);
}

/* given age in seconds, set string to short approx description.
 */
static char *ageStr (long secs, char *str)
{
    if (secs < 60)
        sprintf (str, "%2ld secs", secs);
    else if (secs < 3600)
        sprintf (str, "%2ld mins", secs/60);
    else if (secs < 24*3600)
        sprintf (str, "%2ld hrs", secs/3600);
    else 
        sprintf (str, "1+ days");
    return (str);
}

/* send the current space weather stats to client
 */
static bool getWiFiSpaceWx (WiFiClient *clientp, char *unused)
{
    (void) unused;

    // send html header
    startPlainText(*clientp);

    // collect info
    SPWxValue ssn, flux, kp, swind, drap;
    NOAASpaceWx noaaspw;
    float path[PROP_MAP_N];
    char xray[10];
    time_t noaaspw_age, xray_age, path_age;
    getSpaceWeather (ssn, flux, kp, swind, drap, noaaspw, noaaspw_age, xray, xray_age, path, path_age);

    // send values and ages
    char buf[100];
    char age[20];

    clientp->print (F(" Datum   Value    Age\n"));
    clientp->print (F("-------- -----  -------\n"));

    snprintf (buf, sizeof(buf), _FX("SSN      %5.1f  %s\n"), ssn.value, ageStr(ssn.age, age));
    clientp->print (buf);

    snprintf (buf, sizeof(buf), _FX("KP        %4.0f  %s\n"), kp.value, ageStr(kp.age, age));
    clientp->print (buf);

    snprintf (buf, sizeof(buf), _FX("FLUX     %5.1f  %s\n"), flux.value, ageStr(flux.age, age));
    clientp->print (buf);

    snprintf (buf, sizeof(buf), _FX("XRAY      %4s  %s\n"), xray, ageStr(xray_age, age));
    clientp->print (buf);

    snprintf (buf, sizeof(buf), _FX("SOLWIND   %4.1f  %s\n"), swind.value, ageStr(swind.age, age));
    clientp->print (buf);

    snprintf (buf, sizeof(buf), _FX("DRAP      %4.1f  %s\n"), drap.value, ageStr(drap.age, age));
    clientp->print (buf);

    for (int i = 0; i < PROP_MAP_N; i++) {
        int band = propMap2Band ((PropMapSetting)i);
        // match format in plotBandConditions()
        snprintf (buf, sizeof(buf), _FX("DEDX_%02dm  %4.0f  %s\n"), band, 99*path[i], ageStr(path_age, age));
        clientp->print (buf);
    }

    for (int i = 0; i < N_NOAASW_C; i++) {
        for (int j = 0; j < N_NOAASW_V; j++) {
            snprintf (buf, sizeof(buf), _FX("NSPW_%c%d   %4d  %s\n"), noaaspw.cat[i], j, noaaspw.val[i][j],
                    ageStr(noaaspw_age, age));
            clientp->print (buf);
        }
    }

    // ok
    printFreeHeap (F("getWiFiSpaceWx"));
    return (true);
}



/* send some misc system info
 */
static bool getWiFiSys (WiFiClient *clientp, char *unused)
{
    (void) unused;
    char buf[100];

    // send html header
    startPlainText(*clientp);

    // get latest worst stats
    int worst_heap, worst_stack;
    getWorstMem (&worst_heap, &worst_stack);

    // show basic info
    resetWatchdog();
    FWIFIPR (*clientp, F("Version  ")); clientp->println (hc_version);
    FWIFIPR (*clientp, F("MaxStack ")); clientp->println (worst_stack);
    FWIFIPR (*clientp, F("MaxWDDT  ")); clientp->println (max_wd_dt);
    FWIFIPR (*clientp, F("Platform ")); clientp->println (platform);
    FWIFIPR (*clientp, F("Backend  ")); clientp->println (svr_host);
    FWIFIPR (*clientp, F("SvrPort  ")); clientp->println (svr_port);
#if defined(_IS_ESP8266)
    FWIFIPR (*clientp, F("MinHeap  ")); clientp->println (worst_heap);
    FWIFIPR (*clientp, F("FreeNow  ")); clientp->println (ESP.getFreeHeap());
    FWIFIPR (*clientp, F("MaxBlock ")); clientp->println (ESP.getMaxFreeBlockSize());
    FWIFIPR (*clientp, F("SketchSz ")); clientp->println (ESP.getSketchSize());
    FWIFIPR (*clientp, F("FreeSkSz ")); clientp->println (ESP.getFreeSketchSpace());
    FWIFIPR (*clientp, F("FlChipSz ")); clientp->println (ESP.getFlashChipRealSize());
    FWIFIPR (*clientp, F("CPUMHz   ")); clientp->println (ESP.getCpuFreqMHz());
    FWIFIPR (*clientp, F("CoreVer  ")); clientp->println (ESP.getCoreVersion());
    // #if defined __has_include                        should work but doesn't
        // #if __has_include (<lwip-git-hash.h>)        should work but doesn't either
            // #include <lwip-git-hash.h>
            // FWIFIPR (*clientp, F("lwipVer  ")); clientp->println (LWIP_HASH_STR);
        // #endif
    // #endif
#endif

    // show uptime
    uint16_t days; uint8_t hrs, mins, secs;
    if (getUptime (&days, &hrs, &mins, &secs)) {
        snprintf (buf, sizeof(buf), _FX("%dd%02d:%02d:%02d\n"), days, hrs, mins, secs);
        FWIFIPR (*clientp, F("UpTime   ")); clientp->print (buf);
    }

    // show NTP servers
    const NTPServer *ntp_list;
    int n_ntp = getNTPServers (&ntp_list);
    for (int i = 0; i < n_ntp; i++) {
        int bl = snprintf (buf, sizeof(buf), "NTP      %s ", ntp_list[i].server);
        int rsp = ntp_list[i].rsp_time;
        if (rsp == 0)
            bl += snprintf (buf+bl, sizeof(buf)-bl, "%s\n", _FX("- Not yet measured"));
        else if (rsp == NTP_TOO_LONG)
            bl += snprintf (buf+bl, sizeof(buf)-bl, "%s\n", _FX("- Timed out"));
        else
            bl += snprintf (buf+bl, sizeof(buf)-bl, "%d ms\n", rsp);
        clientp->print (buf);
    }

    // show file system info
    int n_info;
    uint64_t fs_size, fs_used;
    char *fs_name;
    FS_Info *fip0 = getConfigDirInfo (&n_info, &fs_name, &fs_size, &fs_used);
    clientp->print (fs_name);
    if (fs_size > 1000000000U) {
        snprintf (buf, sizeof(buf), " %lu / %lu MiB\n",
                        (unsigned long)(fs_used/1048576U), (unsigned long)(fs_size/1048576U));
    } else
        snprintf (buf, sizeof(buf), " %lu / %lu B\n", (unsigned long)fs_used, (unsigned long)fs_size);
    clientp->print (buf);
    for (int i = 0; i < n_info; i++) {
        FS_Info *fip = &fip0[i];
        snprintf (buf, sizeof(buf), "  %-32s %20s %7u\n", fip->name, fip->date, fip->len);
        clientp->print (buf);
    }
    free (fs_name);
    free (fip0);

    return (true);
}


/* send current clock time
 */
static bool getWiFiTime (WiFiClient *clientp, char *unused)
{
    (void) unused;

    // send html header
    startPlainText(*clientp);

    // report user's idea of time
    char buf[100];
    time_t t = nowWO();
    int yr = year (t);
    int mo = month(t);
    int dy = day(t);
    int hr = hour (t);
    int mn = minute (t);
    int sc = second (t);
    int bl = snprintf (buf, sizeof(buf)-10, _FX("Clock_UTC %d-%02d-%02dT%02d:%02d:%02d"),
                        yr, mo, dy, hr, mn, sc);

    // indicate any time offset
    int32_t off = utcOffset();
    if (off == 0) {
        buf[bl++] = 'Z';                        // append Z if time really is UTC
        buf[bl] = 0;
    } else
        sprintf (buf+bl, " %+g", off/3600.0);   // else show offset in hours
    clientp->println (buf);

    return (true);
}

static bool setWiFiAntennaHeading (WiFiClient *clientp, char line[]) {
    // find each keyword
    int h, w;
    if (sscanf(line, "heading=%d&width=%d", &h, &w) != 2) {
        // User didn't send us valid data.
        strcpy (line, garbcmd);
        return (false);
    }
    antenna_heading = int16_t(h%360);
    antenna_width = int16_t(w%360);

    // ack
    if (clientp) {
        startPlainText (*clientp);
        clientp->print (_FX("ok\n"));
    }

    return(true);
}


/* remote command to set call sign to some message and set fg and bg colors.
 * all are optional in any order; restore defaults if no args at all.
 */
static bool setWiFiTitle (WiFiClient *clientp, char line[])
{
    // find each possible keyword -- remember all are optional
    char *msg = strstr (line, "msg=");
    char *fg = strstr (line, "fg=");
    char *bg = strstr (line, "bg=");

    // check for any unexpected & args
    for (char *amp = strchr (line, '&'); amp; amp = strchr (amp+1, '&')) {
        if ((!msg || amp != msg-1) && (!fg || amp != fg-1) && (!bg || amp != bg-1)) {
            strcpy (line, garbcmd);
            return (false);
        }
    }

    // check for any unexpected = args
    for (char *eq = strchr (line, '='); eq; eq = strchr (eq+1, '=')) {
        if ((!msg || eq != msg+3) && (!fg || eq != fg+2) && (!bg || eq != bg+2)) {
            strcpy (line, garbcmd);
            return (false);
        }
    }

    // if msg, terminate at next & if any
    if (msg) {
        msg += 4;                  // skip msg=
        char *amp = strchr (msg, '&');
        if (amp)
            *amp = '\0';
    }

    // crack fg if found
    uint16_t fg_c = 0;
    if (fg) {
        fg += 3;        // skip fg=
        int r, g, b;
        if (sscanf (fg, _FX("%d,%d,%d"), &r, &g, &b) != 3 || r < 0 || r > 255 || g < 0 || g > 255
                        || b < 0 || b > 255) {
            strcpy (line, garbcmd);
            return (false);
        }
        fg_c = RGB565(r,g,b);
    }

    // crack bg if found
    uint16_t bg_c = 0;
    bool rainbow = false;
    if (bg) {
        bg += 3;        // skip bg=
        if (strncmp (bg, "rainbow", 7) == 0)
            rainbow = true;
        else {
            int r, g, b;
            if (sscanf (bg, _FX("%d,%d,%d"), &r, &g, &b) != 3 || r < 0 || r > 255 || g < 0 || g > 255
                            || b < 0 || b > 255) {
                strcpy (line, garbcmd);
                return (false);
            }
            bg_c = RGB565(r,g,b);
        }
    }

    // all good: update definitions
    if (msg) {
        free (cs_info.call);
        cs_info.call = strdup(msg);
    }
    if (fg)
        cs_info.fg_color = fg_c;
    if (bg) {
        if (rainbow)
            cs_info.bg_rainbow = 1;
        else {
            cs_info.bg_color = bg_c;
            cs_info.bg_rainbow = 0;
        }
    }

    // or restore default if no args
    if (!msg && !fg && !bg) {
        free (cs_info.call);
        cs_info.call = strdup(getCallsign());
        NVReadUInt16 (NV_CALL_FG_COLOR, &cs_info.fg_color);
        NVReadUInt16 (NV_CALL_BG_COLOR, &cs_info.bg_color);
        NVReadUInt8 (NV_CALL_BG_RAINBOW, &cs_info.bg_rainbow);
    }


    // engage
    drawCallsign (true);

    // ack
    if (clientp) {
        startPlainText (*clientp);
        clientp->print (_FX("ok\n"));
    }

    return (true);
}

/* remote command to set up alarm clock
 *   state=off|armed&time=HR:MN
 */
static bool setWiFiAlarm (WiFiClient *clientp, char line[])
{
    // crack
    char state[10];
    int hr = 0, mn = 0;
    int ns = sscanf (line, "%10[^&]&time=%d:%d", state, &hr, &mn);

    // get current state
    AlarmState as;
    uint16_t hr16, mn16;
    getAlarmState (as, hr16, mn16);

    // parse
    if (ns == 1 && strcmp (state, "off") == 0) {
        // turn off but retain current time
        as = ALMS_OFF;
    } else if (ns == 1 && strcmp (state, "armed") == 0) {
        // turn on at current time
        as = ALMS_ARMED;
    } else if (ns == 3 && strcmp (state, "armed") == 0 && hr >= 0 && hr < 24 && mn >= 0 && mn < 60) {
        // turn on at given time
        as = ALMS_ARMED;
        hr16 = hr;
        mn16 = mn;
    } else {
        strcpy (line, garbcmd);
        return (false);
    }

    // engage
    setAlarmState (as, hr16, mn16);

    // ack
    if (clientp) {
        startPlainText (*clientp);
        if (as == ALMS_OFF)
            clientp->print (_FX("alarm off\n"));
        else {
            char buf[30];
            snprintf (buf, sizeof(buf), "armed at %02d:%02d\n", hr16, mn16);
            clientp->print (buf);
        }
    }

    return (true);
}

/* remote command to set display on or off
 */
static bool setWiFiDisplayOnOff (WiFiClient *clientp, char line[])
{
    if (brOnOffOk()) {

        // parse
        if (strcmp (line, "on") == 0)
            brightnessOn();
        else if (strcmp (line, "off") == 0)
            brightnessOff();
        else {
            strcpy (line, _FX("Specify on or off"));
            return (false);
        }

        // ack with same state
        if (clientp) {
            startPlainText (*clientp);
            FWIFIPR (*clientp, F("display "));
            clientp->println (line);
        }

        // ok
        return (true);

    } else {

        strcpy (line, notsupp);
        return (false);

    }
}

/* convert 3-letter day-of-week abbreviation to 1..7 (Sun..Sat),
 * return whether successful.
 */
static bool crackDOW (const char *daystr, int &dow)
{
        for (uint8_t i = 1; i <= DAYSPERWEEK; i++) {
            if (strncmp (dayShortStr(i), daystr, 3) == 0) {
                dow = i;
                return (true);
            }
        }
        return (false);
}

/* remote command to set display on/off/idle times
 * on=HR:MN&off=HR:MN&idle=mins&day=DOW
 */
static bool setWiFiDisplayTimes (WiFiClient *clientp, char line[])
{

    if (brOnOffOk()) {

        // parse -- idle and dow are optional
        int on_hr, on_mn, off_hr, off_mn, idle_mins = -1, dow = -1;
        char *on = strstr (line, _FX("on="));
        char *off = strstr (line, _FX("off="));
        char *day = strstr (line, _FX("day="));
        char *idle = strstr (line, _FX("idle="));
        if (!on || sscanf (on+3, _FX("%d:%d"), &on_hr, &on_mn) != 2
                                || !off || sscanf (off+4, _FX("%d:%d"), &off_hr, &off_mn) != 2
                                || (day && !crackDOW (day+4, dow))
                                || (idle && sscanf (idle+5, "%d", &idle_mins) != 1)) {
            strcpy (line, garbcmd);
            return (false);
        }

        // pack times and validate
        uint16_t on_mins = on_hr*60 + on_mn;
        uint16_t off_mins = off_hr*60 + off_mn;
        if (on_mins >= MINSPERDAY || off_mins >= MINSPERDAY) {
            strcpy (line, _FX("Invalid time"));
            return (false);
        }

        // default today if no dow
        if (dow < 0)
            dow = DEWeekday();

        // set
        if (!setDisplayOnOffTimes (dow, on_mins, off_mins, idle_mins)) {
            strcpy (line, notsupp);
            return (false);
        }

        // ack
        if (clientp) {
            startPlainText (*clientp);
            const char hm_fmt[] = "%02d:%02d";
            char buf[50];

            FWIFIPR (*clientp, F("On    "));
            sprintf (buf, hm_fmt, on_hr, on_mn);
            clientp->println (buf);

            FWIFIPR (*clientp, F("Off   "));
            sprintf (buf, hm_fmt, off_hr, off_mn);
            clientp->println (buf);

            if (idle_mins >= 0) {
                FWIFIPR (*clientp, F("Idle  "));
                clientp->println (idle_mins);
            }

            FWIFIPR (*clientp, F("Day   "));
            if (day)
                snprintf (buf, sizeof(buf), "%.3s", day+4);
            else
                strcpy (buf, dayShortStr(DEWeekday()));
            clientp->println (buf);
        }

        // ok
        return (true);

    } else {

        strcpy (line, notsupp);
        return (false);

    }
}


/* helper to set DE or DX from GET command: lat=XXX&lng=YYY
 * return whether all ok.
 */
static bool setWiFiNewDEDX_helper (WiFiClient *clientp, bool new_dx, char line[])
{
    LatLong ll;

    // crack
    float lat, lng;
    if (sscanf(line, "lat=%f&lng=%f", &lat, &lng) != 2 || lng < -180 || lng >= 180 || lat < -90 || lat > 90) {
        strcpy (line, garbcmd);
        return (false);
    }
    ll.lat_d = lat;
    ll.lng_d = lng;

    // engage -- including normalization
    if (new_dx)
        newDX (ll, NULL, NULL);
    else
        newDE (ll, NULL);

    // ack with updated info as if get
    if (clientp)
        return (getWiFiDEDXInfo_helper (clientp, line, new_dx));
    else
        return (true);
}

/* set DE from GET command: lat=XXX&lng=YYY
 * return whether all ok.
 */
static bool setWiFiNewDE (WiFiClient *clientp, char line[])
{
    return (setWiFiNewDEDX_helper (clientp, false, line));
}

/* set DX from GET command: lat=XXX&lng=YYY
 * return whether all ok.
 */
static bool setWiFiNewDX (WiFiClient *clientp, char line[])
{
    return (setWiFiNewDEDX_helper (clientp, true, line));
}



/* set DE or DX from maidenhead locator, eg, AB12
 * return whether all ok
 */
static bool setWiFiNewGrid_helper (WiFiClient *clientp, bool new_dx, char line[])
{
    Serial.println (line);

    // check and convert
    size_t linelen = strlen(line);
    if (linelen < 4 || linelen > MAID_CHARLEN-1) {
        strcpy (line, _FX("Grid must be 4 or 6 chars"));
        return (false);
    }
    LatLong ll;
    if (!maidenhead2ll (ll, line)) {
        strcpy (line, _FX("Invalid grid"));
        return (false);
    }

    // engage
    if (new_dx)
        newDX (ll, line, NULL);
    else
        newDE (ll, line);

    // ack with updated info as if get
    if (clientp)
        return (getWiFiDEDXInfo_helper (clientp, line, new_dx));
    else
        return (true);
}

/* set DE from maidenhead locator, eg, AB12
 * return whether all ok
 */
static bool setWiFiNewDEGrid (WiFiClient *clientp, char line[])
{
    return (setWiFiNewGrid_helper (clientp, false, line));
}

/* set DX from maidenhead locator, eg, AB12
 * return whether all ok
 */
static bool setWiFiNewDXGrid (WiFiClient *clientp, char line[])
{
    return (setWiFiNewGrid_helper (clientp, true, line));
}



/* set one or more view features of the map, same as menu.
 * syntax: Style=S&Grid=G&Projection=P&RSS=on|off&Night=on|off
 * all keywords optional but require at least 1.
 */
static bool setWiFiMapView (WiFiClient *clientp, char line[])
{
    // look for each keyword
    char *S = strstr (line, _FX("Style="));
    char *G = strstr (line, _FX("Grid="));
    char *P = strstr (line, _FX("Projection="));
    char *R = strstr (line, _FX("RSS="));
    char *N = strstr (line, _FX("Night="));

    // require at least 1
    if (!S && !G && !P && !R && !N) {
        strcpy_P (line, PSTR("bad args"));
        return (false);
    }

    // look for unknown keywords
    for (char *sep = line-1, *kw = line; sep != NULL; sep = strchr (kw, '&'), kw = sep + 1) {
        if (S != kw && G != kw && P != kw && R != kw && N != kw) {
            strcpy_P (line, PSTR("unknown keyword"));
            return (false);
        }
    }

    // sscanf buffer and matching safe sscanf format
    char buf[20];
    const char sfmt[] = "%20[^&]";

    // check style
    CoreMaps my_cm = CM_NONE;
    if (S) {
        if (sscanf (S+6, sfmt, buf) != 1) {
            strcpy (line, garbcmd);
            return (false);
        }
        if (!strcmp (buf, _FX("Countries")))
            my_cm = CM_COUNTRIES;
        else if (!strcmp (buf, _FX("Terrain")))
            my_cm = CM_TERRAIN;
        else if (!strcmp (buf, _FX("DRAP")))
            my_cm = CM_DRAP;
        else {
            strcpy_P (line, PSTR("unknown style"));
            return (false);
        }
    }

    // check grid
    int my_llg = -1;
    if (G) {
        if (sscanf (G+5, sfmt, buf) != 1) {
            strcpy (line, garbcmd);
            return (false);
        }
        if (!strcmp (buf, _FX("None")))
            my_llg = MAPGRID_OFF;
        else if (!strcmp (buf, _FX("Tropics")))
            my_llg = MAPGRID_TROPICS;
        else if (!strcmp (buf, _FX("Lat/Long")))
            my_llg = MAPGRID_LATLNG;
        else if (!strcmp (buf, _FX("Maidenhead")))
            my_llg = MAPGRID_MAID;
        else {
            strcpy_P (line, PSTR("unknown grid"));
            return (false);
        }
    }

    // check projection
    int my_azm = -1;
    if (P) {
        if (sscanf (P+11, sfmt, buf) != 1) {
            strcpy (line, garbcmd);
            return (false);
        }
        if (!strcmp (buf, _FX("Azimuthal")))
            my_azm = 1;
        else if (!strcmp (buf, _FX("Mercator")))
            my_azm = 0;
        else {
            strcpy_P (line, PSTR("unknown projection"));
            return (false);
        }
    }

    // check RSS
    int my_rss = -1;
    if (R) {
        if (sscanf (R+4, sfmt, buf) != 1) {
            strcpy (line, garbcmd);
            return (false);
        }
        if (!strcmp (buf, "on"))
            my_rss = 1;
        else if (!strcmp (buf, "off"))
            my_rss = 0;
        else {
            strcpy_P (line, PSTR("unknown RSS"));
            return (false);
        }
    }

    // check Night
    int my_night = -1;
    if (N) {
        if (sscanf (N+6, sfmt, buf) != 1) {
            strcpy (line, garbcmd);
            return (false);
        }
        if (!strcmp (buf, "on"))
            my_night = 1;
        else if (!strcmp (buf, "off"))
            my_night = 0;
        else {
            strcpy_P (line, PSTR("unknown Night"));
            return (false);
        }
    }

    // all options look good, engage any that have changed.
    // this is rather like drawMapMenu().

    bool full_redraw = false;
    if (S && my_cm != core_map) {
        if (installNewMapStyle (my_cm)) {
            full_redraw = true;
        } else {
            strcpy (line, _FX("style error"));
            return (false);
        }
    }
    if (G && my_llg != mapgrid_choice) {
        mapgrid_choice = my_llg;
        NVWriteUInt8 (NV_LLGRID, mapgrid_choice);
        full_redraw = true;
    }
    if (P && my_azm != azm_on) {
        azm_on = my_azm;
        NVWriteUInt8 (NV_AZIMUTHAL_ON, azm_on);
        full_redraw = true;
    }
    if (N && my_night != night_on) {
        night_on = my_night;
        NVWriteUInt8 (NV_NIGHT_ON, night_on);
        full_redraw = true;
    }
    if (R && my_rss != rss_on) {
        rss_on = my_rss;
        NVWriteUInt8 (NV_RSS_ON, rss_on);
        if (!full_redraw) {
            // minimal change if don't need to restart whole map
            if (rss_on)
                scheduleRSSNow();
            else
                eraseRSSBox();
        }
    }

    // restart map if it has changed
    if (full_redraw)
        initEarthMap();

    // ack
    if (clientp) {
        startPlainText (*clientp);
        strncpySubChar (line, line, '\n', '&', strlen(line));
        clientp->println(line);
    }

    // good
    return (true);
}


/* set new collection of plot choices for a given pane.
 * return whether ok
 */
static bool setWiFiPane (WiFiClient *clientp, char line[])
{
    // first arg is 1-based pane number
    int pane_1;
    char *equals;               // 
    if (sscanf (line, "Pane%d", &pane_1) != 1 || (equals = strchr(line,'=')) == NULL) {
        strcpy (line, garbcmd);
        return (false);
    }

    // convert pane_1 to PlotPane
    if (pane_1 < 1 || pane_1 > PANE_N) {
        strcpy_P (line, PSTR("Bad pane num"));
        return (false);
    }
    PlotPane pp = (PlotPane)(pane_1-1);

    // convert remaining args to list of PlotChoices
    PlotChoice pc[PLOT_CH_N];           // max size, only first n_pc in use
    int n_pc = 0;
    char *start = equals + 1;
    for (char *tok = NULL; (tok = strtok (start, ",")) != NULL; start = NULL) {

        // tok is with line, so copy it so we can use line for err msg
        char tok_copy[strlen(tok)+1];
        strcpy (tok_copy, tok);

        // find tok in plot_names
        PlotChoice tok_pc = PLOT_CH_NONE;
        for (int i = 0; i < PLOT_CH_N; i++) {
            if (strcmp (tok_copy, plot_names[i]) == 0) {
                tok_pc = (PlotChoice)i;
                break;
            }
        }

        // found?
        if (tok_pc == PLOT_CH_NONE) {
            sprintf (line, _FX("Unknown choice for pane %d: %s"), pane_1, tok_copy);
            return (false);
        }

        // in use elsewhere?
        PlotPane inuse_pp = findPaneForChoice(tok_pc);
        if (inuse_pp != PANE_NONE && inuse_pp != pp) {
            sprintf (line, _FX("%s already set in pane %d"), tok_copy, (int)inuse_pp+1);
            return (false);
        }

        // available?
        if (!plotChoiceIsAvailable(tok_pc)) {
            sprintf (line, _FX("%s is not available"), tok_copy);
            return (false);
        }

        // room for more?
        if (n_pc == PLOT_CH_N) {
            sprintf (line, _FX("too many choices"));
            return (false);
        }

        // ok!
        pc[n_pc++] = tok_pc;
    }

    // require at least 1
    if (n_pc == 0) {
        sprintf (line, _FX("specify at least one choice for pane %d"), pane_1);
        return (false);
    }

    // build new rotset
    plot_rotset[pp] = 0;
    for (int i = 0; i < n_pc; i++)
        plot_rotset[pp] |= (1 << pc[i]);

    // show first and persist
    if (!setPlotChoice (pp, pc[0])) {
        sprintf (line, _FX("%s failed for pane %d"), plot_names[pc[0]], pane_1);
        return (false);
    }
    logPaneRotSet(pp, pc[0]);
    savePlotOps();

    // ok!
    if (clientp) {
        startPlainText (*clientp);
        reportPaneChoices (clientp, pp);
    }

    // good
    return (true);
}



/* try to set the satellite to the given name.
 * return whether command is successful.
 */
static bool setWiFiSatName (WiFiClient *clientp, char line[])
{
    resetWatchdog();

    // do it
    if (setSatFromName (line)) {
        if (clientp)
            return (getWiFiSatellite (clientp, line));
        else
            return (true);
    }

    // nope
    strcpy (line, _FX("Unknown sat"));
    return (false);
}

/* set satellite from given TLE: set_sattle?name=n&t1=line1&t2=line2
 * return whether command is successful.
 */
static bool setWiFiSatTLE (WiFiClient *clientp, char line[])
{
    resetWatchdog();

    // find components
    char *name = strstr (line, "name=");
    char *t1 = strstr (line, "&t1=");
    char *t2 = strstr (line, "&t2=");
    if (!name || !t1 || !t2) {
        strcpy (line, garbcmd);
        return (false);
    }

    // break into proper separate strings
    name += 5; *t1 = '\0';
    t1 += 4; *t2 = '\0';
    t2 += 4;

    // enforce known line lengths
    size_t t1l = strlen(t1);
    if (t1l < TLE_LINEL-1) {
        strcpy (line, _FX("t1 short"));
        return(false);
    }
    t1[TLE_LINEL-1] = '\0';
    size_t t2l = strlen(t2);
    if (t2l < TLE_LINEL-1) {
        strcpy (line, _FX("t2 short"));
        return(false);
    }
    t2[TLE_LINEL-1] = '\0';

    // try to install
    if (setSatFromTLE (name, t1, t2)) {
        if (clientp)
            return (getWiFiSatellite (clientp, line));
        else
            return (true);
    }

    // nope
    strcpy (line, _FX("Bad spec"));
    return (false);
}

/* remote command to control stopwatch engine state
 */
static bool setWiFiStopwatch (WiFiClient *clientp, char line[])
{
    // crack
    SWEngineState sws;
    int mins;
    if (sscanf (line, "countdown=%d", &mins) == 1)
        sws = SWE_COUNTDOWN;
    else if (strcmp (line, "reset") == 0)
        sws = SWE_RESET;
    else if (strcmp (line, "run") == 0)
        sws = SWE_RUN;
    else if (strcmp (line, "stop") == 0)
        sws = SWE_STOP;
    else if (strcmp (line, "lap") == 0)
        sws = SWE_LAP;
    else {
        strcpy (line, garbcmd);
        return (false);
    }

    // engage
    if (!setSWEngineState (sws, mins*60000)) {        // mins -> ms
        strcpy (line, _FX("State is not applicable"));
        return (false);
    }

    // turn off any PLOT_CH_COUNTDOWN if no longer applicable
    insureCountdownPaneSensible();

    // ack
    if (clientp)
        return (getWiFiStopwatch (clientp, line));
    else
        return (true);
}

/* set clock time from any of three formats:
 *  ISO=YYYY-MM-DDTHH:MM:SS
 *  unix=s
 *  Now
 * return whether command is fully recognized.
 */
static bool setWiFiTime (WiFiClient *clientp, char line[])
{
    resetWatchdog();

    int yr, mo, dy, hr, mn, sc;

    if (strcmp (line, "Now") == 0) {

        changeTime (0);

    } else if (strncmp (line, "UNIX=", 5) == 0) {

        // crack and engage
        changeTime (atol(line+5));

    } else if (sscanf (line, _FX("ISO=%d-%d-%dT%d:%d:%d"), &yr, &mo, &dy, &hr, &mn, &sc) == 6) {

        // reformat
        tmElements_t tm;
        tm.Year = yr - 1970;
        tm.Month = mo;
        tm.Day = dy;
        tm.Hour = hr;
        tm.Minute = mn;
        tm.Second = sc;

        // convert and engage
        changeTime (makeTime(tm));

    } else {

        strcpy (line, garbcmd);
        return (false);
    }

    // reply
    if (clientp) {
        startPlainText(*clientp);
        char buf[30];
        snprintf (buf, sizeof(buf), "UNIX_time %ld\n", nowWO());
        clientp->print (buf);
    }

    return (true);
}

/* perform a touch screen action based on coordinates received via wifi GET
 * return whether all ok.
 */
static bool setWiFiTouch (WiFiClient *clientp, char line[])
{
    // crack raw screen x and y and optional hold
    int x, y, h = 0;
    if (sscanf (line, _FX("%*[xX]=%d&%*[yY]=%d&hold=%d"), &x, &y, &h) < 2) {
        strcpy (line, garbcmd);
        return (false);
    }

    // must be over display
    if (x < 0 || x >= tft.width() || y < 0 || y >= tft.height()) {
        strcpy (line, _FX("Invalid range"));
        return (false);
    }

    // inform checkTouch() to use wifi_tt_s; it will reset
    wifi_tt_s.x = x;
    wifi_tt_s.y = y;
    wifi_tt = h ? TT_HOLD : TT_TAP;

    // ack
    if (clientp) {
        startPlainText (*clientp);
        FWIFIPR (*clientp, F("Touch_x ")); clientp->println (wifi_tt_s.x);
        FWIFIPR (*clientp, F("Touch_y ")); clientp->println (wifi_tt_s.y);
    }

    // ok
    return (true);
}

/* set the VOACAP map to the given band and/or power
 * return whether all ok.
 */
static bool setWiFivoacap (WiFiClient *clientp, char line[])
{
    // look for each keyword, if any
    char *B = strstr (line, _FX("band="));
    char *P = strstr (line, _FX("power="));

    // look for unknown keywords
    for (char *sep = line-1, *kw = line; sep != NULL; sep = strchr (kw, '&'), kw = sep + 1) {
        if (*kw && P != kw && B != kw) {
            strcpy (line, garbcmd);
            return (false);
        }
    }

    // crack band PROP_MAP
    PropMapSetting pms = prop_map;
    if (B) {
        int band = 0;
        if (sscanf (B+5, "%d", &band) != 1) {
            strcpy (line, garbcmd);
            return (false);
        }
        // find its PROP_MAP
        bool found = false;
        for (int i = 0; i < PROP_MAP_N; i++) {
            if (propMap2Band((PropMapSetting)i) == band) {
                pms = (PropMapSetting)i;
                found = true;
                break;
            }
        }
        if (!found) {
            strcpy (line, _FX("Invalid band"));
            return (false);
        }
    }

    // crack power
    if (P) {
        float power, lp;
        if (sscanf (P+6, "%f", &power) != 1) {
            strcpy (line, garbcmd);
            return (false);
        }
        lp = log10f (power);
        if (lp < 0 || lp > 3 || lp != floor(lp)) {          // 1, 10, 100 or 1000
            strcpy (line, _FX("Invalid power"));
            return (false);
        }
        bc_power = (uint16_t)power;
    }

    // engage or revert
    PlotPane bc_pp = findPaneChoiceNow (PLOT_CH_BC);
    if (B || P) {
        if (bc_pp != PANE_NONE)
            checkBandConditions (plot_b[bc_pp], true);
        newVOACAPMap (pms);
    } else {
        newVOACAPMap (PROP_MAP_OFF);
        newCoreMap (core_map);
        if (bc_pp != PANE_NONE)
            plotBandConditions (plot_b[bc_pp], 0, NULL, NULL);
    }

    // ack
    if (clientp) {
        startPlainText (*clientp);
        char buf[50];
        size_t l = sprintf (buf, "VOACAP");
        if (B)
            l += sprintf (buf+l, " band %d m", propMap2Band(pms));
        if (P && B)
            l += sprintf (buf+l, ",");
        if (P)
            l += sprintf (buf+l, " power %d W", bc_power);
        if (!P && !B)
            l += sprintf (buf+l, " off");
        clientp->println(buf);
    }

    // ok
    return (true);
}



/* finish the wifi then restart
 */
static bool doWiFiReboot (WiFiClient *clientp, char *unused)
{
    (void) unused;

    // send html header then close
    startPlainText(*clientp);
    FWIFIPRLN (*clientp, F("restarting ... bye for now."));
    wdDelay(100);
    clientp->flush();
    clientp->stop();
    wdDelay(1000);

    Serial.println (F("restarting..."));
    reboot();

    // never returns but compiler doesn't know that
    return (true);
}

/* update firmware if available
 */
static bool doWiFiUpdate (WiFiClient *clientp, char *unused)
{
    (void) unused;

    // prep for response but won't be one if we succeed with update
    startPlainText(*clientp);

    // proceed if newer version is available
    char ver[50];
    if (newVersionIsAvailable (ver, sizeof(ver))) {
        char msg[100];
        snprintf (msg, sizeof(msg), "updating from %s to %s ... \n", hc_version, ver);
        clientp->print(msg);
        doOTAupdate(ver);                               // never returns if successful
        FWIFIPRLN (*clientp, F("update failed"));
    } else
        FWIFIPRLN (*clientp, F("You're up to date!"));    // match tapping version

    return (true);
}


#if defined(_IS_UNIX)
/* exit
 */
static bool doWiFiExit (WiFiClient *clientp, char *unused)
{
    (void) unused;

    // ack then die
    startPlainText(*clientp);
    FWIFIPRLN (*clientp, F("exiting"));

    Serial.print (F("Exiting\n"));
    setFullBrightness();
    eraseScreen();
    wdDelay(500);
    exit(0);

    // lint
    return (true);
}
#endif // defined(_IS_UNIX)



/* table of command strings, its implementing function and additional info for help.
 * functions are called with user input string beginning just after the command and sans HTTP.
 * N.B. ! functions returning false shall replace the input string with a brief error message.
 *      ! functions returning true shall send http reply to clientp unless NULL.
 *      ! get_ commands shall include trailing space to detect and prevent trailing garbage.
 *      ! table is down here so all handlers are already conveniently defined above.
 * strings are in arrays so they are in ESP FLASH too.
 */
#define CT_MAX_CMD      30                              // max command string length
#define CT_MAX_HELP     60                              // max help string length
#define CT_FUNP(ctp) ((PCTF)pgm_read_dword(&ctp->funp)) // handy function pointer
typedef bool (*PCTF)(WiFiClient *clientp, char *line);  // ptr to command table function
typedef struct {
    const char command[CT_MAX_CMD];                     // command string
    PCTF funp;                                          // handler function
    const char help[CT_MAX_HELP];                       // more info if available
} CmdTble;
static const CmdTble command_table[] PROGMEM = {
    { "get_capture.bmp ",   getWiFiScreenCapture,  "get live screen shot" },
    { "get_config.txt ",    getWiFiConfig,         "get current display options" },
    { "get_de.txt ",        getWiFiDEInfo,         "get DE info" },
    { "get_dx.txt ",        getWiFiDXInfo,         "get DX info" },
    { "get_dxspots.txt ",   getWiFiDXSpots,        "get DX spots" },
    { "get_satellite.txt ", getWiFiSatellite,      "get current sat info" },
    { "get_satellites.txt ",getWiFiAllSatellites,  "get list of all sats" },
    { "get_sensors.txt ",   getWiFiSensorData,     "get sensor data" },
    { "get_spacewx.txt ",   getWiFiSpaceWx,        "get space weather info" },
    { "get_stopwatch.txt ", getWiFiStopwatch,      "get stopwatch state" },
    { "get_sys.txt ",       getWiFiSys,            "get system stats" },
    { "get_time.txt ",      getWiFiTime,           "get current time" },
    { "set_alarm?",         setWiFiAlarm,          "state=off|armed&time=HR:MN" },
    { "set_antennaheading?",setWiFiAntennaHeading, "heading=degrees,width=degrees" },
    { "set_displayOnOff?",  setWiFiDisplayOnOff,   "on|off" },
    { "set_displayTimes?",  setWiFiDisplayTimes,   "on=HR:MN&off=HR:MN&day=DOW&idle=mins" },
    { "set_mapview?",       setWiFiMapView,        "Style=S&Grid=G&Projection=P&RSS=on|off&Night=on|off" },
    { "set_newde?",         setWiFiNewDE,          "lat=X&lng=Y" },
    { "set_newdegrid?",     setWiFiNewDEGrid,      "AB12" },
    { "set_newdx?",         setWiFiNewDX,          "lat=X&lng=Y" },
    { "set_newdxgrid?",     setWiFiNewDXGrid,      "AB12" },
    { "set_pane?",          setWiFiPane,           "Pane[123]=X,Y,Z... any from:" },
    { "set_satname?",       setWiFiSatName,        "abc|none" },
    { "set_sattle?",        setWiFiSatTLE,         "name=abc&t1=line1&t2=line2" },
    { "set_stopwatch?",     setWiFiStopwatch,      "reset|run|stop|lap|countdown=mins" },
    { "set_time?",          setWiFiTime,           "ISO=YYYY-MM-DDTHH:MM:SS" },
    { "set_time?",          setWiFiTime,           "Now" },
    { "set_time?",          setWiFiTime,           "unix=secs_since_1970" },
    { "set_title?",         setWiFiTitle,          "msg=hello&fg=R,G,B&bg=R,G,B|rainbow" },
    { "set_touch?",         setWiFiTouch,          "x=X&y=Y&hold=0|1" },
    { "set_voacap?",        setWiFivoacap,         "band=80-10&power=p" },
    { "restart ",           doWiFiReboot,          "restart HamClock" },
    { "updateVersion ",     doWiFiUpdate,          "update to latest version"},
#if defined(_IS_UNIX)
    { "exit ",              doWiFiExit,            "exit HamClock" },
#endif // defined(_IS_UNIX)
};
#define N_CT NARRAY(command_table)                      // n entries in command table

/* return whether the given command is allowed in read-only web service
 */
static bool roCommand (const char *cmd)
{
    return (strncmp (cmd, "get_", 4) == 0
                    || strncmp (cmd, "set_alarm", 9) == 0
                    || strncmp (cmd, "set_stopwatch", 13) == 0
                    || strncmp (cmd, "set_touch", 9) == 0);
}


/* run the given web server command.
 * if coming from external client, clientp will be set and we return messages there; if coming
 *    internally, clientp will be NULL and we just log messagaes.
 * return whether command was found, regardless of whether it returned an error.
 */
static bool runWebserverCommand (WiFiClient *clientp, bool ro, char *command)
{
    // search for command depending on context, execute its implementation function if found
    if (!ro || roCommand (command)) {
        resetWatchdog();
        for (unsigned i = 0; i < N_CT; i++) {
            const CmdTble *ctp = &command_table[i];
            size_t cmd_len = strlen_P (ctp->command);
            if (strncmp_P (command, ctp->command, cmd_len) == 0) {

                // found command, skip to start of args
                char *args = command+cmd_len;

                // replace any %20
                replaceBlankEntity (args);

                // chop off trailing HTTP _after_ looking for commands because get_ commands end with blank.
                char *http = strstr (args, " HTTP");
                if (http)
                    *http = '\0';

                // run handler, passing string starting right after the command, reply with error if trouble.
                PCTF funp = CT_FUNP(ctp);
                if (!(*funp)(clientp, args) && clientp)
                    sendHTTPError (*clientp, args);

                // command found, even if it reported an error
                return (true);
            }
        }
    }

    // not found (or allowed)
    return (false);
}

/* service remote connection.
 * if ro, only accept get commands and set_touch
 */
static void serveRemote(WiFiClient *clientp, bool ro)
{
    StackMalloc line_mem(TLE_LINEL*4);          // accommodate longest query, probably set_sattle with %20s
    char *line = (char *) line_mem.getMem();    // handy access to malloced buffer
    char *skipget = line+5;                     // handy location within line[] after "GET /"

    // first line must be the GET
    if (!getTCPLine (*clientp, line, line_mem.getSize(), NULL)) {
        sendHTTPError (*clientp, "empty web query");
        goto out;
    }
    if (strncmp (line, "GET /", 5)) {
        Serial.println (line);
        sendHTTPError (*clientp, "Method Not Allowed");
        goto out;
    }

    // discard remainder of header
    (void) httpSkipHeader (*clientp);

    Serial.print (F("Command from "));
        Serial.print(clientp->remoteIP());
        Serial.print(F(": "));
        Serial.println(line);

    // run command
    if (runWebserverCommand (clientp, ro, skipget))
        goto out;

    // if get here, command was not found so list help
    startPlainText(*clientp);
    for (uint8_t i = 0; i < N_CT; i++) {
        const CmdTble *ctp = &command_table[i];

        // skip if not available for ro
        char ramcmd[CT_MAX_CMD];
        strcpy_P (ramcmd, ctp->command);
        if (ro && !roCommand(ramcmd))
            continue;

        // command followed by help in separate column
        const int indent = 30;
        int cmd_len = strlen (ramcmd);
        clientp->print (ramcmd);
        sprintf (line, "%*s", indent-cmd_len, "");
        clientp->print (line);
        clientp->println (FPSTR(ctp->help));

        // also list pane choices for setWiFiPane
        PCTF funp = CT_FUNP(ctp);
        if (funp == setWiFiPane) {
            const int max_w = 70;
            const char indent[] = "  ";
            int ll = 0;
            for (int i = 0; i < PLOT_CH_N; i++) {
                if (ll == 0)
                    ll = sprintf (line, "%s", indent);
                ll += sprintf (line+ll, " %s", plot_names[i]);
                if (ll > max_w) {
                    clientp->println (line);
                    ll = 0;
                }
            }
            clientp->println (line);
        }
    }

  out:

    clientp->stop();
    printFreeHeap (F("serveRemote"));
}

void checkWebServer()
{
    // check if someone is trying to tell/ask us something
    if (remoteServer) {
        WiFiClient client = remoteServer->available();
        if (client)
            serveRemote(&client, false);
    }
}

void initWebServer()
{
    resetWatchdog();

    if (remoteServer) {
        remoteServer->stop();
        delete remoteServer;
    }

    remoteServer = new WiFiServer(svr_port);
    remoteServer->begin();
}

/* like readCalTouch() but also checks for remote web server touch.
 */
TouchType readCalTouchWS (SCoord &s)
{
    // check for read-only remote commands
    // N.B. might be called before server is set up, eg, a very early fatalError
    if (remoteServer) {
        WiFiClient client = remoteServer->available();
        if (client)
            serveRemote (&client, true);
    }

    // return info for remote else local touch
    TouchType tt;
    if (wifi_tt != TT_NONE) {
        s = wifi_tt_s;
        tt = wifi_tt;
        wifi_tt = TT_NONE;
    } else {
        tt = readCalTouch (s);
    }

    // return event type
    return (tt);
}


/* called from main loop() to run another demo command if time.
 */
void runNextDemoCommand()
{
    // out fast if not using this mode
    if (!getDemoMode())
        return;

    // otherwise only occasionally
    #define DEMO_DT     30000                   // demo update interval, ms
    #if defined(_IS_ESP8266)
        #define ESPASM_DT  60000                // allow extra time for ESP Azm map, ms
    #else
        #define ESPASM_DT 0
    #endif

    // wait for every DEMO_DT
    static uint32_t demo_t;
    if (!timesUp (&demo_t, DEMO_DT))
        return;

    // possible set_mapview settings
    static const char *map_style[] = {"Countries", "Terrain", "DRAP"};
    static const char *map_grid[] = {"None", "Tropics", "Lat/Long", "Maidenhead"};

    // make a random choice, avoid consecutive dups and save ESP FLASH with fewer style changes
    #define DEMO_N_CHOICES 14
    static uint8_t prev_choice;
    uint8_t choice;
    do {
        choice = (uint8_t) random(DEMO_N_CHOICES);
    } while (choice == prev_choice || (choice == 8 && random(10) < 8));
    prev_choice = choice;

    // run choice
    char cmd[100];
    switch (choice) {
    case 0:     // change plot pane 1
        snprintf (cmd, sizeof(cmd), "set_pane?Pane1=%s", plot_names[getAnyAvailableChoice()]);
        break;
    case 1:     // change plot pane 2
        snprintf (cmd, sizeof(cmd), "set_pane?Pane2=%s", plot_names[getAnyAvailableChoice()]);
        break;
    case 2:     // change plot pane 3
        snprintf (cmd, sizeof(cmd), "set_pane?Pane3=%s", plot_names[getAnyAvailableChoice()]);
        break;
    case 3:     // toggle RSS
        snprintf (cmd, sizeof(cmd), "set_mapview?RSS=%s", rss_on ? "off" : "on");
        if (rss_on && azm_on)  // turning RSS off with azm
            demo_t += ESPASM_DT;
        break;
    case 4:     // set new dx location, avoid poles just for aesthetics
        snprintf (cmd, sizeof(cmd), "set_newdx?lat=%ld&lng=%ld", random(120)-60, random(359)-180);
        break;
    case 5:     // change Projection
        snprintf (cmd, sizeof(cmd), "set_mapview?Projection=%s", azm_on ? "Mercator" : "Azimuthal");
        if (!azm_on)    // changing to azm
            demo_t += ESPASM_DT;
        break;
    case 6:     // change night
        snprintf (cmd, sizeof(cmd), "set_mapview?Night=%s", night_on ? "off" : "on");
        if (azm_on)
            demo_t += ESPASM_DT;
        break;
    case 7:     // change Grid
        snprintf (cmd, sizeof(cmd), "set_mapview?Grid=%s", map_grid[(mapgrid_choice+1)%MAPGRID_N]);
        if (azm_on)
            demo_t += ESPASM_DT;
        break;
    case 8:     // change Style
        snprintf (cmd, sizeof(cmd), "set_mapview?Style=%s", map_style[(core_map+1)%CM_N]);
        if (azm_on)
            demo_t += ESPASM_DT;
        break;
    case 9:     // upper right contents
        snprintf (cmd, sizeof(cmd), "set_touch?x=764&y=13");
        break;
    case 10:     // DE pane style
        snprintf (cmd, sizeof(cmd), "set_touch?x=15&y=180");
        break;
    case 11:     // DX sun times
        snprintf (cmd, sizeof(cmd), "set_touch?x=104&y=421");
        break;
    case 12:     // call fg
        snprintf (cmd, sizeof(cmd), "set_touch?x=58&y=13");
        break;
    case 13:     // call bg
        snprintf (cmd, sizeof(cmd), "set_touch?x=208&y=13");
        break;
    default:
        fatalError (_FX("Bug! bogus demo choice: %d / %d"), choice, DEMO_N_CHOICES);
        // never returns
    }

    // run
    Serial.printf ("Demo @ %lu s: %s\n", millis()/1000U, cmd);
    if (!runWebserverCommand (NULL, false, cmd))
        fatalError ("Bug! demo command failed: %s", cmd);
}
