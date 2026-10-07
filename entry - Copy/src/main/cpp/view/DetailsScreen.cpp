#include "DetailsScreen.h"

#include <hilog/log.h>

#include <native_drawing/drawing_brush.h>
#include <native_drawing/drawing_path.h>
#include <native_drawing/drawing_pen.h>
#include <native_drawing/drawing_point.h>
#include <native_drawing/drawing_rect.h>
#include <native_drawing/drawing_shader_effect.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "../common/Formatter.h"
#include "../render/AnimatedIcon.h"
#include "../common/LocalTime.h"
#include "../common/Units.h"
#include "../data/AirQuality.h"
#include "../data/Astronomy.h"
#include "../render/AssetImages.h"
#include "../render/DetailBlockPainter.h"
#include "../render/FrameTrace.h"
#include "../render/Shapes.h"
#include "../render/TextPainter.h"
#include "../render/Theme.h"

namespace breezy {
namespace {

/* ---- geometry, measured off ref_screen4_details_pressure.png at density 3.25 --------------- */

/**
 * How many pollutant buttons stay inline before the rest move into the ⋮ menu.
 *
 * MEASURED on the reference, which fits AQI, PM2.5, PM10 and O3 before its "More" button:
 * [52,1623][339,1779], [346..570], [577..792], [799..961], [968..1150]. The port kept three and
 * pushed O3 into the menu. ArkUIApp carries the same constant.
 */
constexpr int kInlinePollutantCount = 4;
/**
 * The switcher's button height, MEASURED: the reference's chips are 156 px, i.e. 48 dp, where this
 * port drew 40. `ToggleButton` inside a `ButtonGroup` (DetailsAirQuality.kt:548-587).
 */
constexpr double kPollutantChipHeight = 48.0;
/** LinearProgressIndicator height in AirQualityDetails (DetailsAirQuality.kt:824). */
constexpr double kPollutantBarHeight = 10.0;
/** The overflow panel's measured width, the same one the trend tabs' menu uses. */
constexpr double kPollutantMenuWidth = 160.0;

/** BWCenterAlignedTopAppBar is the plain 64 dp bar, not the large one the settings screens use. */
constexpr double kBarHeight = 64.0;
constexpr double kTitleTextSize = 22.0;  // typography.titleLarge

/*
 * PrimaryScrollableTabRow: the "Yda" and "Today" columns are 336 device px apart and the indicator
 * is 231 px wide - 103 vp and 71 vp, i.e. the tab minus 16 vp of padding on each side. The edge
 * padding is Material's own ScrollableTabRowEdgeStartPadding.
 */
constexpr double kTabWidth = 103.0;
constexpr double kTabSidePadding = 16.0;
constexpr double kTabEdgePadding = 52.0;
constexpr double kIndicatorHeight = 3.0;
constexpr double kTabLabelSize = 16.0;  // bodyLarge, the default Text style in the Kotlin
constexpr double kTabDaySize = 28.0;    // typography.headlineMedium

constexpr double kLabelTextSize = 12.0;    // typography.labelMedium
constexpr double kValueTextSize = 36.0;    // typography.displaySmall
/** dimens.xml small_weather_icon_size — `WeatherCondition`'s icon (DetailsConditions.kt:748). */
constexpr double kConditionIconSize = 42.0;
constexpr double kValueUnitTextSize = 24.0;  // typography.headlineSmall
constexpr double kSectionTextSize = 22.0;  // typography.titleLarge
constexpr double kBodyTextSize = 16.0;     // bodyLarge
/**
 * `MaterialTheme.typography.headlineSmall` — the size of the one in-list heading on screen 4, the
 * "Precipitation probability" line that separates the two chart groups (kt:186).
 */
constexpr double kHeadlineSmallTextSize = 24.0;

/** bottomDetailsInset (DetailsCommon.kt:451-456) - 16 - 3 plus the FAB's 56. */
constexpr double kBottomInset = 16.0 * 3.0 + 56.0;
constexpr double kFabSize = 56.0;

const char* const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                               "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
const char* const kWeekdaysShort[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char* const kWeekdaysFull[] = {"Sunday",   "Monday", "Tuesday",  "Wednesday",
                                     "Thursday", "Friday", "Saturday"};

/* ---- the prose, verbatim from strings.xml, split exactly as the Kotlin splits it ----------- */

const char* const kAboutPressure1 =
    "Atmospheric pressure is the downward pressure exerted by the earth\xE2\x80\x99s atmosphere. "
    "This app uses mean sea level pressure (MSLP) to allow easier comparison of pressure from "
    "places with different altitudes.";
const char* const kAboutPressure2 =
    "Significant, rapid changes in pressure are used to predict changes in the weather. For "
    "example, a drop in pressure can mean that rain or snow is on the way, and rising pressure can "
    "mean that weather will improve. Abnormal atmospheric pressure may cause headaches, joint "
    "pain, fatigue and other effects.";
const char* const kAboutHumidity =
    "Relative humidity, more commonly known just as humidity, is the percentage of water vapour "
    "present in the air compared to the maximum amount that the air can hold at a given "
    "temperature.";
const char* const kAboutDewPoint =
    "Dew point is what the temperature would need to be cooled to in order to achieve a relative "
    "humidity of 100%.";
const char* const kAboutVisibility =
    "Visibility tells you how far away you can clearly see objects like buildings and hills. "
    "Visibility can be affected by precipitation, fog, dust, smoke or haze.";
const char* const kAboutCloudCover =
    "Cloud cover refers to the percentage of the sky obscured by clouds on average. The cloud "
    "cover is correlated to the sunshine duration.";
const char* const kAboutUv =
    "The UV index measures the strength of the sun\xE2\x80\x99s ultraviolet radiation. The higher "
    "it is, the greater and faster it can cause sunburn, skin cancer and eye damage.";
const char* const kAboutWind =
    "Wind speed is measured by averaging wind speeds over a period of time. Gusts are sudden "
    "bursts of wind typically lasting under 20 seconds.";
const char* const kAboutAirQuality1 =
    "Air quality index (AQI) is a scale designed to help you understand how polluted the air is in "
    "a particular area, and the potential risks to your health.";
const char* const kAboutAirQuality2 =
    "The scale used in this app is based on World Health Organization recommendations, rather than "
    "local regulations, to have meaningful categories, and make it easier to compare pollution "
    "levels between countries.";
const char* const kAboutEphemeris =
    "The rise time is when the sun or moon fully appears above the horizon.\n\n"
    "The set time is when the sun or moon completely disappears below the horizon.\n\n"
    "The dawn is when the first visible light of the day appears before sunrise.\n\n"
    "The dusk is the darkest stage of twilight before the last visible light disappears.";

/** strings.xml:1299 */
const char* const kNullDataText = "N/A";

double MeanOf(const std::vector<double>& list) {
    double total = 0.0;
    for (double value : list) {
        total += value;
    }
    return total / static_cast<double>(list.size());
}

/** Ratio.getCloudCoverDescription - the okta bands 12.5 / 37.5 / 62.5 / 87.5. */
const char* CloudCoverDescription(double percent) {
    if (percent < 12.5) {
        return "Clear sky";
    }
    if (percent < 37.5) {
        return "Mostly clear";
    }
    if (percent < 62.5) {
        return "Partly cloudy";
    }
    if (percent < 87.5) {
        return "Mostly cloudy";
    }
    return "Cloudy";
}

/** Distance.getVisibilityDescription - the Met Office bands 1 / 4 / 10 / 20 / 40 km. */
const char* VisibilityDescription(double metres) {
    if (metres < 1000.0) {
        return "Very poor";
    }
    if (metres < 4000.0) {
        return "Poor";
    }
    if (metres < 10000.0) {
        return "Moderate";
    }
    if (metres < 20000.0) {
        return "Good";
    }
    if (metres < 40000.0) {
        return "Clear";
    }
    return "Perfectly clear";
}

/** UV.getLevel (UV.kt:29-39). */
const char* UvLevelName(double index) {
    if (index < 3.0) {
        return "Low";
    }
    if (index < 6.0) {
        return "Moderate";
    }
    if (index < 8.0) {
        return "High";
    }
    if (index < 11.0) {
        return "Very high";
    }
    return "Extreme";
}

/*
 * colors.xml `colorLevel_1`-`colorLevel_5`. UV, air quality and pollen all key off these.
 *
 * The first cut of this file invented a palette (#3ED400, #FFE400, -) that is not in the Android
 * resources at all; these are the real values, and ArkUIApp's DetailsData.ets carries the same five.
 */
constexpr uint32_t kColorLevel1 = 0xFF72D572;
constexpr uint32_t kColorLevel2 = 0xFFFFCA28;
constexpr uint32_t kColorLevel3 = 0xFFFFA726;
constexpr uint32_t kColorLevel4 = 0xFFE52F35;
constexpr uint32_t kColorLevel5 = 0xFF99004C;

/** UV.kt:33-36. */
constexpr double kUvIndexLow = 3.0;
constexpr double kUvIndexMiddle = 6.0;
constexpr double kUvIndexHigh = 8.0;
constexpr double kUvIndexExcessive = 11.0;

/** UV.getUVColor (UV.kt:69-77). */
uint32_t UvLevelColor(double index) {
    if (index < kUvIndexLow) {
        return kColorLevel1;
    }
    if (index < kUvIndexMiddle) {
        return kColorLevel2;
    }
    if (index < kUvIndexHigh) {
        return kColorLevel3;
    }
    if (index < kUvIndexExcessive) {
        return kColorLevel4;
    }
    return kColorLevel5;
}

/** "%1$s to %2$s" (strings.xml:341/367/382) - the range collapses when min == max. */
std::string RangeSummary(const std::string& min, const std::string& max) {
    return min == max ? max : min + " to " + max;
}

/** strings.xml:327 / :329 - the two half-day labels every composite header uses. */
const char* const kDaytime = "Daytime";
const char* const kNighttime = "Nighttime";

/**
 * Wind.arrow - the eight-point glyph the Kotlin appends after the speed.
 *
 * Each carries U+FE0E (\xEF\xB8\x8E), the TEXT presentation selector, for the reason ArkUIApp's
 * copy does: without it the device renders the diagonals as emoji, so a north-east arrow came out
 * as a blue rounded square beside a plain black north one.
 */
const char* const kWindArrows[] = {
    "\xE2\x86\x93\xEF\xB8\x8E", "\xE2\x86\x99\xEF\xB8\x8E", "\xE2\x86\x90\xEF\xB8\x8E",
    "\xE2\x86\x96\xEF\xB8\x8E", "\xE2\x86\x91\xEF\xB8\x8E", "\xE2\x86\x97\xEF\xB8\x8E",
    "\xE2\x86\x92\xEF\xB8\x8E", "\xE2\x86\x98\xEF\xB8\x8E"};

std::string WindArrow(const Maybe& degree) {
    if (!degree.has_value()) {
        return "";
    }
    double normalised = std::fmod(std::fmod(*degree, 360.0) + 360.0, 360.0);
    // The arrow points where the wind is GOING, which is the reciprocal of the reported bearing.
    const int index = static_cast<int>(std::llround(normalised / 45.0)) % 8;
    return kWindArrows[index];
}
/**
 * The air-quality and pollen level colours come from the SHARED helpers, not from a copy.
 *
 * `AqiLevelName` (data/AirQuality.h) and `AqiLevelColor` (render/Theme.h) already exist and are
 * what the main screen's air-quality block draws with; a second table here compiled, was ambiguous
 * against them, and would have let the two screens disagree. Same for the pollen dots, which read
 * `kPollenLevelColors` — five entries indexed by level, deliberately NOT starting at the grey
 * pollenLevel_0 (see the note on that array).
 */
uint32_t PollenLevelColor(int level) {
    return kPollenLevelColors[std::max(0, std::min(4, level))];
}

/** strings.xml:43 */
const char* const kNoDailyData = "No data available for this day.";

std::string Pad2(int value) {
    return value < 10 ? "0" + std::to_string(value) : std::to_string(value);
}

/**
 * `PollenConcentrationUnit.PER_CUBIC_METER.formatMeasure` — the value at zero decimals with
 * `volume_m3_per_short` around it, which on English is "%s/m³".
 */
std::string FormatPollenConcentration(double perCubicMeter) {
    return std::to_string(static_cast<long long>(std::llround(perCubicMeter))) + "/m³";
}

}  // namespace

/* ---------------------------------------------------------------- the enum ------------------ */

const char* DetailTagId(DetailTag tag) {
    switch (tag) {
        case DetailTag::Conditions:
            return "conditions";
        case DetailTag::FeelsLike:
            return "feels_like";
        case DetailTag::Wind:
            return "wind";
        case DetailTag::AirQuality:
            return "air_quality";
        case DetailTag::Pollen:
            return "pollen";
        case DetailTag::UvIndex:
            return "uv_index";
        case DetailTag::Precipitation:
            return "precipitation";
        case DetailTag::Humidity:
            return "humidity";
        case DetailTag::Pressure:
            return "pressure";
        case DetailTag::CloudCover:
            return "cloud_cover";
        case DetailTag::Visibility:
            return "visibility";
        case DetailTag::SunMoon:
            return "sun_moon";
    }
    return "conditions";
}

const char* DetailTagName(DetailTag tag) {
    switch (tag) {
        case DetailTag::Conditions:
        case DetailTag::FeelsLike:
            // kt:34-35 - both carry R.string.conditions.
            return "Conditions";
        case DetailTag::Wind:
            return "Wind";
        case DetailTag::AirQuality:
            return "Air quality";
        case DetailTag::Pollen:
            return "Pollen";
        case DetailTag::UvIndex:
            return "UV index";
        case DetailTag::Precipitation:
            return "Precipitation";
        case DetailTag::Humidity:
            // strings.xml:334, NOT the hourly tab's "Humidity / Dew point".
            return "Humidity";
        case DetailTag::Pressure:
            return "Pressure";
        case DetailTag::CloudCover:
            return "Cloud cover";
        case DetailTag::Visibility:
            return "Visibility";
        case DetailTag::SunMoon:
            return "Sun & moon";  // strings.xml:17 `ephemeris`
    }
    return "";
}

/*
 * Two of the Kotlin's icons are vector drawables this port does not carry - `weather_haze_mini_xml`
 * and `weather_clear_night_mini_xml`, the mini variants of the animated weather icons. The port has
 * a single-glyph stand-in for each and uses it; ArkUIApp's detailScreenIconName does the same, from
 * the same two files.
 */
const char* DetailTagIcon(DetailTag tag) {
    switch (tag) {
        case DetailTag::Conditions:
        case DetailTag::FeelsLike:
            return "ic_device_thermostat.svg";
        case DetailTag::Wind:
            return "ic_wind.svg";
        case DetailTag::AirQuality:
            return "ic_air_quality.svg";
        case DetailTag::Pollen:
            return "ic_allergy.svg";
        case DetailTag::UvIndex:
            return "ic_uv.svg";
        case DetailTag::Precipitation:
            return "ic_precipitation.svg";
        case DetailTag::Humidity:
            return "ic_humidity_percentage.svg";
        case DetailTag::Pressure:
            return "ic_gauge.svg";
        case DetailTag::CloudCover:
            return "ic_cloud.svg";
        case DetailTag::Visibility:
            return "ic_eye.svg";
        case DetailTag::SunMoon:
            return "ic_weather_night.svg";
    }
    return "ic_device_thermostat.svg";
}

DetailTag DetailTagFromId(const std::string& id) {
    static const DetailTag kAll[] = {
        DetailTag::Conditions, DetailTag::FeelsLike,     DetailTag::Wind,       DetailTag::AirQuality,
        DetailTag::Pollen,     DetailTag::UvIndex,       DetailTag::Precipitation,
        DetailTag::Humidity,   DetailTag::Pressure,      DetailTag::CloudCover, DetailTag::Visibility,
        DetailTag::SunMoon};
    for (DetailTag tag : kAll) {
        if (id == DetailTagId(tag)) {
            return tag;
        }
    }
    return DetailTag::Conditions;
}

std::vector<DetailTag> DetailTagsFor(const Weather& weather, bool hasAirQualitySource,
                                     bool hasPollenSource) {
    const auto anyHourly = [&weather](bool (*test)(const Hourly&)) {
        // kt:61-88 tests `weather.hourlyForecast`, the whole series - a tag whose only readings
        // are in the past still earns a menu entry, because its past day still has a chart.
        for (const Hourly& hour : weather.hourlyForecast) {
            if (test(hour)) {
                return true;
            }
        }
        return false;
    };

    std::vector<DetailTag> out;
    // kt:58-89, in declaration order.
    out.push_back(DetailTag::Conditions);  // always displayed
    // FeelsLike is never listed: it is a sub-menu of Conditions (kt:59).
    bool wind = anyHourly([](const Hourly& h) { return h.windSpeed.has_value(); });
    for (const Daily& daily : weather.dailyForecast) {
        if (daily.day.windSpeed.has_value() || daily.night.windSpeed.has_value()) {
            wind = true;
        }
    }
    if (wind) {
        out.push_back(DetailTag::Wind);
    }
    /*
     * kt:65-66 gates these on the location's per-feature source. A location restored from this
     * port's table can have those columns empty while its forecast carries both, and the Kotlin
     * rule alone would then hide two entries whose TILES are on the main screen - MainAdapter gates
     * a tile on the VALUE. So the data counts as well; ArkUIApp's toDetailScreenList takes the same
     * union.
     */
    if (hasAirQualitySource || (weather.hasCurrent && weather.current.airQualityIndex.has_value())) {
        out.push_back(DetailTag::AirQuality);
    }
    if (hasPollenSource || (weather.hasCurrent && !weather.current.pollen.empty())) {
        out.push_back(DetailTag::Pollen);
    }
    bool uv = anyHourly([](const Hourly& h) { return h.uvIndex.value_or(0.0) > 0.0; });
    for (const Daily& daily : weather.dailyForecast) {
        if (daily.uvIndex.value_or(0.0) > 0.0) {
            uv = true;
        }
    }
    if (uv) {
        out.push_back(DetailTag::UvIndex);
    }
    out.push_back(DetailTag::Precipitation);  // kt:60 - too many conditions, always displayed
    if (anyHourly([](const Hourly& h) {
            return h.relativeHumidity.has_value() || h.dewPoint.has_value();
        })) {
        out.push_back(DetailTag::Humidity);
    }
    if (anyHourly([](const Hourly& h) { return h.pressure.has_value(); })) {
        out.push_back(DetailTag::Pressure);
    }
    if (anyHourly([](const Hourly& h) { return h.cloudCover.has_value(); })) {
        out.push_back(DetailTag::CloudCover);
    }
    if (anyHourly([](const Hourly& h) { return h.visibility.has_value(); })) {
        out.push_back(DetailTag::Visibility);
    }
    out.push_back(DetailTag::SunMoon);  // kt:89 - always computed
    return out;
}

/* ---------------------------------------------------------------- the screen ---------------- */

void DetailsScreen::SetGeometry(double widthPx, double heightPx, float density, double topInsetPx,
                                double bottomInsetPx) {
    width_ = widthPx;
    height_ = heightPx;
    density_ = density;
    topInset_ = topInsetPx;
    bottomInset_ = bottomInsetPx;
}

void DetailsScreen::Open(const Weather& weather, int utcOffsetSeconds, DetailTag tag, int dayIndex,
                         int64_t nowMs, bool hasAirQualitySource, bool hasPollenSource,
                         double latitude, double longitude) {
    weather_ = weather;
    utcOffsetSeconds_ = utcOffsetSeconds;
    tag_ = tag;
    dayIndex_ = std::max(0, std::min(dayIndex, static_cast<int>(weather.dailyForecast.size()) - 1));
    nowMs_ = nowMs;
    latitude_ = latitude;
    longitude_ = longitude;
    hasAirQualitySource_ = hasAirQualitySource;
    hasPollenSource_ = hasPollenSource;
    menuOpen_ = false;
    // A marker left over from the last visit would put the header on an hour of a different day.
    activeHourMs_ = -1;
    activeHourMs2_ = -1;
    tooltipText_.clear();
    tooltipItem_ = -1;
    // `selectedPollutant` starts null on every open (DetailsActivity's default).
    pollutantId_.clear();
    pollutantMenuOpen_ = false;
    contentScroll_.Reset();
    tabScroll_.Reset();
}

/**
 * DetailsScreen.kt:366-386 - local midnight to the next local midnight, with the first index
 * nudged one hour back when the first hour at or after midnight is not exactly midnight, so a
 * 3-hourly source still draws a complete chart.
 *
 * Reads the WHOLE series, past hours included, which is what the Kotlin slices. It used to read
 * `nextHourlyForecast` "so the two ports summarise the same hours" - they did, and both were
 * wrong: that list starts at the current hour, so Yesterday's page on every detail screen said
 * "no hourly data available" where the reference drew a full chart. Open-Meteo is already asked
 * for past_days=1. ArkUIApp's `hoursForDay` now carries the same list.
 *
 * A day with no hours gives an EMPTY result rather than the first hour of the next one.
 */
std::vector<Hourly> DetailsScreen::HoursForDay(int dayIndex) const {
    std::vector<Hourly> out;
    if (dayIndex < 0 || dayIndex >= static_cast<int>(weather_.dailyForecast.size())) {
        return out;
    }
    const int64_t start = LocalStartOfDay(weather_.dailyForecast[dayIndex].date, utcOffsetSeconds_);
    const int64_t end = start + kMillisPerDay;
    const std::vector<Hourly>& hours = weather_.hourlyForecast;

    int first = -1;
    for (size_t i = 0; i < hours.size(); ++i) {
        if (hours[i].date >= start) {
            first = static_cast<int>(i);
            break;
        }
    }
    if (first == -1 || hours[static_cast<size_t>(first)].date >= end) {
        return out;
    }
    if (first > 0 && hours[static_cast<size_t>(first)].date > start) {
        first -= 1;
    }
    int last = static_cast<int>(hours.size()) - 1;
    for (size_t i = 0; i < hours.size(); ++i) {
        if (hours[i].date >= end) {
            last = static_cast<int>(i);
            break;
        }
    }
    for (int i = first; i <= last && i < static_cast<int>(hours.size()); ++i) {
        out.push_back(hours[static_cast<size_t>(i)]);
    }
    return out;
}

/** Daily.getFullLabel (Daily.kt:41-80). */
std::string DetailsScreen::DayFullLabel(int dayIndex) const {
    if (dayIndex < 0 || dayIndex >= static_cast<int>(weather_.dailyForecast.size())) {
        return "";
    }
    const int64_t dayStart =
        LocalStartOfDay(weather_.dailyForecast[static_cast<size_t>(dayIndex)].date,
                        utcOffsetSeconds_);
    const int64_t todayStart = LocalStartOfDay(nowMs_, utcOffsetSeconds_);
    const int64_t delta = (dayStart - todayStart) / kMillisPerDay;
    if (delta == 0) {
        return "Today";
    }
    if (delta == -1) {
        return "Yesterday";
    }
    if (delta == 1) {
        return "Tomorrow";
    }
    return kWeekdaysFull[LocalDayOfWeek(dayStart, utcOffsetSeconds_)];
}

DetailsScreen::Summary DetailsScreen::BuildSummary(int variant) const {
    Summary summary;
    if (dayIndex_ < 0 || dayIndex_ >= static_cast<int>(weather_.dailyForecast.size())) {
        return summary;
    }
    const Daily& daily = weather_.dailyForecast[static_cast<size_t>(dayIndex_)];
    const std::vector<Hourly> hours = HoursForDay(dayIndex_);
    const std::string label = DayFullLabel(dayIndex_);

    // The chart marker's hour wins over the day summary, as `activeItem` does in every header.
    if ((variant == 1 ? activeHourMs2_ : activeHourMs_) >= 0) {
        SummaryColumn active;
        if (ActiveColumn(hours, &active, variant)) {
            // (the marker column, whichever variant asked for it)
            summary.columns.push_back(active);
            return summary;
        }
    }

    /*
     * VARIANT 1 IS THE PROBABILITY HEADER, WHATEVER THE TAG.
     * `PrecipitationProbabilitySummary` (DetailsPrecipitation.kt:552-585) serves the Precipitation
     * screen's second group AND the Conditions screen's closing one (DetailsConditions.kt:376-386).
     */
    if (variant == 1) {
        summary.columns.push_back(HalfDayPrecipitationProbability(daily.day, kDaytime));
        summary.columns.push_back(HalfDayPrecipitationProbability(daily.night, kNighttime));
        return summary;
    }

    const auto collect = [&hours](Maybe (*pick)(const Hourly&)) {
        std::vector<double> out;
        for (const Hourly& hour : hours) {
            const Maybe value = pick(hour);
            if (value.has_value()) {
                out.push_back(*value);
            }
        }
        return out;
    };
    const auto column = [](const std::string& colLabel, const std::string& value,
                           const std::string& unit = "", const std::string& description = "",
                           uint32_t dot = 0, const std::string& iconAsset = "",
                           float descriptionLine = 0.0f, const std::string& valueLevel = "") {
        SummaryColumn out;
        out.valueLevel = valueLevel;
        out.label = colLabel;
        out.value = value;
        out.unit = unit;
        out.description = description;
        out.dotColor = dot;
        out.iconAsset = iconAsset;
        // A tag with a third line reserves it even when empty; one without leaves this 0 and the
        // column is two lines tall. See SummaryColumn::descriptionLine.
        out.descriptionLine = descriptionLine != 0.0f
                                  ? descriptionLine
                                  : ((!description.empty() || !iconAsset.empty())
                                         ? dimens::kLabelMediumLineHeight
                                         : 0.0f);
        return out;
    };

    switch (tag_) {
        case DetailTag::Pressure: {
            const std::vector<double> list = collect([](const Hourly& h) { return h.pressure; });
            // PressureHeader (kt:177-181) labels the day's figure "Average" - chart_average, :46.
            summary.columns.push_back(
                list.empty() ? column("Average", "")
                             : column("Average", FormatPressure(MeanOf(list)),
                                      " " + PressureUnitLabel()));
            break;
        }
        case DetailTag::Humidity: {
            const std::vector<double> list =
                collect([](const Hourly& h) { return h.relativeHumidity; });
            if (list.empty()) {
                summary.columns.push_back(column(label, ""));
            } else {
                const auto bounds = std::minmax_element(list.begin(), list.end());
                summary.columns.push_back(column(
                    label, RangeSummary(FormatPercent(*bounds.first),
                                        FormatPercent(*bounds.second))));
            }
            break;
        }
        /*
         * Cloud cover and visibility are the two scalar tags with a THIRD line, and
         * `CloudCoverItem` (kt:209-233) / `VisibilityItem` (kt:205-236) stack three UNCONDITIONAL
         * `TextFixedHeight`s — so the line is 16 dp tall on a day with no reading too. The ports
         * only reserved it when there was text, which lost 52 px on an empty day.
         */
        case DetailTag::CloudCover: {
            const std::vector<double> list = collect([](const Hourly& h) { return h.cloudCover; });
            if (list.empty()) {
                summary.columns.push_back(
                    column(label, "", "", "", 0, "", dimens::kLabelMediumLineHeight));
            } else {
                const auto bounds = std::minmax_element(list.begin(), list.end());
                summary.columns.push_back(column(
                    label,
                    RangeSummary(FormatPercent(*bounds.first), FormatPercent(*bounds.second)), "",
                    RangeSummary(CloudCoverDescription(*bounds.first),
                                 CloudCoverDescription(*bounds.second))));
            }
            break;
        }
        case DetailTag::Visibility: {
            const std::vector<double> list = collect([](const Hourly& h) { return h.visibility; });
            if (list.empty()) {
                summary.columns.push_back(
                    column(label, "", "", "", 0, "", dimens::kLabelMediumLineHeight));
            } else {
                const auto bounds = std::minmax_element(list.begin(), list.end());
                summary.columns.push_back(column(
                    label,
                    RangeSummary(FormatDistance(*bounds.first), FormatDistance(*bounds.second)),
                    " " + DistanceUnitLabel(),
                    RangeSummary(VisibilityDescription(*bounds.first),
                                 VisibilityDescription(*bounds.second))));
            }
            break;
        }
        case DetailTag::UvIndex: {
            // UVHeader (kt:184-188) - the day's PEAK, labelled "Maximum value".
            if (!daily.uvIndex.has_value()) {
                summary.columns.push_back(column("Maximum value", ""));
            } else {
                // kt:220-237 — the level rides IN the value line at headlineSmall, not on a third,
                // and `format(decimals = 1)` drops the trailing zero, so 5.0 prints as "5".
                summary.columns.push_back(column("Maximum value",
                                                 FormatDecimals(*daily.uvIndex, 1), "", "",
                                                 UvLevelColor(*daily.uvIndex), "", 0.0f,
                                                 UvLevelName(*daily.uvIndex)));
            }
            break;
        }
        case DetailTag::AirQuality: {
            /*
             * AirQualityHeader (kt:345-374) - "Average", a dot in the reading's colour, then either
             * the index and its level name ("36 Fair") or, when a pollutant is picked, that
             * pollutant's concentration and the level THAT falls in ("14 ug/m3 Fair").
             */
            summary.columns.push_back(AirQualityColumn("Average", SelectedAirQuality()));
            break;
        }
        case DetailTag::Wind: {
            // WindSummary (kt:224-262) - a Daytime column and a Nighttime one, side by side.
            summary.columns.push_back(HalfDayWind(daily.day, kDaytime));
            summary.columns.push_back(HalfDayWind(daily.night, kNighttime));
            break;
        }
        case DetailTag::Precipitation: {
            // PrecipitationSummary (kt:299-337).
            summary.columns.push_back(HalfDayPrecipitation(daily.day, kDaytime));
            summary.columns.push_back(HalfDayPrecipitation(daily.night, kNighttime));
            break;
        }
        case DetailTag::Conditions:
        case DetailTag::FeelsLike: {
            // WeatherConditionSummary (kt:494-551); `showRealTemp` is which of the two tags it is.
            const bool real = tag_ == DetailTag::Conditions;
            summary.columns.push_back(HalfDayCondition(daily.day, kDaytime, real, true));
            summary.columns.push_back(HalfDayCondition(daily.night, kNighttime, real, false));
            break;
        }
        case DetailTag::Pollen:
        case DetailTag::SunMoon:
            // Neither screen has a summary header: both lead with a card of their own
            // (kt:104-126 and DetailsPollen.kt:55-77), which Sections() produces.
            break;
    }
    return summary;
}

bool DetailsScreen::ActiveColumn(const std::vector<Hourly>& hours, SummaryColumn* out,
                                 int variant) const {
    const int64_t activeHour = variant == 1 ? activeHourMs2_ : activeHourMs_;
    const Hourly* hour = nullptr;
    for (const Hourly& candidate : hours) {
        if (candidate.date == activeHour) {
            hour = &candidate;
            break;
        }
    }
    if (hour == nullptr) {
        return false;
    }
    // `activeItem.first.getFormattedTime` - the label is the hour, not the day.
    out->label = HourLabel(hour->date, utcOffsetSeconds_, false);
    // Variant 1 is the probability group's marker, on Precipitation and on Conditions alike.
    if (variant == 1) {
        if (!hour->precipitationProbability.has_value()) {
            return false;
        }
        out->value = FormatPercent(*hour->precipitationProbability);
        return true;
    }
    switch (tag_) {
        case DetailTag::Pressure:
            if (!hour->pressure.has_value()) {
                return false;
            }
            out->value = FormatPressure(*hour->pressure);
            out->unit = " " + PressureUnitLabel();
            return true;
        case DetailTag::Humidity:
            if (!hour->relativeHumidity.has_value()) {
                return false;
            }
            out->value = FormatPercent(*hour->relativeHumidity);
            return true;
        case DetailTag::CloudCover:
            if (!hour->cloudCover.has_value()) {
                return false;
            }
            out->value = FormatPercent(*hour->cloudCover);
            out->description = CloudCoverDescription(*hour->cloudCover);
            return true;
        case DetailTag::Visibility:
            if (!hour->visibility.has_value()) {
                return false;
            }
            out->value = FormatDistance(*hour->visibility);
            out->unit = " " + DistanceUnitLabel();
            out->description = VisibilityDescription(*hour->visibility);
            return true;
        case DetailTag::UvIndex: {
            if (!hour->uvIndex.has_value()) {
                return false;
            }
            out->value = FormatDecimals(*hour->uvIndex, 1);
            out->valueLevel = UvLevelName(*hour->uvIndex);
            out->dotColor = UvLevelColor(*hour->uvIndex);
            return true;
        }
        case DetailTag::Wind:
            if (!hour->windSpeed.has_value()) {
                return false;
            }
            out->value = FormatSpeed(*hour->windSpeed);
            out->unit = " " + SpeedUnitLabel();
            out->description = WindArrow(hour->windDegree);
            return true;
        case DetailTag::Conditions:
        case DetailTag::FeelsLike: {
            const bool real = tag_ == DetailTag::Conditions;
            const Maybe shown = real ? hour->temperature.temperature
                                     : hour->temperature.feelsLikeTemperature;
            if (!shown.has_value()) {
                return false;
            }
            // kt:220-233 — the MARKER column is the same `WeatherConditionItem`, so it carries the
            // hour's own icon and the same one-decimal narrow value as the day summary does.
            out->value = FormatTemperatureShortNarrow(*shown);
            if (real) {
                out->description = hour->weatherText;
                if (hour->weatherKind != WeatherKind::None) {
                    out->iconAsset =
                        std::string(IconBaseName(hour->weatherKind, hour->daylight)) + ".png";
                }
            } else if (hour->temperature.temperature.has_value()) {
                out->description = std::string("Actual") + kColonSeparator +
                                   FormatTemperatureNarrow(*hour->temperature.temperature);
            }
            return true;
        }
        case DetailTag::AirQuality: {
            if (!hour->airQuality.Any()) {
                return false;
            }
            const SummaryColumn column = AirQualityColumn(out->label, &hour->airQuality);
            if (column.value.empty()) {
                return false;
            }
            *out = column;
            return true;
        }
        case DetailTag::Precipitation: {
            out->value = FormatPrecipitation(hour->precipitationTotal);
            out->unit = " " + PrecipitationUnitLabel();
            return true;
        }
        default:
            return false;
    }
}

/**
 * One half-day's wind column — `WindItem`, DetailsWind.kt:262-325.
 *
 * The value is speed + unit + " " + arrow, all on ONE displaySmall line (kt:274-302), and the
 * third line is the GUSTS at labelLarge, drawn only when there are gusts above the speed (kt:303)
 * but reserved either way. Measured on the reference: 52 + 143 + 65 px, against this port's
 * 46 + 137 + 46 with the arrow occupying the line the gusts belong on.
 */
DetailsScreen::SummaryColumn DetailsScreen::HalfDayWind(const HalfDay& half,
                                                        const char* label) const {
    SummaryColumn out;
    out.label = label;
    out.descriptionLine = dimens::kLabelLargeLineHeight;
    if (!half.present || !half.windSpeed.has_value()) {
        return out;
    }
    out.value = FormatSpeed(*half.windSpeed);
    out.unit = " " + SpeedUnitLabel();
    out.valueSuffix = " " + WindArrow(half.windDegree);
    if (half.windGusts.has_value() && *half.windGusts > *half.windSpeed) {
        // strings.xml wind_gusts_short + colon_separator.
        out.description = "Gusts: " + FormatSpeed(*half.windGusts) + " " + SpeedUnitLabel();
    }
    return out;
}

DetailsScreen::SummaryColumn DetailsScreen::HalfDayPrecipitation(const HalfDay& half,
                                                                 const char* label) const {
    SummaryColumn out;
    out.label = label;
    if (!half.present) {
        return out;
    }
    out.value = FormatPrecipitation(half.precipitationTotal);
    out.unit = " " + PrecipitationUnitLabel();
    return out;
}

/** `PrecipitationProbabilityItem` (kt:527-550) — a plain percentage, no unit beside it. */
DetailsScreen::SummaryColumn DetailsScreen::HalfDayPrecipitationProbability(
    const HalfDay& half, const char* label) const {
    SummaryColumn out;
    out.label = label;
    if (!half.present || !half.precipitationProbability.has_value()) {
        return out;
    }
    out.value = FormatPercent(*half.precipitationProbability);
    return out;
}

/**
 * `PrecipitationDetails` (kt:418-475) for variant 0 and `PrecipitationProbabilityDetails`
 * (kt:632-687) for variant 1.
 *
 * Ice and thunderstorm are in the Kotlin's list too; Open-Meteo reports neither, so their `> 0`
 * gate is false and Android draws no row for them either. Rain and snow are what it does report.
 * The probability breakdown is keyed on per-type probabilities Open-Meteo does not publish at all,
 * so it is always empty — and an empty list means the whole block is absent, as it is on Android.
 */
std::vector<DetailsScreen::Pair> DetailsScreen::PrecipitationPairs(int variant) const {
    std::vector<Pair> out;
    if (variant == 1 || dayIndex_ < 0 ||
        dayIndex_ >= static_cast<int>(weather_.dailyForecast.size())) {
        return out;
    }
    const Daily& daily = weather_.dailyForecast[static_cast<size_t>(dayIndex_)];
    const Maybe dayRain = daily.day.present ? daily.day.precipitationRain : Maybe{};
    const Maybe nightRain = daily.night.present ? daily.night.precipitationRain : Maybe{};
    if ((dayRain.has_value() && *dayRain > 0.0) || (nightRain.has_value() && *nightRain > 0.0)) {
        Pair pair;
        pair.label = "Rain";  // strings.xml precipitation_rain
        pair.dayValue = dayRain.has_value()
                            ? FormatPrecipitation(*dayRain) + " " + PrecipitationUnitLabel()
                            : kNullDataText;
        pair.nightValue = nightRain.has_value()
                              ? FormatPrecipitation(*nightRain) + " " + PrecipitationUnitLabel()
                              : kNullDataText;
        out.push_back(pair);
    }
    const Maybe daySnow = daily.day.present ? daily.day.precipitationSnow : Maybe{};
    const Maybe nightSnow = daily.night.present ? daily.night.precipitationSnow : Maybe{};
    if ((daySnow.has_value() && *daySnow > 0.0) || (nightSnow.has_value() && *nightSnow > 0.0)) {
        // kt:483-487 — snow alone is measured in the SNOWFALL unit, the coarser one of the pair.
        Pair pair;
        pair.label = "Snow";  // strings.xml precipitation_snow
        pair.dayValue = daySnow.has_value()
                            ? FormatSnowfall(*daySnow) + " " + SnowfallUnitLabel()
                            : kNullDataText;
        pair.nightValue = nightSnow.has_value()
                              ? FormatSnowfall(*nightSnow) + " " + SnowfallUnitLabel()
                              : kNullDataText;
        out.push_back(pair);
    }
    return out;
}

/**
 * The probability group's gate (kt:178-181): either half-day carries a probability, or any hour of
 * the day does.
 */
bool DetailsScreen::HasProbability() const {
    if (dayIndex_ < 0 || dayIndex_ >= static_cast<int>(weather_.dailyForecast.size())) {
        return false;
    }
    const Daily& daily = weather_.dailyForecast[static_cast<size_t>(dayIndex_)];
    if (daily.day.present && daily.day.precipitationProbability.has_value()) {
        return true;
    }
    if (daily.night.present && daily.night.precipitationProbability.has_value()) {
        return true;
    }
    for (const Hourly& hour : HoursForDay(dayIndex_)) {
        if (hour.precipitationProbability.has_value()) {
            return true;
        }
    }
    return false;
}

/**
 * One half-day's conditions column.
 *
 * `showRealTemp` is TAG_CONDITIONS; the other tag shows the feels-like temperature large and the
 * actual one under it as "Actual: 21 deg" (kt:591-604).
 */
DetailsScreen::SummaryColumn DetailsScreen::HalfDayCondition(const HalfDay& half, const char* label,
                                                             bool showRealTemp,
                                                             bool daylight) const {
    SummaryColumn out;
    out.label = label;
    if (!half.present) {
        return out;
    }
    const Maybe actual = half.temperature.temperature;
    const Maybe feelsLike = half.temperature.feelsLikeTemperature;
    const Maybe shown = showRealTemp ? actual : feelsLike;
    if (!shown.has_value()) {
        return out;
    }
    // kt:578 passes only `unitWidth = NARROW`, so the VALUE keeps the unit's short decimals: the
    // reference reads 18.2° / 11.3° here where the trend labels and the main header read 18° / 11°.
    out.value = FormatTemperatureShortNarrow(*shown);
    if (showRealTemp) {
        out.description = half.weatherText;
        /*
         * kt:630-633 — on a phone `WeatherCondition` follows the temperature, and it carries the
         * half-day's ICON as well as its text. Only the real-temperature tag shows a condition;
         * the feels-like one puts "Actual: 21°" there instead, which has none.
         */
        if (half.weatherKind != WeatherKind::None) {
            out.iconAsset = std::string(IconBaseName(half.weatherKind, daylight)) + ".png";
            out.iconKind = half.weatherKind;
            out.iconDaylight = daylight;
        }
    } else if (actual.has_value()) {
        // strings.xml:52 `temperature_real` plus the colon separator.
        out.description = std::string("Actual") + kColonSeparator +
                          FormatTemperatureNarrow(*actual);
    }
    return out;
}

/* ------------------------------------------------------------------------ the chart -------- */

namespace {

double RoundUpTo(double value, double multiple) {
    return std::ceil(value / multiple) * multiple;
}

double RoundDownTo(double value, double multiple) {
    return std::floor(value / multiple) * multiple;
}

/** DistanceUnit.KILOMETER.chartStep - 5 below 4 km, 10 above. */
double DistanceChartStep(double maxYInUnit) {
    return maxYInUnit < 4.0 ? 5.0 : 10.0;
}

/** "1,050" - the axis labels are grouped, which is the NumberFormatter's doing on English. */
std::string GroupThousands(double value, int decimals) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), decimals > 0 ? "%.1f" : "%.0f", value);
    std::string text = buffer;
    const size_t dot = text.find('.');
    std::string whole = dot == std::string::npos ? text : text.substr(0, dot);
    const std::string tail = dot == std::string::npos ? "" : text.substr(dot);
    const bool negative = !whole.empty() && whole[0] == '-';
    if (negative) {
        whole = whole.substr(1);
    }
    std::string grouped;
    while (whole.size() > 3) {
        grouped = "," + whole.substr(whole.size() - 3) + grouped;
        whole = whole.substr(0, whole.size() - 3);
    }
    grouped = whole + grouped;
    return (negative ? "-" : "") + grouped + tail;
}

