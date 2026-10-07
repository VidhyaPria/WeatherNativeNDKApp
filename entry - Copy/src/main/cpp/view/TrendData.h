/*
 * The per-column data a trend card draws, ported from ArkUIApp's view/trend/ChartData.ets,
 * DoubleHistogramCanvas.ets and TrendDataBuilder.ets.
 *
 * The Kotlin adapters (DailyTemperatureAdapter and friends) do this shaping in onBindViewHolder;
 * both ports hoist it out of the draw so the painter only reads finished numbers.
 */
#ifndef BREEZY_VIEW_TRENDDATA_H
#define BREEZY_VIEW_TRENDDATA_H

#include <cstdint>
#include <string>
#include <vector>

#include "model/Weather.h"

namespace breezy {

/** The tab order from SettingsManager.kt:71-90. */
enum class TrendTabId {
    Temperature,
    AirQuality,
    Wind,
    UvIndex,
    Precipitation,
    Sunshine,
    FeelsLike,
    Humidity,
    /* HourlyTrendDisplay.kt:36-38 — the three the hourly card has and the daily card does not. */
    Pressure,
    CloudCover,
    Visibility,
};

struct TrendTab {
    TrendTabId id;
    const char* label;
};

/** SettingsManager.kt:71-79 */
extern const std::vector<TrendTab> kDailyTabs;
/** SettingsManager.kt:81-90 */
extern const std::vector<TrendTab> kHourlyTabs;

/** Which chart a column draws — the two are different views entirely, not a styling switch. */
enum class ChartKind { Polyline, DoubleHistogram };

/** The three points a column's polyline segment needs: previous, own, next. */
struct PolylinePoints {
    Maybe previous;
    Maybe own;
    Maybe next;
};

struct ChartData {
    PolylinePoints highPolyline;
    PolylinePoints lowPolyline;
    std::string highText;
    std::string lowText;
    Maybe highestValue;
    Maybe lowestValue;

    Maybe histogramValue;
    std::string histogramText;
    Maybe highestHistogramValue;
    Maybe lowestHistogramValue;
    /** 0 means "use the card colour" — the histogram-only tabs recolour every bar. */
    uint32_t histogramColor = 0;
    double histogramAlpha = -1.0;
    uint32_t histogramTextColor = 0;
};

/** DoubleHistogramView — two bars growing outwards from the vertical centre. */
struct DoubleHistogramData {
    Maybe highValue;
    Maybe lowValue;
    std::string highText;
    std::string lowText;
    Maybe highestValue;
    uint32_t highColor = 0;
    uint32_t lowColor = 0;
};

/** A key line across the whole list at a fixed value (TrendRecyclerView.kt). */
struct KeyLine {
    double value = 0.0;
    std::string contentLeft;
    std::string contentRight;
    bool above = true;
};

/** One column. */
struct TrendItemModel {
    std::string topLabel;
    std::string subLabel;
    /** AbsDailyTrendAdapter.kt:76 — today and the future use the accent text colour. */
    bool accent = false;
    bool hasIcons = false;
    std::string dayIcon;
    std::string nightIcon;
    /** The wind tabs swap the weather icon for a tinted, rotated direction arrow. */
    bool iconsAreArrows = false;
    double dayIconRotation = 0.0;
    double nightIconRotation = 0.0;
    uint32_t dayIconTint = 0;
    uint32_t nightIconTint = 0;

    ChartKind chartKind = ChartKind::Polyline;
    ChartData chart;
    DoubleHistogramData doubleHistogram;
};

/** A tab's columns plus the key lines drawn behind them. */
struct TrendSeries {
    std::vector<TrendItemModel> items;
    std::vector<KeyLine> keyLines;
    /** The scale the key lines are placed against — the polyline bounds, or the bar maximum. */
    Maybe highestValue;
    Maybe lowestValue;
};

/**
 * @param titleTextColor colorTitleText for the current theme — the label colour the histogram-only
 *        tabs use, where the polyline tabs use colorPrecipitationProbability instead.
 */
TrendSeries BuildDailySeries(const Weather& weather, TrendTabId tab, int64_t nowMs,
                             int utcOffsetSeconds, uint32_t titleTextColor);

TrendSeries BuildHourlySeries(const Weather& weather, TrendTabId tab, int utcOffsetSeconds,
                              bool is24Hour, uint32_t titleTextColor);

}  // namespace breezy

#endif  // BREEZY_VIEW_TRENDDATA_H
