/* this file manages the background maps, both static styles and VOACAP area propagation.
 *
 * On ESP:
 *    maps are stored in a LittleFS file system, pixels accessed with file.seek and file.read with a cache
 * On all desktops:
 *    maps are stored in $HOME/.hamclock, pixels accessed with mmap
 *
 * all map files are RGB565 BMP V4 format.
 */


#include "HamClock.h"



// BMP file format parameters
#define COREHDRSZ 14                                    // always 14 bytes at front of header
#define HDRVER 108                                      // BITMAPV4HEADER, these many more bytes in subheader
#define BHDRSZ (COREHDRSZ+HDRVER)                       // total header size
#define BPERBMPPIX 2                                    // bytes per BMP pixel


// current CoreMap designation even if not currently being shown, if any
CoreMaps core_map = CM_NONE;                            // current core map, if any


// central file name components for the core background maps -- not including voacap.
// N.B. must be in same order as CoreMaps
const char *map_styles[CM_N] = {
    "Countries",
    "Terrain",
    "DRAP",
};


#if defined(_IS_ESP8266)

/***********************************************************************************************
 *
 * Only on ESP
 *
 ***********************************************************************************************/

/* LittleFS seek+read performance: read_ms = 88 + 0.23/byte
 * Thus longer caches help mercator but greatly slow azimuthal due to cache misses. Whole row is
 * a huge loss for azimuthal.
 */

// persistant state
#define N_CACHE_COLS     50                             // n read-ahead columns to cache
static File day_file, night_file;                       // open LittleFS file handles
static uint8_t *day_row_cache, *night_row_cache;        // row caches
static uint16_t day_cache_row, night_cache_row;         // which starting rows
static uint16_t day_cache_col, night_cache_col;         // which starting cols


/* return day RGB565 pixel at the given location.
 * ESP version
 */
bool getMapDayPixel (uint16_t row, uint16_t col, uint16_t *dayp)
{
        // beware no map
        if (!day_row_cache) {
            *dayp = 0;
            return (false);
        }

        // report cache miss stats occasionally
        static int n_query, cache_miss;
        if (++n_query == 1000) {
            // Serial.printf ("day cache miss   %4d/%4d\n", cache_miss, n_query);
            cache_miss = n_query = 0;
        }

        if (row >= HC_MAP_H || col >= HC_MAP_W) {
            Serial.printf (_FX("%s: day %d %d out of bounds %dx%d\n"), row, col, HC_MAP_W, HC_MAP_H);
            return (false);
        }

        // update cache if miss
        if (row != day_cache_row || col < day_cache_col || col >= day_cache_col+N_CACHE_COLS) {
            cache_miss++;
            resetWatchdog();
            if (!day_file.seek (BHDRSZ + (row*HC_MAP_W+col)*BPERBMPPIX, SeekSet)
                        || !day_file.read (day_row_cache, BPERBMPPIX*N_CACHE_COLS)) {
                Serial.printf (_FX("day pixel read err at %d x %d\n"), row, col);
                return (false);
            }
            day_cache_row = row;
            day_cache_col = col;
        }

        // return value from cache
        int idx0 = (col-day_cache_col)*BPERBMPPIX;
        *dayp = *(uint16_t*)(&day_row_cache[idx0]);

        // ok
        return (true);
}


/* return night RGB565 pixel at the given location.
 * ESP version
 */
bool getMapNightPixel (uint16_t row, uint16_t col, uint16_t *nightp)
{
        // beware no map
        if (!night_row_cache) {
            *nightp = 0;
            return (false);
        }

        // report cache miss stats occasionally
        static int n_query, cache_miss;
        if (++n_query == 1000) {
            // Serial.printf ("night cache miss   %4d/%4d\n", cache_miss, n_query);
            cache_miss = n_query = 0;
        }

        if (row >= HC_MAP_H || col >= HC_MAP_W) {
            Serial.printf (_FX("%s: night %d %d out of bounds %dx%d\n"), row, col, HC_MAP_W, HC_MAP_H);
            return (false);
        }

        // update cache if miss
        if (row != night_cache_row || col < night_cache_col || col >= night_cache_col+N_CACHE_COLS) {
            cache_miss++;
            resetWatchdog();
            if (!night_file.seek (BHDRSZ + (row*HC_MAP_W+col)*BPERBMPPIX, SeekSet)
                        || !night_file.read (night_row_cache, BPERBMPPIX*N_CACHE_COLS)) {
                Serial.printf (_FX("night pixel read err at %d x %d\n"), row, col);
                return (false);
            }
            night_cache_row = row;
            night_cache_col = col;
        }

        // return value from cache
        int idx0 = (col-night_cache_col)*BPERBMPPIX;
        *nightp = *(uint16_t*)(&night_row_cache[idx0]);

        // ok
        return (true);
}

