/* generic modal dialog
 */

#include "HamClock.h"

// basic parameters
// allow setting some/all these in Menu?
#define MENU_TBM        2               // top and bottom margin
#define MENU_RH         13              // row height
#define MENU_IS         6               // indicator size
#define MENU_BH         14              // button height
#define MENU_BDX        2               // button text horizontal offset
#define MENU_BDY        3               // button text vertical offset
#define MENU_TIMEOUT    MENU_TO         // timeout, millis
#define MENU_FGC        RA8875_WHITE    // normal foreground color
#define MENU_BGC        RA8875_BLACK    // normal background color
#define MENU_ERRC       RA8875_RED      // error color
#define MENU_BSYC       RA8875_YELLOW   // busy color

static const char ok_label[] = "Ok";

/* draw selector and possibly the label for the given menu item in the given box
 */
static void menuDrawItem (const MenuItem &mi, const SBox &box, bool draw_label)
{
    // prepare a copy of the label without underscores if drawing
    char *no__copy = NULL;
    if (draw_label) {
        no__copy = strdup (mi.label);
        strncpySubChar (no__copy, mi.label, ' ', '_', strlen(mi.label));
    }

    // draw depending on type
    switch (mi.type) {

    case MENU_IGNORE:
        break;

    case MENU_TITLE:
        if (draw_label) {
            tft.setCursor (box.x + mi.indent, box.y);
            tft.print (no__copy);
        }
        break;

    case MENU_1OFN:
        if (mi.set)
            tft.fillCircle (box.x + mi.indent + MENU_IS/2, box.y + MENU_IS/2 + 1, MENU_IS/2, MENU_FGC);
        else {
            tft.fillCircle (box.x + mi.indent + MENU_IS/2, box.y + MENU_IS/2 + 1, MENU_IS/2, MENU_BGC);
            tft.drawCircle (box.x + mi.indent + MENU_IS/2, box.y + MENU_IS/2 + 1, MENU_IS/2, MENU_FGC);
        }
        if (draw_label) {
            tft.setCursor (box.x + mi.indent + MENU_IS + MENU_IS/2, box.y);
            tft.print (no__copy);
        }
        break;

    case MENU_AL1OFN:   // fallthru
    case MENU_TOGGLE:
        if (mi.set)
            tft.fillRect (box.x + mi.indent, box.y + 1, MENU_IS, MENU_IS, MENU_FGC);
        else {
            tft.fillRect (box.x + mi.indent, box.y + 1, MENU_IS, MENU_IS, MENU_BGC);
            tft.drawRect (box.x + mi.indent, box.y + 1, MENU_IS, MENU_IS, MENU_FGC);
        }
        if (draw_label) {
            tft.setCursor (box.x + mi.indent + MENU_IS + MENU_IS/2, box.y);
            tft.print (no__copy);
        }
        break;
    }

    // clean up
    free ((void*)no__copy);

    // now if just changing indicator
    if (!draw_label)
        tft.drawPR();
}

/* starting with item ii, count how many either side are of the same type and set, including self.
 */
static int menuCountItemsSet (Menu &menu, int ii)
{
    MenuItem &menu_ii = menu.items[ii];
    int n_set = menu_ii.type != MENU_IGNORE && menu_ii.set ? 1 : 0;

    for (int i = ii-1; i >= 0; --i) {
        if (menu.items[i].type == MENU_IGNORE)
            continue;
        if (menu.items[i].type != menu_ii.type)
            break;
        if (menu.items[i].set)
            n_set++;
    }

    for (int i = ii+1; i < menu.n_items; i++) {
        if (menu.items[i].type == MENU_IGNORE)
            continue;
        if (menu.items[i].type != menu_ii.type)
            break;
        if (menu.items[i].set)
            n_set++;
    }

    printf ("nset %d\n", n_set);

    return (n_set);
}

/* starting with item ii, turn off all items either side of the same type, including self.
 */
