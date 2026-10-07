#include "render/TrendCardPainter.h"

#include <native_drawing/drawing_brush.h>
#include <native_drawing/drawing_path.h>
#include <native_drawing/drawing_pen.h>
#include <native_drawing/drawing_point.h>
#include <native_drawing/drawing_rect.h>
#include <native_drawing/drawing_round_rect.h>
#include <native_drawing/drawing_shader_effect.h>

#include <algorithm>
#include <cmath>

#include "render/AssetImages.h"
#include "render/ColumnCache.h"
#include "render/FrameTrace.h"
#include "render/Shapes.h"
#include "render/TextPainter.h"
#include "render/Theme.h"

namespace breezy {
namespace {

/** TrendTabGroup.ets — the chip geometry measured against the Android build. */
constexpr float kChipHeightVp = 40.0f;
constexpr float kChipHorizontalPaddingVp = 12.0f;

/** A round-capped polyline, which is what every chart line is. */
void StrokePath(OH_Drawing_Canvas* canvas, OH_Drawing_Path* path, double width, uint32_t color) {
    OH_Drawing_Pen* pen = OH_Drawing_PenCreate();
    OH_Drawing_PenSetAntiAlias(pen, true);
    OH_Drawing_PenSetColor(pen, color);
    OH_Drawing_PenSetWidth(pen, static_cast<float>(width));
    OH_Drawing_PenSetCap(pen, LINE_ROUND_CAP);
    OH_Drawing_PenSetJoin(pen, LINE_ROUND_JOIN);
    OH_Drawing_CanvasAttachPen(canvas, pen);
    OH_Drawing_CanvasDrawPath(canvas, path);
    OH_Drawing_CanvasDetachPen(canvas);
    OH_Drawing_PenDestroy(pen);
}

/** The layout of one column, in pixels, shared by the measure and the draw. */
struct ColumnMetrics {
    double topLabelHeight = 0.0;
    double subLabelHeight = 0.0;
    double iconSlot = 0.0;
    double chartTop = 0.0;
    double chartHeight = 0.0;
};

TextStyleSpec LabelStyle(double sizeVp, uint32_t color) {
    TextStyleSpec style;
    style.sizeVp = sizeVp;
    style.color = color;
    style.singleLine = true;
    style.alignment = TextAlignment::Center;
    return style;
}

/**
 * DailyTrendItemView.kt:99-153 — week text, date text, day icon, chart, night icon, and a 16 vp
 * bottom margin. The chart takes whatever vertical space is left, exactly as the measure pass does.
 */
ColumnMetrics MeasureColumn(double listHeightPx, bool isDaily, bool hasIcons) {
    const float density = DisplayDensity();
    const double textMargin = trend::kItemTextMargin * density;
    const double iconMargin = trend::kItemIconMargin * density;
    const double iconSize = trend::kItemIconSize * density;

    ColumnMetrics metrics;
    metrics.topLabelHeight = MeasureTextHeight("Wed", 1.0e6, LabelStyle(dimens::kTitleTextSize, 0));
    metrics.subLabelHeight =
        isDaily ? MeasureTextHeight("08/06", 1.0e6, LabelStyle(dimens::kContentTextSize, 0)) : 0.0;
    metrics.iconSlot = hasIcons ? iconSize + iconMargin * 2 : 0.0;

    double used = textMargin * 2 + metrics.topLabelHeight;
    if (isDaily) {
        used += textMargin * 2 + metrics.subLabelHeight;
    }
    used += metrics.iconSlot;
    metrics.chartTop = used;

    double below = trend::kItemMarginBottom * density;
    if (isDaily && hasIcons) {
        below += metrics.iconSlot;
    }
    metrics.chartHeight = std::max(0.0, listHeightPx - used - below);
    return metrics;
}

/** PolylineAndHistogramView.kt:500-507 — computeSingleCoordinate. */
double ComputeY(double value, double max, double min, double chartHeight) {
    const float density = DisplayDensity();
    const double marginTop = trend::kChartMarginTop * density;
    const double marginBottom = trend::kChartMarginBottom * density;
    const double usable = chartHeight - marginTop - marginBottom;
    if (max == min) {
        return chartHeight - marginBottom;
    }
    return chartHeight - marginBottom - usable * (value - min) / (max - min);
}

/**
 * One column's chart, in the column's own coordinates: (0,0) is the top-left of the chart band.
 *
 * Draw order follows onDraw (kt:101-122): the vertical time line, the histogram, the high
 * polyline with its shadow, then the low polyline.
 */
void DrawChart(OH_Drawing_Canvas* canvas, const ChartData& data, const TrendCardColors& colors,
               double width, double height) {
    const float density = DisplayDensity();
    const double marginTop = trend::kChartMarginTop * density;
    const double marginBottom = trend::kChartMarginBottom * density;
    const double bottom = height - marginBottom;
    const double centreX = width / 2.0;

    // 1. The vertical time line (kt:124-137).
    {
        OH_Drawing_Path* path = OH_Drawing_PathCreate();
        OH_Drawing_PathMoveTo(path, static_cast<float>(centreX), static_cast<float>(marginTop));
        OH_Drawing_PathLineTo(path, static_cast<float>(centreX), static_cast<float>(bottom));
        StrokePath(canvas, path, trend::kChartLineSize * density, colors.subLine);
        OH_Drawing_PathDestroy(path);
    }

    const bool hasPolyline = data.highestValue.has_value() && data.lowestValue.has_value();

    // 2. The histogram (kt:106-113, 332-365) — drawn when it has a value, and either that value
    // is non-zero or there is no polyline at all.
    if (data.histogramValue.has_value() && (*data.histogramValue != 0.0 || !hasPolyline) &&
        !data.histogramText.empty() && data.highestHistogramValue.has_value() &&
        data.lowestHistogramValue.has_value()) {
        const double halfWidth = trend::kHistogramWidth * density;
        const double y = ComputeY(*data.histogramValue, *data.highestHistogramValue,
                                  *data.lowestHistogramValue, height);
        // The histogram-only tabs recolour every bar from its own value and draw it opaque; the
        // polyline tabs use the card's low-line colour behind the curve at a low alpha.
        const uint32_t barColor = data.histogramColor != 0 ? data.histogramColor : colors.lowLine;
        const double alpha = data.histogramAlpha >= 0.0 ? data.histogramAlpha
                                                        : colors.histogramAlpha;
        FillRoundRect(canvas, centreX - halfWidth, y, centreX + halfWidth, bottom, halfWidth,
                      WithAlpha(barColor, alpha));

        const uint32_t textColor =
            data.histogramTextColor != 0 ? data.histogramTextColor : colors.histogramText;
        TextStyleSpec style = LabelStyle(trend::kHistogramTextSize, textColor);
        // kt:356-363 — the label sits below the chart area by two text margins plus the polyline
        // text size, leaving room for the low-polyline label above it.
        const double textY = bottom + 2 * trend::kChartTextMargin * density +
                             trend::kPolylineTextSize * density;
        DrawText(canvas, data.histogramText, 0.0, textY, width, style);
    }

    if (!hasPolyline) {
        return;
    }
    const double max = *data.highestValue;
    const double min = *data.lowestValue;

    const auto pointY = [&](const Maybe& value) {
        return value.has_value() ? ComputeY(*value, max, min, height) : 0.0;
    };
    const double highPrev = pointY(data.highPolyline.previous);
    const double highOwn = pointY(data.highPolyline.own);
    const double highNext = pointY(data.highPolyline.next);
    const double lowPrev = pointY(data.lowPolyline.previous);
    const double lowOwn = pointY(data.lowPolyline.own);
    const double lowNext = pointY(data.lowPolyline.next);

    const bool hasPrevious = data.highPolyline.previous.has_value();
    const bool hasNext = data.highPolyline.next.has_value();

    // 3. The high polyline, with its shadow (kt:139-255).
    if (!data.highText.empty()) {
        // The shadow is a vertical gradient from the theme colour to transparent (kt:438-456).
        OH_Drawing_Path* shadow = OH_Drawing_PathCreate();
        if (hasPrevious && hasNext) {
            OH_Drawing_PathMoveTo(shadow, 0.0f, static_cast<float>(highPrev));
            OH_Drawing_PathLineTo(shadow, static_cast<float>(centreX), static_cast<float>(highOwn));
            OH_Drawing_PathLineTo(shadow, static_cast<float>(width), static_cast<float>(highNext));
            OH_Drawing_PathLineTo(shadow, static_cast<float>(width), static_cast<float>(bottom));
            OH_Drawing_PathLineTo(shadow, 0.0f, static_cast<float>(bottom));
        } else if (!hasPrevious) {
            OH_Drawing_PathMoveTo(shadow, static_cast<float>(centreX), static_cast<float>(highOwn));
            OH_Drawing_PathLineTo(shadow, static_cast<float>(width), static_cast<float>(highNext));
            OH_Drawing_PathLineTo(shadow, static_cast<float>(width), static_cast<float>(bottom));
            OH_Drawing_PathLineTo(shadow, static_cast<float>(centreX), static_cast<float>(bottom));
        } else {
            OH_Drawing_PathMoveTo(shadow, 0.0f, static_cast<float>(highPrev));
            OH_Drawing_PathLineTo(shadow, static_cast<float>(centreX), static_cast<float>(highOwn));
            OH_Drawing_PathLineTo(shadow, static_cast<float>(centreX), static_cast<float>(bottom));
            OH_Drawing_PathLineTo(shadow, 0.0f, static_cast<float>(bottom));
        }
        OH_Drawing_PathClose(shadow);

        OH_Drawing_Point* start = OH_Drawing_PointCreate(0.0f, static_cast<float>(marginTop));
        OH_Drawing_Point* end = OH_Drawing_PointCreate(0.0f, static_cast<float>(bottom));
        const uint32_t stops[] = {colors.shadowTop, WithAlpha(colors.shadowTop, 0.0)};
        const float positions[] = {0.0f, 1.0f};
        OH_Drawing_ShaderEffect* gradient =
            OH_Drawing_ShaderEffectCreateLinearGradient(start, end, stops, positions, 2, CLAMP);
        OH_Drawing_Brush* brush = OH_Drawing_BrushCreate();
        OH_Drawing_BrushSetAntiAlias(brush, true);
        OH_Drawing_BrushSetShaderEffect(brush, gradient);
        OH_Drawing_CanvasAttachBrush(canvas, brush);
        OH_Drawing_CanvasDrawPath(canvas, shadow);
        OH_Drawing_CanvasDetachBrush(canvas);
        OH_Drawing_BrushDestroy(brush);
        OH_Drawing_ShaderEffectDestroy(gradient);
        OH_Drawing_PointDestroy(start);
        OH_Drawing_PointDestroy(end);
        OH_Drawing_PathDestroy(shadow);

        OH_Drawing_Path* line = OH_Drawing_PathCreate();
        if (hasPrevious && hasNext) {
            OH_Drawing_PathMoveTo(line, 0.0f, static_cast<float>(highPrev));
            OH_Drawing_PathLineTo(line, static_cast<float>(centreX), static_cast<float>(highOwn));
            OH_Drawing_PathLineTo(line, static_cast<float>(width), static_cast<float>(highNext));
        } else if (!hasPrevious) {
            OH_Drawing_PathMoveTo(line, static_cast<float>(centreX), static_cast<float>(highOwn));
            OH_Drawing_PathLineTo(line, static_cast<float>(width), static_cast<float>(highNext));
        } else {
            OH_Drawing_PathMoveTo(line, 0.0f, static_cast<float>(highPrev));
            OH_Drawing_PathLineTo(line, static_cast<float>(centreX), static_cast<float>(highOwn));
        }
        StrokePath(canvas, line, trend::kPolylineSize * density, colors.highLine);
        OH_Drawing_PathDestroy(line);

        // kt:236-254 — the label goes below its own vertex when the high line has dipped under
        // the low one, above it otherwise.
        TextStyleSpec style = LabelStyle(trend::kPolylineTextSize, colors.highText);
        const double textHeight = MeasureTextHeight(data.highText, width, style);
        const double margin = trend::kChartTextMargin * density;
        const double y = highOwn > lowOwn ? highOwn + margin : highOwn - textHeight - margin;
        DrawText(canvas, data.highText, 0.0, y, width, style);
    }

    // 4. The low polyline — the same shapes with no shadow fill (kt:257-330).
    if (!data.lowText.empty()) {
        OH_Drawing_Path* line = OH_Drawing_PathCreate();
        if (hasPrevious && hasNext) {
            OH_Drawing_PathMoveTo(line, 0.0f, static_cast<float>(lowPrev));
            OH_Drawing_PathLineTo(line, static_cast<float>(centreX), static_cast<float>(lowOwn));
            OH_Drawing_PathLineTo(line, static_cast<float>(width), static_cast<float>(lowNext));
        } else if (!hasPrevious) {
            OH_Drawing_PathMoveTo(line, static_cast<float>(centreX), static_cast<float>(lowOwn));
            OH_Drawing_PathLineTo(line, static_cast<float>(width), static_cast<float>(lowNext));
        } else {
            OH_Drawing_PathMoveTo(line, 0.0f, static_cast<float>(lowPrev));
            OH_Drawing_PathLineTo(line, static_cast<float>(centreX), static_cast<float>(lowOwn));
        }
        StrokePath(canvas, line, trend::kPolylineSize * density, colors.lowLine);
        OH_Drawing_PathDestroy(line);

        TextStyleSpec style = LabelStyle(trend::kPolylineTextSize, colors.lowText);
        const double textHeight = MeasureTextHeight(data.lowText, width, style);
        const double margin = trend::kChartTextMargin * density;
        const double y = highOwn > lowOwn ? lowOwn - textHeight - margin : lowOwn + margin;
        DrawText(canvas, data.lowText, 0.0, y, width, style);
    }
}

/*
 * DoubleHistogramView (kt:80-189) — the daily Wind and Precipitation tabs. Two bars grow outwards
 * from the vertical centre: the high bar up, the low bar down, each labelled on its outer end.
 *
 * Its own constants (kt:243-250), not the polyline chart's.
 */
void DrawDoubleHistogram(OH_Drawing_Canvas* canvas, const DoubleHistogramData& data,
                         const TrendCardColors& colors, double width, double height) {
    const float density = DisplayDensity();
    const double margin = 24.0 * density;
    const double marginCenter = 4.0 * density;
    const double barWidth = 8.0 * density;
    const double textSize = 14.0;
    const double textMargin = 2.0 * density;
    const double centreX = width / 2.0;

    {
        OH_Drawing_Path* path = OH_Drawing_PathCreate();
        OH_Drawing_PathMoveTo(path, static_cast<float>(centreX), static_cast<float>(margin));
        OH_Drawing_PathLineTo(path, static_cast<float>(centreX),
                              static_cast<float>(height - margin));
        StrokePath(canvas, path, 1.0 * density, colors.subLine);
        OH_Drawing_PathDestroy(path);
    }

    // kt:84-91 — a missing scale, or a zero-valued bar, is skipped entirely.
    if (!data.highestValue.has_value() || *data.highestValue == 0.0) {
        return;
    }
    const double highest = *data.highestValue;
    const double barArea = (height - margin * 2 - marginCenter) / 2.0;  // kt:229
    const double halfWidth = barWidth / 2.0;

    if (data.highValue.has_value() && *data.highValue != 0.0 && !data.highText.empty()) {
        const double cy = height / 2.0 - marginCenter / 2.0;
        const double y = cy - barArea * *data.highValue / highest;
        FillRoundRect(canvas, centreX - halfWidth, y, centreX + halfWidth, cy, halfWidth,
                      data.highColor != 0 ? data.highColor : colors.highLine);

        TextStyleSpec style = LabelStyle(textSize, colors.highText);
        const double textHeight = MeasureTextHeight(data.highText, width, style);
        DrawText(canvas, data.highText, 0.0, y - textHeight - textMargin, width, style);
    }

    if (data.lowValue.has_value() && *data.lowValue != 0.0 && !data.lowText.empty()) {
        const double cy = height / 2.0 + marginCenter / 2.0;
        const double y = cy + barArea * *data.lowValue / highest;
        FillRoundRect(canvas, centreX - halfWidth, cy, centreX + halfWidth, y, halfWidth,
                      data.lowColor != 0 ? data.lowColor : colors.lowLine);

        TextStyleSpec style = LabelStyle(textSize, colors.highText);
        DrawText(canvas, data.lowText, 0.0, y + textMargin, width, style);
    }
}

/**
 * The key lines across the whole list (TrendRecyclerView.kt:105-147) — a hairline at a fixed
 * value with its number on the left and its meaning on the right.
 */
/**
 * The key lines' RULES only, in the coordinates of whatever is being drawn into.
 *
 * Split from the labels because each column carries its own slice of them: a cached column is
 * opaque (it includes the card's own background, so the blit is a copy rather than a blend), which
 * means anything drawn UNDER the columns has to be inside them. The labels are not — they sit at
 * the list's left and right edges, once, over the top.
 */
void DrawKeyLineRules(OH_Drawing_Canvas* canvas, const TrendSeries& series,
                      const TrendCardColors& colors, double left, double top, double width,
                      double chartTop, double chartHeight) {
    if (!colors.keyLinesEnabled || series.keyLines.empty() || !series.highestValue.has_value() ||
        !series.lowestValue.has_value()) {
        return;
    }
    const float density = DisplayDensity();
    for (const KeyLine& line : series.keyLines) {
        const double y = top + chartTop +
                         ComputeY(line.value, *series.highestValue, *series.lowestValue,
                                  chartHeight);
        if (y < top + chartTop || y > top + chartTop + chartHeight) {
            continue;  // Outside the plotted range: the Kotlin simply does not draw it.
        }
        OH_Drawing_Path* path = OH_Drawing_PathCreate();
        OH_Drawing_PathMoveTo(path, static_cast<float>(left), static_cast<float>(y));
        OH_Drawing_PathLineTo(path, static_cast<float>(left + width), static_cast<float>(y));
        StrokePath(canvas, path, 1.0 * density, colors.subLine);
        OH_Drawing_PathDestroy(path);
    }
}

/** The key lines' end labels, drawn over the columns at the list's two edges. */
void DrawKeyLineLabels(OH_Drawing_Canvas* canvas, const TrendSeries& series,
                       const TrendCardColors& colors, double left, double top, double width,
                       double chartTop, double chartHeight) {
    if (!colors.keyLinesEnabled || series.keyLines.empty() || !series.highestValue.has_value() ||
        !series.lowestValue.has_value()) {
        return;
    }
    const float density = DisplayDensity();
    const double margin = 2.0 * density;
    TextStyleSpec style;
    style.sizeVp = 12.0;  // TrendRecyclerView KEY_LINE_TEXT_SIZE
    style.color = colors.captionText;
    style.singleLine = true;

    for (const KeyLine& line : series.keyLines) {
        const double y = top + chartTop +
                         ComputeY(line.value, *series.highestValue, *series.lowestValue,
                                  chartHeight);
        if (y < top + chartTop || y > top + chartTop + chartHeight) {
            continue;
        }
        const double textHeight = MeasureTextHeight(line.contentLeft, width, style);
        const double textY = line.above ? y - textHeight - margin : y + margin;
        style.alignment = TextAlignment::Left;
        DrawText(canvas, line.contentLeft, left + margin * 2, textY, width, style);
        style.alignment = TextAlignment::Right;
        DrawText(canvas, line.contentRight, left, textY, width - margin * 2, style);
        style.alignment = TextAlignment::Left;
    }
}

/** The chip row (TrendTabGroup.ets), returning the height it occupied. */
double DrawTabs(OH_Drawing_Canvas* canvas, double xPx, double yPx, double widthPx,
                const TrendCardSpec& spec, const TrendCardColors& colors,
                std::vector<ChipBounds>* chips) {
    const float density = DisplayDensity();
    const double chipHeight = kChipHeightVp * density;
    const double padding = kChipHorizontalPaddingVp * density;
    const double iconSize = dimens::kMaterialIconSize * density;
    const double left = xPx + dimens::kLargeMargin * density;
    // The overflow button is pinned to the trailing edge, so the chips get what is left.
    const double available = widthPx - dimens::kLargeMargin * density * 2 - iconSize;

    TextStyleSpec chip;
    chip.sizeVp = dimens::kContentTextSize;
    chip.bold = true;
    chip.singleLine = true;

    double x = left;
    for (size_t i = 0; i < spec.tabs.size(); ++i) {
        const bool selected = static_cast<int>(i) == spec.selectedTab;
        chip.color = selected ? colors.chipSelectedLabel : colors.chipLabel;
        const double textWidth = MeasureTextWidth(spec.tabs[i], chip);
        const double chipWidth = textWidth + padding * 2;
        if (x + chipWidth > left + available) {
            break;  // The rest live behind the overflow menu.
        }
        /*
         * Widget.Material3Expressive.MaterialButtonGroup.Connected: these are filled buttons, not
         * bare labels — the checked one is a full pill, the unchecked ones square-ish with a small
         * radius. Drawing every chip as plain text is what made the row read as a list of links.
         */
        FillRoundRect(canvas, x, yPx, x + chipWidth, yPx + chipHeight,
                      (selected ? 20.0 : 8.0) * density,
                      selected ? colors.chipSelectedContainer : colors.chipContainer);
        const double textHeight = MeasureTextHeight(spec.tabs[i], textWidth + 1.0, chip);
        DrawText(canvas, spec.tabs[i], x + padding, yPx + (chipHeight - textHeight) / 2.0,
                 textWidth + 1.0, chip);
        if (chips != nullptr) {
            chips->push_back(ChipBounds{static_cast<int>(i), x, x + chipWidth, yPx,
                                        yPx + chipHeight});
        }
        x += chipWidth;
    }

    const double overflowLeft = xPx + widthPx - dimens::kLargeMargin * density - iconSize;
    DrawAsset(canvas, "ic_more_vert.svg", overflowLeft, yPx + (chipHeight - iconSize) / 2.0,
              iconSize, colors.bodyText, true);
    if (chips != nullptr) {
        // TrendTabGroup.ets — the overflow button carries the tabs that did not fit. It is
        // recorded with tabIndex -1 so the tap handler can tell it from a chip.
        chips->push_back(ChipBounds{-1, overflowLeft, overflowLeft + iconSize, yPx,
                                    yPx + chipHeight});
    }
    return chipHeight;
}

/**
 * The overflow menu itself: the tabs that did not fit, listed under the ⋮ button, with a tick
 * against the selected one.
 *
 * MenuItemOptions.endIcon puts that tick on the TRAILING edge with a gap — `selectIcon` is
 * leading-only in ArkUI, and getting it on the wrong side was the first thing the Android
 * side-by-side caught.
 */
void DrawOverflowMenu(OH_Drawing_Canvas* canvas, const TrendCardSpec& spec,
                      const TrendCardColors& colors, const ChipBounds& anchor, double pageWidth,
                      std::vector<ChipBounds>* chips) {
    const float density = DisplayDensity();
    const double menuWidth = 160.0 * density;
    const double rowHeight = kChipHeightVp * density;
    const double padding = dimens::kSmallMargin * density;
    const double iconSize = dimens::kMaterialIconSize * density;

    // Only the tabs the chip row could not show are in the menu.
    std::vector<int> hidden;
    for (size_t i = 0; i < spec.tabs.size(); ++i) {
        bool shown = false;
        if (chips != nullptr) {
            for (const ChipBounds& chip : *chips) {
                if (chip.tabIndex == static_cast<int>(i)) {
                    shown = true;
                    break;
                }
            }
        }
        if (!shown) {
            hidden.push_back(static_cast<int>(i));
        }
    }
    if (hidden.empty()) {
        return;
    }

    const double right = std::min(pageWidth - padding, anchor.right + padding);
    const double left = right - menuWidth;
    const double top = anchor.bottom;
    const double height = rowHeight * static_cast<double>(hidden.size()) + padding * 2.0;
    FillCard(canvas, left, top, right, top + height, dimens::kCardCornerRadius / 2.0 * density,
             colors.cardBackground, density);

    TextStyleSpec style;
    style.sizeVp = dimens::kContentTextSize;
    style.color = colors.bodyText;
    style.singleLine = true;

    double y = top + padding;
    for (int index : hidden) {
        const double textHeight = MeasureTextHeight(spec.tabs[index], menuWidth, style);
        style.color = index == spec.selectedTab ? colors.accentText : colors.bodyText;
        DrawText(canvas, spec.tabs[index], left + padding, y + (rowHeight - textHeight) / 2.0,
                 menuWidth - padding * 2.0 - iconSize, style);
        if (index == spec.selectedTab) {
            DrawAsset(canvas, "ic_check.svg", right - padding - iconSize,
                      y + (rowHeight - iconSize) / 2.0, iconSize, colors.accentText, true);
        }
        if (chips != nullptr) {
            chips->push_back(ChipBounds{index, left, right, y, y + rowHeight});
        }
        y += rowHeight;
    }
}

/** Everything above the list: title row, then the chips. */
double DrawCardHeader(OH_Drawing_Canvas* canvas, double xPx, double yPx, double widthPx,
                      const TrendCardSpec& spec, const TrendCardColors& colors,
                      std::vector<ChipBounds>* chips) {
    const float density = DisplayDensity();
    const double largeMargin = dimens::kLargeMargin * density;
    const double iconSize = dimens::kMaterialIconSize * density;

    double y = yPx + largeMargin;

    TextStyleSpec title;
    title.sizeVp = dimens::kTitleTextSize;
    title.color = colors.onSurfaceInverse;
    // textAppearanceTitleMedium is 16sp at weight 500 with 0.15 letter spacing; the default 400
    // and zero tracking read lighter and tighter than the Android title.
    title.bold = true;
    title.singleLine = true;
    title.letterSpacingVp = 0.15;

    const double titleHeight = MeasureTextHeight(spec.title, widthPx, title);
    const double rowHeight = std::max(titleHeight, iconSize);
    DrawAsset(canvas, spec.titleIcon, xPx + largeMargin, y + (rowHeight - iconSize) / 2.0, iconSize,
              colors.onSurfaceInverse, true);
    DrawText(canvas, spec.title, xPx + largeMargin + iconSize + dimens::kSmallMargin * density,
             y + (rowHeight - titleHeight) / 2.0, widthPx, title);
    y += rowHeight;

    // The subtitle is hidden when the source gives no summary (DailyViewHolder.kt:68-73), and
    // Open-Meteo returns none.
    y += DrawTabs(canvas, xPx, y, widthPx, spec, colors, chips);
    return y - yPx;
}

double CardHeaderHeight(const TrendCardSpec& spec) {
    const float density = DisplayDensity();
    TextStyleSpec title;
    title.sizeVp = dimens::kTitleTextSize;
    title.bold = true;
    title.singleLine = true;
    const double rowHeight = std::max(MeasureTextHeight(spec.title, 1.0e6, title),
                                      static_cast<double>(dimens::kMaterialIconSize * density));
    return dimens::kLargeMargin * density + rowHeight + kChipHeightVp * density;
}

/** Draws one column's icon, rotating it about its own centre when it is a wind arrow. */
void DrawColumnIcon(OH_Drawing_Canvas* canvas, const std::string& name, double x, double y,
                    double size, bool isArrow, double rotation, uint32_t tint) {
    if (name.empty()) {
        return;
    }
    if (!isArrow) {
        DrawAsset(canvas, name, x, y, size, 0, false);
        return;
    }
    // PorterDuff SRC_ATOP tint on the arrows (DailyWindAdapter.kt:71-75), rotated to the
    // bucketed heading.
    OH_Drawing_CanvasSave(canvas);
    OH_Drawing_CanvasTranslate(canvas, static_cast<float>(x + size / 2.0),
                               static_cast<float>(y + size / 2.0));
    OH_Drawing_CanvasRotate(canvas, static_cast<float>(rotation), 0.0f, 0.0f);
    DrawAsset(canvas, name, -size / 2.0, -size / 2.0, size, tint, tint != 0);
    OH_Drawing_CanvasRestore(canvas);
}

}  // namespace

double DrawTrendCard(OH_Drawing_Canvas* canvas, double xPx, double yPx, double widthPx,
                     const TrendCardSpec& spec, const TrendSeries& series,
                     const TrendCardColors& colors, double scrollOffsetPx,
                     std::vector<ChipBounds>* chips, double visibleTop, double visibleBottom,
                     uint64_t generation, bool deferOverflow) {
    const std::vector<TrendItemModel>& items = series.items;
    const float density = DisplayDensity();
    // AbstractMainCardViewHolder.kt:52-59 — 8 dp on every side.
    const double margin = dimens::kSmallMargin * density;
    const double cardLeft = xPx + margin;
    const double cardTop = yPx + margin;
    const double cardWidth = widthPx - margin * 2;
    const double headerHeight = CardHeaderHeight(spec);
    const double listHeight = spec.listHeightVp * density;
    const double cardHeight = headerHeight + margin + listHeight;

    FillCard(canvas, cardLeft, cardTop, cardLeft + cardWidth, cardTop + cardHeight,
             dimens::kCardCornerRadius * density, colors.cardBackground, density);

    DrawCardHeader(canvas, cardLeft, cardTop, cardWidth, spec, colors, chips);

    // The list clips to the card, so a column scrolled past the edge does not bleed over the
    // rounded corner — .clip(true) on the ArkTS side.
    const double listTop = cardTop + headerHeight + margin;
    // A card whose title is on screen but whose columns are not still costs seven charts a frame.
    // The height is unchanged, so the page layout below it does not move.
    if (listTop >= visibleBottom || listTop + listHeight <= visibleTop) {
        return cardHeight + margin * 2;
    }
    OH_Drawing_CanvasSave(canvas);
    OH_Drawing_Rect* clip = OH_Drawing_RectCreate(
        static_cast<float>(cardLeft), static_cast<float>(listTop),
        static_cast<float>(cardLeft + cardWidth), static_cast<float>(listTop + listHeight));
    OH_Drawing_CanvasClipRect(canvas, clip, INTERSECT, true);

    const double columnWidth = dimens::kTrendItemWidth * density;
    const bool hasIcons = !items.empty() && items[0].hasIcons;
    const ColumnMetrics metrics = MeasureColumn(listHeight, spec.isDaily, hasIcons);
    const double iconSize = trend::kItemIconSize * density;
    const double iconMargin = trend::kItemIconMargin * density;
    const double textMargin = trend::kItemTextMargin * density;

    /*
     * The key lines sit BEHIND the columns, and a cached column is opaque, so each column paints
     * its own slice of them inside its bitmap. Only the gaps the columns do not cover — the list's
     * two ends when it is scrolled to a stop — are drawn here.
     */
    DrawKeyLineRules(canvas, series, colors, cardLeft, listTop, cardWidth, metrics.chartTop,
                     metrics.chartHeight);

    for (size_t i = 0; i < items.size(); ++i) {
        const double columnLeft = cardLeft + static_cast<double>(i) * columnWidth - scrollOffsetPx;
        if (columnLeft + columnWidth < cardLeft || columnLeft > cardLeft + cardWidth) {
            continue;  // Off-screen: the equivalent of LazyForEach's window.
        }
        const TrendItemModel& item = items[i];

        /*
         * Nothing inside a column changes while the list is scrolled — only its x. So it is
         * rendered once into its own bitmap and blitted afterwards, which is the CPU equivalent of
         * RecyclerView replaying a scrolled view's display list. `generation` covers everything
         * that can change the pixels; the cache checks the size itself.
         */
        CachedColumn cached(canvas, spec.isDaily ? ColumnOwner::Daily : ColumnOwner::Hourly,
                            static_cast<int>(i), generation, columnLeft, listTop, columnWidth,
                            listHeight);
        OH_Drawing_Canvas* column = cached.Canvas();
        if (!cached.NeedsPaint()) {
            continue;
        }
        // Column-local coordinates when the cache is in use; page coordinates when it bypassed.
        const double x = cached.Bypassed() ? columnLeft : 0.0;
        double y = cached.Bypassed() ? listTop : 0.0;

        if (!cached.Bypassed()) {
            /*
             * The column owns every pixel of its box: the card's background and its slice of the
             * key lines, so the blit is an opaque copy instead of a per-pixel blend.
             *
             * The rules are drawn across the column's OWN width, not offset to the card's — the
             * lines are horizontal and span the whole list, so the result is identical, and it
             * keeps the bitmap free of anything that depends on the scroll position. A cached
             * column that baked in a scroll offset would be wrong the moment it was reused.
             */
            FillRoundRect(column, 0.0, 0.0, cached.Width(), cached.Height(), 0.0,
                          colors.cardBackground);
            DrawKeyLineRules(column, series, colors, 0.0, 0.0, cached.Width(), metrics.chartTop,
                             metrics.chartHeight);
        }

        y += textMargin;
        TextStyleSpec top = LabelStyle(dimens::kTitleTextSize,
                                       item.accent ? colors.accentText : colors.bodyText);
        DrawText(column, item.topLabel, x, y, columnWidth, top);
        y += metrics.topLabelHeight + textMargin;

        if (spec.isDaily && !item.subLabel.empty()) {
            y += textMargin;
            TextStyleSpec sub = LabelStyle(dimens::kContentTextSize,
                                           item.accent ? colors.bodyText : colors.captionText);
            DrawText(column, item.subLabel, x, y, columnWidth, sub);
            y += metrics.subLabelHeight + textMargin;
        }

        if (item.hasIcons) {
            BREEZY_TRACE("breezy:column-icon");
            y += iconMargin;
            DrawColumnIcon(column, item.dayIcon, x + (columnWidth - iconSize) / 2.0, y,
                           iconSize, item.iconsAreArrows, item.dayIconRotation, item.dayIconTint);
            y += iconSize + iconMargin;
        }

        // The chart is drawn in its own coordinates, so the port reads like the Kotlin onDraw.
        {
            BREEZY_TRACE("breezy:column-chart");
            OH_Drawing_CanvasSave(column);
            OH_Drawing_CanvasTranslate(column, static_cast<float>(x), static_cast<float>(y));
            if (item.chartKind == ChartKind::DoubleHistogram) {
                DrawDoubleHistogram(column, item.doubleHistogram, colors, columnWidth,
                                    metrics.chartHeight);
            } else {
                DrawChart(column, item.chart, colors, columnWidth, metrics.chartHeight);
            }
            OH_Drawing_CanvasRestore(column);
        }
        y += metrics.chartHeight;

        // The night icon is pinned above the bottom margin (DailyTrendItemView.kt:137-142).
        if (spec.isDaily && item.hasIcons) {
            DrawColumnIcon(column, item.nightIcon, x + (columnWidth - iconSize) / 2.0,
                           y + iconMargin, iconSize, item.iconsAreArrows, item.nightIconRotation,
                           item.nightIconTint);
        }
    }

    // The key lines' labels ride over the columns, at the list's two edges (kt:122-163).
    DrawKeyLineLabels(canvas, series, colors, cardLeft, listTop, cardWidth, metrics.chartTop,
                      metrics.chartHeight);

    OH_Drawing_CanvasRestore(canvas);
    OH_Drawing_RectDestroy(clip);

    /*
     * The menu floats over the list, so it is drawn outside the clip — but "outside this card's
     * clip" is not the same as "above the page". The cards drawn after this one paint over it, and
     * with seven hidden tabs the menu is taller than the card, so most of it disappeared. The page
     * can defer it and draw it after the last card; see DrawTrendOverflowMenu.
     */
    if (spec.overflowOpen && chips != nullptr && !deferOverflow) {
        for (const ChipBounds& chip : *chips) {
            if (chip.tabIndex == -1) {
                DrawOverflowMenu(canvas, spec, colors, chip, widthPx, chips);
                break;
            }
        }
    }

    return cardHeight + margin * 2;
}

void DrawTrendOverflowMenu(OH_Drawing_Canvas* canvas, const TrendCardSpec& spec,
                           const TrendCardColors& colors, const ChipBounds& anchor,
                           double pageWidth, std::vector<ChipBounds>* chips) {
    DrawOverflowMenu(canvas, spec, colors, anchor, pageWidth, chips);
}

}  // namespace breezy
