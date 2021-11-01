//
// P13.cpp
//
// An implementation of Plan13 in C++ by Mark VandeWettering
//
// Plan13 is an algorithm for satellite orbit prediction first formulated
// by James Miller G3RUH.  I learned about it when I saw it was the basis 
// of the PIC based antenna rotator project designed by G6LVB.
//
// http://www.g6lvb.com/Articles/LVBTracker2/index.htm
//
// I ported the algorithm to Python, and it was my primary means of orbit
// prediction for a couple of years while I operated the "Easy Sats" with 
// a dual band hand held and an Arrow antenna.
//
// I've long wanted to redo the work in C++ so that I could port the code
// to smaller processors including the Atmel AVR chips.  Bruce Robertson,
// VE9QRP started the qrpTracker project to fufill many of the same goals,
// but I thought that the code could be made more compact and more modular,
// and could serve not just the embedded targets but could be of more
// use for more general applications.  And, I like the BSD License a bit
// better too.
//
// So, here it is!
//

// Added a few select DateTime overloaded operators and _DATETIME_UNITTEST      -- ECD

#include "P13.h"

// here are a bunch of constants that will be used throughout the
// code, but which will probably not be helpful outside.

// Updated with 2014 values from
// http://www.amsat.org/amsat/articles/g3ruh/111.html

static const float RE = 6378.137f ;
static const float FL = 1.f/298.257224f ;
static const float GM = 3.986E5f ;
static const float J2 = 1.08263E-3f ;
static const float YM = 365.25f ;
static const float YT = 365.2421874f ;
static const float WW = 2.f*M_PI/YT ;
static const float WE = 2.f*M_PI+ WW ;
static const float W0 = WE/86400.f ;
static const float YG = 2014.f ;
static const float G0 = 99.5828f ;
static const float MAS0 = 356.4105f ;
static const float MASD = 0.98560028f ;
static const float EQC1 = 0.03340 ;
static const float EQC2 = 0.00035 ;
static const float INS = (23.4375f)*M_PI/180.0 ;
static const float CNS = cosf(INS) ;
static const float SNS = sinf(INS) ;


float
RADIANS(float deg)
{
    return deg * M_PI / 180. ;
}

float
DEGREES(float rad)
{
    return rad * 180. / M_PI ;
}


//----------------------------------------------------------------------
//     _              ___       _      _____ _           
//  __| |__ _ ______ |   \ __ _| |_ __|_   _(_)_ __  ___ 
// / _| / _` (_-<_-< | |) / _` |  _/ -_)| | | | '  \/ -_)
// \__|_\__,_/__/__/ |___/\__,_|\__\___||_| |_|_|_|_\___|
//                                                       
//----------------------------------------------------------------------

static long
fnday(int y, uint8_t m, uint8_t d)
{
    if (m < 3) {
        m += 12 ;
        y -- ;
    }
    return (long) (y * YM) + (long) ((m+1)*30.6f) + (long)d - 428L ;
}

static void
fndate(int &y, uint8_t &m, uint8_t &d, long dt)
{
    dt += 428L ;
    y = (int) ((dt-122.1F)/365.25F) ;
    dt -= (long) (y*365.25F) ;
    m = (uint8_t) (dt / 30.61F) ;
    dt -= (long) (m*30.6F) ;
    m -- ;
    if (m > 12) {
        m -= 12 ;
        y++ ;
    }
    d = dt ;
}

DateTime::DateTime(int year, uint8_t month, uint8_t day, uint8_t h, uint8_t m, uint8_t s) 
{
    settime(year, month, day, h, m, s) ;
}


// copy constructor
DateTime::DateTime(const DateTime &dt)
{
    DN = dt.DN ;
    TN = dt.TN ;
}

// default constructor
DateTime::DateTime()
{
   DN = 0L ;
   TN = 0 ;
}

// overload assignment
DateTime &DateTime::operator= (const DateTime &source)
{
    DN = source.DN;
    TN = source.TN;
    return *this;
}

// overload <
bool DateTime::operator< (const DateTime &rhs) const
{
    return (rhs - *this > 0);
}

// overload + seconds
DateTime DateTime::operator+ (long seconds)
{
    DateTime t(*this);
    t += seconds;
    return (t);
}

// overload += seconds
DateTime &DateTime::operator+= (long seconds)
{
    TN += seconds/(24.0F*3600.0F);
    DN += (long) TN ;
    TN -= (long) TN ;
    return (*this);
}

// overload + days
DateTime DateTime::operator+ (float days)
{
    DateTime t(*this);
    t += days;
    return (t);
}

// overload += days
DateTime &DateTime::operator+= (float days)
{
    TN += days ;
    DN += (long) TN ;
    TN -= (long) TN ;
    return (*this);
}