/* invalidate pixel connection until proven good again
 * ESP version
 */
static void invalidatePixels()
{
        if (day_row_cache) {
            free (day_row_cache);
            day_row_cache = NULL;
        }
        if (night_row_cache) {
            free (night_row_cache);
            night_row_cache = NULL;
        }
}

/* prepare open day_file and night_file for pixel access.
 * if trouble close both and return false, else return true.
 * ESP version
 */
static bool installFilePixels (const char *dfile, const char *nfile)
{
        // check files are indeed open
        if (!day_file || !night_file) {

            // note and close the open file(s)
            if (day_file)
                day_file.close();
            else
                Serial.printf (_FX("%s not open\n"), dfile);
            if (night_file)
                night_file.close();
            else
                Serial.printf (_FX("%s not open\n"), nfile);

            // bad
            return (false);
        }

        // init row caches for getMapDay/NightPixel()
        day_row_cache = (uint8_t *) realloc (day_row_cache, BPERBMPPIX*N_CACHE_COLS);
        night_row_cache = (uint8_t *) realloc (night_row_cache, BPERBMPPIX*N_CACHE_COLS);
        day_cache_col = day_cache_row = ~0;     // mark as invalid
        night_cache_col = night_cache_row = ~0;

        // ok!
        return (true);
}

/* qsort-style compare two FS_Info by UNIX time
 * ESP only
 */
static int FSInfoTimeQsort (const void *p1, const void *p2)
{
        time_t t1 = ((FS_Info *)p1)->t0;
        time_t t2 = ((FS_Info *)p2)->t0;

        if (t1 < t2)
            return (-1);
        if (t1 > t2)
            return (1);
        return(0);
}

/* ESP FLASH can only hold 4 map files, remove some if necessary to make room for specied number.
 * ESP version
 */
static void cleanFLASH (const char *title, int need_room)
{
        resetWatchdog();

        // max number of existing files allowable
        int max_ok = 4 - need_room;

        // get info on existing files
        uint64_t fs_size, fs_used;
        char *fs_name;
        int n_files;
        FS_Info *fip0 = getConfigDirInfo (&n_files, &fs_name, &fs_size, &fs_used);

        // done if already room
        if (n_files <= max_ok) {
            free (fs_name);
            free (fip0);
            return;
        }

        // always remove propmaps because they are very transient
        for (int i = 0; i < n_files; i++) {
            FS_Info *fip = &fip0[i];
            if (strstr (fip->name, "PropMap")) {
                Serial.printf (_FX("%s: rm %s\n"), title, fip->name);
                LittleFS.remove (fip->name);
            }
        }

        // recheck
        free (fs_name);
        free (fip0);
        fip0 = getConfigDirInfo (&n_files, &fs_name, &fs_size, &fs_used);
        if (n_files <= max_ok) {
            free (fs_name);
            free (fip0);
            return;
        }

        // remove other than core_map
        if (core_map != CM_NONE) {
            const char *keep_style = map_styles[core_map];
            for (int i = 0; i < n_files; i++) {
                FS_Info *fip = &fip0[i];
                if (!strstr (fip->name, keep_style)) {
                    Serial.printf (_FX("%s: rm %s\n"), title, fip->name);
                    LittleFS.remove (fip->name);
                }
            }
        }

        // recheck
        free (fs_name);
        free (fip0);
        fip0 = getConfigDirInfo (&n_files, &fs_name, &fs_size, &fs_used);
        if (n_files <= max_ok) {
            free (fs_name);
            free (fip0);
            return;
        }

        // still too many. sort by time, oldest first
        qsort (fip0, n_files, sizeof(*fip0), FSInfoTimeQsort);

        // remove oldest until enough room
        for (int i = 0; i < n_files && n_files-i > max_ok; i++) {
            FS_Info *fip = &fip0[i];
            Serial.printf (_FX("%s: rm %s\n"), title, fip->name);
            LittleFS.remove (fip->name);
        }

        // should be ok
        free (fs_name);
        free (fip0);
}


#else   // !_IS_ESP8266



/***********************************************************************************************
 *
 * only on UNIX
 *
 ***********************************************************************************************/



#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/mman.h>


