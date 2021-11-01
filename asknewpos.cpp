/* manage a dialog to let op set a new lat/long xor grid position for either DE or DX.
 *
 * layout:
 *
 *  0:   Lat:
 *  1:   Long:
 *  2:   Grid:
 *  3:   A B C D E F G H I
 *  4:   J K L M N O P Q R
 *  5:   S T U V W X 0 1 2
 *  6:   3 4 5 6 7 8 9 . -
 *  7:   Del   Ok   Cancel
 *
 */
 
#include "HamClock.h"

// basic arrangement
#define NPKB_ROWS       4               // actual keyboard rows
#define NPKB_COLS       9               // actual keyboard cols
#define NP_NBOXR        8               // total number of rows in box
#define NP_FONTW        6               // fixed-width font width
#define NP_FONTH        9               // fixed-width font height
#define NP_ULDROP       2               // underline drop distance
#define NP_ERRDELAY     2500            // err_msg dwell, ms
#define NP_TIMEOUT      MENU_TO         // cancel if idle this long, ms
#define NP_TXCOLOR      RA8875_WHITE    // text color
#define NP_ULCOLOR      RA8875_GREEN    // cursor color


// handy conversions between graphics coords and row number 0 .. NP_NBOXR-1
#define ROW2FY(b,r)     ((b).y + (r)*(b).h/NP_NBOXR + 2)        // font y coord (top of glyph)
#define ROW2CY(b,r)     (ROW2FY(b,r) + NP_FONTH + NP_ULDROP)    // cursor y
#define GY2ROW(b,gy)    (NP_NBOXR*((gy) - (b).y)/(b).h)         // y to 0 .. NP_NBOXR-1

// processNPTap() codes if not alphanumeric
enum {
    NPKB_NONE,
    NPKB_LAT,
    NPKB_LNG,
    NPKB_GRID,
    NPKB_DEL,
    NPKB_CANCEL,
    NPKB_OK
};

// info about each interactive field
#define NPF_MAXLEN 15           // max field length, not including EOS
typedef struct {
    uint16_t x;                 // graphics x coord at start of user str
    uint8_t r;                  // row 0 .. NP_NBOXR-1
    char str[NPF_MAXLEN+1];     // user contents, including EOS
} NPField;
static const char ll_fmt[] = "%.6g%c";  // lat/long + dir format suitable within NPF_MAXLEN

// field names
typedef enum {
    NPF_LAT,
    NPF_LNG,
    NPF_GRID,
    NPF_N
} NPFieldName;

/* given box row and col return keyboard char or NPKB_NONE
 */
static int keyboardMap (int row, int col)
{
    switch (row) {
    case 3: // fallthru
    case 4:
        return ('A' + (row-3)*NPKB_COLS + col);
    case 5:
        return (col <= 5 ? 'S' + col : '0' + col - 6);
    case 6:
        return (col == 8 ? '-' : (col == 7 ? '.' : '3' + col));
        break;
    }

    return (NPKB_NONE);
}

/* set NPF_LAT and NPF_LNG from ll
 */
static void setNPLL (NPField f[NPF_N], const LatLong &ll)
{
    snprintf (f[NPF_LAT].str, sizeof(f[NPF_LAT].str), ll_fmt,
                                            fabsf(ll.lat_d), ll.lat_d < 0 ? 'S' : 'N');
    snprintf (f[NPF_LNG].str, sizeof(f[NPF_LNG].str), ll_fmt,
                                            fabsf(ll.lng_d), ll.lng_d < 0 ? 'W' : 'E');
}


/* visually erase the entire given field but don't reset str
 */
static void veraseNPField (const SBox &b, const NPField &f)
{
    tft.fillRect (f.x, ROW2FY(b,f.r)-1, NPF_MAXLEN*NP_FONTW, NP_FONTH+2, RA8875_BLACK);
}

/* erase and delete the given field
 */
static void eraseNPField (const SBox &b, NPField &f)
{
    veraseNPField (b, f);
    f.str[0] = 0;
}

/* draw the given field fresh
 */
static void drawNPField (const SBox &b, const NPField &f)
{
    veraseNPField (b, f);
    tft.setTextColor(NP_TXCOLOR);
    tft.setCursor (f.x, ROW2FY(b,f.r));
    tft.print (f.str);
}

/* draw focus location for the given field
 */
static void drawNPFocus (const SBox &b, const NPField &f)
{
    uint16_t x = f.x + getTextWidth(f.str);
    uint16_t y = ROW2CY (b, f.r);
    tft.drawLine (x, y, x+NP_FONTW, y, NP_ULCOLOR);
}

