/* Northen California DX Foundation Beacon Network.
 * http://www.ncdxf.org/beacon/index.html#Schedule
 */

/* manage the Northern California DX Foundation beacons.
 *
 * Each beacon is drawn as a colored triangle symbol with call sign text below. The triangle is drawn
 * to high res so is redrawn after being scanned. But the text is just jumped over and never redrawn.
 */

#include "HamClock.h"


#define NBEACONS        18                      // number of beacons
#define BEACONR         9                       // beacon symbol radius, pixels
#define BLEG            (BEACONR-4)             // beacon symbol leg length
#define BEACONCW        6                       // beacon char width
#define BEACONCH        9                       // beacon call height
#define BEACONCY        7                       // beacon call top coord +y from center of triangle
#define BORDER_COL      RGB565(127,127,127)     // control box border color

typedef struct {
    int16_t lat, lng;                           // location, degs north and east
    char call[7];                               // call sign
    SCoord s;                                   // screen coord of triangle symbol center
    uint16_t c;                                 // color
    SBox call_b;                                // enclosing background box
} NCDXFBeacon;

/* listed in order of 14, 18, 21, 24 and 28 MHz starting at 3N minutes after the hour.
 * 4 of the 18 stations each transmit for 10 seconds then rotate down.
 *
 * given s seconds after the hour, find index for each frequency:
 *   14 MHz i = (s/10+0+NBEACONS)%NBEACONS
 *   18 MHz i = (s/10-1+NBEACONS)%NBEACONS
 *   21 MHz i = (s/10-2+NBEACONS)%NBEACONS
 *   24 MHz i = (s/10-3+NBEACONS)%NBEACONS
 *   28 MHz i = (s/10-4+NBEACONS)%NBEACONS
 */
static NCDXFBeacon blist[NBEACONS] = {
    {  41,  -74, "4U1UN",  {0,0}, 0, {0,0,0,0}},
    {  80,  -86, "VE8AT",  {0,0}, 0, {0,0,0,0}},
    {  37, -122, "W6WX",   {0,0}, 0, {0,0,0,0}},
    {  21, -156, "KH6RS",  {0,0}, 0, {0,0,0,0}},
    { -41,  176, "ZL6B",   {0,0}, 0, {0,0,0,0}},
    { -32,  116, "VK6RBP", {0,0}, 0, {0,0,0,0}},
    {  34,  137, "JA2IGY", {0,0}, 0, {0,0,0,0}},
    {  55,   83, "RR9O",   {0,0}, 0, {0,0,0,0}},
    {  22,  114, "VR2B",   {0,0}, 0, {0,0,0,0}},
    {   7,   80, "4S7B",   {0,0}, 0, {0,0,0,0}},
    { -26,   28, "ZS6DN",  {0,0}, 0, {0,0,0,0}},
    {  -1,   37, "5Z4B",   {0,0}, 0, {0,0,0,0}},
    {  32,   35, "4X6TU",  {0,0}, 0, {0,0,0,0}},
    {  60,   25, "OH2B",   {0,0}, 0, {0,0,0,0}},
    {  33,  -17, "CS3B",   {0,0}, 0, {0,0,0,0}},
    { -35,  -58, "LU4AA",  {0,0}, 0, {0,0,0,0}},
    { -12,  -77, "OA4B",   {0,0}, 0, {0,0,0,0}},
    {   9,  -68, "YV5B",   {0,0}, 0, {0,0,0,0}},
};


/* symbol color for each frequency
 */
#define BCOL_14 RA8875_RED              // 14.100 MHz
#define BCOL_18 RA8875_GREEN            // 18.110 MHz
#define BCOL_21 RGB565(100,100,255)     // 21.150 MHz
#define BCOL_24 RA8875_YELLOW           // 24.930 MHz
#define BCOL_28 RA8875_MAGENTA          // 28.200 MHz
#define BCOL_S  RA8875_BLACK            // silent, not actually drawn
#define BCOL_N  6                       // number of color states



/* using the current user time set the color state for each beacon.
 */
