/* this is code to compute lunar and solar positions.
 * it is hacked together for HamClock from my XEphem's libastro, see https://clearskyinstitute.com/xephem
 *
 * unit test:
 *   g++ -D_UNIT_TEST -O2 -Wall -o astro-test astro.cpp
 */

#ifdef _UNIT_TEST

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>

typedef struct {
    float az, el;
    float ra, dec;
    float gha;
    float dist;
    float vel;
    float phase;
} AstroCir;

typedef struct {
    float lat, lng;
    float lat_d, lng_d;
} LatLong;

#define deg2rad(x)       ((x)*M_PI/180)
#define rad2deg(x)       ((x)*180/M_PI)

#else

#include "HamClock.h"

#endif

#define hr2deg(x)        ((x)*15.)
#define deg2hr(x)        ((x)/15.)
#define hr2rad(x)        deg2rad(hr2deg(x))
#define rad2hr(x)        deg2hr(rad2deg(x))

#define SIDRATE         .9972695677

#define REF_PRESS       1000
#define REF_TEMP        10


/* insure 0 <= *v < r.
*/
static void range (double *v, double r)
{
    *v -= r*floor(*v/r);
}


/* given the mjd, find the geocentric ecliptic longitude, lam, and latitude,
* bet, and horizontal parallax, hp for the moon.
* N.B. series for long and lat are good to about 10 and 3 arcseconds.
* N.B. still need to correct for nutation. then for topocentric location
*   further correct for parallax and refraction.
*/
static void moon (double mjd, double *lam, double *bet, double *hp)
{
    double t, t2;
    double ld;
    double ms;
    double md;
    double de;
    double f;
    double n;
    double a, sa, sn, b, sb, c, sc, e, e2, l, g, w1, w2;
    double m1, m2, m3, m4, m5, m6;

    t = mjd/36525.;
    t2 = t*t;

    m1 = mjd/27.32158213;
    m1 = 360.0*(m1-(long)m1);
    m2 = mjd/365.2596407;
    m2 = 360.0*(m2-(long)m2);
    m3 = mjd/27.55455094;
    m3 = 360.0*(m3-(long)m3);
    m4 = mjd/29.53058868;
    m4 = 360.0*(m4-(long)m4);
    m5 = mjd/27.21222039;
    m5 = 360.0*(m5-(long)m5);
    m6 = mjd/6798.363307;
    m6 = 360.0*(m6-(long)m6);

    ld = 270.434164+m1-(.001133-.0000019*t)*t2;
    ms = 358.475833+m2-(.00015+.0000033*t)*t2;
    md = 296.104608+m3+(.009192+.0000144*t)*t2;
    de = 350.737486+m4-(.001436-.0000019*t)*t2;
    f = 11.250889+m5-(.003211+.0000003*t)*t2;
    n = 259.183275-m6+(.002078+.000022*t)*t2;

    a = deg2rad(51.2+20.2*t);
    sa = sin(a);
    sn = sin(deg2rad(n));
    b = 346.56+(132.87-.0091731*t)*t;
    sb = .003964*sin(deg2rad(b));
    c = deg2rad(n+275.05-2.3*t);
    sc = sin(c);
    ld = ld+.000233*sa+sb+.001964*sn;
    ms = ms-.001778*sa;
    md = md+.000817*sa+sb+.002541*sn;
    f = f+sb-.024691*sn-.004328*sc;
    de = de+.002011*sa+sb+.001964*sn;
    e = 1-(.002495+7.52e-06*t)*t;
    e2 = e*e;

    ld = deg2rad(ld);
    ms = deg2rad(ms);
    n = deg2rad(n);
    de = deg2rad(de);
    f = deg2rad(f);
    md = deg2rad(md);

    l = 6.28875*sin(md)+1.27402*sin(2*de-md)+.658309*sin(2*de)+
        .213616*sin(2*md)-e*.185596*sin(ms)-.114336*sin(2*f)+
        .058793*sin(2*(de-md))+.057212*e*sin(2*de-ms-md)+
        .05332*sin(2*de+md)+.045874*e*sin(2*de-ms)+.041024*e*sin(md-ms);
    l = l-.034718*sin(de)-e*.030465*sin(ms+md)+.015326*sin(2*(de-f))-
        .012528*sin(2*f+md)-.01098*sin(2*f-md)+.010674*sin(4*de-md)+
        .010034*sin(3*md)+.008548*sin(4*de-2*md)-e*.00791*sin(ms-md+2*de)-
        e*.006783*sin(2*de+ms);
    l = l+.005162*sin(md-de)+e*.005*sin(ms+de)+.003862*sin(4*de)+
        e*.004049*sin(md-ms+2*de)+.003996*sin(2*(md+de))+
        .003665*sin(2*de-3*md)+e*.002695*sin(2*md-ms)+
        .002602*sin(md-2*(f+de))+e*.002396*sin(2*(de-md)-ms)-
        .002349*sin(md+de);
    l = l+e2*.002249*sin(2*(de-ms))-e*.002125*sin(2*md+ms)-
        e2*.002079*sin(2*ms)+e2*.002059*sin(2*(de-ms)-md)-
        .001773*sin(md+2*(de-f))-.001595*sin(2*(f+de))+
        e*.00122*sin(4*de-ms-md)-.00111*sin(2*(md+f))+.000892*sin(md-3*de);
    l = l-e*.000811*sin(ms+md+2*de)+e*.000761*sin(4*de-ms-2*md)+
         e2*.000704*sin(md-2*(ms+de))+e*.000693*sin(ms-2*(md-de))+
         e*.000598*sin(2*(de-f)-ms)+.00055*sin(md+4*de)+.000538*sin(4*md)+
         e*.000521*sin(4*de-ms)+.000486*sin(2*md-de);
    l = l+e2*.000717*sin(md-2*ms);
    *lam = ld+deg2rad(l);
    range (lam, 2*M_PI);

    g = 5.12819*sin(f)+.280606*sin(md+f)+.277693*sin(md-f)+
        .173238*sin(2*de-f)+.055413*sin(2*de+f-md)+.046272*sin(2*de-f-md)+
        .032573*sin(2*de+f)+.017198*sin(2*md+f)+.009267*sin(2*de+md-f)+
        .008823*sin(2*md-f)+e*.008247*sin(2*de-ms-f);
    g = g+.004323*sin(2*(de-md)-f)+.0042*sin(2*de+f+md)+
        e*.003372*sin(f-ms-2*de)+e*.002472*sin(2*de+f-ms-md)+
        e*.002222*sin(2*de+f-ms)+e*.002072*sin(2*de-f-ms-md)+
        e*.001877*sin(f-ms+md)+.001828*sin(4*de-f-md)-e*.001803*sin(f+ms)-
        .00175*sin(3*f);
    g = g+e*.00157*sin(md-ms-f)-.001487*sin(f+de)-e*.001481*sin(f+ms+md)+
         e*.001417*sin(f-ms-md)+e*.00135*sin(f-ms)+.00133*sin(f-de)+
         .001106*sin(f+3*md)+.00102*sin(4*de-f)+.000833*sin(f+4*de-md)+
         .000781*sin(md-3*f)+.00067*sin(f+4*de-2*md);
    g = g+.000606*sin(2*de-3*f)+.000597*sin(2*(de+md)-f)+
        e*.000492*sin(2*de+md-ms-f)+.00045*sin(2*(md-de)-f)+
        .000439*sin(3*md-f)+.000423*sin(f+2*(de+md))+
        .000422*sin(2*de-f-3*md)-e*.000367*sin(ms+f+2*de-md)-
        e*.000353*sin(ms+f+2*de)+.000331*sin(f+4*de);
    g = g+e*.000317*sin(2*de+f-ms+md)+e2*.000306*sin(2*(de-ms)-f)-
        .000283*sin(md+3*f);
    w1 = .0004664*cos(n);
    w2 = .0000754*cos(c);
    *bet = deg2rad(g)*(1-w1-w2);

    *hp = .950724+.051818*cos(md)+.009531*cos(2*de-md)+.007843*cos(2*de)+
          .002824*cos(2*md)+.000857*cos(2*de+md)+e*.000533*cos(2*de-ms)+
          e*.000401*cos(2*de-md-ms)+e*.00032*cos(md-ms)-.000271*cos(de)-
          e*.000264*cos(ms+md)-.000198*cos(2*f-md);
    *hp = *hp+.000173*cos(3*md)+.000167*cos(4*de-md)-e*.000111*cos(ms)+
         .000103*cos(4*de-2*md)-.000084*cos(2*md-2*de)-
         e*.000083*cos(2*de+ms)+.000079*cos(2*de+2*md)+.000072*cos(4*de)+
         e*.000064*cos(2*de-ms+md)-e*.000063*cos(2*de+ms-md)+
         e*.000041*cos(ms+de);
    *hp = *hp+e*.000035*cos(2*md-ms)-.000033*cos(3*md-2*de)-
         .00003*cos(md+de)-.000029*cos(2*(f-de))-e*.000029*cos(2*md+ms)+
         e2*.000026*cos(2*(de-ms))-.000023*cos(2*(f-de)+md)+
         e*.000019*cos(4*de-ms-md);
    *hp = deg2rad(*hp);
}