// persistent state of open files, allows restarting
static File day_file, night_file;                       // open LittleFS file handles
static int day_fbytes, night_fbytes;                    // bytes mmap'ed
static char *day_pixels, *night_pixels;                 // pixels mmap'ed


// dummies for linking
bool getMapDayPixel (uint16_t row, uint16_t col, uint16_t *nightp) { return (false); }
bool getMapNightPixel (uint16_t row, uint16_t col, uint16_t *nightp) { return (false); }
static void cleanFLASH (const char *title, int n) {}


/* invalidate pixel connection until proven good again
 * UNIX version
 */
static void invalidatePixels()
{
        // disconnect from tft thread
        tft.setEarthPix (NULL, NULL);

        // unmap pixel arrays
        if (day_pixels) {
            munmap (day_pixels, day_fbytes);
            day_pixels = NULL;
        }
        if (night_pixels) {
            munmap (night_pixels, day_fbytes);
            night_pixels = NULL;
        }
}

/* prepare open day_file and night_file for pixel access.
 * return whether ok
 * UNIX version
 */
static bool installFilePixels (const char *dfile, const char *nfile)
{
        bool ok = false;

        // mmap pixels if both files are open
        if (day_file && night_file) {

            day_fbytes = BHDRSZ + HC_MAP_W*HC_MAP_H*2;          // n bytes of 16 bit RGB565 pixels
            night_fbytes = BHDRSZ + HC_MAP_W*HC_MAP_H*2;
            day_pixels = (char *)                               // allow OS to choose addrs
                    mmap (NULL, day_fbytes, PROT_READ, MAP_FILE|MAP_PRIVATE, day_file.fileno(), 0);
            night_pixels = (char *)
                    mmap (NULL, night_fbytes, PROT_READ, MAP_FILE|MAP_PRIVATE, night_file.fileno(), 0);

            ok = day_pixels != MAP_FAILED && night_pixels != MAP_FAILED;
        }

        // install pixels if ok
        if (ok) {

            // Serial.println (F("both mmaps good"));

            // don't need files open once mmap has been established
            day_file.close();
            night_file.close();;

            // install in tft at start of pixels
            tft.setEarthPix (day_pixels+BHDRSZ, night_pixels+BHDRSZ);

        } else {

            // no go -- clean up

            if (day_file)
                day_file.close();
            else
                Serial.printf (_FX("%s not open\n"), dfile);
            if (day_pixels == MAP_FAILED)
                Serial.printf ("%s mmap failed: %s\n", dfile, strerror(errno));
            else if (day_pixels)
                munmap (day_pixels, day_fbytes);
            day_pixels = NULL;

            if (night_file)
                night_file.close();
            else
                Serial.printf (_FX("%s not open\n"), nfile);
            if (night_pixels == MAP_FAILED)
                Serial.printf ("%s mmap failed: %s\n", nfile, strerror(errno));
            else if (night_pixels)
                munmap (night_pixels, night_fbytes);
            night_pixels = NULL;

        }

        printFreeHeap (F("installFilePixels"));

        return (ok);
}

#endif // _IS_ESP8266



/***********************************************************************************************
 *
 * remaining functions are common to both architectures
 *
 ***********************************************************************************************/



/* don't assume we can access unaligned 32 bit values
 */
static uint32_t unpackLE4 (char *buf)
{
        union {
            uint32_t le4;
            char a[4];
        } le4;

        le4.a[0] = buf[0];
        le4.a[1] = buf[1];
        le4.a[2] = buf[2];
        le4.a[3] = buf[3];

        return (le4.le4);
}

/* return whether the given header is the correct BMP format and the total expected file size.
 */
static bool bmpHdrOk (char *buf, uint32_t w, uint32_t h, uint32_t *filesizep)
{
        if (buf[0] != 'B' || buf[1] != 'M') {
            Serial.printf (_FX("Hdr err: 0x%02X 0x%02X\n"), (unsigned)buf[0], (unsigned)buf[1]);
            return (false);
        }

        *filesizep = unpackLE4(buf+2);
        uint32_t type = unpackLE4(buf+14);
        uint32_t nrows = - (int32_t)unpackLE4(buf+22);          // nrows<0 means display upside down
        uint32_t ncols = unpackLE4(buf+18);
        uint32_t pixbytes = unpackLE4(buf+34);

        if (pixbytes != nrows*ncols*BPERBMPPIX || type != HDRVER || w != ncols || h != nrows) {
            Serial.printf (_FX("Hdr err: %d %d %d %d\n"), pixbytes, type, nrows, ncols);
            return (false);
        }

        return (true);
}


