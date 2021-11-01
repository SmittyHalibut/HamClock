/* plot management
 * each PlotPane is in one of PlotChoice state at any given time, all must be different.
 * each pane rotates through the set of bits in its rotset.
 */

#include "HamClock.h"


SBox plot_b[PANE_N] = {
    {235, 0, PLOTBOX_W, PLOTBOX_H},
    {405, 0, PLOTBOX_W, PLOTBOX_H},
    {575, 0, PLOTBOX_W, PLOTBOX_H},
};
PlotChoice plot_ch[PANE_N];
time_t next_rotationT[PANE_N];
uint32_t plot_rotset[PANE_N];
const char *plot_names[PLOT_CH_N] = {

    // N.B. must be in same order as PLOT_CH_* 
    // N.B. take care that names will fit in menu built by askPaneChoice()
    // N.B. names should not include blanks, but _ are changed to blanks for prettier printing

    "VOACAP",           // PLOT_CH_BC,
    "DE_Wx",            // PLOT_CH_DEWX,
    "DX_Cluster",       // PLOT_CH_DXCLUSTER,
    "DX_Wx",            // PLOT_CH_DXWX,
    "Solar_Flux",       // PLOT_CH_FLUX,
    "Planetary_K",      // PLOT_CH_KP,
    "Moon",             // PLOT_CH_MOON,
    "Space_Wx",         // PLOT_CH_NOAASWX,
    "Sunspot_N",        // PLOT_CH_SSN,
    "X-Ray",            // PLOT_CH_XRAY,
    "Gimbal",           // PLOT_CH_GIMBAL,
    "ENV_Temp",         // PLOT_CH_TEMPERATURE,
    "ENV_Press",        // PLOT_CH_PRESSURE,
    "ENV_Humid",        // PLOT_CH_HUMIDITY,
    "ENV_DewPt",        // PLOT_CH_DEWPOINT,
    "SDO_Comp",         // PLOT_CH_SDO_1,
    "SDO_6173A",        // PLOT_CH_SDO_2,
    "SDO_Magneto",      // PLOT_CH_SDO_3,
    "SDO_193A",         // PLOT_CH_SDO_4,
    "Solar_Wind",       // PLOT_CH_SOLWIND,
    "DRAP",             // PLOT_CH_DRAP,
    "Countdown",        // PLOT_CH_COUNTDOWN,
    "STEREO_A",         // PLOT_CH_STEREO_A,
};

/* return number of bits set in the given uint32_t
 * Brian Kernighan's bit counting algorithm
 */
static int nBitsSet (uint32_t n)
{
    int count = 0;
    while (n) {
        n = n & (n -1); // clear least significant set bit
        count++;
    }
    return (count);
}


/* retrieve the plot choice for the given pane from NV, if set
 */
static bool getPlotChoiceNV (PlotPane new_pp, PlotChoice *new_ch)
{
    bool ok = false;
    uint8_t ch;

    switch (new_pp) {
    case PANE_1:
        ok = NVReadUInt8 (NV_PLOT_1, &ch);
        break;
    case PANE_2:
        ok = NVReadUInt8 (NV_PLOT_2, &ch);
        break;
    case PANE_3:
        ok = NVReadUInt8 (NV_PLOT_3, &ch);
        break;
    default:
        fatalError (_FX("Bug! getPlotChoiceNV() bad plot pane %d"), (int)new_pp);
        return (false);
    }

    // beware just bonkers
    if (ch >= PLOT_CH_N)
        return (false);

    if (ok)
        *new_ch = (PlotChoice)ch;
    return (ok);
}

/* set the current choice for the given pane to any one of rotset, or a default if none.
 */
static void setDefaultPaneChoice (PlotPane pp)
{
    // check rotset first
    if (plot_rotset[pp]) {
        for (int i = 0; i < PLOT_CH_N; i++) {
            if (plot_rotset[pp] & (1 << i)) {
                plot_ch[pp] = (PlotChoice) i;
                break;
            }
        }
    } else {
        const PlotChoice ch_defaults[PANE_N] = {PLOT_CH_SSN, PLOT_CH_XRAY, PLOT_CH_SDO_1};
        plot_ch[pp] = ch_defaults[pp];
        plot_rotset[pp] = (1 << plot_ch[pp]);
        Serial.printf (_FX("PANE: Setting pane %d to default %s\n"), (int)pp+1, plot_names[plot_ch[pp]]);
    }
}

