/* code to manage the earth map
 */

/* main map drawing routines.
 */


#include "HamClock.h"


// DX location and path to DE
SCircle dx_c = {{0,0},DX_R};                    // screen coords of DX symbol
LatLong dx_ll;                                  // geo coords of dx spot

// DE and AntiPodal location
SCircle de_c = {{0,0},DE_R};                    // screen coords of DE symbol
LatLong de_ll;                                  // geo coords of DE
float sdelat, cdelat;                           // handy tri
SCircle deap_c = {{0,0},DEAP_R};                // screen coords of DE antipode symbol
LatLong deap_ll;                                // geo coords of DE antipode

// sun
AstroCir solar_cir;
SCircle sun_c = {{0,0},SUN_R};                  // screen coords of sun symbol
LatLong sun_ss_ll;                              // subsolar location
float csslat, ssslat;                           // handy trig

// moon
AstroCir lunar_cir;
SCircle moon_c = {{0,0},MOON_R};                // screen coords of moon symbol
LatLong moon_ss_ll;                             // sublunar location

// dx options
uint8_t show_km;                                // show great circle dist in km, else miles
uint8_t show_lp;                                // display long path, else short part heading

#define GRAYLINE_COS    (-0.208F)               // cos(90 + grayline angle), we use 12 degs
#define GRAYLINE_POW    (0.75F)                 // cos power exponent, sqrt is too severe, 1 is too gradual
static SCoord moremap_s;                        // drawMoreEarth() scanning location 

// cached grid colors
static uint16_t GRIDC, GRIDC00;                 // main and highlighted

// flag to defer drawing over map until opportune time:
// ESP: draw after any line
// UNIX: draw after entire map
bool mapmenu_pending;


// establish GRIDC and GRIDC00
static void getGridColorCache()
{
    if (GRIDC != 0 || GRIDC00 != 0)
        return;

    // get base color
    GRIDC = getGridColor();

    // invert v
    uint8_t r = RGB565_R(GRIDC);
    uint8_t g = RGB565_G(GRIDC);
    uint8_t b = RGB565_B(GRIDC);
    uint8_t h, s, v;
    rgbtohsv (&h, &s, &v, r, g, b);
    v += 128U;
    hsvtorgb (&r, &g, &b, h, s, v);

    GRIDC00 = RGB565(r,g,b);
}

/* erase the DE symbol by restoring map contents.
 * N.B. we assume coords insure marker will be wholy within map boundaries.
 */
void eraseDEMarker()
{
    eraseSCircle (de_c);
}

/* draw DE marker.
 * N.B. we assume coords insure marker will be wholy within map boundaries.
 */
void drawDEMarker(bool force)
{
    // test for over visible map unless force, eg might be under RSS now
    if (!force && !overMap(de_c.s))
        return;

    tft.fillCircle (de_c.s.x, de_c.s.y, DE_R, RA8875_BLACK);
    tft.drawCircle (de_c.s.x, de_c.s.y, DE_R, DE_COLOR);
    tft.fillCircle (de_c.s.x, de_c.s.y, DE_R/2, DE_COLOR);
}

/* erase the antipode symbol by restoring map contents.
 * N.B. we assume coords insure marker will be wholy within map boundaries.
 */
void eraseDEAPMarker()
{
    eraseSCircle (deap_c);
}

/* draw antipodal marker.
 * N.B. we assume coords insure marker will be wholy within map boundaries.
 */
void drawDEAPMarker()
{
    tft.fillCircle (deap_c.s.x, deap_c.s.y, DEAP_R, DE_COLOR);
    tft.drawCircle (deap_c.s.x, deap_c.s.y, DEAP_R, RA8875_BLACK);
    tft.fillCircle (deap_c.s.x, deap_c.s.y, DEAP_R/2, RA8875_BLACK);
}

/* draw the NVRAM grid square to 4 chars in the given screen location
 */
static void drawMaidenhead(NV_Name nv, SBox &b, uint16_t color)
{
    char maid[MAID_CHARLEN];
    getNVMaidenhead (nv, maid);
    maid[4] = 0;

    tft.fillRect (b.x, b.y, b.w, b.h, RA8875_BLACK);

    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (color);
    tft.setCursor (b.x, b.y+b.h-7);
    tft.print (maid);
}

/* draw de_info_b according to de_time_fmt
 */
void drawDEInfo()
{
    // init info block
    tft.fillRect (de_info_b.x, de_info_b.y, de_info_b.w, de_info_b.h, RA8875_BLACK);

    // draw desired contents
    if (de_time_fmt == DETIME_INFO) {

        uint16_t vspace = de_info_b.h/DE_INFO_ROWS;
        selectFontStyle (LIGHT_FONT, SMALL_FONT);
        tft.setTextColor (DE_COLOR);

        // time
        drawDETime(false);

        // lat and lon
        char buf[30];
        sprintf (buf, "%.0f%c  %.0f%c",
                    roundf(fabsf(de_ll.lat_d)), de_ll.lat_d < 0 ? 'S' : 'N',
                    roundf(fabsf(de_ll.lng_d)), de_ll.lng_d < 0 ? 'W' : 'E');
        tft.setCursor (de_info_b.x, de_info_b.y+2*vspace-6);
        tft.print(buf);

        // maidenhead
        drawMaidenhead(NV_DE_GRID, de_maid_b, DE_COLOR);

        // sun rise/set info
        drawDESunRiseSetInfo();

    } else if (de_time_fmt == DETIME_ANALOG || de_time_fmt == DETIME_ANALOG_DTTM) {

        drawTZ (de_tz);
        updateClocks(true);

    } else if (de_time_fmt == DETIME_CAL) {

        drawDETime(true);
        drawCalendar(true);

    }
}

void drawDETime(bool center)
{
    drawTZ (de_tz);

    // get time
    time_t utc = nowWO();
    time_t local = utc + de_tz.tz_secs;
    int hr = hour (local);
    int mn = minute (local);
    int dy = day(local);
    int mo = month(local);

    // generate text
    char buf[32];
    sprintf (buf, "%02d:%02d %s %d", hr, mn, monthShortStr(mo), dy);

    // set position
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    uint16_t vspace = de_info_b.h/DE_INFO_ROWS;
    uint16_t x0 = de_info_b.x;
    if (center) {
        uint16_t bw = getTextWidth (buf);
        x0 += (de_info_b.w - bw)/2;
    }

    // draw
    tft.fillRect (de_info_b.x, de_info_b.y, de_info_b.w, vspace, RA8875_BLACK);
    tft.setTextColor (DE_COLOR);
    tft.setCursor (x0, de_info_b.y+vspace-6);
    tft.print(buf);
}

/* draw some fake stars for the azimuthal projection
 */
void drawAzmStars()
{
    #define N_AZMSTARS 200
    uint8_t n_stars = 0;
    while (n_stars < N_AZMSTARS) {
        int32_t x = random (map_b.w);
        int32_t y = random (map_b.h);
        int32_t dx = (x > map_b.w/2) ? (x - 3*map_b.w/4) : (x - map_b.w/4);
        int32_t dy = y - map_b.h/2;
        if (dx*dx + dy*dy > map_b.w*map_b.w/16) {
            uint16_t c = random(256);
            c = RGB565(c,c,c);
            tft.drawPixel (map_b.x+x, map_b.y+y, c);
            n_stars++;
        }
    }
}