/** DetailScreen.CHART_MIN_COUNT (kt:50). */
constexpr size_t kChartMinCount = 2;

/**
 * Vico's axis guideline is a dashed rule. There is no dash effect on the pen here, so it is drawn
 * as a run of short filled segments - cheap, and the only thing on this screen that needs one.
 */
void DrawDashedLine(OH_Drawing_Canvas* canvas, double x1, double y1, double x2, double y2,
                    float density, uint32_t color) {
    const double dash = 4.0 * density;
    const double dx = x2 - x1;
    const double dy = y2 - y1;
    const double length = std::sqrt(dx * dx + dy * dy);
    if (length <= 0.0) {
        return;
    }
    const double stepX = dx / length * dash;
    const double stepY = dy / length * dash;
    double travelled = 0.0;
    double x = x1;
    double y = y1;
    bool on = true;
    while (travelled < length) {
        const double nextX = x + stepX;
        const double nextY = y + stepY;
        if (on) {
            // Horizontal and vertical are the only orientations this screen draws.
            FillRect(canvas, std::min(x, nextX), std::min(y, nextY) - (dy == 0.0 ? 0.5 : 0.0),
                     std::max(x, nextX) + (dx == 0.0 ? 1.0 : 0.0),
                     std::max(y, nextY) + (dy == 0.0 ? 0.5 : 0.0), color);
        }
        x = nextX;
        y = nextY;
        travelled += dash;
        on = !on;
    }
}

