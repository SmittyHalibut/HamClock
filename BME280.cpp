/* support up to two Adafruit BME280 humidity, temperature & pressure sensors connected in I2C mode.
 */

#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

#include "HamClock.h"


// only possible addresses -- correspond to BME_76 and BME_77 indices
#define I2CADDR_1       0x76                    // always at [0] in data arrays below
#define I2CADDR_2       0x77                    // always at [1] in data arrays below

// polling management. total display period eventually approaches N_BME_READINGS * SLOWEST_DT
#define GOSLOWER        (5*60000L)              // take data more slowly after up this long, millis()
#define GOSLOWEST       (60*60000L)             // take data even more slowly after up this long, millis()
#define INITIAL_DT      (5*1000L)               // initial sensing period until GOSLOWER, millis()
#define SLOWER_DT       (60*1000L)              // sensing period after GOSLOWER, millis()
#define SLOWEST_DT      (900*1000L)             // sensing period after GOSLOWEST, millis()

// data management.
static const uint8_t bme_i2c[MAX_N_BME] = {I2CADDR_1, I2CADDR_2};    // N.B. match BME_76 and BME_77 indices
static BMEData *bme_data[MAX_N_BME];            // malloced queues, if found
static Adafruit_BME280 bme_io[MAX_N_BME];       // one for each potential sensor

// time management.
static uint32_t readDT = INITIAL_DT;            // period between readings, millis();
static uint32_t last_reading;                   // last time either sensor was read, millis()
static bool new_t, new_p, new_h, new_d;         // whether new data has been read but not displayed

/* try to connect to "all" sensors else try to reconnect to ones that originally worked.
 */
static void connectSensors(bool all)
{
#ifdef _SUPPORT_ENVSENSOR

    // skip if don't want external IO
    if (!GPIOOk())
        return;

    // try to (re)open each sensor
    for (int i = 0; i < MAX_N_BME; i++) {

        // skip unless all or succeeded before
        if (!all && !bme_data[i])
            continue;

        uint8_t addr = bme_i2c[i];
        Serial.printf (_FX("BME %strying 0x%x\n"), !bme_data[i] ? "" : "re", addr);
        Adafruit_BME280 &bme = bme_io[i];
        if (!bme.begin(addr)) {
            Serial.println (F("BME init fail"));
            continue;
        }

        // open worked: init if first time
        if (!bme_data[i]) {
            bme_data[i] = (BMEData *) calloc (1, sizeof(BMEData));
            bme_data[i]->i2c = addr;
        }

        // Forced mode sleeps until read; normal mode runs continuously and warms the sensor
        bme.setSampling(Adafruit_BME280::MODE_FORCED,
                    Adafruit_BME280::SAMPLING_X1, // temperature
                    Adafruit_BME280::SAMPLING_X1, // pressure
                    Adafruit_BME280::SAMPLING_X1, // humidity
                    Adafruit_BME280::FILTER_OFF,
                    Adafruit_BME280::STANDBY_MS_1000);

        // initial readings are a little flaky, read and discard temp until fairly stable
        #define _N_OK 10
        #define _N_TRY (5*_N_OK)
        #define _TOT_DT 5000    // max millis for entire test
        int n_stable = 0;
        float prev_t = 1e6;
        for (int i = 0; i < _N_TRY && n_stable < _N_OK; i++) {
            float t = bme.readTemperature();
            if (!isnan(t) && t > -40) {
                if (fabsf(t-prev_t) < 1)
                    n_stable++;
                else
                    n_stable = 0;
                prev_t = t;
            }
            wdDelay(_TOT_DT/_N_TRY);
        }

        if (n_stable == _N_OK)
            Serial.println (F("BME init success"));
        else
            Serial.println (F("BME not stable"));
    }

    if (getNBMEConnected() == 0)
        Serial.println(F("BME none found"));

#endif // _SUPPORT_ENVSENSOR
}

/* read current temperature, pressure and humidity in units determined by useMetricUnits() into
 * next q enttry. if ok advance q and return if either ok.
 */