/* draw the Maidenhead grid key around the map if appropriate.
 */
static void drawMaidGridKey()
{
    // only if selected and using mercator projection
    if (mapgrid_choice != MAPGRID_MAID || azm_on)
        return;

    resetWatchdog();

    // keep right stripe above RSS and DRAP scale, if on
    uint16_t right_h = map_b.h;
    if (rss_on)
        right_h = rss_bnr_b.y - map_b.y;
    if (DRAPScaleIsUp())
        right_h = drap_b.y - map_b.y;           // drap_b.y already above rss if on

    // prep background stripes
    tft.fillRect (map_b.x, map_b.y, map_b.w, MH_TR_H, RA8875_BLACK);                            // top
    tft.fillRect (map_b.x+map_b.w-MH_RC_W, map_b.y, MH_RC_W, right_h, RA8875_BLACK);            // right
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (RA8875_WHITE);

    // print labels across the top
    uint16_t rowy = map_b.y + MH_TR_DY;
    for (uint8_t i = 0; i < 18; i++) {
        LatLong ll;
        SCoord s;
        ll.lat_d = 0;
        ll.lng_d = -180 + (i+0.45F)*360/18;     // center character within square
        ll2s (ll, s, 10);
        tft.setCursor (s.x, rowy);
        tft.print ((char)('A' + (180+ll.lng_d)/20));
    }

    // print labels down the right
    uint16_t colx = map_b.x + map_b.w - MH_RC_W + MH_RC_DX;
    for (uint8_t i = 0; i < 18; i++) {
        uint16_t y = map_b.y + map_b.h - (i+1)*map_b.h/18 + MH_RC_DY;
        if (y < map_b.y + right_h - 8) {        // - font height
            tft.setCursor (colx, y);
            tft.print ((char)('A' + i));
        }
    }

}

#if defined(_IS_UNIX)

/* draw lat/long with given step sizes (used for ll and maidenhead).
 * UNIX only
 */
static void drawLLGrid (int lat_step, int lng_step)
{
    int fine_step = 1;

    if (azm_on) {

        // lines of latitude, exclude the poles
        for (float lat = -90+lat_step; lat < 90; lat += lat_step) {
            SCoord s0, s1;
            ll2s (deg2rad(lat), deg2rad(-180), s0, 0);
            bool s0_left = s0.x < map_b.x+map_b.w/2;
            for (float lng = -180+lng_step; lng <= 180; lng += lng_step) {
                ll2s (deg2rad(lat), deg2rad(lng), s1, 0);
                bool s1_left = s1.x < map_b.x+map_b.w/2;
                if (s0_left == s1_left && !overRSS(s0) && !overRSS(s1)) {
                    // full deg spacing on same hemisphere
                    tft.drawLine (s0.x, s0.y, s1.x, s1.y, lat == 0 ? GRIDC00 : GRIDC);
                } else {
                    // backfill with finer fine_steps
                    for (float lg = lng-lng_step+fine_step; lg <= lng; lg += fine_step) {
                        ll2s (deg2rad(lat), deg2rad(lg), s1, 0);
                        s1_left = s1.x < map_b.x+map_b.w/2;
                        if (s0_left == s1_left && !overRSS(s0) && !overRSS(s1))
                            tft.drawLine (s0.x, s0.y, s1.x, s1.y, lat == 0 ? GRIDC00 : GRIDC);
                        s0 = s1;
                        s0_left = s1_left;
                    }
                }
                s0 = s1;
                s0_left = s1_left;
            }
        }

        // lines of longitude -- pole to pole
        for (float lng = -180; lng < 180; lng += lng_step) {
            SCoord s0, s1;
            ll2s (deg2rad(-90), deg2rad(lng), s0, 0);
            bool s0_left = s0.x < map_b.x+map_b.w/2;
            for (float lat = -90+lat_step; lat <= 90; lat += lat_step) {
                ll2s (deg2rad(lat), deg2rad(lng), s1, 0);
                bool s1_left = s1.x < map_b.x+map_b.w/2;
                if (s0_left == s1_left && !overRSS(s0) && !overRSS(s1)) {
                    // full deg spacing on same hemisphere
                    tft.drawLine (s0.x, s0.y, s1.x, s1.y, lng == 0 ? GRIDC00 : GRIDC);
                } else {
                    // backfill with finer fine_steps
                    for (float lt = lat-lat_step+fine_step; lt <= lat; lt += fine_step) {
                        ll2s (deg2rad(lt), deg2rad(lng), s1, 0);
                        s1_left = s1.x < map_b.x+map_b.w/2;
                        if (s0_left == s1_left && !overRSS(s0) && !overRSS(s1))
                            tft.drawLine (s0.x, s0.y, s1.x, s1.y, lng == 0 ? GRIDC00 : GRIDC);
                        s0 = s1;
                        s0_left = s1_left;
                    }
                }
                s0 = s1;
                s0_left = s1_left;
            }
        }

    } else {

        // easy! just straight lines but beware View menu button

        int n_lngstep = 360/lng_step;
        int n_latstep = 180/lat_step;

        // vertical
        for (int i = 0; i < n_lngstep; i++) {
            LatLong ll;
            SCoord s;
            ll.lat_d = 0;
            ll.lng_d = -180 + i*lng_step;
            ll2s (ll, s, 1);
            uint16_t top_y = s.x < view_btn_b.x + view_btn_b.w ? view_btn_b.y + view_btn_b.h : map_b.y;
            uint16_t bot_y = map_b.y+map_b.h-1;
            if (rss_on)
                bot_y = rss_bnr_b.y - 1;
            if (DRAPScaleIsUp())
                bot_y = drap_b.y - 1;                   // drap_b.y already above rss if on
            tft.drawLine (s.x, top_y, s.x, bot_y, i == n_lngstep/2 ? GRIDC00 : GRIDC);
        }

        // horizontal
        for (int i = 1; i < n_latstep; i++) {
            uint16_t y = map_b.y + i*map_b.h/n_latstep;
            if ((!rss_on || y < rss_bnr_b.y) && (!DRAPScaleIsUp() || y < drap_b.y)) {
                uint16_t left_x = y < view_btn_b.y + view_btn_b.h ? view_btn_b.x + view_btn_b.w : map_b.x;
                tft.drawLine (left_x, y, map_b.x+map_b.w-1, y, i == n_latstep/2 ? GRIDC00 : GRIDC);
            }
        }

    }
}

/* draw the complete proper map grid, ESP draws incrementally as map is drawn.
 * UNIX only
 */
