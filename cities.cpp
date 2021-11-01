/* manage list of cities.
 * sparse 2d table contains largest city in each region.
 */

#include "HamClock.h"

// linux only

#if defined(_IS_UNIX)

// one city entry
typedef struct {
    const char *name;           // malloced name
    float lat, lng;             // degs +N +E
    int16_t lngbin;             // deg bin
} City;

// one row descriptor
typedef struct {
    City *lngs;                 // malloc array of City sorted by increasing lng
    int n_lngs;                 // number of Cities
} LatRow;

// rows
static int LAT_SIZ, LNG_SIZ;    // region size
static LatRow *latrows;         // malloced list of 180/LAT_SIZ rows

// name of server file containing cities
static const char cities_fn[] = "/ham/HamClock/cities.txt";

/* qsort-style function to compare a pair of pointers to City by lngbin
 */
static int cityQS (const void *p1, const void *p2)
{
    return (((City*)p1)->lngbin - ((City*)p2)->lngbin);
}


/* query for list of cities, fill regions.
 * harmless if called more than once.
 * N.B. UNIX only.
 */
void readCities()
{

        // ignore if already done
        if (latrows)
            return;

        // connection
        WiFiClient cities_client;

        Serial.println (cities_fn);
        resetWatchdog();
        if (wifiOk() && cities_client.connect (svr_host, HTTPPORT)) {

            // stay current
            updateClocks(false);
            resetWatchdog();

            // send query
            httpGET (cities_client, svr_host, cities_fn);

            // skip http header
            if (!httpSkipHeader (cities_client)) {
                Serial.print (F("Cities: bad header\n"));
                goto out;
            }

            // first line is binning sizes
            char line[100];
            if (!getTCPLine (cities_client, line, sizeof(line), NULL)) {
                Serial.print (F("Cities: no bin line\n"));
                goto out;
            }
            if (sscanf (line, "%d %d", &LAT_SIZ, &LNG_SIZ) != 2) {
                Serial.printf (_FX("Cities: bad bin line: %s\n"), line);
                goto out;
            }

            // create row array
            latrows = (LatRow *) calloc (180/LAT_SIZ, sizeof(LatRow));
            if (!latrows) {
                Serial.print (F("Cities: no latrows memory\n"));
                goto out;
            }

            // read each city
            int n_cities = 0;
            while (getTCPLine (cities_client, line, sizeof(line), NULL)) {

                // crack info
                float rlat, rlng;
                if (sscanf (line, "%f, %f", &rlat, &rlng) != 2)
                    continue;
                int latbin = LAT_SIZ*floorf(rlat/LAT_SIZ);
                int lngbin = LAT_SIZ*floorf(rlng/LNG_SIZ);
                if (latbin<-90 || latbin>=90 || lngbin<-180 || lngbin>=180)
                    continue;
                char *city_start = strchr (line, '"');
                if (!city_start)
                    continue;
                city_start += 1;
                char *city_end = strchr (city_start, '"');
                if (!city_end)
                    continue;
                *city_end = '\0';

                // append to appropriate latrow, sort later
                LatRow *lrp = &latrows[(latbin+90)/LAT_SIZ];
                lrp->lngs = (City *) realloc (lrp->lngs, (lrp->n_lngs+1) * sizeof(City));
                City *cp = &lrp->lngs[lrp->n_lngs++];
                cp->name = strdup (city_start);
                cp->lngbin = lngbin;
                cp->lat = rlat;
                cp->lng = rlng;

                // good
                n_cities++;

            }
            Serial.printf (_FX("Cities: found %d\n"), n_cities);

            // sort each row
            for (int i = 0; i < 180/LAT_SIZ; i++)
                qsort (latrows[i].lngs, latrows[i].n_lngs, sizeof(City), cityQS);
        }

    out:
        cities_client.stop();

}

/* return name of city and location near the given ll, else NULL.
 */
const char *getNearestCity (const LatLong &ll, LatLong &city_ll)
{

        // ignore if not ready
        if (!latrows)
            return (NULL);

        // decide row based on latitude bin, not all have entries
        LatRow *lrp = &latrows[(int)((ll.lat_d+90)/LAT_SIZ)];
        if (!lrp->lngs)
            return (NULL);

        // binary search this row for matching lng bin
        City key;
        key.lngbin = LNG_SIZ*(int)floorf(ll.lng_d/LNG_SIZ);
        City *found = (City *) bsearch (&key, lrp->lngs, lrp->n_lngs, sizeof(City), cityQS);

        // report results if successful
        if (found) {
            city_ll.lat_d = found->lat;
            city_ll.lng_d = found->lng;
            normalizeLL (city_ll);
            return (found->name);
        } else {
            return (NULL);
        }

}

#else

// dummies

const char *getNearestCity (const LatLong &ll, LatLong &city_ll) {
    (void) ll;
    (void) city_ll;
    return NULL;
}

void readCities() {}

#endif // _IS_UNIX
