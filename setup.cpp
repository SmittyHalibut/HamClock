/* handle the initial setup screen.
 */

#include <ctype.h>

#include "HamClock.h"

/* defaults
 */
#define DEF_SSID        "FiOS-9QRT4-Guest"
#define DEF_PASS        "Veritium2017"
#define DEF_CALL        "WB0OEW"

// feature tests.

// ESP always needs wifi setup, linux is up to user, others never
#if defined(_IS_ESP8266)
#define _WIFI_ALWAYS
#elif defined(_IS_LINUX)
#include <string.h>
#include <errno.h>
#define _WIFI_ASK
#else
#define _WIFI_NEVER
#endif


// debugs: force all on just for visual testing, and show bounds
// #define _SHOW_ALL
// #define _MARK_BOUNDS
#ifdef _SHOW_ALL
    #undef _WIFI_NEVER
    #undef _WIFI_ASK
    #define _WIFI_ALWAYS
    #define _SUPPORT_FLIP
    #define _SUPPORT_KX3 
    #define _SUPPORT_ENVSENSOR
    #define _SUPPORT_BR
    #define _SUPPORT_GPIO
#endif // _SHOW_ALL


// static storage for published setup items
static char wifissid[NV_WIFI_SSID_LEN];
static char wifipw[NV_WIFI_PW_LEN];
static char call[NV_CALLSIGN_LEN];
static char dxhost[NV_DXHOST_LEN];
static char gpsdhost[NV_GPSDHOST_LEN];
static char ntphost[NV_NTPHOST_LEN];
static uint8_t bright_min, bright_max;
static uint16_t dxport;
static float temp_corr[MAX_N_BME];
static float pres_corr[MAX_N_BME];
static int16_t center_lng;
static int16_t alt_center_lng;
static bool alt_center_lng_set;


// layout constants
#define NQR             4                       // number of virtual keyboard rows
#define NQC             13                      // max number of keyboard columns
#define KB_NCOLS        14                      // n cols in keyboard layout
#define KB_CHAR_H       60                      // height of box containing 1 keyboard character
#define KB_CHAR_W       59                      // width "
#define KB_SPC_Y        (KB_Y0+NQR*KB_CHAR_H)   // top edge of special keyboard chars
#define KB_SPC_H        40                      // heights of special keyboard chars
#define KB_INDENT       16                      // keyboard indent
#define SBAR_X          (KB_INDENT+3*KB_CHAR_W/2)// space bar x coord
#define SBAR_W          (KB_CHAR_W*10)          // space bar width
#define F_DESCENT       5                       // font descent below baseline
#define F_INDENT        20                      // font indent within square
#define KB_Y0           190                     // y coord of keyboard top
#define PR_W            18                      // width of character
#define PR_A            24                      // ascending height above font baseline
#define PR_D            9                       // descending height below font baseline
#define PR_H            (PR_A+PR_D)             // prompt height
#define ASK_TO          10                      // user option timeout, seconds
#define PAGE_W          100                     // page button width
#define CURSOR_DROP     2                       // pixels to drop cursor rot
#define NVMS_NONE       0                       // NV_MAPSPOTS to map nothing
#define NVMS_PREFIX     1                       // NV_MAPSPOTS to map just prefix
#define NVMS_CALL       2                       // NV_MAPSPOTS to map full callsign
#define R2Y(r)          ((r)*(PR_H+2))          // macro given row index return screen y

// color selector constants
#define CSEL_WXC        570                     // x coord of center of color wheel
#define CSEL_WYC        100                     // y coord of center of color wheel
#define CSEL_WR         70                      // radius of color wheel
#define CSEL_DX         220                     // demo strip left x
#define CSEL_DW         150                     // demo strip width
#define CSEL_DH         4                       // demo strip height
#define CSEL_TBCOL      RA8875_RED              // tick box active color
#define CSEL_TBSZ       20                      // tick box size

// OnOff layout constants
#define OO_Y0           240                     // top y -- stay below full screen settings
#define OO_X0           50                      // left x
#define OO_CI           50                      // OnOff label to first column indent
#define OO_CW           90                      // weekday column width
#define OO_RH           28                      // row height -- N.B. must be at least font height
#define OO_ASZ          10                      // arrow size
#define OO_DHX(d)       (OO_X0+OO_CI+(d)*OO_CW) // day of week to hours x
#define OO_CPLX(d)      (OO_DHX(d)+OO_ASZ)      // day of week to copy left x
#define OO_CPRX(d)      (OO_DHX(d)+OO_CW-OO_ASZ)// day of week to copy right x
#define OO_CHY          (OO_Y0-2)               // column headings y
#define OO_CPLY         (OO_Y0-OO_RH/2)         // copy left y
#define OO_CPRY         (OO_Y0-OO_RH/2)         // copy right y
#define OO_ONY          (OO_Y0+2*OO_RH-4)       // on row y
#define OO_OFFY         (OO_Y0+5*OO_RH-4)       // off row y
#define OO_TW           (OO_CI+OO_CW*DAYSPERWEEK)  // total width

// colors
#define TX_C            RA8875_WHITE            // text color
#define BG_C            RA8875_BLACK            // overall background color
#define KB_C            RGB565(80,80,255)       // key border color
#define KF_C            RA8875_WHITE            // key face color
#define PR_C            RGB565(255,125,0)       // prompt color
#define DEL_C           RA8875_RED              // Delete color
#define DONE_C          RA8875_GREEN            // Done color
#define BUTTON_C        RA8875_CYAN             // option buttons color
#define CURSOR_C        RA8875_GREEN            // cursor color
#define ERR_C           RA8875_RED              // err msg color

// validation constants
#define MAX_BME_DTEMP   15
#define MAX_BME_DPRES   20


// NV_X11FLAGS bit defns
#define X11BIT_FULLSCREEN       0x1


// define a string prompt
typedef struct {
    uint8_t page;                               // page number, 0 .. N_PAGES-1
    SBox p_box;                                 // prompt box
    SBox v_box;                                 // value box
    const char *p_str;                          // prompt string
    char *v_str;                                // value string
    uint8_t v_len;                              // size of v_str including EOS
    uint16_t v_cx;                              // x coord of cursor
} StringPrompt;


// N.B. must match string_pr[] order
typedef enum {
    CALL_SPR,
    LAT_SPR,
    LNG_SPR,
    GPSD_SPR,
    SSID_SPR,
    PASS_SPR,
    CENTERLNG_SPR,
    DXHOST_SPR,
    DXPORT_SPR,
    NTPHOST_SPR,
    TEMPCORR_SPR,
    TEMPCORR2_SPR,
    BRMIN_SPR,
    PRESCORR_SPR,
    PRESCORR2_SPR,
    BRMAX_SPR,
    N_SPR
} SPIds; 

// string prompts for each page. N.B. must match SPIds order
static StringPrompt string_pr[N_SPR] = {

    // page 1 -- index 0

    {0, {10,  R2Y(0), 70, PR_H}, {110, R2Y(0), 270, PR_H}, "Call:",   call, NV_CALLSIGN_LEN, 0}, 
    {0, {380, R2Y(0), 90, PR_H}, {480, R2Y(0), 110, PR_H}, "DE Lat:", NULL, 0, 0},             // shadowed
    {0, {590, R2Y(0), 70, PR_H}, {670, R2Y(0), 129, PR_H}, "Lng:",    NULL, 0, 0},             // shadowed

    {0, {330, R2Y(1), 70, PR_H}, {400, R2Y(1), 270, PR_H}, "host:", gpsdhost, NV_GPSDHOST_LEN, 0},
    {0, {110, R2Y(2), 65, PR_H}, {180, R2Y(2), 480, PR_H}, "SSID:", wifissid, NV_WIFI_SSID_LEN, 0},
    {0, {670, R2Y(2),110, PR_H}, { 10, R2Y(3), 789, PR_H}, "Password:", wifipw, NV_WIFI_PW_LEN, 0},
    {0, {480, R2Y(4),120, PR_H}, {600, R2Y(4), 70, PR_H},  "Center Lng:", NULL, 0, 0},         // shadowed

    // page 2 -- index 1

    {1, {110, R2Y(0), 60, PR_H}, {170, R2Y(0), 330, PR_H}, "host:", dxhost, NV_DXHOST_LEN, 0},
    {1, {510, R2Y(0), 60, PR_H}, {575, R2Y(0),  85, PR_H}, "port:", NULL, 0, 0},               // shadowed

    {1, {110, R2Y(1), 60, PR_H}, {170, R2Y(1), 330, PR_H}, "host:", ntphost, NV_NTPHOST_LEN, 0},

    {1, {220, R2Y(2),120, PR_H}, {350, R2Y(2),  70, PR_H}, "dTemp@76:", NULL, 0, 0},           // shadowed
    {1, {430, R2Y(2),120, PR_H}, {560, R2Y(2),  70, PR_H}, "dTemp@77:", NULL, 0, 0},           // shadowed
    {1, {640, R2Y(2), 90, PR_H}, {740, R2Y(2),  50, PR_H}, "brMin%:", NULL, 0, 0},             // shadowed

    {1, {220, R2Y(3),120, PR_H}, {350, R2Y(3),  70, PR_H}, "dPres@76:", NULL, 0, 0},           // shadowed
    {1, {430, R2Y(3),120, PR_H}, {560, R2Y(3),  70, PR_H}, "dPres@77:", NULL, 0, 0},           // shadowed
    {1, {640, R2Y(3), 90, PR_H}, {740, R2Y(3),  50, PR_H}, "brMax%:", NULL, 0, 0},             // shadowed

    // page 3 -- index 2

};



// define a boolean prompt
typedef struct {
    uint8_t page;                               // page number, 0 .. N_PAGES-1
    SBox p_box;                                 // prompt box
    SBox s_box;                                 // state box, if t/f_str
    bool state;                                 // on or off
    const char *p_str;                          // prompt string, or NULL to use just f/t_str
    const char *f_str;                          // "false" string, or NULL
    const char *t_str;                          // "true" string, or NULL
} BoolPrompt;


// N.B. must match bool_pr[] order
typedef enum {
    GEOIP_BPR,
    GPSD_BPR,
    WIFI_BPR,
    LOGUSAGE_BPR,
    DEMO_BPR,
    CLUSTER_BPR,
    CLMAP_BPR,
    CLLABEL_BPR,
    NTPSET_BPR,
    UNITS_BPR,
    X11_FULLSCRN_BPR,
    GPIOOK_BPR,
    KX3ON_BPR,
    KX3BAUD_BPR,
    FLIP_BPR,
    N_BPR
} BPIds;

/* bool prompts. N.B. must match BPIds order
 * N.B. cluster map and kx3 use two "entangled" bools to create 3 states which means
 *      one bool turns a feature on and off, the second holds possible values
 *      when on, eg CLMAP_BPR+CLLABEL_BPR, KX3ON_BPR+KX3BAUD_BPR
 */
