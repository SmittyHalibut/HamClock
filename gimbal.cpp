/* Manage the gimbal/rotator GUI and communicate with the Auxiliary Rotator Control module.
 *
 * We first watch for UDP multicast on 239.9.8.7:7625 from ARC. Once we know its IP and port we make a TCP
 * connection for rotator control. This repeats if we ever loose contact. See ARC for protocol details.
 *
 * If only 1 axis is found we rotate it to point at DX. With 2 axes, we use both to track satellites.
 * TODO: somebody may want to track sat az with their yagi?
 *
 * Some gimbals can move 0-180 in elevation. If so, satellites that pass through north are tracked
 * "upside down" to avoid unwrapping az through north. Gimbals without this capability will incure
 * a lengthy unwrap if the sat moves through north.
 *
 * To be on the safe side, all motion is stopped unless the Gimbal plot pane is visible. If decide later to
 * leave it run note earthsat.cpp turns off tracking any time a new sat might be selected.
 */

#include "HamClock.h"


// uncomment the #define to get additional performance information
// #define WANT__TRACE_IO
#if defined(WANT__TRACE_IO)
#define _TRACE_IO(x)        Serial.printf x
#else
#define _TRACE_IO(x)        do{}while(0)
#endif


// listen for UDP multicast from ARC to get server host address then connect with TCP socket
static WiFiClient arc_client;                           // TCP ARC connection
#define MC_IPA          239                             // multicast ip address octet 1
#define MC_IPB          9                               // multicast ip address octet 2
#define MC_IPC          8                               // multicast ip address octet 3
#define MC_IPD          7                               // multicast ip address octet 4
#define MC_PORT         7625                            // multicast port
#define MIN_MC_TRY      10                              // n times to try minimally listening for multicast
#define MAX_MC_TRY      20                              // n times to try hard listening for multicast
#define MC_TRY_PERIOD   100                             // time between trys, ms
#define MAX_SNDRETRY    3                               // n times to retry sending command

// GUI configuration
#define CHAR_H          25                              // large character height
#define TITLE_Y         (box.y+PLOTBOX_H/5-2)           // title y coord, match VOCAP
#define VERSION_Y       (TITLE_Y+3)                     // version y coord
#define VALU_INDENT     40                              // az or el value indent
#define STATE_INDENT    98                              // az or el state indent
#define DIRBOX_SZ       11                              // direction control box size
#define DIRBOX_GAP       2                              // gap between control box pairs
#define AUTO_Y          (box.y+PLOTBOX_H-CHAR_H-15)     // auto button y coord
#define ARROW_COLOR     RA8875_CYAN                     // color for directional arrow controls
#define UPOVER_COLOR    RA8875_RED                      // upover symbol color
#define UPDATE_MS       950                             // command update interval, ms
#define AZSTEP          az_deadband                     // small az manual step size
#define AZSTEP2         20                              // large az manual step size
#define ELSTEP          el_deadband                     // small el manual step size
#define ELSTEP2         10                              // large el manual step size
#define MSG_DWELL       5000                            // error message display period, ms

// possible axis states
typedef enum {
    AZS_UNKNOWN,                                        // unknown
    AZS_STOPPED,                                        // stopped
    AZS_CWROT,                                          // rotating cw as seen from above
    AZS_CCWROT,                                         // rotating ccw "
    AZS_CCWLIMIT,                                       // at minimum rotation limit
    AZS_CWLIMIT,                                        // at maximum rotation limit
    AZS_INPOS,                                          // at commanded az
    AZS_NONE,                                           // no az axis
} AzState;
typedef enum {
    ELS_UNKNOWN,                                        // unknown
    ELS_STOPPED,                                        // stopped
    ELS_UPROT,                                          // el angle increasing
    ELS_DOWNROT,                                        // el angle decreasing
    ELS_UPLIMIT,                                        // at minimum rotation angle
    ELS_DOWNLIMIT,                                      // at maximum rotation angle
    ELS_INPOS,                                          // at commanded el
    ELS_NONE,                                           // no el axis
} ElState;
typedef enum {
    AR_LEFT,
    AR_DOWN,
    AR_UP,
    AR_RIGHT
} ArrowDir;

// controls and state
static uint16_t AZ_Y, EL_Y;                             // drawing locations
static SBox azccw_b, azcw_b, azccw2_b, azcw2_b;         // manual az ccw and cw buttons
static SBox elup_b, eldown_b, elup2_b, eldown2_b;       // manual el up and down buttons
static SBox auto_b;                                     // tracking button
static SBox stop_b;                                     // stop button
static bool auto_track;                                 // whether we track a target, else only manual input
static bool sat_upover;                                 // whether using el > 90 to avoid wrapping thru N
static bool upover_pending;                             // avoid sat el near SAT_MIN_EL
static bool user_stop;                                  // user has commanded stop
static float az_target, el_target;                      // target now, degrees
static float az_now, el_now;                            // gimbal now, degrees
static float az_deadband;                               // max az error ignored
static float az_mnt0;                                   // az of mount fully CCW
static float el_min, el_max;                            // el command limits
static float el_deadband;                               // max el error ignored
static AzState az_state;                                // az run state now
static ElState el_state;                                // el run state now
static int16_t paz_target, pel_target;                  // previous az and el target, degrees
static int16_t paz_now, pel_now;                        // previous gimbal az and el, degrees
static AzState paz_state;                               // previous az run state
static ElState pel_state;                               // previous el run state
static char title[14];                                  // title from model
static char version[10];                                // version from model
static bool send_now;                                   // whether to send current time

