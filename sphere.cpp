#include "HamClock.h"


/* solve a spherical triangle:
 *           A
 *          /  \
 *         /    \
 *      c /      \ b
 *       /        \
 *      /          \
 *    B ____________ C
 *           a
 *
 * given A, b, c find B and a in range -PI..B..PI and 0..a..PI, respectively..
 * cap and Bp may be NULL if not interested in either one.
 * N.B. we pass in cos(c) and sin(c) because in many problems one of the sides
 *   remains constant for many values of A and b.
 */
void solveSphere (float A, float b, float cc, float sc, float *cap, float *Bp)
{
        float cb = cosf(b), sb = sinf(b);
        float sA, cA = cosf(A);
        float x, y;
        float ca;
        float B;

        ca = cb*cc + sb*sc*cA;
        if (ca >  1.0F) ca =  1.0F;
        if (ca < -1.0F) ca = -1.0F;
        if (cap)
            *cap = ca;

        if (!Bp)
            return;

        if (sc < 1e-7F)
            B = cc < 0 ? A : M_PIF-A;
        else {
            sA = sinf(A);
            y = sA*sb*sc;
            x = cb - ca*cc;
            B = y ? (x ? atan2f(y,x) : (y>0 ? M_PI_2F : -M_PI_2F)) : (x>=0 ? 0 : M_PIF);
        }

        *Bp = B;
}