static BoolPrompt bool_pr[N_BPR] = {

    // page 1 -- index 0

    {0, {10,  R2Y(1), 140, PR_H}, {160, R2Y(1), 40,  PR_H}, false, "IP Geolocate?", "No", "Yes"},
    {0, {260, R2Y(1),  60, PR_H}, {330, R2Y(1), 40,  PR_H}, false, "gpsd?", "No", NULL},
    {0, {10,  R2Y(2),  70, PR_H}, {110, R2Y(2), 50,  PR_H}, false, "WiFi?", "No", NULL},
    {0, {10,  R2Y(4), 120, PR_H}, {140, R2Y(4), 90,  PR_H}, false, "Log usage?", "Opt-Out", "Opt-In"},
    {0, {260, R2Y(4), 150, PR_H}, {410, R2Y(4), 40,  PR_H}, false, "Demo mode?", "No", "Yes"},

    // page 2 -- index 1

    {1, {10,  R2Y(0),  90, PR_H},  {110, R2Y(0), 110, PR_H}, false, "Cluster?", "No", NULL},
    {1, {665, R2Y(0),  70, PR_H},  {735, R2Y(0), 60,  PR_H}, false, "Map?", "No", NULL},         // map on/off
    {1, {735, R2Y(0),   0, PR_H},  {735, R2Y(0), 60,  PR_H}, false, NULL, "Prefix", "Call"},     // how
                                                                // prefix must be the false state

    {1, {10,  R2Y(1),  90, PR_H},  {110, R2Y(1), 110, PR_H}, false, "NTP?", "Default set", NULL},

    {1, {10,  R2Y(2),  90, PR_H},  {110, R2Y(2), 110, PR_H}, false, "Units?", "Imperial", "Metric"},

    {1, {10,  R2Y(3),  90, PR_H},  {110, R2Y(3), 110, PR_H}, false, "Full scrn?", "No", "Yes"},
                                                                // state box wide enough for "Won't fit"

    {1, {10,  R2Y(4),  90, PR_H},  {110, R2Y(4), 110, PR_H}, false, "GPIO?", "Off", "Active"},
    {1, {220, R2Y(4), 120, PR_H},  {350, R2Y(4),  70, PR_H}, false, "KX3?", "No", NULL},        // kx3 on/off
    {1, {340, R2Y(4),   0, PR_H},  {350, R2Y(4),  70, PR_H}, false, NULL, "4800", "38400"},     // baud
    {1, {510, R2Y(4), 130, PR_H},  {640, R2Y(4),  40, PR_H}, false, "Flip U/D?", "No", "Yes"},

    // page 3 -- index 2

};


// store info about a given string or bool focus field
typedef struct {
    // N.B. always one, the other NULL
    StringPrompt *sp;
    BoolPrompt *bp;
} Focus;

// current focus and page
static Focus cur_focus;
static int cur_page;



/* color selector information.
 * since mouse is required it does not participate in tabbing or Focus.
 */

typedef struct {
    SBox p_box;                                 // prompt box
    SBox t_box;                                 // state tick box
    SBox d_box;                                 // demo patch box
    bool state;                                 // tick box on or off
    uint16_t c;                                 // RGB565 color
    NV_Name nv;                                 // nvram location
    const char *p_str;                          // prompt string
} ColSelPrompt;

// N.B. must match colsel_pr[] order
typedef enum {
    SATFOOT_CSPR,
    SATPATH_CSPR,
    SHORTPATH_CSPR,
    LONGPATH_CSPR,
    GRID_CSPR,
    N_CSPR
} CSIds;

/* color selector prompts. N.B. must match CSIds order
 */
static ColSelPrompt colsel_pr[N_CSPR] = {
    {{30, R2Y(0), 140, PR_H}, {2, R2Y(0)+4, CSEL_TBSZ, CSEL_TBSZ}, {CSEL_DX, R2Y(0)+PR_H/2, CSEL_DW, CSEL_DH},
                true, RA8875_RED, NV_SATFOOTCOLOR, "Sat footprint:"},
    {{30, R2Y(1), 140, PR_H}, {2, R2Y(1)+4, CSEL_TBSZ, CSEL_TBSZ}, {CSEL_DX, R2Y(1)+PR_H/2, CSEL_DW, CSEL_DH},
                false, RGB565(128,0,0), NV_SATPATHCOLOR, "Sat path:"},
    {{30, R2Y(2), 140, PR_H}, {2, R2Y(2)+4, CSEL_TBSZ, CSEL_TBSZ}, {CSEL_DX, R2Y(2)+PR_H/2, CSEL_DW, CSEL_DH},
                false, DE_COLOR, NV_SHORTPATHCOLOR, "Short prop path:"},
    {{30, R2Y(3), 140, PR_H}, {2, R2Y(3)+4, CSEL_TBSZ, CSEL_TBSZ}, {CSEL_DX, R2Y(3)+PR_H/2, CSEL_DW, CSEL_DH},
                false, RA8875_WHITE, NV_LONGPATHCOLOR, "Long prop path:"},
    {{30, R2Y(4), 140, PR_H}, {2, R2Y(4)+4, CSEL_TBSZ, CSEL_TBSZ}, {CSEL_DX, R2Y(4)+PR_H/2, CSEL_DW, CSEL_DH},
                false, RA8875_BLACK, NV_GRIDCOLOR, "Map Grid:"},
};

// color selector wheel
static SBox colorwheel_b  = {CSEL_WXC-CSEL_WR, CSEL_WYC-CSEL_WR, 2*CSEL_WR, 2*CSEL_WR};





// virtual qwerty keyboard
typedef struct {
    char n, s;                                  // normal and shifted char
} Key;
static const Key qwerty[NQR][NQC] PROGMEM = {
    { {'`', '~'}, {'1', '!'}, {'2', '@'}, {'3', '#'}, {'4', '$'}, {'5', '%'}, {'6', '^'},
    {'7', '&'}, {'8', '*'}, {'9', '('}, {'0', ')'}, {'-', '_'}, {'=', '+'}
    },
    { {'Q', 'q'}, {'W', 'w'}, {'E', 'e'}, {'R', 'r'}, {'T', 't'}, {'Y', 'y'}, {'U', 'u'},
      {'I', 'i'}, {'O', 'o'}, {'P', 'p'}, {'[', '{'}, {']', '}'}, {'\\', '|'},
    },
    { {'A', 'a'}, {'S', 's'}, {'D', 'd'}, {'F', 'f'}, {'G', 'g'}, {'H', 'h'}, {'J', 'j'},
      {'K', 'k'}, {'L', 'l'}, {';', ':'}, {'\'', '"'},
    },
    { {'Z', 'z'}, {'X', 'x'}, {'C', 'c'}, {'V', 'v'}, {'B', 'b'}, {'N', 'n'}, {'M', 'm'},
      {',', '<'}, {'.', '>'}, {'/', '?'},
    }
};


// horizontal pixel offset of each virtual keyboard row then follow every KB_CHAR_W
static const uint8_t qroff[NQR] = {
    KB_INDENT,
    KB_INDENT,
    KB_INDENT+KB_CHAR_W,
    KB_INDENT+3*KB_CHAR_W/2
};

// special virtual keyboard chars
static const SBox delete_b  = {KB_INDENT, KB_SPC_Y, SBAR_X-KB_INDENT+1, KB_SPC_H};
static const SBox space_b   = {SBAR_X, KB_SPC_Y, SBAR_W, KB_SPC_H};
static const SBox done_b    = {SBAR_X+SBAR_W, KB_SPC_Y, SBAR_X-KB_INDENT+1, KB_SPC_H};
static const SBox page_b    = {800-PAGE_W-KB_INDENT-1, KB_Y0-37, PAGE_W, 35};

// note whether ll edited
static bool ll_edited;

/* set ll_edited if cur_focus is lat or long
 */
static void checkLLEdit()
{
    if (cur_focus.sp && (cur_focus.sp == &string_pr[LAT_SPR] || cur_focus.sp == &string_pr[LNG_SPR] ))
        ll_edited = true;
}

static void drawPageButton()
{
    char buf[32];
    snprintf (buf, sizeof(buf), _FX("Page %d ..."), cur_page+1);      // user sees 1-based
    drawStringInBox (buf, page_b, false, DONE_C);
}

static void drawDoneButton()
{
    drawStringInBox ("Done", done_b, false, DONE_C);
}


/* return whether the given bool prompt is currently relevant
 */
static bool boolIsRelevant (BoolPrompt *bp)
{
    if (bp->page != cur_page)
        return (false);

#if !defined(_USE_X11)
    if (bp == &bool_pr[X11_FULLSCRN_BPR])
        return (false);
#endif

    if (bp == &bool_pr[WIFI_BPR]) {
        #if defined(_WIFI_ALWAYS) || defined(_WIFI_NEVER)
            return (false);
        #endif
        #if defined(_WIFI_ASK)
            return (true);
        #endif
    }

    if (bp == &bool_pr[CLMAP_BPR]) {
        if (!bool_pr[CLUSTER_BPR].state)
            return (false);
    }

    if (bp == &bool_pr[CLLABEL_BPR]) {
        if (!bool_pr[CLUSTER_BPR].state || !bool_pr[CLMAP_BPR].state)
            return (false);
    }

    if (bp == &bool_pr[FLIP_BPR]) {
        #if !defined(_SUPPORT_FLIP)
            return (false);
        #endif
    }

    if (bp == &bool_pr[KX3ON_BPR]) {
        #if !defined(_SUPPORT_KX3)
            return (false);
        #else
            return (bool_pr[GPIOOK_BPR].state);
        #endif
    }

    if (bp == &bool_pr[KX3BAUD_BPR]) {
        #if !defined(_SUPPORT_KX3)
            return (false);
        #else
            if (!bool_pr[KX3ON_BPR].state || !bool_pr[GPIOOK_BPR].state)
                return (false);
        #endif
    }

    if (bp == &bool_pr[GPIOOK_BPR]) {
        #if !defined(_SUPPORT_GPIO) || !defined(_SUPPORT_ENVSENSOR)
            return (false);
        #endif
    }

    return (true);
}

/* return whether the given string prompt is currently relevant
 */
static bool stringIsRelevant (StringPrompt *sp)
{
    if (sp->page != cur_page)
        return (false);

    if (sp == &string_pr[SSID_SPR] || sp == &string_pr[PASS_SPR]) {
        #if defined(_WIFI_NEVER)
            return (false);
        #endif
        #if defined(_WIFI_ASK)
            if (!bool_pr[WIFI_BPR].state)
                return (false);
        #endif
    }

    if (sp == &string_pr[DXHOST_SPR] || sp == &string_pr[DXPORT_SPR]) {
        if (!bool_pr[CLUSTER_BPR].state)
            return (false);
    }

    if (sp == &string_pr[NTPHOST_SPR]) {
        if (!bool_pr[NTPSET_BPR].state)
            return (false);
    }

    if (sp == &string_pr[LAT_SPR] || sp == &string_pr[LNG_SPR]) {
        if (bool_pr[GEOIP_BPR].state || bool_pr[GPSD_BPR].state)
            return (false);
    }

    if (sp == &string_pr[GPSD_SPR]) {
        if (!bool_pr[GPSD_BPR].state)
            return (false);
    }

    if (sp == &string_pr[TEMPCORR_SPR] || sp == &string_pr[TEMPCORR2_SPR]
                    || sp == &string_pr[PRESCORR_SPR] || sp == &string_pr[PRESCORR2_SPR]) {
        #if defined(_SUPPORT_GPIO) && defined(_SUPPORT_ENVSENSOR)
            return (bool_pr[GPIOOK_BPR].state);
        #else
            return (false);
        #endif
    }

    if (sp == &string_pr[BRMIN_SPR] || sp == &string_pr[BRMAX_SPR]) {
        #if defined(_SUPPORT_BR)
            return (true);
        #else
            return (brControlOk());
        #endif
    }

    return (true);
}

/* move cur_focus to the next tab position.
 * ESP does not know about keyboard input
 */
