/* draw most plotting areas.
 */

#include "HamClock.h"

#define BORDER_COLOR    GRAY
#define TICKLEN         2                       // length of plot tickmarks, pixels
#define TGAP            10                      // top gap for title
#define BGAP            15                      // bottom gap for x labels
#define FONTW           6                       // font width with gap
#define FONTH           8                       // font height


// forward declarations
static int tickmarks (float min, float max, int numdiv, float ticks[]);


/* plot the given data within the given box.
 * if y_min == y_max: auto scale min and max from data
 * if y_min < y_max:  force min to y_min and max to y_max
 * if y_min > y_max:  force min to y_min but auto scale max from data
 * return whether had anything to plot.
 * N.B. if both labels are NULL, use same labels and limits as previous call as an "overlay"
 * N.B. special y axis labeling hack when ylabel contains the string "Ray"
 * N.B. special plot format hack when ylabel contains "Kp"
 */
bool plotXY (const SBox &box, float x[], float y[], int nxy, const char *xlabel, const char *ylabel,
uint16_t color, float y_min, float y_max, float label_value)
{
        char buf[32];
        sprintf (buf, "%.*f", label_value >= 1000 ? 0 : 1, label_value);
        return (plotXYstr (box, x, y, nxy, xlabel, ylabel, color, y_min, y_max, buf));
}

/* same as plotXY but label is a string
 */
