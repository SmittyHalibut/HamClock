/* manage the PLOT_CH_MOON option
 */

#include "HamClock.h"

#define TEXT_COLOR      RA8875_CYAN

static void drawMoonImage (const SBox &b)
{
    // prep
    prepPlotBox (b);

    float phase = lunar_cir.phase;
    // printf ("Phase %g deg\n", rad2deg(phase));

    const uint16_t mr = HC_MOON_W/2;                        // moon radius on output device
    uint16_t mcx = tft.SCALESZ*(b.x+b.w/2);                 // moon center x "
    uint16_t mcy = tft.SCALESZ*(b.y+b.h/2);                 // moon center y "
    int pix_i = 0;                                          // moon_image index
    for (int16_t dy = -mr; dy < mr; dy++) {                 // scan top-to-bot, matching image
        float Ry = sqrtf(mr*mr-dy*dy);                      // moon circle half-width at y
        int16_t Ryi = floorf(Ry+0.5F);                      // " as int
        for (int16_t dx = -mr; dx < mr; dx++) {             // scan left-to-right, matching image
            uint16_t pix = pgm_read_word(&moon_image[pix_i++]);  // next pixel
            if (dx > -Ryi && dx < Ryi) {                    // if inside moon circle
                float a = acosf((float)dx/Ryi);             // looking down from NP CW from right limb
                if (isnan(a) || (phase > 0 && a > phase) || (phase < 0 && a < phase+M_PIF))
                    pix = RGB565(RGB565_R(pix)/3, RGB565_G(pix)/3, RGB565_B(pix)/3); // unlit side
                tft.drawSubPixel (mcx+dx, mcy+dy, pix);
            }
            if ((dy%50) == 0)
                resetWatchdog();
        }
    }
}

/* update moon pane info if likely changed or force.
 * image is in moon_image[HC_MOON_W*HC_MOON_H].
 * N.B. in deciding whether to update we only consider changes in nowWO, not de_ll or lunar_cir.
 */
void updateMoonPane (bool force)
{
        #define INFO_DT         30              // seconds change to update numeric info
        #define IMG_DT          1800            // seconds change to update image
        static time_t last_info_update;         // last nowWO we updated info
        static time_t last_img_update;          // last nowWO we updated image

        // skip altogether if pane not selected
        PlotPane moonpp = findPaneChoiceNow (PLOT_CH_MOON);
        if (moonpp == PANE_NONE)
            return;
        const SBox &box = plot_b[moonpp];

        // skip if too soon unless force
        time_t t0 = nowWO();
        bool update_info = force || labs(t0 - last_info_update) > INFO_DT;
        bool update_img = force || labs(t0 - last_img_update) > IMG_DT;
        if (!update_info && !update_img)
            return;

        // keep the strings so we can erase them exactly next time; using boxes cut chits from moon
        static char az_str[10];
        static char el_str[10];
        static char rs_str[10];
        static char rt_str[10];

        resetWatchdog();

        selectFontStyle (LIGHT_FONT, FAST_FONT);

        if (update_img) {

            // this also erases the stats so must update info too
            drawMoonImage(box);
            update_info = true;

            // record update time
            last_img_update = t0;
        }

        if (update_info) {

            // squeeze in some interesting info in the corners; no color worked overlaying the lit moon

            if (!update_img) {
                // no pane erase so erase previous individual stats
                tft.setTextColor (RA8875_BLACK);
                tft.setCursor (box.x+1, box.y+2);
                tft.print (az_str);
                tft.setCursor (box.x+box.w-getTextWidth(el_str)-1, box.y+2);
                tft.print (el_str);
                tft.setCursor (box.x+1, box.y+box.h-10);
                tft.print (rs_str);
                tft.setCursor (box.x+box.w-getTextWidth(rt_str)-1, box.y+box.h-10);
                tft.print (rt_str);
            }

            tft.setTextColor (TEXT_COLOR);

            snprintf (az_str, sizeof(az_str), "Az: %.0f", rad2deg(lunar_cir.az));
            tft.setCursor (box.x+1, box.y+2);
            tft.print (az_str);

            snprintf (el_str, sizeof(el_str), "El: %.0f", rad2deg(lunar_cir.el));
            tft.setCursor (box.x+box.w-getTextWidth(el_str)-1, box.y+2);
            tft.print (el_str);

            // show which ever rise or set event comes next
            time_t rise, set;
            getLunarRS (t0, de_ll, &rise, &set);
            if (rise > t0 && (set < t0 || rise - t0 < set - t0))
                snprintf (rs_str, sizeof(rs_str), "R@%02d:%02d", hour(rise+de_tz.tz_secs),
                                                                  minute (rise+de_tz.tz_secs));
            else if (set > t0 && (rise < t0 || set - t0 < rise - t0))
                snprintf (rs_str, sizeof(rs_str), "S@%02d:%02d", hour(set+de_tz.tz_secs),
                                                                  minute (set+de_tz.tz_secs));
            else {
                Serial.printf (_FX("No R/S %ld : %ld %ld\n"), t0, rise, set);
                strcpy (rs_str, "No R/S");
            }
            tft.setCursor (box.x+1, box.y+box.h-10);
            tft.print (rs_str);

            snprintf (rt_str, sizeof(rt_str), "%.0f m/s", lunar_cir.vel);;
            tft.setCursor (box.x+box.w-getTextWidth(rt_str)-1, box.y+box.h-10);
            tft.print (rt_str);

            // record update time
            last_info_update = t0;
        }
}