/* erase focus location for given field
 */
static void eraseNPFocus (const SBox &b, const NPField &f)
{
    uint16_t x = f.x + getTextWidth(f.str);
    uint16_t y = ROW2CY (b, f.r);
    tft.drawLine (x, y, x+NP_FONTW, y, RA8875_BLACK);
}

/* remove and erase last character from the given field, if any
 */
static void eraseNPChar (const SBox &b, NPField &f)
{
    if (f.str[0]) {
        size_t l = strlen(f.str);
        eraseNPFocus (b, f);
        f.str[--l] = '\0';
        uint16_t x = f.x + getTextWidth(f.str);
        tft.fillRect (x, ROW2FY(b,f.r), NP_FONTW, NP_FONTH, RA8875_BLACK);
        drawNPFocus (b, f);
    }
}

/* try to set Grid from L/L
 */
static void updateNPGrid (const SBox &b, NPField f[NPF_N])
{
    LatLong ll;

    if (latSpecIsValid(f[NPF_LAT].str, ll.lat_d) && lngSpecIsValid(f[NPF_LNG].str, ll.lng_d)) {
        char maid[MAID_CHARLEN];
        ll2maidenhead (maid, ll);
        strcpy (f[NPF_GRID].str, maid);
        drawNPField (b, f[NPF_GRID]);
    } else {
        eraseNPField (b, f[NPF_GRID]);
    }
}

/* try to set L/L from Grid, else erase
 */
static void updateNPLL (const SBox &b, NPField f[NPF_N])
{
    LatLong ll;

    if (maidenhead2ll (ll, f[NPF_GRID].str)) {
        setNPLL (f, ll);
        drawNPField (b, f[NPF_LAT]);
        drawNPField (b, f[NPF_LNG]);
    } else {
        eraseNPField (b, f[NPF_LAT]);
        eraseNPField (b, f[NPF_LNG]);
    }
}

/* update fields opposite from the one now with focus.
 */
static void updateOpposite (const SBox &b, NPField f[NPF_N], NPFieldName focus_fn)
{
    switch (focus_fn) {
    case NPF_LAT:
    case NPF_LNG:      // fallthru
        // update grid when ll changes
        updateNPGrid (b, f);
        break;
    case NPF_GRID:
        // update ll when grid changes
        updateNPLL (b, f);
        break;
    case NPF_N:
        break;
    }
}

/* temporarily show the given message in the given field.
 * N.B. do not change the actual field string.
 */
static void showNPFieldError (const SBox &b, const NPField &f, const char *msg)
{
    veraseNPField (b, f);
    tft.setCursor (f.x, ROW2FY(b,f.r));
    tft.setTextColor(RA8875_RED);
    tft.print (msg);
}

/* add character to the given focus field if it fits and update view
 */
static void addNPChar (const SBox &b, NPField f[NPF_N], NPFieldName focus_fn, int new_char)
{
    NPField &focus_f = f[focus_fn];
    int maxlen = focus_fn == NPF_GRID ? MAID_CHARLEN-1 : NPF_MAXLEN;
    int l = strlen(focus_f.str);

    if (l < maxlen) {
        // erase current cursor
        eraseNPFocus (b, focus_f);

        // change to lower case if this is 3rd section of grid
        if (focus_fn == NPF_GRID && l >= 4)
            new_char = tolower (new_char);

        // append to string
        focus_f.str[l] = new_char;
        focus_f.str[++l] = '\0';

        // draw char
        tft.setCursor (focus_f.x, ROW2FY(b,focus_f.r));
        tft.setTextColor (NP_TXCOLOR);
        tft.print(focus_f.str);

        // draw cursor in new location
        drawNPFocus (b, focus_f);
    }
}

/* draw the persistent portion of the complete dialog box, and set f[] coords from ll.
 */
