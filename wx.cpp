/* look up current weather
 */


#include "HamClock.h"

static const char wx_base[] = "/ham/HamClock/wx.pl";

/* look up current weather info for the given location.
 * if wip is filled ok return true, else return false with short reason in ynot[]
 */
bool getCurrentWX (const LatLong &ll, bool is_de, WXInfo *wip, char ynot[])
{
    WiFiClient wx_client;
    char line[100];

    bool ok = false;

    resetWatchdog();

    // get
    if (wifiOk() && wx_client.connect(svr_host, HTTPPORT)) {
        updateClocks(false);
        resetWatchdog();

        // query web page
        snprintf (line, sizeof(line), _FX("%s?is_de=%d&lat=%g&lng=%g"), wx_base, is_de, ll.lat_d, ll.lng_d);
        Serial.println (line);
        httpGET (wx_client, svr_host, line);

        // skip response header
        if (!httpSkipHeader (wx_client)) {
            strcpy_P (ynot, PSTR("WX timeout"));
            goto out;
        }

        // init response 
        memset (wip, 0, sizeof(*wip));

        // crack response
        uint8_t n_found = 0;
        while (n_found < N_WXINFO_FIELDS && getTCPLine (wx_client, line, sizeof(line), NULL)) {
            // Serial.printf (_FX("WX: %s\n"), line);
            updateClocks(false);

            // check for error message in which case abandon further search
            if (sscanf (line, "error=%[^\n]", ynot) == 1)
                goto out;

            // find start of data value after =
            char *vstart = strchr (line, '=');
            if (!vstart)
                continue;
            vstart++;

            // check for content line
            if (strncmp_P (line, PSTR("city="), 5) == 0) {
                strncpy (wip->city, vstart, sizeof(wip->city)-1);
                n_found++;
            } else if (strncmp_P (line, PSTR("temperature_c="), 14) == 0) {
                wip->temperature_c = atof (vstart);
                n_found++;
            } else if (strncmp_P (line, PSTR("humidity_percent="), 17) == 0) {
                wip->humidity_percent = atof (vstart);
                n_found++;
            } else if (strncmp_P (line, PSTR("wind_speed_mps="), 15) == 0) {
                wip->wind_speed_mps = atof (vstart);
                n_found++;
            } else if (strncmp_P (line, PSTR("wind_dir_name="), 14) == 0) {
                strncpy (wip->wind_dir_name, vstart, sizeof(wip->wind_dir_name)-1);
                n_found++;
            } else if (strncmp_P (line, PSTR("clouds="), 7) == 0) {
                strncpy (wip->clouds, vstart, sizeof(wip->clouds)-1);
                n_found++;
            } else if (strncmp_P (line, PSTR("conditions="), 11) == 0) {
                strncpy (wip->conditions, vstart, sizeof(wip->conditions)-1);
                n_found++;
            } else if (strncmp_P (line, PSTR("attribution="), 12) == 0) {
                strncpy (wip->attribution, vstart, sizeof(wip->attribution)-1);
                n_found++;
            }

            // Serial.printf (_FX("WX %d: %s\n"), n_found, line);
        }

        if (n_found < N_WXINFO_FIELDS) {
            strcpy_P (ynot, PSTR("No WX data"));
            goto out;
        }

        // ok!
        ok = true;

    } else {

        strcpy_P (ynot, PSTR("WX connection failed"));

    }



    // clean up
out:
    wx_client.stop();
    resetWatchdog();
    printFreeHeap (F("getCurrentWX"));
    return (ok);
}

/* display current DE weather in the given box.
 * this is used by updateWiFi() for persistent display, use showDEWX() for transient display
 */
bool updateDEWX (const SBox &box)
{
    char ynot[32];
    WXInfo wi;

    bool ok = getCurrentWX (de_ll, true, &wi, ynot);
    if (ok)
        plotWX (box, DE_COLOR, wi);
    else
        plotMessage (box, DE_COLOR, ynot);

    return (ok);
}

/* display current DX weather in the given box.
 * this is used by updateWiFi() for persistent display, use showDXWX() for transient display
 */
bool updateDXWX (const SBox &box)
{
    char ynot[32];
    WXInfo wi;

    bool ok = getCurrentWX (dx_ll, false, &wi, ynot);
    if (ok)
        plotWX (box, DX_COLOR, wi);
    else
        plotMessage (box, DX_COLOR, ynot);

    return (ok);
}