// misc forward declarations
static bool doARCMessage (const SBox &box, char *resp, int maxresp_l, const char *msgfmt, ...);
static bool getARCFloat (const SBox &box, const char *msg, float *valp);
static bool getAz(const SBox &box);
static bool getEl(const SBox &box);


/* return whether the clock is providing correct time
 */
static bool goodTime()
{
    return (utcOffset() == 0 && clockTimeOk());
}

/* return whether are currently connected to ARC server
 */
static bool ARCIsConnected()
{
    return (arc_client);
}

/* insure disconnected from ARC server
 */
void closeGimbal()
{
    if (ARCIsConnected()) {
        Serial.printf (_FX("ARC: %lu disconnecting\n"), now());
        arc_client.stop();
    }
}

/* return whether arc_client is successfully connected to ARC.
 * if not, listen at most ntry times for its multicast then try to connect.
 * print any err info in box.
 * return success immediately if already connected.
 */
static bool connectARCOk(const SBox &box, uint8_t ntry)
{
    // success if already connected
    if (ARCIsConnected())
        return (true);

    // pointless going further if no wifi
    if (!wifiOk())
        return (false);

    _TRACE_IO (("ARC: %lu starting connection attempt\n", now()));

    // need UDP to receive multicast
    WiFiUDP arc_mc;

    // misc buffers
    StackMalloc msg_mem(150);
    char *msg = msg_mem.getMem();
    StackMalloc buf_mem(150);
    char *buf = buf_mem.getMem();

    // check several times for ARC multicast, connect if found
    uint8_t t;
    for (t = 0; t < ntry; t++) {

        // insure we can listen for multicast, else give up
        if (!arc_mc && !arc_mc.beginMulticast(WiFi.localIP(),IPAddress(MC_IPA,MC_IPB,MC_IPC,MC_IPD),MC_PORT)){
            Serial.println (F("ARC: multicast listen fail"));
            return (false);
        }

        _TRACE_IO (("ARC: %lu try %d checking for mc packet\n", now(), t));

        if (arc_mc.parsePacket()) {

            _TRACE_IO (("ARC: %lu found mc\n", now()));

            // read packet, extract port
            uint16_t msgl = arc_mc.read ((uint8_t*)msg, msg_mem.getSize()-1);
            IPAddress rip = arc_mc.remoteIP();
            msg[msgl] = '\0';
            int port;
            if (msgl < 6 || sscanf (msg, _FX("port %d"), &port) != 1) {
                Serial.printf(_FX("ARC: bad multicast message: %s"), msg);
                continue;
            }
            Serial.printf (_FX("ARC: %lu found server at %u.%u.%u.%u:%d\n"),
                        now(), rip[0], rip[1], rip[2], rip[3], port);

            // ready to connect but first close udp connection so two are not open together
            arc_mc.stop();

            // connect to arc server
            if (arc_client.connect (rip, port)) {

                _TRACE_IO (("ARC: %lu connected to server\n", now()));

                // disable Nagle for immediate transmission
                arc_client.setNoDelay(true);

                // start
                if (!doARCMessage (box, buf, buf_mem.getSize(), "start")) {
                    closeGimbal();
                    continue;
                }
                if (strncmp (buf, "OK ", 3)) {
                    Serial.printf (_FX("ARC: %s\n"), buf);
                    plotMessage (box, RA8875_RED, buf);
                    wdDelay(MSG_DWELL);
                    closeGimbal();
                    break;
                }

                // get model
                if (!doARCMessage (box, buf, buf_mem.getSize(), "get model")) {
                    closeGimbal();
                    continue;
                }
                if (strncmp (buf, "OK ", 3)) {
                    Serial.printf (_FX("ARC: %s\n"), buf);
                    plotMessage (box, RA8875_RED, buf);
                    wdDelay(MSG_DWELL);
                    closeGimbal();
                    break;
                }

                // save model as title
                strncpy (title, buf+9, sizeof(title)-1);

                // get version
                float v;
                if (!getARCFloat (box, _FX("get version"), &v)) {
                    closeGimbal();
                    continue;
                }
                sprintf (version, _FX("Ver %.2f"), v);

                // init axes and hold here, just probe for optionl El, not fatal if it errors
                if (!getAz(box)) {
                    // already posted error
                    wdDelay(MSG_DWELL);
                    closeGimbal();
                    break;
                }
                (void) getEl(box);
                stopGimbalNow();

                // friendly show az_mnt0
                displaySatInfo();

                // send fresh time next opportunity
                send_now = true;

                // made it!
                return (true);
            }
        }

        wdDelay (MC_TRY_PERIOD);
    }

    // fail or not ready
    if (t < ntry)
        Serial.println (F("ARC: server not ready"));
    else
        Serial.println (F("ARC: no server found"));
    arc_mc.stop();
    return (false);
}