/* given the modified Julian date, mjd, find the obliquity of the
* ecliptic, *eps, in radians.
*/
static void obliquity (double mjd, double *eps)
{
    double t = mjd/36525.;
    *eps = deg2rad(2.345229444E1 - ((((-1.81E-3*t)+5.9E-3)*t+4.6845E1)*t)/3600.0);
}

/* given the modified JD, mjd, find the nutation in obliquity, *deps, and
* the nutation in longitude, *dpsi, each in radians.
*/
static void nutation (double mjd, double *deps, double *dpsi)
{
    double ls, ld;      /* sun's mean longitude, moon's mean longitude */
    double ms, md;      /* sun's mean anomaly, moon's mean anomaly */
    double nm;  /* longitude of moon's ascending node */
    double t, t2;       /* number of Julian centuries of 36525 days since
                     * Jan 0.5 1900.
                     */
    double tls, tnm, tld;       /* twice above */
    double a, b;        /* temps */

    t = mjd/36525.;
    t2 = t*t;

    a = 100.0021358*t;
    b = 360.*(a-(long)a);
    ls = 279.697+.000303*t2+b;

    a = 1336.855231*t;
    b = 360.*(a-(long)a);
    ld = 270.434-.001133*t2+b;

    a = 99.99736056000026*t;
    b = 360.*(a-(long)a);
    ms = 358.476-.00015*t2+b;

    a = 13255523.59*t;
    b = 360.*(a-(long)a);
    md = 296.105+.009192*t2+b;

    a = 5.372616667*t;
    b = 360.*(a-(long)a);
    nm = 259.183+.002078*t2-b;

    /* convert to radian forms for use with trig functions.
     */
    tls = 2*deg2rad(ls);
    nm = deg2rad(nm);
    tnm = 2*nm;
    ms = deg2rad(ms);
    tld = 2*deg2rad(ld);
    md = deg2rad(md);

    /* find delta psi and eps, in arcseconds.
     */
    *dpsi = (-17.2327-.01737*t)*sin(nm)+(-1.2729-.00013*t)*sin(tls)
               +.2088*sin(tnm)-.2037*sin(tld)+(.1261-.00031*t)*sin(ms)
               +.0675*sin(md)-(.0497-.00012*t)*sin(tls+ms)
               -.0342*sin(tld-nm)-.0261*sin(tld+md)+.0214*sin(tls-ms)
               -.0149*sin(tls-tld+md)+.0124*sin(tls-nm)+.0114*sin(tld-md);
    *deps = (9.21+.00091*t)*cos(nm)+(.5522-.00029*t)*cos(tls)
               -.0904*cos(tnm)+.0884*cos(tld)+.0216*cos(tls+ms)
               +.0183*cos(tld-nm)+.0113*cos(tld+md)-.0093*cos(tls-ms)
               -.0066*cos(tls-nm);

    /* convert to radians.
     */
    *dpsi = deg2rad(*dpsi/3600);
    *deps = deg2rad(*deps/3600);
}