bool plotXYstr (const SBox &box, float x[], float y[], int nxy, const char *xlabel, const char *ylabel,
uint16_t color, float y_min, float y_max, char *label_str)
{
    resetWatchdog();

    // no labels implies overlay previous plot
    bool overlay = xlabel == NULL && ylabel == NULL;

    // check for special kp plot. N.B. ylabel is NULL if this is an overlay plot
    bool kp_plot = ylabel && strstr (ylabel, "Kp") != NULL;

    // check for xray plot
    bool xray_plot = ylabel && strstr (ylabel, "Ray") != NULL;

    // persistent scale info in case of subsequent overlay
    #define MAXTICKS     10
    static float xticks[MAXTICKS+2], yticks[MAXTICKS+2];
    static uint8_t nxt, nyt;
    static float minx, maxx;
    static float miny, maxy;
    static float dx, dy;
    static uint16_t LGAP;

    char buf[32];

    // set initial font and color
    selectFontStyle (BOLD_FONT, FAST_FONT);
    tft.setTextColor(color);

    // report if no data
    if (nxy < 1 || !x || !y) {
        plotMessage (box, color, "No data");
        return (false);
    }

    // find new limits unless this is an overlay
    if (!overlay) {

        // find data extrema
        minx = x[0]; maxx = x[0];
        miny = y[0]; maxy = y[0];
        for (int i = 1; i < nxy; i++) {
            if (x[i] > maxx) maxx = x[i];
            if (x[i] < minx) minx = x[i];
            if (y[i] > maxy) maxy = y[i];
            if (y[i] < miny) miny = y[i];
        }
        minx = floor(minx);
        maxx = ceil(maxx);
        if (maxx < minx + 1)
            maxx = minx + 1;

        if (y_min < y_max) {
            // force miny and maxy to the given y range
            miny = y_min;
            maxy = y_max;
        } else {
            if (y_min == y_max) {
                // auto scale both miny and maxy
                miny = floor(miny);
            } else {
                // force miny, still autoscale maxy
                miny = y_min;
            }
            // autoscale maxy
            maxy = ceil(maxy);
            if (maxy < miny + 1)
                maxy = miny + 1;
        }

        // find tickmarks
        nxt = tickmarks (minx, maxx, MAXTICKS, xticks);
        nyt = tickmarks (miny, maxy, MAXTICKS, yticks);

        // find minimal LGAP that accommodates widest y label
        LGAP = 0;
        for (int i = 0; i < nyt; i++) {
            sprintf (buf, "%.0f", yticks[i]);   // N.B. use same format as label 
            uint16_t g = getTextWidth(buf) + TICKLEN + 5;
            if (g > LGAP)
                LGAP = g;
        }

        // handy extrema
        minx = xticks[0];
        maxx = xticks[nxt-1];
        miny = yticks[0];
        maxy = yticks[nyt-1];
        dx = maxx-minx;
        dy = maxy-miny;

        // erase -- don't use prepPlotBox because we prefer no border on these plots
        tft.fillRect (box.x, box.y, box.w, box.h, RA8875_BLACK);

        // y labels and tickmarks just to the left of the plot
        if (xray_plot) {
            // mark exponents and customary X ray levels
            uint16_t tx = box.x+2*FONTW+TICKLEN+5;
            uint16_t steph = (box.h-BGAP-TGAP)/nyt;
            for (int i = 0; i < nyt; i++) {
                uint16_t ty = (uint16_t)(box.y + TGAP + (box.h-BGAP-TGAP)*(1 - (yticks[i]-miny)/dy) + 0.5F);
                tft.drawLine (tx-TICKLEN, ty, tx, ty, color);
                tft.setCursor (tx-FONTW-1, ty-steph+(steph-FONTH)/2-1);
                switch ((int)yticks[i]) {
                case -9: tft.setCursor (tx-TICKLEN-2*FONTW-1, ty-FONTH/2); tft.print(-9); break;
                case -8: tft.print ('A'); break;
                case -7: tft.print ('B'); break;
                case -6: tft.print ('C'); break;
                case -5: tft.print ('M'); break;
                case -4: tft.print ('X'); break;
                case -2: tft.setCursor (tx-TICKLEN-2*FONTW-1, ty-FONTH/2); tft.print(-2); break;
                }
            }
        } else {
            uint16_t tx = box.x+LGAP-TICKLEN;
            bool prev_tick = false;
            for (int i = 0; i < nyt; i++) {
                uint16_t ty = (uint16_t)(box.y + TGAP + (box.h-BGAP-TGAP)*(1 - (yticks[i]-miny)/dy) + 0.5F);
                tft.drawLine (tx, ty, tx+TICKLEN, ty, color);
                // label first, last or whole number change but never two adjacent or just before last
                if (i == 0 || i == nyt-1 || (!prev_tick && (int)yticks[i-1] != (int)yticks[i] && i != nyt-2)){
                    sprintf (buf, "%.0f", yticks[i]);
                    tft.setCursor (tx - getTextWidth(buf) - 1, ty - FONTH/2);
                    tft.print (buf);
                    prev_tick = true;
                } else
                    prev_tick = false;
            }
        }

        // y label is title over plot
        uint16_t tl = getTextWidth(ylabel);
        tft.setCursor (box.x+LGAP+(box.w-LGAP-tl)/2, box.y+(TGAP-FONTH)/2);
        tft.print (ylabel);

        // x labels and tickmarks just below plot
        uint16_t txty = box.y+box.h-FONTH-2;
        tft.setCursor (box.x+LGAP, txty);
        tft.print (minx,0);
        sprintf (buf, "%c%d", maxx > 0 ? '+' : ' ', (int)maxx);
        tft.setCursor (box.x+box.w-getTextWidth(buf)-1, txty);
        tft.print (buf);
        for (int i = 0; i < nxt; i++) {
            uint16_t tx = (uint16_t)(box.x+LGAP + (box.w-LGAP-1)*(xticks[i]-minx)/dx + 0.5F);
            tft.drawLine (tx, box.y+box.h-BGAP, tx, box.y+box.h-BGAP+TICKLEN, color);
        }

        // always label 0 if within larger range
        if (minx < 0 && maxx > 0) {
            uint16_t zx = (uint16_t)(box.x+LGAP + (box.w-LGAP)*(0-minx)/dx + 0.5F);
            tft.setCursor (zx-FONTW/2, txty);
            tft.print (0);
        }

        // x label is centered about the plot across the bottom
        tft.setCursor (box.x + LGAP + (box.w-LGAP-getTextWidth (xlabel))/2, box.y+box.h-FONTH-2);
        tft.print (xlabel);

    }

    uint16_t last_px = 0, last_py = 0;
    resetWatchdog();
    for (int i = 0; i < nxy; i++) {
        // Serial.printf ("kp %2d: %g %g\n", i, x[i], y[i]);
        if (kp_plot) {
            // plot Kp values vertical bars colored depending on strength
            uint16_t w = (box.w-LGAP-2)/nxy;
            uint16_t h = y[i]*(box.h-BGAP-TGAP)/maxy;
            uint16_t px = (uint16_t)(box.x+LGAP+1 + (box.w-LGAP-2-w)*(x[i]-minx)/dx);
            uint16_t py = (uint16_t)(box.y + TGAP + 1 + (box.h-BGAP-TGAP)*(1 - (y[i]-miny)/dy));
            uint16_t co = y[i] < 4 ? RA8875_GREEN : y[i] == 4 ? RA8875_YELLOW : RA8875_RED;
            if (h > 0)
                tft.fillRect (px, py, w, h, co);
        } else {
            // other plots are connect-the-dots
            uint16_t px = (uint16_t)(box.x+LGAP+1 + (box.w-LGAP-2)*(x[i]-minx)/dx);   // stay inside border
            uint16_t py = (uint16_t)(box.y + TGAP + (box.h-BGAP-TGAP)*(1 - (y[i]-miny)/dy));
            if (i > 0 && (last_px != px || last_py != py))
                tft.drawLine (last_px, last_py, px, py, color);            // avoid bug with 0-length lines
            else if (nxy == 1)
                tft.drawLine (box.x+LGAP, py, box.x+box.w-1, py, color);   // one value clear across
            last_px = px;
            last_py = py;
        }
    }

    // draw plot border
    tft.drawRect (box.x+LGAP, box.y+TGAP, box.w-LGAP, box.h-BGAP-TGAP+1, BORDER_COLOR);

    if (!overlay) {

        // overlay large center value on top in gray
        tft.setTextColor(BRGRAY);
        selectFontStyle (BOLD_FONT, LARGE_FONT);
        uint16_t lw, lh;
        getTextBounds (label_str, &lw, &lh);
        uint16_t text_x = box.x+LGAP+(box.w-LGAP-lw)/2;
        uint16_t text_y = box.y+TGAP+(box.h-TGAP-BGAP)/25+lh;
        tft.setCursor (text_x, text_y);
        tft.print (label_str);
    }

    // printFreeHeap (F("plotXYstr"));

    // ok
    return (true);
}

