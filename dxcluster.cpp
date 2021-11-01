/* handle the DX Cluster display. Only active when visible on a Pane.
 *
 * Clusters:
 *   [ ] support ClusterSpider only
 *   [ ] code for AR-Cluster exists but it is not active -- see comment below.
 *
 * WSJT-X:
 *   [ ] packet definition: https://github.com/roelandjansen/wsjt-x/blob/master/NetworkMessage.hpp
 *   [ ] We don't actually enforce the Status ID to be WSJT-X so this may also work for, say, JTCluster.
 */

#include "HamClock.h"



// uncomment if want to try AR Cluster support
// #define _SUPPORT_ARCLUSTER

/* AR-Cluster commands are inconsistent but we have attempted to implement version 6. But worse, results
 * from "show heading" are unreliable, often, but not always, due to a sign error in longitude. This is not
 * likely to get fixed because it seems the author is SK : https://www.qrz.com/db/ab5k
 * 
 * Example of poor location:
 *
 * telnet dxc.nc7j.com 7373                                                          // AR-Cluster
 *   set station grid DM42jj
 *   set station latlon 32 0 N -111 0 W
 *   show heading ut7lw
 *   Heading/distance to: UT7LW/Ukraine   48 deg/228 lp   3873 mi/6233 km            // N Atlantic!
 *
 * telnet dxc.ww1r.com 7300                                                          // Spider
 *   set/qra DM42jj
 *   set/location 32 0 N -111 0 W
 *   show/heading ut7lw
 *   UT Ukraine-UR: 23 degs - dist: 6258 mi, 10071 km Reciprocal heading: 329 degs   // reasonable
 *
 *
 * Examples of some command variations:
 *
 * telnet dxc.nc7j.com 7373
 *
 *   NC7J AR-Cluster node version 6.1.5123
 *   *** TAKE NOTE! AR-Cluster 6 Commands ***
 *
 *   show heading LZ7AA
 *   Heading/distance to: LZ7AA/Bulgaria   31 deg/211 lp   6521 mi/10495 km
 *
 *   show/heading LZ7AA
 *   Heading/distance to: LZ7AA/Bulgaria   31 deg/211 lp   6521 mi/10495 km
 *
 *   set/qra DM42jj
 *   Unknown command - (set/qra DM42jj) - Type Help for a list of commands
 *
 *   SEt Station GRid DM42jj
 *   Grid set to: DM42JJ
 *
 *   show heading LZ7AA
 *   Heading/distance to: LZ7AA/Bulgaria   31 deg/211 lp   6521 mi/10495 km
 *
 *
 * telnet w6cua.no-ip.org 7300
 *
 *   Welcome to the W6CUA AR-Cluster Telnet port in Castro Valley, Ca.
 *   
 *   Your name is Elwood.  Is this correct? (Y or N) >
 *   Your QTH is Tucson, AZ.  Is this correct? (Y or N) >
 *   
 *   Please set your latitude/longitude information with SET/LOCATION.  Thank you!
 *   
 *   set/qra DM42jj
 *   QRA set to DM42jj
 *   
 *   show/heading LZ7AA
 *   Country: LZ = Bulgaria  24 deg (LP 204) 6500 mi (10458 km) from W6CUA
 *   
 *   set/location 30 00 N 110 0 W
 *   Lat/Lon set to 30 00 N 110 0 W
 *   
 *   show/heading LZ7AA
 *   Country: LZ = Bulgaria  32 deg (LP 212) 6629 mi (10666 km) from WB0OEW
 *   
 *   logout/back in: no questions, it remembered everything
 *
 *   logout/back in: give fictious call
 *
 *   Welcome to the W6CUA AR-Cluster node Telnet port!
 *   Please enter your call: w0oew
 *
 *   Please enter your name
 *   set/qra DM42jj
 *   Your name is set/qra DM42jj.  Is this correct? (Y or N) >
 *   set/qra DM42jj
 *   Please enter your name
 *    
 *
 * telnet dxc.ai9t.com 7373
 *    
 *   Running AR-Cluster Version 6 Software 
 *    
 *   set/qra DM42jj
 *   Unknown command - (set/qra DM42jj) - Type Help for a list of commands
 *    
 *   SEt Station GRid DM42jj
 *   Grid set to: DM42JJ
 *    
 *   show heading LZ7AA
 *   Heading/distance to: LZ7AA/Bulgaria   31 deg/211 lp   6567 mi/10569 km
 *
 */



