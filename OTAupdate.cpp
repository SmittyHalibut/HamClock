/* handle remote firmware updating
 */

#include <ESP8266httpUpdate.h>
#include <WiFiUdp.h>


#include "HamClock.h"

// server path to script that returns the newest version available
static const char v_page[] = "/ham/HamClock/version.pl";


#define ASK_TO          60000U                          // ask timeout, millis()
#define BOX_W           120                             // box width
#define BOX_H           40                              // box height
#define INDENT          20                              // indent
#define Q_Y             40                              // question y
#define LH              30                              // line height
#define BOX_Y           (Q_Y+LH)                        // yes/no boxes y
#define INFO_Y          (BOX_Y+2*LH)                    // first info y

#define PBAR_INDENT     30                              // left and right indent
#define PBAR_H          30                              // progress bar height
#define PBAR_W          (tft.width()-2*PBAR_INDENT)     // progress bar width
#define FLASHBPS        60000                           // approx flash rate, b/s

static uint16_t pbar_x0, pbar_y0;                       // lower left of progress bar


#if defined(_IS_ESP8266)

/* called by ESPhttpUpdate during download with bytes so far and total.
 */
static void onProgressCB (int sofar, int total)
{
    if (sofar > 49*total/50) {
        // report estimate for overwrite program -- can't run then!
        tft.setCursor (pbar_x0+PBAR_W/2, pbar_y0-3);
        tft.printf (_FX(" ... about %d more seconds ... "), total/FLASHBPS);
    } else {
        // progressively fill to half
        tft.drawRect (pbar_x0, pbar_y0-PBAR_H, PBAR_W, PBAR_H, RA8875_WHITE);
        int w = sofar*PBAR_W/total/2;
        if (w > 0)              // avoid 0-width bug
            tft.fillRect (pbar_x0, pbar_y0-PBAR_H, w, PBAR_H, RA8875_WHITE);
    }
}

#else

/* called by ESPhttpUpdate during download with bytes so far and total.
 */
static void onProgressCB (int sofar, int total)
{
    tft.drawRect (pbar_x0, pbar_y0-PBAR_H, PBAR_W, PBAR_H, RA8875_WHITE);
    tft.fillRect (pbar_x0, pbar_y0-PBAR_H, sofar*PBAR_W/total, PBAR_H, RA8875_WHITE);
}

#endif




/* return whether a new version is available.
 * if so pass back the name in new_ver[new_verl]
 * default no if error.
 */
bool newVersionIsAvailable (char *new_ver, uint16_t new_verl)
{
    WiFiClient v_client;
    char line[100];
    bool found_newer = false;

    Serial.print (svr_host); Serial.println (v_page);
    if (wifiOk() && v_client.connect (svr_host, HTTPPORT)) {
        resetWatchdog();

        // query page
        httpGET (v_client, svr_host, v_page);

        // skip header
        if (!httpSkipHeader (v_client)) {
            Serial.println (F("Version query header is short"));
            goto out;
        }

        // next line is new version number
        if (!getTCPLine (v_client, line, sizeof(line), NULL)) {
            Serial.println (F("Version query timed out"));
            goto out;
        }

        // non-rc accepts newer non-rc; rc accepts newer or any rc
        Serial.printf (_FX("found version %s\n"), line);
        float this_v = atof(hc_version);
        float new_v = atof(line);
        bool this_rc = strstr (hc_version, "rc");
        bool new_rc = strstr (line, "rc");
        // Serial.printf ("V %g >? %g\n", new_v, this_v);
        if ((!this_rc && !new_rc && new_v > this_v) || (this_rc && (new_rc || new_v >= this_v))) {
            found_newer = true;
            strncpy (new_ver, line, new_verl);
        }

        // just log next few lines for debug
        for (int i = 0; i < 2 && getTCPLine (v_client, line, sizeof(line), NULL); i++)
            Serial.printf ("  %s\n", line);
    }

out:

    // finished with connection
    v_client.stop();

    return (found_newer);
}

/* ask and return whether to install the given (presumably newer) version.
 * default no if trouble of no user response.
 */