/* shorten str IN PLACE as needed to be less that maxw pixels wide.
 * return final width in pixels.
 */
uint16_t maxStringW (char *str, uint16_t maxw)
{
    uint8_t strl = strlen (str);
    uint16_t bw = 0;

    while (strl > 0 && (bw = getTextWidth(str)) >= maxw)
        str[--strl] = '\0';

    return (bw);
}

/* print weather info in the given box
 */
void plotWX (const SBox &box, uint16_t color, const WXInfo &wi)
{
    resetWatchdog();

    // prep
    prepPlotBox (box);

    const uint8_t indent = FONTW+1;     // allow for attribution down right side
    uint16_t dy = box.h/3;
    uint16_t ddy = box.h/5;
    float f;
    char buf[32];
    uint16_t w;

    // large temperature with degree symbol and units
    tft.setTextColor(color);
    selectFontStyle (BOLD_FONT, LARGE_FONT);
    f = useMetricUnits() ? wi.temperature_c : 9*wi.temperature_c/5+32;
    sprintf (buf, "%.0f %c", f, useMetricUnits() ? 'C' : 'F');
    w = maxStringW (buf, box.w-indent);
    tft.setCursor (box.x+(box.w-indent-w)/2, box.y+dy);
    tft.print(buf);
    uint16_t bw, bh;
    getTextBounds (buf+strlen(buf)-2, &bw, &bh);
    selectFontStyle (BOLD_FONT, SMALL_FONT);
    tft.setCursor (tft.getCursorX()-bw, tft.getCursorY()-2*bh/3);
    tft.print('o');
    dy += ddy;


    // remaining info smaller
    selectFontStyle (LIGHT_FONT, SMALL_FONT);

    // humidity
    sprintf (buf, "%.0f%% RH", wi.humidity_percent);
    w = maxStringW (buf, box.w-indent);
    tft.setCursor (box.x+(box.w-indent-w)/2, box.y+dy);
    tft.print (buf);
    dy += ddy;

    // wind
    f = (useMetricUnits() ? 3.6 : 2.237) * wi.wind_speed_mps; // kph or mph
    sprintf (buf, "%s @ %.0f %s", wi.wind_dir_name, f, useMetricUnits() ? "kph" : "mph");
    w = maxStringW (buf, box.w-indent);
    if (buf[strlen(buf)-1] != 'h') {
        // try shorter string in case of huge speed
        sprintf (buf, "%s @ %.0f%s", wi.wind_dir_name, f, useMetricUnits() ? "k/h" : "m/h");
        w = maxStringW (buf, box.w-indent);
    }
    tft.setCursor (box.x+(box.w-indent-w)/2, box.y+dy);
    tft.print (buf);
    dy += ddy;

    // nominal conditions
    strcpy (buf, wi.conditions);
    w = maxStringW (buf, box.w-indent);
    tft.setCursor (box.x+(box.w-indent-w)/2, box.y+dy);
    tft.print(buf);

    // attribution very small down the right side
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    uint8_t ylen = strlen(wi.attribution);
    uint16_t ly0 = box.y + (box.h - ylen*FONTH)/2;
    for (uint8_t i = 0; i < ylen; i++) {
        tft.setCursor (box.x+box.w-indent, ly0+i*FONTH);
        tft.print (wi.attribution[i]);
    }

    // printFreeHeap (F("plotWX"));
}