// setup 
#define TITLE_COLOR     RA8875_GREEN
#define LISTING_COLOR   RA8875_WHITE
#define CLUSTER_TIMEOUT 30000           // send line feed if idle this long, millis
#define MAX_AGE         300000          // max age to restore spot in list, millis
#define TITLE_Y0        27              // title dy, match VOACAP title position
#define HOSTNM_Y0       32              // host name y down from box top
#define LISTING_Y0      47              // first spot y down from box top
#define LISTING_DY      16              // listing row separation
#define FONT_H          7               // listing font height
#define FONT_W          6               // listing font width
#define DWELL_MS        5000            // period to show non-fatal message, ms
#define LISTING_N       ((PLOTBOX_H - LISTING_Y0)/LISTING_DY)       // max n list rows

// connection info
static WiFiClient dx_client;            // persistent TCP connection while displayed ...
static WiFiUDP wsjtx_server;            // or persistent UDP "connection" to WSJT-X client program
static uint32_t last_action;            // time of most recent spot or user activity, millis()

// spots
static DXClusterSpot spots[LISTING_N];
static uint8_t n_spots;                 // n spots already displayed

// type
typedef enum {
    CT_UNKNOWN,
    CT_ARCLUSTER,
    CT_DXSPIDER,
    CT_WSJTX,
} DXClusterType;
static DXClusterType cl_type;




/* convert any upper case letter in str to lower case IN PLACE
 */
static void strtolower (char *str)
{
        for (char c = *str; c != '\0'; c = *++str)
            if (isupper(c))
                *str = tolower(c);
}

static void dxcTrace (const char *buf)
{
        Serial.printf (_FX("DXC: %s\n"), buf);
}

/* read cluster into buf until see a line containing the given string. 
 * intended for seeking command responses.
 */
static bool lookForDXClusterString (char *buf, uint16_t bufl, const char *str)
{
        // bale right away if first read gets nothing at all
        if (!getTCPLine (dx_client, buf, bufl, NULL))
            return (false);

        // cluster is talking, check a few more lines
        for (int i = 0; i < 3; i++) {
            if (strstr (buf, str))
                return (true);
            if (!getTCPLine (dx_client, buf, bufl, NULL))
                return (false);
        }

        // still not found
        dxcTrace (_FX("Failed to find cluster response"));
        return (false);
}

/* search through buf for " <number> str" followed by non-alnum.
 * if found set *valuep to number and return true, else return false.
 */
static bool findLabeledValue (const char *buf, int *valuep, const char *str)
{
        size_t strl = strlen(str);

        for (; *buf; buf++) {
            if (*buf == ' ' && isdigit(buf[1])) {
                // found start of a number: crack then look for str to follow
                char *vend;
                int v = strtol (buf, &vend, 10);
                if (*vend++ == ' ' && strncmp (vend, str, strl) == 0 && !isalnum(vend[strl])) {
                    // found it
                    *valuep = v;
                    return (true);
                }
            }
        }

        return (false);
}

/* given heading from DE in degrees E of N, dist in miles, return lat degs +N and longitude degs +E
 */
static void findLLFromDEHeadingDist (float heading, float miles, LatLong &ll)
{
        float A = deg2rad(heading);
        float b = miles/ERAD_M;             // 2Pi * miles / (2Pi*ERAD_M)
        float cx = de_ll.lat;               // really (Pi/2 - lat) then exchange sin/cos
        float ca, B;                        // cos polar angle, delta lng
        solveSphere (A, b, sinf(cx), cosf(cx), &ca, &B);
        ll.lat_d = rad2deg(asinf(ca));      // asin(ca) = Pi/2 - acos(ca)
        ll.lng_d = rad2deg(de_ll.lng + B);
        normalizeLL (ll);
}

/* given a call sign return its lat/long by querying dx_client.
 * technique depends on cl_type.
 * return whether successful.
 */