static void ecleq_aux (int sw, double mjd, double x, double y, double *p, double *q)
{
        double seps, ceps;              /* sin and cos of mean obliquity */
        double sx, cx, sy, cy, ty;

        double eps;
        double deps, dpsi;
        obliquity (mjd, &eps);          /* mean obliquity for date */
        nutation (mjd, &deps, &dpsi);
        eps += deps;
        seps = sin(eps);
        ceps = cos(eps);

        sy = sin(y);
        cy = cos(y);                            /* always non-negative */
        if (fabs(cy)<1e-20) cy = 1e-20;         /* insure > 0 */
        ty = sy/cy;
        cx = cos(x);
        sx = sin(x);
        *q = asin((sy*ceps)-(cy*seps*sx*sw));
        *p = atan(((sx*ceps)+(ty*seps*sw))/cx);
        if (cx<0) *p += M_PI;           /* account for atan quad ambiguity */
        range (p, 2*M_PI);
}

/* given the modified Julian date, mjd, and a geocentric ecliptic latitude,
 * *lat, and longititude, *lng, each in radians, find the corresponding
 * equitorial ra and dec, also each in radians.
 * correction for the effect on the angle of the obliquity due to nutation is
 * included.
 */
static void ecl_eq (double mjd, double lat, double lng, double *ra, double *dec)
{
        ecleq_aux (-1, mjd, lng, lat, ra, dec);
}

/* given the mean anomaly, ma, and the eccentricity, s, of elliptical motion,
 * find the true anomaly, *nu, and the eccentric anomaly, *ea.
 * all angles in radians.
 */
static void anomaly (double ma, double s, double *nu, double *ea)
{
        double m, fea;

        m = ma-2*M_PI*(long)(ma/(2*M_PI));
        if (m > M_PI) m -= 2*M_PI;
        if (m < -M_PI) m += 2*M_PI;
        fea = m;

        if (s < 1.0) {
            /* elliptical */
            double dla;
            while (1) {
                dla = fea-(s*sin(fea))-m;
                if (fabs(dla)<1e-6)
                    break;
                dla /= 1-(s*cos(fea));
                fea -= dla;
            }
            *nu = 2*atan(sqrt((1+s)/(1-s))*tan(fea/2));
        } else {
            /* hyperbolic */
            double corr = 1;
            while (fabs(corr) > 0.000001) {
              corr = (m - s * sinh(fea) + fea) / (s*cosh(fea) - 1);
              fea += corr;
            }
            *nu = 2*atan(sqrt((s+1)/(s-1))*tanh(fea/2));
        }
        *ea = fea;
}