/* this function draws the Band Conditions pane. It can be called in two quite different ways:
 *   1. when called by updateBandConditions(), we are given a table containing relative propagation values
 *      for each band and a summary line to be drawn across the bottom.
 *   2. we can also be called just to update annotation as indicated by bmp or cfg_str being NULL. In this
 *      case we only draw the band indicators showing prop_map according to busy with the others normal
 *      and we redraw the time line.
 * matrix is 24 rows of UTC 0 .. 23, 8 columns of bands 80-40-30-20-17-15-12-10.
 * we rotate matrix so rows go up from 80 and cols are DE local time 0 .. 23.
 * busy: <0 err, 0 idle, >0 active.
 * N.B. coordinate the layout geometry with checkBCTouch()
 */
void plotBandConditions (const SBox &box, int busy, const BandMatrix *bmp, char *cfg_str)
{
    resetWatchdog();

    // whether full or just updating labels
    bool draw_all = bmp != NULL && cfg_str != NULL;

    // prep box if all
    if (draw_all)
        prepPlotBox (box);

    // layout
    #define PFONT_H 6                                   // plot labels font height
    #define PLOT_ROWS BMTRX_COLS                        // plot rows
    #define PLOT_COLS BMTRX_ROWS                        // plot columns
    #define TOP_B 27                                    // top border -- match VOACAP
    #define PGAP 5                                      // gap between title and plot
    #define PBOT_B 20                                   // plot bottom border -- room for config and time
    #define PLEFT_B 18                                  // left border -- room for band
    #define PTOP_Y (box.y + TOP_B + PGAP)               // plot top y
    #define PBOT_Y (box.y+box.h-PBOT_B)                 // plot bottom y
    #define PLEFT_X (box.x + PLEFT_B)                   // plot left x
    #define PRIGHT_X (box.x+box.w-2)                    // plot right x
    #define PLOT_W (PRIGHT_X - PLEFT_X)                 // plot width
    #define PLOT_H (PBOT_Y - PTOP_Y)                    // plot height
    #define PCOL_W (PLOT_W/PLOT_COLS-1)                 // plot column width
    #define PROW_H (PLOT_H/PLOT_ROWS-1)                 // plot row height

    // to help organize the matrix rotation, p_ variables refer to plot indices, m_ matrix

    // label band names -- indicate current voacap map, if any
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor(GRAY);
    for (int p_row = 0; p_row < PLOT_ROWS; p_row++) {
        uint16_t y = PBOT_Y - PLOT_H*(p_row+1)/PLOT_ROWS;
        if (p_row == prop_map) {
            uint16_t rect_col = busy > 0 ? DYELLOW : (busy < 0 ? RA8875_RED : RA8875_WHITE);
            tft.fillRect (box.x+1, y+1, PLEFT_B-4, PFONT_H+4, rect_col);
        } else if (!draw_all) {
            tft.fillRect (box.x+1, y+1, PLEFT_B-4, PFONT_H+4, RA8875_BLACK);
        }
        tft.setCursor (box.x+2, y + 2);
        tft.print (propMap2Band((PropMapSetting)p_row));
    }

    // erase timeline if not drawing all
    if (!draw_all)
        tft.fillRect (box.x + 1, PBOT_Y, box.w-2, PFONT_H+1, RA8875_BLACK);

    // mark local time now on UTC scale
    int de_hrs = (nowWO()/3600 + 48) % 24;
    uint16_t now_x = PLEFT_X + PLOT_W*de_hrs/PLOT_COLS;
    tft.fillRect (now_x, PBOT_Y, PCOL_W, PFONT_H, RA8875_WHITE);

    // label DE time -- utc 0 always on left end
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    for (int utc = 0; utc < BMTRX_ROWS; utc += 4) {
        uint16_t x = PLEFT_X + PLOT_W*utc/PLOT_COLS;
        uint16_t y = PBOT_Y-1;
        int de_lt = (utc + de_tz.tz_secs/3600 + 48) % 24;
        if (de_lt >= 10) {
            // close packing centered
            tft.setCursor (x-3, y);
            tft.print (de_lt/10);
            tft.setCursor (x+1, y);
            tft.print (de_lt%10);
        } else {
            tft.setCursor (x-1, y);
            tft.print (de_lt);
        }
    }

    // that's it unless drawing all
    if (!draw_all)
        return;

    // center title across the top
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setTextColor(RA8875_WHITE);
    const char *title = "VOACAP DE-DX";
    uint16_t tw = getTextWidth (title);
    tft.setCursor (box.x+(box.w-tw)/2, box.y + TOP_B);
    tft.print ((char*)title);

    // center the config across the bottom
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor(BRGRAY);
    uint16_t cw = maxStringW (cfg_str, box.w);
    tft.setCursor (box.x+(box.w-cw)/2, box.y + box.h - 10);
    tft.print ((char*)cfg_str);

    // scan matrix in row-major order but plot in col-major order to affect rotation
    for (int m_row = 0; m_row < BMTRX_ROWS; m_row++) {
        int p_col = m_row;                              // plot column
        uint16_t x = PLEFT_X + PLOT_W*p_col/PLOT_COLS;
        for (int m_col = 0; m_col < BMTRX_COLS; m_col++) {
            // get reliability
            uint8_t rel = (*bmp)[m_row][m_col];

            // choose color similar to fetchVOACAPArea.pl
            // rel:    0     10         33         66          100
            // color: black   |   red    |  yellow  |   green
            uint8_t h, s = 210, v, r, g, b;
            v = rel < 10 ? 0 : 210;
            h = rel < 33 ? 0 : (rel < 66 ? 43 : 85);
            hsvtorgb (&r, &g, &b, h, s, v);
            uint16_t color = RGB565 (r, g, b);

            // draw color box
            int p_row = m_col;
            uint16_t y = PBOT_Y - PLOT_H*(p_row+1)/PLOT_ROWS;
            tft.fillRect (x, y, PCOL_W, PROW_H, color);
        }
    }

    printFreeHeap (F("plotBandConditions"));
}

