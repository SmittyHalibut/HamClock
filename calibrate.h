/*
 *
 *   Copyright (c) 2001, Carlos E. Vidales. All rights reserved.
 *
 *   This sample program was written and put in the public domain 
 *    by Carlos E. Vidales.  The program is provided "as is" 
 *    without warranty of any kind, either expressed or implied.
 *   If you choose to use the program within your own products
 *    you do so at your own risk, and assume the responsibility
 *    for servicing, repairing or correcting the program should
 *    it prove defective in any manner.
 *   You may copy and distribute the program's source code in any 
 *    medium, provided that you also include in each copy an
 *    appropriate copyright notice and disclaimer of warranty.
 *   You may also modify this program and distribute copies of
 *    it provided that you include prominent notices stating 
 *    that you changed the file(s) and the date of any change,
 *    and that you do not charge any royalties or licenses for 
 *    its use.
 * 
 *
 *   File Name:  calibrate.h
 *
 *
 *   Definition of constants and structures, and declaration of functions 
 *    in Calibrate.c
 *
 */

#ifndef _CALIBRATE_H_

#define _CALIBRATE_H_

/****************************************************/
/*                                                  */
/* Included files                                   */
/*                                                  */
/****************************************************/

#include <stdint.h>
#include <math.h>


/****************************************************/
/*                                                  */
/* Definitions                                      */
/*                                                  */
/****************************************************/

#ifndef		_CALIBRATE_C_
	#define		EXTERN         extern
#else
	#define		EXTERN
#endif



#ifndef		OK
	#define		OK		        0
	#define		NOT_OK		   	(-1)
#endif



#define			INT32			uint32_t




/****************************************************/
/*                                                  */
/* Structures                                       */
/*                                                  */
/****************************************************/


typedef struct {
                        INT32    x,
                                 y ;
                     } CAL_POINT ;



typedef struct {
							/* This arrangement of values facilitates 
							 *  calculations within getDisplayPoint() 
							 */
                        INT32    An,     /* A = An/Divider */
                                 Bn,     /* B = Bn/Divider */
                                 Cn,     /* C = Cn/Divider */
                                 Dn,     /* D = Dn/Divider */
                                 En,     /* E = En/Divider */
                                 Fn,     /* F = Fn/Divider */
                                 Divider ;
                     } CAL_MATRIX ;




/****************************************************/
/*                                                  */
/* Function declarations                            */
/*                                                  */
/****************************************************/


EXTERN int setCalibrationMatrix( const CAL_POINT display[3],
                                 const CAL_POINT screen[3],
                                 CAL_MATRIX * matrix) ;


EXTERN int getDisplayPoint( CAL_POINT * display,
                            const CAL_POINT * screen,
                            const CAL_MATRIX * matrix ) ;


#endif  /* _CALIBRATE_H_ */
