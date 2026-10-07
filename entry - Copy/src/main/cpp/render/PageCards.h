/*
 * The three page-level pieces that are neither trend cards nor detail blocks: the alert card, the
 * precipitation nowcast, and the footer attribution line.
 *
 * Ports ArkUIApp's view/main/AlertCard.ets, MinutelyCard.ets and FooterCard.ets.
 * MainAdapter.kt:107-115 puts the alert card directly under the header, ahead of every reorderable
 * card, then the nowcast; kt:173 appends the footer unconditionally as the last row.
 */
#ifndef BREEZY_RENDER_PAGECARDS_H
#define BREEZY_RENDER_PAGECARDS_H

#include <native_drawing/drawing_canvas.h>

#include <cstdint>
#include <string>
#include <vector>

#include "model/Weather.h"

namespace breezy {

struct PageCardColors {
    uint32_t cardBackground;
    /** ?attr/colorSurfaceInverse — titles, and the footer text over the weather background. */
    uint32_t onSurfaceInverse;
    uint32_t bodyText;
    /** colorOutline — the nowcast's axis and threshold lines. */
    uint32_t line;
    /** colorTextGrey — the nowcast's axis labels. */
    uint32_t label;
    /** getThemeColors(WEATHER_KIND_RAINY, …)[0] — the nowcast columns. */
    uint32_t column;
    /** @color/alert_background — the alert card is a filled pill, not a normal white card. */
    uint32_t alertBackground;
    /** onSurface — the headline and the warning triangle. */
    uint32_t alertText;
    /** onSurfaceVariant — the date range under each headline. */
    uint32_t alertDates;
    /** colorPrimary / colorOnPrimary — the nowcast marker's pill and its text (kt:165-167). */
    uint32_t markerBackground;
    uint32_t markerText;
};

/**
 * Where the nowcast chart ended up, so the renderer can hit-test a touch against its buckets.
 *
 * The card decides its own geometry from the text it measures, so the bounds are an OUTPUT of the
 * draw rather than something the caller can compute — the same arrangement the trend lists use.
 */
struct NowcastChartBounds {
    bool valid = false;
    double left = 0.0;
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;
    int buckets = 0;
};

/**
 * PrecipitationNowcastViewHolder.kt:79-254. Returns the height it occupied, or 0 when there is
 * nothing to draw — MainAdapter.kt:110-115 only adds the card when there is minutely precipitation
 * and at least three buckets.
 */
double DrawNowcastCard(OH_Drawing_Canvas* canvas, double leftPx, double topPx, double widthPx,
                       const std::vector<Minutely>& minutely, const PageCardColors& colors,
                       int utcOffsetSeconds, bool is24Hour, int markerIndex = -1,
                       NowcastChartBounds* outBounds = nullptr);

/**
 * AlertViewHolder.kt:62-195. Returns 0 when there are no alerts.
 *
 * Two states, as the Kotlin picks between them (kt:84-88): warnings that have already STARTED are
 * listed one per row; when none has started but some are pending, the card collapses to a single
 * "Alerts to follow" row (kt:96-131). `nowMs` is what separates the two.
 */
double DrawAlertCard(OH_Drawing_Canvas* canvas, double leftPx, double topPx, double widthPx,
                     const std::vector<Alert>& alerts, const PageCardColors& colors,
                     int utcOffsetSeconds, bool is24Hour, int64_t nowMs);

/** FooterViewHolder.kt:115-162 — the attribution line closing the list. */
double DrawFooter(OH_Drawing_Canvas* canvas, double leftPx, double topPx, double widthPx,
                  const std::string& sourceName, uint32_t contentColor);

}  // namespace breezy

#endif  // BREEZY_RENDER_PAGECARDS_H