/* qsort-style function to compare pointers to two MenuItems by their string names
 */
static int menuChoiceQS (const void *p1, const void *p2)
{
    return (strcmp (((MenuItem*)p1)->label, ((MenuItem*)p2)->label));
}

/* return whether the given choice is currently physically available on this platform.
 * N.B. does not consider whether in use by panes -- for that use findPaneForChoice()
 */
bool plotChoiceIsAvailable (PlotChoice ch)
{
    uint32_t sw_timer;

    switch (ch) {

    case PLOT_CH_DXCLUSTER:     return (useDXCluster());
    case PLOT_CH_GIMBAL:        return (haveGimbal());
    case PLOT_CH_TEMPERATURE:   return (getNBMEConnected() > 0);
    case PLOT_CH_PRESSURE:      return (getNBMEConnected() > 0);
    case PLOT_CH_HUMIDITY:      return (getNBMEConnected() > 0);
    case PLOT_CH_DEWPOINT:      return (getNBMEConnected() > 0);
    case PLOT_CH_COUNTDOWN:     return (getSWEngineState(sw_timer) == SWE_COUNTDOWN);

    case PLOT_CH_BC:            // fallthru
    case PLOT_CH_DEWX:          // fallthru
    case PLOT_CH_DXWX:          // fallthru
    case PLOT_CH_FLUX:          // fallthru
    case PLOT_CH_KP:            // fallthru
    case PLOT_CH_MOON:          // fallthru
    case PLOT_CH_NOAASWX:       // fallthru
    case PLOT_CH_SSN:           // fallthru
    case PLOT_CH_XRAY:          // fallthru
    case PLOT_CH_SDO_1:         // fallthru
    case PLOT_CH_SDO_2:         // fallthru
    case PLOT_CH_SDO_3:         // fallthru
    case PLOT_CH_SDO_4:         // fallthru
    case PLOT_CH_SOLWIND:       // fallthru
    case PLOT_CH_DRAP:          // fallthru
    case PLOT_CH_STEREO_A:      // fallthru
        return (true);
        break;

    default:
        fatalError (_FX("Bug! plotChoiceIsAvailable() bad choice %d"), (int)ch);
        return (false);
    }

    return (false);

}

/* log the rotation set for the given pain, tag PlotChoice if in the set.
 */
void logPaneRotSet (PlotPane pp, PlotChoice ch)
{
    Serial.printf (_FX("Pane %d choices:\n"), (int)pp+1);
    for (int i = 0; i < PLOT_CH_N; i++)
        if (plot_rotset[pp] & (1 << i))
            Serial.printf (_FX("    %c%s\n"), i == ch ? '*' : ' ', plot_names[i]);
}

/* show a table of suitable plot choices in and for the given pane and allow user to choose one or more.
 * always return a selection even if it's the current selection again, never PLOT_CH_NONE.
 */