static bool getDXClusterSpotLL (const char *call, LatLong &ll)
{
        char buf[120];

        if (cl_type == CT_DXSPIDER) {

            // ask for heading 
            snprintf (buf, sizeof(buf), _FX("show/heading %s"), call);
            dxcTrace (buf);
            dx_client.println (buf);

            // find response
            if (!lookForDXClusterString (buf, sizeof(buf), "degs"))
                return (false);

    #if defined(_SUPPORT_ARCLUSTER)

        } else if (cl_type == CT_ARCLUSTER) {

            // ask for heading 
            snprintf (buf, sizeof(buf), _FX("show heading %s"), call);
            dxcTrace (buf);
            dx_client.println (buf);

            // find response
            if (!lookForDXClusterString (buf, sizeof(buf), "distance"))
                return (false);

    #endif // _SUPPORT_ARCLUSTER

        } else {

            Serial.printf (_FX("Bug! cl_type= %d\n"), cl_type);
            return (false);
        }



        // if get here we should have a line containing <heading> deg .. <miles> mi
        // strcpy(buf,"9miW8WTS Michigan-K: 71 degs - dist: 790 mi, 1272 km Reciprocal heading: 261 degs");
        dxcTrace (buf);
        strtolower(buf);
        int heading, miles;
        if (findLabeledValue (buf, &heading, "degs") && findLabeledValue (buf, &miles, "mi")) {
            findLLFromDEHeadingDist (heading, miles, ll);
            Serial.printf (_FX("DXC: %s heading= %d miles= %d lat= %g lon= %g\n"), call,
                                                                    heading, miles, ll.lat_d, ll.lng_d);
        } else {
            Serial.println (F("DXC: No heading"));
            return (false);
        }

        // if get here it worked!
        return (true);
}

/* set radio and DX from given row, known to be defined
 */
static void engageRow (DXClusterSpot &s)
{
        // get ll 
        LatLong ll;

        if (cl_type == CT_DXSPIDER || cl_type == CT_ARCLUSTER) {

            // get LL from cluster
            if (!getDXClusterSpotLL (s.call, ll))
                return;

        } else if (cl_type == CT_WSJTX) {

            // get LL from grid
            LatLong ll;
            char maid[MAID_CHARLEN];
            if (!maidenhead2ll (ll, s.grid)) {
                Serial.printf (_FX("DXC: bogus grid %s for %s\n"), maid, s.call);
                return;
            }
        } else {

            Serial.printf (_FX("Bug! cl_type= %d\n"), cl_type);
            return;
        }

        // do it -- HB9CEY wants his KX3 to change ASAP
        setRadioSpot(s.freq);
        newDX (ll, NULL, s.call);
}

static void setDXClusterSpotMapPosition (DXClusterSpot &s)
{
        char prefix[MAX_PREF_LEN];
        char *tag;

        if (plotSpotCallsigns())
            tag = s.call;
        else {
            call2Prefix (s.call, prefix);
            tag = prefix;
        }

        SCoord center;
        ll2s (s.ll, center, 0);
        setMapTagBox (tag, center, 0, s.map_b);
}

static void drawSpotOnMap (DXClusterSpot &s)
{
        if (mapDXClusterSpots()) {
            if (plotSpotCallsigns()) {
                drawMapTag (s.call, s.map_b);
            } else {
                char prefix[MAX_PREF_LEN];
                call2Prefix (s.call, prefix);
                drawMapTag (prefix, s.map_b);
            }
        }
}

/* draw a spot at the given row in the given box.
 */
static void drawSpotOnList (const SBox &box, uint8_t row)
{
        DXClusterSpot *sp = &spots[row];
        char line[50];

        selectFontStyle (LIGHT_FONT, FAST_FONT);
        tft.setTextColor(LISTING_COLOR);

        uint16_t x = box.x+4;
        uint16_t y = box.y + LISTING_Y0 + row*LISTING_DY;
        tft.fillRect (x, y, box.w-5, LISTING_DY-1, RA8875_BLACK);
        tft.setCursor (x, y);

        // pretty freq, fixed 8 chars
        const char *f_fmt = sp->freq < 1e6 ? "%8.1f" : "%8.0f";
        (void) sprintf (line, f_fmt, sp->freq);

        // add remaining fields
        snprintf (line+8, sizeof(line)-8, _FX(" %-*s %04u"), MAX_SPOTCALL_LEN-1, sp->call, sp->uts);
        tft.print (line);
}

/* add a new spot both on map and in list, scrolling list if already full.
 * use grid to get ll if set, else look up call to set both.
 * return false if same spot again or some error.
 */