#if defined(_OLD_TABLE_STYLE)

/* this function handles the actual drawing of the Band Conditions pane. It can be called in two quite
 * different ways:
 * 1. when called by updateBandConditions(), we are given a table containing relative propagation values
 *    for each band and a summary line to be drawn across the bottom.
 * 2. we can also be called just to update the visual appearance of one of the band indicators as indicated
 *    by the table and summary line are NULL. In this case we only draw the band indicators showing
 *    prop_map according to busy and the others normal.
 * N.B. coordinate the layout geometry with checkBCTouch()
 */
void plotBandConditions (const SBox &box, int busy, float rel_tbl[PROP_MAP_N], char *cfg_str)
{
    // handy conversion of rel to text color
    #define RELCOL(r)       ((r) < 0.33 ? RA8875_RED : ((r) < 0.66 ? RA8875_YELLOW : RA8875_GREEN))

    // prep layout
    uint16_t ty = box.y + 27;           // BOTTOM of title; match DX Cluster title
    uint16_t cy = box.y+box.h-10;       // TOP of config string; beware comma descender
    uint16_t br_gap = box.w/5;
    uint16_t col1_x = box.x + 10;
    uint16_t col2_x = box.x + 5*box.w/9;
    uint16_t row_h = (cy-2-ty)/(PROP_MAP_N/2);

    // start over of we have a new table
    if (rel_tbl && cfg_str) {

        // prep
        prepPlotBox (box);

        // center title across the top
        selectFontStyle (LIGHT_FONT, SMALL_FONT);
        tft.setTextColor(RA8875_WHITE);
        const char *title = "VOACAP DE-DX";
        uint16_t bw = getTextWidth (title);
        tft.setCursor (box.x+(box.w-bw)/2, ty);
        tft.print ((char*)title);

        // center the config across the bottom
        selectFontStyle (LIGHT_FONT, FAST_FONT);
        tft.setTextColor(GRAY);
        bw = maxStringW (cfg_str, box.w);
        tft.setCursor (box.x+(box.w-bw)/2, cy);
        tft.print ((char*)cfg_str);

        // draw each rel_tab entry, 4 rows between ty and cy
        selectFontStyle (LIGHT_FONT, SMALL_FONT);
        for (int i = 0; i < PROP_MAP_N; i++) {
            uint16_t row_x = (i < PROP_MAP_N/2) ? col1_x : col2_x;
            uint16_t row_y = ty + row_h + (i%(PROP_MAP_N/2))*row_h;              // this is bottom of string

            char buf[10];
            tft.setTextColor(RELCOL(rel_tbl[i]));
            tft.setCursor (row_x + br_gap, row_y);
            snprintf (buf, sizeof(buf), "%2.0f", 99*rel_tbl[i]); // 100 doesn't fit
            tft.print (buf);
            if (i == PROP_MAP_80M)
                tft.print("%");
        }

    } 

    // always draw each band number
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    for (int i = 0; i < PROP_MAP_N; i++) {
        uint16_t row_x = (i < PROP_MAP_N/2) ? col1_x : col2_x;
        uint16_t row_y = ty + row_h + (i%(PROP_MAP_N/2))*row_h;

        // background square then number
        if (i == prop_map) {
            // show highlighted as per busy
            uint16_t rect_col = busy > 0 ? RA8875_YELLOW : (busy < 0 ? RA8875_RED : GRAY);
            tft.fillRect (row_x-1, row_y-row_h+4, box.w/6, row_h-2, rect_col);
            tft.setTextColor(RA8875_BLACK);
        } else {
            // show plain
            tft.fillRect (row_x-1, row_y-row_h+4, box.w/6, row_h-2, RA8875_BLACK);
            tft.setTextColor(BRGRAY);
        }
        tft.setCursor (row_x, row_y);
        tft.print (propMap2Band((PropMapSetting)i));
    }

    printFreeHeap (F("plotBandConditions"));
}

