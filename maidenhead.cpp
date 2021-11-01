/* maidenhead conversion functions.
 *
 * unit test: gcc -D_UNIT_TEST -o maidenhead{,.cpp}
 *
 *   ./maidenhead 32.3334 -111.1666     # move a little NE of SW corner
 *    32.3334 -111.1667: DM42ki
 *
 *   ./maidenhead DM42ki
 *    DM42ki:   32.3333 -111.1667
 *
 */

#ifdef _UNIT_TEST

// stand-alone test program

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <string.h>
#include <math.h>

typedef struct {
    float lat, lng;             // radians north, east
    float lat_d, lng_d;         // degrees
} LatLong;

#define MAID_CHARLEN    7       // maidenhead string length, including EOS


float rad2deg (float r) { return (57.29578F*r); }
float deg2rad (float d) { return (0.01745329F*d); }

#else

// part of HamClock

#include "HamClock.h"

#endif // !_UNIT_TEST



/* convert lat_d,lng_d to the containing maidenhead designation string.
 * grids grow northward from -90 and westward from -180
 */
void ll2maidenhead (char maid[MAID_CHARLEN], const LatLong &ll)
{
    uint16_t o;

    float lg = ll.lng_d + 180;          // move to -180 .. 180
    o = lg/20;                          // 20 deg steps
    maid[0] = 'A' + o;
    lg -= o*20;
    o = lg/2;                           // 2 deg steps
    maid[2] = '0' + o;
    lg -= o*2;
    o = lg/(5.0F/60.0F);                // 5 minute steps
    maid[4] = 'a' + o;

    float lt = ll.lat_d + 90;           // move to 0 .. 180
    o = lt/10;                          // 10 deg steps
    maid[1] = 'A' + o;
    lt -= o*10;
    o = lt/1;                           // 1 deg steps
    maid[3] = '0' + o;
    lt -= o*1;
    o = lt/(2.5F/60.0F);                // 2.5 minute steps
    maid[5] = 'a' + o;

    maid[6] = '\0';
}

/* convert maidenhead string to ll at SW corner.
 * accept 4 or 6 char version and allow either case.
 */
bool maidenhead2ll (LatLong &ll, const char maid[MAID_CHARLEN])
{
    // work in all upper-case
    char uc_maid[MAID_CHARLEN];
    for (int i = 0; i < MAID_CHARLEN; i++)
        uc_maid[i] = toupper(maid[i]);

    // check first four chars, always required
    if (uc_maid[0] < 'A' || uc_maid[0] > 'R'
                  || uc_maid[1] < 'A' || uc_maid[1] > 'R'
                  || !isdigit(uc_maid[2]) || !isdigit(uc_maid[3]))
        return (false);

    // next 2 are optional as either absent or blanks; synth noop if absent
    if (uc_maid[4] == '\0' || (uc_maid[4] == ' ' && (uc_maid[5] == '\0' || uc_maid[5] == ' ')))
        strcpy (uc_maid+4, "AA");
    else if (uc_maid[4] < 'A' || uc_maid[4] > 'X' || uc_maid[5] < 'A' || uc_maid[5] > 'X')
        return (false);

    ll.lng_d = 20.0F*(uc_maid[0] - 'A') + 2.0F*(uc_maid[2] - '0') + (5.0F/60.0F)*(uc_maid[4] - 'A') + - 180;
    ll.lng = deg2rad (ll.lng_d);

    ll.lat_d = 10.0F*(uc_maid[1] - 'A') + 1.0F*(uc_maid[3] - '0') + (2.5F/60.0F)*(uc_maid[5] - 'A') + - 90;
    ll.lat = deg2rad (ll.lat_d);

    return (true);
}


#if !defined(_UNIT_TEST)


/* set NVRAM nv to the maidenhead location for ll
 */
void setNVMaidenhead(NV_Name nv, LatLong &ll)
{
    char maid[MAID_CHARLEN];
    ll2maidenhead (maid, ll);
    NVWriteString (nv, maid);
}

/* return the given maidenhead value from NV.
 * use *OLD version if new one not set yet
 */
void getNVMaidenhead (NV_Name nv, char maid[MAID_CHARLEN])
{
    if (!NVReadString (nv, maid)) {
        // new value never used yet, try to set from old values
        NV_Name old_nv = nv == NV_DE_GRID ? NV_DE_GRID_OLD : NV_DX_GRID_OLD;
        uint32_t old_grid;
        if (NVReadUInt32 (old_nv, &old_grid)) {
            // unpack as 4 chars
            memcpy (maid, &old_grid, 4);
            maid[5] = 0;
        } else {
            // no old either, return 0/0
            LatLong ll;
            ll.lat_d = ll.lng_d = 0;
            ll2maidenhead (maid, ll);
        }
        NVWriteString (nv, maid);
    }
}

#endif // !_UNIT_TEST

#ifdef _UNIT_TEST

int main (int ac, char *av[])
{
    if (ac == 2) {
        // given maidenhead, find ll
        LatLong ll;
        char *maid = av[1];
        if (!maidenhead2ll (ll, maid))
            printf ("Bad maidenhead: %s\n", maid);
        else
            printf ("%s: %9.4f %9.4f\n", maid, ll.lat_d, ll.lng_d);
    } else if (ac == 3) {
        // given ll, find maidenhead
        LatLong ll;
        ll.lat_d = atof(av[1]);
        ll.lng_d = atof(av[2]);
        char maid[MAID_CHARLEN];
        ll2maidenhead (maid, ll);
        printf ("%9.4f %9.4f: %s\n", ll.lat_d, ll.lng_d, maid);
    } else {
        fprintf (stderr, "Purpose: comvert between lat/long and maidenhead grid square.\n");
        fprintf (stderr, "Usage 1: %s <grid>\n", av[0]);
        fprintf (stderr, "Usage 2: %s <lat> <long>\n", av[0]);
        exit (1);
    }

    return (0);
}

#endif // _UNIT_TEST