static bool readSensors ()
{
    // skip if don't want any external IO
    if (!GPIOOk())
        return (false);

    bool ok = false;

    for (int i = 0; i < MAX_N_BME; i++) {

        // get data pointer, skip if not used
        BMEData *dp = bme_data[i];
        if (!dp)
            continue;
        Adafruit_BME280 &bme = bme_io[i];

        resetWatchdog();
        bme.takeForcedMeasurement();
        float t = bme.readTemperature();                                // C
        float p = bme.readPressure();                                   // Pascals
        float h = bme.readHumidity();                                   // percent
        if (isnan(t) || t < -40 || isnan(p) || isnan(h)) {
            // try restarting
            Serial.printf (_FX("BME %x read err\n"), dp->i2c);
            connectSensors(false);
        } else {
            // all good
            if (useMetricUnits()) {
                // want C and hPa
                dp->t[dp->q_head] = t + getBMETempCorr(i);              // already C
                dp->p[dp->q_head] = p/100 + getBMEPresCorr(i);          // Pascals to hPa
            } else {
                // want F and inches Hg
                dp->t[dp->q_head] = 1.8*t + 32.0 + getBMETempCorr(i);   // C to F
                dp->p[dp->q_head] = p / 3386.39 + getBMEPresCorr(i);    // Pascals to inches Hg
            }
            dp->h[dp->q_head] = h;
            dp->u[dp->q_head] = now();

            // Serial.printf (_FX("BME %u %x %7.2f %7.2f %7.2f\n"), dp->u[dp->q_head], dp->i2c,
                        // dp->t[dp->q_head], dp->p[dp->q_head], dp->h[dp->q_head]); 

            // advance q
            dp->q_head = (dp->q_head+1)%N_BME_READINGS;
            ok = true;
        }
    }

    // return whether either success
    return (ok);
}

/* convert temperature and relative humidity to dewpoint.
 * both temp units are as per useMetricUnits().
 * http://irtfweb.ifa.hawaii.edu/~tcs3/tcs3/Misc/Dewpoint_Calculation_Humidity_Sensor_E.pdf
 */
float dewPoint (float T, float RH)
{
    // want C
    if (!useMetricUnits())
        T = 5.0F/9.0F*(T-32);           // F to C
    float H = (log10f(RH)-2)/0.4343F + (17.62F*T)/(243.12F+T);
    float Dp = 243.12F*H/(17.62F-H);
    if (!useMetricUnits())
        Dp = 9.0F/5.0F*Dp + 32;         // C to F
    return (Dp);
}

/* plot the given sensor data type choice in the given box, if said choice is one of ours
 */
void drawOneBME280Pane (const SBox &box, PlotChoice ch)
{
    resetWatchdog();

    for (int i = 0; i < MAX_N_BME; i++) {

        // get data pointer, skip if not used
        BMEData *dp = bme_data[i];
        if (!dp)
            continue;

        // prepare the appropriate plot
        float *q;
        const char *ylabel;
        uint16_t color;
        switch (ch) {
        case PLOT_CH_TEMPERATURE:
            q = dp->t;
            if (useMetricUnits())
                ylabel = "Temperature, C";
            else
                ylabel = "Temperature, F";
            color = 0xFBEF;
            new_t = false;
            break;
        case PLOT_CH_PRESSURE:
            q = dp->p;
            if (useMetricUnits())
                ylabel = "Pressure, hPa";
            else
                ylabel = "Pressure, inHg";
            color = RA8875_YELLOW;
            new_p = false;
            break;
        case PLOT_CH_HUMIDITY:
            q = dp->h;
            ylabel = "Humidity, %";
            color = RA8875_CYAN;
            new_h = false;
            break;
        case PLOT_CH_DEWPOINT:
            q = NULL;               // DP is derived, see below
            if (useMetricUnits())
                ylabel = "Dew point, C";
            else
                ylabel = "Dew point, F";
            color = RA8875_GREEN;
            new_d = false;
            break;
        default: 
            // not showing a sensor in this box
            return;
        }

        // x axis depends on time span
        const char *xlabel;
        float time_scale;
        if (readDT >= SLOWEST_DT ) {
            xlabel = "Hours";
            time_scale = -3600.0F;
        } else {
            xlabel = "Minutes";
            time_scale = -60.0F;
        }

        // build linear x and y
        StackMalloc x_mem(N_BME_READINGS*sizeof(float));
        StackMalloc y_mem(N_BME_READINGS*sizeof(float));
        float *x = (float *) x_mem.getMem();
        float *y = (float *) y_mem.getMem();
        time_t t0 = now();
        uint8_t nxy = 0;                                        // count entries with valid times
        resetWatchdog();
        for (int j = 0; j < N_BME_READINGS; j++) {
            uint8_t qi = (dp->q_head + j) % N_BME_READINGS;     // oldest .. newest == qhead .. qhead-1
            if (dp->u[qi] > 0) {                                // skip if time not set
                x[nxy] = (t0 - dp->u[qi])/time_scale;           // minutes ago .. beware unsigned time_t
                if (ch == PLOT_CH_DEWPOINT) {
                    y[nxy] = dewPoint (dp->t[qi], dp->h[qi]);
                } else {
                    y[nxy] = q[qi];
                }
                nxy++;
            }
        }

        // prep appropriate plot box
        SBox plbox = box;                                       // start assuming whole
        char ylplusaddr[strlen(ylabel)+20];
        if (getNBMEConnected() > 1) {
            plbox.h /= 2;                                       // 2 sensors so plot must be half height
            if (i > 0)
                plbox.y += plbox.h;                             // second sensor uses lower half
            // add addr to ylabel if more than one sensor
            snprintf (ylplusaddr, sizeof(ylplusaddr), "I2C %x: %s", bme_i2c[i], ylabel);
            ylabel = ylplusaddr;
        }

        // plot in plbox, showing a bit more precision for imperial pressure
        if (ch == PLOT_CH_PRESSURE && !useMetricUnits()) {
            char buf[32];
            sprintf (buf, "%.2f", y[nxy-1]);
            plotXYstr (plbox, x, y, nxy, xlabel, ylabel, color, 0, 0, buf);
        } else {
            plotXY (plbox, x, y, nxy, xlabel, ylabel, color, 0, 0, y[nxy-1]);
        }
    }
}