static void nextTabFocus()
{
#if defined(_IS_UNIX)

    /* table of ordered fields for moving to next focus with each tab.
     * N.B. group and order within to their respective pages
     */
    static const Focus tab_fields[] = {
        // page 1
        {       &string_pr[CALL_SPR], NULL},
        {       &string_pr[LAT_SPR], NULL},
        {       &string_pr[LNG_SPR], NULL},
        { NULL, &bool_pr[GEOIP_BPR] },
        { NULL, &bool_pr[GPSD_BPR] },
        {       &string_pr[GPSD_SPR], NULL},
        { NULL, &bool_pr[WIFI_BPR] },
        {       &string_pr[SSID_SPR], NULL},
        {       &string_pr[PASS_SPR], NULL},
        { NULL, &bool_pr[LOGUSAGE_BPR] },
        { NULL, &bool_pr[DEMO_BPR] },
        {       &string_pr[CENTERLNG_SPR], NULL},

        // page 2
        { NULL, &bool_pr[CLUSTER_BPR] },
        {       &string_pr[DXHOST_SPR], NULL},
        {       &string_pr[DXPORT_SPR], NULL},
        { NULL, &bool_pr[CLMAP_BPR] },
        { NULL, &bool_pr[CLLABEL_BPR] },
        { NULL, &bool_pr[NTPSET_BPR] },
        {       &string_pr[NTPHOST_SPR], NULL},
        { NULL, &bool_pr[UNITS_BPR] },
        {       &string_pr[TEMPCORR_SPR], NULL},
        {       &string_pr[TEMPCORR2_SPR], NULL},
        {       &string_pr[BRMIN_SPR], NULL},
        { NULL, &bool_pr[X11_FULLSCRN_BPR] },
        {       &string_pr[PRESCORR_SPR], NULL},
        {       &string_pr[PRESCORR2_SPR], NULL},
        {       &string_pr[BRMAX_SPR], NULL},
        { NULL, &bool_pr[GPIOOK_BPR] },
        { NULL, &bool_pr[KX3ON_BPR] },
        { NULL, &bool_pr[KX3BAUD_BPR] },
        { NULL, &bool_pr[FLIP_BPR] },

        // page 3
    };
    #define N_TAB_FIELDS    NARRAY(tab_fields)

    // find current position in table
    unsigned f;
    for (f = 0; f < N_TAB_FIELDS; f++)
        if (memcmp (&cur_focus, &tab_fields[f], sizeof(cur_focus)) == 0)
            break;
    if (f == N_TAB_FIELDS) {
        Serial.printf (_FX("cur_focus not found\n"));
        return;
    }

    // move to next relevant field, wrapping if necessary
    for (unsigned i = 1; i <= N_TAB_FIELDS; i++) {
        const Focus *fp = &tab_fields[(f+i)%N_TAB_FIELDS];
        if (fp->sp) {
            if (stringIsRelevant(fp->sp)) {
                cur_focus = *fp;
                return;
            }
        } else {
            if (boolIsRelevant(fp->bp)) {
                cur_focus = *fp;
                return;
            }
        }
    }
    Serial.printf (_FX("new focus not found\n"));

#endif // _IS_UNIX
}

/* set focus to the given string or bool prompt, opposite assumed to be NULL.
 * N.B. effect of setting both is undefined
 */
static void setFocus (StringPrompt *sp, BoolPrompt *bp)
{
    cur_focus.sp = sp;
    cur_focus.bp = bp;
}

/* set focus to the first relevant prompt in the current page
 */
static void setInitialFocus()
{
    StringPrompt *sp0 = NULL;
    BoolPrompt *bp0 = NULL;

    for (uint8_t i = 0; i < N_SPR; i++) {
        StringPrompt *sp = &string_pr[i];
        if (stringIsRelevant(sp)) {
            sp0 = sp;
            break;
        }
    }

    if (!sp0) {
        for (uint8_t i = 0; i < N_BPR; i++) {
            BoolPrompt *bp = &bool_pr[i];
            if (boolIsRelevant(bp)) {
                bp0 = bp;
                break;
            }
        }
    }

    setFocus (sp0, bp0);
}

/* draw cursor for cur_focus
 */
static void drawCursor()
{
    uint16_t y, x1, x2;

    if (cur_focus.sp) {
        StringPrompt *sp = cur_focus.sp;
        y = sp->v_box.y+sp->v_box.h-CURSOR_DROP;
        x1 = sp->v_cx;
        x2 = sp->v_cx+PR_W;
    } else if (cur_focus.bp) {
        BoolPrompt *bp = cur_focus.bp;
        y = bp->p_box.y+bp->p_box.h-CURSOR_DROP;
        if (bp->p_str) {
            // cursor in prompt
            x1 = bp->p_box.x;
            x2 = bp->p_box.x+PR_W;
        } else {
            // cursor in state
            x1 = bp->s_box.x;
            x2 = bp->s_box.x+PR_W;
        }
    } else {
        return;
    }

    tft.drawLine (x1, y, x2, y, CURSOR_C);
    tft.drawLine (x1, y+1, x2, y+1, CURSOR_C);
}

/* erase cursor for cur_focus
 */
static void eraseCursor()
{
    uint16_t y, x1, x2;

    if (cur_focus.sp) {
        StringPrompt *sp = cur_focus.sp;
        y = sp->v_box.y+sp->v_box.h-CURSOR_DROP;
        x1 = sp->v_cx;
        x2 = sp->v_cx+PR_W;
    } else if (cur_focus.bp) {
        BoolPrompt *bp = cur_focus.bp;
        y = bp->p_box.y+bp->p_box.h-CURSOR_DROP;
        x1 = bp->p_box.x;
        x2 = bp->p_box.x+PR_W;
    } else {
        return;
    }

    tft.drawLine (x1, y, x2, y, BG_C);
    tft.drawLine (x1, y+1, x2, y+1, BG_C);
}

/* draw the prompt of the given StringPrompt
 */
static void drawSPPrompt (StringPrompt *sp)
{
    tft.setTextColor (PR_C);
    tft.setCursor (sp->p_box.x, sp->p_box.y+sp->p_box.h-PR_D);
    tft.print(sp->p_str);
#ifdef _MARK_BOUNDS
    tft.drawRect (sp->p_box.x, sp->p_box.y, sp->p_box.w, sp->p_box.h, GRAY);
#endif // _MARK_BOUNDS
}

/* erase the prompt of the given StringPrompt
 */
static void eraseSPPrompt (StringPrompt *sp)
{
    tft.fillRect (sp->p_box.x, sp->p_box.y, sp->p_box.w, sp->p_box.h, BG_C);
}

/* erase the value of the given StringPrompt
 */
static void eraseSPValue (StringPrompt *sp)
{
    tft.fillRect (sp->v_box.x, sp->v_box.y, sp->v_box.w, sp->v_box.h, BG_C);
}

/* draw the value of the given StringPrompt and set v_cx (but don't draw cursor here)
 * N.B. we will shorten v_str to insure it fits within v_box
 */
static void drawSPValue (StringPrompt *sp)
{
    // prep writing into v_box
    tft.setTextColor (TX_C);
    tft.setCursor (sp->v_box.x, sp->v_box.y+sp->v_box.h-PR_D);

    // insure value string fits within box, shortening if necessary
    size_t vl0 = strlen (sp->v_str);
    (void) maxStringW (sp->v_str, sp->v_box.w);
    size_t vl1 = strlen (sp->v_str);

    if (vl1 < vl0) {
        // string was shortened to fit, show cursor under last character
        eraseSPValue (sp);                              // start over
        tft.printf (_FX("%.*s"), vl1 - 1, sp->v_str);   // show all but last char
        sp->v_cx = tft.getCursorX();                    // cursor goes here
        tft.print(sp->v_str[vl1-1]);                    // draw last char over cursor
    } else {
        // more room available, cursor follows string
        tft.print(sp->v_str);
        sp->v_cx = tft.getCursorX();
    }

    // insure cursor remains within box
    if (sp->v_cx + PR_W > sp->v_box.x + sp->v_box.w)
        sp->v_cx = sp->v_box.x + sp->v_box.w - PR_W;

#ifdef _MARK_BOUNDS
    tft.drawRect (sp->v_box.x, sp->v_box.y, sp->v_box.w, sp->v_box.h, GRAY);
#endif // _MARK_BOUNDS
}

/* draw both prompt and value of the given StringPrompt
 */
static void drawSPPromptValue (StringPrompt *sp)
{
    drawSPPrompt (sp);
    drawSPValue (sp);
}

/* erase both prompt and value of the given StringPrompt
 */
static void eraseSPPromptValue (StringPrompt *sp)
{
    eraseSPPrompt (sp);
    eraseSPValue (sp);
}

/* draw the prompt of the given BoolPrompt, if any
 */
static void drawBPPrompt (BoolPrompt *bp)
{
    if (!bp->p_str)
        return;

    #ifdef _WIFI_ALWAYS
        if (bp == &bool_pr[WIFI_BPR])
            tft.setTextColor (PR_C);            // required wifi is just a passive prompt but ...
        else
    #endif
    tft.setTextColor (BUTTON_C);                // ... others are a question.

    tft.setCursor (bp->p_box.x, bp->p_box.y+bp->p_box.h-PR_D);
    tft.print(bp->p_str);
#ifdef _MARK_BOUNDS
    tft.drawRect (bp->p_box.x, bp->p_box.y, bp->p_box.w, bp->p_box.h, GRAY);
#endif // _MARK_BOUNDS
}

/* draw the state of the given BoolPrompt, if any
 */
static void drawBPState (BoolPrompt *bp)
{
    bool show_t = bp->state && bp->t_str;
    bool show_f = !bp->state && bp->f_str;

    if (show_t || show_f) {
        tft.setTextColor (TX_C);
        tft.setCursor (bp->s_box.x, bp->s_box.y+bp->s_box.h-PR_D);
        tft.fillRect (bp->s_box.x, bp->s_box.y, bp->s_box.w, bp->s_box.h, BG_C);
        if (show_t)
            tft.print(bp->t_str);
        if (show_f)
            tft.print(bp->f_str);
    #ifdef _MARK_BOUNDS
        tft.drawRect (bp->s_box.x, bp->s_box.y, bp->s_box.w, bp->s_box.h, GRAY);
    #endif // _MARK_BOUNDS
    }
}

/* erase state of the given BoolPrompt, if any
 */
static void eraseBPState (BoolPrompt *bp)
{
    tft.fillRect (bp->s_box.x, bp->s_box.y, bp->s_box.w, bp->s_box.h, BG_C);
}


/* draw both prompt and state of the given BoolPrompt
 */
static void drawBPPromptState (BoolPrompt *bp)
{
    drawBPPrompt (bp);
    drawBPState (bp);
}


/* erase prompt of the given BoolPrompt 
 */
static void eraseBPPrompt (BoolPrompt *bp)
{
    tft.fillRect (bp->p_box.x, bp->p_box.y, bp->p_box.w, bp->p_box.h, BG_C);
}

/* erase both prompt and state of the given BoolPrompt
 */
static void eraseBPPromptState (BoolPrompt *bp)
{
    eraseBPPrompt (bp);
    eraseBPState (bp);
}


/* draw the virtual keyboard
 */
static void drawKeyboard()
{
    tft.fillRect (0, KB_Y0, tft.width(), tft.height()-KB_Y0-1, BG_C);
    tft.setTextColor (KF_C);

    for (uint8_t r = 0; r < NQR; r++) {
        resetWatchdog();
        uint16_t y = r * KB_CHAR_H + KB_Y0 + KB_CHAR_H;
        const Key *row = qwerty[r];
        for (uint8_t c = 0; c < NQC; c++) {
            const Key *kp = &row[c];
            char n = (char)pgm_read_byte(&kp->n);
            if (n) {
                uint16_t x = qroff[r] + c * KB_CHAR_W;

                // shifted on top
                tft.setCursor (x+F_INDENT, y-KB_CHAR_H/2-F_DESCENT);
                tft.print((char)pgm_read_byte(&kp->s));

                // non-shifted below
                tft.setCursor (x+F_INDENT, y-F_DESCENT);
                tft.print(n);

                // key border
                tft.drawRect (x, y-KB_CHAR_H, KB_CHAR_W, KB_CHAR_H, KB_C);
            }
        }
    }

    drawStringInBox ("", space_b, false, KF_C);
    drawStringInBox ("Delete", delete_b, false, DEL_C);
}


/* remove blanks from s IN PLACE.
 */
static void noBlanks (char *s)
{
    char c, *s_to = s;
    while ((c = *s++) != '\0')
        if (c != ' ')
            *s_to++ = c;
    *s_to = '\0';
}


/* convert a screen coord on the virtual keyboard to its char value, if any.
 * N.B. this does NOT handle Delete or Done.
 */
