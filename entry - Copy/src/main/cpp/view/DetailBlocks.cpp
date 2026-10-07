#include "view/DetailBlocks.h"

#include <cmath>

#include "common/Formatter.h"
#include "common/LocalTime.h"
#include "common/Units.h"
#include "data/AirQuality.h"
#include "data/Pollutant.h"
#include "render/Theme.h"

namespace breezy {
namespace {

/**
 * The half-day the precipitation tile's total covers — PrecipitationViewHolder.kt:44-52.
 *
 * Before 05:00 it is YESTERDAY's night, before 17:00 today's day, otherwise tonight. The same
 * window `CurrentHalfDayPrecipitation` picks the total from; this returns the half-day itself
 * because the tile's LABEL depends on its rain and snow as well as on its total.
 */
const HalfDay* CurrentHalfDay(const Weather& weather, int64_t nowMs, int utcOffsetSeconds) {
    const int todayIndex = weather.todayIndex;
    if (todayIndex < 0 || todayIndex >= static_cast<int>(weather.dailyForecast.size())) {
        return nullptr;
    }
    const Daily& today = weather.dailyForecast[static_cast<size_t>(todayIndex)];
    const int hour = LocalHours(nowMs, utcOffsetSeconds);
    if (hour < 5) {
        if (todayIndex == 0) {
            return nullptr;
        }
        return &weather.dailyForecast[static_cast<size_t>(todayIndex - 1)].night;
    }
    return hour < 17 ? &today.day : &today.night;
}

/**
 * The tile's second line — PrecipitationViewHolder.kt:54-84.
 *
 * SIX strings, not one: "Daytime"/"Nighttime" by the half-day, and "precipitation"/"rain"/"snow"
 * by what the half-day is made of. It narrows to "rain" or "snow" only when that part is the WHOLE
 * total; a mixed half-day, or one with both, stays on the general wording.
 *
 * Both ports hard-coded "Daytime precipitation" until now — symmetric, so it never showed up as an
 * asymmetry, but wrong against Android for every evening and every snowy day. It could not be
 * ported before this session because `HalfDay` carried no rain or snow to test.
 */
std::string PrecipitationTileLabel(const HalfDay* half, bool isDay) {
    const double rain = (half != nullptr && half->precipitationRain.has_value())
                            ? *half->precipitationRain : 0.0;
    const double snow = (half != nullptr && half->precipitationSnow.has_value())
                            ? *half->precipitationSnow : 0.0;
    const double total = half != nullptr ? half->precipitationTotal : 0.0;
    /*
     * The Kotlin compares Precipitation values, which are whole micrometres and so exactly equal
     * when one part is the entire total. These are accumulated doubles in millimetres, so the same
     * test needs a tolerance well under a micrometre.
     */
    const auto isWholeTotal = [total](double part) { return std::fabs(part - total) < 1e-6; };
    if (rain > 0.0 && snow > 0.0) {
        return isDay ? "Daytime precipitation" : "Nighttime precipitation";
    }
    if (snow > 0.0 && isWholeTotal(snow)) {
        return isDay ? "Daytime snow" : "Nighttime snow";
    }
    if (rain > 0.0 && snow <= 0.0 && isWholeTotal(rain)) {
        return isDay ? "Daytime rain" : "Nighttime rain";
    }
    return isDay ? "Daytime precipitation" : "Nighttime precipitation";
}

/** HumidityViewHolder.kt:56-69 — the wave fill is chosen by humidity bucket. */
const char* HumidityWaves(double percent) {
    if (percent <= 20.0) return "humidity_percent_7.svg";
    if (percent <= 40.0) return "humidity_percent_30.svg";
    if (percent <= 60.0) return "humidity_percent_50.svg";
    if (percent <= 80.0) return "humidity_percent_75.svg";
    return "humidity_percent_90.svg";
}

/** UV.getShape() (domain UV.kt:81-91) — the WHO bands 3 / 6 / 8 / 11. */
/*
 * The scalloped body of each uv_*.xml is `?attr/colorSurface`, which Android resolves per theme.
 * Converting the vectors to SVG baked in the LIGHT value (#FDFBFF), so the block rendered as a
 * white blob in the dark theme. The five coloured dots beside it are literal hex and must not be
 * touched, so a blanket tint is no good — hence a `_dark` variant with only the body swapped to
 * the dark surface (#1B1B1D).
 */
const char* UvShape(double index, bool darkMode) {
    if (index < 3.0)  return darkMode ? "uv_low_dark.svg" : "uv_low.svg";
    if (index < 6.0)  return darkMode ? "uv_moderate_dark.svg" : "uv_moderate.svg";
    if (index < 8.0)  return darkMode ? "uv_high_dark.svg" : "uv_high.svg";
    if (index < 11.0) return darkMode ? "uv_very_high_dark.svg" : "uv_very_high.svg";
    return darkMode ? "uv_extreme_dark.svg" : "uv_extreme.svg";
}

/** UV.getLevel() (UV.kt:29-39) — strings.xml:145-149, same bands as the shape. */
const char* UvLevelName(double index) {
    if (index < 3.0)  return "Low";
    if (index < 6.0)  return "Moderate";
    if (index < 8.0)  return "High";
    if (index < 11.0) return "Very high";
    return "Extreme";
}

/**
 * Distance.getVisibilityDescription (UnitExtensions.kt:112-121) against the Met Office bands at
 * kt:94-98 — 1 / 4 / 10 / 20 / 40 km.
 */
const char* VisibilityDescription(double metres) {
    if (metres < 1000.0)  return "Very poor";
    if (metres < 4000.0)  return "Poor";
    if (metres < 10000.0) return "Moderate";
    if (metres < 20000.0) return "Good";
    if (metres < 40000.0) return "Clear";
    return "Perfectly clear";
}

/**
 * AstroViewHolder.kt:125-139 — a light theme takes themeColors[0] for the arc and themeColors[1]
 * at 66 % for the track; a dark theme takes themeColors[2] for the arc and the same at 50 %.
 *
 * kt:84-90 themes both arcs off WEATHER_KIND_CLEAR, passing `isSun` as the daylight flag — so the
 * sun arc is clear-day amber and the moon arc clear-night blue, whatever the weather actually is.
 */
void ApplyAstroColors(DetailBlock& block, bool darkMode) {
    const uint32_t accent = WeatherThemeAccent(WeatherKind::Clear, block.astroIsSun);
    if (!darkMode) {
        block.astroLineColor = accent;
        block.astroTrackColor = WithAlpha(accent, 0.66);
    } else {
        const uint32_t faded = WithAlpha(accent, 0.5);
        block.astroLineColor = faded;
        block.astroTrackColor = WithAlpha(accent, 0.5);
    }
}

void SetInsets(DetailBlock& block, double percent) {
    block.insetTopPercent = percent;
    block.insetBottomPercent = percent;
    block.insetHorizontalPercent = percent;
}

}  // namespace

std::vector<DetailBlock> BuildDetailBlocks(const Weather& weather, bool darkMode, int64_t nowMs,
                                           int utcOffsetSeconds, bool is24Hour) {
    std::vector<DetailBlock> blocks;
    if (!weather.hasCurrent) {
        return blocks;
    }
    const Current& c = weather.current;

    // PrecipitationViewHolder.kt:111-117
    if (c.precipitationTotal.has_value()) {
        DetailBlock block;
        block.id = "precipitation";
        block.icon = "ic_precipitation.svg";
        block.title = "Precipitation";  // strings.xml:76
        block.value = FormatPrecipitation(*c.precipitationTotal);
        block.unit = PrecipitationUnitLabel();
        // PrecipitationViewHolder shows which half-day the total covers, and what it is made of.
        // kt:51 — `isDay` is the 05:00-16:59 window, the same one that picks the half-day.
        const int localHour = LocalHours(nowMs, utcOffsetSeconds);
        block.secondaryLabel = PrecipitationTileLabel(
            CurrentHalfDay(weather, nowMs, utcOffsetSeconds), localHour >= 5 && localHour < 17);
        blocks.push_back(std::move(block));
    }

    // WindViewHolder.kt:98-104
    if (c.windSpeed.has_value()) {
        DetailBlock block;
        block.id = "wind";
        block.icon = "ic_wind.svg";
        block.title = "Wind";  // strings.xml:102
        block.value = FormatSpeed(*c.windSpeed);
        block.unit = SpeedUnitLabel();
        if (c.windGusts.has_value()) {
            block.secondaryLabel = "Gusts: " + FormatSpeed(*c.windGusts) + " " + SpeedUnitLabel();
        }
        // wind_arrow fills with @color/sys_widget_background and turns to point along the wind
        // direction (WindViewHolder.kt:68-75). container_main_wind.xml — 0.2 / 0.8 guidelines.
        SetInsets(block, 0.2);
        block.isCircle = true;
        block.shape = "wind_arrow.svg";
        block.shapeTint = darkMode ? 0xFF404659 : 0xFFDCE2F9;  // sys_widget_background
        block.shapeFitsInside = true;
        block.shapeRotation = c.windDegree.value_or(0.0);
        blocks.push_back(std::move(block));
    }

    // AirQualityViewHolder.kt:101-107
    if (c.airQualityIndex.has_value()) {
        DetailBlock block;
        block.id = "air_quality";
        block.icon = "ic_air_quality.svg";
        block.title = "Air quality";  // strings.xml:14 air_quality_short
        block.value = FormatIndex(*c.airQualityIndex);
        block.secondaryLabel = AqiLevelName(*c.airQualityIndex);
        // kt:81-91 — level colour, background the same at 10 %, max = aqiThresholds.last() = 250.
        const uint32_t color = AqiLevelColor(*c.airQualityIndex);
        SetInsets(block, 0.2);
        block.isCircle = true;
        block.arcProgress = *c.airQualityIndex;
        block.arcMax = 250.0;
        block.arcColor = color;
        block.arcBackground = WithAlpha(color, 0.1);
        blocks.push_back(std::move(block));
    }

    /*
     * PollenViewHolder.kt:129-135, listing `pollensWithConcentration` (Pollen.kt:32-38) — only the
     * allergens reading above zero. The mapper now keeps every allergen the source REPORTS, because
     * the detail screen's PollenGrid lists `validPollens` instead, so the "> 0" rule lives here
     * with the consumer that actually has it.
     */
    std::vector<PollenEntry> pollenWithConcentration;
    for (const PollenEntry& entry : c.pollen) {
        if (entry.concentration > 0.0) {
            pollenWithConcentration.push_back(entry);
        }
    }
    if (!pollenWithConcentration.empty()) {
        DetailBlock block;
        block.id = "pollen";
        block.icon = "ic_allergy.svg";
        block.title = "Pollen";  // strings.xml:16
        SetInsets(block, 0.1);
        block.pollenRows = std::move(pollenWithConcentration);
        blocks.push_back(std::move(block));
    }

    // HumidityViewHolder.kt:89-95
    if (c.relativeHumidity.has_value()) {
        DetailBlock block;
        block.id = "humidity";
        block.icon = "ic_humidity_percentage.svg";
        block.title = "Humidity";  // strings.xml:334
        block.value = FormatIndex(*c.relativeHumidity);
        block.unit = "%";
        block.secondaryLabel = "Dew point";  // strings.xml:342
        if (c.dewPoint.has_value()) {
            block.secondaryValue = FormatTemperatureNarrow(*c.dewPoint);
        }
        block.shape = HumidityWaves(*c.relativeHumidity);
        block.shapeTint = kTertiaryContainer.Resolve(darkMode);
        block.secondaryIsChip = true;
        blocks.push_back(std::move(block));
    }

    // UvViewHolder.kt:71-77
    if (c.uvIndex.has_value()) {
        DetailBlock block;
        block.id = "uv";
        block.icon = "ic_uv.svg";
        block.title = "UV index";  // strings.xml:144 uv_index_short
        block.value = FormatIndex(*c.uvIndex);
        block.secondaryLabel = UvLevelName(*c.uvIndex);
        // No MaterialCardView in container_main_uv.xml — the sun graphic is the whole block.
        block.hasCard = false;
        block.isCircle = true;
        block.shape = UvShape(*c.uvIndex, darkMode);
        // The sun graphic IS the block — it fills the square rather than sitting inside a card,
        // which is why container_main_uv.xml has no MaterialCardView to inset it against.
        block.shapeFitsInside = false;
        // container_main_uv.xml:16-42 — tighter at the bottom than every other block, which is
        // what keeps the level label clear of the sun graphic's beads.
        block.insetTopPercent = 0.2;
        block.insetBottomPercent = 1.0 - 0.77;
        block.insetHorizontalPercent = 0.2;
        blocks.push_back(std::move(block));
    }

    // VisibilityViewHolder.kt:63-69
    if (c.visibility.has_value()) {
        DetailBlock block;
        block.id = "visibility";
        block.icon = "ic_eye.svg";
        block.title = "Visibility";  // strings.xml:361
        block.value = FormatDistance(*c.visibility);
        block.unit = DistanceUnitLabel();
        block.secondaryLabel = VisibilityDescription(*c.visibility);
        SetInsets(block, 0.2);
        block.isCircle = true;
        block.shape = "visibility_shape.svg";
        block.shapeTint = kTertiaryContainer.Resolve(darkMode);
        blocks.push_back(std::move(block));
    }

    // PressureViewHolder.kt:76-82
    if (c.pressure.has_value()) {
        DetailBlock block;
        block.id = "pressure";
        block.icon = "ic_gauge.svg";
        block.title = "Pressure";  // strings.xml:350
        block.value = FormatPressure(*c.pressure);
        block.unit = PressureUnitLabel();
        // kt:59-68 — progress is hPa above 963 on a 100-wide scale, in colorPrimary.
        const uint32_t primary = kPrimary.Resolve(darkMode);
        SetInsets(block, 0.2);
        block.unitOnOwnLine = true;
        block.isCircle = true;
        block.arcProgress = *c.pressure - 963.0;
        block.arcMax = 100.0;
        block.arcColor = primary;
        block.arcBackground = WithAlpha(primary, 0.1);
        blocks.push_back(std::move(block));
    }

    // AstroViewHolder.kt:150-156 — both are rectangular cards showing rise and set.
    if (c.sun.hasRise || c.sun.hasSet) {
        DetailBlock block;
        block.id = "sun";
        // kt:93-98 — weather_clear_day_mini, not the UV lightbulb.
        block.icon = "ic_sunny.svg";
        block.title = "Sun";
        SetInsets(block, 0.1);
        block.hasAstro = true;
        block.astro = c.sun;
        block.astroIsSun = true;
        block.astroIcon = "weather_clear_day.png";
        block.astroProgress = c.sun.ProgressAt(nowMs);
        ApplyAstroColors(block, darkMode);
        blocks.push_back(std::move(block));
    }
    if (c.moon.hasRise || c.moon.hasSet) {
        DetailBlock block;
        block.id = "moon";
        block.icon = "ic_weather_night.svg";
        block.title = "Moon";
        SetInsets(block, 0.1);
        block.hasAstro = true;
        block.astro = c.moon;
        block.astroIsSun = false;
        block.astroIcon = "weather_clear_night.png";
        block.astroProgress = c.moon.ProgressAt(nowMs);
        ApplyAstroColors(block, darkMode);
        blocks.push_back(std::move(block));
    }

    /*
     * ClockViewHolder.kt:36-77 -> TAG_CONDITIONS on today (kt:69-76).
     *
     * Unconditional: it is a clock, so unlike every block above it there is no reading that can be
     * absent. It is only ever hidden by being left out of `card_display`, which is where it starts
     * â€” CardDisplay's default list does not include it, so it appears only once the user adds it
     * on the Blocks screen, exactly as on Android.
     *
     * `hasCard = false`: container_main_clock.xml has no MaterialCardView at all, only a
     * ConstraintLayout whose 1:1 child holds the scalloped `clock_shape` drawable. The shape IS
     * the surface, which is why it sits straight on the weather background like the UV block.
     *
     * Insets are the layout's own guidelines: 0.1/0.9 vertically for the text pair, and NONE
     * horizontally â€” the LinearLayout is `match_parent` wide, so it overrides the 0.2/0.8 vertical
     * guidelines it is also constrained to.
     */
    {
        DetailBlock block;
        block.id = "clock";
        // Never drawn â€” the clock has no title row.
        block.icon = "ic_time.svg";
        block.title = "Clock";
        block.isClock = true;
        block.clockHour = LocalHours(nowMs, utcOffsetSeconds);
        block.clockMinute = LocalMinutes(nowMs, utcOffsetSeconds);
        block.clockIs24Hour = is24Hour;
        block.clockDigitColor = kPrimary.Resolve(darkMode);
        block.hasCard = false;
        block.insetTopPercent = 0.1;
        block.insetBottomPercent = 0.1;
        block.insetHorizontalPercent = 0.0;
        block.shape = "clock_shape.svg";
        block.shapeTint = kSurface.Resolve(darkMode);
        /*
         * FALSE, and it means "fill the square" â€” which is what the shape has to do here, because
         * it IS the card's surface. Note the flag does NOT mean the same thing in the two ports:
         * ArkUIApp maps it to ImageFit.Contain vs Cover (identical for a square asset), whereas
         * here `true` insets the asset by 18 % so the wind arrow and UV sun stay whole. Both ports
         * set it false for the clock so both draw the shape at the full side.
         */
        block.shapeFitsInside = false;
        blocks.push_back(std::move(block));
    }

    return blocks;
}

}  // namespace breezy