static void setBeaconStates ()
{
    time_t t = nowWO();
    int mn = minute(t);
    int sc = second(t);
    uint16_t s_10 = (60*mn + sc)/10;

    for (BeaconID id = 0; id < NBEACONS; id++)
        blist[id].c = BCOL_S;

    blist[(s_10-0+NBEACONS)%NBEACONS].c = BCOL_14;
    blist[(s_10-1+NBEACONS)%NBEACONS].c = BCOL_18;
    blist[(s_10-2+NBEACONS)%NBEACONS].c = BCOL_21;
    blist[(s_10-3+NBEACONS)%NBEACONS].c = BCOL_24;
    blist[(s_10-4+NBEACONS)%NBEACONS].c = BCOL_28;
}


/* draw beacon symbol centered at given screen location with the given color
 */
static void drawBeaconSymbol (const SCoord &s, uint16_t c)
{
    tft.fillTriangle (s.x, s.y-BEACONR, s.x-9*BEACONR/10, s.y+BEACONR/2,
                s.x+9*BEACONR/10, s.y+BEACONR/2, RA8875_BLACK);
    tft.fillTriangle (s.x, s.y-BLEG, s.x-9*BLEG/10, s.y+BLEG/2, s.x+9*BLEG/10, s.y+BLEG/2, c);
}

/* draw the given beacon, including callsign beneath.
 */
static void drawBeacon (NCDXFBeacon &nb)
{
    // triangle symbol
    drawBeaconSymbol (nb.s, nb.c);

    // draw call sign
    drawMapTag (nb.call, nb.call_b);
}

/* erase beacon
 * N.B. redrawing sat path does avoid gaps but can be out of sync with currently visible and show double.
 */
static void eraseBeacon (NCDXFBeacon &nb)
{
    resetWatchdog();

    // redraw map under symbol
    for (int8_t dy = -BEACONR; dy <= BEACONR/2; dy += 1) {
        int8_t hw = 3*(dy+BEACONR)/5+1;
        for (int8_t dx = -hw; dx <= hw; dx += 1)
            drawMapCoord (nb.s.x+dx, nb.s.y+dy);
    }

    // redraw map under call
    for (uint16_t y = nb.call_b.y; y < nb.call_b.y + nb.call_b.h; y++) {
        for (uint16_t x = nb.call_b.x; x < nb.call_b.x + nb.call_b.w; x++)
            drawMapCoord (x, y);
    }
}


/* return whether the given point is anywhere inside a beacon symbol or call
 */
static bool overBeacon (const SCoord &s, const NCDXFBeacon &nb)
{
    // check call
    if (inBox (s, nb.call_b))
        return (true);

    // check above or below symbol
    if (s.y < nb.s.y - BEACONR || s.y > nb.s.y + BEACONR/2)
        return (false);

    // distance below top tip
    uint16_t dy = s.y - (nb.s.y - BEACONR);

    // width at this y (same as eraseBeacon)
    int8_t hw = 3*dy/5+1;

    // left or right
    if (s.x < nb.s.x - hw || s.x > nb.s.x + hw)
        return (false);

    // yup
    return (true);
}


/* update beacon display, typically on each 10 second period and
 *   if erase_too then also erase those that are off
 *   if immediate then do it now, else only if time for the 10 second update period
 *   if force do it even if brb_mode != BRB_SHOW_BEACONS (eg erase all after turning off)
 */
void updateBeacons (bool erase_too, bool immediate, bool force)
{
    // bale if not showing beacons and not asked to do it anyway
    if (brb_mode != BRB_SHOW_BEACONS && !force)
        return;

    // bale if not immediate and we are still in the same 10 second interval
    static uint8_t prev_sec10;
    uint8_t sec10 = second(nowWO())/10;
    if (!immediate && sec10 == prev_sec10)
        return;
    prev_sec10 = sec10;

    resetWatchdog();

    // ok, update each beacon
    bool erased_any = false;
    setBeaconStates();
    for (NCDXFBeacon *bp = blist; bp < &blist[NBEACONS]; bp++) {
        if (bp->c == BCOL_S || brb_mode != BRB_SHOW_BEACONS) {
            if (erase_too) {
                eraseBeacon (*bp);
                erased_any = true;
            }
        } else if (overMap(bp->s) && !overRSS (bp->call_b)) {
            drawBeacon (*bp);
        }
    }

    // draw other symbols in case erasing a beacon clobbered some
    if (erased_any)
        drawAllSymbols(false);

    updateClocks(false);

    // printFreeHeap(F("updateBeacons"));
}

/* update screen location for all beacons.
 */