static void drawMapGrid()
{
    resetWatchdog();

    switch (mapgrid_choice) {
    case MAPGRID_MAID:

        drawMaidGridKey();
        drawLLGrid (10, 20);
        break;

    case MAPGRID_LATLNG:

        drawLLGrid (15, 15);
        break;

    case MAPGRID_TROPICS:

        if (azm_on) {

            // just 2 lines at lat +- 23.5
            SCoord s00, s01, s10, s11;
            ll2s (deg2rad(-23.5F), deg2rad(-180), s00, 0);
            ll2s (deg2rad(23.5F), deg2rad(-180), s10, 0);
            for (float lng = -180; lng <= 180; lng += 1) {
                ll2s (deg2rad(-23.5), deg2rad(lng), s01, 0);
                ll2s (deg2rad(23.5), deg2rad(lng), s11, 0);
                if (segmentSpanOk(s00,s01))
                    tft.drawLine (s00.x, s00.y, s01.x, s01.y, GRIDC);
                s00 = s01;
                if (segmentSpanOk(s10,s11))
                    tft.drawLine (s10.x, s10.y, s11.x, s11.y, GRIDC);
                s10 = s11;
            }

        } else {

            // easy! just 2 straight lines
            uint16_t y = map_b.y + map_b.h/2 - 23.5F*map_b.h/180;
            tft.drawLine (map_b.x, y, map_b.x+map_b.w-1, y, GRIDC);
            y = map_b.y + map_b.h/2 + 23.5F*map_b.h/180;
            tft.drawLine (map_b.x, y, map_b.x+map_b.w-1, y, GRIDC);

        }

        break;

    case MAPGRID_OFF:           // fallthru
    default:

        // none
        break;

    }
}

/* draw local information about the current cursor position over the world map.
 * does not work for ESP because there is no way to follow touch without making a tap.
 * called after every map draw so we only have to erase parts of azm outside the hemispheres.
 * UNIX only
 */
static void drawMouseLoc()
{
    resetWatchdog();

    // draw just below map View button
    uint16_t tx = view_btn_b.x;
    uint16_t ty = view_btn_b.y + view_btn_b.h;
    const uint16_t line_dy = 9;

    // size and location of names bar
    const uint16_t names_y = view_btn_b.y;
    const uint16_t names_h = 14;

    // get current mouse location and whether over HamClock window at all.
    uint16_t mx, my;
    bool over_window = tft.getMouse(&mx, &my);

    // get corresponding map location, if any
    LatLong ll;
    bool overmap = over_window && s2ll (mx, my, ll);

    // prep for text
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (RA8875_WHITE);

    // get city if applicable, erase bg if found or cleanup
    static uint16_t prev_cityw;         // previous bg
    LatLong city_ll;
    const char *city = names_on && overmap ? getNearestCity (ll, city_ll) : NULL;
    uint16_t cityw = city ? getTextWidth(city)+10 : 0;
    if (cityw > prev_cityw)             // looks a little better if bg doesn't shrink
        prev_cityw = cityw;
    if (names_on && prev_cityw > 0)
        tft.fillRect (map_b.x + (map_b.w-prev_cityw)/2, names_y, prev_cityw, names_h, RA8875_BLACK);
    if (!city)
        prev_cityw = 0;

    // erase data area if going to show new data or clean up for azm not over hemispheres
    static bool was_overmap;
    if (overmap || (azm_on && was_overmap))
        tft.fillRect (tx, ty, VIEWBTN_W, MOUSELOC_H, RA8875_BLACK);
    was_overmap = overmap;

    // that's it if mouse is not over map
    if (!overmap)
        return;


    // show closest city, if any
    if (city) {
        SCoord s;
        ll2s (city_ll, s, 4);
        tft.fillCircle (s.x, s.y, 4, RA8875_RED);
        tft.setCursor (map_b.x + (map_b.w-cityw)/2, names_y + 3);
        tft.print(city);
    }

    // show lat/long
    tft.setCursor (tx+1, ty+1);
    tft.printf ("%5.1f%c", fabsf(ll.lat_d), ll.lat_d < 0 ? 'S' : 'N');
    tft.setCursor (tx+1, ty+=line_dy);
    tft.printf ("%5.1f%c", fabsf(ll.lng_d), ll.lng_d < 0 ? 'W' : 'E');

    // show maid
    char maid[MAID_CHARLEN];
    ll2maidenhead (maid, ll);
    tft.setCursor (tx+13, ty+=line_dy);
    tft.printf ("%.4s", maid);

    // show local time
    time_t lt = nowWO() + getTZ(ll);
    tft.setCursor (tx+7, ty+=line_dy);
    tft.printf ("%02d:%02d", hour(lt), minute(lt));

    // show distance and bearing to cursor location
    float dist, bearing;
    propDEDXPath (show_lp, ll, &dist, &bearing);
    dist *= ERAD_M;                             // angle to miles
    bearing *= 180/M_PIF;                       // rad -> degrees
    if (show_km)
        dist *= 1.609344F;                      // mi - > km
    tft.setCursor (tx+1, ty+=line_dy);
    tft.printf ("%s %3.0f", show_lp ? "LP" : "SP", bearing);
    tft.setCursor (tx+1, ty+=line_dy);
    if (dist <= 999)
        tft.printf ("%s %3.0f", show_km ? "km" : "mi", dist);
    else if (dist <= 9900)
        tft.printf ("%s%3.1fk", show_km ? "km" : "mi", dist/1000);
    else
        tft.printf ("%s %2.0fk", show_km ? "km" : "mi", dist/1000);

    // prefix
    char prefix[MAX_PREF_LEN+1];
    if (nearestPrefix (city ? city_ll : ll, prefix)) {
        tft.setCursor (tx+1, ty+=line_dy);
        tft.printf ("%6s", prefix);
    }
}

#endif // _IS_UNIX

static void updateCircumstances()
{
    time_t utc = nowWO();

    getSolarCir (utc, de_ll, solar_cir);
    sun_ss_ll.lat_d = rad2deg(solar_cir.dec);
    sun_ss_ll.lng_d = -rad2deg(solar_cir.gha);
    normalizeLL (sun_ss_ll);
    csslat = cosf(sun_ss_ll.lat);
    ssslat = sinf(sun_ss_ll.lat);
    ll2s (sun_ss_ll, sun_c.s, SUN_R+1);

    getLunarCir (utc, de_ll, lunar_cir);
    moon_ss_ll.lat_d = rad2deg(lunar_cir.dec);
    moon_ss_ll.lng_d = -rad2deg(lunar_cir.gha);
    normalizeLL (moon_ss_ll);
    ll2s (moon_ss_ll, moon_c.s, MOON_R+1);

    updateSatPath();
}

/* draw the map view menu button.
 * adjust position depending on whether we are drawing the maidenhead labels.
 * adjust view_pick_b to match.
 */