/* try to connect to sensors
 */
void initBME280()
{
    connectSensors(true);
}


/* retrieve pointer to the given sensor data if connected, else NULL.
 * index 0 always for 76, 1 for 77.
 */
const BMEData *getBMEData (int i)
{
    return (bme_data[i%MAX_N_BME]);
}

/* take a new reading if it's time.
 * N.B. ignore if no sensors connected or clock not set.
 */
void readBME280 ()
{
        resetWatchdog();

        if (getNBMEConnected() == 0 || !clockTimeOk())
            return;

        uint32_t t0 = millis();

        if (!last_reading || t0 - last_reading >= readDT) {

            // note attempt time whether to not we succeed
            last_reading = millis();

            // read new values into queues and advance
            if (readSensors()) {

                // note type
                new_t = new_p = new_h = new_d = true;

                // gradually slow
                switch (readDT) {
                case INITIAL_DT:
                    if (t0 > GOSLOWER)
                        readDT = SLOWER_DT;
                    break;
                case SLOWER_DT:
                    if (t0 > GOSLOWEST)
                        readDT = SLOWEST_DT;
                    break;
                }
            }
        }
}

/* draw all panes showing any BME data
 */
void drawBME280Panes()
{
        PlotPane pp;

        pp = findPaneChoiceNow (PLOT_CH_TEMPERATURE);
        if (pp != PANE_NONE)
            drawOneBME280Pane (plot_b[pp], PLOT_CH_TEMPERATURE);
        pp = findPaneChoiceNow (PLOT_CH_PRESSURE);
        if (pp != PANE_NONE)
            drawOneBME280Pane (plot_b[pp], PLOT_CH_PRESSURE);
        pp = findPaneChoiceNow (PLOT_CH_HUMIDITY);
        if (pp != PANE_NONE)
            drawOneBME280Pane (plot_b[pp], PLOT_CH_HUMIDITY);
        pp = findPaneChoiceNow (PLOT_CH_DEWPOINT);
        if (pp != PANE_NONE)
            drawOneBME280Pane (plot_b[pp], PLOT_CH_DEWPOINT);
}

/* return whether new data of the given type has been read that has not been displayed
 */
bool newBME280data(PlotChoice ch)
{
        switch (ch) {
        case PLOT_CH_TEMPERATURE:   return (new_t);
        case PLOT_CH_PRESSURE:      return (new_p);
        case PLOT_CH_HUMIDITY:      return (new_h);
        case PLOT_CH_DEWPOINT:      return (new_d);
        default:
            fatalError (_FX("Bug! newBME280data() bogus choice %d"), (int)ch);
            return (false);
        }
}

/* return number of connected BME sensors.
 * N.B. only valid after connectSensors()
 */
int getNBMEConnected(void)
{
        return ((bme_data[BME_76] != NULL) + (bme_data[BME_77] != NULL));
}