/* send the given rotator command string and return complete response.
 * return true if io ok, else log reason and show in box, close arc_client and return false.
 * eg: get model -> OK model Yaesu G5500
 * N.B. msg shall NOT include \n
 * N.B. return bool just reflects whether proper io occured, not whether response started with OK
 */
static bool doARCMessage (const SBox &box, char *resp, int maxresp_l, const char *msgfmt, ...)
{
        // require connection
        if (!ARCIsConnected())
            return (false);

        // build message
        char msg[128];
        va_list ap;
        va_start (ap, msgfmt);
        vsnprintf (msg, sizeof(msg), msgfmt, ap);
        va_end (ap);

        // send with a few retrys
        for (uint8_t t = 1; t <= MAX_SNDRETRY; t++) {

            // send message
            _TRACE_IO (("ARC: %lu TX %d: %s\n", now(), t, msg));
            arc_client.println(msg);

            // get response
            if (getTCPLine (arc_client, resp, maxresp_l, NULL)) {
                _TRACE_IO (("ARC: %lu RX %d: %s\n", now(), t, resp));
                return (true);
            }

            // failed, try sending again then expect two responses
            Serial.printf (_FX("ARC: %lu TXB %d: %s\n"), now(), t, msg);
            arc_client.println(msg);

            if (getTCPLine (arc_client, resp, maxresp_l, NULL)
                        && getTCPLine (arc_client, resp, maxresp_l, NULL)) {
                Serial.printf (_FX("ARC: %lu RXB %d: %s\n"), now(), t, resp);
                return (true);
            }

            // still fails, try closing and reconnect
            Serial.printf (_FX("ARC: %lu try %d reconnecting\n"), now(), t);
            closeGimbal();
            if (!connectARCOk(box, MAX_MC_TRY))
                break;                          // give up
        }

        // retry efforts failed
        Serial.printf (_FX("ARC: no response to %s\n"), msg);
        plotMessage (box, RA8875_RED, _FX("Connection lost"));
        closeGimbal();
        return (false);
}

/* send the given get command that returns a floating value and return value.
 * if io trouble then log and show in box, close and return false; else if starts with OK return
 *   value and true; else log and return false.
 * eg:  get az -> OK az 123
 * N.B. msg shall NOT include \n
 */
static bool getARCFloat (const SBox &box, const char *msg, float *valp)
{
        // send and collect response
        StackMalloc resp_mem(150);
        char *resp = resp_mem.getMem();
        if (!doARCMessage (box, resp, resp_mem.getSize(), "%s", msg))
            return (false);                     // reason already logged

        // check response OK and echo
        if (sscanf (resp, _FX("OK %*s %f"), valp) != 1 || strncmp (&resp[3], &msg[4], strlen(&msg[4]))) {
            Serial.printf (_FX("ARC: %s\n"), resp);
            plotMessage (box, RA8875_RED, resp);
            closeGimbal();
            return (false);
        }

        // looks good
        return (true);
}

/* send the given command that sets a floating value and return whether it was acked ok.
 * if io trouble then log and show in box, close arc_client and return false.
 * eg: set az 123 -> OK set az 123
 * N.B. msg shall NOT include \n
 */
static bool setARCFloat (const SBox &box, const char *msg, float val)
{
        // send and collect response
        StackMalloc resp_mem(150);
        char *resp = resp_mem.getMem();
        if (!doARCMessage (box, resp, resp_mem.getSize(), "%s %g", msg, val))
            return (false);                     // reason already logged

        // check response OK and echo
        if (strncmp (resp, "OK ", 3) || strncmp (&resp[3], msg, strlen(msg))) {
            Serial.printf (_FX("ARC: %s\n"), resp);
            plotMessage (box, RA8875_RED, resp);
            closeGimbal();
            return (false);
        }

        // ok
        return (true);
}

/* get az position and related info.
 * this axis is required so close if trouble.
 * log and report errors in box
 */
static bool getAz(const SBox &box)
{
    StackMalloc resp_mem(150);
    char *resp = resp_mem.getMem();
    char moving[10], ataz[10], atlim[10];

    if (doARCMessage (box, resp, resp_mem.getSize(), "get az")) {

        float azm0;

        if (sscanf (resp, _FX("OK az %f moving %10s ataz %10s deadband %f atlimit %10s az0 %f"),
                        &az_now, moving, ataz, &az_deadband, atlim, &azm0) == 6) {


            // friendly print and sat az_mnt0 when new
            if (azm0 != az_mnt0) {
                az_mnt0 = azm0;
                displaySatInfo();
                Serial.printf (_FX("ARC: az_mnt0 %g\n"), az_mnt0);
            }

            if (strcmp (ataz, "YES") == 0)
                az_state = AZS_INPOS;
            else if (strcmp (atlim, "CW") == 0)
                az_state = AZS_CWLIMIT;
            else if (strcmp (atlim, "CCW") == 0)
                az_state = AZS_CCWLIMIT;
            else if (strcmp (moving, "CW") == 0)
                az_state = AZS_CWROT;
            else if (strcmp (moving, "CCW") == 0)
                az_state = AZS_CCWROT;
            else if (strcmp (moving, "NO") == 0)
                az_state = AZS_STOPPED;
            return (true);

        } else {

            Serial.printf (_FX("ARC: %s\n"), resp);
            plotMessage (box, RA8875_RED, resp);
            closeGimbal();
            return (false);
        }

    } else {

        // doARCMessage already closed connection and posted reason
        az_state = AZS_NONE;
        return (false);
    }
}