/** The three drawables the Kotlin picks between: ic_arrow_upward_alt / _downward_alt / ic_equal. */
void DrawTrendArrow(OH_Drawing_Canvas* canvas, double x, double y, double size, int kind,
                    uint32_t color) {
    const double half = size / 2.0;
    OH_Drawing_Pen* pen = OH_Drawing_PenCreate();
    OH_Drawing_PenSetAntiAlias(pen, true);
    OH_Drawing_PenSetColor(pen, color);
    OH_Drawing_PenSetWidth(pen, static_cast<float>(std::max(1.0, size / 9.0)));
    OH_Drawing_PenSetCap(pen, LINE_ROUND_CAP);
    OH_Drawing_Path* path = OH_Drawing_PathCreate();
    if (kind == 0) {
        OH_Drawing_PathMoveTo(path, static_cast<float>(x - half * 0.6),
                              static_cast<float>(y - size * 0.16));
        OH_Drawing_PathLineTo(path, static_cast<float>(x + half * 0.6),
                              static_cast<float>(y - size * 0.16));
        OH_Drawing_PathMoveTo(path, static_cast<float>(x - half * 0.6),
                              static_cast<float>(y + size * 0.16));
        OH_Drawing_PathLineTo(path, static_cast<float>(x + half * 0.6),
                              static_cast<float>(y + size * 0.16));
    } else {
        const double sign = kind > 0 ? -1.0 : 1.0;
        const double tipY = y + half * sign;
        OH_Drawing_PathMoveTo(path, static_cast<float>(x), static_cast<float>(y - half * sign));
        OH_Drawing_PathLineTo(path, static_cast<float>(x), static_cast<float>(tipY));
        OH_Drawing_PathMoveTo(path, static_cast<float>(x), static_cast<float>(tipY));
        OH_Drawing_PathLineTo(path, static_cast<float>(x - half * 0.5),
                              static_cast<float>(tipY - half * 0.5 * sign));
        OH_Drawing_PathMoveTo(path, static_cast<float>(x), static_cast<float>(tipY));
        OH_Drawing_PathLineTo(path, static_cast<float>(x + half * 0.5),
                              static_cast<float>(tipY - half * 0.5 * sign));
    }
    OH_Drawing_CanvasAttachPen(canvas, pen);
    OH_Drawing_CanvasDrawPath(canvas, path);
    OH_Drawing_CanvasDetachPen(canvas);
    OH_Drawing_PathDestroy(path);
    OH_Drawing_PenDestroy(pen);
}

/**
 * Precipitation.kt:42-45 — the hourly intensity thresholds, in millimetres per hour. They are the
 * bar chart's axis floor and its three trendHorizontalLines.
 */
constexpr double kPrecipitationHourlyLight = 5.0;
constexpr double kPrecipitationHourlyMedium = 10.0;
constexpr double kPrecipitationHourlyHeavy = 15.0;
/** `barColorFill = Fill(Color(60, 116, 160))` — DetailsPrecipitation.kt:377, and kt:621. */
constexpr uint32_t kPrecipitationBarColor = 0xFF3C74A0;

/**
 * `SunImplementor.themeColor` = rgb(253, 188, 76), and the moon's is
 * `getBrighterColor(MeteorShowerImplementor.themeColor)`: rgb(20, 28, 44) taken to HSV, saturation
 * -0.25 and value +0.25, back to RGB (MaterialWeatherThemeDelegate.kt:38-44) = rgb(76, 87, 108).
 */
constexpr uint32_t kSunLineColor = 0xFFFDBC4C;
constexpr uint32_t kMoonLineColor = 0xFF4C576C;

/**
 * `PointConnector.cubic(curvature = (PI - 2) / PI)` — DetailsSunMoon.kt:123-125, the one chart that
 * curves.
 */
const double kCubicCurvature = (3.14159265358979323846 - 2.0) / 3.14159265358979323846;

/** BreezyLineChart.kt:327-330 and the axis furniture around it. */
constexpr double kLineChartHeightMin = 100.0;
constexpr double kLineThickness = 5.0;
constexpr double kAxisLabelSize = 12.0;
constexpr double kAxisLabelPadding = 4.0;
constexpr double kTickLength = 4.0;
constexpr double kTopAxisHeight = 23.0;  // DetailsPressure.kt:269
constexpr double kArrowSize = 18.0;

}  // namespace

/**
 * The ephemeris chart's pair of gradients (DetailsSunMoon.kt:107-122).
 *
 * ONE colour, but two maps: the LINE keeps the colour above the horizon and drops to it at 50 %
 * below, while the AREA keeps it above and fades to nothing below. The stops sit at ±0.1°, so on a
 * -90..90 axis the change is a hard edge at the horizon rather than a wash.
 */
void DetailsScreen::ScaleFill(std::vector<ChartStop>* lineStops, std::vector<ChartStop>* areaStops,
                              uint32_t color) {
    lineStops->push_back({0.1, color});
    lineStops->push_back({-0.1, (color & 0x00FFFFFFu) | 0x80000000u});
    areaStops->push_back({0.1, color});
    areaStops->push_back({-0.1, color & 0x00FFFFFFu});
}

/**
 * `getMappedAstroValues` (DetailsSunMoon.kt:226-316) — the body's altitude at every noon and nadir
 * that can show on the chart.
 *
 * The Kotlin walks two sorted lists of rise and set instants: whichever comes first decides whether
 * the next sample is a noon (halfway from a rise to the following set) or a nadir (halfway from a
 * set to the following rise), and the altitude is computed THERE. The cubic connector does the rest,
 * which is why two samples a day are enough to draw the curve.
 *
 * Android is handed the lists by the view model; here they are scanned for the two days on each
 * side of the one being shown, which is more than the bookending needs and covers the moon's
 * ~50-minute daily drift.
 */
void DetailsScreen::MappedAstroValues(bool isSun, int64_t startMs, int64_t endMs,
                                      std::vector<double>* xs, std::vector<double>* ys) const {
    std::vector<int64_t> riseTimes;
    std::vector<int64_t> setTimes;
    for (int day = -2; day <= 2; ++day) {
        const int64_t dayStart = startMs + static_cast<int64_t>(day) * kMillisPerDay;
        const RiseSet times = isSun ? SunRiseSetOn(dayStart, latitude_, longitude_)
                                    : MoonRiseSetOn(dayStart, latitude_, longitude_);
        if (times.hasRise) {
            riseTimes.push_back(times.rise);
        }
        if (times.hasSet) {
            setTimes.push_back(times.set);
        }
    }
    std::sort(riseTimes.begin(), riseTimes.end());
    std::sort(setTimes.begin(), setTimes.end());

    // kt:252-263 — bookend each list with one extra cycle so the first and last day look complete.
    if (riseTimes.size() > 1 && setTimes.size() > 1) {
        riseTimes.insert(riseTimes.begin(), riseTimes[0] - (riseTimes[1] - riseTimes[0]));
        setTimes.insert(setTimes.begin(), setTimes[0] - (setTimes[1] - setTimes[0]));
        riseTimes.push_back(riseTimes.back() + (riseTimes.back() - riseTimes[riseTimes.size() - 2]));
        setTimes.push_back(setTimes.back() + (setTimes.back() - setTimes[setTimes.size() - 2]));
    }
    if (riseTimes.empty() || setTimes.empty()) {
        return;
    }

    // kt:265-295 — start at the last crossing before the day and stop at the first one after it.
    size_t riseCounter = 0;
    size_t setCounter = 0;
    while (riseCounter < riseTimes.size() && riseTimes[riseCounter] < startMs) {
        ++riseCounter;
    }
    if (riseCounter > 0) {
        --riseCounter;
    }
    while (setCounter < setTimes.size() && setTimes[setCounter] < startMs) {
        ++setCounter;
    }
    if (setCounter > 0) {
        --setCounter;
    }
    int riseLimit = static_cast<int>(riseTimes.size()) - 1;
    int setLimit = static_cast<int>(setTimes.size()) - 1;
    while (riseLimit >= 0 && riseTimes[static_cast<size_t>(riseLimit)] > endMs) {
        --riseLimit;
    }
    if (riseLimit < static_cast<int>(riseTimes.size()) - 1) {
        ++riseLimit;
    }
    while (setLimit >= 0 && setTimes[static_cast<size_t>(setLimit)] > endMs) {
        --setLimit;
    }
    if (setLimit < static_cast<int>(setTimes.size()) - 1) {
        ++setLimit;
    }

    while (static_cast<int>(riseCounter) <= riseLimit && static_cast<int>(setCounter) <= setLimit) {
        int64_t at = 0;
        if (riseTimes[riseCounter] < setTimes[setCounter]) {
            at = riseTimes[riseCounter] + (setTimes[setCounter] - riseTimes[riseCounter]) / 2;
            ++riseCounter;
        } else {
            at = setTimes[setCounter] + (riseTimes[riseCounter] - setTimes[setCounter]) / 2;
            ++setCounter;
        }
        xs->push_back(static_cast<double>(at));
        ys->push_back(isSun ? SunAltitudeDegrees(at, latitude_, longitude_)
                            : MoonAltitudeDegrees(at, latitude_, longitude_));
    }
}

double DetailsScreen::ChartHeight() const {
    /*
     * kt:244 - max(100 dp, windowHeightInDp / 4), and `windowHeightInDp` is
     * `resources.displayMetrics.heightPixels`: the ACTIVITY WINDOW, which on the reference is the
     * display less the status bar (its DetailsActivity frame is [0,130][1260,2720]).
     *
     * Measured: Android's chart is 648 px and this port's was 680, because it divided the whole
     * 2720 px display. (2720 - 123) / 4 = 649, one pixel off Android's.
     */
    const double windowHeightPx = height_ - topInset_;
    return std::max(kLineChartHeightMin, windowHeightPx / density_ / 4.0) * density_;
}

std::vector<DetailsScreen::ChartStop> DetailsScreen::TemperatureStops() {
    const auto t = [](double celsius) { return TemperatureValue(celsius); };
    return {{t(47.0), 0xFF470E00},  {t(30.0), 0xFFE85319},  {t(21.0), 0xFFF3B704},
            {t(10.0), 0xFF809318},  {t(1.0), 0xFF447D63},   {t(0.0), 0xFF5D85C6},
            {t(-4.0), 0xFF64A6BD},  {t(-8.0), 0xFF6ABFB5},  {t(-15.0), 0xFF9DDBD9},
            {t(-25.0), 0xFF8F59A9}, {t(-40.0), 0xFFA24691}, {t(-55.0), 0xFFCAACC3},
            {t(-70.0), 0xFF734669}};
}

DetailsScreen::ChartSpec DetailsScreen::BuildChart(int variant) const {
    ChartSpec spec;
    if (dayIndex_ < 0 || dayIndex_ >= static_cast<int>(weather_.dailyForecast.size())) {
        return spec;
    }
    const std::vector<Hourly> hours = HoursForDay(dayIndex_);
    spec.minX = static_cast<double>(
        LocalStartOfDay(weather_.dailyForecast[static_cast<size_t>(dayIndex_)].date,
                        utcOffsetSeconds_));
    spec.maxX = spec.minX + static_cast<double>(kMillisPerDay);

    const auto push = [&spec](int64_t at, double value) {
        spec.xs.push_back(static_cast<double>(at));
        spec.ys.push_back(value);
    };

    /*
     * VARIANT 1 IS THE SECOND GROUP'S CHART — which chart that is depends on the tag.
     *
     * Three screens have a second heading, header and chart. On Precipitation (kt:166-213) and on
     * CONDITIONS (DetailsConditions.kt:360-405) it is the probability group; on HUMIDITY it is the
     * DEW POINT group (DetailsHumidity.kt:176-216), which was missing from both ports entirely.
     * ArkUIApp carries the same split.
     */
    if (variant == 1 && tag_ == DetailTag::Humidity) {
        // `DewPointChart` (kt:449-523) — a temperature line on the temperature ramp, bounds
        // rounded out to the unit's chart step, exactly like the Conditions chart.
        for (const Hourly& hour : hours) {
            if (hour.dewPoint.has_value()) {
                push(hour.date, TemperatureValue(*hour.dewPoint));
            }
        }
        if (spec.xs.size() < static_cast<size_t>(kChartMinCount)) {
            return spec;
        }
        spec.step = TemperatureChartStep();
        spec.unitLabel = TemperatureUnitLabel();
        const auto bounds = std::minmax_element(spec.ys.begin(), spec.ys.end());
        spec.maxY = RoundUpTo(*bounds.second, spec.step);
        spec.minY = RoundDownTo(*bounds.first, spec.step);
        if (spec.maxY == spec.minY) {
            spec.maxY = spec.minY + spec.step;
        }
        spec.stops = TemperatureStops();
        spec.valid = true;
        return spec;
    }

    /*
     * An ordinary line chart, 0-100 %, every 20 %, and the one chart whose "gradient" is a single
     * colour top and bottom, which the Kotlin marks with a TODO of its own (kt:620-623).
     */
    if (variant == 1) {
        for (const Hourly& hour : hours) {
            if (hour.precipitationProbability.has_value()) {
                push(hour.date, *hour.precipitationProbability);
            }
        }
        if (spec.xs.size() < static_cast<size_t>(kChartMinCount)) {
            return spec;
        }
        spec.step = 20.0;
        spec.minY = 0.0;
        spec.maxY = 100.0;
        spec.unitLabel = "%";
        spec.stops = {{100.0, kPrecipitationBarColor}, {0.0, kPrecipitationBarColor}};
        spec.decimals = 0;
        spec.valid = true;
        return spec;
    }

    switch (tag_) {
        case DetailTag::Pressure: {
            for (const Hourly& hour : hours) {
                if (hour.pressure.has_value()) {
                    push(hour.date, ValueIn(UnitFamily::Pressure, *hour.pressure));
                }
            }
            spec.step = 15.0;  // PressureUnit.HECTOPASCAL.chartStep
            const double standard = 1013.25;
            spec.unitLabel = " " + PressureUnitLabel();
            spec.keyLines.push_back({standard, "Standard"});  // strings.xml:355
            double dataMax = standard;
            double dataMin = standard;
            if (!spec.ys.empty()) {
                const auto bounds = std::minmax_element(spec.ys.begin(), spec.ys.end());
                dataMin = *bounds.first;
                dataMax = *bounds.second;
            }
            spec.maxY = RoundUpTo(std::max(standard + spec.step * 1.6, dataMax), spec.step);
            spec.minY = RoundDownTo(std::min(standard - spec.step * 1.6, dataMin), spec.step);
            // kt:271-291, hPa.
            spec.stops = {{1080.0, 0xFF300818}, {1046.0, 0xFF6F1840},   {1038.0, 0xFF8E2F39},
                          {1030.0, 0xFF9F512C}, {1024.0, 0xFFA37443},   {1019.0, 0xFFA7936B},
                          {1015.25, 0xFFB0AE98}, {1013.25, 0xFFB6B6B6}, {1011.25, 0xFF9BB7AC},
                          {1007.0, 0xFF67A29B}, {1002.0, 0xFF1A8C93},   {995.0, 0xFF007592},
                          {986.0, 0xFF005A94},  {976.0, 0xFF003492},    {950.0, 0xFF002060},
                          {900.0, 0xFF081030}};
            // The arrows compare each hour with the previous, dead-banded at 0.5 in the MODEL unit.
            bool hasPrevious = false;
            double previous = 0.0;
            for (const Hourly& hour : hours) {
                if (!hour.pressure.has_value()) {
                    continue;
                }
                if (hasPrevious) {
                    const double delta = *hour.pressure - previous;
                    spec.arrowXs.push_back(static_cast<double>(hour.date));
                    spec.arrowKinds.push_back(delta >= 0.5 ? 1 : (delta <= -0.5 ? -1 : 0));
                }
                previous = *hour.pressure;
                hasPrevious = true;
            }
            break;
        }
        case DetailTag::Humidity: {
            for (const Hourly& hour : hours) {
                if (hour.relativeHumidity.has_value()) {
                    push(hour.date, *hour.relativeHumidity);
                }
            }
            spec.step = 20.0;  // RatioUnit.PERCENT.chartStep
            spec.maxY = 100.0;
            spec.minY = 0.0;
            spec.unitLabel = "%";
            // DetailsHumidity.kt:331-345.
            spec.stops = {{100.0, 0xFF384672}, {97.0, 0xFF38629D}, {93.0, 0xFF387BAD},
                          {90.0, 0xFF3884AD},  {87.0, 0xFF3887AD}, {83.0, 0xFF3894AD},
                          {80.0, 0xFF389DAD},  {75.0, 0xFF38A0AD}, {70.0, 0xFF38AEAD},
                          {60.0, 0xFF38AD79},  {50.0, 0xFF69AD38}, {40.0, 0xFFAD9238},
                          {30.0, 0xFFAD6E38},  {0.0, 0xFFAD5538}};
            break;
        }
        case DetailTag::CloudCover: {
            for (const Hourly& hour : hours) {
                if (hour.cloudCover.has_value()) {
                    push(hour.date, *hour.cloudCover);
                }
            }
            spec.step = 20.0;
            spec.maxY = 100.0;
            spec.minY = 0.0;
            spec.unitLabel = "%";
            // DetailsCloudCover.kt:288-294.
            spec.stops = {{100.0, 0xFFD5D5CD}, {98.0, 0xFFC6C9C9}, {95.0, 0xFFABB4B3},
                          {50.0, 0xFF747474},  {10.0, 0xFF847746}, {0.0, 0xFF928246}};
            break;
        }
        case DetailTag::Visibility: {
            for (const Hourly& hour : hours) {
                if (hour.visibility.has_value()) {
                    push(hour.date, ValueIn(UnitFamily::Distance, *hour.visibility));
                }
            }
            // kt:288-300 - a floor of 22 850 m so the axis reaches ~75 000 ft once rounded.
            const double floorInUnit = ValueIn(UnitFamily::Distance, 22850.0);
            double dataMax = floorInUnit;
            if (!spec.ys.empty()) {
                dataMax = *std::max_element(spec.ys.begin(), spec.ys.end());
            }
            const double rawMax = std::max(floorInUnit, dataMax);
            spec.step = DistanceChartStep(rawMax);
            spec.maxY = RoundUpTo(rawMax, spec.step);
            spec.minY = 0.0;
            spec.unitLabel = " " + DistanceUnitLabel();
            // kt:327-336, metres converted to the display unit.
            const auto d = [](double metres) { return ValueIn(UnitFamily::Distance, metres); };
            spec.stops = {{d(20000.0), 0xFF778D78}, {d(15000.0), 0xFF5BA763},
                          {d(9000.0), 0xFF5AA95A},  {d(8000.0), 0xFF627AA0},
                          {d(6000.0), 0xFF627AA0},  {d(5000.0), 0xFFA75B5B},
                          {d(2200.0), 0xFFA75B5B},  {d(1600.0), 0xFFA261A0},
                          {0.0, 0xFFA65DA5}};
            break;
        }
        case DetailTag::UvIndex: {
            for (const Hourly& hour : hours) {
                if (hour.uvIndex.has_value()) {
                    push(hour.date, *hour.uvIndex);
                }
            }
            // kt:251-257 - at least the "high" threshold, else the peak rounded up by one.
            double dataMax = 0.0;
            if (!spec.ys.empty()) {
                dataMax = *std::max_element(spec.ys.begin(), spec.ys.end());
            }
            spec.maxY = std::max(kUvIndexHigh, std::round(dataMax) + 1.0);
            spec.minY = 0.0;
            spec.step = 1.0;  // "every rounded UVI"
            spec.keyLines.push_back({kUvIndexMiddle, "Alert"});  // `uv_alert_level`
            // kt:286-293.
            spec.stops = {{19.0, 0xFFFFFFFF}, {11.0, kColorLevel5}, {10.0, kColorLevel4},
                          {7.0, kColorLevel3}, {5.0, kColorLevel2}, {2.0, kColorLevel1},
                          {0.0, 0xFF6E6E6E}};
            break;
        }
        case DetailTag::Wind: {
            /*
             * WindChart (DetailsWind.kt:336-490).
             *
             * ONE series, not two. The Kotlin adds a second for gusts (kt:359-366), but neither
             * port's `Hourly` carries gusts - only `Current` does - so there is nothing to plot.
             * Same in both ports, so the comparison stays like-for-like; a data-layer gap, not a
             * chart one.
             */
            for (const Hourly& hour : hours) {
                if (hour.windSpeed.has_value()) {
                    push(hour.date, ValueIn(UnitFamily::Speed, *hour.windSpeed));
                }
            }
            if (spec.ys.empty()) {
                return spec;
            }
            spec.step = SpeedChartStep();
            spec.unitLabel = " " + SpeedUnitLabel();
            // kt:339-347 - a floor of 15 m/s so a calm day still gets a usable axis.
            const double floorSpeed = ValueIn(UnitFamily::Speed, 15.0);
            const double dataMax = *std::max_element(spec.ys.begin(), spec.ys.end());
            spec.maxY = RoundUpTo(std::max(floorSpeed, dataMax), spec.step);
            spec.minY = 0.0;
            /*
             * kt:477-485 - a Beaufort rule at each end, labelled with its strength name. Beaufort 7
             * is 13.9 m/s and Beaufort 3 is 3.4 m/s (the WIND_SPEED_* table, held in cm/s).
             */
            const double bf7 = ValueIn(UnitFamily::Speed, 13.9);
            const double bf3 = ValueIn(UnitFamily::Speed, 3.4);
            if (spec.maxY > bf7) {
                spec.keyLines.push_back({bf7, "Moderate gale"});  // wind_strength_7
            }
            if (spec.maxY < ValueIn(UnitFamily::Speed, 18.9)) {
                spec.keyLines.push_back({bf3, "Gentle breeze"});  // wind_strength_3
            }
            // kt:383-416; colors.xml windStrength_bf*.
            const auto w = [](double ms) { return ValueIn(UnitFamily::Speed, ms); };
            spec.stops = {{w(104.0), 0xFF808080},   {w(77.0), 0xFFCDCA70},
                          {w(51.0), 0xFFDBD487},    {w(46.0), 0xFFE7D7D7},
                          {w(36.0), 0xFF7D44A5},    {w(30.5), 0xFF5C9098},
                          {w(26.4), 0xFF44698D},    {w(24.475), 0xFF6D61A3},
                          {w(22.55), 0xFF754A93},   {w(18.9), 0xFFAF5088},
                          {w(17.175), 0xFF813A4E},  {w(15.45), 0xFFA1645C},
                          {w(13.85), 0xFF9F7F3A},   {w(12.25), 0xFFA79D51},
                          {w(9.3), 0xFF359F35},     {w(6.7), 0xFF53A553},
                          {w(4.4), 0xFF4D8D7B},     {w(2.4), 0xFF4A94A9},
                          {w(1.0), 0xFF39619F},     {0.0, 0xFF6271B7}};
            break;
        }
        case DetailTag::Conditions:
        case DetailTag::FeelsLike: {
            /*
             * TemperatureChart (DetailsConditions.kt:769-905). This is the screen every daily and
             * hourly trend column opens, so it is the one that most needed a chart.
             *
             * Feels like plots the feels-like temperature AND the actual one behind it in flat
             * grey (kt:869-872); Conditions plots one line.
             */
            const bool real = tag_ == DetailTag::Conditions;
            for (const Hourly& hour : hours) {
                const Maybe shown = real ? hour.temperature.temperature
                                         : hour.temperature.feelsLikeTemperature;
                if (shown.has_value()) {
                    push(hour.date, TemperatureValue(*shown));
                }
                if (!real && hour.temperature.temperature.has_value()) {
                    spec.xs2.push_back(static_cast<double>(hour.date));
                    spec.ys2.push_back(TemperatureValue(*hour.temperature.temperature));
                }
                // kt:857-866 — the top axis is an icon per hour, the same placer as the arrows.
                if (hour.weatherKind != WeatherKind::None) {
                    spec.topIconXs.push_back(static_cast<double>(hour.date));
                    spec.topIconAssets.push_back(
                        std::string(IconBaseName(hour.weatherKind, hour.daylight)) + ".png");
                }
            }
            spec.step = TemperatureChartStep();
            spec.unitLabel = TemperatureUnitLabel();
            /*
             * kt:781-810 pulls the bounds out to the monthly NORMALS as well. Neither port has
             * normals: ArkUIApp leaves `weather.normals` null on purpose (Open-Meteo is not a
             * normals source) and this model has no such field, so the "Normal" key lines never
             * appear in either. Scope, not an omission - and why these bounds are the data's own.
             */
            std::vector<double> all = spec.ys;
            all.insert(all.end(), spec.ys2.begin(), spec.ys2.end());
            if (all.empty()) {
                return spec;
            }
            const auto bounds = std::minmax_element(all.begin(), all.end());
            spec.maxY = RoundUpTo(*bounds.second, spec.step);
            spec.minY = RoundDownTo(*bounds.first, spec.step);
            if (spec.maxY == spec.minY) {
                spec.maxY = spec.minY + spec.step;
            }
            spec.stops = TemperatureStops();
            break;
        }
        case DetailTag::AirQuality: {
            /*
             * AirQualityChart (DetailsAirQuality.kt:438-540). The plotted value is the INDEX with
             * no pollutant picked and that pollutant's CONCENTRATION with one, so the axis changes
             * with the selection — which is why the chart could not be ported before the picker.
             */
            const PollutantDef* def = PollutantById(pollutantId_);
            for (const Hourly& hour : hours) {
                if (!hour.airQuality.Any()) {
                    continue;
                }
                const Maybe value = def == nullptr
                                        ? OverallIndexOf(hour.airQuality)
                                        : ConcentrationOf(hour.airQuality, def->id);
                if (value.has_value()) {
                    push(hour.date, *value);
                }
            }
            if (spec.ys.empty()) {
                return spec;
            }
            const std::vector<int>& bands = def == nullptr ? kAqiBands : def->thresholds;
            /*
             * kt:447-458 — the floor is the pollutant's own `maxY`, or aqiThresholds[4] = 150 for
             * the index, raised to the data's peak when that is higher. Concentrations are rounded
             * up to a whole unit first (`roundUpToNearestMultiplier(1.0)`).
             */
            const double peak = std::ceil(*std::max_element(spec.ys.begin(), spec.ys.end()));
            spec.maxY = std::max(def == nullptr ? static_cast<double>(kAqiBands[4])
                                                : static_cast<double>(def->maxY),
                                 peak);
            spec.minY = 0.0;
            spec.unitLabel = def == nullptr ? "" : " " + ConcentrationUnitLabel(*def);
            /*
             * `SpecificVerticalAxisItemPlacer` (kt:526-538): the labels sit on the BAND EDGES, not
             * on a uniform step, with the axis maximum appended when the data ran past the last.
             */
            for (int band : bands) {
                if (band <= spec.maxY) {
                    spec.axisValues.push_back(static_cast<double>(band));
                }
            }
            if (spec.maxY > bands.back()) {
                spec.axisValues.push_back(spec.maxY);
            }
            spec.step = bands.size() > 1 ? static_cast<double>(bands[1] - bands[0]) : 1.0;
            // kt:509-516 — every band edge takes its own level colour.
            for (size_t i = 0; i < bands.size(); ++i) {
                spec.stops.push_back(
                    {static_cast<double>(bands[i]), kLevelColors[std::min<size_t>(i, 5)]});
            }
            // kt:518-521 — one rule, at the fourth band, labelled with that level's name.
            spec.keyLines.push_back(
                {static_cast<double>(bands[3]), AqiLevelName(kAqiBands[3])});
            break;
        }
        case DetailTag::Precipitation: {
            /*
             * PrecipitationChart (kt:339-415) — the one COLUMN chart on screen 4, and the reason
             * `BreezyBarChart` exists at all.
             */
            for (const Hourly& hour : hours) {
                push(hour.date, ValueIn(UnitFamily::Precipitation, hour.precipitationTotal));
            }
            if (spec.ys.empty()) {
                return spec;
            }
            spec.bars = true;
            spec.barColor = kPrecipitationBarColor;
            spec.step = PrecipitationChartStep();
            spec.unitLabel = " " + PrecipitationUnitLabel();
            // kt:349-354 — a floor of PRECIPITATION_HOURLY_HEAVY so a dry day gets a usable axis.
            const double heavy = ValueIn(UnitFamily::Precipitation, kPrecipitationHourlyHeavy);
            spec.minY = 0.0;
            spec.maxY = RoundUpTo(
                std::max(heavy, *std::max_element(spec.ys.begin(), spec.ys.end())), spec.step);
            /*
             * kt:391-411 — the "Heavy" rule always, and the other two only while the axis has not
             * run past twice heavy, where three labels would crowd the bottom of the plot.
             */
            spec.keyLines.push_back({heavy, "Heavy"});  // precipitation_intensity_heavy
            if (spec.maxY < ValueIn(UnitFamily::Precipitation, kPrecipitationHourlyHeavy * 2.0)) {
                spec.keyLines.push_back(
                    {ValueIn(UnitFamily::Precipitation, kPrecipitationHourlyMedium), "Moderate"});
                spec.keyLines.push_back(
                    {ValueIn(UnitFamily::Precipitation, kPrecipitationHourlyLight), "Light"});
            }
            break;
        }
        case DetailTag::SunMoon: {
            /*
             * EphemerisChart (EphemerisChart.kt:72-159, fed by DetailsSunMoon.kt:152-224).
             *
             * Not a weather series: it plots the sun's and the moon's ALTITUDE in degrees over the
             * day, sampled at each body's noon and nadir and joined by a cubic connector, on a
             * fixed -90..90 axis labelled only at 0 and 90.
             */
            std::vector<double> sunXs;
            std::vector<double> sunYs;
            std::vector<double> moonXs;
            std::vector<double> moonYs;
            MappedAstroValues(true, static_cast<int64_t>(spec.minX),
                              static_cast<int64_t>(spec.maxX), &sunXs, &sunYs);
            MappedAstroValues(false, static_cast<int64_t>(spec.minX),
                              static_cast<int64_t>(spec.maxX), &moonXs, &moonYs);
            if (sunYs.size() < kChartMinCount && moonYs.size() < kChartMinCount) {
                return spec;
            }
            spec.curved = true;
            spec.minY = -90.0;
            spec.maxY = 90.0;
            // "90 is the zenith, but add a safe-zone" (kt:84-90); labels at 0 and 90 only.
            spec.axisValues = {0.0, 90.0};
            spec.step = 90.0;
            // The formatter is TemperatureUnit.CELSIUS at NARROW width — the Kotlin calls it a hack.
            spec.unitLabel = "°";
            if (sunYs.size() < kChartMinCount) {
                spec.xs = moonXs;
                spec.ys = moonYs;
                ScaleFill(&spec.stops, &spec.areaStops, kMoonLineColor);
            } else {
                // The sun is the LAST series the Kotlin adds, so it draws over the moon.
                spec.xs = sunXs;
                spec.ys = sunYs;
                spec.xs2 = moonXs;
                spec.ys2 = moonYs;
                ScaleFill(&spec.stops, &spec.areaStops, kSunLineColor);
                ScaleFill(&spec.stops2, &spec.areaStops2, kMoonLineColor);
            }
            break;
        }
        default:
            // Pollen is a GRID, not a chart of any kind (DetailsPollen.kt:70-77).
            return spec;
    }

    if (spec.ys.size() < kChartMinCount) {
        return spec;
    }
    spec.decimals = spec.step < 1.0 ? 1 : 0;
    // A canvas gradient wants its stops in increasing OFFSET order, which is decreasing value.
    const auto byValue = [](const ChartStop& a, const ChartStop& b) { return a.value > b.value; };
    std::sort(spec.stops.begin(), spec.stops.end(), byValue);
    std::sort(spec.stops2.begin(), spec.stops2.end(), byValue);
    std::sort(spec.areaStops.begin(), spec.areaStops.end(), byValue);
    std::sort(spec.areaStops2.begin(), spec.areaStops2.end(), byValue);
    spec.valid = true;
    return spec;
}