// overload - to yield difference in days
// double to retain full precision
double DateTime::operator- (const DateTime &rhs) const
{
    long ddn = DN - rhs.DN;
    double dtn = TN - rhs.TN;
    return (ddn + dtn);
}


void
DateTime::gettime(int &year, uint8_t &month, uint8_t &day, uint8_t &h, uint8_t &m, uint8_t &s)
{
    fndate(year, month, day, DN) ;
    float t = TN ;
    t *= 24.F ;
    h = (uint8_t) t ;
    t -= h ;
    t *= 60 ;
    m = (uint8_t) t ;
    t -= m ;
    t *= 60 ;
    s = (uint8_t) (t+0.5F) ;
    if (s == 60)
        s = 59;

}

void
DateTime::settime(int year, uint8_t month, uint8_t day, uint8_t h, uint8_t m, uint8_t s) 
{
    DN = fnday(year, month, day) ;
    TN = ((float) h + m / 60.F + s / 3600.F) / 24.F ;
}

//----------------------------------------------------------------------
//     _               ___  _                            
//  __| |__ _ ______  / _ \| |__ ___ ___ _ ___ _____ _ _ 
// / _| / _` (_-<_-< | (_) | '_ (_-</ -_) '_\ V / -_) '_|
// \__|_\__,_/__/__/  \___/|_.__/__/\___|_|  \_/\___|_|  
//                                                      
//----------------------------------------------------------------------

Observer::Observer(float lat, float lng, float hgt)
{
    LA = RADIANS(lat) ;
    LO = RADIANS(lng) ;
    HT = hgt / 1000 ;

    U[0] = cosf(LA)*cosf(LO) ;
    U[1] = cosf(LA)*sinf(LO) ;
    U[2] = sinf(LA) ;

    E[0] = -sinf(LO) ;
    E[1] =  cosf(LO) ;
    E[2] =  0.F ;

    N[0] = -sinf(LA)*cosf(LO) ;
    N[1] = -sinf(LA)*sinf(LO) ;
    N[2] =  cos(LA) ;

    float RP = RE * (1 - FL) ;
    float XX = RE * RE ;
    float ZZ = RP * RP ;
    float D = sqrt(XX*cosf(LA)*cosf(LA) + 
                    ZZ*sinf(LA)*sinf(LA)) ;
    float Rx = XX / D + HT ;
    float Rz = ZZ / D + HT ;

    O[0] = Rx * U[0] ;
    O[1] = Rx * U[1] ;
    O[2] = Rz * U[2] ;

    V[0] = -O[1] * W0 ;
    V[1] =  O[0] * W0 ;
    V[2] =  0 ;
}

//----------------------------------------------------------------------
//     _              ___       _       _ _ _ _       
//  __| |__ _ ______ / __| __ _| |_ ___| | (_) |_ ___ 
// / _| / _` (_-<_-< \__ \/ _` |  _/ -_) | | |  _/ -_)
// \__|_\__,_/__/__/ |___/\__,_|\__\___|_|_|_|\__\___|
//
//----------------------------------------------------------------------

static float
getfloat(const char *c, int i0, int i1)
{
    char buf[20] ;
    int i ;
    for (i=0; i0+i<i1; i++) 
        buf[i] = c[i0+i] ;
    buf[i] = '\0' ;
    return strtod(buf, NULL) ;
}

static long
getlong(const char *c, int i0, int i1)
{
    char buf[20] ;
    int i ;
    for (i=0; i0+i<i1; i++) 
        buf[i] = c[i0+i] ;
    buf[i] = '\0' ;
    return atol(buf) ;
}

Satellite::Satellite(const char *l1, const char *l2)
{
    tle(l1, l2) ;
}

Satellite::~Satellite()
{
}