static bool addDXClusterSpot (const SBox &box, float kHz, const char call[], const char *grid, uint16_t ut)
{
        // skip if same station on same freq as previous
        if (n_spots > 0) {
            DXClusterSpot &spot = spots[n_spots-1];
            if (fabsf(kHz-spot.freq) < 0.1F && strcmp (call, spot.call) == 0)
                return (false);
        }

        // find next available row, scrolling if necessary
        if (n_spots == LISTING_N) {
            // scroll up, discarding top (first) entry
            for (uint8_t i = 0; i < LISTING_N-1; i++) {
                spots[i] = spots[i+1];
                drawSpotOnList (box, i);
            }
            n_spots = LISTING_N-1;
        }
        DXClusterSpot &spot = spots[n_spots];

        // store the easy info
        spot.freq = kHz;
        memcpy (spot.call, call, MAX_SPOTCALL_LEN-1);      // preserve existing EOS
        spot.uts = ut;

        // find ll and grid some way
        char errmsg[50] = "";
        bool ok = false;
        if (grid) {
            // save grid then use it to get ll
            strcpy (spot.grid, grid);
            ok = maidenhead2ll (spot.ll, spot.grid);
            if (ok)
                Serial.printf (_FX("DXC: %s %s lat= %g lng= %g\n"),
                                        spot.call, spot.grid, spot.ll.lat_d, spot.ll.lng_d);
            else
                snprintf (errmsg, sizeof(errmsg), _FX("%s bad grid: %s"), call, grid);
        } else {
            // get ll from cluster, then grid from ll
            ok = getDXClusterSpotLL (call, spot.ll);
            if (ok)
                ll2maidenhead (spot.grid, spot.ll);
            else
                snprintf (errmsg, sizeof(errmsg), _FX("%s ll lookup failed"), call);
        }
        if (!ok) {
            // error set grid and ll to 0/0
            dxcTrace (errmsg);
            memset (&spot.ll, 0, sizeof(spot.ll));
            ll2maidenhead (spot.grid, spot.ll);
            return (false);
        }


        // draw
        drawSpotOnList (box, n_spots);
        setDXClusterSpotMapPosition (spot);
        drawSpotOnMap (spot);

        // ok
        n_spots++;
        return (true);
}

/* given address of pointer into a WSJT-X message, extract bool and advance pointer to next field.
 */
static bool wsjtx_bool (uint8_t **bpp)
{
        bool x = **bpp > 0;
        *bpp += 1;
        return (x);
}

/* given address of pointer into a WSJT-X message, extract uint32_t and advance pointer to next field.
 * bytes are big-endian order.
 */
static uint32_t wsjtx_quint32 (uint8_t **bpp)
{
        uint32_t x = ((*bpp)[0] << 24) | ((*bpp)[1] << 16) | ((*bpp)[2] << 8) | (*bpp)[3];
        *bpp += 4;
        return (x);
}

/* given address of pointer into a WSJT-X message, extract utf8 string and advance pointer to next field.
 * N.B. returned string points into message so will only be valid as long as message memory is valid.
 */
static char *wsjtx_utf8 (uint8_t **bpp)
{
        // save begining of this packet entry
        uint8_t *bp0 = *bpp;

        // decode length
        uint32_t len = wsjtx_quint32 (bpp);

        // check for flag meaning null length string same as 0 for our purposes
        if (len == 0xffffffff)
            len = 0;

        // advance packet pointer over contents
        *bpp += len;

        // copy contents to front, overlaying length, to make room to add EOS
        memmove (bp0, bp0+4, len);
        bp0[len] = '\0';

        // Serial.printf (_FX("DXC: utf8 %d '%s'\n"), len, (char*)bp0);

        // return address of content now within packet
        return ((char *)bp0);
}

/* given address of pointer into a WSJT-X message, extract double and advance pointer to next field.
 */
static uint64_t wsjtx_quint64 (uint8_t **bpp)
{
        uint64_t x;

        x = ((uint64_t)(wsjtx_quint32(bpp))) << 32;
        x |= wsjtx_quint32 (bpp);

        return (x);
}

/* return whether the given packet contains a WSJT-X Status packet.
 * if true, leave *bpp positioned just after ID.
 */