static void initNPDialog (const SBox &box, NPField f[NPF_N], const LatLong &ll, const char grid[MAID_CHARLEN])
{
    // prep
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (NP_TXCOLOR);

    // box
    tft.fillRect (box.x, box.y, box.w, box.h, RA8875_BLACK);
    tft.drawRect (box.x, box.y, box.w, box.h, NP_TXCOLOR);

    // set lat and long fields
    setNPLL (f, ll);

    // finish setting up lat
    tft.setCursor (box.x+1, ROW2FY(box,0));
    tft.print (F("Lat:"));
    f[NPF_LAT].x = box.x + 1 + 6*NP_FONTW;
    f[NPF_LAT].r = 0;
    drawNPField (box, f[NPF_LAT]);


    // finish setting up lng
    tft.setCursor (box.x+1, ROW2FY(box,1));
    tft.print (F("Long:"));
    f[NPF_LNG].x = box.x + 1 + 6*NP_FONTW;
    f[NPF_LNG].r = 1;
    drawNPField (box, f[NPF_LNG]);

    // grid
    tft.setCursor (box.x+1, ROW2FY(box,2));
    tft.print (F("Grid:"));
    strcpy (f[NPF_GRID].str, grid);
    f[NPF_GRID].x = box.x + 1 + 6*NP_FONTW;
    f[NPF_GRID].r = 2;
    drawNPField (box, f[NPF_GRID]);

    // keyboard
    for (int row = 3; row <= 6; row++) {
        uint16_t y = ROW2FY(box,row);
        for (int col = 0; col < NPKB_COLS; col++) {
            int kbc = keyboardMap(row,col);
            if (kbc != NPKB_NONE) {
                uint16_t x = box.x + (box.w/NPKB_COLS-NP_FONTW)/2 + col*box.w/NPKB_COLS;
                tft.setCursor (x, y);
                tft.print ((char)kbc);
            }
        }
    }

    // bottom controls
    uint16_t x, y = ROW2FY(box,7);
    x = box.x + NP_FONTW;
    tft.setCursor (x, y);
    tft.print (F("Delete"));
    tft.drawRect (x-2, y-2, NP_FONTW*6+4, NP_FONTH+3, NP_TXCOLOR);
    x = box.x + box.w/2 - NP_FONTW;
    tft.setCursor (x, y);
    tft.print (F("Ok"));
    tft.drawRect (x-2, y-2, NP_FONTW*2+4, NP_FONTH+3, NP_TXCOLOR);
    x = box.x+box.w-7*NP_FONTW;
    tft.setCursor (x, y);
    tft.drawRect (x-2, y-2, NP_FONTW*6+4, NP_FONTH+3, NP_TXCOLOR);
    tft.print (F("Cancel"));
}

/* check for a complete and consistent set of fields.
 * if any problems display err and return false, else set ll and grid and return true.
 */
static bool allNPFieldsOK (const SBox &b, const NPField f[NPF_N], LatLong &ll, char grid[MAID_CHARLEN])
{
    float lt, lg;
    LatLong grid_ll;
    bool ok = true;

    if (!maidenhead2ll (grid_ll, f[NPF_GRID].str)) {
        showNPFieldError (b, f[NPF_GRID], strlen(f[NPF_GRID].str) > 0 ? "Bad grid" : "Missing grid");
        ok = false;
    }
    if (!latSpecIsValid(f[NPF_LAT].str, lt)) {
        showNPFieldError (b, f[NPF_LAT], strlen(f[NPF_LAT].str) > 0 ? "Bad lat" : "Missing lat");
        ok = false;
    }
    if (!lngSpecIsValid(f[NPF_LNG].str, lg)) {
        showNPFieldError (b, f[NPF_LNG], strlen(f[NPF_LNG].str) > 0 ? "Bad long" : "Missing long");
        ok = false;
    }

    if (ok) {

        // all good, return results to caller
        ll.lat_d = lt;
        ll.lng_d = lg;
        strcpy (grid, f[NPF_GRID].str);

    } else {

        // some errors: dwell then restore
        wdDelay (NP_ERRDELAY);
        for (int i = 0; i < NPF_N; i++) {
            veraseNPField (b, f[i]);
            tft.setCursor (f[i].x, ROW2FY(b,f[i].r));
            tft.setTextColor(NP_TXCOLOR);
            tft.print (f[i].str);
        }
    }

    return (ok);
}

/* given a keyboard char and current focus or location known to be within box b return
 * ascii char or one of NPKB_*.
 * N.B. must use same layout as initNPDialog()
 */