void
Satellite::tle(const char *l1, const char *l2)
{
    // direct quantities from the orbital elements

    N = getlong(l2, 2, 7) ;
    YE = getlong(l1, 18, 20) ;
    if (YE < 58)
        YE += 2000 ;
    else
        YE += 1900 ;

    TE = getfloat(l1, 20, 32) ;
    M2 = RADIANS(getfloat(l1, 33, 43)) ;

    IN = RADIANS(getfloat(l2, 8, 16)) ;
    RA = RADIANS(getfloat(l2, 17, 25)) ;
    EC = getfloat(l2, 26, 33)/1e7f ;
    WP = RADIANS(getfloat(l2, 34, 42)) ;
    MA = RADIANS(getfloat(l2, 43, 51)) ;
    MM = 2.0f * M_PI * getfloat(l2, 52, 63) ;
    RV = getlong(l2, 63, 68) ;

    // derived quantities from the orbital elements 

    // convert TE to DE and TE 
    DE = fnday(YE, 1, 0) + (long) TE ;
    TE -= (long) TE ;
    N0 = MM/86400 ;
    A_0 = pow(GM/(N0*N0), 1.F/3.F) ;
    B_0 = A_0*sqrt(1.F-EC*EC) ;
    PC = RE*A_0/(B_0*B_0) ;
    PC = 1.5f*J2*PC*PC*MM ;
    float CI = cosf(IN) ;
    QD = -PC*CI ;
    WD =  PC*(5*CI*CI-1)/2 ;
    DC = -2*M2/(3*MM) ;
}
void
Satellite::predict(const DateTime &dt)
{
    long DN = dt.DN ;
    float TN = dt.TN ;

    float TEG = DE - fnday(YG, 1, 0) + TE ;

    float GHAE = RADIANS(G0) + TEG * WE ;

    float T = (float) (DN - DE) + (TN-TE) ;
    float DT = DC * T / 2.F ;
    float KD = 1.F + 4.F * DT ;
    float KDP = 1.F - 7.F * DT ;
  
    float M = MA + MM * T * (1.F - 3.F * DT) ;
    float DR = (long) (M / (2.F * M_PI)) ;
    M -= DR * 2.F * M_PI ;
    float EA = M ;

    float DNOM, C_EA, S_EA ;

    for (;;) {
        C_EA = cosf(EA) ;
        S_EA = sinf(EA) ;
        DNOM = 1.F - EC * C_EA ;
        float D = (EA-EC*S_EA-M)/DNOM ;
        EA -= D ;
        if (fabs(D) < 1e-5)
            break ;
    }

    float A = A_0 * KD ;
    float B = B_0 * KD ;
    RS = A * DNOM ;

    float Vx, Vy ;
    float Sx, Sy ;
    Sx = A * (C_EA - EC) ;
    Sy = B * S_EA ;

    Vx = -A * S_EA / DNOM * N0 ;
    Vy =  B * C_EA / DNOM * N0 ;

    float AP = WP + WD * T * KDP ;
    float CW = cosf(AP) ;
    float SW = sinf(AP) ;

    float RAAN = RA + QD * T * KDP ;
 
    float CQ = cosf(RAAN) ;
    float SQ = sinf(RAAN) ;

    float CI = cosf(IN) ;
    float SI = sinf(IN) ;

    // CX, CY, and CZ form a 3x3 matrix
    // that converts between orbit coordinates,
    // and celestial coordinates.

    Vec3 CX, CY, CZ ;
   
    CX[0] =  CW * CQ - SW * CI * SQ ;
    CX[1] = -SW * CQ - CW * CI * SQ ;
    CX[2] =  SI * SQ ;

    CY[0] =  CW * SQ + SW * CI * CQ ;
    CY[1] = -SW * SQ + CW * CI * CQ ;
    CY[2] = -SI * CQ ;

    CZ[0] = SW * SI ;
    CZ[1] = CW * SI ;
    CZ[2] = CI ;

    // satellite in celestial coords

    SAT[0] = Sx * CX[0] + Sy * CX[1] ;
    SAT[1] = Sx * CY[0] + Sy * CY[1] ;
    SAT[2] = Sx * CZ[0] + Sy * CZ[1] ;

    VEL[0] = Vx * CX[0] + Vy * CX[1] ;
    VEL[1] = Vx * CY[0] + Vy * CY[1] ;
    VEL[2] = Vx * CZ[0] + Vy * CZ[1] ;

    // and in geocentric coordinates

    float GHAA = (GHAE + WE * T) ;
    float CG = cosf(-GHAA) ;
    float SG = sinf(-GHAA) ;

    S[0] = SAT[0] * CG - SAT[1] * SG ;
    S[1] = SAT[0] * SG + SAT[1] * CG ;
    S[2] = SAT[2] ;

    V[0] = VEL[0] * CG - VEL[1]* SG ;
    V[1] = VEL[0] * SG + VEL[1]* CG ;
    V[2] = VEL[2] ;
}

/* find local apparent circumstances
 */
void
Satellite::topo(const Observer *obs, float &alt, float &az, float &range, float &range_rate)
{
    Vec3 R ;
    R[0] = S[0] - obs->O[0] ;
    R[1] = S[1] - obs->O[1] ;
    R[2] = S[2] - obs->O[2] ;
    range = sqrtf(R[0]*R[0]+R[1]*R[1]+R[2]*R[2]) ;
    R[0] /= range ;
    R[1] /= range ;
    R[2] /= range ;

    range_rate = 1000*((V[0]-obs->V[0])*R[0] + (V[1]-obs->V[1])*R[1] + V[2]*R[2]);      // m/s

    float u = R[0] * obs->U[0] + R[1] * obs->U[1] + R[2] * obs->U[2] ;
    float e = R[0] * obs->E[0] + R[1] * obs->E[1] + R[2] * obs->E[2] ;
    float n = R[0] * obs->N[0] + R[1] * obs->N[1] + R[2] * obs->N[2] ;

    az = DEGREES(atan2f(e, n)) ;
    if (az < 0) az += 360.F ;

    alt = DEGREES(asinf(u)) ;

    // Saemundson refraction, true to apparent, 10C 1000 mbar (29.5 inch Hg)
    alt += (1000.0F/1010.0F)*(283.0F/(273.0F+10.0F))*1.02F/tanf(RADIANS(alt + 10.3F/(alt+5.11)))/60.0F;
}

