// Font structures for newer Adafruit_GFX (1.1 and later).
// Example fonts are included in 'Fonts' directory.
// To use a font in your Arduino sketch, #include the corresponding .h
// file and pass address of GFXfont struct to setFont().  Pass NULL to
// revert to 'classic' fixed-space bitmap font.

#ifndef _GFXFONT_H_
#define _GFXFONT_H_

typedef struct { // Data stored PER GLYPH
	uint32_t bitmapOffset;      // Pointer into GFXfont->bitmap
	uint16_t  width, height;    // Bitmap dimensions in pixels
	uint16_t  xAdvance;         // Distance to advance cursor (x axis)
	int16_t   xOffset, yOffset; // Dist from cursor pos to UL corner
} GFXglyph;

typedef struct { // Data stored for FONT AS A WHOLE:
	uint8_t  *bitmap;      // Glyph bitmaps, concatenated
	GFXglyph *glyph;       // Glyph array
	uint16_t   first, last; // ASCII extents
	uint16_t   yAdvance;   // Newline distance (y axis)
} GFXfont;

#endif // _GFXFONT_H_