static void menuItemsAllOff (Menu &menu, SBox *boxes, int ii)
{
    MenuItem &menu_ii = menu.items[ii];

    if (menu_ii.type != MENU_IGNORE && menu_ii.set) {
        menu_ii.set = false;
        menuDrawItem (menu_ii, boxes[ii], false);
    }

    for (int i = ii-1; i >= 0; --i) {
        if (menu.items[i].type == MENU_IGNORE)
            continue;
        if (menu.items[i].type != menu_ii.type)
            break;
        if (menu.items[i].set) {
            menu.items[i].set = false;
            menuDrawItem (menu.items[i], boxes[i], false);
        }
    }

    for (int i = ii+1; i < menu.n_items; i++) {
        if (menu.items[i].type == MENU_IGNORE)
            continue;
        if (menu.items[i].type != menu_ii.type)
            break;
        if (menu.items[i].set) {
            menu.items[i].set = false;
            menuDrawItem (menu.items[i], boxes[i], false);
        }
    }
}

/* operate the given menu in the given box and let op use until ok, cancel, timeout or tap outside a box.
 * caller passes a box we use for ok so they can use it later with menuRedrawOk if necessary.
 * N.B. menu_b height will be set to fit menu, other fields unchanged.
 * return true if op clicked ok else false for all other cases.
 */
bool runMenu (Menu &menu, const SBox &outside_b, SBox &menu_b, SBox &ok_b)
{
    // font
    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setTextColor (MENU_FGC);

    // number of visible menu rows -- not counting MENU_IGNORE and ok/cancel
    int n_vrows = (menu.n_rows + menu.n_cols - 1)/menu.n_cols;

    // menu box
    menu_b.h = MENU_TBM + (n_vrows+1)*MENU_RH + MENU_TBM;
    tft.fillRect (menu_b.x, menu_b.y, menu_b.w, menu_b.h, MENU_BGC);
    tft.drawRect (menu_b.x, menu_b.y, menu_b.w, menu_b.h, MENU_FGC);

    // cancel box
    SBox cancel_b;
    static const char cancel_label[] = "Cancel";
    uint16_t cancel_w = getTextWidth (cancel_label);
    cancel_b.x = menu_b.x + 9*menu_b.w/10 - cancel_w - 4;
    cancel_b.w = cancel_w + MENU_BDX*2;
    cancel_b.h = MENU_BH;
    cancel_b.y = menu_b.y + menu_b.h - MENU_TBM - cancel_b.h;
    tft.fillRect (cancel_b.x, cancel_b.y, cancel_b.w, cancel_b.h, MENU_BGC);
    tft.drawRect (cancel_b.x, cancel_b.y, cancel_b.w, cancel_b.h, MENU_FGC);
    tft.setCursor (cancel_b.x+MENU_BDX, cancel_b.y+MENU_BDY);
    tft.print (cancel_label);

    // ok box
    uint16_t ok_w = getTextWidth (ok_label);
    ok_b.x = menu_b.x + menu_b.w/10;
    ok_b.w = ok_w + MENU_BDX*2;
    ok_b.h = MENU_BH;
    ok_b.y = menu_b.y + menu_b.h - MENU_TBM - ok_b.h;
    tft.fillRect (ok_b.x, ok_b.y, ok_b.w, ok_b.h, MENU_BGC);
    tft.drawRect (ok_b.x, ok_b.y, ok_b.w, ok_b.h, MENU_FGC);
    tft.setCursor (ok_b.x+MENU_BDX, ok_b.y+MENU_BDY);
    tft.print (ok_label);

    // show each non-IGNORE item, creating selection box for each (even IGNORE)
    StackMalloc ibox_mem(menu.n_items*sizeof(SBox));
    SBox *items_b = (SBox *) ibox_mem.getMem();
    uint16_t col_w = menu_b.w/menu.n_cols;
    uint8_t row_i = 0;                          // visual row, only incremented for non-IGNORE
    for (int i = 0; i < menu.n_items; i++) {
        SBox &ib = items_b[i];
        MenuItem &mi = menu.items[i];

        if (mi.type == MENU_IGNORE) {
            // unpickable box
            ib.x = 0;
            ib.y = 0;
            ib.w = 0;
            ib.h = 0;
        } else {
            ib.x = menu_b.x + (row_i/n_vrows)*col_w;
            ib.y = menu_b.y + MENU_TBM + (row_i%n_vrows)*MENU_RH;
            ib.w = col_w;
            ib.h = MENU_RH;
            menuDrawItem (mi, ib, true);
            row_i++;
        }
    }
    if (row_i != menu.n_rows)
        fatalError ("Bug! menu row %d != %d / %d", row_i, menu.n_rows, menu.n_items);

    tft.drawPR();

    // run
    bool ok = false;
    SCoord tap;
    while (waitForTap (menu_b, outside_b, NULL, MENU_TIMEOUT, tap)) {

        // check for tap in ok or cancel
        if (inBox (tap, ok_b)) {
            ok = true;
            break;
        }
        if (inBox (tap, cancel_b)) {
            break;
        }

        // check for tap in menu items
        for (int i = 0; i < menu.n_items; i++) {
            SBox &ib = items_b[i];

            if (inBox (tap, ib)) {
                MenuItem &mi = menu.items[i];

                // implement each type of behavior
                switch (mi.type) {
                case MENU_TITLE:        // fallthru
                case MENU_IGNORE:
                    break;

                case MENU_1OFN:
                    // ignore if already set, else turn this one on and all others in this group off
                    if (!mi.set) {
                        menuItemsAllOff (menu, items_b, i);
                        mi.set = true;
                        menuDrawItem (mi, ib, false);
                    }
                    break;

                case MENU_AL1OFN:
                    // turn on unconditionally, but turn off only if not the last one
                    if (!mi.set) {
                        mi.set = true;
                        menuDrawItem (mi, ib, false);
                    } else {
                        if (menuCountItemsSet (menu, i) > 1) {
                            mi.set = false;
                            menuDrawItem (mi, ib, false);
                        }
                    }
                    break;

                case MENU_TOGGLE:
                    // uncondition change
                    mi.set = !mi.set;
                    menuDrawItem (mi, ib, false);
                    break;
                }

                // tap found
                break;
            }
        }
    }

    drainTouch();

    return (ok);
}