#endif // _OLD_TABLE_STYLE

/* print the NOAA RSG Space Weather Scales in the given box.
 */
void plotNOAASWx (const SBox &box, const NOAASpaceWx &noaaspw)
{
    resetWatchdog();

    // prep
    prepPlotBox (box);

    // title
    tft.setTextColor(RA8875_YELLOW);
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    uint16_t h = box.h/5-2;                             // text row height
    char *title = (char *) "NOAA SpaceWx";
    uint16_t bw = getTextWidth (title);
    tft.setCursor (box.x+(box.w-bw)/2, box.y+h);
    tft.print (title);

    // print each line
    for (int i = 0; i < N_NOAASW_C; i++) {

        uint16_t w = box.w/7-1;
        h += box.h/4;
        tft.setCursor (box.x+w+(i==2?-2:0), box.y+h);   // tweak G to better center
        tft.setTextColor(GRAY);
        tft.print (noaaspw.cat[i]);

        w += box.w/10;
        for (int j = 0; j < N_NOAASW_V; j++) {
            int val = noaaspw.val[i][j];
            w += box.w/7;
            tft.setCursor (box.x+w, box.y+h);
            tft.setTextColor(val == 0 ? RA8875_GREEN : (val <= 3 ? RA8875_YELLOW : RA8875_RED));
            tft.print (val);
        }
    }
}