static bool wsjtxIsStatusMsg (uint8_t **bpp)
{
        resetWatchdog();

        // crack magic header
        uint32_t magic = wsjtx_quint32 (bpp);
        // Serial.printf (_FX("DXC: magic 0x%x\n"), magic);
        if (magic != 0xADBCCBDA) {
            Serial.println (F("DXC: packet received but wrong magic"));
            return (false);
        }

        // crack and ignore the max schema value
        (void) wsjtx_quint32 (bpp);                         // skip past max schema

        // crack message type. we only care about Status messages which are type 1
        uint32_t msgtype = wsjtx_quint32 (bpp);
        // Serial.printf (_FX("DXC: type %d\n"), msgtype);
        if (msgtype != 1)
            return (false);

        // if we get this far assume packet is what we want.
        // crack ID but ignore to allow compatibility with clones.
        volatile char *id = wsjtx_utf8 (bpp);
        (void)id;           // lint
        // Serial.printf (_FX("DXC: id '%s'\n"), id);
        // if (strcmp ("WSJT-X", id) != 0)
            // return (false);

        // ok!
        return (true);
}

/* parse and process WSJT-X message known to be Status.
 * *bpp is positioned just after ID field.
 * draw on screen in box.
 */
static void wsjtxParseStatusMsg (const SBox &box, uint8_t **bpp)
{
        resetWatchdog();
        // Serial.println (_FX("DXC: Parsing status"));

        // crack remaining fields down to grid
        uint64_t dial_freq = wsjtx_quint64 (bpp);           // capture Hz
        (void) wsjtx_utf8 (bpp);                            // skip over mode
        char *dx_call = wsjtx_utf8 (bpp);                   // capture call
        (void) wsjtx_utf8 (bpp);                            // skip over report
        (void) wsjtx_utf8 (bpp);                            // skip over Tx mode
        (void) wsjtx_bool (bpp);                            // skip over Tx enabled flag
        (void) wsjtx_bool (bpp);                            // skip over transmitting flag
        (void) wsjtx_bool (bpp);                            // skip over decoding flag
        (void) wsjtx_quint32 (bpp);                         // skip over Rx DF -- not always correct
        (void) wsjtx_quint32 (bpp);                         // skip over Tx DF
        (void) wsjtx_utf8 (bpp);                            // skip over DE call
        (void) wsjtx_utf8 (bpp);                            // skip over DE grid
        char *dx_grid = wsjtx_utf8 (bpp);                   // capture grid

        // Serial.printf (_FX("DXC: dial freq %lu\n"), dial_freq);
        // Serial.printf (_FX("DXC: dx call %s\n"), dx_call);
        // Serial.printf (_FX("DXC: dx grid %s\n"), dx_grid);

        // ignore if frequency is clearly bogus (which I have seen)
        if (dial_freq == 0)
            return;

        // get ll from grid valid
        LatLong ll;
        if (!maidenhead2ll (ll, dx_grid)) {
            // Serial.printf (_FX("DXC: %s invalid grid: %s\n"), dx_call, dx_grid);
            return;
        }

        // prep current UT time
        int hr = hour();
        int mn = minute();
        uint16_t ut = hr*100 + mn;

        // add to list with actual frequency and set if new
        if (addDXClusterSpot (box, dial_freq*1e-3, dx_call, dx_grid, ut)) {                  // Hz to kHz
            // Serial.printf (_FX("DXC: WSJT-X %s @ %s\n"), dx_call, dx_grid);
            engageRow (spots[n_spots-1]);
        }

        // printFreeHeap(F("wsjtxParseStatusMsg"));
}

/* display the given error message and shut down the connection.
 * draw entire box in case we were not the front pane at time of error.
 */
static void showDXClusterErr (const SBox &box, const char *msg)
{
        // erase box
        tft.fillRect (box.x, box.y, box.w, box.h, RA8875_BLACK);

        // show title and message
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        tft.setTextColor(RA8875_RED);
        const char *title = "DX Cluster error:";
        uint16_t tw = getTextWidth (title);
        tft.setCursor (box.x + (box.w-tw)/2, box.y + box.h/3);
        tft.print (title);
        uint16_t mw = getTextWidth (msg);
        tft.setCursor (box.x + (box.w-mw)/2, box.y + box.h/3+2*FONT_H);
        tft.print (msg);

        // log
        dxcTrace (msg);

        // shut down connection
        closeDXCluster();
}


/* try to connect to the cluster defined by getDXClusterHost():getDXClusterPort().
 * if success: dx_client or wsjtx_server is live and return true,
 * else: both are closed, display error msg in box, return false.
 */