static void drawMapMenuButton()
{
    resetWatchdog();

    if (mapgrid_choice == MAPGRID_MAID && !azm_on)
        view_pick_b.y = view_btn_b.y = map_b.y + MH_TR_H;
    else
        view_pick_b.y = view_btn_b.y = map_b.y;

    // 1 pixel inside so onMap() gives 2-pixel thick sat footprints some room
    tft.fillRect (view_btn_b.x, view_btn_b.y, view_btn_b.w-1, view_btn_b.h-1, RA8875_BLACK);
    tft.drawRect (view_btn_b.x, view_btn_b.y, view_btn_b.w-1, view_btn_b.h-1, RA8875_WHITE);

    const char *str = "View";
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    uint16_t str_w = getTextWidth(str);
    tft.setCursor (view_btn_b.x+(view_btn_b.w-str_w)/2, view_btn_b.y+2);
    tft.setTextColor (RA8875_WHITE);
    tft.print (str);
}

/* erase the RSS box
 */
void eraseRSSBox ()
{
    resetWatchdog();

    // erase entire banner if azm mode because redrawing the map will miss the corners
    if (azm_on)
        tft.fillRect (rss_bnr_b.x, rss_bnr_b.y, rss_bnr_b.w, rss_bnr_b.h, RA8875_BLACK);

    // restore map and sat path
    for (uint16_t y = rss_bnr_b.y; y < rss_bnr_b.y+rss_bnr_b.h; y++) {
        updateClocks(false);
        for (uint16_t x = rss_bnr_b.x; x < rss_bnr_b.x+rss_bnr_b.w; x++)
            drawMapCoord (x, y);
        drawSatPointsOnRow (y);
    }

    // restore maid key
    drawMaidGridKey();
}

/* draw, perform and engage results of the map View menu
 */
void drawMapMenu()
{
    enum MIName {                               // menu items -- N.B. must be in same order as mitems[]
        MI_STY_TTL, MI_STY_CTY, MI_STY_TER, MI_STY_DRA, MI_STY_PRP,
        MI_GRD_TTL, MI_GRD_NON, MI_GRD_TRO, MI_GRD_LLG, MI_GRD_MAI,
        MI_PRJ_TTL, MI_PRJ_AZM, MI_PRJ_MER,
        MI_RSS_YES,
        MI_NON_YES,
    #if defined(_IS_UNIX)
        MI_PLA_YES,
    #endif
        MI_N
    };
    #define PRI_INDENT 2
    #define SEC_INDENT 8
    MenuItem mitems[MI_N] = {
        {MENU_TITLE, false, PRI_INDENT, "Style:"},
            {MENU_1OFN, false, SEC_INDENT, map_styles[CM_COUNTRIES]},
            {MENU_1OFN, false, SEC_INDENT, map_styles[CM_TERRAIN]},
            {MENU_1OFN, false, SEC_INDENT, map_styles[CM_DRAP]},
            {MENU_IGNORE, false, SEC_INDENT, NULL},     // see later
        {MENU_TITLE, false, PRI_INDENT, "Grid:"},
            {MENU_1OFN, false, SEC_INDENT, "None"},
            {MENU_1OFN, false, SEC_INDENT, "Tropics"},
            {MENU_1OFN, false, SEC_INDENT, "Lat/Long"},
            {MENU_1OFN, false, SEC_INDENT, "Maidenhead"},
        {MENU_TITLE, false, PRI_INDENT, "Projection:"},
            {MENU_1OFN, false, SEC_INDENT, "Azimuthal"},
            {MENU_1OFN, false, SEC_INDENT, "Mercator"},
        {MENU_TOGGLE, false, PRI_INDENT, "RSS"},
        {MENU_TOGGLE, false, PRI_INDENT, "Night"},
        #if defined(_IS_UNIX)
            {MENU_TOGGLE, false, PRI_INDENT, "Names"},
        #endif
    };
    Menu menu = {
        1,      // n_cols
        0,      // n_rows -- see later
        MI_N,   // n_items
        mitems
    };

    // init selections with current states

    // if showing a propmap list in menu as selected else core map
    char propband[NV_MAPSTYLE_LEN];             // must be persistent for runMenu()
    if (prop_map != PROP_MAP_OFF) {
        menu.items[MI_STY_PRP].type = MENU_1OFN;
        menu.items[MI_STY_PRP].set = true;
        menu.items[MI_STY_PRP].label = getMapStyle (propband);
        menu.n_rows = MI_N;                     // use all rows
    } else {
        menu.items[MI_STY_CTY].set = core_map == CM_COUNTRIES;
        menu.items[MI_STY_TER].set = core_map == CM_TERRAIN;
        menu.items[MI_STY_DRA].set = core_map == CM_DRAP;
        menu.n_rows = MI_N - 1;                 // 1 IGNORE row
    }

    menu.items[MI_GRD_NON].set = mapgrid_choice == MAPGRID_OFF;
    menu.items[MI_GRD_TRO].set = mapgrid_choice == MAPGRID_TROPICS;
    menu.items[MI_GRD_LLG].set = mapgrid_choice == MAPGRID_LATLNG;
    menu.items[MI_GRD_MAI].set = mapgrid_choice == MAPGRID_MAID;

    menu.items[MI_PRJ_AZM].set = azm_on;
    menu.items[MI_PRJ_MER].set = !azm_on;

    menu.items[MI_RSS_YES].set = rss_on;
    menu.items[MI_NON_YES].set = night_on;
    #if defined(_IS_UNIX)
        menu.items[MI_PLA_YES].set = names_on;
    #endif

    // create a box for the menu
    SBox menu_b;
    menu_b.x = view_btn_b.x;                    // left edge matches view button
    menu_b.y = view_btn_b.y+view_btn_b.h;       // top just below view button
    menu_b.w = VIEWMENU_W;                      // enough for widest string
    menu_b.h = 0;                               // set by runMenu

    // run menu
    SBox ok_b;
    bool menu_ok = runMenu (menu, map_b, menu_b, ok_b);

    bool full_redraw = false;
    if (menu_ok) {

        resetWatchdog();

        // set Ok yellow while processing
        menuRedrawOk (ok_b, MENU_OK_BUSY);

        // update map if style changed; restore core_map if prop_map turned off
        CoreMaps new_cm = CM_NONE;
        if (prop_map != PROP_MAP_OFF && !menu.items[MI_STY_PRP].set)
            new_cm = core_map;
        else if (menu.items[MI_STY_CTY].set && core_map != CM_COUNTRIES)
            new_cm = CM_COUNTRIES;
        else if (menu.items[MI_STY_TER].set && core_map != CM_TERRAIN)
            new_cm = CM_TERRAIN;
        else if (menu.items[MI_STY_DRA].set && core_map != CM_DRAP)
            new_cm = CM_DRAP;
        if (new_cm != CM_NONE) {
            if (installNewMapStyle (new_cm))
                full_redraw = true;
            else {
                menuRedrawOk (ok_b, MENU_OK_ERR);
                wdDelay(1000);
            }
        }

        // check for new grid
        if (menu.items[MI_GRD_NON].set && mapgrid_choice != MAPGRID_OFF) {
            mapgrid_choice = MAPGRID_OFF;
            NVWriteUInt8 (NV_LLGRID, mapgrid_choice);
            full_redraw = true;
        } else if (menu.items[MI_GRD_TRO].set && mapgrid_choice != MAPGRID_TROPICS) {
            mapgrid_choice = MAPGRID_TROPICS;
            NVWriteUInt8 (NV_LLGRID, mapgrid_choice);
            full_redraw = true;
        } else if (menu.items[MI_GRD_LLG].set && mapgrid_choice != MAPGRID_LATLNG) {
            mapgrid_choice = MAPGRID_LATLNG;
            NVWriteUInt8 (NV_LLGRID, mapgrid_choice);
            full_redraw = true;
        } else if (menu.items[MI_GRD_MAI].set && mapgrid_choice != MAPGRID_MAID) {
            mapgrid_choice = MAPGRID_MAID;
            NVWriteUInt8 (NV_LLGRID, mapgrid_choice);
            full_redraw = true;
        }

        // check for different azm/merc
        if (menu.items[MI_PRJ_AZM].set != azm_on) {
            azm_on = menu.items[MI_PRJ_AZM].set;
            NVWriteUInt8 (NV_AZIMUTHAL_ON, azm_on);
            full_redraw = true;
        }

        // check for change night option
        if (menu.items[MI_NON_YES].set != night_on) {
            night_on = menu.items[MI_NON_YES].set;
            NVWriteUInt8 (NV_NIGHT_ON, night_on);
            full_redraw = true;
        }


    #if defined(_IS_UNIX)
        // check for change of names option
        if (menu.items[MI_PLA_YES].set != names_on) {
            names_on = menu.items[MI_PLA_YES].set;
            NVWriteUInt8 (NV_NAMES_ON, names_on);
        }
    #endif

        // check for changed RSS -- N.B. do this last to utilize full_redraw
        if (menu.items[MI_RSS_YES].set != rss_on) {
            rss_on = menu.items[MI_RSS_YES].set;
            NVWriteUInt8 (NV_RSS_ON, rss_on);

            // do minimal restore if not restart map
            if (!full_redraw) {
                if (rss_on) {
                    scheduleRSSNow();
                    if (DRAPScaleIsUp()) {
                        eraseDRAPScale();       // erase where it is now
                        drawDRAPScale();        // draw in new location
                        drawMaidGridKey();      // tidy up
                    }
                } else {
                    if (DRAPScaleIsUp())
                        eraseDRAPScale();
                    eraseRSSBox();
                    if (DRAPScaleIsUp())
                        drawDRAPScale();
                    drawMaidGridKey();
                }
            }
        }

        // restart map if it has changed
        if (full_redraw)
            initEarthMap();

        // update state
        logState();
    }

    if (!menu_ok || !full_redraw) {
        // just erase menu
        // TODO: black rectangle is for azm mode, better to restore stars
        resetWatchdog();
        tft.fillRect (menu_b.x, menu_b.y, menu_b.w, menu_b.h, RA8875_BLACK);
        for (uint16_t dy = 0; dy < menu_b.h; dy++)
            for (uint16_t dx = 0; dx < menu_b.w; dx++)
                drawMapCoord (menu_b.x+dx, menu_b.y+dy);
    }

    tft.drawPR();

    // discard any extra taps
    drainTouch();

    printFreeHeap (F("drawMapMenu"));

}