void updateBeaconScreenLocations()
{
    for (NCDXFBeacon *bp = blist; bp < &blist[NBEACONS]; bp++) {
        ll2s (deg2rad(bp->lat), deg2rad(bp->lng), bp->s, 3*BEACONCW);   // about max
        setMapTagBox (bp->call, bp->s, BEACONCH, bp->call_b);
    }
}

/* return whether the given screen coord is over any visible symbol or call box
 */
bool overAnyBeacon (const SCoord &s)
{
    if (brb_mode != BRB_SHOW_BEACONS)
        return (false);

    for (NCDXFBeacon *bp = blist; bp < &blist[NBEACONS]; bp++) {
        if (bp->c == BCOL_S)
            continue;
        if (overBeacon (s, *bp))
            return (true);
    }

    return (false);
}

/* draw the beacon control box
 */
void drawBeaconBox()
{
    static const char label[] = "NCDXF";

    // tiny font
    selectFontStyle (BOLD_FONT, FAST_FONT);

    if (brb_mode == BRB_SHOW_BEACONS) {

        // draw button on
        tft.fillRect (NCDXF_b.x+6, NCDXF_b.y+8, NCDXF_b.w-12, 16, RA8875_WHITE);
        tft.setCursor (NCDXF_b.x+14, NCDXF_b.y+12);
        tft.setTextColor (RA8875_BLACK);
        tft.print (label);

        // erase below button
        tft.fillRect (NCDXF_b.x, NCDXF_b.y+30, NCDXF_b.w, NCDXF_b.h-30, RA8875_BLACK);

        // draw key
        SCoord s;
        s.x = NCDXF_b.x + BEACONR-1;
        s.y = NCDXF_b.y + 45;
        uint8_t dy = (NCDXF_b.h-40)/(BCOL_N-1);         // silent color not drawn
        uint16_t c;
        
        c = BCOL_14;
        drawBeaconSymbol (s, c);
        tft.setTextColor (c);
        tft.setCursor (s.x+BEACONR, s.y-BEACONR/2);
        tft.print (F("14.100"));

        s.y += dy;
        c = BCOL_18;
        drawBeaconSymbol (s, c);
        tft.setTextColor (c);
        tft.setCursor (s.x+BEACONR, s.y-BEACONR/2);
        tft.print (F("18.110"));

        s.y += dy;
        c = BCOL_21;
        drawBeaconSymbol (s, c);
        tft.setTextColor (c);
        tft.setCursor (s.x+BEACONR, s.y-BEACONR/2);
        tft.print (F("21.150"));

        s.y += dy;
        c = BCOL_24;
        drawBeaconSymbol (s, c);
        tft.setTextColor (c);
        tft.setCursor (s.x+BEACONR, s.y-BEACONR/2);
        tft.print (F("24.930"));

        s.y += dy;
        c = BCOL_28;
        drawBeaconSymbol (s, c);
        tft.setTextColor (c);
        tft.setCursor (s.x+BEACONR, s.y-BEACONR/2);
        tft.print (F("28.200"));

    } else {

        // draw button state off
        tft.fillRect (NCDXF_b.x+6, NCDXF_b.y+8, NCDXF_b.w-12, 16, RA8875_BLACK);
        tft.drawRect (NCDXF_b.x+6, NCDXF_b.y+8, NCDXF_b.w-12, 16, RA8875_WHITE);
        tft.setCursor (NCDXF_b.x+13, NCDXF_b.y+12);
        tft.setTextColor (RA8875_WHITE);
        tft.print(label);

        // erase below button
        tft.fillRect (NCDXF_b.x, NCDXF_b.y+30, NCDXF_b.w, NCDXF_b.h-30, RA8875_BLACK);

        // display brightness state
        drawBrightness();
    }

    // border
    tft.drawLine (NCDXF_b.x, NCDXF_b.y, NCDXF_b.x+NCDXF_b.w, NCDXF_b.y, BORDER_COL);
    tft.drawLine (NCDXF_b.x, NCDXF_b.y, NCDXF_b.x, NCDXF_b.y+NCDXF_b.h, BORDER_COL);
    tft.drawLine (NCDXF_b.x+NCDXF_b.w, NCDXF_b.y,
                  NCDXF_b.x+NCDXF_b.w, NCDXF_b.y+NCDXF_b.h, BORDER_COL);

}