static bool connectDXCluster (const SBox &box)
{
        const char *dxhost = getDXClusterHost();
        int dxport = getDXClusterPort();

        Serial.printf (_FX("DXC: Connecting to %s:%d\n"), dxhost, dxport);
        resetWatchdog();

        // decide type from host name
        if (!strcasecmp (dxhost, "WSJT-X") || !strcasecmp (dxhost, "JTDX")) {

            // create fresh UDP for WSJT-X
            wsjtx_server.stop();
            if (wsjtx_server.begin(dxport)) {

                // record and claim ok so far
                cl_type = CT_WSJTX;
                return (true);
            }

        } else {

            // open fresh socket
            dx_client.stop();
            if (wifiOk() && dx_client.connect(dxhost, dxport)) {

                // look alive
                resetWatchdog();
                updateClocks(false);
                dxcTrace (_FX("connect ok"));

                // assume we have been asked for our callsign
                dx_client.println (getCallsign());

                // read until find a line ending with '>', looking for clue about type of cluster
                uint16_t bl;
                StackMalloc buf_mem(200);
                char *buf = buf_mem.getMem();
                cl_type = CT_UNKNOWN;
                while (getTCPLine (dx_client, buf, buf_mem.getSize(), &bl)) {
                    // Serial.println (buf);
                    strtolower(buf);
                    if (strstr (buf, "dx") && strstr (buf, "spider"))
                        cl_type = CT_DXSPIDER;
    #if defined(_SUPPORT_ARCLUSTER)
                    else if (strstr (buf, "ar-cluster") && strstr (buf, "ersion") && strchr (buf, '6'))
                        cl_type = CT_ARCLUSTER;
    #endif // _SUPPORT_ARCLUSTER

                    if (buf[bl-1] == '>')
                        break;
                }

                if (cl_type == CT_UNKNOWN) {
                    showDXClusterErr (box, _FX("Type unknown"));
                    return (false);
                }

                if (!sendDXClusterDELLGrid()) {
                    showDXClusterErr (box, _FX("Failed sending DE grid"));
                    return (false);
                }

                // confirm still ok
                if (!dx_client) {
                    showDXClusterErr (box, _FX("Login failed"));
                    return (false);
                }

                // all ok so far
                return (true);
            }
        }

        // sorry
        showDXClusterErr (box, _FX("Connection failed"));    // also calls dx_client.stop()
        return (false);
}

/* display the current cluster host and port in the given color
 */
static void showHostPort (const SBox &box, uint16_t c)
{
        const char *dxhost = getDXClusterHost();
        int dxport = getDXClusterPort();

        char name[(box.w-2)/FONT_W];
        snprintf (name, sizeof(name), _FX("%s:%d"), dxhost, dxport);

        selectFontStyle (LIGHT_FONT, FAST_FONT);
        tft.setTextColor(c);
        uint16_t nw = getTextWidth (name);
        tft.setCursor (box.x + (box.w-nw)/2, box.y + HOSTNM_Y0);
        tft.print (name);
}

/* send our lat/long and grid to dx_client, depending on cluster type.
 * return whether successful.
 * N.B. can be called any time so be prepared to do nothing if not appropriate.
 */
bool sendDXClusterDELLGrid()
{
        if (!useDXCluster() || !dx_client)
            return (true);

        char buf[100];

        // handy DE grid as string
        char maid[MAID_CHARLEN];
        getNVMaidenhead (NV_DE_GRID, maid);

        // handy DE lat/lon in common format
        char llstr[30];
        snprintf (llstr, sizeof(llstr), _FX("%.0f %.0f %c %.0f %.0f %c"),
                    fabsf(de_ll.lat_d), fmodf(60*fabsf(de_ll.lat_d), 60), de_ll.lat_d < 0 ? 'S' : 'N',
                    fabsf(de_ll.lng_d), fmodf(60*fabsf(de_ll.lng_d), 60), de_ll.lng_d < 0 ? 'W' : 'E');

        if (cl_type == CT_DXSPIDER) {

            // set grid
            snprintf (buf, sizeof(buf), _FX("set/qra %s"), maid);
            dx_client.println(buf);
            dxcTrace (buf);
            if (!lookForDXClusterString (buf, sizeof(buf), ">")) {
                Serial.println (F("No > after set/qra"));
                return (false);
            }

            // set DE ll
            snprintf (buf, sizeof(buf), _FX("set/location %s"), llstr);
            dx_client.println(buf);
            dxcTrace (buf);
            if (!lookForDXClusterString (buf, sizeof(buf), ">")) {
                Serial.println (F("No > after set/loc"));
                return (false);
            }

            // ok!
            return (true);

    #if defined(_SUPPORT_ARCLUSTER)

        } else if (cl_type == CT_ARCLUSTER) {

            // friendly turn off skimmer just avoid getting swamped
            strcpy_P (buf, PSTR("set dx filter not skimmer"));
            dx_client.println(buf);
            dxcTrace (buf);
            if (!lookForDXClusterString (buf, sizeof(buf), "filter"))
                return (false);

            // set grid
            snprintf (buf, sizeof(buf), _FX("set station grid %sjj"), maid);    // fake 6-char grid
            dx_client.println(buf);
            dxcTrace (buf);
            if (!lookForDXClusterString (buf, sizeof(buf), "set to"))
                return (false);

            // set ll
            snprintf (buf, sizeof(buf), _FX("set station latlon %s"), llstr);
            dx_client.println(buf);
            dxcTrace (buf);
            if (!lookForDXClusterString (buf, sizeof(buf), "location"))
                return (false);

            // ok!
            return (true);

    #endif // _SUPPORT_ARCLUSTER

        }

        // fail
        return (false);
}