/* print a message in a (plot?) box, take care not to go outside
 */
void plotMessage (const SBox &box, uint16_t color, const char *message)
{
    // log
    Serial.printf (_FX("PlotMsg: %s\n"), message);

    // prep font
    selectFontStyle (BOLD_FONT, FAST_FONT);
    tft.setTextColor(color);

    // prep box
    prepPlotBox (box);

    // make a copy so we can use destructive maxStringW
    char *msg_cpy = strdup (message);
    size_t msg_len = strlen (message);
    uint16_t msg_printed = 0;
    uint16_t y = box.y + box.h/4;

    // show up to at least a few lines
    resetWatchdog();
    for (int n_lines = 0; n_lines < 5 && msg_printed < msg_len; n_lines++) {

        // draw one line
        uint16_t msgw = maxStringW (msg_cpy, box.w-2);
        tft.setCursor (box.x+(box.w-msgw)/2, y);                // horizontally centered
        tft.print(msg_cpy);

        // advance
        msg_printed += strlen (msg_cpy);
        strcpy (msg_cpy, message + msg_printed);
        y += 2*FONTH;
    }

    // done
    free (msg_cpy);
}

/* prep a box for plotting
 */
void prepPlotBox (const SBox &box)
{
    // erase all
    tft.fillRect (box.x, box.y, box.w, box.h, RA8875_BLACK);

    // not bottom so it appears to connect with map top
    uint16_t rx = box.x+box.w-1;
    uint16_t by = box.y+box.h-1;
    tft.drawLine (box.x, box.y, box.x, by, BORDER_COLOR);               // left
    tft.drawLine (box.x, box.y, rx, box.y, BORDER_COLOR);               // top
    tft.drawLine (rx, box.y, rx, by, BORDER_COLOR);                     // right
}

/* check for touch in the given pane, return whether ours.
 * N.B. accommodate a few choices that have their own touch features.
 */