static bool s2char (SCoord &s, char *cp)
{
    // no KB on page 2
    if (cur_page == 2)
        return (false);

    // check main qwerty
    if (s.y >= KB_Y0) {
        uint16_t kb_y = s.y - KB_Y0;
        uint8_t row = kb_y/KB_CHAR_H;
        if (row < NQR) {
            uint8_t col = (s.x-qroff[row])*KB_NCOLS/tft.width();
            if (col < NQC) {
                const Key *kp = &qwerty[row][col];
                char n = (char)pgm_read_byte(&kp->n);
                if (n) {
                    *cp = kb_y-row*KB_CHAR_H < KB_CHAR_H/2 ? (char)pgm_read_byte(&kp->s) : n;
                    return(true);
                }
            }
        }
    }

    // check space bar
    if (inBox (s, space_b)) {
        *cp = ' ';
        return (true);
    }

    // s is not on the virtual keyboard
    return (false);
}


/* find whether s is in any string_pr.
 * if so return true and set *spp, else return false.
 */
static bool tappedStringPrompt (SCoord &s, StringPrompt **spp)
{
    for (uint8_t i = 0; i < N_SPR; i++) {
        StringPrompt *sp = &string_pr[i];
        if (stringIsRelevant(sp) && inBox (s, sp->v_box)) {
            *spp = sp;
            return (true);
        }
    }
    return (false);
}

/* find whether s is in any relevant bool object.
 * require s within prompt else within state if none.
 * if so return true and set *bpp, else return false.
 */
static bool tappedBool (SCoord &s, BoolPrompt **bpp)
{
    for (uint8_t i = 0; i < N_BPR; i++) {
        BoolPrompt *bp = &bool_pr[i];
        if (!boolIsRelevant(bp))
            continue;
        if ((bp->p_str && inBox (s, bp->p_box)) || (!bp->p_str && inBox (s, bp->s_box))) {
            *bpp = bp;
            return (true);
        }
    }
    return (false);
}


/* convert an x,y value ostensibly within the color wheel box to a color.
 * return true and color if inside circle, else return false.
 */
static bool getCSelBoxColor (uint16_t x, uint16_t y, uint16_t &color)
{
    int16_t dx = (int16_t)x - CSEL_WXC;
    int16_t dy = (int16_t)y - CSEL_WYC;
    uint16_t r2 = dx*dx + dy*dy;
    if (r2 <= CSEL_WR*CSEL_WR) {
        float theta = atan2f (dy, dx) + M_PIF;  // 0 .. 2pi CCW from +x
        float r_frac = sqrtf(r2)/CSEL_WR;
        uint8_t h = 255*theta/(2*M_PIF);
        uint8_t v = r_frac <= 0.5 ? 255*sqrtf(2*r_frac) : 255;
        uint8_t s = r_frac <= 0.5 ? 255 : 255*sqrtf(2*(1-r_frac));
        uint8_t r, g, b;
        hsvtorgb (&r, &g, &b, h, s, v);
        color = RGB565 (r, g, b);
        return (true);
    }

    return (false);
}

/* draw a color selector demo
 */
static void drawCSelDemoSwatch (const ColSelPrompt &p)
{
    tft.fillRect (p.d_box.x, p.d_box.y, p.d_box.w, p.d_box.h, p.c);
}

/* draw a color selector prompt tick box
 */
static void drawCSelTickBox (const ColSelPrompt &p)
{
    tft.fillRect (p.t_box.x, p.t_box.y, p.t_box.w, p.t_box.h, p.state ? CSEL_TBCOL : RA8875_BLACK);
    tft.drawRect (p.t_box.x, p.t_box.y, p.t_box.w, p.t_box.h, RA8875_WHITE);
}

/* handle a possible touch event while on the color selection page.
 * return whether ours
 */
static bool handleCSelTouch (SCoord &s)
{
    bool ours = false;

    if (inBox (s, colorwheel_b)) {
        // set new color for current selection
        for (uint8_t i = 0; i < N_CSPR; i++) {
            ColSelPrompt &pi = colsel_pr[i];
            if (pi.state) {
                if (getCSelBoxColor(s.x, s.y, pi.c))
                    drawCSelDemoSwatch (pi);
                break;
            }
        }
        ours = true;

    } else {
        // check for changing focus
        for (uint8_t i = 0; i < N_CSPR; i++) {
            ColSelPrompt &pi = colsel_pr[i];
            if (inBox (s, pi.t_box) && !pi.state) {
                // clicked an off box, make it the only one on
                for (uint8_t j = 0; j < N_CSPR; j++) {
                    ColSelPrompt &pj = colsel_pr[j];
                    if (pj.state) {
                        pj.state = false;
                        drawCSelTickBox (pj);
                    }
                }
                pi.state = true;
                drawCSelTickBox (pi);
                ours = true;
                break;
            }
        }
    }

    return (ours);
}

/* draw the color controls
 */
static void drawCSelPage()
{
    // draw prompts
    resetWatchdog();
    tft.setTextColor (PR_C);
    for (uint8_t i = 0; i < N_CSPR; i++) {
        ColSelPrompt &p = colsel_pr[i];
        tft.setCursor (p.p_box.x, p.p_box.y+p.p_box.h-PR_D);
        tft.print (p.p_str);
        drawCSelTickBox (p);
        drawCSelDemoSwatch (p);
    }

    // draw color wheel
    resetWatchdog();
    for (int16_t y = CSEL_WYC-CSEL_WR; y < CSEL_WYC+CSEL_WR; y++) {
        for (int16_t x = CSEL_WXC-CSEL_WR; x < CSEL_WXC+CSEL_WR; x++) {
            uint16_t c;
            if (getCSelBoxColor (x, y, c))
                tft.drawPixel (x, y, c);
        }
    }

}


/* draw a V inscribed in a square with size s and given center in one of 4 directions.
 * dir is degs CCW from right
 */
static void drawVee (uint16_t x0, uint16_t y0, uint16_t s, uint16_t dir, uint16_t c16)
{
    uint16_t r = s/2;

    switch (dir) {
    case 0:     // point right
        tft.drawLine (x0+r, y0, x0-r, y0-r, c16);
        tft.drawLine (x0+r, y0, x0-r, y0+r, c16);
        break;
    case 90:    // point up
        tft.drawLine (x0, y0-r, x0-r, y0+r, c16);
        tft.drawLine (x0, y0-r, x0+r, y0+r, c16);
        break;
    case 180:   // point left
        tft.drawLine (x0-r, y0, x0+r, y0-r, c16);
        tft.drawLine (x0-r, y0, x0+r, y0+r, c16);
        break;
    case 270:   // point down
        tft.drawLine (x0, y0+r, x0-r, y0-r, c16);
        tft.drawLine (x0, y0+r, x0+r, y0-r, c16);
        break;
    }
}


/* given dow 0..6, y coord of text and hhmm time print new value
 */
static void drawOnOffTimeCell (int dow, uint16_t y, uint16_t thm)
{
    char buf[20];

    tft.setTextColor(TX_C);
    sprintf (buf, "%02d:%02d", thm/60, thm%60);
    tft.setCursor (OO_DHX(dow)+(OO_CW-getTextWidth(buf))/2, y);
    tft.fillRect (OO_DHX(dow)+1, y-OO_RH+1, OO_CW-2, OO_RH, RA8875_BLACK);
    tft.print (buf);
}

/* draw OnOff table from scratch
 */
static void drawOnOffControls()
{
    // title
    const char *title = brControlOk() ? _FX("Daily Display On/Dim Times") : _FX("Daily Display On/Off Times");
    tft.setCursor (OO_X0+(OO_TW-getTextWidth(title))/2, OO_Y0-OO_RH-4);
    tft.setTextColor (PR_C);
    tft.print (title);


    // DOW column headings and copy marks
    for (int i = 0; i < DAYSPERWEEK; i++) {
        uint16_t l = getTextWidth(dayShortStr(i+1));
        tft.setTextColor (PR_C);
        tft.setCursor (OO_DHX(i)+(OO_CW-l)/2, OO_CHY);
        tft.print (dayShortStr(i+1));
        drawVee (OO_CPLX(i), OO_CPLY, OO_ASZ, 180, BUTTON_C);
        drawVee (OO_CPRX(i), OO_CPRY, OO_ASZ, 0, BUTTON_C);
    }

    // On Off labels
    tft.setTextColor (PR_C);
    tft.setCursor (OO_X0+2, OO_ONY);
    tft.print (F("On"));
    tft.setCursor (OO_X0+2, OO_OFFY);
    if (brControlOk())
        tft.print (F("Dim"));
    else
        tft.print (F("Off"));

    // inc/dec hints
    drawVee (OO_X0+(OO_CI-OO_CW/6)/2, OO_Y0+1*OO_RH/2, OO_ASZ, 90, BUTTON_C);
    drawVee (OO_X0+(OO_CI-OO_CW/6)/2, OO_Y0+5*OO_RH/2, OO_ASZ, 270, BUTTON_C);
    drawVee (OO_X0+(OO_CI-OO_CW/6)/2, OO_Y0+7*OO_RH/2, OO_ASZ, 90, BUTTON_C);
    drawVee (OO_X0+(OO_CI-OO_CW/6)/2, OO_Y0+11*OO_RH/2, OO_ASZ, 270, BUTTON_C);

    // graph lines
    tft.drawRect (OO_X0, OO_Y0-OO_RH, OO_CI+OO_CW*DAYSPERWEEK, OO_RH*7, KB_C);
    tft.drawLine (OO_X0, OO_Y0, OO_X0+OO_CI+OO_CW*DAYSPERWEEK, OO_Y0, KB_C);
    for (int i = 0; i < DAYSPERWEEK; i++)
        tft.drawLine (OO_DHX(i), OO_Y0-OO_RH, OO_DHX(i), OO_Y0+6*OO_RH, KB_C);

    // init table
    uint16_t onoff[NV_DAILYONOFF_LEN];
    NVReadString (NV_DAILYONOFF, (char*)onoff);
    for (int i = 0; i < DAYSPERWEEK; i++) {
        drawOnOffTimeCell (i, OO_ONY, onoff[i]);
        drawOnOffTimeCell (i, OO_OFFY, onoff[i+DAYSPERWEEK]);
    }
}

/* handle possible touch on the onoff controls page.
 * return whether really ours
 */
static bool handleOnOffTouch (SCoord &s)
{

#ifndef _SHOW_ALL
    if (!brOnOffOk())
        return (false);
#endif

    int dow = ((int)s.x - (OO_X0+OO_CI))/OO_CW;
    int row = ((int)s.y - (OO_Y0-OO_RH))/OO_RH;
    if (dow < 0 || dow >= DAYSPERWEEK || row < 0 || row > 6)
        return (false);

    // read onoff times and make handy shortcuts
    uint16_t onoff[NV_DAILYONOFF_LEN];
    NVReadString (NV_DAILYONOFF, (char*)onoff);
    uint16_t *ontimes = &onoff[0];
    uint16_t *offtimes = &onoff[DAYSPERWEEK];

    int col = ((s.x - (OO_X0+OO_CI))/(OO_CW/2)) % 2;
    bool hour_col   = col == 0; // same as copy left
    bool mins_col   = col == 1; // same as copy right
    bool oncpy_row  = row == 0;
    bool oninc_row  = row == 1;
    bool ondec_row  = row == 3;
    bool offinc_row = row == 4;
    bool offdec_row = row == 6;

    if (oncpy_row) {
        int newdow;
        if (hour_col) {
            // copy left
            newdow = (dow - 1 + DAYSPERWEEK) % DAYSPERWEEK;
        } else if (mins_col) {
            // copy right
            newdow = (dow + 1) % DAYSPERWEEK;
        } else
            return (false);
        ontimes[newdow] = ontimes[dow];
        offtimes[newdow] = offtimes[dow];
        drawOnOffTimeCell (newdow, OO_ONY, ontimes[newdow]);
        drawOnOffTimeCell (newdow, OO_OFFY, offtimes[newdow]);
    } else if (oninc_row) {
        if (hour_col)
            ontimes[dow] = (ontimes[dow] + 60) % MINSPERDAY;
        else if (mins_col)
            ontimes[dow] = (ontimes[dow] + 5)  % MINSPERDAY;
        else
            return (false);
        drawOnOffTimeCell (dow, OO_ONY, ontimes[dow]);
    } else if (ondec_row) {
        if (hour_col)
            ontimes[dow] = (ontimes[dow] + MINSPERDAY-60) % MINSPERDAY;
        else if (mins_col)
            ontimes[dow] = (ontimes[dow] + MINSPERDAY-5)  % MINSPERDAY;
        else
            return (false);
        drawOnOffTimeCell (dow, OO_ONY, ontimes[dow]);
    } else if (offinc_row) {
        if (hour_col)
            offtimes[dow] = (offtimes[dow] + 60) % MINSPERDAY;
        else if (mins_col)
            offtimes[dow] = (offtimes[dow] + 5)  % MINSPERDAY;
        else
            return (false);
        drawOnOffTimeCell (dow, OO_OFFY, offtimes[dow]);
    } else if (offdec_row) {
        if (hour_col)
            offtimes[dow] = (offtimes[dow] + MINSPERDAY-60) % MINSPERDAY;
        else if (mins_col)
            offtimes[dow] = (offtimes[dow] + MINSPERDAY-5)  % MINSPERDAY;
        else
            return (false);
        drawOnOffTimeCell (dow, OO_OFFY, offtimes[dow]);
    }

    // save
    NVWriteString (NV_DAILYONOFF, (char*)onoff);

    // ok
    return (true);
}