/* given the modified JD, mjd, return the true geocentric ecliptic longitude
 *   of the sun for the mean equinox of the date, *lsn, in radians, and the
 *   sun-earth distance, *rsn, in AU. (the true ecliptic latitude is never more
 *   than 1.2 arc seconds and so may be taken to be a constant 0.)
 * if the APPARENT ecliptic longitude is required, correct the longitude for
 *   nutation to the true equinox of date and for aberration (light travel time,
 *   approximately  -9.27e7/186000/(3600*24*365)*2*pi = -9.93e-5 radians).
 */
static void sunpos (double mjd, double *lsn, double *rsn)
{
        double t, t2;
        double ls, ms;    /* mean longitude and mean anomoay */
        double s, nu, ea; /* eccentricity, true anomaly, eccentric anomaly */
        double a, b, a1, b1, c1, d1, e1, h1, dl, dr;

        t = mjd/36525.;
        t2 = t*t;
        a = 100.0021359*t;
        b = 360.*(a-(long)a);
        ls = 279.69668+.0003025*t2+b;
        a = 99.99736042000039*t;
        b = 360*(a-(long)a);
        ms = 358.47583-(.00015+.0000033*t)*t2+b;
        s = .016751-.0000418*t-1.26e-07*t2;
        anomaly (deg2rad(ms), s, &nu, &ea);
        a = 62.55209472000015*t;
        b = 360*(a-(long)a);
        a1 = deg2rad(153.23+b);
        a = 125.1041894*t;
        b = 360*(a-(long)a);
        b1 = deg2rad(216.57+b);
        a = 91.56766028*t;
        b = 360*(a-(long)a);
        c1 = deg2rad(312.69+b);
        a = 1236.853095*t;
        b = 360*(a-(long)a);
        d1 = deg2rad(350.74-.00144*t2+b);
        e1 = deg2rad(231.19+20.2*t);
        a = 183.1353208*t;
        b = 360*(a-(long)a);
        h1 = deg2rad(353.4+b);
        dl = .00134*cos(a1)+.00154*cos(b1)+.002*cos(c1)+.00179*sin(d1)+
                                                                .00178*sin(e1);
        dr = 5.43e-06*sin(a1)+1.575e-05*sin(b1)+1.627e-05*sin(c1)+
                                            3.076e-05*cos(d1)+9.27e-06*sin(h1);
        *lsn = nu+deg2rad(ls-ms+dl);
        *rsn = 1.0000002*(1-s*cos(ea))+dr;
        range (lsn, 2*M_PI);
}

/* given geocentric ecliptic longitude and latitude, lam and bet, of some object
 * and the longitude of the sun, lsn, find the elongation, el. this is the
 * actual angular separation of the object from the sun, not just the difference
 * in the longitude. the sign, however, IS set simply as a test on longitude
 * such that el will be >0 for an evening object <0 for a morning object.
 * to understand the test for el sign, draw a graph with lam going from 0-2*M_PI
 *   down the vertical axis, lsn going from 0-2*M_PI across the hor axis. then
 *   define the diagonal regions bounded by the lines lam=lsn+M_PI, lam=lsn and
 *   lam=lsn-M_PI. the "morning" regions are any values to the lower left of the
 *   first line and bounded within the second pair of lines.
 * all angles in radians.
 */
static void elongation (double lam, double bet, double lsn, double *el)
{
        *el = acos(cos(bet)*cos(lam-lsn));
        if (lam>lsn+M_PI || (lam>lsn-M_PI && lam<lsn)) *el = - *el;
}

/* given the modified Julian date (number of days elapsed since 1900 jan 0.5,),
 * mjd, return the calendar date in months, *mn, days, *dy, and years, *yr.
 */
static void mjd_cal (double mjd, int *mn, double *dy, int *yr)
{
        double d, f;
        double i, a, b, ce, g;

        d = mjd + 0.5;
        i = floor(d);
        f = d-i;
        if (f == 1) {
            f = 0;
            i += 1;
        }

        if (i > -115860.0) {
            a = floor((i/36524.25)+.9983573)+14;
            i += 1 + a - floor(a/4.0);
        }

        b = floor((i/365.25)+.802601);
        ce = i - floor((365.25*b)+.750001)+416;
        g = floor(ce/30.6001);
        *mn = g - 1;
        *dy = ce - floor(30.6001*g)+f;
        *yr = b + 1899;

        if (g > 13.5)
            *mn = g - 13;
        if (*mn < 2.5)
            *yr = b + 1900;
        if (*yr < 1)
            *yr -= 1;
}