/* get el position and related info.
 * this axis is optional, so just mark state ELS_NONE if error 
 */
static bool getEl(const SBox &box)
{
    StackMalloc resp_mem(150);
    char *resp = resp_mem.getMem();
    char moving[10], atel[10], atlim[10];

    if (doARCMessage (box, resp, resp_mem.getSize(), "get el")) {
        
        if (sscanf (resp, _FX("OK el %f moving %10s atel %10s min %f max %f deadband %f atlimit %10s"),
                        &el_now, moving, atel, &el_min, &el_max, &el_deadband, atlim) == 7) {

            if (strcmp (atel, "YES") == 0)
                el_state = ELS_INPOS;
            else if (strcmp (atlim, "UP") == 0)
                el_state = ELS_UPLIMIT;
            else if (strcmp (atlim, "DOWN") == 0)
                el_state = ELS_DOWNLIMIT;
            else if (strcmp (moving, "UP") == 0)
                el_state = ELS_UPROT;
            else if (strcmp (moving, "DOWN") == 0)
                el_state = ELS_DOWNROT;
            else if (strcmp (moving, "NO") == 0)
                el_state = ELS_STOPPED;
            return (true);

        } else {

            el_state = ELS_NONE;
            return (false);
        }

    } else {

        // doARCMessage already closed connection and posted reason
        el_state = ELS_NONE;
        return (false);
    }
}

/* return whether a satellite with the given rise and set azimuths will pass through az_mnt0.
 * N.B. we assume no satellite orbit can ever subtend more than 180 in az.
 * The moon can be up for wider than 180 degrees but always east to west.
 */
static bool passesThruWrap (float raz, float saz, bool isMoon)
{

    if (isMoon) {

        // TODO: eg same raz 100 saz 260 but could go N or S of zenith
        return (az_mnt0 > raz && az_mnt0 < saz);

    } else {

        // normalize as if az_mnt0 was north
        raz = fmodf (raz - az_mnt0 + 720, 360);
        saz = fmodf (saz - az_mnt0 + 720, 360);

        return ((raz > 180 && saz < raz - 180) || (raz < 180 && saz > raz + 180));
    }
}

/* draw current Track button state with message msg, else default.
 */
static void drawTrackButton(bool force, const char *msg)
{
    // decide string to draw
    const char *str = msg ? msg : "Auto";

    // avoid flashing from redrawing the same string in the same state
    static char prev_str[15];
    static bool prev_track;
    if (!force && prev_str[0] != 0 && strcmp (str, prev_str) == 0 && prev_track == auto_track)
        return;
    strncpy (prev_str, str, sizeof(prev_str)-1);                // preserve EOS
    prev_track = auto_track;

    // prepare button
    if (auto_track) {
        tft.fillRect (auto_b.x, auto_b.y, auto_b.w, auto_b.h, RA8875_WHITE);
        tft.setTextColor (RA8875_BLACK);
    } else {
        tft.fillRect (auto_b.x, auto_b.y, auto_b.w, auto_b.h, RA8875_BLACK);
        tft.drawRect (auto_b.x, auto_b.y, auto_b.w, auto_b.h, RA8875_WHITE);
        tft.setTextColor (msg ? RA8875_RED : RA8875_WHITE);
    }

    // draw string
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    uint16_t sw = getTextWidth ((char*)str);
    tft.setCursor (auto_b.x+(auto_b.w-sw)/2, auto_b.y+3);
    tft.print (str);

    // wait a moment if message is temporary
    if (msg)
        wdDelay(1500);
}

/* draw Stop button in the given state
 */
static void drawStopButton (bool stop)
{
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    if (stop) {
        tft.fillRect (stop_b.x, stop_b.y, stop_b.w, stop_b.h, RA8875_WHITE);
        tft.setTextColor (RA8875_BLACK);
    } else {
        tft.fillRect (stop_b.x, stop_b.y, stop_b.w, stop_b.h, RA8875_BLACK);
        tft.drawRect (stop_b.x, stop_b.y, stop_b.w, stop_b.h, RA8875_WHITE);
        tft.setTextColor (RA8875_WHITE);
    }
    tft.setCursor (stop_b.x+7, stop_b.y+3);
    tft.print (F("Stop"));
}

/* draw info for one axis in box.
 * N.B. we assume initGimbalGUI() has already been called.
 */