/* prep the given box and connect dx_client to a dx cluster or wsjtx_server.
 * return whether successful.
 */
static bool initDXCluster(const SBox &box)
{
        // skip if not configured
        if (!useDXCluster())
            return (true);              // feign success to avoid retries

        // prep
        prepPlotBox (box);

        // title
        selectFontStyle (LIGHT_FONT, SMALL_FONT);
        tft.setTextColor(TITLE_COLOR);
        tft.setCursor (box.x + 27, box.y + TITLE_Y0);
        tft.print (F("DX Cluster"));

        // show cluster host busy
        showHostPort (box, RA8875_YELLOW);

        // connect to dx cluster
        if (connectDXCluster(box)) {

            // ok: show host in green
            showHostPort (box, RA8875_GREEN);

            // restore known spots if not too old else reset list
            if (millis() - last_action < MAX_AGE) {
                for (uint8_t i = 0; i < n_spots; i++)
                    drawSpotOnList (box, i);
            } else {
                n_spots = 0;
            }

            // reinit time
            last_action = millis();

            // ok
            return (true);

        } // else already displayed error message

        printFreeHeap(F("initDXCluster"));

        // sorry
        return (false);
}


/* called frequently to drain and process cluster connection, open if not already running.
 * return whether connection is ok.
 */
bool updateDXCluster(const SBox &box)
{
        // open if not already
        if (!isDXClusterConnected() && !initDXCluster(box)) {
            // error already shown
            return(false);
        }

        if ((cl_type == CT_DXSPIDER || cl_type == CT_ARCLUSTER) && dx_client) {

            // this works for both types of cluster

            // roll any new spots into list
            char line[120];
            char call[30];
            float kHz;
            while (dx_client.available() && getTCPLine (dx_client, line, sizeof(line), NULL)) {
                // DX de KD0AA:     18100.0  JR1FYS       FT8 LOUD in FL!                2156Z EL98

                // look alive
                updateClocks(false);
                resetWatchdog();

                // log but note some clusters embed \a bell in their reports, remove so they don't beep
                for (char *lp = line; *lp; lp++)
                    if (!isprint(*lp))
                        *lp = ' ';
                // Serial.println (line);

                // crack
                if (sscanf (line, _FX("DX de %*s %f %10s"), &kHz, call) == 2) {
                    dxcTrace (line);

                    // looks like a spot, extract time also
                    char *utp = &line[70];
                    uint16_t ut = atoi(utp) % 2400;

                    // note and display
                    last_action = millis();
                    (void) addDXClusterSpot (box, kHz, call, NULL, ut);
                }
            }

            // check for lost connection
            if (!dx_client) {
                showDXClusterErr (box, _FX("Lost connection"));
                return(false);
            }

            // send something if quiet for too long
            if (millis() - last_action > CLUSTER_TIMEOUT) {
                last_action = millis();        // avoid banging
                dxcTrace (_FX("feeding"));
                if (!sendDXClusterDELLGrid()) {
                    showDXClusterErr (box, _FX("Lost connection"));
                    return(false);
                }
            }

        } else if (cl_type == CT_WSJTX && wsjtx_server) {

            resetWatchdog();

            // drain ALL pending packets, retain most recent Status message if any

            uint8_t *any_msg = NULL;        // malloced if get a new packet of any type
            uint8_t *sts_msg = NULL;        // malloced if find Status msg

            int packet_size;
            while ((packet_size = wsjtx_server.parsePacket()) > 0) {
                // Serial.printf (_FX("DXC: WSJT-X size= %d heap= %d\n"), packet_size, ESP.getFreeHeap());
                any_msg = (uint8_t *) realloc (any_msg, packet_size);
                resetWatchdog();
                if (wsjtx_server.read (any_msg, packet_size) > 0) {
                    uint8_t *bp = any_msg;
                    if (wsjtxIsStatusMsg (&bp)) {
                        // save from bp to the end in prep for wsjtxParseStatusMsg()
                        int n_skip = bp - any_msg;
                        // Serial.printf (_FX("DXC: skip= %d packet_size= %d\n"), n_skip, packet_size);
                        sts_msg = (uint8_t *) realloc (sts_msg, packet_size - n_skip);
                        memcpy (sts_msg, any_msg + n_skip, packet_size - n_skip);
                    }
                }
            }

            // process then free newest Status message if received
            if (sts_msg) {
                uint8_t *bp = sts_msg;
                wsjtxParseStatusMsg (box, &bp);
                free (sts_msg);
            }

            // clean up
            if (any_msg)
                free (any_msg);
        }

        // didn't break
        return (true);
}