bool askOTAupdate(char *new_ver)
{
    // prep
    eraseScreen();
    hideClocks();
    selectFontStyle (BOLD_FONT, SMALL_FONT);
    tft.setTextColor (RA8875_WHITE);
    char line[128];

    // ask whether to install
    tft.setCursor (INDENT, Q_Y);
    sprintf (line, _FX("New version %s is available. Update now?  ... "), new_ver);
    tft.print (line);
    uint16_t count_x = tft.getCursorX();
    uint16_t count_y = tft.getCursorY();
    uint8_t count_s = ASK_TO/1000U;
    tft.print(count_s);

    // draw yes/no boxes
    SBox no_b =  {INDENT, BOX_Y, BOX_W, BOX_H};
    SBox yes_b = {(uint16_t)(tft.width()-INDENT-BOX_W), BOX_Y, BOX_W, BOX_H};
    drawStringInBox ("No", no_b, true, RA8875_WHITE);
    drawStringInBox ("Yes", yes_b, false, RA8875_WHITE);

    // prep for potentially long wait
    closeDXCluster();           // prevent inbound msgs from clogging network
    closeGimbal();              // avoid dangling connection

    // list changes
    WiFiClient v_client;
    uint16_t liney = INFO_Y+LH;
    selectFontStyle (LIGHT_FONT, SMALL_FONT);
    tft.setCursor (INDENT, liney);
    if (wifiOk() && v_client.connect (svr_host, HTTPPORT)) {
        resetWatchdog();

        // query page
        httpGET (v_client, svr_host, v_page);

        // skip header
        if (!httpSkipHeader (v_client)) {
            Serial.println (F("Info header is short"));
            goto out;
        }

        // skip next line which is new version number
        if (!getTCPLine (v_client, line, sizeof(line), NULL)) {
            Serial.println (F("Info timed out"));
            goto out;
        }

        // remaining lines are changes
        while (getTCPLine (v_client, line, sizeof(line), NULL)) {
            tft.setCursor (INDENT, liney);
            (void) maxStringW (line, tft.width()-2*INDENT);
            tft.print(line);
            if ((liney += LH) >= tft.height()-10)
                break;
        }
    }
  out:
    v_client.stop();

    // wait for response or time out
    selectFontStyle (BOLD_FONT, SMALL_FONT);
    drainTouch();
    uint32_t t0 = millis();
    Serial.println (F("Waiting for update y/n ..."));
    while (count_s > 0) {

        // update countdown
        wdDelay(100);
        if (timesUp(&t0,1000)) {
            tft.fillRect (count_x, count_y-30, 60, 40, RA8875_BLACK);
            tft.setCursor (count_x, count_y);
            tft.print(--count_s);
        }

        // check buttons
        SCoord s;
        if (readCalTouchWS(s) != TT_NONE) {
            if (inBox (s, yes_b)) {
                drawStringInBox ("Yes", yes_b, true, RA8875_WHITE);
                return (true);
            }
            if (inBox (s, no_b)) {
                drawStringInBox ("No", no_b, false, RA8875_WHITE);
                return (false);
            }
        }
    }

    // if get here we timed out
    return (false);
}

/* reload HamClock with the given version.
 * we never return regardless of success or fail.
 */
void doOTAupdate(const char *newver)
{
    Serial.println (F("Begin download"));

    // inform user
    eraseScreen();
    selectFontStyle (BOLD_FONT, SMALL_FONT);
    tft.setCursor (0, 100);
    tft.setTextColor (RA8875_WHITE);
    tft.println (F("Beginning remote update..."));
    tft.println (F("  Do not interrupt power or WiFi during this process."));
    tft.println();

    // save cursor as progress bar location
    pbar_x0 = tft.getCursorX() + PBAR_INDENT;
    pbar_y0 = tft.getCursorY();

    // connect progress callback
    ESPhttpUpdate.onProgress (onProgressCB);

    // build url
    resetWatchdog();
    WiFiClient client;
    char url[200];
  #if defined(_IS_ESP8266)
    if (strstr(hc_version, "rc") && strstr (newver, "rc"))
        snprintf (url, sizeof(url), _FX("http://%s/ham/HamClock/ESPHamClock-V%s.ino.bin"), svr_host, newver);
    else
        snprintf (url, sizeof(url), _FX("http://%s/ham/HamClock/ESPHamClock.ino.bin"), svr_host);
  #else
    if (strstr(hc_version, "rc") && strstr (newver, "rc"))
        snprintf (url, sizeof(url), _FX("http://%s/ham/HamClock/ESPHamClock-V%s.zip"), svr_host, newver);
    else
        snprintf (url, sizeof(url), _FX("https://%s/ham/HamClock/ESPHamClock.zip"), svr_host);
  #endif

    // go
    t_httpUpdate_return ret = ESPhttpUpdate.update(client, url);
    resetWatchdog();

    // show error message and exit
    switch (ret) {
    case HTTP_UPDATE_FAILED:
        fatalError (_FX("Update failed: Error %d\n%s\n"),
                        ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
        break;

    case HTTP_UPDATE_NO_UPDATES:
        fatalError (_FX("No updates found"));
        break;

    case HTTP_UPDATE_OK:
        fatalError (_FX("Update Ok??"));
        break;

    default:
        fatalError (_FX("Unknown failure code: "));
        tft.println (ret);
        break;
    }
}