double DetailsScreen::DrawChart(OH_Drawing_Canvas* canvas, const ChartSpec& spec, double left,
                                double top, double width, int variant) {
    const double height = ChartHeight();
    if (!spec.valid || width <= 0.0) {
        return 0.0;
    }
    // colorTextGrey / colorTextGrey2nd - BreezyLineChart.kt:142-144 picks between them by theme,
    // which is exactly what this ColorPair holds.
    const uint32_t labelColor = kTextGrey.Resolve(dark_);
    const uint32_t lineColor = kComposeOutline.Resolve(dark_);

    TextStyleSpec label;
    label.sizeVp = kAxisLabelSize;
    label.color = labelColor;
    label.singleLine = true;

    /* the plot rectangle: the end axis takes the right, the two horizontal axes the ends */
    double widest = 0.0;
    if (!spec.axisValues.empty()) {
        for (double value : spec.axisValues) {
            widest = std::max(
                widest,
                MeasureTextWidth(GroupThousands(value, spec.decimals) + spec.unitLabel, label));
        }
    } else {
        for (double value = spec.minY; value <= spec.maxY + 1e-6; value += spec.step) {
            widest = std::max(
                widest,
                MeasureTextWidth(GroupThousands(value, spec.decimals) + spec.unitLabel, label));
        }
    }
    // Pressure reserves the band for its arrows; the conditions chart reserves it for its icons.
    const bool hasTopAxis = !spec.arrowXs.empty() || !spec.topIconAssets.empty();
    const double plotLeft = left;
    const double plotRight =
        left + width - widest - (kAxisLabelPadding * 2.0 + kTickLength) * density_;
    /*
     * Without a top axis the plot still needs half a label of headroom, or the TOPMOST end-axis
     * label is centred on the plot's top edge and half of it is cut off.
     */
    const double plotTop =
        top + (hasTopAxis ? kTopAxisHeight * density_
                          : (kAxisLabelSize / 2.0 + kAxisLabelPadding) * density_);
    const double plotBottom = top + height - (kAxisLabelSize + kAxisLabelPadding * 2.0) * density_;
    if (plotRight <= plotLeft || plotBottom <= plotTop) {
        return height;
    }

    const auto xAt = [&](double value) {
        return plotLeft + (value - spec.minX) / (spec.maxX - spec.minX) * (plotRight - plotLeft);
    };
    const auto yAt = [&](double value) {
        return plotBottom - (value - spec.minY) / (spec.maxY - spec.minY) * (plotBottom - plotTop);
    };

    /*
     * The end axis. `VerticalAxis.ItemPlacer.step` treats the tag's chartStep as a MINIMUM, not a
     * fixed interval: it takes the smallest multiple of it whose labels still fit. Without this,
     * metres per second (chartStep 0.5) would draw thirty gridlines over a 15 m/s range. Pressure
     * at 15 hPa and UV at 1 already fit, so they are unaffected.
     */
    double axisStep = spec.step;
    {
        const double span = spec.maxY - spec.minY;
        const double minSpacing = kAxisLabelSize * 2.2 * density_;
        if (span > 0.0 && spec.step > 0.0) {
            for (int multiple = 1; multiple <= 64; ++multiple) {
                axisStep = spec.step * multiple;
                if (axisStep / span * (plotBottom - plotTop) >= minSpacing) {
                    break;
                }
            }
        }
    }
    /*
     * `SpecificVerticalAxisItemPlacer` wins when the spec supplies one — the air-quality chart's
     * labels are its band edges, which are not evenly spaced.
     */
    std::vector<double> ticks = spec.axisValues;
    if (ticks.empty()) {
        for (double value = spec.minY; value <= spec.maxY + 1e-6; value += axisStep) {
            ticks.push_back(value);
        }
    }
    for (double value : ticks) {
        const double y = yAt(value);
        DrawDashedLine(canvas, plotLeft, y, plotRight, y, density_, lineColor);
        FillRect(canvas, plotRight, y - 0.5, plotRight + kTickLength * density_, y + 0.5,
                 lineColor);
        const std::string text = GroupThousands(value, spec.decimals) + spec.unitLabel;
        const double textHeight = MeasureTextHeight(text, widest + 1.0, label);
        DrawText(canvas, text, plotRight + (kTickLength + kAxisLabelPadding) * density_,
                 y - textHeight / 2.0, widest + 1.0, label);
    }
    FillRect(canvas, plotRight - 0.5, plotTop, plotRight + 0.5, plotBottom, lineColor);

    /* the bottom axis: local 06:00, 12:00, 18:00 (AxisItemPlacer.kt:45-50) */
    FillRect(canvas, plotLeft, plotBottom - 0.5, plotRight, plotBottom + 0.5, lineColor);
    TextStyleSpec timeLabel = label;
    timeLabel.alignment = TextAlignment::Center;
    for (int hour = 6; hour <= 18; hour += 6) {
        const double at = spec.minX + static_cast<double>(hour) * kMillisPerHour;
        const double x = xAt(at);
        FillRect(canvas, x - 0.5, plotBottom, x + 0.5, plotBottom + kTickLength * density_,
                 lineColor);
        DrawDashedLine(canvas, x, plotTop, x, plotBottom, density_, lineColor);
        const std::string text = HourLabel(static_cast<int64_t>(at), utcOffsetSeconds_, false);
        DrawText(canvas, text, x - 60.0 * density_,
                 plotBottom + (kTickLength + kAxisLabelPadding) * density_, 120.0 * density_,
                 timeLabel);
    }

    /*
     * Vico paints its layer inside `layerBounds` and nothing outside it. That only started to
     * matter with the ephemeris chart, whose curve runs from before the day's first sample to after
     * its last — without the clip the spline crosses the end axis and draws over its labels.
     */
    OH_Drawing_CanvasSave(canvas);
    OH_Drawing_Rect* plotClip = OH_Drawing_RectCreate(
        static_cast<float>(plotLeft), static_cast<float>(plotTop - kLineThickness * density_),
        static_cast<float>(plotRight), static_cast<float>(plotBottom + kLineThickness * density_));
    OH_Drawing_CanvasClipRect(canvas, plotClip, INTERSECT, true);
    OH_Drawing_RectDestroy(plotClip);

    /*
     * The polyline through a series, or the cubic spline through it when the spec asks for one.
     *
     * `PointConnector.cubic(curvature)` puts each segment's control points on the horizontal at the
     * two ends, `curvature × dx` in from each. Only the ephemeris chart sets it.
     */
    const auto buildPath = [&](const std::vector<double>& xs, const std::vector<double>& ys) {
        OH_Drawing_Path* path = OH_Drawing_PathCreate();
        if (xs.empty()) {
            return path;
        }
        OH_Drawing_PathMoveTo(path, static_cast<float>(xAt(xs[0])),
                              static_cast<float>(yAt(ys[0])));
        for (size_t i = 1; i < xs.size(); ++i) {
            const double x0 = xAt(xs[i - 1]);
            const double y0 = yAt(ys[i - 1]);
            const double x1 = xAt(xs[i]);
            const double y1 = yAt(ys[i]);
            if (spec.curved) {
                const double grip = (x1 - x0) * kCubicCurvature;
                OH_Drawing_PathCubicTo(path, static_cast<float>(x0 + grip),
                                       static_cast<float>(y0), static_cast<float>(x1 - grip),
                                       static_cast<float>(y1), static_cast<float>(x1),
                                       static_cast<float>(y1));
            } else {
                OH_Drawing_PathLineTo(path, static_cast<float>(x1), static_cast<float>(y1));
            }
        }
        return path;
    };

    /** The vertical shader of a value scale — `getShader` (BreezyLineChart.kt:315-325). */
    const auto drawSeries = [&](const std::vector<double>& xs, const std::vector<double>& ys,
                                const std::vector<ChartStop>& stops,
                                const std::vector<ChartStop>& areaStops) {
        if (xs.size() < 2) {
            return;
        }
        const auto makeShader = [&](const std::vector<ChartStop>& source) {
            std::vector<uint32_t> colors;
            std::vector<float> positions;
            for (const ChartStop& stop : source) {
                const double offset = 1.0 - (stop.value - spec.minY) / (spec.maxY - spec.minY);
                colors.push_back(stop.color);
                positions.push_back(static_cast<float>(std::max(0.0, std::min(1.0, offset))));
            }
            OH_Drawing_Point* from = OH_Drawing_PointCreate(static_cast<float>(plotLeft),
                                                            static_cast<float>(plotTop));
            OH_Drawing_Point* to = OH_Drawing_PointCreate(static_cast<float>(plotLeft),
                                                          static_cast<float>(plotBottom));
            OH_Drawing_ShaderEffect* shader = OH_Drawing_ShaderEffectCreateLinearGradient(
                from, to, colors.data(), positions.data(), static_cast<uint32_t>(colors.size()),
                CLAMP);
            OH_Drawing_PointDestroy(from);
            OH_Drawing_PointDestroy(to);
            return shader;
        };

        OH_Drawing_Path* line = buildPath(xs, ys);
        OH_Drawing_Path* area = buildPath(xs, ys);
        OH_Drawing_PathLineTo(area, static_cast<float>(xAt(xs.back())),
                              static_cast<float>(plotBottom));
        OH_Drawing_PathLineTo(area, static_cast<float>(xAt(xs.front())),
                              static_cast<float>(plotBottom));
        OH_Drawing_PathClose(area);

        OH_Drawing_ShaderEffect* areaShader =
            makeShader(areaStops.empty() ? stops : areaStops);
        OH_Drawing_Brush* brush = OH_Drawing_BrushCreate();
        OH_Drawing_BrushSetAntiAlias(brush, true);
        OH_Drawing_BrushSetShaderEffect(brush, areaShader);
        OH_Drawing_BrushSetAlpha(brush, 128);  // kt:283
        OH_Drawing_CanvasAttachBrush(canvas, brush);
        OH_Drawing_CanvasDrawPath(canvas, area);
        OH_Drawing_CanvasDetachBrush(canvas);
        OH_Drawing_BrushDestroy(brush);
        OH_Drawing_ShaderEffectDestroy(areaShader);

        OH_Drawing_ShaderEffect* lineShader = makeShader(stops);
        OH_Drawing_Pen* pen = OH_Drawing_PenCreate();
        OH_Drawing_PenSetAntiAlias(pen, true);
        OH_Drawing_PenSetWidth(pen, static_cast<float>(kLineThickness * density_));
        OH_Drawing_PenSetCap(pen, LINE_ROUND_CAP);
        OH_Drawing_PenSetJoin(pen, LINE_ROUND_JOIN);
        OH_Drawing_PenSetShaderEffect(pen, lineShader);
        OH_Drawing_CanvasAttachPen(canvas, pen);
        OH_Drawing_CanvasDrawPath(canvas, line);
        OH_Drawing_CanvasDetachPen(canvas);
        OH_Drawing_PenDestroy(pen);
        OH_Drawing_ShaderEffectDestroy(lineShader);

        OH_Drawing_PathDestroy(area);
        OH_Drawing_PathDestroy(line);
    };

    if (spec.bars) {
        /*
         * `BreezyBarChart`'s ColumnCartesianLayer (kt:162-173): one rounded column per point, in
         * one flat colour, thickness 500 dp — far more than a slot is wide, so Vico clamps it to
         * the slot and the columns end up touching. The slot here is one hour of the fixed 24-hour
         * x range.
         */
        const double slot =
            (plotRight - plotLeft) * static_cast<double>(kMillisPerHour) / (spec.maxX - spec.minX);
        for (size_t i = 0; i < spec.xs.size(); ++i) {
            const double centre = xAt(spec.xs[i]);
            const double barTop = yAt(spec.ys[i]);
            if (plotBottom - barTop <= 0.0) {
                continue;
            }
            // CorneredShape.rounded(allPercent = 15) — 15 % of the shorter side, all four corners.
            const double radius = std::min(slot, plotBottom - barTop) * 0.15;
            FillRoundRect(canvas, centre - slot / 2.0, barTop, centre + slot / 2.0, plotBottom,
                          radius, spec.barColor);
        }
    } else {
        /*
         * The second series FIRST, so the primary one draws over it.
         *
         * Two shapes: Feels like puts the actual temperature behind its own line as a flat grey
         * stroke with no area, while the ephemeris chart's second series is a full one — its own
         * gradient, its own area (DetailsSunMoon.kt:182-193).
         */
        if (spec.xs2.size() > 1) {
            if (!spec.stops2.empty()) {
                drawSeries(spec.xs2, spec.ys2, spec.stops2, spec.areaStops2);
            } else {
                OH_Drawing_Path* behind = buildPath(spec.xs2, spec.ys2);
                OH_Drawing_Pen* behindPen = OH_Drawing_PenCreate();
                OH_Drawing_PenSetAntiAlias(behindPen, true);
                OH_Drawing_PenSetColor(behindPen, spec.secondaryColor);
                OH_Drawing_PenSetWidth(behindPen, static_cast<float>(kLineThickness * density_));
                OH_Drawing_PenSetCap(behindPen, LINE_ROUND_CAP);
                OH_Drawing_PenSetJoin(behindPen, LINE_ROUND_JOIN);
                OH_Drawing_CanvasAttachPen(canvas, behindPen);
                OH_Drawing_CanvasDrawPath(canvas, behind);
                OH_Drawing_CanvasDetachPen(canvas);
                OH_Drawing_PenDestroy(behindPen);
                OH_Drawing_PathDestroy(behind);
            }
        }
        drawSeries(spec.xs, spec.ys, spec.stops, spec.areaStops);
    }
    OH_Drawing_CanvasRestore(canvas);

    /* the trendHorizontalLine, gated on screen 3c's setting exactly as kt:223-235 gates it */
    if (keyLinesEnabled_) {
        for (const ChartSpec::KeyLine& keyLine : spec.keyLines) {
            if (keyLine.y < spec.minY || keyLine.y > spec.maxY) {
                continue;
            }
            const double y = yAt(keyLine.y);
            FillRect(canvas, plotLeft, y - 0.5, plotRight, y + 0.5, lineColor);
            // Position.Vertical.Bottom - the label hangs under its line, at the start.
            DrawText(canvas, keyLine.label, plotLeft + kAxisLabelPadding * density_,
                     y + kAxisLabelPadding * density_, plotRight - plotLeft, label);
        }
    }

    /* the marker: a guideline at the touched hour, with a dot on the line */
    plotLeft_ = plotLeft;
    plotRight_ = plotRight;
    const int64_t activeHour = variant == 1 ? activeHourMs2_ : activeHourMs_;
    if (activeHour >= 0) {
        for (size_t i = 0; i < spec.xs.size(); ++i) {
            if (static_cast<int64_t>(spec.xs[i]) != activeHour) {
                continue;
            }
            const double x = xAt(spec.xs[i]);
            // rememberDefaultCartesianMarker's `guideline` is a plain rule in the label colour.
            FillRect(canvas, x - 0.5, plotTop, x + 0.5, plotBottom, labelColor);
            FillCircle(canvas, x, yAt(spec.ys[i]), kLineThickness * density_, labelColor);
            break;
        }
    }

    /*
     * The top axis (AxisItemPlacer.kt:77-108). Pressure fills it with trend arrows; the conditions
     * chart fills it with an 18 dp WEATHER ICON per hour (DetailsConditions.kt:857-866). Both use
     * the same placer, so the thinning below serves either.
     */
    if (hasTopAxis) {
        const bool icons = !spec.topIconAssets.empty();
        const std::vector<double>& xs = icons ? spec.topIconXs : spec.arrowXs;
        const double hoursDisplayed = (spec.maxX - spec.minX) / kMillisPerHour;
        const double labelWidth = (kArrowSize + kAxisLabelPadding * 2.0) * density_;
        const int maxLabels = static_cast<int>((plotRight - plotLeft) / labelWidth);
        if (maxLabels > 0) {
            const double skip = std::max(1.0, std::ceil(hoursDisplayed / maxLabels));
            const double edge = std::max(1.0, skip - 1.0) * kMillisPerHour;
            const double firstAllowed = spec.minX + edge;
            const double lastAllowed = spec.maxX - edge;
            double previous = 0.0;
            for (size_t i = 0; i < xs.size(); ++i) {
                const double at = xs[i];
                if (at < firstAllowed || at > lastAllowed ||
                    at < previous + skip * kMillisPerHour) {
                    continue;
                }
                previous = at;
                const double centre = top + kTopAxisHeight * density_ / 2.0;
                if (icons) {
                    const double size = kArrowSize * density_;
                    DrawAsset(canvas, spec.topIconAssets[i], xAt(at) - size / 2.0,
                              centre - size / 2.0, size, 0, false);
                } else {
                    DrawTrendArrow(canvas, xAt(at), centre, kArrowSize * density_,
                                   spec.arrowKinds[i], labelColor);
                }
            }
        }
    }
    return height;
}