static void drawAxisInfo (const SBox &box, float target_value, float value_now, SBox &lbox, SBox &rbox,
uint16_t y0, const char *state_str, uint16_t state_color)
{
    // erase from indent to end of box
    tft.fillRect (box.x+VALU_INDENT, y0, box.w-VALU_INDENT-1, CHAR_H+1, RA8875_BLACK);

    // show value now
    char buf[10];
    snprintf (buf, sizeof(buf), _FX("%4.0f"), value_now);
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (RA8875_WHITE);
    tft.setCursor (box.x+VALU_INDENT, y0+CHAR_H);
    tft.print(buf);

    // show state
    tft.setTextColor (state_color);
    tft.setCursor (box.x + STATE_INDENT, y0+CHAR_H);
    tft.print(state_str);

    // show target value between l and r boxes
    uint16_t x_l = lbox.x + lbox.w + 1;
    tft.fillRect (x_l, lbox.y, rbox.x - x_l, lbox.h, RA8875_BLACK);
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (RA8875_WHITE);
    snprintf (buf, sizeof(buf), _FX("%4.0f"), target_value);
    tft.setCursor (x_l + 7, lbox.y+2);
    tft.print (buf);

    if (y0 == AZ_Y) {
        // show az_mnt0
        x_l = azcw2_b.x + azcw2_b.w + 10;
        uint16_t w = box.x + box.w - x_l - 1;
        tft.fillRect (x_l, lbox.y, w, lbox.h, RA8875_BLACK);
        snprintf (buf, sizeof(buf), _FX("W@%.0f"), az_mnt0);
        tft.setCursor (x_l, lbox.y+2);
        tft.print (buf);
    }

}

/* draw or erase the up-and-over symbol
 */
static void drawUpOver()
{
    uint16_t r = elup2_b.h - 3;          // too tall and it hits the tracking button
    uint16_t x_c = elup2_b.x + 30;
    uint16_t y_c = elup2_b.y + elup2_b.h - 2;

    if (el_target > 90 || el_now > 90) {
        tft.drawCircle (x_c, y_c, r, UPOVER_COLOR);
        tft.drawLine (x_c+r, y_c+1, x_c+5*r/4, y_c-r/2, UPOVER_COLOR);
        tft.drawLine (x_c+r, y_c+1, x_c+r/2, y_c-r/2, UPOVER_COLOR);
        tft.fillRect (x_c-r-2, y_c+1, 2*r+4, r, RA8875_BLACK);
    } else {
        tft.fillRect (x_c-r-2, y_c-r-2, 9*r/4+4, r+4, RA8875_BLACK);
    }
}

/* draw current state of gimbal in box, avoid needless redraws by comparing with previous values.
 * N.B. we assume initGimbalGUI() has already been called.
 */
static void updateGUI(const SBox &box)
{
    // find az state description
    uint16_t color = 0;
    const char *str = NULL;
    switch (az_state) {
    case AZS_STOPPED:
        color = BRGRAY;
        str = "Idle";
        break;
    case AZS_CWROT:
        color = RA8875_YELLOW;
        str = " CW";
        break;
    case AZS_CCWROT:
        color = RA8875_YELLOW;
        str = "CCW";
        break;
    case AZS_CCWLIMIT:
        color = RA8875_RED;
        str = "Min";
        break;
    case AZS_CWLIMIT:
        color = RA8875_RED;
        str = "Max";
        break;
    case AZS_INPOS:
        color = RA8875_GREEN;
        str = " Ok";
        break;
    case AZS_UNKNOWN:
        color = RA8875_RED;
        str = "???";
        break;
    case AZS_NONE:
        return;
    }

    // draw if changed enough
    if (az_state != paz_state || roundf(az_target) != paz_target || roundf(az_now) != paz_now) {
        drawAxisInfo (box, az_target, az_now, azccw_b, azcw_b, AZ_Y, str, color);
        paz_state = az_state;
        paz_target = roundf(az_target);
        paz_now = roundf(az_now);
    }

    // show el state if gimbal
    if (el_state != ELS_NONE) {
        switch (el_state) {
        case ELS_STOPPED:
            color = BRGRAY;
            str = "Idle";
            break;
        case ELS_UPROT:
            color = RA8875_YELLOW;
            str = " UP";         // avoid p descender
            break;
        case ELS_DOWNROT:
            color = RA8875_YELLOW;
            str = "Down";
            break;
        case ELS_DOWNLIMIT:
            color = RA8875_RED;
            str = "Min";
            break;
        case ELS_UPLIMIT:
            color = RA8875_RED;
            str = "Max";
            break;
        case ELS_INPOS:
            color = RA8875_GREEN;
            str = " Ok";
            break;
        case ELS_UNKNOWN:
            color = RA8875_RED;
            str = "???";
            break;
        case ELS_NONE:
            break;
        }

        // draw if changed enough
        if (el_state != pel_state || roundf(el_target) != pel_target || roundf(el_now)!= pel_now) {
            drawAxisInfo (box, el_target, el_now, eldown_b, elup_b, EL_Y, str, color);
            pel_state = el_state;
            pel_target = roundf(el_target);
            pel_now = roundf(el_now);
        }
    }

    // button
    drawTrackButton(false, NULL);
    drawStopButton(user_stop);

    // add up-over marker
    drawUpOver();
}