bool checkPlotTouch (const SCoord &s, PlotPane pp, TouchType tt)
{
    // out fast if not ours
    SBox &box = plot_b[pp];
    if (!inBox (s, box))
        return (false);

    // reserver top 20% for bringing up choice menu
    bool in_top = s.y < box.y + box.h/5;

    // check a few choices that have their own active areas
    switch (plot_ch[pp]) {
    case PLOT_CH_DXCLUSTER:
        if (checkDXClusterTouch (s, box))
            return (true);
        break;
    case PLOT_CH_BC:
        if (checkBCTouch (s, box))
            return (true);
        break;
    case PLOT_CH_GIMBAL:
        if (checkGimbalTouch (s, box))
            return (true);
        break;
    case PLOT_CH_COUNTDOWN:
        if (!in_top) {
            checkStopwatchTouch(tt);
            return (true);
        }
        break;

    // tapping a BME below top rotates just among other BME and disables auto rotate.
    // try all possibilities because they might be on other panes.
    case PLOT_CH_TEMPERATURE:
        if (!in_top) {
            if (setPlotChoice (pp, PLOT_CH_PRESSURE)
                            || setPlotChoice (pp, PLOT_CH_HUMIDITY)
                            || setPlotChoice (pp, PLOT_CH_DEWPOINT)) {
                plot_rotset[pp] = (1 << plot_ch[pp]);   // no auto rotation
                savePlotOps();
                return (true);
            }
        }
        break;
    case PLOT_CH_PRESSURE:
        if (!in_top) {
            if (setPlotChoice (pp, PLOT_CH_HUMIDITY)
                            || setPlotChoice (pp, PLOT_CH_DEWPOINT)
                            || setPlotChoice (pp, PLOT_CH_TEMPERATURE)) {
                plot_rotset[pp] = (1 << plot_ch[pp]);   // no auto rotation
                savePlotOps();
                return (true);
            }
        }
        break;
    case PLOT_CH_HUMIDITY:
        if (!in_top) {
            if (setPlotChoice (pp, PLOT_CH_DEWPOINT)
                            || setPlotChoice (pp, PLOT_CH_TEMPERATURE)
                            || setPlotChoice (pp, PLOT_CH_PRESSURE)) {
                plot_rotset[pp] = (1 << plot_ch[pp]);   // no auto rotation
                savePlotOps();
                return (true);
            }
        }
        break;
    case PLOT_CH_DEWPOINT:
        if (!in_top) {
            if (setPlotChoice (pp, PLOT_CH_TEMPERATURE)
                            || setPlotChoice (pp, PLOT_CH_PRESSURE)
                            || setPlotChoice (pp, PLOT_CH_HUMIDITY)) {
                plot_rotset[pp] = (1 << plot_ch[pp]);   // no auto rotation
                savePlotOps();
                return (true);
            }
        }
        break;

    default:
        break;
    }

    if (!in_top)
        return (false);

    // draw menu with choices for this pane
    PlotChoice ch = askPaneChoice(pp);

    // always engage even if same to erase menu
    if (!setPlotChoice (pp, ch)) {
        fatalError (_FX("Bug! checkPlotTouch bad choice %d pane %d"), (int)ch, (int)pp+1);
        // never returns
    }

    // it was ours
    return (true);
}

/* given min and max and an approximate number of divisions desired,
 * fill in ticks[] with nicely spaced values and return how many.
 * N.B. return value, and hence number of entries to ticks[], might be as
 *   much as 2 more than numdiv.
 */
static int tickmarks (float min, float max, int numdiv, float ticks[])
{
    static int factor[] = { 1, 2, 5 };
    #define NFACTOR    NARRAY(factor)
    float minscale;
    float delta;
    float lo;
    float v;
    int n;

    minscale = fabsf (max - min);

    if (minscale == 0) {
        /* null range: return ticks in range min-1 .. min+1 */
        for (n = 0; n < numdiv; n++)
            ticks[n] = min - 1.0 + n*2.0/numdiv;
        return (numdiv);
    }

    delta = minscale/numdiv;
    for (n=0; n < (int)NFACTOR; n++) {
        float scale;
        float x = delta/factor[n];
        if ((scale = (powf(10.0F, ceilf(log10f(x)))*factor[n])) < minscale)
            minscale = scale;
    }
    delta = minscale;

    lo = floor(min/delta);
    for (n = 0; (v = delta*(lo+n)) < max+delta; )
        ticks[n++] = v;

    return (n);
}