static int processNPTap (char kbc, NPFieldName focus_fn, const SBox &b, const SCoord &s)
{
    // use kbc if given
    if (kbc != NPKB_NONE) {
        switch (kbc) {
        case '\t':      // tab advances to next field
            switch (focus_fn) {
            case NPF_LAT: return (NPKB_LNG);
            case NPF_LNG: return (NPKB_GRID);
            default:      return (NPKB_LAT);
            }
            break;
        case '\n':      // fallthru
        case '\r':
            return (NPKB_OK);
        case 27:        // esc
            return (NPKB_CANCEL);
        case '\b':      // fallthru
        case 127:       // del
            return (NPKB_DEL);
        default:
            return (toupper(kbc));
        }
    }

    // convert s to relative row and col within b
    int row = GY2ROW(b, s.y);
    int col = NPKB_COLS*(s.x - b.x)/b.w;

    // check for output fields
    if (row == 0) return (NPKB_LAT);
    if (row == 1) return (NPKB_LNG);
    if (row == 2) return (NPKB_GRID);
    
    // check keyboard rows
    int k = keyboardMap (row, col);
    if (k != NPKB_NONE)
        return (k);

    // last row?
    if (row == 7)
        return (s.x < b.x+b.w/3 ? NPKB_DEL : (s.x < b.x+2*b.w/3 ? NPKB_OK : NPKB_CANCEL));

    // hmm
    return (NPKB_NONE);
}


/* react to a new character entered when the given field is in focus.
 */
static void newNPChar (const SBox &b, NPField f[NPF_N], NPFieldName focus_fn, int new_char)
{
    // add char to focus field if room
    addNPChar (b, f, focus_fn, new_char);

    // update fields that are "opposite" to focus field
    updateOpposite (b, f, focus_fn);
}

/* run the lat/long/grid input box.
 * call with current values. if return true values are from user and known to be validated.
 * N.B. we do not call updateClocks; if want to, add flag to prevent it from writing the fields we obscure.
 */
bool askNewPos (const SBox &b, LatLong &op_ll, char op_grid[MAID_CHARLEN])
{
    // info for each interactive field
    NPField fields[NPF_N];
    memset (fields, 0, sizeof(fields));

    // draw initial dialog and set up fields[]
    initNPDialog(b, fields, op_ll, op_grid);

    // pick an initial focus field
    NPField *focus_fp = &fields[NPF_LAT];
    drawNPFocus (b, *focus_fp);

    // now user can operate until cancels
    bool ok = false;
    bool cancelled = false;
    LatLong new_ll;
    char new_grid[MAID_CHARLEN];
    memset (&new_ll, 0, sizeof(new_ll));
    memset (new_grid, 0, sizeof(new_grid));
    do {

        // always fresh
        ok = false;
        cancelled = false;

        // handy current field number
        NPFieldName focus_fn = (NPFieldName)(focus_fp - fields);

        // read input touch or keyboard, cancel if time out
        uint32_t t0 = millis();
        TouchType tt;
        SCoord s;
        char kbc = 0;
        while (!cancelled
                        && ((tt = readCalTouchWS(s)) == TT_NONE || !inBox (s, b))
                        && (kbc = tft.getChar()) == 0) {
            if (timesUp (&t0, NP_TIMEOUT))
                cancelled = true;
            wdDelay(100);
        }
        if (cancelled)
            continue;

        // see whatever happened
        int tap = processNPTap (kbc, focus_fn, b, s);
        // Serial.printf ("ask %d %c\n", tap, isalnum(tap) ? tap : '*');

        // update action
        switch (tap) {
        case NPKB_NONE:
            break;
        case NPKB_LAT:
            eraseNPFocus (b, *focus_fp);
            focus_fp = &fields[NPF_LAT];
            drawNPField (b, *focus_fp);
            drawNPFocus (b, *focus_fp);
            break;
        case NPKB_LNG:
            eraseNPFocus (b, *focus_fp);
            focus_fp = &fields[NPF_LNG];
            drawNPField (b, *focus_fp);
            drawNPFocus (b, *focus_fp);
            break;
        case NPKB_GRID:
            eraseNPFocus (b, *focus_fp);
            focus_fp = &fields[NPF_GRID];
            drawNPField (b, *focus_fp);
            drawNPFocus (b, *focus_fp);
            break;
        case NPKB_DEL:
            eraseNPChar (b, *focus_fp);
            updateOpposite (b, fields, focus_fn);
            break;
        case NPKB_CANCEL:
            cancelled = true;
            break;
        case NPKB_OK:
            ok = true;
            break;
        default:
            newNPChar (b, fields, focus_fn, tap);
            break;
        }

    } while (!cancelled && (!ok || !allNPFieldsOK (b, fields, new_ll, new_grid)));

    // pass back if ok
    if (ok && !cancelled) {
        op_ll = new_ll;
        normalizeLL (op_ll);
        strcpy (op_grid, new_grid);
        return (true);
    } else {
        return (false);
    }
}