/* given a date in months, mn, days, dy, years, yr,
 * return the modified Julian date (number of days elapsed since 1900 jan 0.5),
 * *mjd.
 */
static void cal_mjd (int mn, double dy, int yr, double *mjd)
{
        int b, d, m, y;
        long c;

        m = mn;
        y = (yr < 0) ? yr + 1 : yr;
        if (mn < 3) {
            m += 12;
            y -= 1;
        }

        if (yr < 1582 || (yr == 1582 && (mn < 10 || (mn == 10 && dy < 15))))
            b = 0;
        else {
            int a;
            a = y/100;
            b = 2 - a + a/4;
        }

        if (y < 0)
            c = (long)((365.25*y) - 0.75) - 694025L;
        else
            c = (long)(365.25*y) - 694025L;

        d = 30.6001*(m+1);

        *mjd = b + c + d + dy - 0.5;
}

static double mjd_day(double jd)
{
        return (floor(jd-0.5)+0.5);
}

static double mjd_hr(double jd)
{
        return ((jd-mjd_day(jd))*24.0);
}


static double tnaught (double mjd)
{
        double dmjd;
        int m, y;
        double d;
        double t, t0;

        mjd_cal (mjd, &m, &d, &y);
        cal_mjd (1, 0., y, &dmjd);
        t = dmjd/36525;
        t0 = 6.57098e-2 * (mjd - dmjd) - 
             (24 - (6.6460656 + (5.1262e-2 + (t * 2.581e-5))*t) -
                   (2400 * (t - (((double)y - 1900)/100))));
        return (t0);
}

/* given a modified julian date, mjd, and a universally coordinated time, utc,
 * return greenwich mean siderial time, *gst.
 */
static void utc_gst (double mjd, double utc, double *gst)
{
        double t0 = tnaught (mjd);
        *gst = (1.0/SIDRATE)*utc + t0;
        range (gst, 24.0);
}


static void now_lst (double mjd, double lng, double *lst)
{
        utc_gst (mjd_day(mjd), mjd_hr(mjd), lst);
        *lst += rad2hr(lng);
        range (lst, 24.0);
}

// return days since 1900 jan 0.5
static double unix2mjd (long unix_time)
{
       return ((unix_time/86400.0) + 2440587.5 - 2415020.0);
}


/* given true ha and dec, tha and tdec, the geographical latitude, phi, the
 * height above sea-level (as a fraction of the earths radius, 6378.16km),
 * ht, and the equatorial horizontal parallax, ehp, find the apparent
 * ha and dec, aha and adec allowing for parallax.
 * all angles in radians. ehp is the angle subtended at the body by the
 * earth's equator.
 */
static void ta_par (double tha, double tdec, double phi, double ht, double ehp, double *aha, double *adec)
{
        double rp;      /* distance to object in Earth radii */
        double ctha;
        double stdec, ctdec;
        double tdtha, dtha;
        double caha;

        double cphi, sphi, u, rsp, rcp;
        cphi = cos(phi);
        sphi = sin(phi);
        u = atan(9.96647e-1*sphi/cphi);
        rsp = (9.96647e-1*sin(u))+(ht*sphi);
        rcp = cos(u)+(ht*cphi);

        rp = 1/sin(ehp);

        ctha = cos(tha);
        stdec = sin(tdec);
        ctdec = cos(tdec);
        tdtha = (rcp*sin(tha))/((rp*ctdec)-(rcp*ctha));
        dtha = atan(tdtha);
        *aha = tha+dtha;
        caha = cos(*aha);
        range (aha, 2*M_PI);
        *adec = atan(caha*(rp*stdec-rsp)/(rp*ctdec*ctha-rcp));
}


/* the actual formula is the same for both transformation directions so
 * do it here once for each way.
 * N.B. all arguments are in radians.
 */
static void aaha_aux (double lat, double x, double y, double *p, double *q)
{
        double sy, cy;
        double sx, cx;

        double sinlat = sin (lat);
        double coslat = cos (lat);

        sy = sin (y);
        cy = cos (y);
        sx = sin (x);
        cx = cos (x);

        *q = asin ((sy*sinlat) + (cy*coslat*cx));
        *p = atan2 (-cy*sx, -cy*cx*sinlat + sy*coslat);
}

/* given latitude (n+, radians), lat, hour angle (radians), ha, and declination
 * (radians), dec,
 * return altitude (up+, radians), alt, and
 * azimuth (angle round to the east from north+, radians),
 */
static void hadec_aa (double lat, double ha, double dec, double *alt, double *az)
{
        aaha_aux (lat, ha, dec, az, alt);
}

/* correct the true altitude, ta, for refraction to the apparent altitude, aa,
 * each in radians, given the local atmospheric pressure, pr, in mbars, and
 * the temperature, tr, in degrees C.
 */