/* draw the current page of prompts and their current values
 */
static void drawCurrentPageFields()
{
    // draw relevant string prompts on this page
    for (uint8_t i = 0; i < N_SPR; i++) {
        StringPrompt *sp = &string_pr[i];
        if (stringIsRelevant(sp))
            drawSPPromptValue(sp);
    }

    // draw relevant bool prompts on this page
    for (uint8_t i = 0; i < N_BPR; i++) {
        BoolPrompt *bp = &bool_pr[i];
        if (boolIsRelevant(bp))
            drawBPPromptState (bp);
    }

    #if defined(_WIFI_ALWAYS)
        // show prompt but otherwise is not relevant
        if (bool_pr[WIFI_BPR].page == cur_page)
            drawBPPrompt (&bool_pr[WIFI_BPR]);
    #endif

    // set initial focus
    setInitialFocus ();
    drawCursor ();
}

/* cycle cur_page.
 */
static void drawNextPage()
{
    // draw page button first for fast feedback afer tap

    switch (cur_page) {
    case 0:
        // from A to B: keyboard already ok
        cur_page = 1;
        tft.fillRect (0, 0, tft.width(), KB_Y0-1, BG_C);
        drawCurrentPageFields();
        drawPageButton();
        break;

    case 1:
        // from B to C: fresh start, color, on/off
        cur_page = 2;
        eraseScreen();
        drawCSelPage();
        drawPageButton();
        #if defined(_SHOW_ALL)
            drawOnOffControls();
        #else
            if (brOnOffOk())
                drawOnOffControls();
        #endif
        drawDoneButton();
        break;

    default:
    case 2:
        // from scratch or C back to A:
        cur_page = 0;
        eraseScreen();
        drawCurrentPageFields();
        drawPageButton();
        drawKeyboard();
        drawDoneButton();
        break;
    }
}

/* validate all string fields, temporarily indicate ones in error if on current page.
 * return whether all ok.
 */
static bool validateStringPrompts()
{
    // collect bad ids to flag
    SPIds badsid[N_SPR];
    uint8_t n_badsid = 0;

    // check lat/long unless using something else
    if (!bool_pr[GEOIP_BPR].state && !bool_pr[GPSD_BPR].state) {

        char *lat_str = string_pr[LAT_SPR].v_str;
        if (!latSpecIsValid (lat_str, de_ll.lat_d))
            badsid[n_badsid++] = LAT_SPR;

        char *lng_str = string_pr[LNG_SPR].v_str;
        if (!lngSpecIsValid (lng_str, de_ll.lng_d))
            badsid[n_badsid++] = LNG_SPR;
    }

    // check cluster host and port if used
    if (bool_pr[CLUSTER_BPR].state) {

        // host must either be "WSJT-X" or "JTDX" or contain a dot surrounded by chars
        char *host_str = string_pr[DXHOST_SPR].v_str;
        noBlanks(host_str);
        char *dot = strchr (host_str, '.');
        if (!(!strcasecmp (host_str, "WSJT-X") || !strcasecmp (host_str, "JTDX")
                                || (dot && dot != host_str && dot[1] != '\0'))) {
            badsid[n_badsid++] = DXHOST_SPR;
        }

        char *port_str = string_pr[DXPORT_SPR].v_str;
        char *first_bad;
        long portn = strtol (port_str, &first_bad, 10);
        if (*first_bad != '\0' || portn < 23 || portn > 65535)  // 23 is telnet
            badsid[n_badsid++] = DXPORT_SPR;
        else
            dxport = portn;
    }

    // check for plausible temperature corrections
    char *tc_str = string_pr[TEMPCORR_SPR].v_str;
    temp_corr[BME_76] = atof (tc_str);
    if (fabsf(temp_corr[BME_76]) > MAX_BME_DTEMP)
        badsid[n_badsid++] = TEMPCORR_SPR;
    char *tc2_str = string_pr[TEMPCORR2_SPR].v_str;
    temp_corr[BME_77] = atof (tc2_str);
    if (fabsf(temp_corr[BME_77]) > MAX_BME_DTEMP)
        badsid[n_badsid++] = TEMPCORR2_SPR;

    // check for plausible pressure corrections
    char *pc_str = string_pr[PRESCORR_SPR].v_str;
    pres_corr[BME_76] = atof (pc_str);
    if (fabsf(pres_corr[BME_76]) > MAX_BME_DPRES)
        badsid[n_badsid++] = PRESCORR_SPR;
    char *pc2_str = string_pr[PRESCORR2_SPR].v_str;
    pres_corr[BME_77] = atof (pc2_str);
    if (fabsf(pres_corr[BME_77]) > MAX_BME_DPRES)
        badsid[n_badsid++] = PRESCORR2_SPR;

    // require ssid and pw if wifi
    if (bool_pr[WIFI_BPR].state) {
        if (strlen (string_pr[SSID_SPR].v_str) == 0)
            badsid[n_badsid++] = SSID_SPR;
        if (strlen (string_pr[PASS_SPR].v_str) == 0)
            badsid[n_badsid++] = PASS_SPR;
    }

    // allow no spaces in call sign
    if (strchr (string_pr[CALL_SPR].v_str, ' ')) {
        badsid[n_badsid++] = CALL_SPR;
    }

    // require finite gpsd host name if used
    if (bool_pr[GPSD_BPR].state) {
        char *str = string_pr[GPSD_SPR].v_str;
        noBlanks(str);
        if (strlen(str) == 0)
            badsid[n_badsid++] = GPSD_SPR;
    }

    // require finite ntp host name if used
    if (bool_pr[NTPSET_BPR].state) {
        char *str = string_pr[NTPHOST_SPR].v_str;
        noBlanks(str);
        if (strlen(str) == 0)
            badsid[n_badsid++] = NTPHOST_SPR;
    }

    // require both brightness 0..100 and min < max.
    if (brControlOk()) {
        // Must use ints to check for < 0
        int brmn = atoi (string_pr[BRMIN_SPR].v_str);
        int brmx = atoi (string_pr[BRMAX_SPR].v_str);
        bool brmn_ok = brmn >= 0 && brmn <= 100;
        bool brmx_ok = brmx >= 0 && brmx <= 100;
        bool order_ok = brmn < brmx;
        if (!brmn_ok || (!order_ok && brmx_ok))
            badsid[n_badsid++] = BRMIN_SPR;
        if (!brmx_ok || (!order_ok && brmn_ok))
            badsid[n_badsid++] = BRMAX_SPR;
        if (brmn_ok && brmx_ok && order_ok) {
            bright_min = brmn;
            bright_max = brmx;
        }
    }

    // require mercator center longitude -180 <= x < 180
    float clng;
    if (lngSpecIsValid (string_pr[CENTERLNG_SPR].v_str, clng))
        center_lng = clng;
    else
        badsid[n_badsid++] = CENTERLNG_SPR;

    // indicate any values in error, changing pages if necessary to show all
    if (n_badsid > 0) {

        bool show_bad = false;

        do {

            // flag each erroneous value on current page
            for (uint8_t i = 0; i < n_badsid; i++) {
                StringPrompt *sp = &string_pr[badsid[i]];
                if (sp->page == cur_page) {
                    eraseSPValue (sp);
                    tft.setTextColor (ERR_C);
                    tft.setCursor (sp->v_box.x, sp->v_box.y+sp->v_box.h-PR_D);
                    tft.print (F("Err"));
                    show_bad = true;
                }
            }

            // next page if no bad fields on this one
            if (!show_bad) {

                // no bad fields on this page, try next
                drawNextPage();

            } else {

                // found bad field on this page

                // dwell error flag(s)
                wdDelay(2000);

                // restore values
                for (uint8_t i = 0; i < n_badsid; i++) {
                    StringPrompt *sp = &string_pr[badsid[i]];
                    if (sp->page == cur_page) {
                        eraseSPValue (sp);
                        drawSPValue (sp);
                    }
                }

                // redraw cursor in case it's value was flagged
                drawCursor();
            }

        } while (show_bad == false);

        // at least one bad field
        return (false);
    }

    // all good
    return (true);
}


/* if linux try to set NV_WIFI_SSID and NV_WIFI_PASSWD from wpa_supplicant.conf 
 */
static bool getWPA()
{
#if defined(_IS_LINUX)

    // open
    static const char wpa_fn[] = "/etc/wpa_supplicant/wpa_supplicant.conf";
    FILE *fp = fopen (wpa_fn, "r");
    if (!fp) {
        printf ("%s: %s\n", wpa_fn, strerror(errno));
        return (false);
    }

    // read, looking for ssid and psk
    char buf[100], wpa_ssid[100], wpa_psk[100];
    bool found_ssid = false, found_psk = false;
    while (fgets (buf, sizeof(buf), fp)) {
        if (sscanf (buf, " ssid=\"%100[^\"]\"", wpa_ssid) == 1)
            found_ssid = true;
        if (sscanf (buf, " psk=\"%100[^\"]\"", wpa_psk) == 1)
            found_psk = true;
    }

    // finished with file
    fclose (fp);

    // save if found both
    if (found_ssid && found_psk) {
        wpa_ssid[NV_WIFI_SSID_LEN-1] = '\0';
        strcpy (wifissid, wpa_ssid);
        NVWriteString(NV_WIFI_SSID, wifissid);
        wpa_psk[NV_WIFI_PW_LEN-1] = '\0';
        strcpy (wifipw, wpa_psk);
        NVWriteString(NV_WIFI_PASSWD, wifipw);
        return (true);
    }

    // nope
    return (false);

#else

    return (false);

#endif // _IS_LINUX
}


/* load all setup values from nvram or set default values:
 */
