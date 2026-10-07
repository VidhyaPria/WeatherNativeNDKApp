/*
 * The half-width detail block grid.
 *
 * Ports ArkUIApp's view/main/HalfBlockCard.ets together with ArcProgressCanvas, AstroArcCanvas and
 * MoonPhaseCanvas — one file here, because on this side there are no components to split across.
 *
 * Layout from container_main_humidity.xml, shared by every half-size block: a square card, inset
 * by its own guideline percentages, with the icon and title at the top, the big number below, and
 * an optional secondary row under that. Circular blocks centre all three; rectangular blocks
 * left-align them.
 */
#ifndef BREEZY_RENDER_DETAILBLOCKPAINTER_H
#define BREEZY_RENDER_DETAILBLOCKPAINTER_H

#include <native_drawing/drawing_canvas.h>

#include <cstdint>
#include <vector>

#include "view/DetailBlocks.h"

namespace breezy {

struct DetailBlockColors {
    uint32_t cardBackground;
    /** ?attr/colorSurfaceInverse — the icon, title and big number. */
    uint32_t content;
    /** ?attr/colorOnSurfaceVariant — the secondary row. */
    uint32_t secondary;
    /* The humidity block's dew-point badge — see kTertiary in Theme.h. */
    uint32_t tertiary;
    uint32_t onTertiary;
    uint32_t onTertiaryContainer;
    bool darkMode;
};

/**
 * Draws the whole grid, two per row, starting at `topPx`, and returns the height it occupied.
 * `is24Hour` picks the rise/set clock format on the astro blocks.
 */
double DrawDetailBlocks(OH_Drawing_Canvas* canvas, double topPx, double widthPx,
                        const std::vector<DetailBlock>& blocks, const DetailBlockColors& colors,
                        int utcOffsetSeconds, bool is24Hour);

/** One block in its own square — the page lays the grid out itself so blocks can be reordered. */
/**
 * `MoonPhaseView.onDraw` — the lit fraction of the disc for a phase angle, inset 4 dp inside its
 * square. `angle` is the head of the lit surface clockwise: 0 new, 90 first quarter, 180 full.
 *
 * Used by the main screen's astro block and by the Sun & moon detail card's fourth `ListItem`,
 * whose leading content is a MoonPhaseView rather than an image (DetailsSunMoon.kt:534-548).
 */
void DrawMoonPhase(OH_Drawing_Canvas* canvas, double left, double top, double size, int angle,
                   uint32_t light, uint32_t dark, uint32_t stroke);

void DrawDetailBlock(OH_Drawing_Canvas* canvas, const DetailBlock& block, double left, double top,
                     double side, const DetailBlockColors& colors, int utcOffsetSeconds,
                     bool is24Hour);

}  // namespace breezy

#endif  // BREEZY_RENDER_DETAILBLOCKPAINTER_H
