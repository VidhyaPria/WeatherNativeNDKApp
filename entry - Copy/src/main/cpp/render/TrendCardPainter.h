/*
 * The daily trend card.
 *
 * Ports, together, what ArkUIApp splits across view/main/TrendCard.ets (chrome), TrendTabGroup.ets
 * (the chips), view/trend/TrendItem.ets (a column) and PolylineHistogramCanvas.ets (the chart) —
 * because on this side there are no components to split across, only draw calls.
 *
 * Android layouts: container_main_daily_trend_card.xml, DailyTrendItemView.kt:99-153,
 * PolylineAndHistogramView.kt:101-365.
 */
#ifndef BREEZY_RENDER_TRENDCARDPAINTER_H
#define BREEZY_RENDER_TRENDCARDPAINTER_H

#include <native_drawing/drawing_canvas.h>

#include <cstdint>
#include <string>
#include <vector>

#include "view/TrendData.h"

namespace breezy {

/** The colours a card resolves once from the theme and the current weather. */
struct TrendCardColors {
    uint32_t cardBackground;
    uint32_t onSurfaceInverse;
    uint32_t accentText;
    uint32_t bodyText;
    uint32_t captionText;
    /** PolylineAndHistogramView colours (DailyTemperatureAdapter.kt:142-166). */
    uint32_t highLine;
    uint32_t lowLine;
    uint32_t subLine;
    uint32_t shadowTop;
    uint32_t highText;
    uint32_t lowText;
    uint32_t histogramText;
    double histogramAlpha;
    /** The chips are Material buttons: a filled container with its own label colour. */
    uint32_t chipContainer;
    uint32_t chipSelectedContainer;
    uint32_t chipLabel;
    uint32_t chipSelectedLabel;
    /**
     * screen 3c's `trend_horizontal_line_switch` (MainScreenSettingsScreen.kt:139-157).
     *
     * The lines are still BUILT when this is off — TrendData keeps producing them, exactly as the
     * Kotlin's adapters do — they are simply not painted. It rides here rather than in TrendSeries
     * because it is a render-time choice, and because a change to it has to invalidate the cached
     * columns that bake the rules in (see Renderer's `seriesGeneration_`).
     */
    bool keyLinesEnabled = true;
};

struct TrendCardSpec {
    std::string title;
    /** The rawfile name of the title icon — ic_calendar.svg for daily. */
    std::string titleIcon;
    std::vector<std::string> tabs;
    int selectedTab = 0;
    /** dimens.xml — 298 vp for the daily card, 206 vp for the hourly one. */
    float listHeightVp = 298.0f;
    bool isDaily = true;
    /** Whether the ⋮ menu is open on this card. */
    bool overflowOpen = false;
};

/** Where a chip ended up, so a tap can be matched back to its tab. */
struct ChipBounds {
    int tabIndex = 0;
    double left = 0.0;
    double right = 0.0;
    double top = 0.0;
    double bottom = 0.0;
};

/**
 * Draws the card with its top-left at (xPx, yPx) across `widthPx`, and returns the height it
 * occupied — including the 8 dp margin the card carries on every side.
 *
 * `chips`, when given, is filled with the chip rectangles in the same coordinates the card was
 * drawn in, which is how the tap handler finds them without a second layout pass.
 *
 * `visibleTop`/`visibleBottom` are the part of the page that is actually on screen, in the same
 * coordinates as `yPx`. A card can be on screen while its column list is not — the hourly card sits
 * just below the fold with only its title showing for the whole of a daily-list fling — and its
 * seven charts are the single most expensive thing left in the frame. Pass the whole page's range
 * to draw unconditionally.
 *
 * `generation` identifies the content of the columns: the cache in ColumnCache.h keeps each
 * rendered column as a bitmap and only re-renders when this changes, so a fling blits rather than
 * rasterises. Bump it for anything that alters what a column looks like.
 */
double DrawTrendCard(OH_Drawing_Canvas* canvas, double xPx, double yPx, double widthPx,
                     const TrendCardSpec& spec, const TrendSeries& series,
                     const TrendCardColors& colors, double scrollOffsetPx,
                     std::vector<ChipBounds>* chips, double visibleTop, double visibleBottom,
                     uint64_t generation, bool deferOverflow = false);

/**
 * The tab overflow menu, on its own so the page can draw it AFTER every card.
 *
 * Drawing it inside `DrawTrendCard` put it underneath the cards that come next — with seven hidden
 * tabs the menu is taller than the card, and the detail blocks below simply painted over it. It is
 * not a clipping problem (the card's clip is already restored before it draws); it is z-order, and
 * the only fix is to draw it last. ArkUIApp is unaffected because its menu is a real `bindMenu`
 * popup that floats above the page.
 *
 * Pass `deferOverflow = true` to DrawTrendCard and then call this once, after the last card.
 */
void DrawTrendOverflowMenu(OH_Drawing_Canvas* canvas, const TrendCardSpec& spec,
                           const TrendCardColors& colors, const ChipBounds& anchor,
                           double pageWidth, std::vector<ChipBounds>* chips);

}  // namespace breezy

#endif  // BREEZY_RENDER_TRENDCARDPAINTER_H