PlotChoice askPaneChoice (PlotPane pp)
{
    resetWatchdog();

    // set this temporarily to show all choices, just for testing worst-case layout
    #define ASKP_SHOWALL 0

    // init menu descriptor
    Menu menu;
    menu.n_cols = 2;
    menu.n_rows = 0;
    menu.n_items = 0;
    menu.items = NULL;

    // collect all candidates suitable for this pane
    for (int i = 0; i < PLOT_CH_N; i++) {
        // use if not used elsewhere and available or already assigned to this pane
        PlotChoice ch = (PlotChoice) i;
        PlotPane pp_ch = findPaneForChoice (ch);
        if ( (pp_ch == PANE_NONE && plotChoiceIsAvailable(ch)) || pp_ch == pp || ASKP_SHOWALL) {
            // set up next menu item
            menu.items = (MenuItem *) realloc (menu.items, (menu.n_items+1)*sizeof(MenuItem));
            MenuItem &mi = menu.items[menu.n_items++];
            mi.type = MENU_AL1OFN;
            mi.set = (plot_rotset[pp] & (1 << ch)) ? true : false;
            mi.label = plot_names[ch];
            mi.indent = 4;
        }
    }
    menu.n_rows = menu.n_items; // no MENU_IGNORE

    // nice sort by label
    qsort (menu.items, menu.n_items, sizeof(MenuItem), menuChoiceQS);

    // run the menu in copy of plot box so its height is not changed
    SBox pb = plot_b[pp];
    SBox ok_b;
    bool menu_ok = runMenu (menu, plot_b[pp], pb, ok_b);

    // return current choice by default
    PlotChoice return_ch = plot_ch[pp];

    if (menu_ok) {

        // show feedback
        menuRedrawOk (ok_b, MENU_OK_BUSY);

        // set new rotset
        plot_rotset[pp] = 0;
        for (int i = 0; i < menu.n_items; i++) {
            if (menu.items[i].set) {
                // find which choice this refers to by matching labels
                for (int j = 0; j < PLOT_CH_N; j++) {
                    if (strcmp (plot_names[j], menu.items[i].label) == 0) {
                        plot_rotset[pp] |= (1 << j);
                        break;
                    }
                }
            }
        }
        savePlotOps();

        // return current choice if still in rotset, else just pick one
        if (!(plot_rotset[pp] & (1 << return_ch))) {
            for (int i = 0; i < PLOT_CH_N; i++) {
                if (plot_rotset[pp] & (1 << i)) {
                    return_ch = (PlotChoice)i;
                    break;
                }
            }
        }
    }

    // finished with menu. labels were static.
    free ((void*)menu.items);

    // report
    logPaneRotSet(pp, return_ch);

    // done
    return (return_ch);
}

/* return which pane _is currently showing_ the given choice, else PANE_NONE
 */
PlotPane findPaneChoiceNow (PlotChoice ch)
{
    // unroll the loop ourselves to be sure
    // for (int i = 0; i < PANE_N; i++)
        // if (plot_ch[i] == ch)
            // return ((PlotPane)i);
    // return (PANE_NONE);

    if (PANE_N != 3)
        fatalError (_FX("Bug! PANE_N != 3"));

    if (plot_ch[PANE_1] == ch)
        return (PANE_1);
    if (plot_ch[PANE_2] == ch)
        return (PANE_2);
    if (plot_ch[PANE_3] == ch)
        return (PANE_3);
    return (PANE_NONE);
}

/* return which pane _could show_ the given choice, else PANE_NONE
 */
PlotPane findPaneForChoice (PlotChoice ch)
{
    // unroll the loop ourselves to be sure
    // for (int i = PANE_1; i < PANE_N; i++)
        // if ( (plot_rotset[i] & (1<<ch)) )
            // return ((PlotPane)i);
    // return (PANE_NONE);

    if (PANE_N != 3)
        fatalError (_FX("Bug! PANE_N != 3"));

    uint32_t mask = 1 << ch;
    if (plot_rotset[PANE_1] & mask)
        return (PANE_1);
    if (plot_rotset[PANE_2] & mask)
        return (PANE_2);
    if (plot_rotset[PANE_3] & mask)
        return (PANE_3);
    return (PANE_NONE);
}

/* given a current choice, select the next rotation plot choice for the given pane.
 * if not rotating return the same choice.
 */
PlotChoice getNextRotationChoice (PlotPane pp, PlotChoice pc)
{
    // search starting after given selection
    for (int i = 1; i <= PLOT_CH_N; i++) {
        int pc_test = ((int)pc + i) % PLOT_CH_N;

        // done if in selection set
        if (plot_rotset[pp] & (1 << pc_test))
            return ((PlotChoice)pc_test);
    }

    fatalError (_FX("Bug! getNextRotationChoice() none for pane %d"), (int)pp+1);
    return (plot_ch[pp]);
}

/* return any available unassigned plot choice
 */
PlotChoice getAnyAvailableChoice()
{
    int s = random (PLOT_CH_N);
    for (int i = 0; i < PLOT_CH_N; i++) {
        PlotChoice ch = (PlotChoice)((s + i) % PLOT_CH_N);
        if (plotChoiceIsAvailable (ch)) {
            bool inuse = false;
            for (int j = 0; !inuse && j < PANE_N; j++) {
                if (plot_ch[j] == ch || (plot_rotset[j] & (1 << ch))) {
                    inuse = true;
                }
            }
            if (!inuse)
                return (ch);
        }
    }
    fatalError (_FX("Bug! no available pane choices"));

    // never get here, just for lint
    return (PLOT_CH_FLUX);
}

