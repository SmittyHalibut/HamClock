/* interface that allows direct usage of output files created by fontconvert tool without wrangling.
 */

#include "HamClock.h"

void selectFontStyle (FontWeight w, FontSize s)
{
    if (s == SMALL_FONT) {
        if (w == BOLD_FONT)
            tft.setFont(&Germano_Bold16pt7b);
        else
            tft.setFont(&Germano_Regular16pt7b);
    } else if (s == LARGE_FONT) {
        if (w == BOLD_FONT)
            tft.setFont(&Germano_Bold30pt7b);
        else
            tft.setFont(&Germano_Bold30pt7b);
    } else /* FAST_FONT */ {
        tft.setFont(NULL);
    }
}