/* insure cluster connection is closed
 */
void closeDXCluster()
{
        // make sure either/both connection is/are closed
        if (dx_client) {
            dx_client.stop();
            Serial.printf (_FX("DXC: disconnect %s\n"), dx_client ? "failed" : "ok");
        }
        if (wsjtx_server) {
            wsjtx_server.stop();
            Serial.printf (_FX("DXC: WSTJ-X disconnect %s\n"), wsjtx_server ?"failed":"ok");
        }
}

/* try to set DX from the touched spot.
 * return true if looks like user is interacting with the cluster, false if wants to change pane.
 */
bool checkDXClusterTouch (const SCoord &s, const SBox &box)
{
        // ours at all?
        if (!inBox (s, box))
            return (false);

        // tapping title always leaves this pane
        if (s.y < box.y + TITLE_Y0) {
            closeDXCluster();             // insure disconnected
            last_action = millis();       // in case op wants to come back soon
            return (false);
        }

        // engage tapped row, if defined
        int click_row = ((s.y+LISTING_DY/2-FONT_H/2-box.y-LISTING_Y0)/LISTING_DY);
        if (click_row >= 0 && click_row < n_spots && spots[click_row].call[0] != '\0' && isDXClusterConnected())
            engageRow (spots[click_row]);

        // ours
        return (true);
}

/* pass back current spots list, and return whether enabled at all.
 * ok to pass back if not displayed because spot list is still intact.
 */
bool getDXClusterSpots (DXClusterSpot **spp, uint8_t *nspotsp)
{
        if (useDXCluster()) {
            *spp = spots;
            *nspotsp = n_spots;
            return (true);
        }

        return (false);
}

/* update map positions of all spots, eg, because the projection has changed
 */
void updateDXClusterSpotScreenLocations()
{
        for (uint8_t i = 0; i < n_spots; i++)
            setDXClusterSpotMapPosition (spots[i]);
}

/* draw all spots on map, if up
 */
void drawDXClusterSpotsOnMap ()
{
        // skip if we are not up or don't want spots on map
        if (!useDXCluster() || findPaneForChoice(PLOT_CH_DXCLUSTER) == PANE_NONE || !mapDXClusterSpots())
            return;

        for (uint8_t i = 0; i < n_spots; i++)
            drawSpotOnMap (spots[i]);
}

/* return whether the given screen coord lies over any spot label.
 * N.B. we assume map_s are set
 */
bool overAnyDXClusterSpots(const SCoord &s)
{
        // false for sure if spots are not on
        if (!useDXCluster() || findPaneForChoice(PLOT_CH_DXCLUSTER) == PANE_NONE)
            return (false);

        for (uint8_t i = 0; i < n_spots; i++)
            if (inBox (s, spots[i].map_b))
                return (true);

        return (false);
}

/* return whether cluster is currently connected
 */
bool isDXClusterConnected()
{
        return (useDXCluster() && (dx_client || wsjtx_server));
}
