/* Get lat/long from gpsd daemon running on any host port 2947.
 *
 *   general info: https://gpsd.gitlab.io/gpsd/
 *   raw interface: https://gpsd.gitlab.io/gpsd/client-howto.html
 *   more info: https://gpsd.gitlab.io/gpsd/gpsd_json.html
 *
 * Simple server test, run this command:
 *   while true; do echo '"class":"TPV","mode":2,"lat":34.567,"lon":-123.456,"time":"2020-01-02T03:04:05.000Z"' | nc -l 192.168.7.11 2947; done
 */

#include "HamClock.h"

#define GPSD_PORT       2947                            // tcp port



/* look for time and sufficient mode in the given string from gpsd.
 * if found, save in arg (ptr to a time_t) and return true, else return false.
 */
static bool lookforTime (const char *buf, void *arg)
{
        // get time now so we can correct after we process and display
        uint32_t t0 = millis();

        // check for all required fields, might be more than one class
        // Serial.printf (_FX("\nGPSD: look for time in: %s\n"), buf);

        const char *classstr;
        bool found_tpv = false;
        for (const char *bp = buf; !found_tpv && (classstr = strstr (bp, "\"class\":\"")) != NULL;
                                                                        bp = classstr+9) {
            if (strncmp (classstr+9, "TPV", 3) == 0)
                found_tpv = true;
        }
        if (!found_tpv)
            return (false);

        const char *modestr = strstr (classstr, "\"mode\":");
        if (!modestr || atoi(modestr+7) < 2)
            return (false);

        const char *timestr = strstr (classstr, "\"time\":\"");
        if (!timestr || strlen(timestr) < 8+19)
            return(false);

        // crack time form: "time":"2012-04-05T15:00:01.501Z"
        int yr, mo, dy, hr, mn, sc;
        if (sscanf (timestr+8, _FX("%d-%d-%dT%d:%d:%d"), &yr, &mo, &dy, &hr, &mn, &sc) != 6)
            return (false);
        Serial.printf (_FX("GPSD: %04d-%02d-%02dT%02d:%02d:%02d\n"), yr, mo, dy, hr, mn, sc);

        // reformat to UNIX time
        tmElements_t tm;
        tm.Year = yr - 1970;
        tm.Month = mo;
        tm.Day = dy;
        tm.Hour = hr;
        tm.Minute = mn;
        tm.Second = sc;
        time_t gpsd_time = makeTime (tm);

        // correct for time spent here
        gpsd_time += (millis() - t0 + 500)/1000;

        // good
        *(time_t*)arg = gpsd_time;

        // success
        return (true);
}

/* look for lat and lon and sufficient mode in the given string from gpsd.
 * if found, save in arg (ptr to LatLong) and return true, else return false.
 */
static bool lookforLatLong (const char *buf, void *arg)
{
        // check for all required fields, might be more than one class
        // Serial.printf (_FX("\nGPSD: look for ll in: %s\n"), buf);

        const char *classstr;
        bool found_tpv = false;
        for (const char *bp = buf; !found_tpv && (classstr = strstr (bp, "\"class\":\"")) != NULL;
                                                                        bp = classstr+9) {
            if (strncmp (classstr+9, "TPV", 3) == 0)
                found_tpv = true;
        }
        if (!found_tpv)
            return (false);

        const char *modestr = strstr (classstr, "\"mode\":");
        if (!modestr || atoi(modestr+7) < 2)
            return(false);

        const char *latstr = strstr (classstr, "\"lat\":");
        if (!latstr || strlen(latstr) < 6+10)
            return (false);

        const char *lonstr = strstr (classstr, "\"lon\":");
        if (!lonstr || strlen(latstr) < 6+10)
            return (false);

        // crack fields
        LatLong *llp = (LatLong *) arg;
        llp->lat_d = atof(latstr+6);
        llp->lng_d = atof(lonstr+6);

        // success
        Serial.printf (_FX("GPSD: lat %.2f long %.2f\n"), llp->lat_d, llp->lng_d);
        return (true);
}

/* connect to gpsd and return whether lookf() found what it wants.
 */
static bool getGPSDSomething(bool (*lookf)(const char *buf, void *arg), void *arg)
{
        // skip if no configured
        if (!useGPSD())
            return (false);

        // get host name
        const char *host = getGPSDHost();
        Serial.printf (_FX("GPSD: trying %s:%d\n"), host, GPSD_PORT);

        // prep state
        WiFiClient gpsd_client;
        bool look_ok = false;
        bool connect_ok = false;
        bool got_something = false;

        // connect to and read from gpsd server, 
        // N.B. do not use getTCPLine() which calls updateClocks() which calls now() which can call for
        //      a time refresh which calls us!
        resetWatchdog();
        if (wifiOk() && gpsd_client.connect (host, GPSD_PORT)) {

            // initial progress
            connect_ok = true;

            // enable reporting
            gpsd_client.print (F("?WATCH={\"enable\":true,\"json\":true};?POLL;\n"));

            // read lines, give to lookf, done when it's happy or no more
            const size_t MAXGLL = 2000;                    // max line length
            StackMalloc line_mem(MAXGLL);
            char *line = (char *) line_mem.getMem();
            for (size_t ll = 0; ll < MAXGLL && !look_ok && getChar (gpsd_client, &line[ll]); /* none */ ) {
                if (line[ll] == '\n') {
                    line[ll] = '\0';
                    got_something = true;
                    look_ok = (*lookf)(line, arg);
                    ll = 0;
                } else
                    ll++;
            }
        }

        // finished with connection
        gpsd_client.stop();

        // report problems
        if (!look_ok) {
            if (got_something)
                Serial.println (F("GPSD: unexpected response"));
            else if (connect_ok)
                Serial.println (F("GPSD: connected but no response"));
            else
                Serial.println (F("GPSD: no connection"));
        }

        // success?
        return (look_ok);
}

/* return time and server used from GPSD if available, else return 0
 */
time_t getGPSDUTC(const char **server)
{
        time_t gpsd_time;

        if (getGPSDSomething (lookforTime, &gpsd_time)) {
            *server = getGPSDHost();
            return (gpsd_time);
        }

        return (0);
}

/* get lat/long from GPSD and set de_ll, return whether successful.
 */
bool getGPSDLatLong(LatLong *llp)
{
        return (getGPSDSomething (lookforLatLong, llp));
}