/* marshall the day and night file names and titles for the given style.
 * N.B. we do not check for suffient room in the arrays
 * N.B. DRAP name adds -S for no-scale version as of HamClock V2.67
 */
static void buildMapNames (const char *style, char *dfile, char *nfile, char *dtitle, char *ntitle)
{
        if (strcmp (style, "DRAP") == 0) {
            sprintf (dfile, "/map-D-%dx%d-%s-S.bmp", HC_MAP_W, HC_MAP_H, style);
            sprintf (nfile, "/map-N-%dx%d-%s-S.bmp", HC_MAP_W, HC_MAP_H, style);
        } else {
            sprintf (dfile, "/map-D-%dx%d-%s.bmp", HC_MAP_W, HC_MAP_H, style);
            sprintf (nfile, "/map-N-%dx%d-%s.bmp", HC_MAP_W, HC_MAP_H, style);
        }

        sprintf (dtitle, _FX("%s D map"), style);
        sprintf (ntitle, _FX("%s N map"), style);
}

/* qsort-style compare two FS_Info by name
 */
static int FSInfoNameQsort (const void *p1, const void *p2)
{
        return (strcmp (((FS_Info *)p1)->name, ((FS_Info *)p2)->name));
}


/* rather like tftMsg but also shows message over map_b unless verbose
 */
static void mapMsg (bool verbose, const char *fmt, ...)
{
    // format msg
    va_list ap;
    va_start(ap, fmt);
    char msg[200];
    vsnprintf (msg, sizeof(msg), fmt, ap);
    va_end(ap);

    tftMsg (verbose, 0, "%s", msg);

    if (!verbose) {
        selectFontStyle (LIGHT_FONT, SMALL_FONT);
        tft.setTextColor (RA8875_WHITE);
        size_t msg_l = getTextWidth(msg);
        tft.fillRect (map_b.x + map_b.w/5, map_b.y+map_b.h/3, 3*map_b.w/5, 40, RA8875_BLACK);
        tft.setCursor (map_b.x + (map_b.w-msg_l)/2, map_b.y+map_b.h/3+30);
        tft.print(msg);
    }
}


/* download the given file of expected size and load into LittleFS.
 * client is already postioned at first byte of image.
 * if verbose show messages on setup screen, else overlay map_b.
 */
static bool downloadMapFile (bool verbose, WiFiClient &client, const char *file, const char *title)
{
        resetWatchdog();

        // set if all ok
        bool ok = false;

        // alloc copy buffer
        #define COPY_BUF_SIZE 1024                      // > BHDRSZ but beware RAM pressure
        const uint32_t npixbytes = HC_MAP_W*HC_MAP_H*BPERBMPPIX;
        uint32_t nbufbytes = 0;
        StackMalloc buf_mem(COPY_BUF_SIZE);
        char *copy_buf = (char *) buf_mem.getMem();

        // (re)create file
        // extra open/close/remove avoids LitteLFS duplicate COW behavior
        File f = LittleFS.open (file, "r");
        if (f) {
            f.close();
            LittleFS.remove(file);
        }
        f = LittleFS.open (file, "w");
        if (!f) {
            #if defined(_IS_ESP8266)
                // using fatalError would probably leave user stranded in what is likely a persistent err
                mapMsg (verbose, _FX("%s: create failed\r"), title);
                return (false);
            #else
                // use non-standard File members for richer error msg
                fatalError ("Error creating required file:\n%s\n%s", f.fpath.c_str(), f.errstr.c_str());
                // never returns
            #endif
        }

        // read and check remote header
        for (int i = 0; i < BHDRSZ; i++) {
            if (!getChar (client, &copy_buf[i])) {
                Serial.printf (_FX("short header: %.*s\n"), i, copy_buf); // might be err message
                mapMsg (verbose, _FX("%s: header is short\r"), title);
                goto out;
            }
        }
        uint32_t filesize;
        if (!bmpHdrOk (copy_buf, HC_MAP_W, HC_MAP_H, &filesize)) {
            Serial.printf (_FX("bad header: %.*s\n"), BHDRSZ, copy_buf); // might be err message
            mapMsg (verbose, _FX("%s: bad header\r"), title);
            goto out;
        }
        if (filesize != npixbytes + BHDRSZ) {
            Serial.printf (_FX("%s: wrong size %u != %u\n"), title, filesize, npixbytes);
            mapMsg (verbose, _FX("%s: wrong size\r"), title);
            goto out;
        }

        // write header
        f.write (copy_buf, BHDRSZ);
        updateClocks(false);

        // copy pixels
        mapMsg (verbose, _FX("%s: downloading\r"), title);
        for (uint32_t nbytescopy = 0; nbytescopy < npixbytes; nbytescopy++) {
            resetWatchdog();

            if (((nbytescopy%(npixbytes/10)) == 0) || nbytescopy == npixbytes-1)
                mapMsg (verbose, _FX("%s: %3d%%\r"), title, 100*(nbytescopy+1)/npixbytes);

            // read more
            if (nbufbytes < COPY_BUF_SIZE && !getChar (client, &copy_buf[nbufbytes++])) {
                Serial.printf (_FX("%s: file is short: %u %u\n"), title, nbytescopy, npixbytes);
                mapMsg (verbose, _FX("%s: file is short\r"), title);
                goto out;
            }

            // write when copy_buf is full or last
            if (nbufbytes == COPY_BUF_SIZE || nbytescopy == npixbytes-1) {
                updateClocks(false);
                if (f.write (copy_buf, nbufbytes) != nbufbytes) {
                    mapMsg (verbose, _FX("%s: write failed\r"), title);
                    goto out;
                }
                nbufbytes = 0;
            }
        }

        // if get here, it worked!
        ok = true;

    out:

        f.close();
        if (!ok)
            LittleFS.remove (file);

        printFreeHeap (F("_downloadMapFile"));
        return (ok);
}


