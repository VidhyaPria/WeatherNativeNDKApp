#include "view/TrendData.h"

#include <algorithm>
#include <cmath>

#include "common/LocalTime.h"
#include "common/Units.h"
#include "data/AirQuality.h"
#include "render/Theme.h"

namespace breezy {

const std::vector<TrendTab> kDailyTabs = {
    {TrendTabId::Temperature, "Conditions"},
    {TrendTabId::AirQuality, "Air quality"},
    {TrendTabId::Wind, "Wind"},
    {TrendTabId::UvIndex, "UV index"},
    {TrendTabId::Precipitation, "Precipitation"},
    // DailySunshineAdapter.kt:151 labels it "Sunshine" but navigates to cloud cover (kt:123).
    {TrendTabId::Sunshine, "Sunshine"},
    {TrendTabId::FeelsLike, "Feels like"},
};

const std::vector<TrendTab> kHourlyTabs = {
    {TrendTabId::Temperature, "Conditions"},
    {TrendTabId::AirQuality, "Air quality"},
    {TrendTabId::Wind, "Wind"},
    {TrendTabId::UvIndex, "UV index"},
    {TrendTabId::Precipitation, "Precipitation"},
    {TrendTabId::FeelsLike, "Feels like"},
    // strings.xml:337 — the row reads "Humidity / Dew point", which is what the line plots.
    {TrendTabId::Humidity, "Humidity / Dew point"},
    {TrendTabId::Pressure, "Pressure"},
    {TrendTabId::CloudCover, "Cloud cover"},
    {TrendTabId::Visibility, "Visibility"},
};

namespace {

const char* const kWeekdaysShort[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

std::string Pad2(int value) {
    return value < 10 ? "0" + std::to_string(value) : std::to_string(value);
}

/** DateExtensions.kt:139-144 — "MM-dd" rendered with the English best pattern "MM/dd". */
std::string ShortDayAndMonth(int64_t instantMs, int utcOffsetSeconds) {
    return Pad2(LocalMonth(instantMs, utcOffsetSeconds) + 1) + "/" +
           Pad2(LocalDayOfMonth(instantMs, utcOffsetSeconds));
}

/** DateExtensions.kt:198-205 — "6 PM" on a 12-hour device, "18:00" on a 24-hour one. */
std::string HourLabel(int64_t instantMs, int utcOffsetSeconds, bool is24Hour) {
    const int hours = LocalHours(instantMs, utcOffsetSeconds);
    if (is24Hour) {
        return std::to_string(hours) + ":" + Pad2(LocalMinutes(instantMs, utcOffsetSeconds));
    }
    const int display = hours % 12 == 0 ? 12 : hours % 12;
    return std::to_string(display) + (hours < 12 ? " AM" : " PM");
}

struct Bounds {
    Maybe highest;
    Maybe lowest;
};

void Extend(Bounds& bounds, const Maybe& value) {
    if (!value.has_value()) {
        return;
    }
    if (!bounds.highest.has_value() || *value > *bounds.highest) {
        bounds.highest = value;
    }
    if (!bounds.lowest.has_value() || *value < *bounds.lowest) {
        bounds.lowest = value;
    }
}

/** The largest observed value, floored at a minimum the Android adapters impose. */
double HighestOf(const std::vector<Maybe>& values, double floor) {
    double highest = floor;
    for (const Maybe& value : values) {
        if (value.has_value() && *value > highest) {
            highest = *value;
        }
    }
    return highest;
}

Maybe HalfDayTemperature(const HalfDay& half) {
    return half.present ? half.temperature.temperature : Maybe{};
}

Maybe HalfDayFeelsLike(const HalfDay& half) {
    return half.present ? half.temperature.feelsLikeTemperature : Maybe{};
}

Maybe HalfDayProbability(const HalfDay& half) {
    return half.present ? half.precipitationProbability : Maybe{};
}

Maybe HalfDayWind(const HalfDay& half) {
    return half.present ? half.windSpeed : Maybe{};
}

Maybe HalfDayPrecipitation(const HalfDay& half) {
    return half.present ? Maybe{half.precipitationTotal} : Maybe{};
}

/** DailyTemperatureAdapter.kt:197-237 — real values at even indices, midpoints at odd ones. */
std::vector<Maybe> BuildDoubledSeries(const std::vector<Maybe>& values) {
    const size_t size = values.empty() ? 0 : values.size() * 2 - 1;
    std::vector<Maybe> out(size);
    for (size_t i = 0; i < size; i += 2) {
        out[i] = values[i / 2];
    }
    for (size_t i = 1; i < size; i += 2) {
        const Maybe& before = out[i - 1];
        const Maybe& after = out[i + 1];
        if (before.has_value() && after.has_value()) {
            out[i] = (*before + *after) * 0.5;
        }
    }
    return out;
}

/** DailyTemperatureAdapter.kt:177-192 */
PolylinePoints SliceForItem(const std::vector<Maybe>& doubled, size_t position) {
    const size_t centre = 2 * position;
    PolylinePoints points;
    if (centre > 0) {
        points.previous = doubled[centre - 1];
    }
    if (centre < doubled.size()) {
        points.own = doubled[centre];
    }
    if (centre + 1 < doubled.size()) {
        points.next = doubled[centre + 1];
    }
    return points;
}

/** Wind.kt:101 — anything outside the compass is "variable", and the arrow is not rotated. */
std::string WindArrowIcon(const Maybe& degree) {
    if (!degree.has_value()) {
        return {};
    }
    return (*degree < 0.0 || *degree > 360.0) ? "wind_variable.svg" : "wind_arrow.svg";
}

/**
 * wind_arrow.svg is authored pointing north, matching R.drawable.arrow_north at rotation 0.
 * Bucketing to 45° first — rather than rotating by the raw degree — keeps the same discrete set
 * of headings the Android app shows.
 */
double WindArrowRotation(const Maybe& degree) {
    if (!degree.has_value() || *degree < 0.0 || *degree > 360.0) {
        return 0.0;
    }
    const double d = *degree;
    if (d <= 22.5 || d > 337.5) return 0.0;
    if (d <= 67.5)  return 45.0;
    if (d <= 112.5) return 90.0;
    if (d <= 157.5) return 135.0;
    if (d <= 202.5) return 180.0;
    if (d <= 247.5) return 225.0;
    if (d <= 292.5) return 270.0;
    return 315.0;
}

/** The daily label pair, shared by every daily tab. */
void FillDailyLabels(TrendItemModel& item, const Daily& daily, size_t index, int todayIndex,
                     int64_t nowMs, int utcOffsetSeconds) {
    // AbsDailyTrendAdapter.kt:59-76 — "Today" replaces the weekday name, and today and anything
    // in the future take the accent colour.
    if (todayIndex >= 0 && static_cast<int>(index) == todayIndex) {
        item.topLabel = "Today";  // strings.xml:39 daily_today_short
        item.accent = true;
    } else {
        item.topLabel = kWeekdaysShort[LocalDayOfWeek(daily.date, utcOffsetSeconds)];
        item.accent = daily.date > nowMs;
    }
    item.subLabel = ShortDayAndMonth(daily.date, utcOffsetSeconds);
}

using Formatter = std::string (*)(double);
using ColorOf = uint32_t (*)(double);

std::string FormatTemperature(double value) { return FormatTemperatureNarrow(value); }

/** DailyUVAdapter / DailySunshineAdapter / DailyAirQualityAdapter: one opaque bar, no icons. */
ChartData HistogramOnlyChart(const Maybe& value, Formatter format, double highest, ColorOf colorOf,
                             uint32_t titleTextColor) {
    ChartData chart;
    chart.histogramValue = value;
    if (value.has_value()) {
        chart.histogramText = format(*value);
        chart.histogramColor = colorOf != nullptr ? colorOf(*value) : 0;
    }
    chart.highestHistogramValue = highest;
    chart.lowestHistogramValue = 0.0;
    chart.histogramAlpha = 1.0;
    chart.histogramTextColor = titleTextColor;
    return chart;
}

KeyLine MakeKeyLine(double value, const std::string& label, bool above, Formatter format) {
    KeyLine line;
    line.value = value;
    line.contentLeft = format(value);
    line.contentRight = label;
    line.above = above;
    return line;
}

}  // namespace

TrendSeries BuildDailySeries(const Weather& weather, TrendTabId tab, int64_t nowMs,
                             int utcOffsetSeconds, uint32_t titleTextColor) {
    TrendSeries series;
    const std::vector<Daily>& days = weather.dailyForecast;
    const int todayIndex = weather.todayIndex;

    /* ---- the polyline pair: Conditions and Feels like ---- */
    if (tab == TrendTabId::Temperature || tab == TrendTabId::FeelsLike) {
        const bool isFeelsLike = tab == TrendTabId::FeelsLike;
        std::vector<Maybe> highValues;
        std::vector<Maybe> lowValues;
        for (const Daily& day : days) {
            highValues.push_back(isFeelsLike ? HalfDayFeelsLike(day.day)
                                             : HalfDayTemperature(day.day));
            lowValues.push_back(isFeelsLike ? HalfDayFeelsLike(day.night)
                                            : HalfDayTemperature(day.night));
        }

        // kt:238-241 — the normals would participate in the bounds so the key lines stay inside
        // the chart. Open-Meteo is not a normals source (RefreshHelper.kt:884-898).
        Bounds bounds;
        for (size_t i = 0; i < highValues.size(); ++i) {
            Extend(bounds, highValues[i]);
            Extend(bounds, lowValues[i]);
        }
        series.highestValue = bounds.highest;
        series.lowestValue = bounds.lowest;

        const std::vector<Maybe> highDoubled = BuildDoubledSeries(highValues);
        const std::vector<Maybe> lowDoubled = BuildDoubledSeries(lowValues);

        for (size_t i = 0; i < days.size(); ++i) {
            const Daily& daily = days[i];
            TrendItemModel item;
            FillDailyLabels(item, daily, i, todayIndex, nowMs, utcOffsetSeconds);
            item.hasIcons = true;
            if (daily.day.present) {
                item.dayIcon = std::string(IconBaseName(daily.day.weatherKind, true)) + ".png";
            }
            if (daily.night.present) {
                item.nightIcon = std::string(IconBaseName(daily.night.weatherKind, false)) + ".png";
            }

            ChartData& chart = item.chart;
            chart.highPolyline = SliceForItem(highDoubled, i);
            chart.lowPolyline = SliceForItem(lowDoubled, i);
            if (highValues[i].has_value()) {
                chart.highText = FormatTemperatureNarrow(*highValues[i]);
            }
            if (lowValues[i].has_value()) {
                chart.lowText = FormatTemperatureNarrow(*lowValues[i]);
            }
            chart.highestValue = bounds.highest;
            chart.lowestValue = bounds.lowest;

            // DailyTemperatureAdapter.kt:116-141 — the larger of the two half-day probabilities,
            // only when above zero, on a fixed 0..100 scale.
            const Maybe dayP = HalfDayProbability(daily.day);
            const Maybe nightP = HalfDayProbability(daily.night);
            Maybe peak;
            if (dayP.has_value() && nightP.has_value()) {
                peak = std::max(*dayP, *nightP);
            } else if (dayP.has_value()) {
                peak = dayP;
            } else if (nightP.has_value()) {
                peak = nightP;
            }
            if (peak.has_value() && *peak > 0.0) {
                chart.histogramValue = peak;
                chart.histogramText = FormatPercent(*peak);
                chart.highestHistogramValue = 100.0;
                chart.lowestHistogramValue = 0.0;
            }
            series.items.push_back(std::move(item));
        }
        return series;
    }

    /* ---- the two-bars-from-the-centre tabs: Wind and Precipitation ---- */
    if (tab == TrendTabId::Wind || tab == TrendTabId::Precipitation) {
        const bool isWind = tab == TrendTabId::Wind;
        std::vector<Maybe> highValues;
        std::vector<Maybe> lowValues;
        for (const Daily& day : days) {
            highValues.push_back(isWind ? HalfDayWind(day.day) : HalfDayPrecipitation(day.day));
            lowValues.push_back(isWind ? HalfDayWind(day.night)
                                       : HalfDayPrecipitation(day.night));
        }
        std::vector<Maybe> all;
        for (size_t i = 0; i < highValues.size(); ++i) {
            all.push_back(highValues[i]);
            all.push_back(lowValues[i]);
        }
        // Without a floor a calm week would scale a 1 m/s breeze to a full-height bar.
        // DailyWindAdapter.kt:48 — 15 m/s. DailyPrecipitationAdapter.kt:56 — HALF_DAY_HEAVY.
        const double highest = HighestOf(all, isWind ? 15.0 : 57.75);
        series.highestValue = highest;

        const Formatter format = isWind ? &FormatSpeed : &FormatPrecipitation;
        const ColorOf colorOf = isWind ? &WindColor : &HalfDayPrecipitationColor;

        for (size_t i = 0; i < days.size(); ++i) {
            const Daily& daily = days[i];
            TrendItemModel item;
            FillDailyLabels(item, daily, i, todayIndex, nowMs, utcOffsetSeconds);
            item.chartKind = ChartKind::DoubleHistogram;
            item.hasIcons = true;

            const uint32_t dayColor =
                highValues[i].has_value() ? colorOf(*highValues[i]) : 0;
            const uint32_t nightColor =
                lowValues[i].has_value() ? colorOf(*lowValues[i]) : 0;

            if (isWind) {
                // DailyWindAdapter.kt:71-75 / :101-105 — the arrow, tinted with the half-day's
                // own colour, is what makes the Wind tab look unlike every other tab.
                const Maybe dayDegree = daily.day.present ? daily.day.windDegree : Maybe{};
                const Maybe nightDegree = daily.night.present ? daily.night.windDegree : Maybe{};
                item.iconsAreArrows = true;
                item.dayIcon = WindArrowIcon(dayDegree);
                item.nightIcon = WindArrowIcon(nightDegree);
                item.dayIconRotation = WindArrowRotation(dayDegree);
                item.nightIconRotation = WindArrowRotation(nightDegree);
                item.dayIconTint = dayColor;
                item.nightIconTint = nightColor;
            } else {
                // DailyPrecipitationAdapter.kt:95-98 / :115-118 — ordinary weather icons.
                if (daily.day.present) {
                    item.dayIcon = std::string(IconBaseName(daily.day.weatherKind, true)) + ".png";
                }
                if (daily.night.present) {
                    item.nightIcon =
                        std::string(IconBaseName(daily.night.weatherKind, false)) + ".png";
                }
            }

            DoubleHistogramData& chart = item.doubleHistogram;
            chart.highValue = highValues[i];
            chart.lowValue = lowValues[i];
            if (highValues[i].has_value()) {
                chart.highText = format(*highValues[i]);
            }
            if (lowValues[i].has_value()) {
                chart.lowText = format(*lowValues[i]);
            }
            chart.highestValue = highest;
            chart.highColor = dayColor;
            chart.lowColor = nightColor;
            series.items.push_back(std::move(item));
        }
        return series;
    }

    /* ---- the histogram-only tabs: Air quality, UV index, Sunshine ---- */
    std::vector<Maybe> values;
    Formatter format = &FormatIndex;
    ColorOf colorOf = nullptr;
    double floor = 0.0;

    if (tab == TrendTabId::AirQuality) {
        for (const Daily& day : days) {
            values.push_back(day.airQualityIndex);
        }
        // DailyAirQualityAdapter.kt:137-163 — floored at the "excessive" threshold, with three
        // key lines at the band boundaries.
        floor = kAqiThresholds[4];
        colorOf = &AqiLevelColor;
        series.keyLines = {MakeKeyLine(kAqiThresholds[1], "Fresh air", true, format),
                           MakeKeyLine(kAqiThresholds[2], "High pollution", true, format),
                           MakeKeyLine(kAqiThresholds[3], "Excessive pollution", true, format)};
    } else if (tab == TrendTabId::UvIndex) {
        for (const Daily& day : days) {
            values.push_back(day.uvIndex);
        }
        floor = 8.0;  // DailyUVAdapter.kt:47 — UV_INDEX_HIGH.
        colorOf = &UvColor;
        // DailyUVAdapter.kt:134-141 — one key line at UV_INDEX_MIDDLE.
        series.keyLines = {MakeKeyLine(6.0, "Alert level", true, format)};
    } else {
        // Sunshine — DailySunshineAdapter.kt:80-91, a fixed #fdbc4c bar and no key lines.
        for (const Daily& day : days) {
            values.push_back(day.sunshineHours);
        }
        format = &FormatHours;
        colorOf = nullptr;
    }

    const double highest = HighestOf(values, floor);
    series.highestValue = highest;
    series.lowestValue = 0.0;

    for (size_t i = 0; i < days.size(); ++i) {
        TrendItemModel item;
        FillDailyLabels(item, days[i], i, todayIndex, nowMs, utcOffsetSeconds);
        // DailyUVAdapter / DailySunshineAdapter never call setDayIconDrawable.
        item.hasIcons = false;
        item.chart = HistogramOnlyChart(values[i], format, highest, colorOf, titleTextColor);
        if (tab == TrendTabId::Sunshine && values[i].has_value()) {
            item.chart.histogramColor = kSunshineColor;
        }
        series.items.push_back(std::move(item));
    }
    return series;
}

TrendSeries BuildHourlySeries(const Weather& weather, TrendTabId tab, int utcOffsetSeconds,
                              bool is24Hour, uint32_t titleTextColor) {
    TrendSeries series;
    const std::vector<Hourly>& hours = weather.nextHourlyForecast;

    /*
     * Wind, UV, Precipitation and Air quality pass six nulls to setData and render only a
     * histogram; the rest draw a single (high) line. Drawing a polyline for all of them is the
     * whole of the hourly half of the original mismatch.
     */
    const bool histogramOnly = tab == TrendTabId::Wind || tab == TrendTabId::UvIndex ||
                               tab == TrendTabId::Precipitation ||
                               tab == TrendTabId::CloudCover ||
                               tab == TrendTabId::AirQuality;

    if (histogramOnly) {
        std::vector<Maybe> values;
        Formatter format = &FormatIndex;
        ColorOf colorOf = nullptr;
        double floor = 100.0;

        for (const Hourly& hour : hours) {
            switch (tab) {
                case TrendTabId::Wind: values.push_back(hour.windSpeed); break;
                case TrendTabId::UvIndex: values.push_back(hour.uvIndex); break;
                case TrendTabId::AirQuality: values.push_back(hour.airQualityIndex); break;
                case TrendTabId::CloudCover: values.push_back(hour.cloudCover); break;
                // HourlyPrecipitationAdapter.kt:81-89 bars the TOTAL, not the probability.
                default: values.push_back(hour.precipitationTotal); break;
            }
        }
        switch (tab) {
            case TrendTabId::Wind:
                format = &FormatSpeed; colorOf = &WindColor; floor = 15.0; break;
            case TrendTabId::UvIndex:
                colorOf = &UvColor; floor = 8.0; break;
            case TrendTabId::AirQuality:
                colorOf = &AqiLevelColor; floor = kAqiThresholds[4]; break;
            // HourlyCloudCoverAdapter.kt:64-75 — a fixed 0-100 % scale.
            case TrendTabId::CloudCover:
                format = &FormatPercent; colorOf = &CloudCoverColor; floor = 100.0; break;
            default:
                format = &FormatPrecipitation; colorOf = &HourlyPrecipitationColor; floor = 15.0;
                break;
        }

        const double highest = HighestOf(values, floor);
        series.highestValue = highest;
        series.lowestValue = 0.0;

        for (size_t i = 0; i < hours.size(); ++i) {
            const Hourly& hour = hours[i];
            TrendItemModel item;
            item.topLabel = HourLabel(hour.date, utcOffsetSeconds, is24Hour);
            item.accent = i == 0 || LocalHours(hour.date, utcOffsetSeconds) == 0;

            if (tab == TrendTabId::Wind) {
                // HourlyWindAdapter.kt:69-73 — one arrow above the chart, in the bar's colour.
                item.hasIcons = true;
                item.iconsAreArrows = true;
                item.dayIcon = WindArrowIcon(hour.windDegree);
                item.dayIconRotation = WindArrowRotation(hour.windDegree);
                item.dayIconTint = values[i].has_value() ? WindColor(*values[i]) : 0;
            } else if (tab == TrendTabId::Precipitation) {
                // HourlyPrecipitationAdapter keeps the weather icon.
                item.hasIcons = true;
                item.dayIcon = std::string(IconBaseName(hour.weatherKind, hour.daylight)) + ".png";
            } else {
                // UV, Air quality and Cloud cover show no icon at all.
                item.hasIcons = false;
            }

            item.chart = HistogramOnlyChart(values[i], format, highest, colorOf, titleTextColor);
            series.items.push_back(std::move(item));
        }
        return series;
    }

    /* ---- the polyline tabs: Conditions, Feels like, Humidity, Pressure, Visibility ---- */
    std::vector<Maybe> values;
    Formatter format = &FormatTemperature;
    for (const Hourly& hour : hours) {
        switch (tab) {
            case TrendTabId::FeelsLike:
                values.push_back(hour.temperature.feelsLikeTemperature);
                break;
            // HourlyHumidityAdapter.kt:85-101 — the LINE is dew point; humidity is the histogram
            // behind it. Dew point is a temperature, so it formats like one.
            case TrendTabId::Humidity:
                values.push_back(hour.dewPoint);
                break;
            case TrendTabId::Pressure:
                values.push_back(hour.pressure);
                break;
            case TrendTabId::Visibility:
                values.push_back(hour.visibility);
                break;
            default:
                values.push_back(hour.temperature.temperature);
                break;
        }
    }
    // Each plots in its own unit, so the label formatter follows the tab (ArkUIApp's
    // hourlyPolylineFormatter). Temperature, feels-like and dew point all format as temperatures.
    if (tab == TrendTabId::Pressure) {
        format = &FormatPressure;
    } else if (tab == TrendTabId::Visibility) {
        format = &FormatDistance;
    }

    Bounds bounds;
    for (const Maybe& value : values) {
        Extend(bounds, value);
    }
    // A flat series would divide by zero when mapping values to pixels.
    if (bounds.highest.has_value() && bounds.lowest.has_value() &&
        *bounds.highest == *bounds.lowest) {
        bounds.highest = *bounds.highest + 1.0;
        bounds.lowest = *bounds.lowest - 1.0;
    }
    series.highestValue = bounds.highest;
    series.lowestValue = bounds.lowest;

    const std::vector<Maybe> doubled = BuildDoubledSeries(values);

    for (size_t i = 0; i < hours.size(); ++i) {
        const Hourly& hour = hours[i];
        TrendItemModel item;
        item.topLabel = HourLabel(hour.date, utcOffsetSeconds, is24Hour);
        // AbsHourlyTrendAdapter.kt:56 — the first column and every midnight use the accent colour.
        item.accent = i == 0 || LocalHours(hour.date, utcOffsetSeconds) == 0;
        item.hasIcons = true;
        item.dayIcon = std::string(IconBaseName(hour.weatherKind, hour.daylight)) + ".png";

        ChartData& chart = item.chart;
        chart.highPolyline = SliceForItem(doubled, i);
        // The hourly card draws ONE line; the low polyline stays empty.
        if (values[i].has_value()) {
            chart.highText = format(*values[i]);
        }
        chart.highestValue = bounds.highest;
        chart.lowestValue = bounds.lowest;

        if (tab == TrendTabId::Temperature && hour.precipitationProbability.has_value() &&
            *hour.precipitationProbability > 0.0) {
            // HourlyTemperatureAdapter.kt:91-107 — probability behind the curve.
            chart.histogramValue = hour.precipitationProbability;
            chart.histogramText = FormatPercent(*hour.precipitationProbability);
            chart.highestHistogramValue = 100.0;
            chart.lowestHistogramValue = 0.0;
        } else if (tab == TrendTabId::Humidity && hour.relativeHumidity.has_value()) {
            // On the same fixed 0..100 scale the probability histogram uses.
            chart.histogramValue = hour.relativeHumidity;
            chart.histogramText = FormatPercent(*hour.relativeHumidity);
            chart.highestHistogramValue = 100.0;
            chart.lowestHistogramValue = 0.0;
        }
        series.items.push_back(std::move(item));
    }
    return series;
}

}  // namespace breezy
