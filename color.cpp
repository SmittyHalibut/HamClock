#include "HamClock.h"

/* https://stackoverflow.com/questions/3018313/algorithm-to-convert-rgb-to-hsv-and-hsv-to-rgb-in-range-0-255-for-both Leszek Szary
 */

/* HSV to RGB conversion function with only integer math.
 */
void hsvtorgb(uint8_t *r, uint8_t *g, uint8_t *b, uint8_t h, uint8_t s, uint8_t v)
{
    uint8_t region, remainder, p, q, t;
    
    if(s == 0) {
        /* color is grayscale */
        *r = *g = *b = v;
        return;
    }
    
    /* make hue 0-5 */
    region = h / 43;
    /* find remainder part, make it from 0-255 */
    remainder = (h - (region * 43)) * 6;
    
    /* calculate temp vars, doing integer multiplication */
    p = (v * (255 - s)) >> 8;
    q = (v * (255 - ((s * remainder) >> 8))) >> 8;
    t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;
        
    /* assign temp vars based on color cone region */
    switch(region) {
        case 0:
            *r = v; *g = t; *b = p; break;
        case 1:
            *r = q; *g = v; *b = p; break;
        case 2:
            *r = p; *g = v; *b = t; break;
        case 3:
            *r = p; *g = q; *b = v; break;
        case 4:
            *r = t; *g = p; *b = v; break;
        default:
            *r = v; *g = p; *b = q; break;
    }
    
    return;
}

/* RGB to HSV conversion function with only integer math.
 */
void rgbtohsv(uint8_t *h, uint8_t *s, uint8_t *v, uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t rgbMin, rgbMax;

    rgbMin = r < g ? (r < b ? r : b) : (g < b ? g : b);
    rgbMax = r > g ? (r > b ? r : b) : (g > b ? g : b);

    *v = rgbMax;
    if (*v == 0)
    {
        *h = 0;
        *s = 0;
        return;
    }

    *s = 255U * (rgbMax - rgbMin) / *v;
    if (*s == 0)
    {
        *h = 0;
        return;
    }

    if (rgbMax == r)
        *h = 0 + 43 * (g - b) / (rgbMax - rgbMin);
    else if (rgbMax == g)
        *h = 85 + 43 * (b - r) / (rgbMax - rgbMin);
    else
        *h = 171 + 43 * (r - g) / (rgbMax - rgbMin);
}