/* open the given file and confirm its size, downloading fresh if not found, no match or newer.
 * if successful return:
 *   with position offset at first pixel,
 *   indicate whether a file was downloaded,
 *   open LittleFS File
 * else return a closed File
 */
static File openMapFile (bool verbose, bool *downloaded, const char *file, const char *title)
{
        resetWatchdog();

        // assume no download yet
        *downloaded = false;

        // putting all variables up here avoids pendantic goto warnings
        File f;
        WiFiClient client;
        uint32_t filesize;
        uint32_t local_time = 0;
        uint32_t remote_time = 0;
        char hdr_buf[BHDRSZ];
        int nr = 0;
        bool file_ok = false;

        Serial.printf (_FX("%s: %s\n"), title, file);
        tftMsg (verbose, 500, _FX("%s: checking\r"), title);

        // start remote file download, even if only to check whether newer
        if (wifiOk() && client.connect(svr_host, HTTPPORT)) {
            snprintf (hdr_buf, sizeof(hdr_buf), _FX("/ham/HamClock/maps/%s"), file);
            httpGET (client, svr_host, hdr_buf);
            if (!httpSkipHeader (client, &remote_time) || remote_time == 0) {
                tftMsg (verbose, 1000, _FX("%s: err - try local\r"), title);
                client.stop();
            }
            Serial.printf (_FX("%s: %d remote_time\n"), title, remote_time);
        }
        
        // even if no net connection, still try using local file if available

        // open local file
        f = LittleFS.open (file, "r");
        if (!f) {
            tftMsg (verbose, 1000, _FX("%s: not local\r"), title);
            goto out;
        }

        // file is "bad" if remote is newer than flash
        local_time = f.getCreationTime();
        Serial.printf (_FX("%s: %d local_time\n"), title, local_time);
        if (client.connected() && remote_time > local_time) {
            tftMsg (verbose, 1000, _FX("%s: found newer map\r"), title);
            goto out;
        }

        // read local file header
        nr = f.read ((uint8_t*)hdr_buf, BHDRSZ);
        if (nr != BHDRSZ) {
            tftMsg (verbose, 1000, _FX("%s: read err\r"), title);
            goto out;
        }

        // check flash file type and size
        if (!bmpHdrOk (hdr_buf, HC_MAP_W, HC_MAP_H, &filesize)) {
            tftMsg (verbose, 1000, _FX("%s: bad format\r"), title);
            goto out;
        }
        if (filesize != f.size()) {
            tftMsg (verbose, 1000, _FX("%s: wrong size\r"), title);
            goto out;
        }

        // all good
        file_ok = true;

    out:

        // download if not ok for any reason but remote connection is ok
        if (!file_ok && client.connected()) {

            if (f) {
                // file exists but is not correct in some way
                f.close();
                LittleFS.remove(file);
            }

            // insure room
            cleanFLASH (title, 1);

            // download and open again if success
            if (downloadMapFile (verbose, client, file, title)) {
                *downloaded = true;
                f = LittleFS.open (file, "r");
            }
        }

        // leave error message up if not ok
        if (f)
            tftMsg (verbose, 0, _FX("%s: good\r"), title);
        tftMsg(verbose, 0, NULL);   // next row

        // finished with remote connection
        client.stop();

        // return result, open if good or closed if not
        printFreeHeap (F("_openMapFile"));
        return (f);
}