/* return whether pane pp is currently rotating among more than one choice
 */
bool paneIsRotating (PlotPane pp)
{
    return (nBitsSet(plot_rotset[pp]) > 1);
}

/* remove any PLOT_CH_COUNTDOWN from rotset if stopwatch engine not SWE_COUNTDOWN,
 * and if it is currently visible replace with an alternative.
 */
void insureCountdownPaneSensible()
{
    uint32_t sw_timer;
    if (getSWEngineState(sw_timer) != SWE_COUNTDOWN) {
        for (int i = 0; i < PANE_N; i++) {
            if (plot_rotset[i] & (1 << PLOT_CH_COUNTDOWN)) {
                plot_rotset[i] &= ~(1 << PLOT_CH_COUNTDOWN);
                if (plot_ch[i] == PLOT_CH_COUNTDOWN) {
                    setDefaultPaneChoice((PlotPane)i);
                    if (!setPlotChoice ((PlotPane)i, plot_ch[i])) {
                        fatalError (_FX("Bug! can not replace Countdown pain %d with %s"),
                                    i+1, plot_names[plot_ch[i]]);
                    }
                }
            }
        }
    }
}

/* called once to init plot info from NV and insure legal and consistent values
 */
void initPlotPanes()
{
    // retrieve rotation sets -- ok to leave 0 for now if not yet defined
    NVReadUInt32 (NV_PANE1ROTSET, &plot_rotset[PANE_1]);
    NVReadUInt32 (NV_PANE2ROTSET, &plot_rotset[PANE_2]);
    NVReadUInt32 (NV_PANE3ROTSET, &plot_rotset[PANE_3]);

    // rm any rotset not available
    for (int i = 0; i < PANE_N; i++) {
        for (int j = 0; j < PLOT_CH_N; j++) {
            if ((plot_rotset[i] & (1 << j)) && !plotChoiceIsAvailable ((PlotChoice)j)) {
                plot_rotset[i] &= ~(1 << j);
                Serial.printf (_FX("PANE: Removing %s from pane %d: not available\n"), plot_names[j], i+1);
            }
        }
    }

    // if current selection not yet defined or not in rotset pick one from rotset or set a default
    for (int i = 0; i < PANE_N; i++) {
        if (!getPlotChoiceNV ((PlotPane)i, &plot_ch[i]) || !(plot_rotset[i] & (1 << plot_ch[i])))
            setDefaultPaneChoice ((PlotPane)i);
    }

    // insure same choice not in more than 1 pane
    for (int i = 0; i < PANE_N; i++) {
        for (int j = i+1; j < PANE_N; j++) {
            if (plot_ch[i] == plot_ch[j]) {
                // found dup -- replace with some other unused choice
                for (int k = 0; k < PLOT_CH_N; k++) {
                    PlotChoice new_ch = (PlotChoice)k;
                    if (plotChoiceIsAvailable(new_ch) && findPaneChoiceNow(new_ch) == PANE_NONE) {
                        Serial.printf (_FX("PANE: Reassigning dup pane %d from %s to %s\n"), j+1,
                                        plot_names[plot_ch[j]], plot_names[new_ch]);
                        // remove dup from rotation set then replace with new choice
                        plot_rotset[j] &= ~(1 << plot_ch[j]);
                        plot_rotset[j] |= (1 << new_ch);
                        plot_ch[j] = new_ch;
                        break;
                    }
                }
            }
        }
    }

    // one last bit of paranoia: insure each pane choice is in its rotation set
    for (int i = 0; i < PANE_N; i++)
        plot_rotset[i] |= (1 << plot_ch[i]);

    // log and save final arrangement
    for (int i = 0; i < PANE_N; i++)
        logPaneRotSet ((PlotPane)i, plot_ch[i]);
    savePlotOps();
}

/* update NV_PANE?CH from plot_rotset[] and NV_PLOT_? from plot_ch[]
 */
void savePlotOps()
{
    NVWriteUInt32 (NV_PANE1ROTSET, plot_rotset[PANE_1]);
    NVWriteUInt32 (NV_PANE2ROTSET, plot_rotset[PANE_2]);
    NVWriteUInt32 (NV_PANE3ROTSET, plot_rotset[PANE_3]);

    NVWriteUInt8 (NV_PLOT_1, plot_ch[PANE_1]);
    NVWriteUInt8 (NV_PLOT_2, plot_ch[PANE_2]);
    NVWriteUInt8 (NV_PLOT_3, plot_ch[PANE_3]);
}