/* restart map given de_ll and dx_ll
 */
void initEarthMap()
{
    resetWatchdog();

    // completely erase map
    tft.fillRect (map_b.x, map_b.y, map_b.w, map_b.h, RA8875_BLACK);

    // add funky star field if azm
    if (azm_on)
        drawAzmStars();

    // get grid colors
    getGridColorCache();

    // freshen RSS and clocks
    scheduleRSSNow();
    updateClocks(true);

    // draw map view button over map
    drawMapMenuButton();

    // reset any pending great circle path
    setDXPathInvalid();

    // update astro info
    updateCircumstances();

    // update DE and DX info
    sdelat = sinf(de_ll.lat);
    cdelat = cosf(de_ll.lat);
    ll2s (de_ll, de_c.s, DE_R);
    antipode (deap_ll, de_ll);
    ll2s (deap_ll, deap_c.s, DEAP_R);
    ll2s (dx_ll, dx_c.s, DX_R);

    // show updated info
    drawDEInfo();
    drawDXInfo();

    // insure NCDXF and DX spots screen coords match current map type
    updateBeaconScreenLocations();
    updateDXClusterSpotScreenLocations();

    // init scan line in map_b
    moremap_s.x = 0;                    // avoid updateCircumstances() first call to drawMoreEarth()
    moremap_s.y = map_b.y;

    // now main loop can resume with drawMoreEarth()
}

/* display another earth map row at mmoremap_s.
 * ESP draws map one line at a time, others draw all the map then all the symbols to overlay.
 */
void drawMoreEarth()
{
    resetWatchdog();

    // handy health indicator and update timer
    digitalWrite(LIFE_LED, !digitalRead(LIFE_LED));

    // refresh circumstances at start of each map scan but not very first call after initEarthMap()
    if (moremap_s.y == map_b.y && moremap_s.x != 0)
        updateCircumstances();
    
    uint16_t last_x = map_b.x + EARTH_W - 1;

#if defined(_IS_ESP8266)

    // freeze if showing a temporary DX-DE path
    if (waiting4DXPath())
        return;

    // draw all symbols when hit first one after start of sweep, maid key right away
    static bool drew_symbols;
    if (moremap_s.y == map_b.y) {
        drew_symbols = false;
        drawMaidGridKey();
    }

    // draw next row, avoid symbols but note when hit
    resetWatchdog();
    bool hit_symbol = false;
    for (moremap_s.x = map_b.x; moremap_s.x <= last_x; moremap_s.x++) {

        // make symbols appear as overlaied by not drawing map over them.
        if (overAnySymbol (moremap_s))
            hit_symbol = true;
        else
            drawMapCoord (moremap_s);           // also draws grid
    }

    // draw symbols first time hit
    if (!drew_symbols && hit_symbol) {
        drawAllSymbols(false);
        drew_symbols = true;
    }

    // overlay any sat lines on this row except drap scale
    // N.B. can't use !inBox(moremap_s, drap_b) because .x is off the map now
    if (!DRAPScaleIsUp() || moremap_s.y < drap_b.y || moremap_s.y > drap_b.y + drap_b.h) {
        drawSatPointsOnRow (moremap_s.y);
        drawSatNameOnRow (moremap_s.y);
    }

    // advance row and wrap and reset at the end
    if ((moremap_s.y += 1) >= map_b.y + EARTH_H)
        moremap_s.y = map_b.y;

    // check for map menu after each row
    if (mapmenu_pending) {
        drawMapMenu();
        mapmenu_pending = false;
    }

#endif  // _IS_ESP8266

#if defined(_IS_UNIX)

    // draw next row
    for (moremap_s.x = map_b.x; moremap_s.x <= last_x; moremap_s.x++)
        drawMapCoord (moremap_s);               // does not draw grid

    // advance row, wrap and reset and finish up at the end
    if ((moremap_s.y += 1) >= map_b.y + EARTH_H) {
        moremap_s.y = map_b.y;

        drawMapGrid();
        drawHeadingPath();
        drawSatPathAndFoot();
        drawSatNameOnRow (0);
        drawAllSymbols(false);
        if (waiting4DXPath())
            drawDXPath();
        drawMouseLoc();

        // draw now
        tft.drawPR();

        // check for map menu after each full map
        if (mapmenu_pending) {
            drawMapMenu();
            mapmenu_pending = false;
        }
    }

#endif // _IS_UNIX

}