/* install maps for the given style, use/establish default, download if absent locally newer on server.
 * if verbose then update display with tftMsg, else just log.
 * return whether all ok and, if so, whether a new map was downloaded if care.
 */
bool installBackgroundMaps (bool verbose, CoreMaps cm, bool *downloaded)
{
        resetWatchdog();

        // set desired map, or use or establish default
        if (cm == CM_NONE) {
            cm = core_map;
            if (cm == CM_NONE) {
                char s[NV_MAPSTYLE_LEN];
                if (NVReadString (NV_MAPSTYLE, s)) {
                    for (int i = 0; i < CM_N; i++) {
                        if (strcmp (map_styles[i], s) == 0) {
                            cm = (CoreMaps)i;
                            break;
                        }
                    }
                    if (cm == CM_NONE)
                        fatalError (_FX("Bug! bogus default map style: %s"), s);
                } else {
                    NVWriteString (NV_MAPSTYLE, map_styles[CM_TERRAIN]);
                    cm = CM_TERRAIN;
                }
            }
        }
        const char *style = map_styles[cm];
        NVWriteString (NV_MAPSTYLE, style);
        core_map = cm;

        // create names and titles
        char dfile[LFS_NAME_MAX];
        char nfile[LFS_NAME_MAX];
        char dtitle[NV_MAPSTYLE_LEN+10];
        char ntitle[NV_MAPSTYLE_LEN+10];
        buildMapNames (style, dfile, nfile, dtitle, ntitle);

        // close any previous
        invalidatePixels();
        if (day_file)
            day_file.close();
        if (night_file)
            night_file.close();

        // open each file, downloading if newer or not found locally
        bool dd = false, nd = false;
        day_file = openMapFile (verbose, &dd, dfile, dtitle);
        night_file = openMapFile (verbose, &nd, nfile, ntitle);

        // install pixels and indicate whether either file was downloaded
        if (installFilePixels (dfile, nfile)) {
            if (dd || nd)
                Serial.printf (_FX("%s: fresh download\n"), dtitle);
            if (downloaded)
                *downloaded = dd || nd;
            return (true);
        } else
            return (false);
}

/* install new core map style, insure voa off and BC shows it's off and schedule next
 * N.B. does NOT call initEarthMap
 * return whether ok
 */
bool installNewMapStyle (CoreMaps new_cm)
{
        prop_map = PROP_MAP_OFF;
        PlotPane bc_pp = findPaneChoiceNow (PLOT_CH_BC);
        if (bc_pp != PANE_NONE)
            plotBandConditions (plot_b[bc_pp], 0, NULL, NULL);
        if (installBackgroundMaps (false, new_cm, NULL)) {
            newCoreMap (new_cm);
            return (true);
        } else
            return (false);
}

/* query VOACAP for current time and given band, retrieve and install maps.
 * return whether ok
 */
bool installPropMaps (float MHz)
{
        static char prop_page[] = "/ham/HamClock/fetchVOACAPArea.pl";

        resetWatchdog();

        // get clock time
        time_t t = nowWO();
        int yr = year(t);
        int mo = month(t);
        int hr = hour(t);

        // prepare query
        #define DEF_TOA 3.0
        static char qfmt[] = 
     "%s?YEAR=%d&MONTH=%d&UTC=%d&TXLAT=%.3f&TXLNG=%.3f&PATH=%d&WATTS=%d&WIDTH=%d&HEIGHT=%d&MHZ=%.2f&TOA=%.1f";
        StackMalloc query_mem(300);
        char *query = (char *) query_mem.getMem();
        snprintf (query, query_mem.getSize(), qfmt,
            prop_page, yr, mo, hr, de_ll.lat_d, de_ll.lng_d, show_lp, bc_power, HC_MAP_W, HC_MAP_H,
            MHz, DEF_TOA);

        Serial.printf ("PropMap query: %s\n", query);

        // assign a style and compose names and titles
        static const char style[] = "PropMap";
        char dfile[32];                 // match LFS_NAME_MAX
        char nfile[32];
        char dtitle[NV_MAPSTYLE_LEN+10];
        char ntitle[NV_MAPSTYLE_LEN+10];
        buildMapNames (style, dfile, nfile, dtitle, ntitle);

        // insure fresh start
        cleanFLASH (dtitle, 2);
        invalidatePixels();

        // download new voacap maps
        updateClocks(false);
        WiFiClient client;
        bool ok = false;
        if (wifiOk() && client.connect(svr_host, HTTPPORT)) {
            httpGET (client, svr_host, query);
            ok = httpSkipHeader (client) && downloadMapFile (false, client, dfile, dtitle)
                                         && downloadMapFile (false, client, nfile, ntitle);
            client.stop();
        }

        // install if ok
        if (ok) {
            day_file = LittleFS.open (dfile, "r");
            night_file = LittleFS.open (nfile, "r");
            ok = installFilePixels (dfile, nfile);
        }

        if (!ok)
            Serial.printf (_FX("%s: fail\n"), style);

        return (ok);
}