// subsat location
void
Satellite::geo(float &lat, float &lng)
{
    float r = sqrt(S[0]*S[0] + S[1]*S[1]);
    lat = atan2(S[2],r);
    lng = atan2f(S[1],S[0]);
}

// celestial coords
void
Satellite::celest (float &lat, float &lng)
{
    float r = sqrt(SAT[0]*SAT[0] + SAT[1]*SAT[1]);
    lat = atan2(SAT[2],r);
    lng = atan2f(SAT[1],SAT[0]);
}

// period, days
float
Satellite::period()
{
    // MM is radians per day -> 1/MM is days/radian -> mult by 2PI to get days/rev
    return ((2*M_PI)/MM);
}

// return great-circle radius from subsat point to viewing circle at given altitude
float
Satellite::viewingRadius(float alt)
{
    float h = sqrtf(S[0]*S[0] + S[1]*S[1] + S[2]*S[2]);
    return (acosf(RE/h*cosf(alt)) - alt);
}

bool
Satellite::eclipsed(Sun *sp)
{
    float CUA = -(SAT[0]*sp->SUN[0]+SAT[1]*sp->SUN[1]+SAT[2]*sp->SUN[2])/RS;
    float UMD = RS*sqrt(1-CUA*CUA)/RE;
    return (UMD<=1 && CUA>=0);
       
}

DateTime Satellite::epoch()
{
    DateTime dt;
    dt.DN = DE;
    dt.TN = TE;
    return (dt);
}

//----------------------------------------------------------------------

Sun::Sun()
{
}

void
Sun::predict(const DateTime &dt)
{
    long DN = dt.DN ;
    float TN = dt.TN ;

    float T = (float) (DN - fnday(YG, 1, 0)) + TN ;
    float GHAE = RADIANS(G0) + T * WE ;
    float MRSE = RADIANS(G0) + T * WW + M_PI ;
    float MASE = RADIANS(MAS0 + T * MASD) ;
    float TAS = MRSE + EQC1*sinf(MASE) + EQC2*sinf(2.F*MASE) ;
    float C, S ;

    C = cosf(TAS) ;
    S = sinf(TAS) ;
    SUN[0]=C ;
    SUN[1]=S*CNS ;
    SUN[2]=S*SNS ;
    C = cosf(-GHAE) ; 
    S = sinf(-GHAE) ; 
    H[0]=SUN[0]*C - SUN[1]*S ;
    H[1]=SUN[0]*S + SUN[1]*C ;
    H[2]=SUN[2] ;
}


#ifdef _DATETIME_UNITTEST

#include <stdio.h>

#include "P13.h"

int main (int ac, char *av[])
{
    DateTime t0(2019, 1, 1, 0, 0, 0);
    DateTime t1 = t0;
    DateTime t2(2019, 1, 1, 12, 0, 0);

    printf ("t1 < t2:    1 =?= %d\n", t1 < t2);
    printf ("t1 > t2:    0 =?= %d\n", t1 > t2);
    printf ("t2 - t1:  0.5 =?= %g\n", t2 - t1);
    printf ("t1 - t2: -0.5 =?= %g\n", t1 - t2);

    printf ("\nt1 += 2 days\n");
    t1 += 2.0F;

    printf ("t1 < t2:    0 =?= %d\n", t1 < t2);
    printf ("t1 > t2:    1 =?= %d\n", t1 > t2);
    printf ("t1 - t0:    2 =?= %g\n", t1 - t0);
    printf ("t1 - t2:  1.5 =?= %g\n", t1 - t2);
    printf ("t2 - t1: -1.5 =?= %g\n", t2 - t1);

    printf ("\nt2 += %ld seconds\n", 2*24*3600L);
    t2 += 2*24*3600L;

    printf ("t1 < t2:    1 =?= %d\n", t1 < t2);
    printf ("t1 > t2:    0 =?= %d\n", t1 > t2);
    printf ("t1 - t2: -0.5 =?= %g\n", t1 - t2);
    printf ("t2 - t0:  2.5 =?= %g\n", t2 - t0);
    printf ("t2 - t1:  0.5 =?= %g\n", t2 - t1);

    printf ("\nt2 < t1 + -1.0F: 0 =?= %d\n", t2 < t1 + -1.0F);

    return (0);
}

#endif // _DATETIME_UNITTEST