/* draw a plot border nearly ready to change or not
 */
void showRotatingBorder (bool soon, PlotPane pp)
{
    uint16_t c = (next_rotationT[pp] - now() > PLOT_ROT_WARNING) || soon ? RA8875_WHITE : GRAY;
    tft.drawRect (plot_b[pp].x, plot_b[pp].y, plot_b[pp].w, plot_b[pp].h, c);

}

/* download the given url containing a bmp image and display in the given box.
 * show error messages in the given color.
 * return whether all ok
 */
bool drawHTTPBMP (const char *url, const SBox &box, uint16_t color)
{
    WiFiClient client;
    bool ok = false;

    Serial.println(url);
    resetWatchdog();
    if (wifiOk() && client.connect(svr_host, HTTPPORT)) {
        updateClocks(false);

        // composite types
        union { char c[4]; uint32_t x; } i32;
        union { char c[2]; uint16_t x; } i16;

        // query web page
        httpGET (client, svr_host, url);

        // skip response header
        if (!httpSkipHeader (client)) {
            plotMessage (box, color, _FX("image header short"));
            goto out;
        }

        // keep track of our offset in the image file
        uint32_t byte_os = 0;
        char c;

        // read first two bytes to confirm correct format
        if (!getChar(client,&c) || c != 'B' || !getChar(client,&c) || c != 'M') {
            plotMessage (box, color, _FX("bad file"));
            goto out;
        }
        byte_os += 2;

        // skip down to byte 10 which is the offset to the pixels offset
        while (byte_os++ < 10) {
            if (!getChar(client,&c)) {
                plotMessage (box, color, _FX("header offset error"));
                goto out;
            }
        }
        for (uint8_t i = 0; i < 4; i++, byte_os++) {
            if (!getChar(client,&i32.c[i])) {
                plotMessage (box, color, _FX("pix_start error"));
                goto out;
            }
        }
        uint32_t pix_start = i32.x;
        // Serial.printf (_FX("pixels start at %d\n"), pix_start);

        // next word is subheader size, must be 40 BITMAPINFOHEADER
        for (uint8_t i = 0; i < 4; i++, byte_os++) {
            if (!getChar(client,&i32.c[i])) {
                plotMessage (box, color, _FX("hdr size error"));
                goto out;
            }
        }
        uint32_t subhdr_size = i32.x;
        if (subhdr_size != 40) {
            Serial.printf (_FX("DIB must be 40: %d\n"), subhdr_size);
            plotMessage (box, color, _FX("DIB err"));
            goto out;
        }

        // next word is width
        for (uint8_t i = 0; i < 4; i++, byte_os++) {
            if (!getChar(client,&i32.c[i])) {
                plotMessage (box, color, _FX("width error"));
                goto out;
            }
        }
        int32_t img_w = i32.x;

        // next word is height
        for (uint8_t i = 0; i < 4; i++, byte_os++) {
            if (!getChar(client,&i32.c[i])) {
                plotMessage (box, color, _FX("height error"));
                goto out;
            }
        }
        int32_t img_h = i32.x;
        int32_t n_pix = img_w*img_h;
        Serial.printf (_FX("image is %d x %d = %d\n"), img_w, img_h, img_w*img_h);

        // next short is n color planes
        for (uint8_t i = 0; i < 2; i++, byte_os++) {
            if (!getChar(client,&i16.c[i])) {
                plotMessage (box, color, _FX("planes error"));
                goto out;
            }
        }
        uint16_t n_planes = i16.x;
        if (n_planes != 1) {
            Serial.printf (_FX("planes must be 1: %d\n"), n_planes);
            plotMessage (box, color, _FX("n planes error"));
            goto out;
        }

        // next short is bits per pixel
        for (uint8_t i = 0; i < 2; i++, byte_os++) {
            if (!getChar(client,&i16.c[i])) {
                plotMessage (box, color, _FX("bits/pix error"));
                goto out;
            }
        }
        uint16_t n_bpp = i16.x;
        if (n_bpp != 24) {
            Serial.printf (_FX("bpp must be 24: %d\n"), n_bpp);
            plotMessage (box, color, _FX("bpx error"));
            goto out;
        }

        // next word is compression method
        for (uint8_t i = 0; i < 4; i++, byte_os++) {
            if (!getChar(client,&i32.c[i])) {
                plotMessage (box, color, _FX("compression error"));
                goto out;
            }
        }
        uint32_t comp = i32.x;
        if (comp != 0) {
            Serial.printf (_FX("compression must be 0: %d\n"), comp);
            plotMessage (box, color, _FX("comp error"));
            goto out;
        }

        // skip down to start of pixels
        while (byte_os++ <= pix_start) {
            if (!getChar(client,&c)) {
                plotMessage (box, color, _FX("header 3 error"));
                goto out;
            }
        }

        // prep logical box
        prepPlotBox (box);

        // display box depends on actual output size.
        SBox v_b;
        v_b.x = box.x * tft.SCALESZ;
        v_b.y = box.y * tft.SCALESZ;
        v_b.w = box.w * tft.SCALESZ;
        v_b.h = box.h * tft.SCALESZ;

        // clip and center the image within v_b
        uint16_t xborder = img_w > v_b.w ? (img_w - v_b.w)/2 : 0;
        uint16_t yborder = img_h > v_b.h ? (img_h - v_b.h)/2 : 0;

        // scan all pixels ...
        for (uint16_t img_y = 0; img_y < img_h; img_y++) {

            // keep time active
            resetWatchdog();
            updateClocks(false);

            for (uint16_t img_x = 0; img_x < img_w; img_x++) {

                char b, g, r;

                // read next pixel -- note order!
                if (!getChar (client, &b) || !getChar (client, &g) || !getChar (client, &r)) {
                    // allow a little loss because ESP TCP stack can fall behind while also drawing
                    int32_t n_draw = img_y*img_w + img_x;
                    if (n_draw > 9*n_pix/10) {
                        // close enough
                        Serial.printf (_FX("read error after %d pixels but good enough\n"), n_draw);
                        ok = true;
                        goto out;
                    } else {
                        Serial.printf (_FX("read error after %d pixels\n"), n_draw);
                        plotMessage (box, color, _FX("file is short"));
                        goto out;
                    }
                }

                // ... but only draw if fits inside border
                if (img_x > xborder && img_x < xborder + v_b.w - tft.SCALESZ
                            && img_y > yborder && img_y < yborder + v_b.h - tft.SCALESZ) {

                    uint8_t ur = r;
                    uint8_t ug = g;
                    uint8_t ub = b;
                    uint16_t color16 = RGB565(ur,ug,ub);
                    tft.drawSubPixel (v_b.x + img_x - xborder,
                                v_b.y + v_b.h - (img_y - yborder) - 1, color16); // vertical flip
                }
            }

            // skip padding to bring total row length to multiple of 4
            uint8_t extra = img_w % 4;
            if (extra > 0) {
                for (uint8_t i = 0; i < 4 - extra; i++) {
                    if (!getChar(client,&c)) {
                        plotMessage (box, color, _FX("row padding error"));
                        goto out;
                    }
                }
            }
        }

        // Serial.println (F("image complete"));
        ok = true;

    } else {
        plotMessage (box, color, _FX("connection failed"));
    }

out:
    client.stop();
    return (ok);
}

/* wait until:
 *   a tap occurs inside the given box,
 *   a tap occurs outside the given box,
 *   the given function (if not NULL) returns true or
 *   the given timeout occurs.
 * if tap inbox return location and true, else false for all other cases.
 * while waiting we update clocks and allow some web server commands.
 */
bool waitForTap (const SBox &inbox, const SBox &outbox, bool (*fp)(void), uint32_t to_ms, SCoord &tap)
{
    drainTouch();

    uint32_t t0 = millis();
    for(;;) {

        SCoord s;
        if (readCalTouchWS(s) != TT_NONE) {
            drainTouch();
            if (inBox (s, inbox)) {
                tap = s;
                return(true);
            }
            if (!inBox (s, outbox))
                return (false);
            t0 = millis();
        }

        if (timesUp (&t0, to_ms))
            return (false);

        if (fp && (*fp)())
            return (false);

        updateClocks(false);
        wdDelay (100);
    }
}