/**
 * `PrecipitationDetails` and its two siblings (kt:418-475): a Daytime column and a Nighttime column
 * of `DetailsItem`s built from the same list of labels, each row a bold headline over its value,
 * and the whole block absent when the list is empty.
 *
 * Returns the height it occupied, INCLUDING the normal_margin the Kotlin pads the block with.
 */
double DetailsScreen::DrawPairs(OH_Drawing_Canvas* canvas, const std::vector<Pair>& pairs,
                                double left, double top, double width) const {
    if (pairs.empty()) {
        return 0.0;
    }
    const double margin = dimens::kNormalMargin * density_;
    const double small = dimens::kSmallMargin * density_;
    const uint32_t onSurface = kOnSurface.Resolve(dark_);

    TextStyleSpec labelStyle;
    labelStyle.sizeVp = kLabelTextSize;
    labelStyle.color = onSurface;
    labelStyle.singleLine = true;
    TextStyleSpec headlineStyle;
    headlineStyle.sizeVp = kBodyTextSize;
    headlineStyle.color = onSurface;
    headlineStyle.bold = true;
    headlineStyle.singleLine = true;
    /*
     * `DailyPrecipitationDetail` (kt:477-503) is a `DetailsItem` with no icon, so a row carries
     * the SAME metric as the degree-day and sunshine rows: a bodyLarge headline on its 24 sp line
     * (78 px) over a BARE 16 sp supporting line at the font's own leading (61), 139 in total.
     * Measured on the reference: label `[52,1623][215,1675]`, row `[52,1727][237,1866]`, against
     * this port's 46 + 61 + 53.
     */
    TextStyleSpec valueStyle;
    valueStyle.sizeVp = kBodyTextSize;
    valueStyle.color = onSurface;
    valueStyle.singleLine = true;

    const double columnWidth = (width - small) / 2.0;
    double bottom = top + margin;
    for (int c = 0; c < 2; ++c) {
        const double columnLeft = left + static_cast<double>(c) * (columnWidth + small);
        double y = top + margin;
        // `DaytimeLabel` is a TextFixedHeight at labelMedium — 16 dp, not the font's own box.
        y += DrawTextFixed(canvas, c == 0 ? kDaytime : kNighttime, columnLeft, y, columnWidth,
                           labelStyle, dimens::kLabelMediumLineHeight * density_);
        for (const Pair& pair : pairs) {
            y += margin;
            y += DrawTextFixed(canvas, pair.label, columnLeft, y, columnWidth, headlineStyle,
                               dimens::kBodyLargeLineHeight * density_);
            y += DrawText(canvas, c == 0 ? pair.dayValue : pair.nightValue, columnLeft, y,
                          columnWidth, valueStyle);
        }
        bottom = std::max(bottom, y);
    }
    return bottom - top;
}

/**
 * A Material 3 `PlainTooltip` — an inverseSurface bubble with inverseOnSurface bodySmall text,
 * sitting above the row it explains (TooltipAnchorPosition.Above, DetailsConditions.kt:291-299).
 *
 * ArkUIApp gets the same thing from `.bindPopup`, which is the framework's own tooltip; here it is
 * four drawing calls, which is the shape of every difference between these two ports.
 */
void DetailsScreen::DrawTooltip(OH_Drawing_Canvas* canvas, const std::string& text, double left,
                                double anchorTop, double width) const {
    const double padH = 8.0 * density_;
    const double padV = 4.0 * density_;
    const double radius = 4.0 * density_;
    const double gap = 4.0 * density_;
    TextStyleSpec style;
    style.sizeVp = dimens::kSubtitleTextSize;
    style.color = kOnSurfaceInverse.Resolve(dark_);
    const double textWidth = width - padH * 2.0;
    const double textHeight = MeasureTextHeight(text, textWidth, style);
    const double bubbleBottom = anchorTop - gap;
    const double bubbleTop = bubbleBottom - textHeight - padV * 2.0;
    FillRoundRect(canvas, left, bubbleTop, left + width, bubbleBottom, radius,
                  dark_ ? kSurfaceInverseDark : kSurfaceInverseLight);
    DrawText(canvas, text, left + padH, bubbleTop + padV, textWidth, style);
}

const AirQuality* DetailsScreen::SelectedAirQuality() const {
    if (dayIndex_ >= 0 && dayIndex_ < static_cast<int>(weather_.dailyForecast.size())) {
        const AirQuality& daily = weather_.dailyForecast[static_cast<size_t>(dayIndex_)].airQuality;
        if (OverallIndexOf(daily).has_value()) {
            return &daily;
        }
    }
    if (weather_.hasCurrent && weather_.current.airQuality.Any()) {
        return &weather_.current.airQuality;
    }
    return nullptr;
}

/**
 * The picker's entries: every pollutant the reading carries, in DECLARATION order.
 *
 * `supportedPollutants` (DetailsScreen.kt:426-441) filters `PollutantIndex.entries` — PM2.5, PM10,
 * O3, NO2, SO2, CO — not `validPollutants`, which puts NO2 and O3 first. The two orders differ and
 * this used to take the wrong one: the reference's switcher reads AQI / PM2.5 / PM10 / O3 and the
 * ports read AQI / NO2 / O3. ArkUIApp's `pollutantOptions` makes the same correction.
 */
std::vector<const PollutantDef*> DetailsScreen::PollutantChoices() const {
    const AirQuality* air = SelectedAirQuality();
    std::vector<const PollutantDef*> out;
    if (air == nullptr) {
        return out;
    }
    for (const PollutantDef& def : Pollutants()) {
        if (ConcentrationOf(*air, def.id).has_value()) {
            out.push_back(&def);
        }
    }
    return out;
}

std::string DetailsScreen::PollutantLabel(int index) const {
    if (index <= 0) {
        return "AQI";  // strings.xml `air_quality_index_short`
    }
    const std::vector<const PollutantDef*> choices = PollutantChoices();
    return index <= static_cast<int>(choices.size())
               ? choices[static_cast<size_t>(index) - 1]->shortName
               : std::string{};
}

std::string DetailsScreen::PollutantIdAt(int index) const {
    if (index <= 0) {
        return "";
    }
    const std::vector<const PollutantDef*> choices = PollutantChoices();
    return index <= static_cast<int>(choices.size())
               ? choices[static_cast<size_t>(index) - 1]->id
               : std::string{};
}

void DetailsScreen::SelectPollutant(const std::string& id) {
    pollutantId_ = id;
    // The chart's axis, the header and the sections all change with it, so the marker has to go.
    activeHourMs_ = -1;
}

int DetailsScreen::SelectedPollutantIndex() const {
    const std::vector<const PollutantDef*> choices = PollutantChoices();
    for (size_t i = 0; i < choices.size(); ++i) {
        if (pollutantId_ == choices[i]->id) {
            return static_cast<int>(i) + 1;
        }
    }
    return 0;
}

DetailsScreen::SummaryColumn DetailsScreen::AirQualityColumn(const std::string& label,
                                                             const AirQuality* air) const {
    SummaryColumn out;
    out.label = label;
    if (air == nullptr) {
        return out;
    }
    const PollutantDef* def = PollutantById(pollutantId_);
    if (def == nullptr) {
        const Maybe index = OverallIndexOf(*air);
        if (!index.has_value()) {
            return out;
        }
        out.value = FormatIndex(*index);
        out.valueLevel = AqiLevelName(*index);
        out.dotColor = AqiLevelColor(*index);
        return out;
    }
    const Maybe concentration = ConcentrationOf(*air, def->id);
    const Maybe index = PollutantIndexOf(*def, concentration);
    if (!concentration.has_value() || !index.has_value()) {
        return out;
    }
    // The unit rides a font size down, as `formatUnitsDifferentFontSize` renders it (kt:409-414).
    out.value = std::to_string(static_cast<long long>(std::llround(*concentration)));
    out.unit = " " + ConcentrationUnitLabel(*def);
    out.valueLevel = AqiLevelName(*index);
    out.dotColor = AqiLevelColor(*index);
    return out;
}

/**
 * `detailedPollutants` (kt:168-215) — PM2.5, PM10, O3 and NO2 whenever they are present, then SO2
 * and CO only when they read above zero.
 */
static std::vector<const PollutantDef*> DetailedPollutants(const AirQuality& air) {
    std::vector<const PollutantDef*> out;
    for (const char* id : {"pm25", "pm10", "o3", "no2"}) {
        const PollutantDef* def = PollutantById(id);
        if (def != nullptr && ConcentrationOf(air, id).has_value()) {
            out.push_back(def);
        }
    }
    for (const char* id : {"so2", "co"}) {
        const PollutantDef* def = PollutantById(id);
        const Maybe value = def == nullptr ? Maybe{} : ConcentrationOf(air, id);
        if (def != nullptr && value.has_value() && *value > 0.0) {
            out.push_back(def);
        }
    }
    return out;
}

/**
 * The Air quality screen's blocks, which depend on the picked pollutant.
 *
 * With a pollutant picked the Kotlin shows ONLY that pollutant's About card (kt:255-268); with
 * none it shows Health information, Primary pollutant and Pollutant details (kt:269-308). Both
 * then get the shared About-the-index card and the scale table, whose heading and rows also change
 * with the selection (kt:309-337).
 */
std::vector<DetailsScreen::Section> DetailsScreen::AirQualitySections(const AirQuality* air) const {
    std::vector<Section> out;
    const PollutantDef* def = PollutantById(pollutantId_);

    if (def != nullptr) {
        out.push_back({def->aboutHeading, "", {def->sources}, {}});
    } else if (air != nullptr) {
        const Maybe index = OverallIndexOf(*air);
        if (index.has_value()) {
            // air_quality_health_information - the level's own description paragraph.
            out.push_back({"Health information", "", {AqiLevelDescription(*index)}, {}});
        }
        /*
         * The primary pollutant is the one with the HIGHEST sub-index (kt:216-218). Its full name
         * is the heading's subtitle and its sources are the card.
         */
        const PollutantDef* worst = nullptr;
        double worstIndex = -1.0;
        for (const PollutantDef* candidate : DetailedPollutants(*air)) {
            const Maybe value = PollutantIndexOf(*candidate, ConcentrationOf(*air, candidate->id));
            if (value.has_value() && *value > worstIndex) {
                worstIndex = *value;
                worst = candidate;
            }
        }
        if (worst != nullptr) {
            Section primary;
            primary.heading = "Primary pollutant";
            primary.subtitle = worst->fullName;
            primary.paragraphs.push_back(worst->sources);
            out.push_back(primary);
        }
        /*
         * `AirQualityDetails` (kt:656-682). The bar's fraction is the sub-index over
         * `indexExcessivePollution` (250), NOT the concentration over its band, "for a more
         * realistic bar" (kt:170).
         */
        Section details;
        details.heading = "Pollutant details";
        details.pollutantGrid = true;
        for (const PollutantDef* candidate : DetailedPollutants(*air)) {
            const Maybe concentration = ConcentrationOf(*air, candidate->id);
            if (!concentration.has_value()) {
                continue;
            }
            const Maybe index = PollutantIndexOf(*candidate, concentration);
            const double fraction =
                index.has_value() ? std::min(1.0, *index / kAqiBands.back()) : 0.0;
            Row row;
            row.label = candidate->shortName;
            row.value = FormatConcentration(*candidate, *concentration);
            row.dotColor = index.has_value() ? AqiLevelColor(*index) : 0;
            row.middle = std::to_string(fraction);
            details.rows.push_back(row);
        }
        out.push_back(details);
    }

    out.push_back({"About air quality index", "", {kAboutAirQuality1, kAboutAirQuality2}, {}});

    /*
     * `AirQualityScale` (kt:725-797) — THREE columns, and the whole table changes with the
     * selection. With a pollutant the rows are its CONCENTRATION bands, each labelled with the
     * level its own index falls in, and the last column carries concentrations.
     */
    Section scale;
    scale.heading = def == nullptr ? "Air quality index scale" : def->scaleHeading;
    scale.headerLabel = "Description";
    scale.headerMiddle = "Harmless exposure";
    scale.headerValue = def == nullptr ? "AQI" : ConcentrationUnitLabel(*def);
    scale.labelWeight = 2.0f;  // kt:750-769
    scale.middleWeight = 1.5f;
    scale.valueWeight = 1.5f;
    const std::vector<int>& bands = def == nullptr ? kAqiBands : def->thresholds;
    for (size_t i = 0; i < bands.size(); ++i) {
        const Maybe aqi =
            def == nullptr ? Maybe{static_cast<double>(bands[i])} : PollutantIndexOf(*def, bands[i]);
        const std::string range =
            i + 1 < bands.size()
                ? std::to_string(bands[i]) + " \xE2\x80\x93 " + std::to_string(bands[i + 1] - 1)
                : std::to_string(bands[i]) + "+";
        const double indexValue = aqi.has_value() ? *aqi : static_cast<double>(bands[i]);
        Row row;
        row.label = AqiLevelName(indexValue);
        row.value = range;
        row.dotColor = AqiLevelColor(indexValue);
        row.middle = AqiHarmlessExposureAt(LevelOf(indexValue, kAqiBands));
        scale.rows.push_back(row);
    }
    out.push_back(scale);
    return out;
}

/* strings.xml:295-298. */
constexpr const char* kDegreeDayHeating = "Heating degree day";
constexpr const char* kDegreeDayHeatingExplanation =
    "Difference between the outside temperature and a reference comfortable temperature used to "
    "estimate heating energy requirements";
constexpr const char* kDegreeDayCooling = "Cooling degree day";
constexpr const char* kDegreeDayCoolingExplanation =
    "Difference between the outside temperature and a reference comfortable temperature used to "
    "estimate cooling energy requirements";

/**
 * The bare `DetailsItem` rows a tag draws under its chart.
 *
 * TWO tags have one. Conditions and Feels like draw the degree day, shown when `DegreeDay.isValid`
 * — heating > 0 OR cooling > 0 (DegreeDay.kt:27); inside the comfortable band both are zero and the
 * Kotlin draws nothing at all, which is what most days give. CLOUD COVER draws the SUNSHINE
 * DURATION whenever the day has one (`SunshineItem`, DetailsCloudCover.kt:144-151), which this port
 * omitted: the reference has a 139 px row at [52,1675][622,1814] between the chart and the About
 * section, and the port went straight from one to the other.
 */
std::vector<DetailsScreen::Item> DetailsScreen::Items() const {
    std::vector<Item> out;
    if (tag_ != DetailTag::Conditions && tag_ != DetailTag::FeelsLike &&
        tag_ != DetailTag::CloudCover) {
        return out;
    }
    if (dayIndex_ < 0 || dayIndex_ >= static_cast<int>(weather_.dailyForecast.size())) {
        return out;
    }
    const Daily& daily = weather_.dailyForecast[static_cast<size_t>(dayIndex_)];
    if (tag_ == DetailTag::CloudCover) {
        // `daily.sunshineDuration?.let` — no row at all when the day has none. The duration is
        // formatted by the same `formatTime(smallestUnit = MINUTES)` the Sun & moon card uses.
        if (daily.sunshineHours.has_value()) {
            Item row;
            row.headline = "Sunshine duration";  // strings.xml `sunshine_duration`
            row.supporting = FormatDurationShort(
                static_cast<int64_t>(std::llround(*daily.sunshineHours * 3600000.0)));
            row.icon = "ic_sunshine_duration.svg";
            /*
             * The Spacer ABOVE it is a normal_margin here (kt:145-147) where the degree day's is a
             * small_margin (DetailsConditions.kt:286-288). Measured on the reference: the cloud
             * cover chart ends at 1623 and the row starts at 1675.
             */
            row.spacerAbove = dimens::kNormalMargin;
            out.push_back(row);
        }
        return out;
    }
    const double heating = daily.heatingDegreeDay.value_or(0.0);
    const double cooling = daily.coolingDegreeDay.value_or(0.0);
    if (heating <= 0.0 && cooling <= 0.0) {
        return out;  // DegreeDay.isValid is false — kt:285 skips the whole block.
    }
    // kt:289 / kt:328 — heating takes precedence, and only ONE of the two rows is ever drawn.
    if (heating > 0.0) {
        out.push_back({kDegreeDayHeating, FormatTemperatureDeviation(heating), "ic_mode_heat.svg",
                       kDegreeDayHeatingExplanation});
    } else {
        out.push_back({kDegreeDayCooling, FormatTemperatureDeviation(cooling), "ic_mode_cool.svg",
                       kDegreeDayCoolingExplanation});
    }
    return out;
}