static void drawArrow (const SBox &b, ArrowDir d)
{
    uint16_t x_c = b.x + b.w/2;         // x center
    uint16_t x_r = b.x + b.w - 1;       // x right
    uint16_t y_c = b.y + b.h/2;         // y center
    uint16_t y_b = b.y + b.h - 1;       // y bottom

    switch (d) {
    case AR_LEFT:
        tft.drawLine (x_r, b.y, b.x, y_c, ARROW_COLOR);
        tft.drawLine (b.x, y_c, x_r, y_b, ARROW_COLOR);
        tft.drawLine (x_r, y_b, x_r, b.y, ARROW_COLOR);
        break;

    case AR_DOWN:
        tft.drawLine (b.x, b.y, x_r, b.y, ARROW_COLOR);
        tft.drawLine (b.x, b.y, x_c, y_b, ARROW_COLOR);
        tft.drawLine (x_c, y_b, x_r, b.y, ARROW_COLOR);
        break;

    case AR_UP:
        tft.drawLine (b.x, y_b, x_c, b.y, ARROW_COLOR);
        tft.drawLine (x_c, b.y, x_r, y_b, ARROW_COLOR);
        tft.drawLine (x_r, y_b, b.x, y_b, ARROW_COLOR);
        break;

    case AR_RIGHT:
        tft.drawLine (b.x, b.y, x_r, y_c, ARROW_COLOR);
        tft.drawLine (b.x, y_b, x_r, y_c, ARROW_COLOR);
        tft.drawLine (b.x, b.y, b.x, y_b, ARROW_COLOR);
        break;
    }
}


/* determine sat_upover if we have a 2-axis gimbal tracking sats with el_max > 90.
 * "upover" means use gimbal el > 90 with opposite az to avoid tracking through wrap location.
 * N.B. beware sat rise/set times may not occur exactly when el is SAT_MIN_EL so avoid when near 
 *      by setting upover_pending then calling again often from updateGimbal().
 */
static void initUpOver()
{
    #define SAT_EL_RSERR  0.2F                  // approx el gap due to err in predicted rise/set times
    float az, el, range, rate, riseaz, setaz;

    // assume no
    sat_upover = false;

    // never for 1 axis system or one with no el travel
    if (el_state == ELS_NONE || el_max <= 90)
        return;

    if (getSatAzElNow (NULL, &az, &el, &range, &rate, &riseaz, &setaz, NULL, NULL) && riseaz != SAT_NOAZ) {
        if (el < SAT_MIN_EL - SAT_EL_RSERR) {

            // sat not up so determine upover using next rise/set locations
            sat_upover = setaz == SAT_NOAZ ? false : passesThruWrap (riseaz, setaz, isSatMoon());
            upover_pending = false;
            // Serial.printf (_FX("UPOVER %d el %g rise %g set %g\n"), sat_upover, el, riseaz, setaz);

        } else if (el < SAT_MIN_EL + SAT_EL_RSERR) {

            // defer until out of abiguous range
            upover_pending = true;
            // Serial.printf (_FX("UPOVER pending el %g\n"), el);

        } else {

            // sat is up now so determine upover using az now and set locations for remainder of pass
            sat_upover = setaz == SAT_NOAZ ? false : passesThruWrap (az, setaz, isSatMoon());
            upover_pending = false;
            // Serial.printf (_FX("UPOVER %d el %g az %g set %g\n"), sat_upover, el, az, setaz);
        }
    }
}

/* inform ARC it is ok to go now
 */
static void unStopGimbal(const SBox &box)
{
    if (ARCIsConnected()) {
        // preset current target
        (void) setARCFloat (box, "set az", az_target);
        if (el_state != ELS_NONE)
            setARCFloat (box, "set el", el_target);

        // go
        char resp[150];
        (void) doARCMessage (box, resp, sizeof(resp), _FX("set stop 0"));
    }
}

/* init the gimbal GUI: erase and draw fixed content
 */