/* convert lat and long in radians to screen coords.
 * keep result no closer than the given edge distance.
 * N.B. we assume lat/lng are in range [-90,90] [-180,180)
 */
void ll2s (float lat, float lng, SCoord &s, uint8_t edge)
{
    LatLong ll;
    ll.lat = lat;
    ll.lat_d = rad2deg(ll.lat);
    ll.lng = lng;
    ll.lng_d = rad2deg(ll.lng);
    ll2s (ll, s, edge);
}
void ll2s (const LatLong &ll, SCoord &s, uint8_t edge)
{
    resetWatchdog();

    if (azm_on) {

        // azimuthal projection

        // sph tri between de, dx and N pole
        float ca, B;
        solveSphere (ll.lng - de_ll.lng, M_PI_2F-ll.lat, sdelat, cdelat, &ca, &B);

        if (ca > 0) {
            // front (left) side, centered at DE
            float a = acosf (ca);
            float R = fminf (a*map_b.w/(2*M_PIF), map_b.w/4 - edge - 1);        // well clear
            float dx = R*sinf(B);
            float dy = R*cosf(B);
            s.x = roundf(map_b.x + map_b.w/4 + dx);
            s.y = roundf(map_b.y + map_b.h/2 - dy);
        } else {
            // back (right) side, centered at DE antipode
            float a = M_PIF - acosf (ca);
            float R = fminf (a*map_b.w/(2*M_PIF), map_b.w/4 - edge - 1);        // well clear
            float dx = -R*sinf(B);
            float dy = R*cosf(B);
            s.x = roundf(map_b.x + 3*map_b.w/4 + dx);
            s.y = roundf(map_b.y + map_b.h/2 - dy);
        }

    } else {

        // straight rectangular Mercator projection
        s.x = roundf(map_b.x + map_b.w*fmodf(ll.lng_d-getCenterLng()+540,360)/360);
        s.y = roundf(map_b.y + map_b.h*(90-ll.lat_d)/180);

        // guard edge
        uint16_t e;
        e = map_b.x + edge;
        if (s.x < e)
            s.x = e;
        e = map_b.x + map_b.w - edge - 1;
        if (s.x > e)
            s.x = e;
        e = map_b.y + edge;
        if (s.y < e)
            s.y = e;
        e = map_b.y + map_b.h - edge - 1;
        if (s.y > e)
            s.y = e;
    }

}

/* convert a screen coord to lat and long.
 * return whether location is really over valid map.
 */
bool s2ll (uint16_t x, uint16_t y, LatLong &ll)
{
    SCoord s;
    s.x = x;
    s.y = y;
    return (s2ll (s, ll));
}
bool s2ll (const SCoord &s, LatLong &ll)
{
    if (!overMap(s))
        return (false);

    if (azm_on) {

        // radius from center of point's hemisphere
        bool on_right = s.x > map_b.x + map_b.w/2;
        int32_t dx = on_right ? s.x - (map_b.x + 3*map_b.w/4) : s.x - (map_b.x + map_b.w/4);
        int32_t dy = (map_b.y + map_b.h/2) - s.y;
        int32_t r2 = dx*dx + dy*dy;

        // see if really on surface
        int32_t w2 = map_b.w*map_b.w/16;
        if (r2 > w2)
            return(false);

        // use screen triangle to find globe
        float b = sqrtf((float)r2/w2)*(M_PI_2F);
        float A = (M_PI_2F) - atan2f (dy, dx);
        float ca, B;
        solveSphere (A, b, (on_right ? -1 : 1) * sdelat, cdelat, &ca, &B);
        float lt = M_PI_2F - acosf(ca);
        ll.lat_d = rad2deg(lt);
        float lg = fmodf (de_ll.lng + B + (on_right?6:5)*M_PIF, 2*M_PIF) - M_PIF;
        ll.lng_d = rad2deg(lg);

    } else {

        // straight rectangular mercator projection

        ll.lat_d = 90 - 180.0F*(s.y - map_b.y)/(EARTH_H);
        ll.lng_d = fmodf(360.0F*(s.x - map_b.x)/(EARTH_W)+getCenterLng()+720,360) - 180;

    }

    normalizeLL(ll);

    return (true);
}

/* given numeric difference between two longitudes in degrees, return shortest diff
 */
float lngDiff (float dlng)
{
    float fdiff = fmodf(fabsf(dlng + 720), 360);
    if (fdiff > 180)
        fdiff = 360 - fdiff;
    return (fdiff);
}

#if defined(_IS_ESP8266)

/* given lat/lng and cos of angle from terminator, return earth map pixel.
 * only used by ESP, all others draw at higher resolution.
 */
static uint16_t getEarthMapPix (LatLong ll, float cos_t)
{
    // indices into pixel array at this location
    uint16_t ex = (uint16_t)((EARTH_W*(ll.lng_d+180)/360)+0.5F) % EARTH_W;
    uint16_t ey = (uint16_t)((EARTH_H*(90-ll.lat_d)/180)+0.5F) % EARTH_H;

    // final color
    uint16_t pix_c;

    // decide color
    if (!night_on || cos_t > 0) {
        // < 90 deg: full sunlit
        getMapDayPixel (ey, ex, &pix_c);
    } else if (cos_t > GRAYLINE_COS) {
        // blend from day to night
        uint16_t day_c, night_c;
        getMapDayPixel (ey, ex, &day_c);
        getMapNightPixel (ey, ex, &night_c);
        uint8_t day_r = RGB565_R(day_c);
        uint8_t day_g = RGB565_G(day_c);
        uint8_t day_b = RGB565_B(day_c);
        uint8_t night_r = RGB565_R(night_c);
        uint8_t night_g = RGB565_G(night_c);
        uint8_t night_b = RGB565_B(night_c);
        float fract_night = powf(cos_t/GRAYLINE_COS, GRAYLINE_POW);
        float fract_day = 1 - fract_night;
        uint8_t twi_r = (fract_day*day_r + fract_night*night_r);
        uint8_t twi_g = (fract_day*day_g + fract_night*night_g);
        uint8_t twi_b = (fract_day*day_b + fract_night*night_b);
        pix_c = RGB565 (twi_r, twi_g, twi_b);
    } else {
        // full night side
        getMapNightPixel (ey, ex, &pix_c);
    }

    return (pix_c);
}

#endif

/* draw at the given screen location, if it's over the map.
 * ESP also draws the grid one point at a time.
 */