std::vector<DetailsScreen::Section> DetailsScreen::Sections() const {
    std::vector<Section> out;
    switch (tag_) {
        case DetailTag::Pressure:
            out.push_back({"About pressure", "", {kAboutPressure1, kAboutPressure2}, {}});
            break;
        case DetailTag::Humidity:
            out.push_back({"About relative humidity", "", {kAboutHumidity}, {}});
            out.push_back({"About dew point", "", {kAboutDewPoint}, {}});
            break;
        /*
         * The scale tables. One card per tag, a bold two-column header then a coloured dot, the
         * band's name and its range - DetailsUV.kt:312-380 is the shape they all share. Each tag's
         * bands are the SAME thresholds its description function above already uses, so the table
         * and the header can never disagree. ArkUIApp's DetailsData.ets builds the same three.
         */
        case DetailTag::Visibility: {
            out.push_back({"About visibility", "", {kAboutVisibility}, {}});
            Section scale;
            scale.heading = "Visibility scale";  // strings.xml:365
            scale.headerLabel = "Description";
            // The value column is headed with the UNIT's display name ("km"), not the quantity.
            scale.headerValue = DistanceUnitDisplayName();
            /*
             * `VisibilityScale` (DetailsVisibility.kt:388-415) formats the BAND, not the boundary:
             * a row ends one step BELOW the next threshold (0.1 when the unit shows a decimal,
             * 1 when it does not), and carries no unit. Measured on the reference: "0 – 0.9",
             * "1 – 3.9", "4 – 9.9" against this port's "0 – 1 km", "1 – 4 km".
             */
            const double starts[] = {0.0, 1000.0, 4000.0, 10000.0, 20000.0, 40000.0};
            const double step = DistanceDecimals() == 0 ? 1.0 : 0.1;
            for (size_t i = 0; i < 6; ++i) {
                const std::string range =
                    i + 1 < 6 ? FormatDistance(starts[i]) + " \xE2\x80\x93 " +
                                    FormatDistanceValue(DistanceValue(starts[i + 1]) - step)
                              : FormatDistance(starts[i]) + "+";
                scale.rows.push_back({VisibilityDescription(starts[i]), range, 0});
            }
            out.push_back(scale);
            break;
        }
        case DetailTag::CloudCover: {
            out.push_back({"About cloud cover", "", {kAboutCloudCover}, {}});
            Section scale;
            scale.heading = "Cloud cover scale";  // strings.xml:385
            scale.headerLabel = "Description";
            // The value column is headed with the UNIT, "%", not the quantity (kt:363-369).
            scale.headerValue = "%";
            /*
             * `CloudCoverScale` (kt:370-392) prints the boundaries themselves, unrounded and with
             * no per-cell "%". Measured: "0 – 12.5", "12.5 – 37.5" against "0% – 13%".
             */
            const double starts[] = {0.0, 12.5, 37.5, 62.5, 87.5};
            for (size_t i = 0; i < 5; ++i) {
                const double next = i + 1 < 5 ? starts[i + 1] : 100.0;
                scale.rows.push_back({CloudCoverDescription(starts[i]),
                                      FormatPlainNumber(starts[i]) + " \xE2\x80\x93 " +
                                          FormatPlainNumber(next),
                                      0});
            }
            out.push_back(scale);
            break;
        }
        case DetailTag::UvIndex: {
            out.push_back({"About UV index", "", {kAboutUv}, {}});
            Section scale;
            scale.heading = "UV index scale";  // strings.xml:153
            scale.headerLabel = "Description";
            scale.headerValue = "UV index";
            // 2 / 1 here, where cloud cover and visibility are 1.5 / 1 (DetailsUV.kt:336-342).
            scale.labelWeight = 2.0f;
            // UV.uvThresholds - the four constants with 0 in front.
            const double starts[] = {0.0, kUvIndexLow, kUvIndexMiddle, kUvIndexHigh,
                                     kUvIndexExcessive};
            for (size_t i = 0; i < 5; ++i) {
                char buffer[32];
                if (i + 1 < 5) {
                    std::snprintf(buffer, sizeof(buffer), "%.0f \xE2\x80\x93 %.0f", starts[i],
                                  starts[i + 1] - 1.0);
                } else {
                    std::snprintf(buffer, sizeof(buffer), "%.0f+", starts[i]);
                }
                scale.rows.push_back({UvLevelName(starts[i]), buffer, UvLevelColor(starts[i])});
            }
            out.push_back(scale);
            break;
        }
        case DetailTag::Wind: {
            out.push_back({"About wind speed and gusts", "", {kAboutWind}, {}});
            Section scale;
            scale.heading = "Beaufort scale";  // strings.xml:127
            /*
             * THREE columns, not two (DetailsWind.kt:504-592): the force NUMBER with its dot under
             * "Bft" (speed_bf_display_name_short), the strength name under
             * `wind_strength_scale_description`, and the band under the speed unit's display name.
             * Measured on the reference: "0 / Calm / 0 – 0.2" against this port's "Calm | 0 – 0.3 m/s".
             */
            scale.headerLabel = "Bft";
            scale.headerMiddle = "Description";
            scale.headerValue = SpeedUnitDisplayName();
            scale.labelWeight = 1.0f;
            scale.middleWeight = 1.5f;
            scale.valueWeight = 1.5f;
            /*
             * The WIND_SPEED_* table, held in cm/s so 30 is 0.3 m/s, with its thirteen strength
             * names (strings.xml:118-130) and the windStrength_bf* colours.
             */
            static const double kStarts[] = {0.0,  0.3,  1.6,  3.4,  5.5,  8.0, 10.8,
                                             13.9, 17.2, 20.8, 24.5, 28.5, 32.7};
            static const char* const kNames[] = {
                "Calm",          "Light air",     "Light breeze", "Gentle breeze",
                "Moderate breeze", "Fresh breeze", "Strong breeze", "Moderate gale",
                "Gale",          "Strong gale",   "Storm",        "Violent storm",
                "Hurricane-force"};
            static const uint32_t kColors[] = {
                0xFF6271B7, 0xFF39619F, 0xFF4A94A9, 0xFF4D8D7B, 0xFF53A553, 0xFF359F35, 0xFFA79D51,
                0xFFA1645C, 0xFFAF5088, 0xFF754A93, 0xFF44698D, 0xFF5C9098, 0xFF7D44A5};
            for (size_t i = 0; i < 13; ++i) {
                // The band ends one tenth BELOW the next force's threshold (kt:547-556), and
                // carries no unit - the unit is the column heading.
                const std::string range =
                    i + 1 < 13 ? FormatSpeed(kStarts[i]) + " \xE2\x80\x93 " +
                                     FormatSpeedValue(SpeedValue(kStarts[i + 1]) - 0.1)
                               : FormatSpeed(kStarts[i]) + "+";
                scale.rows.push_back({std::to_string(i), range, kColors[i], kNames[i]});
            }
            out.push_back(scale);
            break;
        }
        case DetailTag::AirQuality:
            // Depends on the day's reading and the picked pollutant; built by AirQualitySections.
            for (const Section& section : AirQualitySections(SelectedAirQuality())) {
                out.push_back(section);
            }
            break;
        case DetailTag::SunMoon: {
            /*
             * No summary header: the screen leads with a card of the day's times (kt:104-126),
             * then the About block.
             *
             * FOUR `ListItem`s, each with a 24 dp leading image and a single bodyLarge line
             * (kt:339-558):
             *   sun       weather_clear_day_mini    "5:24 AM↑ / 9:26 PM↓ / 16h 2m"
             *   twilight  ic_twilight               "4:30 AM↑ / 10:20 PM↓"
             *   moon      weather_clear_night_mini  "2:25 AM↑ / 9:20 PM↓"
             *   phase     a MoonPhaseView disc      "New moon"
             * This port drew five bare text lines: no icons, no twilight, no duration, and each
             * hugging its text at 104 px where the reference's ListItem is a fixed 182.
             *
             * The astro is for the DAY THE PAGER IS ON, not for "now" — `today.sun` and friends
             * are per-day fields. Computed here for the one day being shown rather than stored for
             * all sixteen on every refresh; ArkUIApp's `sunMoonSections` does the same.
             */
            Section times;
            times.listItemRows = true;
            if (dayIndex_ >= 0 && dayIndex_ < static_cast<int>(weather_.dailyForecast.size())) {
                const int64_t date = weather_.dailyForecast[static_cast<size_t>(dayIndex_)].date;
                const AstroPair sun = CalculatedSun(date, latitude_, longitude_, utcOffsetSeconds_);
                const AstroPair twilight =
                    CalculatedCivilTwilight(date, latitude_, longitude_, utcOffsetSeconds_);
                const AstroPair moon =
                    CalculatedMoon(date, latitude_, longitude_, utcOffsetSeconds_);
                const auto line = [this](const AstroPair& pair) {
                    // "5:24 AM↑ / 9:26 PM↓" (kt:362-369); "-" is strings.xml null_data_text.
                    const std::string rise =
                        pair.hasRise ? ClockTime(pair.rise, utcOffsetSeconds_, false) : "-";
                    const std::string set =
                        pair.hasSet ? ClockTime(pair.set, utcOffsetSeconds_, false) : "-";
                    return rise + "\xE2\x86\x91 / " + set + "\xE2\x86\x93";
                };
                if (sun.hasRise || sun.hasSet) {
                    // kt:360 — the duration is appended only when there is one, as " / 16h 2m".
                    std::string text = line(sun);
                    if (sun.hasRise && sun.hasSet) {
                        text += " / " + FormatDurationShort(sun.set - sun.rise);
                    }
                    Row row;
                    row.label = text;
                    row.icon = "weather_clear_day_mini_light.png";
                    times.rows.push_back(row);
                }
                if (twilight.hasRise || twilight.hasSet) {
                    Row row;
                    row.label = line(twilight);
                    row.icon = "ic_twilight.svg";
                    times.rows.push_back(row);
                }
                if (moon.hasRise || moon.hasSet) {
                    Row row;
                    row.label = line(moon);
                    row.icon = "weather_clear_night_mini_light.png";
                    times.rows.push_back(row);
                }
                const int angle = MoonPhaseAngle(date, utcOffsetSeconds_);
                Row phase;
                phase.label = MoonPhaseName(angle);
                phase.moonPhaseAngle = static_cast<double>(angle);
                times.rows.push_back(phase);
            }
            if (times.rows.empty()) {
                times.listItemRows = false;
                times.paragraphs.push_back(kNoDailyData);
            }
            out.push_back(times);
            out.push_back({"About rise and set times", "", {kAboutEphemeris}, {}});
            break;
        }
        case DetailTag::Pollen: {
            // DetailsPollen.kt:55-77 - the worst allergen names the first heading, then the grid.
            if (!weather_.hasCurrent || weather_.current.pollen.empty()) {
                Section empty;
                empty.paragraphs.push_back(kNoDailyData);
                out.push_back(empty);
                break;
            }
            /*
             * getPollenWithMaxIndex (Pollen.kt:49-62): the allergens with an index ABOVE ZERO,
             * worst first, and nothing at all when every one of them reads zero — in which case the
             * Kotlin's `?.let` skips the whole "Primary pollen" header.
             */
            const PollenEntry* worst = nullptr;
            for (const PollenEntry& entry : weather_.current.pollen) {
                if (entry.level > 0 && (worst == nullptr || entry.level > worst->level)) {
                    worst = &entry;
                }
            }
            if (worst != nullptr) {
                Section primary;
                primary.heading = "Primary pollen";  // strings.xml:243
                primary.subtitle = worst->name;
                out.push_back(primary);
            }

            /*
             * PollenGrid (AllergenComposables.kt:63-94): `validPollens` — everything the source
             * REPORTS, not only what is in season — sorted by localised name, two per row, and each
             * subtitle is the concentration, an en dash, then the level.
             */
            std::vector<PollenEntry> sorted = weather_.current.pollen;
            std::sort(sorted.begin(), sorted.end(),
                      [](const PollenEntry& a, const PollenEntry& b) { return a.name < b.name; });
            Section details;
            details.heading = "Pollen details";  // strings.xml:244
            details.pollenGrid = true;
            for (const PollenEntry& entry : sorted) {
                details.rows.push_back(
                    {entry.name,
                     FormatPollenConcentration(entry.concentration) + " – " + entry.levelName,
                     PollenLevelColor(entry.level)});
            }
            out.push_back(details);
            break;
        }
        default:
            // Conditions, Feels like, Precipitation and Pollen have no About block in the Kotlin.
            break;
    }
    return out;
}

double DetailsScreen::TabRowHeight() const {
    // Three text lines plus 8 vp above and below, then the indicator.
    TextStyleSpec line;
    line.sizeVp = kTabLabelSize;
    line.singleLine = true;
    TextStyleSpec day;
    day.sizeVp = kTabDaySize;
    day.singleLine = true;
    const double lineHeight = MeasureTextHeight("Today", 1.0e6, line);
    const double dayHeight = MeasureTextHeight("09", 1.0e6, day);
    return dimens::kSmallMargin * 2.0 * density_ + lineHeight * 2.0 + dayHeight +
           kIndicatorHeight * density_;
}

double DetailsScreen::ContentTop() const {
    return topInset_ + kBarHeight * density_ + TabRowHeight() + 1.0;
}