static void refract (double pr, double tr, double ta, double *aa)
{
        double r;       /* refraction correction*/

        if (ta >= deg2rad(15.)) {
            /* model for altitudes at least 15 degrees above horizon */
            r = 7.888888e-5*pr/((273+tr)*tan(ta));
        } else if (ta > deg2rad(-5.)) {
            /* hairier model for altitudes at least -5 and below 15 degrees */
            double a, b, tadeg = rad2deg(ta);
            a = ((2e-5*tadeg+1.96e-2)*tadeg+1.594e-1)*pr;
            b = (273+tr)*((8.45e-2*tadeg+5.05e-1)*tadeg+1);
            r = deg2rad(a/b);
        } else {
            /* do nothing if more than 5 degrees below horizon.
             */
            r = 0;
        }

        *aa  =  ta + r;
}



/* given UNIX time, location, and function to compute object gha and dec,
 *    return UNIX secs of next rise and set.
 * if never rises: *trise (only) will be 0; if never sets: *tset (only) will be 0.
 */
static void riseset (const time_t t0, const LatLong &ll,
void (*cir_func)(time_t t0, const LatLong &ll, AstroCir &cir), time_t *riset, time_t *sett)
{
        /* use the generalized sunrise equation:
         * cos w = (sin(a) - sin(phi) sin (del))/(cos(phi) cos (del))
         *   w = hr angle at sunrise (neg) or sunset (pos)
         *   phi = lat of observer
         *   del = object dec
         *     a = real altitude at sunrise
         * N.B. do need some iteration especially for moon
         * N.B. we presume cir_func does not include refraction, so we do it here.
         */

        #define MAX_LOOP 10                     // rarely more than a few unless near a pole
        #define MAX_DT   30                     // convergence tolerance, secs

        double cos_riseset_ha, riseset_ha, local_ha, rise_deg, set_deg;
        AstroCir cir;
        int n_loops;

        // refraction correction
        double ref_dep = 0;
        refract (REF_PRESS, REF_TEMP, ref_dep, &ref_dep);
        // printf ("ref_dep %g\n", rad2deg(ref_dep)*60);

        // repeat riset until converges
        *riset = t0 + 6*3600;
        // printf ("  RS2: rise %ld\n", *riset);
        time_t rt;
        n_loops = 0;
        do {
            rt = *riset;
            // find gha and dec of 
            (*cir_func)(rt, ll, cir);
            // find cos of rise/set ha, no event if outside -1..1
            cos_riseset_ha =  (sin (-ref_dep) - sin(ll.lat)*sin(cir.dec))/(cos(ll.lat)*cos(cir.dec));
            if (cos_riseset_ha < -1) {
                // printf ("RS: lat= %g no set\n", ll.lat_d);
                *riset = 1;
                *sett = 0;
                return;
            }
            if (cos_riseset_ha > 1) {
                // printf ("RS: lat= %g no rise\n", ll.lat_d);
                *riset = 0;
                *sett = 1;
                return;
            }
            // find rise/set ha, 0 .. 180 degs
            riseset_ha = rad2deg (acos (cos_riseset_ha));
            // find local solar hour angle now, in degrees, -180 .. 180
            local_ha = fmod (rad2deg(cir.gha) + ll.lng_d + 360 + 180, 360) - 180;
            // rise: angle -180 .. 180 => time -12 .. +12 hrs
            rise_deg = fmod (local_ha + riseset_ha + 360 + 180, 360) - 180;
            *riset -= deg2hr(rise_deg)*3600;
            // printf ("  RS2: rise %ld\n", *riset);
        } while (++n_loops < MAX_LOOP && labs (*riset - rt) > MAX_DT);
        // printf ("  Rise %d loops\n", n_loops);
        if (n_loops == MAX_LOOP) {
            *riset = 0;
            *sett = 1;
            return;
        }

        // repeat sett until converges
        *sett = t0 + 6*3600;
        // printf ("  RS2: set  %ld\n", *sett);
        time_t st;
        n_loops = 0;
        do {
            st = *sett;
            (*cir_func)(st, ll, cir);
            cos_riseset_ha =  (sin (-ref_dep) - sin(ll.lat)*sin(cir.dec))/(cos(ll.lat)*cos(cir.dec));
            if (cos_riseset_ha < -1) {
                // printf ("RS: lat= %g no set\n", ll.lat_d);
                *sett = 0;
                *riset = 1;
                return;
            }
            if (cos_riseset_ha > 1) {
                // printf ("RS: lat= %g no rise\n", ll.lat_d);
                *riset = 0;
                *sett = 1;
                return;
            }
            riseset_ha = rad2deg (acos (cos_riseset_ha));
            local_ha = fmod (rad2deg(cir.gha) + ll.lng_d + 360 + 180, 360) - 180;
            set_deg = fmod (riseset_ha - local_ha + 360 + 180, 360) - 180;
            *sett += deg2hr(set_deg)*3600;
            // printf ("  RS2: set  %ld\n", *sett);
        } while (++n_loops < MAX_LOOP && labs (*sett - st) > MAX_DT);
        // printf ("  Set  %d loops\n", n_loops);
        if (n_loops == MAX_LOOP) {
            *riset = 1;
            *sett = 0;
            return;
        }

}