static void initSetup()
{
    // init wifi, accept OLD PW if valid

    if (!getWPA() && !NVReadString(NV_WIFI_SSID, wifissid)) {
        strncpy (wifissid, DEF_SSID, NV_WIFI_SSID_LEN-1);
        NVWriteString(NV_WIFI_SSID, wifissid);
    }
    if (!NVReadString(NV_WIFI_PASSWD, wifipw) && !NVReadString(NV_WIFI_PASSWD_OLD, wifipw)) {
        strncpy (wifipw, DEF_PASS, NV_WIFI_PW_LEN-1);
        NVWriteString(NV_WIFI_PASSWD, wifipw);
    }

#if defined(_SHOW_ALL) || defined(_MARK_BOUNDS)
    // don't show my creds when testing
    strcpy (wifissid, "mywifissid");
    strcpy (wifipw, "mywifipassword");
#endif



    // init call sign

    if (!NVReadString(NV_CALLSIGN, call)) {
        strncpy (call, DEF_CALL, NV_CALLSIGN_LEN-1);
        NVWriteString(NV_CALLSIGN, call);
    }



    // init gpsd host and option

    if (!NVReadString (NV_GPSDHOST, gpsdhost)) {
        gpsdhost[0] = '\0';
        NVWriteString (NV_GPSDHOST, gpsdhost);
    }
    uint8_t nv_gpsd;
    if (!NVReadUInt8 (NV_USEGPSD, &nv_gpsd)) {
        nv_gpsd = bool_pr[GPSD_BPR].state = false;
        NVWriteUInt8 (NV_USEGPSD, 0);
    } else
        bool_pr[GPSD_BPR].state = (nv_gpsd != 0);



    // init ntp host and option

    if (!NVReadString (NV_NTPHOST, ntphost)) {
        ntphost[0] = '\0';
        NVWriteString (NV_NTPHOST, ntphost);
    }
    uint8_t nv_ntp;
    if (!NVReadUInt8 (NV_NTPSET, &nv_ntp)) {
        nv_ntp = bool_pr[NTPSET_BPR].state = false;
        NVWriteUInt8 (NV_NTPSET, 0);
    } else
        bool_pr[NTPSET_BPR].state = (nv_ntp != 0);



    // init dx cluster host, port and map options

    if (!NVReadString(NV_DXHOST, dxhost)) {
        memset (dxhost, 0, sizeof(dxhost));
        NVWriteString(NV_DXHOST, dxhost);
    }
    if (!NVReadUInt16(NV_DXPORT, &dxport)) {
        dxport = 0;
        NVWriteUInt16(NV_DXPORT, dxport);
    }
    uint8_t nv_dx;
    if (!NVReadUInt8 (NV_USEDXCLUSTER, &nv_dx)) {
        nv_dx = bool_pr[CLUSTER_BPR].state = false;
        NVWriteUInt8 (NV_USEDXCLUSTER, 0);
    } else
        bool_pr[CLUSTER_BPR].state = (nv_dx != 0);
    uint8_t clmap;
    if (!NVReadUInt8 (NV_MAPSPOTS, &clmap)) {
        clmap = NVMS_NONE;
        NVWriteUInt8 (NV_MAPSPOTS, clmap);
    }
    bool_pr[CLMAP_BPR].state = (clmap != NVMS_NONE);
    bool_pr[CLLABEL_BPR].state = (clmap == NVMS_CALL);



    // init de lat/lng

    // if de never set before set to cental US so it differs from default DX which is 0/0.
    if (!NVReadFloat (NV_DE_LAT, &de_ll.lat_d) || !NVReadFloat (NV_DE_LNG, &de_ll.lng_d)) {
        // http://www.kansastravel.org/geographicalcenter.htm
        de_ll.lng_d = -99;
        de_ll.lat_d = 40;
        normalizeLL(de_ll);
        setNVMaidenhead(NV_DE_GRID, de_ll);
        de_tz.tz_secs = getTZ (de_ll);
        NVWriteInt32(NV_DE_TZ, de_tz.tz_secs);
        NVWriteFloat (NV_DE_LAT, de_ll.lat_d);
        NVWriteFloat (NV_DE_LNG, de_ll.lng_d);
    }

    // reset until ll fields are edited this session
    ll_edited = false;


    // init KX3. NV stores actual baud rate, we just toggle between 4800 and 38400, 0 means off

    uint32_t kx3;
    if (!NVReadUInt32 (NV_KX3BAUD, &kx3)) {
        kx3 = 0;                                // default off
        NVWriteUInt32 (NV_KX3BAUD, kx3);
    }
    bool_pr[KX3ON_BPR].state = (kx3 != 0);
    bool_pr[KX3BAUD_BPR].state = (kx3 == 38400);


    // init GPIOOK -- might effect KX3ON

    uint8_t gpiook;
    if (!NVReadUInt8 (NV_GPIOOK, &gpiook)) {
        gpiook = 0;                             // default off
        NVWriteUInt8 (NV_GPIOOK, gpiook);
    }
    bool_pr[GPIOOK_BPR].state = (gpiook != 0);
    if (!gpiook && bool_pr[KX3ON_BPR].state) {
        // no KX3 if no GPIO
        bool_pr[KX3ON_BPR].state = false;
        NVWriteUInt32 (NV_KX3BAUD, 0);
    }


    // init WiFi

#if defined(_WIFI_ALWAYS)
    bool_pr[WIFI_BPR].state = true;             // always on
    bool_pr[WIFI_BPR].p_str = "WiFi:";          // not a question
#elif defined(_WIFI_ASK)
    bool_pr[WIFI_BPR].state = false;            // default off
#endif


    // init colors

    for (uint8_t i = 0; i < N_CSPR; i++) {
        ColSelPrompt &p = colsel_pr[i];
        if (!NVReadUInt16 (p.nv, &p.c))
            NVWriteUInt16 (p.nv, p.c);
    }


    // X11 flags, engage immediately if defined or sensible thing to do
    uint16_t x11flags;
    int dspw, dsph;
    tft.getScreenSize (&dspw, &dsph);
    Serial.printf ("Display is %d x %d\n", dspw, dsph);
    Serial.printf ("Built for %d x %d\n", BUILD_W, BUILD_H);
    if (NVReadUInt16 (NV_X11FLAGS, &x11flags)) {
        Serial.printf ("x11flags found 0x%02X\n", x11flags);
        bool_pr[X11_FULLSCRN_BPR].state = (x11flags & X11BIT_FULLSCREEN) == X11BIT_FULLSCREEN;
        tft.X11OptionsEngageNow(getX11FullScreen());
    } else {
        // set typical defaults but wait for user choices to save
        bool_pr[X11_FULLSCRN_BPR].state = false;

        // engage full screen now if required to see app
        if (BUILD_W == dspw || BUILD_H == dsph) {
            bool_pr[X11_FULLSCRN_BPR].state = true;
            tft.X11OptionsEngageNow(getX11FullScreen());
        }
    }

    // init and validate daily on-off times

    uint16_t onoff[NV_DAILYONOFF_LEN];
    if (!NVReadString (NV_DAILYONOFF, (char*)onoff)) {
        // try to init from deprecated values
        uint16_t on, off;
        if (!NVReadUInt16 (NV_DPYON, &on))
            on = 0;
        if (!NVReadUInt16 (NV_DPYOFF, &off))
            off = 0;   
        for (int i = 0; i < DAYSPERWEEK; i++) {
            onoff[i] = on;
            onoff[i+DAYSPERWEEK] = off;
        }
        NVWriteString (NV_DAILYONOFF, (char*)onoff);
    } else {
        // reset all if find any bogus from 2.60 bug
        for (int i = 0; i < 2*DAYSPERWEEK; i++) {
            if (onoff[i] >= MINSPERDAY || (onoff[i]%5)) {
                memset (onoff, 0, sizeof(onoff));
                NVWriteString (NV_DAILYONOFF, (char*)onoff);
                break;
            }
        }
    }


    // init several more misc

    uint8_t logok;
    if (!NVReadUInt8 (NV_LOGUSAGE, &logok)) {
        logok = 0;
        NVWriteUInt8 (NV_LOGUSAGE, logok);
    }
    bool_pr[LOGUSAGE_BPR].state = (logok != 0);

    uint8_t rot;
    if (!NVReadUInt8 (NV_ROTATE_SCRN, &rot)) {
        rot = 0;
        NVWriteUInt8 (NV_ROTATE_SCRN, rot);
    }
    bool_pr[FLIP_BPR].state = (rot != 0);

    uint8_t met;
    if (!NVReadUInt8 (NV_METRIC_ON, &met)) {
        met = 0;
        NVWriteUInt8 (NV_METRIC_ON, met);
    }
    bool_pr[UNITS_BPR].state = (met != 0);

    if (!NVReadInt16 (NV_CENTERLNG, &center_lng)) {
        center_lng = 0;
        NVWriteInt16 (NV_CENTERLNG, center_lng);
    }

    // init night option
    if (!NVReadUInt8 (NV_NIGHT_ON, &night_on)) {
        night_on = 1;
        NVWriteUInt8 (NV_NIGHT_ON, night_on);
    }

    // init place names option
    if (!NVReadUInt8 (NV_NAMES_ON, &names_on)) {
        names_on = 0;
        NVWriteUInt8 (NV_NAMES_ON, names_on);
    }

    if (!NVReadFloat (NV_TEMPCORR, &temp_corr[BME_76])) {
        temp_corr[BME_76] = 0;
        NVWriteFloat (NV_TEMPCORR, temp_corr[BME_76]);
    }
    if (!NVReadFloat (NV_PRESCORR, &pres_corr[BME_76])) {
        pres_corr[BME_76] = 0;
        NVWriteFloat (NV_PRESCORR, pres_corr[BME_76]);
    }
    if (!NVReadFloat (NV_TEMPCORR2, &temp_corr[BME_77])) {
        temp_corr[BME_77] = 0;
        NVWriteFloat (NV_TEMPCORR2, temp_corr[BME_77]);
    }
    if (!NVReadFloat (NV_PRESCORR2, &pres_corr[BME_77])) {
        pres_corr[BME_77] = 0;
        NVWriteFloat (NV_PRESCORR2, pres_corr[BME_77]);
    }

    bool_pr[GEOIP_BPR].state = false;

    if (!NVReadUInt8 (NV_BR_MIN, &bright_min)) {
        bright_min = 0;
        NVWriteUInt8 (NV_BR_MIN, bright_min);
    }
    if (!NVReadUInt8 (NV_BR_MAX, &bright_max)) {
        bright_max = 100;
        NVWriteUInt8 (NV_BR_MAX, bright_max);
    }
}


/* return whether user wants to run setup.
 */ 
static bool askRun()
{
    eraseScreen();

    drawStringInBox ("Skip", skip_b, false, TX_C);

    tft.setTextColor (TX_C);
    tft.setCursor (tft.width()/6, tft.height()/5);

    // appropriate prompt
#if defined(_IS_ESP8266)
    tft.print (F("Tap anywhere to enter Setup screen ... "));
#else
    tft.print (F("Click anywhere to enter Setup screen ... "));
#endif // _IS_ESP8266

    int16_t x = tft.getCursorX();
    int16_t y = tft.getCursorY();
    uint16_t to;
    for (to = ASK_TO*10; !skip_skip && to > 0; --to) {
        resetWatchdog();
        if ((to+9)/10 != (to+10)/10) {
            tft.fillRect (x, y-PR_A, 2*PR_W, PR_A+PR_D, BG_C);
            tft.setCursor (x, y);
            tft.print((to+9)/10);
        }

        // check for touch or abort box
        SCoord s;
        TouchType tt = readCalTouch (s);
        if (tt != TT_NONE || tft.getChar()) {
            drainTouch();
            if (tt != TT_NONE && inBox (s, skip_b)) {
                drawStringInBox ("Skip", skip_b, true, TX_C);
                return (false);
            }
                
            break;
        }
        wdDelay(100);
    }

    return (!skip_skip && to > 0);
}





/* init display and supporting StringPrompt and BoolPrompt data structs
 * N.B. must call finishSettingUp() when done
 */
