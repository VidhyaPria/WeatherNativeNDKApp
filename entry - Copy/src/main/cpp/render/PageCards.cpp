#include "render/PageCards.h"

#include <native_drawing/drawing_brush.h>
#include <native_drawing/drawing_path.h>
#include <native_drawing/drawing_pen.h>
#include <native_drawing/drawing_rect.h>
#include <native_drawing/drawing_round_rect.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "common/Formatter.h"
#include "render/AssetImages.h"
#include "render/Shapes.h"
#include "render/TextPainter.h"
#include "render/Theme.h"

namespace breezy {
namespace {

/** Precipitation.kt:42-45 — the hourly intensity thresholds, in mm/h. */
constexpr double kHourlyLight = 5.0;
constexpr double kHourlyMedium = 10.0;
constexpr double kHourlyHeavy = 15.0;

/**
 * container_main_precipitation_nowcast_card chart geometry, in vp.
 *
 * `kChartBoxHeight` is `nowcasting_chart_height` (dimens.xml) — the WHOLE box, which contains the
 * subtitle band at its top, the plot, the axis labels and a `large_margin` bottom padding. The plot
 * proper is what is left.
 */
constexpr double kChartBoxHeight = 150.0;
constexpr double kChartBottomPadding = 20.0;  // the layout's paddingBottom
constexpr double kAxisLabelHeight = 24.0;
constexpr double kChartHeight = kChartBoxHeight - kAxisLabelHeight - kChartBottomPadding;
/** The band the subtitle occupies at the top of the box: content text plus the layout's 2 dp. */
constexpr double kChartTopPadding = dimens::kContentTextSize + 8.0;

/** MainAdapter.kt:110-115 — fewer than three buckets is not a chart. */
constexpr size_t kMinBuckets = 3;

/** `TextComponent.MinWidth.fixed(40f)` — the marker pill never narrower than this (kt:179). */
constexpr double kMarkerMinWidth = 40.0;
/** LABEL_BACKGROUND_SHADOW_RADIUS_DP / _DY_DP (kt:297-298). */
constexpr double kMarkerShadowRadius = 4.0;
constexpr double kMarkerShadowDy = 2.0;

void StrokeLine(OH_Drawing_Canvas* canvas, double x1, double y, double x2, double width,
                uint32_t color) {
    OH_Drawing_Pen* pen = OH_Drawing_PenCreate();
    OH_Drawing_PenSetAntiAlias(pen, true);
    OH_Drawing_PenSetColor(pen, color);
    OH_Drawing_PenSetWidth(pen, static_cast<float>(width));
    OH_Drawing_Path* path = OH_Drawing_PathCreate();
    OH_Drawing_PathMoveTo(path, static_cast<float>(x1), static_cast<float>(y));
    OH_Drawing_PathLineTo(path, static_cast<float>(x2), static_cast<float>(y));
    OH_Drawing_CanvasAttachPen(canvas, pen);
    OH_Drawing_CanvasDrawPath(canvas, path);
    OH_Drawing_CanvasDetachPen(canvas);
    OH_Drawing_PathDestroy(path);
    OH_Drawing_PenDestroy(pen);
}

/** The marker's guideline — the same primitive as StrokeLine, vertical. */
void StrokeVerticalLine(OH_Drawing_Canvas* canvas, double x, double y1, double y2, double width,
                        uint32_t color) {
    OH_Drawing_Pen* pen = OH_Drawing_PenCreate();
    OH_Drawing_PenSetAntiAlias(pen, true);
    OH_Drawing_PenSetColor(pen, color);
    OH_Drawing_PenSetWidth(pen, static_cast<float>(width));
    OH_Drawing_Path* path = OH_Drawing_PathCreate();
    OH_Drawing_PathMoveTo(path, static_cast<float>(x), static_cast<float>(y1));
    OH_Drawing_PathLineTo(path, static_cast<float>(x), static_cast<float>(y2));
    OH_Drawing_CanvasAttachPen(canvas, pen);
    OH_Drawing_CanvasDrawPath(canvas, path);
    OH_Drawing_CanvasDetachPen(canvas);
    OH_Drawing_PathDestroy(path);
    OH_Drawing_PenDestroy(pen);
}

/**
 * A rounded rect with a Material shadow under it, at a caller-chosen radius and offset.
 *
 * `FillCard` hard-codes the 2 dp card elevation; the marker's label carries its own
 * (LABEL_BACKGROUND_SHADOW_RADIUS_DP = 4, _DY_DP = 2), so it needs its own shadow layer.
 */
void FillRoundRectShadow(OH_Drawing_Canvas* canvas, double left, double top, double right,
                         double bottom, double radius, uint32_t color, double blur, double dy) {
    OH_Drawing_Brush* brush = OH_Drawing_BrushCreate();
    OH_Drawing_BrushSetColor(brush, color);
    OH_Drawing_BrushSetAntiAlias(brush, true);
    // The brush takes the shadow layer directly, as FillCard's does — there is no Filter in this
    // SDK's C API, which the first attempt assumed.
    OH_Drawing_ShadowLayer* shadow = OH_Drawing_ShadowLayerCreate(
        static_cast<float>(blur), 0.0f, static_cast<float>(dy), 0x40000000);
    OH_Drawing_BrushSetShadowLayer(brush, shadow);

    OH_Drawing_Rect* rect = OH_Drawing_RectCreate(
        static_cast<float>(left), static_cast<float>(top), static_cast<float>(right),
        static_cast<float>(bottom));
    OH_Drawing_RoundRect* round =
        OH_Drawing_RoundRectCreate(rect, static_cast<float>(radius), static_cast<float>(radius));
    OH_Drawing_CanvasAttachBrush(canvas, brush);
    OH_Drawing_CanvasDrawRoundRect(canvas, round);
    OH_Drawing_CanvasDetachBrush(canvas);

    OH_Drawing_RoundRectDestroy(round);
    OH_Drawing_RectDestroy(rect);
    OH_Drawing_ShadowLayerDestroy(shadow);
    OH_Drawing_BrushDestroy(brush);
}

TextStyleSpec Style(double sizeVp, uint32_t color, bool bold, TextAlignment alignment) {
    TextStyleSpec style;
    style.sizeVp = sizeVp;
    style.color = color;
    style.bold = bold;
    style.alignment = alignment;
    style.singleLine = true;
    return style;
}

double PeakIntensity(const std::vector<Minutely>& minutely) {
    double peak = 0.0;
    for (const Minutely& bucket : minutely) {
        peak = std::max(peak, bucket.precipitationIntensity);
    }
    return peak;
}

/** kt:109-112 — the scale never drops below the heavy threshold. */
double MaxY(const std::vector<Minutely>& minutely) {
    return std::max(PeakIntensity(minutely), kHourlyHeavy);
}

struct ThresholdLine {
    double value;
    const char* label;
};

/**
 * kt:113-136 — which threshold lines to draw:
 *   below heavy          all three
 *   heavy .. 2 × heavy   light and heavy
 *   above 2 × heavy      heavy only
 */
std::vector<ThresholdLine> Thresholds(double max) {
    std::vector<ThresholdLine> lines;
    if (max < kHourlyHeavy * 2.0) {
        lines.push_back({kHourlyLight, "Light"});  // strings.xml:95
    }
    if (max <= kHourlyHeavy) {
        lines.push_back({kHourlyMedium, "Medium"});  // strings.xml:96
    }
    lines.push_back({kHourlyHeavy, "Heavy"});  // strings.xml:97
    return lines;
}

/** Weather.kt:46-64. `dbz > 0` becomes `precipitationIntensity > 0` — see Minutely. */
std::string NowcastTitle(const std::vector<Minutely>& minutely) {
    if (minutely.empty()) {
        return "Precipitation";  // strings.xml:76
    }
    const bool startsWet = minutely.front().precipitationIntensity > 0.0;
    const bool endsWet = minutely.back().precipitationIntensity > 0.0;
    if (!startsWet) {
        return "Precipitation soon";  // strings.xml:903
    }
    return endsWet ? "Precipitation"                  // strings.xml:906
                   : "Precipitation stopping soon";   // strings.xml:909
}

/** Weather.kt:66-92 — the description names the time the precipitation starts or stops. */
std::string NowcastSubtitle(const std::vector<Minutely>& minutely, int utcOffsetSeconds,
                            bool is24Hour) {
    if (minutely.empty()) {
        return "No precipitation";  // strings.xml:98
    }
    const bool startsWet = minutely.front().precipitationIntensity > 0.0;
    const bool endsWet = minutely.back().precipitationIntensity > 0.0;

    if (!startsWet) {
        for (const Minutely& bucket : minutely) {
            if (bucket.precipitationIntensity > 0.0) {
                // strings.xml:905
                return "Expected to start at " +
                       HourLabel(bucket.date, utcOffsetSeconds, is24Hour);
            }
        }
        return "No precipitation";
    }

    const Minutely* lastWet = nullptr;
    for (const Minutely& bucket : minutely) {
        if (bucket.precipitationIntensity > 0.0) {
            lastWet = &bucket;
        }
    }
    if (lastWet == nullptr) {
        return "No precipitation";
    }
    const std::string when = HourLabel(lastWet->endingDate, utcOffsetSeconds, is24Hour);
    return endsWet ? "Expected to continue until at least " + when  // strings.xml:908
                   : "Expected to stop at " + when;                 // strings.xml:911
}

}  // namespace

namespace {

/**
 * kt:137-145 — true when every bucket is exactly 0 or one of the three thresholds.
 *
 * Open-Meteo reports real accumulations, so this is normally false and the label prints a quantity.
 * Ported anyway: a source reporting only the three levels would otherwise read "5.0 mm/h" where
 * Android reads "Light".
 */
bool HasOnlyThresholdValues(const std::vector<Minutely>& minutely) {
    for (const Minutely& bucket : minutely) {
        const double value = bucket.precipitationIntensity;
        if (value != 0.0 && value != kHourlyLight && value != kHourlyMedium &&
            value != kHourlyHeavy) {
            return false;
        }
    }
    return true;
}

/** `MarkerLabelFormatterMinutelyDecorator.format` (kt:264-294). */
std::string NowcastMarkerLabel(const std::vector<Minutely>& minutely, size_t index,
                               int utcOffsetSeconds, bool is24Hour) {
    const Minutely& bucket = minutely[index];
    const double value = bucket.precipitationIntensity;
    std::string quantity;
    if (HasOnlyThresholdValues(minutely)) {
        // kt:274-281 — the four exact cases, then the measured fallback.
        if (value == 0.0) {
            quantity = "None";
        } else if (value == kHourlyLight) {
            quantity = "Light";
        } else if (value == kHourlyMedium) {
            quantity = "Medium";
        } else if (value == kHourlyHeavy) {
            quantity = "Heavy";
        }
    }
    if (quantity.empty()) {
        char buffer[32] = {};
        std::snprintf(buffer, sizeof(buffer), "%.1f mm/h", value);
        quantity = buffer;
    }
    // `colon_separator` is ": " in English (kt:291).
    return HourLabel(bucket.date, utcOffsetSeconds, is24Hour) + "-" +
           HourLabel(bucket.endingDate, utcOffsetSeconds, is24Hour) + ": " + quantity;
}

}  // namespace

double DrawNowcastCard(OH_Drawing_Canvas* canvas, double leftPx, double topPx, double widthPx,
                       const std::vector<Minutely>& minutely, const PageCardColors& colors,
                       int utcOffsetSeconds, bool is24Hour, int markerIndex,
                       NowcastChartBounds* outBounds) {
    if (outBounds != nullptr) {
        *outBounds = NowcastChartBounds();
    }
    if (minutely.size() < kMinBuckets || PeakIntensity(minutely) <= 0.0) {
        return 0.0;
    }
    const float density = DisplayDensity();
    const double margin = dimens::kSmallMargin * density;
    const double padding = dimens::kNormalMargin * density;
    const double cardLeft = leftPx + margin;
    const double cardTop = topPx + margin;
    const double cardWidth = widthPx - margin * 2.0;
    const double chartWidth = cardWidth - padding * 2.0;
    const double chartHeight = kChartHeight * density;
    const double axisHeight = kAxisLabelHeight * density;

    const TextStyleSpec titleStyle =
        Style(dimens::kTitleTextSize, colors.onSurfaceInverse, true, TextAlignment::Left);
    const TextStyleSpec subtitleStyle =
        Style(dimens::kContentTextSize, colors.bodyText, false, TextAlignment::Left);
    const std::string title = NowcastTitle(minutely);
    const std::string subtitle = NowcastSubtitle(minutely, utcOffsetSeconds, is24Hour);
    const double titleHeight = MeasureTextHeight(title, chartWidth, titleStyle);
    const double subtitleHeight = MeasureTextHeight(subtitle, chartWidth, subtitleStyle);

    /*
     * The layout is NOT a stack (container_main_precipitation_nowcast_card.xml).
     *
     * The chart is constrained `Top_toTopOf="nowcasting_block_subtitle"`, so its
     * `nowcasting_chart_height` (150 dp) box STARTS at the subtitle's top and the subtitle is drawn
     * over its first rows; the axis labels and a `large_margin` bottom padding are inside the box.
     * The card therefore ends where the box ends and has no bottom padding of its own.
     *
     * Stacking title, subtitle, plot and axis strip one under the other — which both ports did —
     * made the card 261 vp against the reference's 193, measured off ref_main_cardorder.png. This is
     * the tallest card on the page, so 68 vp of it pushed everything below it off-screen sooner
     * than Android does.
     */
    const double large = dimens::kLargeMargin * density;
    const double cardHeight = large + titleHeight + 2.0 * density + kChartBoxHeight * density;
    FillCard(canvas, cardLeft, cardTop, cardLeft + cardWidth, cardTop + cardHeight,
             dimens::kCardCornerRadius * density, colors.cardBackground, density);

    double y = cardTop + large;
    DrawText(canvas, title, cardLeft + large, y, chartWidth, titleStyle);
    y += titleHeight + 2.0 * density;
    // The subtitle sits INSIDE the chart box, over its top rows, rather than above it.
    DrawText(canvas, subtitle, cardLeft + large, y, chartWidth, subtitleStyle);

    const double chartLeft = cardLeft + large;
    const double baseline = y + chartHeight;
    const double usable = chartHeight - kChartTopPadding * density;
    const double max = MaxY(minutely);

    // Threshold lines first, so the columns sit on top of them.
    const TextStyleSpec labelStyle =
        Style(dimens::kSubtitleTextSize, colors.label, false, TextAlignment::Left);
    for (const ThresholdLine& line : Thresholds(max)) {
        const double lineY = baseline - usable * line.value / max;
        StrokeLine(canvas, chartLeft, lineY, chartLeft + chartWidth, 1.0 * density, colors.line);
        // Position.Vertical.Bottom (kt:228) — the label hangs under its line, at the start edge.
        DrawText(canvas, line.label, chartLeft, lineY, chartWidth, labelStyle);
    }

    // Columns. thicknessDp is 500 in the Kotlin, i.e. "as wide as the slot allows", so each
    // bucket fills its share of the width with a small gap.
    const double slot = chartWidth / static_cast<double>(minutely.size());
    const double columnWidth = std::max(2.0, slot * 0.8);
    for (size_t i = 0; i < minutely.size(); ++i) {
        const double intensity = minutely[i].precipitationIntensity;
        if (intensity <= 0.0) {
            continue;
        }
        const double height = usable * intensity / max;
        const double left = chartLeft + slot * static_cast<double>(i) + (slot - columnWidth) / 2.0;
        // CorneredShape.rounded(allPercent = 15) — 15 % of the column width.
        FillRoundRect(canvas, left, baseline - height, left + columnWidth, baseline,
                      columnWidth * 0.15, colors.column);
    }

    StrokeLine(canvas, chartLeft, baseline, chartLeft + chartWidth, 1.0 * density, colors.line);

    // Three time labels on the bottom axis: start, midpoint, end (kt:107, 209-223).
    const Minutely& first = minutely.front();
    const Minutely& last = minutely.back();
    const int64_t midMs = first.date + (last.endingDate - first.date) / 2;
    const double labelY = baseline + axisHeight - MeasureTextHeight("0", chartWidth, labelStyle);

    DrawText(canvas, HourLabel(first.date, utcOffsetSeconds, is24Hour), chartLeft, labelY,
             chartWidth, labelStyle);
    TextStyleSpec centred = labelStyle;
    centred.alignment = TextAlignment::Center;
    DrawText(canvas, HourLabel(midMs, utcOffsetSeconds, is24Hour), chartLeft, labelY, chartWidth,
             centred);
    TextStyleSpec right = labelStyle;
    right.alignment = TextAlignment::Right;
    DrawText(canvas, HourLabel(last.endingDate, utcOffsetSeconds, is24Hour), chartLeft, labelY,
             chartWidth, right);

    if (outBounds != nullptr) {
        outBounds->valid = true;
        outBounds->left = chartLeft;
        outBounds->top = y;
        outBounds->right = chartLeft + chartWidth;
        outBounds->bottom = baseline;
        outBounds->buckets = static_cast<int>(minutely.size());
    }

    /*
     * The marker (kt:163-183) — a guideline down the touched bucket and a pill label above it.
     *
     * Drawn LAST, over the columns and the axis, which is where a Vico marker sits.
     */
    if (markerIndex >= 0 && markerIndex < static_cast<int>(minutely.size())) {
        const double centre = chartLeft + slot * static_cast<double>(markerIndex) + slot / 2.0;
        StrokeVerticalLine(canvas, centre, y + kChartTopPadding * density, baseline, 1.0 * density,
                           colors.label);

        const std::string label =
            NowcastMarkerLabel(minutely, static_cast<size_t>(markerIndex), utcOffsetSeconds,
                               is24Hour);
        TextStyleSpec pillStyle =
            Style(dimens::kSubtitleTextSize, colors.markerText, false, TextAlignment::Center);
        const double textWidth = MeasureTextWidth(label, pillStyle);
        // `normal_margin` horizontal padding, a 40 dp floor (kt:174-179).
        const double pillWidth =
            std::max(kMarkerMinWidth * density, textWidth + dimens::kNormalMargin * density * 2.0);
        // `small_margin` vertical padding.
        const double pillHeight =
            (dimens::kSubtitleTextSize * 1.45 + dimens::kSmallMargin * 2.0) * density;
        double pillLeft = centre - pillWidth / 2.0;
        // Vico keeps the marker inside the chart rather than letting it run off either end.
        pillLeft = std::max(chartLeft, std::min(pillLeft, chartLeft + chartWidth - pillWidth));

        FillRoundRectShadow(canvas, pillLeft, y, pillLeft + pillWidth, y + pillHeight,
                            pillHeight / 2.0, colors.markerBackground,
                            kMarkerShadowRadius * density, kMarkerShadowDy * density);
        const double textHeight = MeasureTextHeight(label, pillWidth, pillStyle);
        DrawText(canvas, label, pillLeft, y + (pillHeight - textHeight) / 2.0, pillWidth, pillStyle);
    }

    return cardHeight + margin * 2.0;
}

double DrawAlertCard(OH_Drawing_Canvas* canvas, double leftPx, double topPx, double widthPx,
                     const std::vector<Alert>& alerts, const PageCardColors& colors,
                     int utcOffsetSeconds, bool is24Hour, int64_t nowMs) {
    if (alerts.empty()) {
        return 0.0;
    }
    // kt:84-88 — only warnings already in force are listed; the rest are "to follow".
    std::vector<Alert> current;
    for (const Alert& alert : alerts) {
        if (!alert.hasStart || alert.startDate <= nowMs) {
            current.push_back(alert);
        }
    }
    const float density = DisplayDensity();

    // Both layouts are a Material 3 `ListItem`, and its height is a FIXED token rather than the
    // sum of its content — AlertViewHolder adds no padding of its own. Deriving the height from
    // content plus padding is what made this card 72 vp where Android draws 56.
    const double margin = dimens::kSmallMargin * density;
    const double padding = dimens::kNormalMargin * density;
    const double iconSize = dimens::kMaterialIconSize * density;
    const double cardLeft = leftPx + margin;
    const double cardTop = topPx + margin;
    const double cardWidth = widthPx - margin * 2.0;
    // ListItemDefaults spaces the leading content from the headline by 16 dp, not 8.
    const double textLeft = cardLeft + padding + iconSize + padding;
    const double textWidth = cardWidth - padding * 3.0 - iconSize;

    if (current.empty()) {
        // kt:96-131 — one row, the warning icon and a single line. strings.xml alerts_to_follow.
        TextStyleSpec style =
            Style(dimens::kTitleTextSize, colors.alertText, true, TextAlignment::Left);
        style.singleLine = false;
        const std::string label = "Upcoming alerts, tap here to see them.";
        const double textHeight = MeasureTextHeight(label, textWidth, style);
        const double height = dimens::kListItemOneLineHeight * density;

        FillCard(canvas, cardLeft, cardTop, cardLeft + cardWidth, cardTop + height,
                 dimens::kCardCornerRadius * density, colors.alertBackground, density);
        DrawAsset(canvas, "ic_warning.svg", cardLeft + padding,
                  cardTop + (height - iconSize) / 2.0, iconSize, colors.alertText, true);
        DrawText(canvas, label, textLeft, cardTop + (height - textHeight) / 2.0, textWidth, style);
        return height + margin * 2.0;
    }

    const TextStyleSpec headlineStyle =
        Style(dimens::kTitleTextSize, colors.alertText, true, TextAlignment::Left);
    const TextStyleSpec datesStyle =
        Style(dimens::kContentTextSize, colors.alertDates, false, TextAlignment::Left);

    // A headline plus a supporting line is a two-line ListItem: 72 dp per row.
    const double rowHeight = dimens::kListItemTwoLineHeight * density;
    const double height = rowHeight * static_cast<double>(current.size());

    // container_main_alert.xml tints the card with @color/alert_background — a filled pill.
    FillCard(canvas, cardLeft, cardTop, cardLeft + cardWidth, cardTop + height,
             dimens::kCardCornerRadius * density, colors.alertBackground, density);

    double rowTop = cardTop;
    for (const Alert& alert : current) {
        // kt:181-189 — the leading content is NOT the warning icon here: it is a
        // MaterialShapes.Triangle filled with the alert's own severity colour.
        DrawTriangle(canvas, cardLeft + padding, rowTop + (rowHeight - iconSize) / 2.0, iconSize,
                     alert.color != 0 ? alert.color : colors.alertText);

        /*
         * `Alert.getFormattedDates` (Alert.kt) — "Aug 7, 10:00 AM — 8:00 PM".
         *
         * The start is always DAY + locale separator + TIME; the end repeats the day only when it
         * differs from the start's, which is why a same-day alert ends in a bare time. The port
         * printed the times alone and dropped the date.
         */
        std::string dates;
        if (alert.hasStart) {
            const std::string startDay = MediumDayAndMonth(alert.startDate, utcOffsetSeconds);
            dates = startDay + kLocaleSeparator +
                    ClockTime(alert.startDate, utcOffsetSeconds, is24Hour);
            if (alert.hasEnd) {
                const std::string endDay = MediumDayAndMonth(alert.endDate, utcOffsetSeconds);
                dates += " — ";
                if (endDay != startDay) {
                    dates += endDay + kLocaleSeparator;
                }
                dates += ClockTime(alert.endDate, utcOffsetSeconds, is24Hour);
            }
        }
        const double headlineHeight = MeasureTextHeight(alert.headline, textWidth, headlineStyle);
        const double datesHeight = MeasureTextHeight(dates, textWidth, datesStyle);
        // The two lines are centred as a block within the fixed row.
        double y = rowTop + (rowHeight - headlineHeight - datesHeight) / 2.0;
        DrawText(canvas, alert.headline, textLeft, y, textWidth, headlineStyle);
        y += headlineHeight;
        DrawText(canvas, dates, textLeft, y, textWidth, datesStyle);
        rowTop += rowHeight;
    }
    return height + margin * 2.0;
}

double DrawFooter(OH_Drawing_Canvas* canvas, double leftPx, double topPx, double widthPx,
                  const std::string& sourceName, uint32_t contentColor) {
    const float density = DisplayDensity();
    // FooterViewHolder.kt:117 — normal_margin / 2.
    const double padding = dimens::kNormalMargin / 2.0 * density;
    const TextStyleSpec style =
        Style(dimens::kContentTextSize, contentColor, true, TextAlignment::Left);

    /*
     * The row is drawn as three runs so the underline can cover the two LINKS and skip the dot
     * between them — FooterViewHolder underlines the source name (:128-144) and "More" (:149),
     * never the separator. strings.xml action_more / data_sources (:150).
     */
    const std::string more = sourceName.empty() ? "Data sources" : "More";
    const std::string separator = " • ";
    const double sourceWidth = sourceName.empty() ? 0.0 : MeasureTextWidth(sourceName, style);
    const double separatorWidth = sourceName.empty() ? 0.0 : MeasureTextWidth(separator, style);
    const double moreWidth = MeasureTextWidth(more, style);
    const double total = sourceWidth + separatorWidth + moreWidth;

    double x = leftPx + (widthPx - total) / 2.0;
    const double y = topPx + padding;
    const double height = MeasureTextHeight(more, moreWidth + 1.0, style);
    const double ruleY = y + height - 2.0 * density;
    const double rule = 1.0 * density;

    if (!sourceName.empty()) {
        DrawText(canvas, sourceName, x, y, sourceWidth + 1.0, style);
        FillRoundRect(canvas, x, ruleY, x + sourceWidth, ruleY + rule, 0.0, contentColor);
        x += sourceWidth;
        DrawText(canvas, separator, x, y, separatorWidth + 1.0, style);
        x += separatorWidth;
    }
    DrawText(canvas, more, x, y, moreWidth + 1.0, style);
    FillRoundRect(canvas, x, ruleY, x + moreWidth, ruleY + rule, 0.0, contentColor);

    return height + padding * 2.0;
}

}  // namespace breezy