void initGimbalGUI(const SBox &box)
{
    // erase all then draw border
    prepPlotBox(box);

    // position main rows, Y depends on 1 or 2 axes
    AZ_Y = el_state != ELS_NONE ? box.y + box.h/3-10 : box.y + box.h/2-20;
    EL_Y = box.y + 2*box.h/3-18;

    // position controls
    azccw_b.x = box.x + box.w/5;
    azccw_b.y = AZ_Y + CHAR_H + 4;
    azccw_b.w = DIRBOX_SZ;
    azccw_b.h = DIRBOX_SZ;

    azccw2_b.x = azccw_b.x - DIRBOX_SZ - DIRBOX_GAP;
    azccw2_b.y = azccw_b.y;
    azccw2_b.w = DIRBOX_SZ;
    azccw2_b.h = DIRBOX_SZ;

    azcw_b.x = box.x + box.w/2 + 10;
    azcw_b.y = azccw_b.y;
    azcw_b.w = DIRBOX_SZ;
    azcw_b.h = DIRBOX_SZ;

    azcw2_b.x = azcw_b.x + DIRBOX_SZ + DIRBOX_GAP;
    azcw2_b.y = azcw_b.y;
    azcw2_b.w = DIRBOX_SZ;
    azcw2_b.h = DIRBOX_SZ;

    eldown_b.x = box.x + box.w/5;
    eldown_b.y = EL_Y + CHAR_H + 4;
    eldown_b.w = DIRBOX_SZ;
    eldown_b.h = DIRBOX_SZ;

    eldown2_b.x = eldown_b.x - DIRBOX_SZ - DIRBOX_GAP;
    eldown2_b.y = eldown_b.y;
    eldown2_b.w = DIRBOX_SZ;
    eldown2_b.h = DIRBOX_SZ;

    elup_b.x = box.x + box.w/2 + 10;
    elup_b.y = EL_Y + CHAR_H + 4;
    elup_b.w = DIRBOX_SZ;
    elup_b.h = DIRBOX_SZ;

    elup2_b.x = elup_b.x + DIRBOX_SZ + DIRBOX_GAP;
    elup2_b.y = elup_b.y;
    elup2_b.w = DIRBOX_SZ;
    elup2_b.h = DIRBOX_SZ;

    stop_b.x = box.x + box.w/8;
    stop_b.y = box.y + box.h - 20;
    stop_b.w = 2*box.w/8;
    stop_b.h = 15;

    auto_b.x = box.x + 4*box.w/8;
    auto_b.y = box.y + box.h - 20;
    auto_b.w = 3*box.w/8;
    auto_b.h = 15;

    // draw title
    tft.setTextColor(RA8875_WHITE);
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    uint16_t tw = getTextWidth (title);
    tft.setCursor (box.x+(box.w-tw)/2, TITLE_Y);
    tft.print(title);

    // draw version
    tft.setTextColor(BRGRAY);
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tw = getTextWidth (version);
    tft.setCursor (box.x+(box.w-tw)/2, VERSION_Y);
    tft.print(version);

    // label az for sure
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor(BRGRAY);
    tft.setCursor (box.x+10, AZ_Y+CHAR_H);
    tft.print(F("Az"));

    // az controls
    drawArrow (azccw_b, AR_LEFT);
    drawArrow (azccw2_b, AR_LEFT);
    drawArrow (azcw_b, AR_RIGHT);
    drawArrow (azcw2_b, AR_RIGHT);

    // el labels and controls if gimbal
    if (el_state != ELS_NONE) {
        tft.setTextColor(BRGRAY);
        tft.setCursor (box.x+10, EL_Y+CHAR_H);
        tft.print(F("El"));

        drawArrow (elup_b, AR_UP);
        drawArrow (elup2_b, AR_UP);
        drawArrow (eldown_b, AR_DOWN);
        drawArrow (eldown2_b, AR_DOWN);
    }

    // init and draw buttons
    stopGimbalNow();
    user_stop = true;
    auto_track = false;
    drawStopButton(user_stop);
    drawTrackButton(true, NULL);

    // insure all previous values appear invalid so updateGUI will draw them
    paz_target = 999;
    pel_target = 999;
    paz_now = 999;
    pel_now = 999;
    paz_state = AZS_UNKNOWN;
    pel_state = ELS_UNKNOWN;
}

/* call any time to stop all motion immediately.
 * safe to call under any circumstances.
 */
void stopGimbalNow()
{
    if (ARCIsConnected()) {
        PlotPane gpp = findPaneChoiceNow (PLOT_CH_GIMBAL);
        if (gpp != PANE_NONE) {
            SBox &box = plot_b[gpp];
            char resp[150];
            (void) doARCMessage (box, resp, sizeof(resp), _FX("set stop 1"));
        }
    }

    auto_track = false;
    sat_upover = false;
    user_stop = true;
}

/* return whether we have something to run
 */
bool haveGimbal()
{
    // TODO
    return (false);

#if _TODO
    // if not connected and we can't connect, then for sure we don't have a gimbal
    if (!ARCIsConnected() && !connectARCOk(box, MIN_MC_TRY))
        return (false);

    // ok, we can go
    return (true);
#endif 

}

/* provide the current wrap az value, if applicable
 */
bool getGimbalWrapAz (float *azp)
{
    if (!ARCIsConnected())
        return (false);

    *azp = az_mnt0;
    return (true);
}

/* called often to update gimbal (or rotator).
 * details depend on hardware available, if any.
 */
void updateGimbal ()
{
    // get out fast if not connected
    if (!ARCIsConnected())
        return;

    // not crazy often
    static uint32_t prev_ms;
    if (!timesUp(&prev_ms, UPDATE_MS))
        return;

    PlotPane gpp = findPaneChoiceNow (PLOT_CH_GIMBAL);
    if (gpp == PANE_NONE)
        return;
    SBox &box = plot_b[gpp];

    // set now once (can't do it in connectARCOk() because that can get called before now() is ready)
    if (send_now) {
        char resp[150];
        (void) doARCMessage (box, resp, sizeof(resp), _FX("set now %g %g %g"),
                    2020 + (now()-1577836800)/31556736.0F, de_ll.lat_d, de_ll.lng_d);
        send_now = false;
    }

    // get current positions
    if (!getAz(box) || (el_state != ELS_NONE && !getEl(box))) {
        closeGimbal();
        return;
    }

    // if auto: set target to satellite if one is defined and we have a gimbal, else DX az
    if (auto_track) {
        if (el_state != ELS_NONE) {

            // require good time for satellites
            if (!goodTime()) {
                auto_track = false;
                stopGimbalNow();
                drawTrackButton(false, "Not UTC");
                return;
            }

            // update location
            float az, el, range, rate, riseaz, setaz;
            if (getSatAzElNow (NULL, &az, &el, &range, &rate, &riseaz, &setaz, NULL, NULL)) {

                // compute new upover if new pass or pending an accurate prediction
                if (isNewPass() || upover_pending)
                    initUpOver();

                // just hold position if still pending
                if (!upover_pending) {

                    // decide how to track
                    if (el < SAT_MIN_EL && riseaz == SAT_NOAZ) {
                        // down now and doesn't rise
                        stopGimbalNow();
                        drawTrackButton(false, "No Rise");
                        return;
                    }

                    if (el < SAT_MIN_EL) {
                        // sat not up yet so sit on horizon at its rise az
                        if (sat_upover) {
                            az_target = fmodf (riseaz + 180 + 360, 360);
                            el_target = 180;
                        } else {
                            az_target = riseaz;
                            el_target = 0;
                        }

                    } else if (sat_upover) {
                        // avoid wrap by running upside down
                        az_target = fmodf (az + 180 + 360, 360);
                        el_target = 180 - el;

                    } else {
                        // no mods required
                        az_target = az;
                        el_target = el;
                    }
                }

            } else {

                // we have a gimbal but no sat is defined that rises so just stay where we are
                stopGimbalNow();
                drawTrackButton(false, "No Sat");
                return;
            }

        } else {

            // just a rotator so point at DX, time does not matter
            float dist, bear;
            propDEDXPath (false, dx_ll, &dist, &bear);
            az_target = rad2deg(bear);
        }

    } // else just move to location commanded from GUI

    // move unless stopped, show GUI if ok
    if (user_stop || (setARCFloat (box, "set az", az_target)
                                && (el_state == ELS_NONE || setARCFloat (box, "set el", el_target))))
        updateGUI(box);
}