static void initDisplay()
{
    // erase screen
    eraseScreen();

    // set invalid page
    cur_page = -1;

    // init shadow strings. N.B. free() before leaving
    snprintf (string_pr[LAT_SPR].v_str = (char*)malloc(8), string_pr[LAT_SPR].v_len = 8,
                                        "%.2f%c", fabsf(de_ll.lat_d), de_ll.lat_d < 0 ? 'S' : 'N');
    snprintf (string_pr[LNG_SPR].v_str = (char*)malloc(9), string_pr[LNG_SPR].v_len = 9,
                                        "%.2f%c", fabsf(de_ll.lng_d), de_ll.lng_d < 0 ? 'W' : 'E');
    snprintf (string_pr[DXPORT_SPR].v_str = (char*)malloc(8), string_pr[DXPORT_SPR].v_len = 8,
                                        "%u", dxport);
    snprintf (string_pr[TEMPCORR_SPR].v_str = (char*)malloc(8), string_pr[TEMPCORR_SPR].v_len = 8,
                                        "%.2f", temp_corr[BME_76]);
    snprintf (string_pr[PRESCORR_SPR].v_str = (char*)malloc(8), string_pr[PRESCORR_SPR].v_len = 8,
                                        "%.3f", pres_corr[BME_76]);
    snprintf (string_pr[TEMPCORR2_SPR].v_str = (char*)malloc(8), string_pr[TEMPCORR2_SPR].v_len = 8,
                                        "%.2f", temp_corr[BME_77]);
    snprintf (string_pr[PRESCORR2_SPR].v_str = (char*)malloc(8), string_pr[PRESCORR2_SPR].v_len = 8,
                                        "%.3f", pres_corr[BME_77]);
    snprintf (string_pr[BRMIN_SPR].v_str = (char*)malloc(8), string_pr[BRMIN_SPR].v_len = 8,
                                        "%u", bright_min);
    snprintf (string_pr[BRMAX_SPR].v_str = (char*)malloc(8), string_pr[BRMAX_SPR].v_len = 8,
                                        "%u", bright_max);
    snprintf (string_pr[CENTERLNG_SPR].v_str = (char*)malloc(5), string_pr[CENTERLNG_SPR].v_len = 5,
                                        "%.0f%c", fabsf((float)center_lng), center_lng < 0 ? 'W' : 'E');
                                        // conversion to float just to avoid g++ snprintf size warning

    // draw first page
    drawNextPage();
}

/* display an entangled pair of bools, first one is on/off, second is one of two states if first is on
 */
static void drawBoolPair (BPIds on_off, BPIds a_or_b)
{
    if (bool_pr[on_off].state) {
        drawBPPrompt (&bool_pr[on_off]);
        drawBPState (&bool_pr[a_or_b]);
    } else {
        drawBPPromptState (&bool_pr[on_off]);
    }
}

/* run the setup screen until all fields check ok and user wants to exit
 */
static void runSetup()
{
    drainTouch();

    SCoord s;
    char c;

    do {
        StringPrompt *sp;
        BoolPrompt *bp;

        // wait for next tap or character input
        for (;;) {

            // if touch try to also find what char it might be from virtual kb
            if (readCalTouch(s) != TT_NONE) {
                if (!s2char (s, &c))
                    c = 0;
                break;
            }

            // if real kb input, invalidate touch location
            c = tft.getChar();
            if (c) {
                s.x = s.y = 0;
                break;
            }

            // neither, wait and repeat
            resetWatchdog();
            wdDelay(10);
        }

        // process special cases first

        if (inBox (s, page_b) || c == 27) {              // esc

            // show next page
            drawNextPage();
            continue;
        }

        if (cur_page == 2) {

            if (handleCSelTouch(s) || handleOnOffTouch(s))
                continue;

        }

        // proceed with normal fields processing

        if (c == '\t') {

            // move focus to next tab position
            eraseCursor();
            nextTabFocus();
            drawCursor();

        } else if (cur_focus.sp && (inBox (s, delete_b) || c == '\b' || c == 127)) {

            // tapped Delete or kb equiv while focus is string: remove one char

            StringPrompt *sp = cur_focus.sp;
            size_t vl = strlen (sp->v_str);
            if (vl > 0) {
                // erase cursor, shorten string, find new width, erase to end, redraw
                eraseCursor ();
                sp->v_str[vl-1] = '\0';
                uint16_t sw = getTextWidth (sp->v_str);
                tft.fillRect (sp->v_box.x+sw, sp->v_box.y, sp->v_box.w-sw, sp->v_box.h, BG_C);
                drawSPValue (sp);
                drawCursor ();

                checkLLEdit();
            }


        } else if (cur_focus.sp && isprint(c)) {

            // received a new char for string in focus

            StringPrompt *sp = cur_focus.sp;

            // append c if room, else ignore
            size_t vl = strlen (sp->v_str);
            if (vl < sp->v_len-1U) {

                eraseCursor ();

                sp->v_str[vl++] = c;
                sp->v_str[vl] = '\0';

                drawSPValue (sp);
                drawCursor ();

                checkLLEdit();
            }

        } else if (tappedBool (s, &bp) || (c == ' ' && cur_focus.bp)) {

            // typing space applies to focus bool
            if (c == ' ')
                bp = cur_focus.bp;

            // ignore tapping on bools not being shown
            if (!boolIsRelevant(bp))
                continue;

            // move focus here
            eraseCursor ();
            setFocus (NULL, bp);

            // toggle and redraw with new cursor position
            bp->state = !bp->state;
            drawBPState (bp);
            drawCursor ();

            // check for possible secondary implications

            if (bp == &bool_pr[X11_FULLSCRN_BPR]) {

                // check for full screen that won't fit
                if (bp->state) {
                    int maxw = 0, maxh = 0;
                    tft.getScreenSize (&maxw, &maxh);
                    if (BUILD_W > maxw || BUILD_H > maxh) {
                        tft.setCursor (bp->s_box.x, bp->s_box.y+PR_H-PR_D);
                        tft.setTextColor (RA8875_RED);
                        eraseBPState (bp);
                        tft.print ("Won't fit");
                        wdDelay (2000);
                        bp->state = false;
                        drawBPState (bp);
                    }
                }
            }

            else if (bp == &bool_pr[GEOIP_BPR]) {
                // show/hide lat/lng prompts, gpsd
                if (bp->state) {
                    // no gpsd host
                    bool_pr[GPSD_BPR].state = false;
                    eraseSPPromptValue (&string_pr[GPSD_SPR]);
                    drawBPState (&bool_pr[GPSD_BPR]);
                    // no lat/long
                    eraseSPPromptValue (&string_pr[LAT_SPR]);
                    eraseSPPromptValue (&string_pr[LNG_SPR]);
                } else {
                    // show lat/long
                    drawSPPromptValue (&string_pr[LAT_SPR]);
                    drawSPPromptValue (&string_pr[LNG_SPR]);
                }
            }
            
            else if (bp == &bool_pr[NTPSET_BPR]) {
                // show/hide NTP host
                if (bp->state) {
                    // show host prompt
                    eraseBPState (&bool_pr[NTPSET_BPR]);
                    drawSPPromptValue (&string_pr[NTPHOST_SPR]);
                } else {
                    // show default 
                    eraseSPPromptValue (&string_pr[NTPHOST_SPR]);
                    drawBPState (&bool_pr[NTPSET_BPR]);
                }
            }

            else if (bp == &bool_pr[CLUSTER_BPR]) {
                // show/hide dx cluster prompts
                if (bp->state) {
                    // query for host, port and map
                    eraseBPState (&bool_pr[CLUSTER_BPR]);
                    drawSPPromptValue (&string_pr[DXHOST_SPR]);
                    drawSPPromptValue (&string_pr[DXPORT_SPR]);
                    drawBoolPair (CLMAP_BPR, CLLABEL_BPR);
                } else {
                    // hide all and say No
                    eraseSPPromptValue (&string_pr[DXHOST_SPR]);
                    eraseSPPromptValue (&string_pr[DXPORT_SPR]);
                    eraseBPPromptState (&bool_pr[CLLABEL_BPR]);
                    eraseBPPromptState (&bool_pr[CLMAP_BPR]);
                    drawBPState (&bool_pr[CLUSTER_BPR]);
                }
            }

            else if (bp == &bool_pr[GPSD_BPR]) {
                // show/hide gpsd host, geolocate, lat/long
                if (bp->state) {
                    // no lat/long
                    eraseSPPromptValue (&string_pr[LAT_SPR]);
                    eraseSPPromptValue (&string_pr[LNG_SPR]);
                    // no geolocate
                    bool_pr[GEOIP_BPR].state = false;
                    drawBPState (&bool_pr[GEOIP_BPR]);
                    // show gpsd host
                    eraseBPState (&bool_pr[GPSD_BPR]);
                    drawSPPromptValue (&string_pr[GPSD_SPR]);
                } else {
                    // no gpsd host
                    eraseSPPromptValue (&string_pr[GPSD_SPR]);
                    drawBPState (&bool_pr[GPSD_BPR]);
                    // show lat/long
                    drawSPPromptValue (&string_pr[LAT_SPR]);
                    drawSPPromptValue (&string_pr[LNG_SPR]);
                }
            }

            else if (bp == &bool_pr[CLMAP_BPR]) {
                // show cluster label type, or none
                if (bp->state)
                    drawBPPromptState (&bool_pr[CLLABEL_BPR]);
                else
                    drawBPPromptState (&bool_pr[CLMAP_BPR]);
            }

          #if defined(_SUPPORT_GPIO) && defined(_SUPPORT_ENVSENSOR)
            else if (bp == &bool_pr[GPIOOK_BPR]) {
                // toggle KX3 and env sensors
                if (bp->state) {
                    drawBoolPair(KX3ON_BPR, KX3BAUD_BPR);
                    drawSPPromptValue (&string_pr[TEMPCORR_SPR]);
                    drawSPPromptValue (&string_pr[PRESCORR_SPR]);
                    drawSPPromptValue (&string_pr[TEMPCORR2_SPR]);
                    drawSPPromptValue (&string_pr[PRESCORR2_SPR]);
                } else {
                    // turn off kx3 and erase
                    bool_pr[KX3ON_BPR].state = false;
                    eraseBPPromptState (&bool_pr[KX3ON_BPR]);
                    eraseBPPromptState (&bool_pr[KX3BAUD_BPR]);
                    eraseSPPromptValue (&string_pr[TEMPCORR_SPR]);
                    eraseSPPromptValue (&string_pr[PRESCORR_SPR]);
                    eraseSPPromptValue (&string_pr[TEMPCORR2_SPR]);
                    eraseSPPromptValue (&string_pr[PRESCORR2_SPR]);
                }
            }
          #endif // _SUPPORT_GPIO

          #if defined(_WIFI_ASK)
            else if (bp == &bool_pr[WIFI_BPR]) {
                // show/hide wifi prompts
                if (bp->state) {
                    eraseBPState (&bool_pr[WIFI_BPR]);
                    drawSPPromptValue (&string_pr[SSID_SPR]);
                    drawSPPromptValue (&string_pr[PASS_SPR]);
                } else {
                    eraseSPPromptValue (&string_pr[SSID_SPR]);
                    eraseSPPromptValue (&string_pr[PASS_SPR]);
                    drawBPState (&bool_pr[WIFI_BPR]);
                }
            }
          #endif // _WIFI_ASK

          #if defined(_SUPPORT_KX3)
            else if (bp == &bool_pr[KX3ON_BPR]) {
                // show/hide baud rate but honor GPIOOK
                if (bool_pr[GPIOOK_BPR].state) {
                    drawBoolPair(KX3ON_BPR, KX3BAUD_BPR);
                } else if (bool_pr[KX3ON_BPR].state) {
                    // maintain off if no GPIO
                    bool_pr[KX3ON_BPR].state = false;
                    drawBPPromptState (&bool_pr[KX3ON_BPR]);
                }
            }
          #endif // _SUPPORT_KX3

        } else if (tappedStringPrompt (s, &sp) && stringIsRelevant (sp)) {

            // move focus here unless already there
            if (cur_focus.sp != sp) {
                eraseCursor ();
                setFocus (sp, NULL);
                drawCursor ();
            }
        }

    } while (!(inBox (s, done_b) || c == '\r' || c == '\n') || !validateStringPrompts());

    // all fields are valid

}

/* all done
 */