/* produce a listing of the map storage directory.
 * N.B. return malloced array and malloced name -- caller must free()
 */
FS_Info *getConfigDirInfo (int *n_info, char **fs_name, uint64_t *fs_size, uint64_t *fs_used)
{
        // get basic fs info
        FSInfo fs_info;
        LittleFS.info(fs_info);

        // pass back basic info
        *fs_name = strdup ("HamClock file system");
        *fs_size = fs_info.totalBytes;
        *fs_used = fs_info.usedBytes;

        // build each entry
        FS_Info *fs_array = NULL;
        int n_fs = 0;
        Dir dir = LittleFS.openDir("/");
        while (dir.next()) {

            // extend array
            fs_array = (FS_Info *) realloc (fs_array, (n_fs+1)*sizeof(FS_Info));
            FS_Info *fip = &fs_array[n_fs++];

            // store name
            strncpy (fip->name, dir.fileName().c_str(), sizeof(fip->name)-1);
            fip->name[sizeof(fip->name)-1] = 0;

            // store time
            time_t t = dir.fileCreationTime();
            fip->t0 = t;

            // as handy date string too
            int yr = year(t);
            int mo = month(t);
            int dy = day(t);
            int hr = hour(t);
            int mn = minute(t);
            int sc = second(t);
            snprintf (fip->date, sizeof(fip->date), "%04d-%02d-%02dT%02d:%02d:%02dZ", yr, mo, dy, hr, mn, sc);

            // store length
            fip->len = dir.fileSize();
        }
        // Dir has no close method, hope destructor cleans up

        // nice sorted order
        qsort (fs_array, n_fs, sizeof(FS_Info), FSInfoNameQsort);

        // ok
        *n_info = n_fs;
        return (fs_array);
}

/* return the current _effective_ map style, meaning core style unless showing a prop map.
 * N.B. only for reporting purposes, not for accessing NV_MAPSTYLE.
 * N.B. s[] assumed to be at least NV_MAPSTYLE_LEN
 */
const char *getMapStyle (char s[])
{
        if (prop_map == PROP_MAP_OFF)
            NVReadString (NV_MAPSTYLE, s);
        else
            snprintf (s, NV_MAPSTYLE_LEN, "%dm/%dW", propMap2Band(prop_map), bc_power);

        return (s);
}

/* return MHz for each PropMapSetting.
 * match column headings in voacapx.out
 */
float propMap2MHz (PropMapSetting pms)
{
        switch (pms) {
        case PROP_MAP_80M: return ( 3.6);
        case PROP_MAP_40M: return ( 7.1);
        case PROP_MAP_30M: return (10.1);
        case PROP_MAP_20M: return (14.1);
        case PROP_MAP_17M: return (18.1);
        case PROP_MAP_15M: return (21.1);
        case PROP_MAP_12M: return (24.9);
        case PROP_MAP_10M: return (28.2);
        default: fatalError (_FX("Bug! bad MHz PMS %d"), pms); return (0);
        }
}

/* return band for each PropMapSetting
 */
int propMap2Band (PropMapSetting pms)
{
        switch (pms) {
        case PROP_MAP_80M: return (80);
        case PROP_MAP_40M: return (40);
        case PROP_MAP_30M: return (30);
        case PROP_MAP_20M: return (20);
        case PROP_MAP_17M: return (17);
        case PROP_MAP_15M: return (15);
        case PROP_MAP_12M: return (12);
        case PROP_MAP_10M: return (10);
        default: fatalError (_FX("Bug! bad Band PMS %d"), pms); return (0);
        }
}

/* draw the DRAP frequency scale at drap_b.
 * N.B. we move drap_b depending on rss_on
 */