void drawMapCoord (uint16_t x, uint16_t y)
{

    SCoord s;
    s.x = x;
    s.y = y;
    drawMapCoord (s);
}
void drawMapCoord (const SCoord &s)
{

    #if defined(_IS_ESP8266)

        // draw one pixel, which might be an annotation line if over map

        // a latitude cache really helps Mercator time; anything help Azimuthal??
        static float slat_c, clat_c;
        static SCoord s_c;

        // find lat/lng at this screen location, done if not over map
        LatLong lls;
        if (!s2ll(s, lls))
            return;

        // update handy Mercator cache, but always required for Azm.
        if (azm_on || s.y != s_c.y) {
            s_c = s;
            slat_c = sinf(lls.lat);
            clat_c = cosf(lls.lat);
        }

        // draw lat/long grid if enabled
        #define DLAT        0.6F
        #define DLNG        (0.5F/clat_c)

        switch (mapgrid_choice) {
        case MAPGRID_LATLNG:

            if (azm_on) {

                if (fmodf(lls.lat_d+90, 15) < DLAT || fmodf (lls.lng_d+180, 15) < DLNG) {
                    uint32_t grid_c = (fabsf (lls.lat_d) < DLAT || fabsf (lls.lng_d) < DLNG) ? GRIDC00:GRIDC;
                    tft.drawPixel (s.x, s.y, grid_c);
                    return;                                         // done
                }

            } else {

                // extra gymnastics are because pixels-per-division is not integral and undo getCenterLng
                #define ALL_PPLG (EARTH_W/(360/15))
                #define ALL_PPLT (EARTH_H/(180/15))
                uint16_t x = map_b.x + ((s.x - map_b.x + map_b.w + map_b.w*getCenterLng()/360) % map_b.w);
                if ( (((x - map_b.x) - (x - map_b.x)/(2*ALL_PPLG)) % ALL_PPLG) == 0
                                    || (((s.y - map_b.y) - (s.y - map_b.y)/(2*ALL_PPLT)) % ALL_PPLT) == 0) {
                    uint32_t grid_c = (fabsf (lls.lat_d) < DLAT || fabsf (lls.lng_d) < DLNG) ? GRIDC00:GRIDC;
                    tft.drawPixel (s.x, s.y, grid_c);
                    return;                                         // done
                }
            }

            break;

        case MAPGRID_TROPICS:

            if (azm_on) {

                if (fabsf (fabsf (lls.lat_d) - 23.5F) < 0.3F) {
                    tft.drawPixel (s.x, s.y, GRIDC);
                    return;                                         // done
                }

            } else {

                // we already know exactly where the grid lines go.
                if (abs(s.y - (map_b.y+EARTH_H/2)) == (uint16_t)((23.5F/180)*(EARTH_H))) {
                    tft.drawPixel (s.x, s.y, GRIDC);
                    return;                                         // done
                }
            }
            break;

        case MAPGRID_MAID:

            if (azm_on) {

                if (fmodf(lls.lat_d+90, 10) < DLAT || fmodf (lls.lng_d+180, 20) < DLNG) {
                    uint32_t grid_c = (fabsf (lls.lat_d) < DLAT || fabsf (lls.lng_d) < DLNG) ? GRIDC00:GRIDC;
                    tft.drawPixel (s.x, s.y, grid_c);
                    return;                                         // done
                }

            } else {

                // extra gymnastics are because pixels-per-division is not integral and undo getCenterLng
                #define MAI_PPLG (EARTH_W/(360/20))
                #define MAI_PPLT (EARTH_H/(180/10))
                uint16_t x = map_b.x + ((s.x - map_b.x + map_b.w + map_b.w*getCenterLng()/360) % map_b.w);
                if ( (((x - map_b.x) - 2*(x - map_b.x)/(3*MAI_PPLG)) % MAI_PPLG) == 0
                                    || (((s.y - map_b.y) - (s.y - map_b.y)/(3*MAI_PPLT)) % MAI_PPLT) == 0) {
                    tft.drawPixel (s.x, s.y, GRIDC);
                    return;                                         // done
                }
            }

            break;

        default:

            // none
            break;

        }

        // if get here we did not draw a lat/long grid point

        // find angle between subsolar point and this location
        float cos_t = ssslat*slat_c + csslat*clat_c*cosf(sun_ss_ll.lng-lls.lng);

        uint16_t pix_c = getEarthMapPix (lls, cos_t);
        tft.drawPixel (s.x, s.y, pix_c);

        // preserve for next call
        s_c = s;


    #else // !_IS_ESP8266


        // draw one map pixel at full screen resolution. requires lat/lng gradients.

        // find lat/lng at this screen location, bale if not over map
        LatLong lls;
        if (!s2ll(s,lls))
            return; 

        /* even though we only draw one application point, s, plotEarth needs points r and d to
         * interpolate to full map resolution.
         *   s - - - r
         *   |
         *   d
         */
        SCoord sr, sd;
        LatLong llr, lld;
        sr.x = s.x + 1;
        sr.y = s.y;
        if (!s2ll(sr,llr))
            llr = lls;
        sd.x = s.x;
        sd.y = s.y + 1;
        if (!s2ll(sd,lld))
            lld = lls;

        // find angle between subsolar point and any visible near this location
        // TODO: actually different at each subpixel, this causes striping
        float clat = cosf(lls.lat);
        float slat = sinf(lls.lat);
        float cos_t = ssslat*slat + csslat*clat*cosf(sun_ss_ll.lng-lls.lng);

        // decide day, night or twilight
        float fract_day;
        if (!night_on || cos_t > 0) {
            // < 90 deg: sunlit
            fract_day = 1;
        } else if (cos_t > GRAYLINE_COS) {
            // blend from day to night
            fract_day = 1 - powf(cos_t/GRAYLINE_COS, GRAYLINE_POW);
        } else {
            // night side
            fract_day = 0;
        }

        // draw the full res map point
        tft.plotEarth (s.x, s.y, lls.lat_d, lls.lng_d, llr.lat_d - lls.lat_d, llr.lng_d - lls.lng_d,
                    lld.lat_d - lls.lat_d, lld.lng_d - lls.lng_d, fract_day);

    #endif  // _IS_ESP8266

}

/* draw sun symbol.
 * N.B. we assume sun_c coords insure marker will be wholy within map boundaries.
 */
void drawSun ()
{
    resetWatchdog();

#   define      N_SUN_RAYS      12
    uint16_t body_r = 5*SUN_R/9;
    tft.fillCircle (sun_c.s.x, sun_c.s.y, SUN_R, RA8875_BLACK);
    tft.fillCircle (sun_c.s.x, sun_c.s.y, body_r, RA8875_YELLOW);
    for (uint8_t i = 0; i < N_SUN_RAYS; i++) {
        float a = i*2*M_PIF/N_SUN_RAYS;
        float sa = sinf(a);
        float ca = cosf(a);
        uint16_t x0 = sun_c.s.x + (body_r+2)*ca + 0.5F;
        uint16_t y0 = sun_c.s.y + (body_r+2)*sa + 0.5F;
        uint16_t x1 = sun_c.s.x + (SUN_R)*ca + 0.5F;
        uint16_t y1 = sun_c.s.y + (SUN_R)*sa + 0.5F;
        tft.drawLine (x0, y0, x1, y1, RA8875_YELLOW);
    }
#   undef N_SUN_RAYS
}