static void finishSettingUp()
{
    // persist results 
    NVWriteString(NV_WIFI_SSID, wifissid);
    NVWriteString(NV_WIFI_PASSWD, wifipw);
    NVWriteString(NV_CALLSIGN, call);
    NVWriteUInt8 (NV_ROTATE_SCRN, bool_pr[FLIP_BPR].state);
    NVWriteUInt8 (NV_METRIC_ON, bool_pr[UNITS_BPR].state);
    NVWriteUInt32 (NV_KX3BAUD, bool_pr[KX3ON_BPR].state ? (bool_pr[KX3BAUD_BPR].state ? 38400 : 4800) : 0);
    NVWriteFloat (NV_TEMPCORR, temp_corr[BME_76]);
    NVWriteFloat (NV_PRESCORR, pres_corr[BME_76]);
    NVWriteFloat (NV_TEMPCORR2, temp_corr[BME_77]);
    NVWriteFloat (NV_PRESCORR2, pres_corr[BME_77]);
    NVWriteUInt8 (NV_BR_MIN, bright_min);
    NVWriteUInt8 (NV_BR_MAX, bright_max);
    NVWriteUInt8(NV_USEGPSD, bool_pr[GPSD_BPR].state);
    NVWriteString(NV_GPSDHOST, gpsdhost);
    NVWriteUInt8(NV_USEDXCLUSTER, bool_pr[CLUSTER_BPR].state);
    NVWriteString(NV_DXHOST, dxhost);
    NVWriteUInt16(NV_DXPORT, dxport);
    NVWriteUInt8 (NV_LOGUSAGE, bool_pr[LOGUSAGE_BPR].state);
    NVWriteUInt8 (NV_MAPSPOTS,
            bool_pr[CLMAP_BPR].state ? (bool_pr[CLLABEL_BPR].state ? NVMS_CALL : NVMS_PREFIX) : NVMS_NONE);
    NVWriteUInt8 (NV_NTPSET, bool_pr[NTPSET_BPR].state);
    NVWriteString(NV_NTPHOST, ntphost);
    NVWriteUInt8 (NV_GPIOOK, bool_pr[GPIOOK_BPR].state);
    NVWriteInt16 (NV_CENTERLNG, center_lng);

    // save and engage user's X11 settings
    uint16_t x11flags = 0;
    if (bool_pr[X11_FULLSCRN_BPR].state)
        x11flags |= X11BIT_FULLSCREEN;
    NVWriteUInt16 (NV_X11FLAGS, x11flags);
    tft.X11OptionsEngageNow(getX11FullScreen());

    // save colors
    for (uint8_t i = 0; i < N_CSPR; i++) {
        ColSelPrompt &p = colsel_pr[i];
        NVWriteUInt16 (p.nv, p.c);
    }

    // set DE tz and grid only if ll was edited and op is not using some other method to set location
    if (!bool_pr[GEOIP_BPR].state && !bool_pr[GPSD_BPR].state && ll_edited) {
        normalizeLL (de_ll);
        NVWriteFloat(NV_DE_LAT, de_ll.lat_d);
        NVWriteFloat(NV_DE_LNG, de_ll.lng_d);
        setNVMaidenhead(NV_DE_GRID, de_ll);
        de_tz.tz_secs = getTZ (de_ll);
        NVWriteInt32(NV_DE_TZ, de_tz.tz_secs);
    }

    // clean up shadow strings
    free (string_pr[LAT_SPR].v_str);
    free (string_pr[LNG_SPR].v_str);
    free (string_pr[DXPORT_SPR].v_str);
    free (string_pr[TEMPCORR_SPR].v_str);
    free (string_pr[PRESCORR_SPR].v_str);
    free (string_pr[TEMPCORR2_SPR].v_str);
    free (string_pr[PRESCORR2_SPR].v_str);
    free (string_pr[BRMIN_SPR].v_str);
    free (string_pr[BRMAX_SPR].v_str);
    free (string_pr[CENTERLNG_SPR].v_str);

}

/* draw the given string with border centered inside the given box using the current font.
 */
void drawStringInBox (const char str[], const SBox &b, bool inverted, uint16_t color)
{
    uint16_t sw = getTextWidth ((char*)str);

    uint16_t fg = inverted ? BG_C : color;
    uint16_t bg = inverted ? color : BG_C;

    tft.setCursor (b.x+(b.w-sw)/2, b.y+3*b.h/4);
    tft.fillRect (b.x, b.y, b.w, b.h, bg);
    tft.drawRect (b.x, b.y, b.w, b.h, KB_C);
    tft.setTextColor (fg);
    tft.print(str);
}


/* grab everything from NV, setting defaults if first time, then allow user to change,
 * saving to NV if needed.
 */
void clockSetup()
{
    // must start with a calibrated screen
    calibrateTouch(false);

    // set font used throughout, could use BOLD if not for long wifi password
    selectFontStyle (LIGHT_FONT, SMALL_FONT);

    // load values from nvram, else set defaults
    initSetup();

    // ask user whether they want to run setup
    if (!askRun())
        return;

    // ok, user wants to run setup

    // init display prompts and options
    initDisplay();

    // get current rotation state so we can tell whether it changes
    bool rotated = rotateScreen();

    // main interaction loop
    runSetup();

    // finish up
    finishSettingUp();

    // must recalibrate if rotating screen
    if (rotated != rotateScreen()) {
        tft.setRotation(rotateScreen() ? 2 : 0);
        calibrateTouch(true);
    }

}

/* return whether the given string is a valid latitude specification, set lat if so
 */
bool latSpecIsValid (const char *lat_spec, float &lat)
{
    float v;
    char ns = ' ', x = ' ';

    int n_lats = sscanf (lat_spec, "%f%c%c", &v, &ns, &x);
    ns = toupper(ns);
    if (n_lats == 1 && v >= -90 && v <= 90)
        lat = v;
    else if ((n_lats == 2 || (n_lats == 3 && isspace(x))) && v >= 0 && v <= 90 && (ns == 'N' || ns == 'S'))
        lat = ns == 'S' ? -v : v;
    else
        return (false);

    return (true);
}

/* return whether the given string is a valid longitude specification, set lng if so.
 * N.B. we allow 180 east in spec but return lng as 180 west.
 */
bool lngSpecIsValid (const char *lng_spec, float &lng)
{
    float v;
    char ew = ' ', x = ' ';

    int n_lngs = sscanf (lng_spec, "%f%c%c", &v, &ew, &x);
    ew = toupper(ew);
    if (n_lngs == 1 && v >= -180 && v <= 180)
        lng = v;
    else if ((n_lngs == 2 || (n_lngs == 3 && isspace(x))) && v >= 0 && v <= 180 && (ew == 'E' || ew == 'W'))
        lng = ew == 'W' ? -v : v;
    else
        return (false);

    if (lng == 180)
        lng = -180;

    return (true);
}



/* only for main() to call once very early to allow setting initial default
 */
void setX11FullScreen (bool on)
{
    uint16_t x11flags = (on ? X11BIT_FULLSCREEN : 0);
    NVWriteUInt16 (NV_X11FLAGS, x11flags);
}


/* return pointer to static storage containing the WiFi SSID, else NULL if not used
 */
const char *getWiFiSSID()
{
    // don't try to set linux wifi while testing
    #ifndef _SHOW_ALL
        if (bool_pr[WIFI_BPR].state)
            return (wifissid);
        else
    #endif // !_SHOW_ALL
            return (NULL);
}


/* return pointer to static storage containing the WiFi password, else NULL if not used
 */
const char *getWiFiPW()
{
    // don't try to set linux wifi while testing
    #ifndef _SHOW_ALL
        if (bool_pr[WIFI_BPR].state)
            return (wifipw);
        else
    #endif // !_SHOW_ALL
            return (NULL);
}


/* return pointer to static storage containing the Callsign
 */
const char *getCallsign()
{
    return (call);
}

/* return pointer to static storage containing the DX cluster host
 * N.B. only sensible if useDXCluster() is true
 */
const char *getDXClusterHost()
{
    return (dxhost);
}

/* return pointer to static storage containing the GPSD host
 * N.B. only sensible if useGPSD() is true
 */
const char *getGPSDHost()
{
    return (gpsdhost);
}

/* return pointer to static storage containing the NTP host defined herein
 * N.B. only sensible if useLocalNTPHost() is true
 */
const char *getLocalNTPHost()
{
    return (ntphost);
}

/* return dx cluster node port
 * N.B. only sensible if useDXCluster() is true
 */
int getDXClusterPort()
{
    return (dxport);
}

/* return whether we should be allowing DX cluster
 */
bool useDXCluster()
{
    return (bool_pr[CLUSTER_BPR].state);
}

/* return whether to rotate the screen
 */
bool rotateScreen()
{
    return (bool_pr[FLIP_BPR].state);
}

/* return whether to use metric units
 */
bool useMetricUnits()
{
    return (bool_pr[UNITS_BPR].state);
}

/* return whether to map dx spots
 */
bool mapDXClusterSpots()
{
    return (bool_pr[CLMAP_BPR].state);
}

/* return whether to plot dx spots as whole callsigns, else just prefix.
 * N.B. only sensible if mapDXClusterSpots() is true
 */
bool plotSpotCallsigns()
{
    return (bool_pr[CLLABEL_BPR].state);
}

/* return whether to use IP geolocation
 */
bool useGeoIP()
{
    return (bool_pr[GEOIP_BPR].state);
}

/* return whether to use GPSD for time and location
 */
bool useGPSD()
{
    return (bool_pr[GPSD_BPR].state);
}

/* return whether to use NTP host set here
 */
bool useLocalNTPHost()
{
    return (bool_pr[NTPSET_BPR].state);
}

/* return whether user is ok with logging usage
 */
bool logUsageOk()
{
    return (bool_pr[LOGUSAGE_BPR].state);
}

/* return whether ok to use GPIO
 */
bool GPIOOk ()
{
    return (bool_pr[GPIOOK_BPR].state);
}

/* return temperature correction for sensor given BME_76 or BME_77.
 * at this point it's just a number, caller should interpret according to useMetricUnits()
 */
float getBMETempCorr(int i)
{
    return (temp_corr[i % MAX_N_BME]);
}

/* return pressure correction for sensor given BME_76 or BME_77.
 * at this point it's just a number, caller should interpret according to useMetricUnits()
 */
float getBMEPresCorr(int i)
{
    return (pres_corr[i % MAX_N_BME]);
}

/* return KX3 baud rate, 0 if off or no GPIO
 */
uint32_t getKX3Baud()
{
    return (bool_pr[KX3ON_BPR].state && bool_pr[GPIOOK_BPR].state
                ? (bool_pr[KX3BAUD_BPR].state ? 38400 : 4800) : 0);
}

/* return desired maximum brightness, percentage
 */
uint8_t getBrMax()
{
    return (bright_max);
}

/* return desired minimum brightness, percentage
 */
uint8_t getBrMin()
{
    return (bright_min);
}

/* return sat path color
 */
uint16_t getSatPathColor()
{
    return (colsel_pr[SATPATH_CSPR].c);
}

/* return sat footprint color
 */
uint16_t getSatFootColor()
{
    return (colsel_pr[SATFOOT_CSPR].c);
}

/* return short path color
 */
uint16_t getShortPathColor()
{
    return (colsel_pr[SHORTPATH_CSPR].c);
}

/* return long path color
 */
uint16_t getLongPathColor()
{
    return (colsel_pr[LONGPATH_CSPR].c);
}

/* return grid color
 */
uint16_t getGridColor()
{
    return (colsel_pr[GRID_CSPR].c);
}

/* whether to engage full screen.
 */
bool getX11FullScreen(void)
{
    return (bool_pr[X11_FULLSCRN_BPR].state);
}

/* whether demo mode is requested
 */
bool getDemoMode(void)
{
    return (bool_pr[DEMO_BPR].state);
}

/* set whether demo mode is active
 */
void setDemoMode(bool on)
{
    bool_pr[DEMO_BPR].state = on;
}

/* return desired mercator map center longitude.
 * caller may assume -180 <= x < 180
 */
int16_t getCenterLng()
{
    return (alt_center_lng_set ? alt_center_lng : center_lng);
}

/* set desired mercator map center longitude.
 * N.B. only works for subsequenct calls to getCenterLng(): ignores initSetup() and not stored to NVRAM
 */
void setCenterLng (int16_t l)
{
    l = ((l + (180+360*10)) % 360) - 180;       // enforce [-180, 180)
    alt_center_lng = l;
    alt_center_lng_set = true;
}