/* find moon's circumstances now.
 * alt is not corrected for refraction so it can be used with HA rise/set algorithm.
 */
static void lunarCir (time_t t0, const LatLong &ll, AstroCir &cir)
{
        double ehp;
        double lst, alt, az;
        double ra, dec, ha;
        double lam, bet;
        double deps, dpsi;
        double lsn, rsn;
        double el;

        double mjd = unix2mjd (t0);

        moon (mjd, &lam, &bet, &ehp);           /* moon's true ecliptic loc */
        nutation (mjd, &deps, &dpsi);           /* correct for nutation */
        lam += dpsi;
        range (&lam, 2*M_PI);

        cir.dist = 6378.14/sin(ehp);            /* earth-moon dist, want km */

        ecl_eq (mjd, bet, lam, &ra, &dec);
        range (&ra, 2*M_PI);
        cir.ra = ra;
        cir.dec = dec;

        sunpos (mjd, &lsn, &rsn);
        range (&lsn, 2*M_PI);

        elongation (lam, bet, lsn, &el);
        cir.phase = el;

        now_lst (mjd, ll.lng, &lst);
        ha = hr2rad(lst) - ra;
        ta_par (ha, dec, ll.lat, 0, ehp, &ha, &dec);
        hadec_aa (ll.lat, ha, dec, &alt, &az);
        range (&az, 2*M_PI);
        cir.el = alt;
        cir.az = az;
        double gha = ha - ll.lng;
        range (&gha, 2*M_PI);
        cir.gha = gha;

        // vel added by caller
        cir.vel = 0;
}

/* find sun's circumstances now.
 * alt is not corrected for refraction so it can be used with HA rise/set algorithm.
 */
static void solarCir (time_t t0, const LatLong &ll, AstroCir &cir)
{
        double lst, alt, az;
        double ra, dec, ha;
        double lsn, rsn;
        double deps, dpsi;

        double mjd = unix2mjd (t0);

        sunpos (mjd, &lsn, &rsn);       /* sun's true ecliptic long * and dist */
        nutation (mjd, &deps, &dpsi);   /* correct for nutation */
        lsn += dpsi;
        lsn -= deg2rad(20.4/3600);      /* and light travel time */

        cir.dist = rsn;
        cir.phase = 0;

        ecl_eq (mjd, 0.0, lsn, &ra, &dec);
        range (&ra, 2*M_PI);
        cir.ra = ra;
        cir.dec = dec;

        now_lst (mjd, ll.lng, &lst);
        ha = hr2rad(lst) - ra;
        // printf ("Sun lng %g lst %g ra %g ha %g\n", ll.lng_d, hr2deg(lst), rad2deg(ra), rad2deg(ha));
        hadec_aa (ll.lat, ha, dec, &alt, &az);
        range (&az, 2*M_PI);
        cir.el = alt;
        cir.az = az;
        double gha = ha - ll.lng;
        range (&gha, 2*M_PI);
        cir.gha = gha;

        // TODO vel
        cir.vel = 0;
}


/*******************************************************************************************
 *
 *    HamClock interface
 *
 *******************************************************************************************/


void getLunarCir (time_t t0, const LatLong &ll, AstroCir &cir)
{
        static time_t t0_vel;   // time of last vel calc
        static float geo_vel;   // last vel calc

        // info now
        lunarCir (t0, ll, cir);
        double alt = cir.el;
        refract (REF_PRESS, REF_TEMP, alt, &alt);
        cir.el = alt;

        // get geocentric vel by measuring change over brief time; infrequent ok, it changes slowly
        #define UPDATE_DT       3600
        #define VEL_DT           200
        if (labs (t0 - t0_vel) > UPDATE_DT) {
            AstroCir cir_plus;
            t0 += VEL_DT;
            lunarCir (t0, ll, cir_plus);
            t0_vel = t0;
            geo_vel = 1000*(cir_plus.dist - cir.dist)/VEL_DT;  // want m/s
        }

        // velocity is sum of earth rotation and lunar distance change
        #define EQUATOR_SPEED_MPS       465
        float lunar_ha = cir.gha + ll.lng;
        float rot_vel = EQUATOR_SPEED_MPS * cosf(ll.lat) * sinf(lunar_ha);
        cir.vel = rot_vel + geo_vel;
}


void getSolarCir (time_t t0, const LatLong &ll, AstroCir &cir)
{
        // info now
        solarCir (t0, ll, cir);
        double alt = cir.el;
        refract (REF_PRESS, REF_TEMP, alt, &alt);
        cir.el = alt;

        // TODO: vel
}