/* draw moon symbol.
 * N.B. we assume moon_c coords insure marker will be wholy within map boundaries.
 */
void drawMoon ()
{
    resetWatchdog();

    float phase = lunar_cir.phase;
    
    const uint16_t mr = MOON_R*tft.SCALESZ;             // moon radius on output device
    for (int16_t dy = -mr; dy <= mr; dy++) {            // scan top to bottom
        float Ry = sqrtf(mr*mr-dy*dy);                  // half-width at y
        int16_t Ryi = floorf(Ry+0.5F);                  // " as int
        for (int16_t dx = -Ryi; dx <= Ryi; dx++) {      // scan left to right at y
            float a = acosf((float)dx/Ryi);             // looking down from NP CW from right limb
            tft.drawSubPixel (tft.SCALESZ*moon_c.s.x+dx, tft.SCALESZ*moon_c.s.y+dy,
                    (isnan(a) || (phase > 0 && a > phase) || (phase < 0 && a < phase+M_PIF))
                        ? RA8875_BLACK : RA8875_WHITE);
        }
    }
}

/* display some info about DX location in dx_info_b
 */
void drawDXInfo ()
{
    resetWatchdog();

    // skip if dx_info_b being used for sat info
    if (dx_info_for_sat)
        return;

    // divide into 5 rows
    uint16_t vspace = dx_info_b.h/DX_INFO_ROWS;

    // time
    drawDXTime();

    // erase and init
    tft.graphicsMode();
    tft.fillRect (dx_info_b.x, dx_info_b.y+2*vspace, dx_info_b.w, dx_info_b.h-2*vspace+1, RA8875_BLACK);
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (DX_COLOR);

    // lat and long
    char buf[30];
    sprintf (buf, "%.0f%c  %.0f%c",
                roundf(fabsf(dx_ll.lat_d)), dx_ll.lat_d < 0 ? 'S' : 'N',
                roundf(fabsf(dx_ll.lng_d)), dx_ll.lng_d < 0 ? 'W' : 'E');
    tft.setCursor (dx_info_b.x, dx_info_b.y+3*vspace-8);
    tft.print(buf);
    uint16_t bw, bh;
    getTextBounds (buf, &bw, &bh);

    // maidenhead
    drawMaidenhead(NV_DX_GRID, dx_maid_b, DX_COLOR);

    // compute dist and bearing in desired units
    float dist, bearing;
    propDEDXPath (show_lp, dx_ll, &dist, &bearing);
    dist *= ERAD_M;                             // angle to miles
    bearing *= 180/M_PIF;                       // rad -> degrees
    if (show_km)
        dist *= 1.609344F;                      // mi - > km

    // print, capturing where units and deg/path can go
    tft.setCursor (dx_info_b.x, dx_info_b.y+5*vspace-4);
    tft.printf ("%.0f", dist);
    uint16_t units_x = tft.getCursorX()+2;
    tft.setCursor (units_x + 6, tft.getCursorY());
    tft.printf ("@%.0f", bearing);
    uint16_t deg_x = tft.getCursorX() + 3;
    uint16_t deg_y = tft.getCursorY();

    // home-made degree symbol
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setCursor (deg_x, deg_y-bh-bh/5);
    tft.print ('o');

    // path direction
    tft.setCursor (deg_x, deg_y-bh/2-bh/5);
    tft.print (show_lp ? 'L' : 'S');
    tft.setCursor (deg_x, deg_y-bh/3);
    tft.print ('P');

    // distance units
    if (show_km) {
        tft.setCursor (units_x, deg_y-bh/2-bh/5);
        tft.print('k');
        tft.setCursor (units_x, deg_y-bh/3);
        tft.print('m');
    } else {
        tft.setCursor (units_x, deg_y-bh/2-bh/5);
        tft.print('m');
        tft.setCursor (units_x, deg_y-bh/3);
        tft.print('i');
    }

    // sun rise/set or prefix
    if (dxsrss == DXSRSS_PREFIX) {
        char prefix[MAX_PREF_LEN+1];
        tft.fillRect (dxsrss_b.x, dxsrss_b.y, dxsrss_b.w, dxsrss_b.h, RA8875_BLACK);
        if (getDXPrefix (prefix)) {
            tft.setTextColor(DX_COLOR);
            selectFontStyle (LIGHT_FONT, SMALL_FONT);
            bw = getTextWidth (prefix);
            tft.setCursor (dxsrss_b.x+(dxsrss_b.w-bw)/2, dxsrss_b.y + 28);
            tft.print (prefix);
        }
    } else {
        drawDXSunRiseSetInfo();
    }
}

/* return whether s is over DX distance portion of dx_info_b
 */
bool checkDistTouch (const SCoord &s)
{
    uint16_t vspace = dx_info_b.h/DX_INFO_ROWS;

    SBox b;
    b.x = dx_info_b.x;
    b.w = dx_info_b.w/2;
    b.y = dx_info_b.y + 4*vspace;
    b.h = vspace;

    return (inBox (s, b));
}

/* return whether s is over DX path direction portion of dx_info_b
 */
bool checkPathDirTouch (const SCoord &s)
{
    uint16_t vspace = dx_info_b.h/DX_INFO_ROWS;

    SBox b;
    b.x = dx_info_b.x + dx_info_b.w/2;
    b.w = dx_info_b.w/2;
    b.y = dx_info_b.y + 4*vspace;
    b.h = vspace;

    return (inBox (s, b));
}

/* draw DX time unless in sat mode
 */
void drawDXTime()
{
    // skip if dx_info_b being used for sat info
    if (dx_info_for_sat)
        return;

    drawTZ (dx_tz);

    uint16_t vspace = dx_info_b.h/DX_INFO_ROWS;

    time_t utc = nowWO();
    time_t local = utc + dx_tz.tz_secs;
    int hr = hour (local);
    int mn = minute (local);
    int dy = day(local);
    int mo = month(local);

    tft.graphicsMode();
    tft.fillRect (dx_info_b.x, dx_info_b.y+vspace, dx_info_b.w, vspace, RA8875_BLACK);
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor (DX_COLOR);
    tft.setCursor (dx_info_b.x, dx_info_b.y+2*vspace-8);

    char buf[32];
    sprintf (buf, "%02d:%02d %s %d", hr, mn, monthShortStr(mo), dy);
    tft.print(buf);
}

/* set `to' to the antipodal location of coords in `from'.
 */
void antipode (LatLong &to, const LatLong &from)
{
    to.lat_d = -from.lat_d;
    to.lng_d = from.lng_d+180;
    normalizeLL(to);
}

/* return whether the given line segment spans a reasonable portion of the map.
 * beware map edge, wrap and crossing center of azm map
 */
bool segmentSpanOk (SCoord &s0, SCoord &s1)
{
    return (s0.x - s1.x < tft.width()/2 && s1.x - s0.x < tft.width()/2
                && (!azm_on || ((s0.x < map_b.x+map_b.w/2) == (s1.x < map_b.x+map_b.w/2)))
                && overMap(s0) && overMap(s1));
}
