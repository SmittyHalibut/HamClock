#ifndef _P13_H
#define _P13_H

//
// Plan13.cpp
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


//----------------------------------------------------------------------

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>

//----------------------------------------------------------------------

// the original BASIC code used three variables (e.g. Ox, Oy, Oz) to
// represent a vector quantity.  I think that makes for slightly more
// obtuse code, so I going to collapse them into a single variable 
// which is an array of three elements

typedef float Vec3[3] ;


extern float RADIANS(float deg);
extern float DEGREES(float rad);


//----------------------------------------------------------------------

class DateTime {
public:
    long DN ;
    float TN ;
    DateTime(int year, uint8_t month, uint8_t day, uint8_t h, uint8_t m, uint8_t s) ;
    DateTime(const DateTime &) ;
    DateTime() ;
    ~DateTime() { } 
    DateTime& operator= (const DateTime &source) ;
    void settime(int year, uint8_t month, uint8_t day, uint8_t h, uint8_t m, uint8_t s) ;
    void gettime(int& year, uint8_t& mon, uint8_t& day, uint8_t& h, uint8_t& m, uint8_t& s) ;
    bool operator< (const DateTime &rhs) const;
    bool operator> (const DateTime &rhs) { return (rhs < *this); }
    DateTime operator+ (long seconds);
    DateTime &operator+= (long seconds);
    DateTime operator+ (float days);
    DateTime &operator+= (float days);
    double operator- (const DateTime &rhs) const;

} ;

//----------------------------------------------------------------------

class Observer {
public:
    float LA ;
    float LO ;
    float HT ;
    Vec3 U, E, N, O, V  ;
    
    Observer(float, float, float) ;
    ~Observer() { } ;
} ;

//----------------------------------------------------------------------


class Sun {
public:
	Vec3 SUN, H ;
	Sun() ;
	~Sun() { } ;
        void predict(const DateTime &dt) ;
} ;

//----------------------------------------------------------------------

class Satellite { 
  	long N ;
	long YE ;	
	float IN ;
	float RA ;
	float EC ;
	float WP ;
	float MA ;
	float MM ;
	float M2 ;
	float RV ;


	// these values are stored, but could be calculated on the fly
	// during calls to predict() 
	// classic space/time tradeoff

        float N0, A_0, B_0 ;
        float PC ;
        float QD, WD, DC ;
        float RS ;

public:
        long DE ;
	float TE ;

	Vec3 SAT, VEL ;		// celestial coordinates
    	Vec3 S, V ; 		// geocentric coordinates
 
	Satellite() { } ;
	Satellite(const char *l1, const char *l2) ;
	~Satellite() ;
        void tle(const char *l1, const char *l2) ;
        void predict(const DateTime &dt) ;
	bool eclipsed(Sun *sp);
	void topo(const Observer *obs, float &alt, float &az, float &range, float &range_rate);
	void geo(float &lat, float &lng);
        void celest (float &lat, float &lng);
	float period (void);
	float viewingRadius(float alt);
	DateTime epoch(void);

} ;

#endif // _P13_H