/* redraw the given ok box in the given visual state.
 * used to allow caller to provide busy or error feedback.
 * N.B. we assume ok_b is same as passed to runMenu and remains unchanged since its return.
 */
void menuRedrawOk (SBox &ok_b, MenuOkState oks)
{
    switch (oks) {
    case MENU_OK_OK:
        tft.setTextColor (MENU_FGC);
        tft.fillRect (ok_b.x, ok_b.y, ok_b.w, ok_b.h, MENU_BGC);
        tft.drawRect (ok_b.x, ok_b.y, ok_b.w, ok_b.h, MENU_FGC);
        break;
    case MENU_OK_BUSY:
        tft.setTextColor (MENU_BGC);
        tft.fillRect (ok_b.x, ok_b.y, ok_b.w, ok_b.h, MENU_BSYC);
        tft.drawRect (ok_b.x, ok_b.y, ok_b.w, ok_b.h, MENU_FGC);
        break;
    case MENU_OK_ERR:
        tft.setTextColor (MENU_BGC);
        tft.fillRect (ok_b.x, ok_b.y, ok_b.w, ok_b.h, MENU_ERRC);
        tft.drawRect (ok_b.x, ok_b.y, ok_b.w, ok_b.h, MENU_FGC);
        break;
    }

    selectFontStyle (LIGHT_FONT, FAST_FONT);
    tft.setCursor (ok_b.x+MENU_BDX, ok_b.y+MENU_BDY);
    tft.print (ok_label);
    tft.drawPR();
}