void getSolarRS (const time_t t0, const LatLong &ll, time_t *riset, time_t *sett)
{
        riseset (t0, ll, getSolarCir, riset, sett);
}


void getLunarRS (const time_t t0, const LatLong &ll, time_t *riset, time_t *sett)
{
        riseset (t0, ll, getLunarCir, riset, sett);
}





#if defined (_UNIT_TEST)


static void usage (const char *me)
{
        fprintf (stderr, "Purpose: test sun and moon algorithms\n");
        fprintf (stderr, "Usage: %s LatN LongE YYYY MM DD HH MM SS\n", me);
        exit (1);
}

/* return pointer to static string of the form deg:min.
 * N.B. use before next call
 */
static char *prradhexa (double rad)
{
        static char prradhexa_str[32];
        int sign = rad < 0 ? -1 : 1;
        double deg = rad2deg(fabs(rad));
        int d = deg;
        int m = (deg - d)*60;
        sprintf (prradhexa_str, "%3d:%02d", d*sign, m);
        return (prradhexa_str);
}

int main (int ac, char *av[])
{
        AstroCir cir;
        time_t rt, st;

        if (ac != 9)
            usage (av[0]);

        // capture qth
        LatLong ll;
        memset (&ll, 0, sizeof(ll));
        ll.lat_d = atof (av[1]);
        ll.lng_d = atof (av[2]);
        ll.lat = deg2rad(ll.lat_d);
        ll.lng = deg2rad(ll.lng_d);

        // convert remaining args to time_t
        struct tm tms;
        memset (&tms, 0, sizeof(tms));
        tms.tm_year = atoi(av[3]) - 1900;       // wants year - 1900
        tms.tm_mon = atoi(av[4]) - 1;           // wants 0..11
        tms.tm_mday = atoi(av[5]);
        tms.tm_hour = atoi(av[6]);
        tms.tm_min = atoi(av[7]);
        tms.tm_sec = atoi(av[8]);
        setenv ("TZ", "", 1);                   // UTC
        tzset();
        time_t t0 = mktime (&tms);

        // find sun info
        getSolarCir (t0, ll, cir);
        printf ("Sun at %g %g @ %ld = %d %d %d %d %d %d:\n", ll.lat_d, ll.lng_d,
                    t0, tms.tm_year+1900, tms.tm_mon+1, tms.tm_mday, tms.tm_hour, tms.tm_min, tms.tm_sec);
        printf ("  El:   %6.2f %s\n", rad2deg(cir.el), prradhexa(cir.el));
        printf ("  Az:   %6.2f %s\n", rad2deg(cir.az), prradhexa(cir.az));
        printf ("  RA:   %6.2f %s\n", rad2deg(cir.ra), prradhexa(cir.ra/15));
        printf ("  Dec:  %6.2f %s\n", rad2deg(cir.dec), prradhexa(cir.dec));
        printf ("  GHA:  %6.2f %s\n", rad2deg(cir.gha), prradhexa(cir.gha/15));
        printf ("  Vel:  %6.2f\n", cir.vel);
        printf ("  Dist: %6.4f AU\n", cir.dist);

        // find sun rise and set, if any
        getSolarRS (t0, ll, &rt, &st);
        if (rt && st) {
            printf ("  Rise: %s", ctime (&rt));
            printf ("  Set:  %s", ctime (&st));
        } else {
            printf ("  No R/S %ld %ld\n", rt, st);
        }

        // find moon info
        getLunarCir (t0, ll, cir);
        printf ("Moon at %g %g @ %ld = %d %d %d %d %d %d:\n", ll.lat_d, ll.lng_d,
                    t0, tms.tm_year+1900, tms.tm_mon+1, tms.tm_mday, tms.tm_hour, tms.tm_min, tms.tm_sec);
        printf ("  El:   %6.2f %s\n", rad2deg(cir.el), prradhexa(cir.el));
        printf ("  Az:   %6.2f %s\n", rad2deg(cir.az), prradhexa(cir.az));
        printf ("  RA:   %6.2f %s\n", rad2deg(cir.ra), prradhexa(cir.ra/15));
        printf ("  Dec:  %6.2f %s\n", rad2deg(cir.dec), prradhexa(cir.dec));
        printf ("  GHA:  %6.2f %s\n", rad2deg(cir.gha), prradhexa(cir.gha/15));
        printf ("  Vel:  %6.2f\n", cir.vel);
        printf ("  Dist: %6.4f %6.0f\n", cir.dist, cir.dist*376341);

        // find moon rise and set, if any
        getLunarRS (t0, ll, &rt, &st);
        if (rt && st) {
            printf ("  Rise: %s", ctime (&rt));
            printf ("  Set:  %s", ctime (&st));
        } else {
            printf ("  No R/S %ld %ld\n", rt, st);
        }

        return (0);
}

#endif // _UNIT_TEST