void drawDRAPScale()
{
    // color scale to match NOAA palette
    typedef struct {
        uint8_t mhz;
        uint32_t color;
    } DRAPScale;
    static DRAPScale scale[] = {
        {0,  0x000000},
        {4,  0x4E138A},
        {9,  0x001EF5},
        {15, 0x78FBD6},
        {20, 0x78FA4D},
        {27, 0xFEFD54},
        {30, 0xEC6F2D},
        {35, 0xE93323},
    };

    resetWatchdog();

    // geometry setup
    #define _DRS_N_SCALE NARRAY(scale)                                  // n scale entries
    #define _DRS_X0     drap_b.x                                        // left x
    #define _DRS_X1     (drap_b.x + drap_b.w)                           // right x
    #define _DRS_DX     (_DRS_X1-_DRS_X0)                               // width
    #define _DRS_MIN    scale[0].mhz                                    // min freq
    #define _DRS_MAX    scale[_DRS_N_SCALE-1].mhz                       // max freq
    #define _DRS_DM     (_DRS_MAX-_DRS_MIN)                             // freq span
    #define _DRS_M2X(m) (_DRS_X0 + _DRS_DX*((m)-_DRS_MIN)/_DRS_DM)      // convert MHz to x
    #define _DRS_NL     (_DRS_DM/5U)                                    // n labels
    #define _DRS_PRY    (drap_b.y+5U)                                   // label y

    // set drap_b.y above RSS if on else at the bottom
    drap_b.y = rss_on ? rss_bnr_b.y - drap_b.h: map_b.y + map_b.h - drap_b.h;

    // draw smoothly-interpolated color scale
    for (unsigned i = 1; i < _DRS_N_SCALE; i++) {
        uint8_t dm = scale[i].mhz - scale[i-1].mhz;
        uint8_t r0 = scale[i-1].color >> 16;
        uint8_t g0 = (scale[i-1].color >> 8) & 0xFF;
        uint8_t b0 = scale[i-1].color & 0xFF;
        uint8_t r1 = scale[i].color >> 16;
        uint8_t g1 = (scale[i].color >> 8) & 0xFF;
        uint8_t b1 = scale[i].color & 0xFF;
        for (uint16_t x = _DRS_M2X(scale[i-1].mhz); x <= _DRS_M2X(scale[i].mhz); x++) {
            if (x < drap_b.x + drap_b.w) {
                // macros can overflow slightlty
                float mhz = (float)_DRS_DM*(x - _DRS_X0)/_DRS_DX;
                float frac = fminf(fmaxf((mhz - scale[i-1].mhz)/dm,0),1);
                uint16_t new_c = RGB565(r0+frac*(r1-r0), g0+frac*(g1-g0), b0+frac*(b1-b0));
                tft.drawLine (x, drap_b.y, x, drap_b.y+drap_b.h-1, 1, new_c);
            }
        }
    }

    // draw labels inside drap_b
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    for (unsigned i = 0; i <= _DRS_NL; i++) {
        // evenly spaced but keep ends just inside drap_b
        uint16_t x = i == 0 ? _DRS_X0+2 : (i == _DRS_NL ? _DRS_X1-12 : _DRS_X0 + _DRS_DX*i/_DRS_NL - 7);
        tft.setCursor (x, _DRS_PRY);
        tft.setTextColor (i < _DRS_NL/2 ? RA8875_WHITE : RA8875_BLACK);
        int mhz_print = _DRS_MIN + _DRS_DM*i/_DRS_NL;
        tft.print (mhz_print);
    }
    tft.setTextColor (RA8875_WHITE);
    tft.setCursor (_DRS_X0 + _DRS_DX/(2*_DRS_NL)-10, _DRS_PRY);
    tft.print ("MHz");
}


/* erase the DRAP scale in drap_b.
 * N.B. beware globals being temporarily changed -- see comments
 */
void eraseDRAPScale ()
{
    resetWatchdog();

    // save then move drap_b off the map so drawMapCoord doesn't skip it
    SBox db = drap_b;
    drap_b.y = 0;

    // save whether rss is on too because it is skipped also
    uint8_t rs = rss_on;
    rss_on = false;

    // erase entire scale if azm mode because redrawing the map will miss the center and corners
    if (azm_on)
        tft.fillRect (db.x, db.y, db.w, db.h, RA8875_BLACK);

    // restore map
    for (uint16_t y = db.y; y < db.y+db.h; y++) {
        for (uint16_t x = db.x; x < db.x+db.w; x++)
            drawMapCoord (x, y);
        drawSatPointsOnRow (y);
    }

    // restore
    drap_b = db;
    rss_on = rs;
}