void DetailsScreen::Draw(OH_Drawing_Canvas* canvas) {
    if (width_ <= 0.0 || height_ <= 0.0) {
        return;
    }
    const double margin = dimens::kNormalMargin * density_;
    const double small = dimens::kSmallMargin * density_;
    const double iconSize = dimens::kMaterialIconSize * density_;
    const uint32_t onSurface = kOnSurface.Resolve(dark_);
    const uint32_t onSurfaceVariant = kOnSurfaceVariant.Resolve(dark_);
    const uint32_t primary = kPrimary.Resolve(dark_);

    FillRect(canvas, 0.0, 0.0, width_, height_, kSurface.Resolve(dark_));

    /* ---- BWCenterAlignedTopAppBar ---- */
    const double barTop = topInset_;
    DrawAsset(canvas, "ic_back.svg", margin, barTop + (kBarHeight * density_ - iconSize) / 2.0,
              iconSize, onSurface, true);
    backBand_ = {margin - small, barTop, margin + iconSize + small, barTop + kBarHeight * density_,
                 0};

    TextStyleSpec title;
    title.sizeVp = kTitleTextSize;
    title.color = onSurface;
    title.singleLine = true;
    title.alignment = TextAlignment::Center;
    const std::string titleText = DetailTagName(tag_);
    const double titleHeight = MeasureTextHeight(titleText, width_, title);
    DrawText(canvas, titleText, 0.0, barTop + (kBarHeight * density_ - titleHeight) / 2.0, width_,
             title);

    /* ---- PrimaryScrollableTabRow ---- */
    const double tabTop = barTop + kBarHeight * density_;
    const double tabWidth = kTabWidth * density_;
    dayBands_.clear();
    OH_Drawing_CanvasSave(canvas);
    OH_Drawing_Rect* tabClip = OH_Drawing_RectCreate(
        0.0f, static_cast<float>(tabTop), static_cast<float>(width_),
        static_cast<float>(tabTop + TabRowHeight()));
    OH_Drawing_CanvasClipRect(canvas, tabClip, INTERSECT, true);
    OH_Drawing_RectDestroy(tabClip);

    TextStyleSpec tabLine;
    tabLine.sizeVp = kTabLabelSize;
    tabLine.singleLine = true;
    tabLine.alignment = TextAlignment::Center;
    TextStyleSpec tabDay;
    tabDay.sizeVp = kTabDaySize;
    tabDay.singleLine = true;
    tabDay.alignment = TextAlignment::Center;
    const double lineHeight = MeasureTextHeight("Today", 1.0e6, tabLine);
    const double dayHeight = MeasureTextHeight("09", 1.0e6, tabDay);

    for (size_t i = 0; i < weather_.dailyForecast.size(); ++i) {
        const double left =
            kTabEdgePadding * density_ + static_cast<double>(i) * tabWidth - tabScroll_.Position();
        const double right = left + tabWidth;
        if (right < 0.0 || left > width_) {
            dayBands_.push_back({left, tabTop, right, tabTop + TabRowHeight(),
                                 static_cast<int>(i)});
            continue;
        }
        const bool selected = static_cast<int>(i) == dayIndex_;
        const uint32_t color = selected ? primary : onSurfaceVariant;
        tabLine.color = color;
        tabDay.color = color;

        const int64_t date = weather_.dailyForecast[i].date;
        // kt:237-245 - "Yda"/"Today"/"Tmw" around today, the short weekday elsewhere.
        std::string label = kWeekdaysShort[LocalDayOfWeek(date, utcOffsetSeconds_)];
        if (weather_.todayIndex >= 0) {
            const int delta = static_cast<int>(i) - weather_.todayIndex;
            if (delta == -1) {
                label = "Yda";  // strings.xml:38
            } else if (delta == 0) {
                label = "Today";  // :39
            } else if (delta == 1) {
                label = "Tmw";  // :40
            }
        }

        double y = tabTop + dimens::kSmallMargin * density_;
        DrawText(canvas, label, left, y, tabWidth, tabLine);
        y += lineHeight;
        DrawText(canvas, Pad2(LocalDayOfMonth(date, utcOffsetSeconds_)), left, y, tabWidth, tabDay);
        y += dayHeight;
        DrawText(canvas, kMonths[LocalMonth(date, utcOffsetSeconds_)], left, y, tabWidth, tabLine);
        y += lineHeight + dimens::kSmallMargin * density_;

        if (selected) {
            // PrimaryTabIndicator spans the tab's CONTENT, not the whole tab.
            const double inset = kTabSidePadding * density_;
            FillRoundRect(canvas, left + inset, y, right - inset, y + kIndicatorHeight * density_,
                          kIndicatorHeight * density_ / 2.0, primary);
        }
        dayBands_.push_back({left, tabTop, right, tabTop + TabRowHeight(), static_cast<int>(i)});
    }
    OH_Drawing_CanvasRestore(canvas);
    tabRowWidth_ = kTabEdgePadding * 2.0 * density_ +
                   static_cast<double>(weather_.dailyForecast.size()) * tabWidth;

    // The row's own divider.
    FillRect(canvas, 0.0, tabTop + TabRowHeight(), width_, tabTop + TabRowHeight() + 1.0,
             kOutline.Resolve(dark_));

    /* ---- the scrolling content ---- */
    const double contentTop = ContentTop();
    OH_Drawing_CanvasSave(canvas);
    OH_Drawing_Rect* clip = OH_Drawing_RectCreate(0.0f, static_cast<float>(contentTop),
                                                  static_cast<float>(width_),
                                                  static_cast<float>(height_));
    OH_Drawing_CanvasClipRect(canvas, clip, INTERSECT, true);
    OH_Drawing_RectDestroy(clip);

    const double left = margin;
    const double textWidth = width_ - margin * 2.0;
    double y = contentTop + small - contentScroll_.Position();

    /* the header */
    /* the header: one column, or two side by side for the half-day tags */
    // Re-latched every frame, because switching tag changes whether there is an icon at all.
    hasConditionIcon_ = false;
    const Summary summary = BuildSummary();
    TextStyleSpec labelStyle;
    labelStyle.sizeVp = kLabelTextSize;
    labelStyle.color = onSurface;
    labelStyle.singleLine = true;
    TextStyleSpec valueStyle;
    valueStyle.sizeVp = kValueTextSize;
    valueStyle.color = onSurface;
    valueStyle.singleLine = true;
    TextStyleSpec unitStyle;
    unitStyle.sizeVp = kValueUnitTextSize;
    unitStyle.color = onSurface;
    unitStyle.singleLine = true;
    TextStyleSpec descStyle = labelStyle;
    descStyle.color = onSurfaceVariant;

    double headerBottom = y;
    if (!summary.columns.empty()) {
        const size_t columnCount = summary.columns.size();
        const double columnWidth =
            (textWidth - small * static_cast<double>(columnCount - 1)) /
            static_cast<double>(columnCount);
        for (size_t c = 0; c < columnCount; ++c) {
            const SummaryColumn& col = summary.columns[c];
            const double columnLeft = left + static_cast<double>(c) * (columnWidth + small);
            double contentLeft = columnLeft;
            double columnY = y;
            if (col.dotColor != 0) {
                // An ic_circle tinted with getUVColor / getColor, leading the column.
                DrawAsset(canvas, "ic_circle.svg", columnLeft, columnY + small, iconSize,
                          col.dotColor, true);
                contentLeft = columnLeft + iconSize + small;
            }
            const double innerWidth = columnWidth - (contentLeft - columnLeft);
            /*
             * Every line of a `<Tag>Item` is a `TextFixedHeight`, so its height comes from the
             * TYPE SCALE and not from what the painter measures: labelMedium's 16 dp for the
             * label, displaySmall's 44 for the value, and labelMedium's 16 or labelLarge's 20 for
             * the third line. Measured on the reference the column is 52 + 143 + 65 px and this
             * port drew 46 + 137 + 46.
             */
            const double labelLine = dimens::kLabelMediumLineHeight * density_;
            const double valueLine = dimens::kDisplaySmallLineHeight * density_;
            columnY += DrawTextFixed(canvas, col.label, contentLeft, columnY, innerWidth,
                                     labelStyle, labelLine);
            const double valueHeight = MeasureTextHeight(col.value, innerWidth, valueStyle);
            DrawTextFixed(canvas, col.value, contentLeft, columnY, innerWidth, valueStyle,
                          valueLine);
            if (!col.unit.empty()) {
                // UnitUtils.formatUnitsDifferentFontSize - the unit rides at headlineSmall on the
                // same baseline, so it is offset by the difference of the two line heights.
                const double unitHeight = MeasureTextHeight(col.unit, innerWidth, unitStyle);
                DrawText(canvas, col.unit, contentLeft + MeasureTextWidth(col.value, valueStyle),
                         columnY + (valueLine - valueHeight) / 2.0 +
                             (valueHeight - unitHeight) * 0.72,
                         innerWidth, unitStyle);
                if (!col.valueSuffix.empty()) {
                    // Wind's arrow: appended after the unit, back at displaySmall (kt:283-286).
                    DrawTextFixed(canvas, col.valueSuffix,
                                  contentLeft + MeasureTextWidth(col.value, valueStyle) +
                                      MeasureTextWidth(col.unit, unitStyle),
                                  columnY, innerWidth, valueStyle, valueLine);
                }
            }
            if (!col.valueLevel.empty()) {
                /*
                 * Air quality's and UV's LEVEL NAME, inside the same displaySmall line at
                 * headlineSmall in onSurfaceVariant (DetailsAirQuality.kt:421-430). The
                 * reference's node reads "27 Fair" and is 143 px; both ports had "Fair" on a
                 * third line, making those two headers 52 px too tall.
                 */
                TextStyleSpec levelStyle = unitStyle;
                levelStyle.color = onSurfaceVariant;
                const double levelHeight = MeasureTextHeight(col.valueLevel, innerWidth,
                                                             levelStyle);
                DrawText(canvas, " " + col.valueLevel,
                         contentLeft + MeasureTextWidth(col.value, valueStyle) +
                             MeasureTextWidth(col.unit, unitStyle),
                         columnY + (valueLine - valueHeight) / 2.0 +
                             (valueHeight - levelHeight) * 0.72,
                         innerWidth, levelStyle);
            }
            columnY += valueLine;
            if (!col.iconAsset.empty()) {
                /*
                 * `WeatherCondition` (DetailsConditions.kt:718-766, placed at kt:630-633) — a
                 * `small_margin` below the temperature, then a Row of the half-day's
                 * `small_weather_icon_size` icon, a `small_margin` gap and the condition text in
                 * BOLD onSurface. Every other tag's description is the plain onSurfaceVariant
                 * line below.
                 */
                const double conditionIcon = kConditionIconSize * density_;
                TextStyleSpec conditionStyle;
                conditionStyle.sizeVp = dimens::kTitleTextSize;
                conditionStyle.color = onSurface;
                conditionStyle.bold = true;
                /*
                 * kt:625-633 — the condition sits BESIDE the temperature in landscape and BELOW it
                 * in portrait.
                 *
                 * The Kotlin's Row carries no weights, so the Column keeps its intrinsic width and
                 * the condition takes what is left after a `large_margin`. The intrinsic width here
                 * is the wider of the label and the value line, which is what those two Texts
                 * measure.
                 */
                const bool landscape = width_ > height_;
                double conditionLeft = contentLeft;
                double conditionWidth = innerWidth - conditionIcon - small;
                if (landscape) {
                    const double blockWidth = std::max(
                        MeasureTextWidth(col.label, labelStyle),
                        MeasureTextWidth(col.value, valueStyle) +
                            MeasureTextWidth(col.unit, unitStyle));
                    const double gap = dimens::kLargeMargin * density_;
                    conditionLeft = contentLeft + blockWidth + gap;
                    conditionWidth =
                        contentLeft + innerWidth - conditionLeft - conditionIcon - small;
                    // The condition is a sibling of the value line, so it rises to sit beside it
                    // rather than under it; `columnY` has already advanced past the value.
                    columnY -= valueLine;
                } else {
                    // kt:631 — the `small_margin` above exists only in the stacked arrangement.
                    columnY += small;
                }
                const double conditionHeight =
                    MeasureTextHeight(col.description, conditionWidth, conditionStyle);
                const double rowHeight = std::max(conditionIcon, conditionHeight);
                /*
                 * kt:732-756 — ANIMATED when the element-animations setting is on, static when it
                 * is off. The port used to draw the static asset unconditionally, so this screen
                 * never animated however the setting was set.
                 *
                 * `conditionIconElapsed_` is wall time since the screen opened; the pose is a pure
                 * function of it, the same way the icon-preview dialog's is.
                 */
                hasConditionIcon_ = hasConditionIcon_ || col.iconKind != WeatherKind::None;
                if (elementAnimations_ && col.iconKind != WeatherKind::None) {
                    DrawAnimatedIcon(canvas, col.iconKind, col.iconDaylight, conditionLeft,
                                     columnY + (rowHeight - conditionIcon) / 2.0, conditionIcon,
                                     conditionIconElapsed_ * 1000.0);
                } else {
                    DrawAsset(canvas, col.iconAsset, conditionLeft,
                              columnY + (rowHeight - conditionIcon) / 2.0, conditionIcon, 0,
                              false);
                }
                DrawText(canvas, col.description, conditionLeft + conditionIcon + small,
                         columnY + (rowHeight - conditionHeight) / 2.0, conditionWidth,
                         conditionStyle);
                // In landscape the condition shares the value's band, so the column ends where the
                // taller of the two does rather than after both.
                columnY += landscape ? std::max(rowHeight, valueLine) : rowHeight;
            } else if (col.descriptionLine > 0.0f) {
                /*
                 * RESERVED even when empty. `WindItem`'s else-branch draws an empty
                 * `TextFixedHeight` on purpose (kt:318-324) so a half-day with no gusts still
                 * lines up with one that has them. 16 dp is labelMedium, 20 labelLarge.
                 */
                TextStyleSpec lineStyle = descStyle;
                lineStyle.sizeVp = col.descriptionLine >= dimens::kLabelLargeLineHeight
                                       ? dimens::kContentTextSize
                                       : kLabelTextSize;
                columnY += DrawTextFixed(canvas, col.description, contentLeft, columnY, innerWidth,
                                         lineStyle, col.descriptionLine * density_);
            }
            headerBottom = std::max(headerBottom, columnY);
        }
    }
    y = headerBottom;


    y += margin;

    /* the chart, or the "not enough data" row the Kotlin shows in its place */
    {
        BREEZY_TRACE("breezy:DetailsChart");
        const ChartSpec chart = BuildChart();
        /*
         * The tags whose chart this port draws. For any other, the "not enough hourly data" row is
         * suppressed: the Kotlin draws a chart there, so showing its unavailable-chart string would
         * blame the DATA for what is a gap in the port. Pollen and Sun & moon are in neither list -
         * the Kotlin gives them a pollen grid and an EphemerisChart, not a line chart.
         * ArkUIApp's Details.ets carries the same list.
         */
        const bool chartPorted = tag_ == DetailTag::Pressure || tag_ == DetailTag::Humidity ||
                                 tag_ == DetailTag::CloudCover ||
                                 tag_ == DetailTag::Visibility || tag_ == DetailTag::UvIndex ||
                                 tag_ == DetailTag::Conditions || tag_ == DetailTag::FeelsLike ||
                                 tag_ == DetailTag::Wind || tag_ == DetailTag::AirQuality ||
                                 tag_ == DetailTag::Precipitation;
        /*
         * Sun & moon is in NEITHER list: its chart is the EphemerisChart, which the Kotlin gates on
         * the day having a valid sun or moon rather than on an hourly count, and which shows
         * nothing at all when it does not (DetailsSunMoon.kt:96-100).
         */
        if (chart.valid) {
            chartTop_ = y;
            y += DrawChart(canvas, chart, left, y, textWidth);
            chartBottom_ = y;
        } else if (!chartPorted) {
            // Nothing at all.
        } else {
            // UnavailableChart (DetailsCommon.kt:137-169): a centred icon and a bodySmall line
            // across half the width.
            TextStyleSpec note;
            note.sizeVp = dimens::kSubtitleTextSize;
            note.color = onSurface;
            const std::string text = weather_.hourlyForecast.empty()
                                         ? "No hourly data available."
                                         : "Not enough hourly data available to display a chart "
                                           "for this day.";
            const double noteWidth = textWidth / 2.0 - iconSize - small;
            DrawAsset(canvas, "ic_info.svg", left, y, iconSize, onSurface, true);
            y += std::max(iconSize,
                          DrawText(canvas, text, left + iconSize + small, y, noteWidth, note));
            y += small;
        }
    }
    /*
     * TemperatureSwitcher (DetailsConditions.kt:451-490), the one interactive part of a header.
     *
     * It is how the two conditions tags relate: onRealTempSwitch calls setSelectedChart with
     * TAG_CONDITIONS or TAG_FEELS_LIKE (kt:409), which is exactly what the hit test does.
     */
    switcherBands_.clear();
    if (tag_ == DetailTag::Conditions || tag_ == DetailTag::FeelsLike) {
        y += small;
        const double buttonHeight = 40.0 * density_;
        const double half = (textWidth - 2.0 * density_) / 2.0;
        const DetailTag tags[] = {DetailTag::Conditions, DetailTag::FeelsLike};
        const char* const names[] = {"Actual", "Feels like"};  // strings.xml:52 / :54
        for (int b = 0; b < 2; ++b) {
            const double buttonLeft = left + static_cast<double>(b) * (half + 2.0 * density_);
            const bool on = tag_ == tags[b];
            FillRoundRect(canvas, buttonLeft, y, buttonLeft + half, y + buttonHeight,
                          buttonHeight / 2.0,
                          on ? kPrimaryContainer.Resolve(dark_) : kSurfaceVariant.Resolve(dark_));
            TextStyleSpec buttonStyle;
            buttonStyle.sizeVp = kBodyTextSize;
            buttonStyle.singleLine = true;
            buttonStyle.alignment = TextAlignment::Center;
            buttonStyle.color =
                on ? kOnPrimaryContainer.Resolve(dark_) : kOnSurfaceVariant.Resolve(dark_);
            const double textHeight = MeasureTextHeight(names[b], half, buttonStyle);
            /*
             * The checked button carries a leading tick (kt:462-468). ArkUIApp draws one and this
             * did not, which is exactly the kind of one-port-only detail the comparison exists to
             * catch. The label shifts right by half the tick so the pair stays centred.
             */
            const double tick = on ? 18.0 * density_ : 0.0;
            const double gap = on ? small : 0.0;
            DrawText(canvas, names[b], buttonLeft + (tick + gap) / 2.0,
                     y + (buttonHeight - textHeight) / 2.0, half, buttonStyle);
            if (on) {
                const double labelWidth = MeasureTextWidth(names[b], buttonStyle);
                DrawAsset(canvas, "ic_check.svg",
                          buttonLeft + (half - labelWidth - tick - gap) / 2.0,
                          y + (buttonHeight - tick) / 2.0, tick, buttonStyle.color, true);
            }
            switcherBands_.push_back(
                {buttonLeft, y, buttonLeft + half, y + buttonHeight, static_cast<int>(tags[b])});
        }
        y += buttonHeight;
    }

    /*
     * AirQualitySwitcher (DetailsAirQuality.kt:542-654) — a ButtonGroup of AQI plus every
     * pollutant the reading carries, drawn under the chart because the chart's own axis is what
     * the selection changes. The tail moves into a ⋮ menu, the same overflow rule the trend tabs
     * use (ArkUIApp's TrendTabGroup.ets:15-16, three inline).
     */
    pollutantBands_.clear();
    pollutantOverflowBand_ = Band{};
    if (tag_ == DetailTag::AirQuality) {
        const std::vector<const PollutantDef*> choices = PollutantChoices();
        if (!choices.empty()) {
            y += margin;
            const int total = static_cast<int>(choices.size()) + 1;
            const int inline_ = std::min(kInlinePollutantCount, total);
            const bool hasOverflow = total > kInlinePollutantCount;
            const double buttonHeight = kPollutantChipHeight * density_;
            const double iconSize = dimens::kMaterialIconSize * density_;
            const double overflow = hasOverflow ? iconSize + small : 0.0;
            const double gap = 2.0 * density_;
            const double half =
                (textWidth - overflow - gap * (inline_ - 1)) / static_cast<double>(inline_);
            for (int b = 0; b < inline_; ++b) {
                const double buttonLeft = left + static_cast<double>(b) * (half + gap);
                const bool on = SelectedPollutantIndex() == b;
                FillRoundRect(canvas, buttonLeft, y, buttonLeft + half, y + buttonHeight,
                              buttonHeight / 2.0,
                              on ? kPrimaryContainer.Resolve(dark_) : kSurfaceVariant.Resolve(dark_));
                TextStyleSpec buttonStyle;
                buttonStyle.sizeVp = kBodyTextSize;
                buttonStyle.singleLine = true;
                buttonStyle.alignment = TextAlignment::Center;
                buttonStyle.color =
                    on ? kOnPrimaryContainer.Resolve(dark_) : kOnSurfaceVariant.Resolve(dark_);
                const std::string name = PollutantLabel(b);
                const double textHeight = MeasureTextHeight(name, half, buttonStyle);
                const double tick = on ? 18.0 * density_ : 0.0;
                const double tickGap = on ? small : 0.0;
                DrawText(canvas, name, buttonLeft + (tick + tickGap) / 2.0,
                         y + (buttonHeight - textHeight) / 2.0, half, buttonStyle);
                if (on) {
                    const double labelWidth = MeasureTextWidth(name, buttonStyle);
                    DrawAsset(canvas, "ic_check.svg",
                              buttonLeft + (half - labelWidth - tick - tickGap) / 2.0,
                              y + (buttonHeight - tick) / 2.0, tick, buttonStyle.color, true);
                }
                pollutantBands_.push_back(
                    {buttonLeft, y, buttonLeft + half, y + buttonHeight, b});
            }
            if (hasOverflow) {
                const double iconLeft = left + textWidth - iconSize;
                DrawAsset(canvas, "ic_more_vert.svg", iconLeft,
                          y + (buttonHeight - iconSize) / 2.0, iconSize,
                          kOnSurfaceVariant.Resolve(dark_), true);
                pollutantOverflowBand_ = {iconLeft - small, y, left + textWidth,
                                          y + buttonHeight, 0};
            }
            y += buttonHeight;
        }
    }

    /*
     * The one-line explanation under the switcher (kt:239-251): which temperature is plotted.
     */
    if (tag_ == DetailTag::Conditions || tag_ == DetailTag::FeelsLike) {
        TextStyleSpec note;
        note.sizeVp = dimens::kSubtitleTextSize;
        note.color = onSurface;
        const char* const text =
            tag_ == DetailTag::Conditions
                ? "The actual temperature."  // strings.xml:53
                : "What the temperature feels like, as a result of humidity, wind and/or sunlight.";
        y += small;
        y += DrawText(canvas, text, left, y, textWidth, note);
    }

    /*
     * The bare DetailsItem rows, between the chart's explanation and the sections — where
     * DetailsConditions.kt:284-360 puts the degree day. Each is preceded by a small_margin spacer
     * (kt:286-288) and drawn only when there is one, so most days show nothing.
     */
    itemBands_.clear();
    {
        const std::vector<Item> items = Items();
        TextStyleSpec headlineStyle;
        headlineStyle.sizeVp = kBodyTextSize;
        headlineStyle.color = onSurface;
        headlineStyle.bold = true;
        headlineStyle.singleLine = true;
        /*
         * The supporting line is a BARE `Text(it)` (DetailsCommon.kt:130-132) — 16 sp with NO
         * line height, unlike the headline, which is styled bodyLarge and so takes its 24 sp line.
         * Measured on the reference a `DetailsItem` is 139 px, and 78 + 61 is exactly that; this
         * port drew 61 + 53 from a 14 sp supporting line and no headline line height.
         */
        TextStyleSpec supportingStyle;
        supportingStyle.sizeVp = kBodyTextSize;
        supportingStyle.color = onSurface;
        supportingStyle.singleLine = true;
        const double iconSize = dimens::kMaterialIconSize * density_;
        // The help glyph is sized to the headline's font, as the Kotlin sizes it (kt:120-128).
        const double helpSize = kBodyTextSize * density_;

        for (size_t i = 0; i < items.size(); ++i) {
            const Item& item = items[i];
            y += item.spacerAbove * density_;
            const double rowTop = y;
            const double columnLeft = item.icon.empty() ? left : left + iconSize + small;
            const double columnWidth = textWidth - (columnLeft - left);

            const double headlineWidth = MeasureTextWidth(item.headline, headlineStyle);
            const double headlineHeight =
                DrawTextFixed(canvas, item.headline, columnLeft, y, columnWidth, headlineStyle,
                              dimens::kBodyLargeLineHeight * density_);
            if (!item.explanation.empty()) {
                DrawAsset(canvas, "ic_help_outline.svg", columnLeft + headlineWidth + small,
                          y + (headlineHeight - helpSize) / 2.0, helpSize, onSurface, true);
            }
            double rowHeight = headlineHeight;
            if (!item.supporting.empty()) {
                rowHeight += DrawText(canvas, item.supporting, columnLeft, y + headlineHeight,
                                      columnWidth, supportingStyle);
            }
            if (!item.icon.empty()) {
                DrawAsset(canvas, item.icon, left, rowTop + (rowHeight - iconSize) / 2.0, iconSize,
                          onSurface, true);
            }
            y += rowHeight;
            if (!item.explanation.empty()) {
                itemBands_.push_back(
                    {left, rowTop, left + textWidth, y, static_cast<int>(i)});
            }
            /*
             * The PlainTooltip, above the row it belongs to (TooltipAnchorPosition.Above,
             * kt:291-299). Drawn here rather than as a final overlay so it tracks the row when the
             * content scrolls; everything under it is drawn after, and everything above it was
             * drawn before, so it lands on top of what it should cover.
             */
            if (tooltipItem_ == static_cast<int>(i) && !tooltipText_.empty()) {
                DrawTooltip(canvas, tooltipText_, left, rowTop, textWidth);
            }
        }
    }

    /*
     * Precipitation's second half — the one screen the Kotlin builds as two chart groups
     * (DetailsPrecipitation.kt:166-221): the quantity breakdown, a divider, then the whole
     * "Precipitation probability" group with its own header, chart and breakdown.
     */
    // Cleared every frame so a tag with no second chart cannot leave last frame's band behind and
    // route a touch to a chart that is no longer on screen.
    chart2Top_ = 0.0;
    chart2Bottom_ = 0.0;
    if (tag_ == DetailTag::Precipitation) {
        y += DrawPairs(canvas, PrecipitationPairs(0), left, y, textWidth);
    }

    /*
     * The probability group closes TWO screens, not one.
     *
     * DetailsPrecipitation.kt:166-221 puts it under the quantity breakdown, and
     * DetailsConditions.kt:360-405 puts the identical group — divider, headlineSmall heading, the
     * Daytime/Nighttime summary, the chart and the breakdown — at the end of the CONDITIONS page.
     * It used to be gated on the precipitation tag alone, so the screen every hourly and daily
     * trend column opens simply ended after the description line. ArkUIApp had the same gate.
     */
    /**
     * A screen's SECOND chart group: a divider, a headlineSmall heading, its own header and its
     * own chart.
     *
     * Three tags have one, and it is the same five elements in the same order every time.
     * Precipitation and Conditions close with the probability group; HUMIDITY has the DEW POINT
     * group (DetailsHumidity.kt:176-216), which sits BETWEEN its two About cards rather than after
     * them — so this is a lambda rather than an inline block, and the sections loop below calls it
     * too. ArkUIApp's `secondGroup` builder is the same shape.
     *
     * The heading is drawn at the page inset, NOT indented by a further normal_margin the way
     * `DetailsSectionHeader` is: the reference puts "Dew point" at x=52 where "About relative
     * humidity" is at 104.
     */
    const auto drawSecondGroup = [&](const std::string& headingText, double headingGap) {
        // DetailsSectionDivider — a rule with a normal_margin above and below (kt:310-317).
        y += margin;
        FillRect(canvas, left, y, left + textWidth, y + 1.0, kOutline.Resolve(dark_));
        y += margin;

        TextStyleSpec heading;
        heading.sizeVp = kHeadlineSmallTextSize;
        heading.color = onSurface;
        heading.singleLine = true;
        y += DrawTextFixed(canvas, headingText, left, y, textWidth, heading,
                           dimens::kHeadlineSmallLineHeight * density_);
        /*
         * The gap under the heading is NOT the same on the two groups: the probability heading is
         * followed by a `Spacer(small_margin)` (DetailsPrecipitation.kt:190-192) and the dew-point
         * one by a `Spacer(normal_margin)` (DetailsHumidity.kt:185-187). Measured on the
         * reference: 2428 -> 2454 against 2393 -> 2445.
         */
        y += headingGap;

        /* the group's own header — the same columns, on the second marker */
        const Summary second = BuildSummary(1);
        double secondBottom = y;
        if (!second.columns.empty()) {
            const size_t columnCount = second.columns.size();
            const double columnWidth =
                (textWidth - small * static_cast<double>(columnCount - 1)) /
                static_cast<double>(columnCount);
            for (size_t c = 0; c < columnCount; ++c) {
                const SummaryColumn& col = second.columns[c];
                const double columnLeft = left + static_cast<double>(c) * (columnWidth + small);
                double columnY = y;
                columnY += DrawTextFixed(canvas, col.label, columnLeft, columnY, columnWidth,
                                         labelStyle, dimens::kLabelMediumLineHeight * density_);
                columnY += DrawTextFixed(canvas, col.value, columnLeft, columnY, columnWidth,
                                         valueStyle, dimens::kDisplaySmallLineHeight * density_);
                secondBottom = std::max(secondBottom, columnY);
            }
        }
        y = secondBottom + margin;

        const ChartSpec secondChart = BuildChart(1);
        if (secondChart.valid) {
            chart2Top_ = y;
            y += DrawChart(canvas, secondChart, left, y, textWidth, 1);
            chart2Bottom_ = y;
        } else {
            TextStyleSpec note;
            note.sizeVp = dimens::kSubtitleTextSize;
            note.color = onSurface;
            const std::string text = weather_.hourlyForecast.empty()
                                         ? "No hourly data available."
                                         : "Not enough hourly data available to display a "
                                           "chart for this day.";
            DrawAsset(canvas, "ic_info.svg", left, y, iconSize, onSurface, true);
            y += std::max(iconSize, DrawText(canvas, text, left + iconSize + small, y,
                                             textWidth / 2.0 - iconSize - small, note));
            y += small;
        }
    };

    if (tag_ == DetailTag::Precipitation || tag_ == DetailTag::Conditions ||
        tag_ == DetailTag::FeelsLike) {
        if (HasProbability()) {
            drawSecondGroup("Precipitation probability", small);
            y += DrawPairs(canvas, PrecipitationPairs(1), left, y, textWidth);
        }
    }

    y += margin;

    /* the About sections */
    TextStyleSpec headingStyle;
    headingStyle.sizeVp = kSectionTextSize;
    headingStyle.color = onSurface;
    headingStyle.singleLine = true;
    TextStyleSpec bodyStyle;
    bodyStyle.sizeVp = kBodyTextSize;
    bodyStyle.color = onSurface;

    {
        // Named because it is the one expensive thing on this screen: laying out a 300-character
        // paragraph is real work, and it was being done four times a frame (twice to measure the
        // card, twice to draw it) on every frame of a fling.
        BREEZY_TRACE("breezy:DetailsAbout");
        TextStyleSpec subtitleStyle;
        subtitleStyle.sizeVp = dimens::kSubtitleTextSize;
        subtitleStyle.color = onSurfaceVariant;
        TextStyleSpec rowValueStyle;
        rowValueStyle.sizeVp = dimens::kContentTextSize;
        rowValueStyle.color = onSurfaceVariant;
        rowValueStyle.singleLine = true;
        rowValueStyle.alignment = TextAlignment::Right;

        const std::vector<Section> sections = Sections();
        for (size_t sectionIndex = 0; sectionIndex < sections.size(); ++sectionIndex) {
            const Section& section = sections[sectionIndex];
            // A heading of "" is a card with no header above it, which is what the sun & moon
            // screen draws before its About block (kt:104-126).
            if (!section.heading.empty()) {
                // DetailsSectionHeader pads a further normal_margin from the start (kt:302), and
                // its Text is titleLarge — 28 dp a line, where the painter's own box is 25.8.
                y += DrawTextFixed(canvas, section.heading, left + margin, y, textWidth - margin,
                                   headingStyle, dimens::kTitleLargeLineHeight * density_);
                if (section.subtitle.empty()) {
                    y += small;
                }
            }
            if (!section.subtitle.empty()) {
                y += DrawTextFixed(canvas, section.subtitle, left + margin, y, textWidth - margin,
                                   subtitleStyle, dimens::kBodySmallLineHeight * density_);
                y += small;
            }
            if (section.paragraphs.empty() && section.rows.empty()) {
                y += margin;
                continue;
            }

            const double cardTextWidth = textWidth - margin * 2.0;
            const double bodyLine = dimens::kBodyLargeLineHeight * density_;
            /*
             * `DetailsCardText` and every scale cell are plain `Text`s at Compose's default
             * bodyLarge — 16 sp on a 24 sp line, so 78 px a line where this painter's box is 61.
             * Measured on the reference: a four-line paragraph is 312 px there and was 244 here.
             */
            const double rowHeight = bodyLine;
            /*
             * A `ListItem` row (the Sun & moon card) is a FIXED 56 dp token instead, and the card
             * around it carries no padding of its own — the reference's card is 728 px for four
             * rows, 182 apiece, where this port drew five 104 px lines inside an 8 dp inset.
             */
            const double listItemHeight = dimens::kListItemOneLineHeight * density_;
            double cardHeight = section.listItemRows ? 0.0 : small * 2.0;
            for (size_t i = 0; i < section.paragraphs.size(); ++i) {
                cardHeight += bodyLine * static_cast<double>(MeasureLineCount(
                                             section.paragraphs[i], cardTextWidth, bodyStyle));
                if (i + 1 < section.paragraphs.size()) {
                    cardHeight += margin;
                }
            }
            /*
             * `AirQualityDetails` is a FlowRow of two per row, each a name, a bar and a value —
             * three stacked lines rather than one (kt:656-682).
             */
            const double gridCellHeight = rowHeight * 2.0 + small * 2.0;
            const size_t gridRows = (section.rows.size() + 1) / 2;
            /*
             * `PollenGrid` (AllergenComposables.kt:97-131) is a dot beside two stacked lines, so
             * its cell is as tall as a material icon or two text lines, whichever is taller, plus
             * the normal_margin the FlowRow spaces its rows by.
             */
            const double pollenCellHeight = std::max(rowHeight, kBodyTextSize * density_ +
                                                                    dimens::kContentTextSize *
                                                                        density_ * 1.4) +
                                            margin;
            if (section.pollutantGrid) {
                cardHeight += static_cast<double>(gridRows) * gridCellHeight;
            } else if (section.pollenGrid) {
                cardHeight += static_cast<double>(gridRows) * pollenCellHeight;
            } else if (section.listItemRows) {
                cardHeight += static_cast<double>(section.rows.size()) * listItemHeight;
            } else {
                cardHeight += static_cast<double>(section.rows.size()) * (rowHeight + small);
                if (!section.rows.empty()) {
                    cardHeight -= small;
                }
            }
            const bool hasHeaderRow = !section.headerLabel.empty();
            if (hasHeaderRow) {
                cardHeight += rowHeight + small;
            }
            FillRoundRect(canvas, left, y, width_ - margin, y + cardHeight,
                          dimens::kSettingsCardCornerRadius * density_,
                          kSettingsCard.Resolve(dark_));
            double textY = y + (section.listItemRows ? 0.0 : small);
            for (size_t i = 0; i < section.paragraphs.size(); ++i) {
                textY += DrawTextFixed(canvas, section.paragraphs[i], left + margin, textY,
                                       cardTextWidth, bodyStyle, bodyLine);
                if (i + 1 < section.paragraphs.size()) {
                    textY += margin;
                }
            }
            /*
             * A scale table's columns, in the weights the Kotlin gives them — which are NOT the
             * same in the four tables (see Section::labelWeight). The gaps between them are the
             * Row's `spacedBy(small_margin)`.
             */
            const double weightTotal =
                section.labelWeight +
                (section.headerMiddle.empty() ? 0.0f : section.middleWeight) + section.valueWeight;
            const double gaps = section.headerMiddle.empty() ? small : small * 2.0;
            const double unitWidth = (cardTextWidth - gaps) / weightTotal;
            const double labelWidth = unitWidth * section.labelWeight;
            const double middleLeft = left + margin + labelWidth + small;
            const double middleWidth = unitWidth * section.middleWeight;
            if (hasHeaderRow) {
                TextStyleSpec head = bodyStyle;
                head.bold = true;
                head.singleLine = true;
                DrawTextFixed(canvas, section.headerLabel, left + margin, textY, labelWidth, head,
                              rowHeight);
                // Air quality and Beaufort have a THIRD column (kt:736-756 / DetailsWind.kt:520).
                if (!section.headerMiddle.empty()) {
                    head.singleLine = false;
                    DrawTextFixed(canvas, section.headerMiddle, middleLeft, textY, middleWidth,
                                  head, rowHeight);
                    head.singleLine = true;
                }
                head.alignment = TextAlignment::Right;
                DrawTextFixed(canvas, section.headerValue, left + margin, textY, cardTextWidth,
                              head, rowHeight);
                textY += rowHeight + small;
            }
            if (section.pollutantGrid) {
                /*
                 * `AirQualityDetails` (kt:656-682): a bold name, a 10 vp bar in the pollutant's
                 * colour over the same colour at 10%, and the concentration right-aligned under.
                 */
                TextStyleSpec nameStyle = bodyStyle;
                nameStyle.bold = true;
                nameStyle.singleLine = true;
                TextStyleSpec valueStyle = bodyStyle;
                valueStyle.singleLine = true;
                valueStyle.alignment = TextAlignment::Right;
                const double cellWidth = cardTextWidth / 2.0;
                const double barHeight = kPollutantBarHeight * density_;
                for (size_t i = 0; i < section.rows.size(); ++i) {
                    const Row& row = section.rows[i];
                    const double cellLeft =
                        left + margin + static_cast<double>(i % 2) * cellWidth;
                    const double cellTop =
                        textY + static_cast<double>(i / 2) * gridCellHeight;
                    const double innerLeft = cellLeft + small / 2.0;
                    const double innerWidth = cellWidth - small;
                    double lineY = cellTop + small / 2.0;
                    lineY += DrawText(canvas, row.label, innerLeft, lineY, innerWidth, nameStyle);
                    // The track is the bar's own colour at 10%, not a separate trackColor.
                    const uint32_t track = (row.dotColor & 0x00FFFFFFu) | 0x1A000000u;
                    FillRoundRect(canvas, innerLeft, lineY, innerLeft + innerWidth,
                                  lineY + barHeight, barHeight / 2.0, track);
                    double fraction = 0.0;
                    if (!row.middle.empty()) {
                        fraction = std::max(0.0, std::min(1.0, std::atof(row.middle.c_str())));
                    }
                    if (fraction > 0.0) {
                        FillRoundRect(canvas, innerLeft, lineY, innerLeft + innerWidth * fraction,
                                      lineY + barHeight, barHeight / 2.0, row.dotColor);
                    }
                    lineY += barHeight;
                    DrawText(canvas, row.value, innerLeft, lineY, innerWidth, valueStyle);
                }
                textY += static_cast<double>(gridRows) * gridCellHeight;
            } else if (section.pollenGrid) {
                /*
                 * `PollenGrid` (AllergenComposables.kt:97-131): a material-icon-sized dot in the
                 * allergen's colour, then its name in titleMedium over "12/m³ – Low" in bodyMedium.
                 */
                TextStyleSpec nameStyle = bodyStyle;
                nameStyle.singleLine = true;
                TextStyleSpec subtitleStyle2;
                subtitleStyle2.sizeVp = dimens::kContentTextSize;
                subtitleStyle2.color = onSurfaceVariant;
                subtitleStyle2.singleLine = true;
                const double cellWidth = cardTextWidth / 2.0;
                for (size_t i = 0; i < section.rows.size(); ++i) {
                    const Row& row = section.rows[i];
                    const double cellLeft =
                        left + margin + static_cast<double>(i % 2) * cellWidth;
                    const double cellTop =
                        textY + static_cast<double>(i / 2) * pollenCellHeight;
                    const double dot = dimens::kMaterialIconSize * density_;
                    const double innerLeft = cellLeft + dot + small;
                    const double innerWidth = cellWidth - dot - small * 2.0;
                    const double nameHeight = MeasureTextHeight(row.label, innerWidth, nameStyle);
                    const double subtitleHeight =
                        MeasureTextHeight(row.value, innerWidth, subtitleStyle2);
                    const double textHeight = nameHeight + subtitleHeight;
                    DrawAsset(canvas, "ic_circle.svg", cellLeft,
                              cellTop + (textHeight - dot) / 2.0, dot, row.dotColor, true);
                    DrawText(canvas, row.label, innerLeft, cellTop, innerWidth, nameStyle);
                    DrawText(canvas, row.value, innerLeft, cellTop + nameHeight, innerWidth,
                             subtitleStyle2);
                }
                textY += static_cast<double>(gridRows) * pollenCellHeight;
            } else if (section.listItemRows) {
                /*
                 * The Sun & moon card — four Material3 `ListItem`s (DetailsSunMoon.kt:339-558),
                 * each a fixed 56 dp with a 24 dp leading image, 16 dp of padding a side and 16 dp
                 * between the image and the headline. Measured on the reference: rows on an exact
                 * 182 px pitch, icon at x=104, text at x=234, off a card edge of 52.
                 */
                TextStyleSpec lineStyle = bodyStyle;
                lineStyle.singleLine = true;
                const double icon = dimens::kMaterialIconSize * density_;
                for (const Row& row : section.rows) {
                    const double iconLeft = left + margin;
                    const double textLeft = iconLeft + icon + margin;
                    if (row.moonPhaseAngle >= 0.0) {
                        // `DailyMoonPhase`'s leading content is a MoonPhaseView (kt:534-548), with
                        // colorTextLight2nd / colorTextDark2nd for the disc and the body text
                        // colour for the outline — the same triple DetailBlockPainter uses.
                        DrawMoonPhase(canvas, iconLeft, textY + (listItemHeight - icon) / 2.0, icon,
                                      static_cast<int>(row.moonPhaseAngle), 0xFF999999, 0xFF666666,
                                      onSurface);
                    } else if (!row.icon.empty()) {
                        // Android tints all three with colorTextContent (kt:344, 420, 481), so the
                        // artwork's own colour never shows and the "_light" cut is as good as the
                        // "_dark" one. ArkUIApp draws the same two files.
                        DrawAsset(canvas, row.icon, iconLeft,
                                  textY + (listItemHeight - icon) / 2.0, icon, onSurface, true);
                    }
                    DrawTextFixed(canvas, row.label, textLeft, textY,
                                  cardTextWidth - (textLeft - left - margin), lineStyle,
                                  listItemHeight);
                    textY += listItemHeight;
                }
            } else {
                for (size_t i = 0; i < section.rows.size(); ++i) {
                    const Row& row = section.rows[i];
                    /*
                     * The first column is a Row of the dot and the label sharing ONE weight, and
                     * the dot is `material_icon_size` — 24 dp, not 16 (DetailsUV.kt:362-368).
                     * Measured: the reference's row text starts at x=208 and this port's at 182.
                     */
                    double rowLeft = left + margin;
                    if (row.dotColor != 0) {
                        const double dot = dimens::kMaterialIconSize * density_;
                        FillCircle(canvas, rowLeft + dot / 2.0, textY + rowHeight / 2.0, dot / 2.0,
                                   row.dotColor);
                        rowLeft += dot + small;
                    }
                    DrawTextFixed(canvas, row.label, rowLeft, textY,
                                  labelWidth - (rowLeft - left - margin), bodyStyle, rowHeight);
                    // The middle column, when the table has three.
                    if (!row.middle.empty()) {
                        TextStyleSpec middleStyle = bodyStyle;
                        DrawTextFixed(canvas, row.middle, middleLeft, textY, middleWidth,
                                      middleStyle, rowHeight);
                    }
                    if (!row.value.empty()) {
                        // Also a plain bodyLarge onSurface Text, not a smaller onSurfaceVariant.
                        TextStyleSpec valueStyle = bodyStyle;
                        valueStyle.singleLine = true;
                        valueStyle.alignment = TextAlignment::Right;
                        DrawTextFixed(canvas, row.value, left + margin, textY, cardTextWidth,
                                      valueStyle, rowHeight);
                    }
                    textY += rowHeight + small;
                }
            }
            y += cardHeight;
            /*
             * Humidity is the one screen whose second group sits BETWEEN its two About cards, not
             * after them (DetailsHumidity.kt:176-216). Measured on the reference: the About card
             * ends at 2182 and "Dew point" starts at 2289 — 107 px, which is the divider's own
             * 16 + 1 + 16 dp and NOT a section gap on top of it, so the card gives up its margin.
             */
            if (tag_ == DetailTag::Humidity && sectionIndex == 0 && BuildChart(1).valid) {
                drawSecondGroup("Dew point", margin);  // strings.xml `dew_point`
            } else {
                y += margin;
            }
        }
    }

    y += kBottomInset * density_ + bottomInset_;
    contentHeight_ = y + contentScroll_.Position() - contentTop;
    OH_Drawing_CanvasRestore(canvas);

    /* ---- the FAB and its menu ---- */
    const double fab = kFabSize * density_;
    const double fabRight = width_ - margin;
    const double fabBottom = height_ - bottomInset_ - margin;
    menuBands_.clear();
    if (menuOpen_) {
        TextStyleSpec itemStyle;
        itemStyle.sizeVp = kBodyTextSize;
        itemStyle.color = kOnPrimaryContainer.Resolve(dark_);
        itemStyle.singleLine = true;
        const std::vector<DetailTag> tags =
            DetailTagsFor(weather_, hasAirQualitySource_, hasPollenSource_);
        double itemBottom = fabBottom - fab - small;
        for (size_t i = tags.size(); i > 0; --i) {
            const DetailTag item = tags[i - 1];
            const std::string name = DetailTagName(item);
            const double nameWidth = MeasureTextWidth(name, itemStyle);
            const double itemHeight = MeasureTextHeight(name, nameWidth + 1.0, itemStyle) +
                                      (small + 4.0 * density_) * 2.0;
            const double itemWidth = margin * 2.0 + iconSize + small + nameWidth;
            const double itemLeft = fabRight - itemWidth;
            const double itemTop = itemBottom - itemHeight;
            FillRoundRect(canvas, itemLeft, itemTop, fabRight, itemBottom,
                          dimens::kSettingsCardCornerRadius * density_,
                          kPrimaryContainer.Resolve(dark_));
            DrawAsset(canvas, DetailTagIcon(item), itemLeft + margin,
                      itemTop + (itemHeight - iconSize) / 2.0, iconSize,
                      kOnPrimaryContainer.Resolve(dark_), true);
            const double nameHeight = MeasureTextHeight(name, nameWidth + 1.0, itemStyle);
            DrawText(canvas, name, itemLeft + margin + iconSize + small,
                     itemTop + (itemHeight - nameHeight) / 2.0, nameWidth + 1.0, itemStyle);
            menuBands_.push_back({itemLeft, itemTop, fabRight, itemBottom,
                                  static_cast<int>(item)});
            itemBottom = itemTop - small;
        }
    }

    /*
     * The pollutant picker's ⋮ menu — the tail of the ButtonGroup (kt:550-570). Drawn last so it
     * sits over the content, and anchored under the ⋮ it belongs to.
     */
    pollutantMenuBands_.clear();
    if (pollutantMenuOpen_ && pollutantOverflowBand_.bottom > 0.0) {
        TextStyleSpec itemStyle;
        itemStyle.sizeVp = kBodyTextSize;
        itemStyle.color = kOnSurface.Resolve(dark_);
        itemStyle.singleLine = true;
        const int total = static_cast<int>(PollutantChoices().size()) + 1;
        const double menuWidth = kPollutantMenuWidth * density_;
        const double itemHeight = 48.0 * density_;
        const double menuLeft = std::max(margin, pollutantOverflowBand_.right - menuWidth);
        double itemTop = pollutantOverflowBand_.bottom + small;
        const double menuHeight =
            static_cast<double>(total - kInlinePollutantCount) * itemHeight + small * 2.0;
        FillRoundRect(canvas, menuLeft, itemTop, menuLeft + menuWidth, itemTop + menuHeight,
                      dimens::kSettingsCardCornerRadius * density_, kSettingsCard.Resolve(dark_));
        itemTop += small;
        for (int index = kInlinePollutantCount; index < total; ++index) {
            const std::string name = PollutantLabel(index);
            const double nameHeight = MeasureTextHeight(name, menuWidth - margin * 2.0, itemStyle);
            DrawText(canvas, name, menuLeft + margin, itemTop + (itemHeight - nameHeight) / 2.0,
                     menuWidth - margin * 2.0, itemStyle);
            if (SelectedPollutantIndex() == index) {
                // The tick goes on the TRAILING edge — `endIcon`, as the trend tabs' menu has it.
                const double tick = dimens::kMaterialIconSize * density_;
                DrawAsset(canvas, "ic_check.svg", menuLeft + menuWidth - margin - tick,
                          itemTop + (itemHeight - tick) / 2.0, tick, itemStyle.color, true);
            }
            pollutantMenuBands_.push_back(
                {menuLeft, itemTop, menuLeft + menuWidth, itemTop + itemHeight, index});
            itemTop += itemHeight;
        }
    }

    FillRoundRect(canvas, fabRight - fab, fabBottom - fab, fabRight, fabBottom, margin,
                  kPrimaryContainer.Resolve(dark_));
    // ToggleFloatingActionButton: the icon becomes a - while the menu is open (kt:303-312).
    DrawAsset(canvas, menuOpen_ ? "ic_close.svg" : DetailTagIcon(tag_),
              fabRight - fab + (fab - iconSize) / 2.0, fabBottom - fab + (fab - iconSize) / 2.0,
              iconSize, kOnPrimaryContainer.Resolve(dark_), true);
    fabBand_ = {fabRight - fab, fabBottom - fab, fabRight, fabBottom, 0};
}