/* handle a touch in our pane.
 * return whether really ours or just tapped on title to leave
 */
bool checkGimbalTouch (const SCoord &s, const SBox &box)
{
    // out fast if not ours 
    if (!inBox (s, box))
        return (false);

    // our box but disavow and stop if leaving by tapping title
    if (s.y < TITLE_Y + 10) {
        stopGimbalNow();
        closeGimbal();
        return (false);
    }

    // if click while no connection, try hard to restablish, if still can't then move on
    if (!ARCIsConnected()) {
        if (connectARCOk(box, MAX_MC_TRY)) {
            initGimbalGUI(box);
            return (true);
        } else
            return (false);
    }

    // check manual controls
    if (inBox (s, stop_b)) {
        user_stop = !user_stop;
        if (user_stop) {
            Serial.println (F("ARC: stop on"));
            stopGimbalNow();
        } else {
            Serial.println (F("ARC: stop off"));
            unStopGimbal(box);
        }
    } else if (inBox (s, auto_b)) {
        auto_track = !auto_track;
        if (auto_track) {
            Serial.println (F("ARC: track on"));
            // this is the only command that automatically turns off Stop
            if (user_stop) {
                user_stop = false;
                unStopGimbal(box);
            }
            initUpOver();
        } else {
            // always Stop when turning off Auto
            Serial.println (F("ARC: track off"));
            user_stop = true;
            stopGimbalNow();
        }
    } else if (inBox (s, azccw_b)) {
        az_target = (auto_track ? az_now : az_target) - AZSTEP;
        az_target -= fmodf (az_target, AZSTEP);
        az_target = fmodf (az_target + 720, 360);
        if (!user_stop) {
            auto_track = false;
            sat_upover = false;
        }
    } else if (inBox (s, azccw2_b)) {
        az_target = (auto_track ? az_now : az_target) - AZSTEP2;
        az_target = fmodf (az_target + 720, 360);
        if (!user_stop) {
            auto_track = false;
            sat_upover = false;
        }
    } else if (inBox (s, azcw_b)) {
        az_target = (auto_track ? az_now : az_target) + AZSTEP;
        az_target -= fmodf (az_target, AZSTEP);
        az_target = fmodf (az_target + 720, 360);
        if (!user_stop) {
            auto_track = false;
            sat_upover = false;
        }
    } else if (inBox (s, azcw2_b)) {
        az_target = (auto_track ? az_now : az_target) + AZSTEP2;
        az_target = fmodf (az_target + 720, 360);
        if (!user_stop) {
            auto_track = false;
            sat_upover = false;
        }
    } else if (el_state != ELS_NONE) {
        if (inBox (s, eldown_b)) {
            el_target = (auto_track ? el_now : el_target) - ELSTEP;
            el_target -= fmodf (el_target, ELSTEP);
            el_target = fminf (fmaxf (el_target, el_min), el_max);
            if (!user_stop) {
                auto_track = false;
                sat_upover = false;
            }
        } else if (inBox (s, eldown2_b)) {
            el_target = (auto_track ? el_now : el_target) - ELSTEP2;
            el_target = fminf (fmaxf (el_target, el_min), el_max);
            if (!user_stop) {
                auto_track = false;
                sat_upover = false;
            }
        } else if (inBox (s, elup_b)) {
            el_target = (auto_track ? el_now : el_target) + ELSTEP;
            el_target -= fmodf (el_target, ELSTEP);
            el_target = fminf (fmaxf (el_target, el_min), el_max);
            if (!user_stop) {
                auto_track = false;
                sat_upover = false;
            }
        } else if (inBox (s, elup2_b)) {
            el_target = (auto_track ? el_now : el_target) + ELSTEP2;
            el_target = fminf (fmaxf (el_target, el_min), el_max);
            if (!user_stop) {
                auto_track = false;
                sat_upover = false;
            }
        }
    }

    // ours
    return (true);
}