DetailsHit DetailsScreen::HitTest(double x, double y) {
    DetailsHit hit;

    /*
     * A TooltipBox's tooltip dismisses on the next touch anywhere, so an open one is cleared
     * before anything else is considered — and that tap does nothing further, which is how
     * dismissing a tooltip behaves on Android.
     */
    if (!tooltipText_.empty()) {
        tooltipText_.clear();
        tooltipItem_ = -1;
        return hit;
    }

    // The pollutant ⋮ menu, same rule: an item picks, anywhere else dismisses.
    if (pollutantMenuOpen_) {
        pollutantMenuOpen_ = false;
        for (const Band& band : pollutantMenuBands_) {
            if (x >= band.left && x < band.right && y >= band.top && y < band.bottom) {
                SelectPollutant(PollutantIdAt(band.index));
                break;
            }
        }
        return hit;
    }

    if (menuOpen_) {
        for (const Band& band : menuBands_) {
            if (x >= band.left && x < band.right && y >= band.top && y < band.bottom) {
                tag_ = static_cast<DetailTag>(band.index);
                menuOpen_ = false;
                ClearMarker();
                contentScroll_.Reset();
                return hit;
            }
        }
        if (x >= fabBand_.left && x < fabBand_.right && y >= fabBand_.top && y < fabBand_.bottom) {
            menuOpen_ = false;
            return hit;
        }
        // Anywhere else dismisses, as tapping outside a FloatingActionButtonMenu does.
        menuOpen_ = false;
        return hit;
    }

    if (x >= fabBand_.left && x < fabBand_.right && y >= fabBand_.top && y < fabBand_.bottom) {
        menuOpen_ = true;
        return hit;
    }
    if (x >= backBand_.left && x < backBand_.right && y >= backBand_.top && y < backBand_.bottom) {
        hit.action = DetailsAction::Close;
        return hit;
    }
    for (const Band& band : dayBands_) {
        if (x >= band.left && x < band.right && y >= band.top && y < band.bottom) {
            dayIndex_ = band.index;
            // A marker left from the previous day would put the header on an hour that is no
            // longer plotted — `Open()` resets it for the same reason.
            ClearMarker();
            contentScroll_.Reset();
            return hit;
        }
    }
    // The Actual / Feels like switcher swaps the tag, which is what onRealTempSwitch does.
    for (const Band& band : switcherBands_) {
        if (x >= band.left && x < band.right && y >= band.top && y < band.bottom) {
            tag_ = static_cast<DetailTag>(band.index);
            ClearMarker();
            contentScroll_.Reset();
            return hit;
        }
    }
    // The pollutant picker: a button selects, the ⋮ opens the overflow (kt:566-655).
    for (const Band& band : pollutantBands_) {
        if (x >= band.left && x < band.right && y >= band.top && y < band.bottom) {
            SelectPollutant(PollutantIdAt(band.index));
            return hit;
        }
    }
    if (pollutantOverflowBand_.bottom > 0.0 && x >= pollutantOverflowBand_.left &&
        x < pollutantOverflowBand_.right && y >= pollutantOverflowBand_.top &&
        y < pollutantOverflowBand_.bottom) {
        pollutantMenuOpen_ = true;
        return hit;
    }
    // A DetailsItem with help is `clickable { tooltipState.show() }` (kt:319-323).
    for (const Band& band : itemBands_) {
        if (x >= band.left && x < band.right && y >= band.top && y < band.bottom) {
            const std::vector<Item> items = Items();
            if (band.index >= 0 && band.index < static_cast<int>(items.size())) {
                tooltipText_ = items[static_cast<size_t>(band.index)].explanation;
                tooltipItem_ = band.index;
            }
            return hit;
        }
    }
    return hit;
}

bool DetailsScreen::ScrollBy(double deltaY) {
    const double viewport = height_ - ContentTop();
    contentScroll_.SetRange(std::max(0.0, contentHeight_ - viewport));
    const double before = contentScroll_.Position();
    contentScroll_.Drag(-deltaY);
    return contentScroll_.Position() != before;
}

bool DetailsScreen::ScrollTabsBy(double deltaX) {
    tabScroll_.SetRange(std::max(0.0, tabRowWidth_ - width_));
    const double before = tabScroll_.Position();
    tabScroll_.Drag(-deltaX);
    return tabScroll_.Position() != before;
}

void DetailsScreen::FlingTabs(double velocity) {
    tabScroll_.SetRange(std::max(0.0, tabRowWidth_ - width_));
    tabScroll_.Fling(velocity);
}

void DetailsScreen::FlingContent(double velocity) {
    const double viewport = height_ - ContentTop();
    contentScroll_.SetRange(std::max(0.0, contentHeight_ - viewport));
    contentScroll_.Fling(velocity);
}

bool DetailsScreen::Advance(double seconds) {
    bool more = tabScroll_.Advance(seconds);
    more = contentScroll_.Advance(seconds) || more;
    /*
     * The condition icon is a clock too — its pose is a pure function of this counter, so advancing
     * it IS the animation. Only the Conditions and Feels like tags draw one, which is what
     * AnimatingConditionIcon() tests; every other tag leaves the loop alone.
     */
    if (AnimatingConditionIcon()) {
        conditionIconElapsed_ += seconds;
        more = true;
    }
    return more;
}

bool DetailsScreen::AnimatingConditionIcon() const {
    /*
     * Latched by Draw rather than recomputed: the columns come from BuildSummary(), which runs per
     * frame and is not something to call twice. Only Conditions and Feels like have an icon, so on
     * every other tag this stays false and the frame loop is left alone.
     */
    return elementAnimations_ && hasConditionIcon_;
}

bool DetailsScreen::MoveMarker(double x, double y) {
    /*
     * Which chart the finger is over. Precipitation draws two, and the Kotlin gives each its own
     * active item (kt:103-151), so the probability chart moves its own marker and leaves the
     * quantity header on its day summary.
     */
    const int variant = (chart2Bottom_ > chart2Top_ && y >= chart2Top_ && y <= chart2Bottom_) ? 1
                                                                                             : 0;
    const ChartSpec spec = BuildChart(variant);
    if (!spec.valid || plotRight_ <= plotLeft_) {
        return false;
    }
    // Only a touch over a chart itself, not the header or the About cards.
    if (variant == 0 && (y < chartTop_ || y > chartBottom_)) {
        return false;
    }
    const double span = spec.maxX - spec.minX;
    const double ratio = std::max(0.0, std::min(1.0, (x - plotLeft_) / (plotRight_ - plotLeft_)));
    const double wanted = spec.minX + ratio * span;
    double best = spec.xs.front();
    double bestGap = std::fabs(best - wanted);
    for (double at : spec.xs) {
        const double gap = std::fabs(at - wanted);
        if (gap < bestGap) {
            bestGap = gap;
            best = at;
        }
    }
    const int64_t hour = static_cast<int64_t>(best);
    int64_t& active = variant == 1 ? activeHourMs2_ : activeHourMs_;
    if (hour == active) {
        return false;
    }
    active = hour;
    return true;
}

bool DetailsScreen::ClearMarker() {
    if (activeHourMs_ < 0 && activeHourMs2_ < 0) {
        return false;
    }
    activeHourMs_ = -1;
    activeHourMs2_ = -1;
    return true;
}

void DetailsScreen::StopScrolling() {
    tabScroll_.Stop();
    contentScroll_.Stop();
}

double DetailsScreen::TabRowBottom() const {
    return topInset_ + kBarHeight * density_ + TabRowHeight();
}

bool DetailsScreen::TabBand(double* top, double* bottom) const {
    *top = topInset_ + kBarHeight * density_;
    // +1 for the row's divider, which is drawn just under it.
    *bottom = TabRowBottom() + 1.0;
    return *bottom > *top;
}

}  // namespace breezy
