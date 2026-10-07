/*
 * The weather model, ported from ArkUIApp's model/Weather.ets — itself Breezy's
 * domain/weather/model/*.kt.
 *
 * Every measurement is optional, because Open-Meteo omits fields rather than zeroing them, and
 * the difference matters: a missing temperature hides a row, a temperature of 0 draws one. The
 * ArkTS port spells that as `number | null`; here it is std::optional<double>.
 *
 * Units are the model's, not the display's: Celsius, metres per second (windspeed_unit=ms),
 * hPa, metres. Conversion happens at the point of formatting, in common/Units.h.
 */
#ifndef BREEZY_MODEL_WEATHER_H
#define BREEZY_MODEL_WEATHER_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "render/Theme.h"

namespace breezy {

using Maybe = std::optional<double>;

struct Temperature {
    Maybe temperature;
    Maybe feelsLikeTemperature;
};

/**
 * One reading's pollutant concentrations — the port of `AirQuality`, minus its derived accessors,
 * which live in data/Pollutant.h.
 *
 * µg/m³ throughout EXCEPT carbon monoxide, held in mg/m³ because that is the unit its thresholds
 * are written in (AirQuality.kt getConcentration).
 */
struct AirQuality {
    Maybe pm25;
    Maybe pm10;
    Maybe o3;
    Maybe no2;
    Maybe so2;
    /** mg/m³ */
    Maybe co;

    bool Any() const {
        return pm25.has_value() || pm10.has_value() || o3.has_value() || no2.has_value() ||
               so2.has_value() || co.has_value();
    }
};

/** One allergen row on the pollen block. */
struct PollenEntry {
    std::string name;
    int level = 0;
    std::string levelName;
    /**
     * Grains per cubic metre — `PollenConcentration.inPerCubicMeter`.
     *
     * The MAIN screen's block lists `pollensWithConcentration` (> 0) and shows the level alone; the
     * detail screen's PollenGrid lists `validPollens` (not null) and its subtitle is "12/m³ – Low"
     * (AllergenComposables.kt:83-85). So the entry carries the number and the "> 0" rule belongs to
     * the consumer.
     */
    double concentration = 0.0;
};

/** A rise/set pair, plus the moon's phase where it applies. */
struct Astro {
    int64_t rise = 0;
    int64_t set = 0;
    bool hasRise = false;
    bool hasSet = false;
    /** 0..360, moon only. Negative when there is none. */
    int phaseAngle = -1;
    std::string phaseName;

    /** SunMoonView's elapsed fraction, or -1 when there is no usable pair. */
    double ProgressAt(int64_t nowMs) const {
        if (!hasRise || !hasSet || set <= rise) {
            return -1.0;
        }
        if (nowMs <= rise) {
            return 0.0;
        }
        if (nowMs >= set) {
            return 1.0;
        }
        return static_cast<double>(nowMs - rise) / static_cast<double>(set - rise);
    }
};

struct Current {
    std::string weatherText;
    WeatherKind weatherKind = WeatherKind::None;
    Temperature temperature;
    Maybe windSpeed;
    Maybe windDegree;
    Maybe windGusts;
    Maybe uvIndex;
    Maybe relativeHumidity;
    Maybe dewPoint;
    Maybe pressure;
    Maybe visibility;
    /** The total for the half-day the current hour falls in (MainAdapter.kt:122-137). */
    Maybe precipitationTotal;
    Maybe airQualityIndex;
    /** The current hour's concentrations — the fallback header when the day has no average. */
    AirQuality airQuality;
    std::vector<PollenEntry> pollen;
    Astro sun;
    Astro moon;
};

struct Hourly {
    int64_t date = 0;
    WeatherKind weatherKind = WeatherKind::None;
    /**
     * The hour's condition text, e.g. "Light drizzle".
     *
     * Only the details screens' chart MARKER shows it — `WeatherConditionItem` is given the
     * hour's `weatherText` when an item is active (DetailsConditions.kt:220-233). It comes from
     * the Open-Meteo weather CODE, not from the derived kind, so it is the finer of the two
     * (a Rainy kind covers drizzle, rain and showers alike). ArkUIApp's `Hourly` carries the
     * same field for the same reason.
     */
    std::string weatherText;
    bool daylight = true;
    Temperature temperature;
    Maybe precipitationProbability;
    Maybe windSpeed;
    Maybe windDegree;
    /** m/s — `wind_gusts_10m`, already requested. The half-day maximum is Wind's third line. */
    Maybe windGusts;
    Maybe cloudCover;
    Maybe visibility;
    Maybe uvIndex;
    Maybe relativeHumidity;
    Maybe dewPoint;
    Maybe pressure;
    /** Filled from the air-quality endpoint, which is a separate request. */
    Maybe airQualityIndex;
    /** The hour's concentrations, which the air-quality chart plots per pollutant. */
    AirQuality airQuality;
    double precipitationTotal = 0.0;
    /**
     * `Precipitation.rain` — Open-Meteo's `rain` PLUS its `showers` (OpenMeteoService.kt:475) — and
     * `Precipitation.snow`, its `snowfall`, which is CENTIMETRES on the wire and is held here in
     * millimetres like every other precipitation quantity. Both absent when the source omits them,
     * which is what keeps the rows off the detail screen.
     */
    Maybe precipitationRain;
    Maybe precipitationSnow;
};

/** One of a day's two halves: 06:00-17:59 is `day`, 18:00-05:59 the next morning is `night`. */
struct HalfDay {
    bool present = false;
    WeatherKind weatherKind = WeatherKind::None;
    std::string weatherText;
    Temperature temperature;
    Maybe precipitationProbability;
    Maybe windSpeed;
    Maybe windDegree;
    /**
     * m/s — the half-day's MAXIMUM gust, which `getHalfDayWindFromHourlyList` (CommonConverter.kt)
     * takes independently of the hour that won the speed: `maxWind.copy(gusts = maxWindGusts)`.
     * Wind's header prints it as its third line (DetailsWind.kt:303-317).
     */
    Maybe windGusts;
    double precipitationTotal = 0.0;
    /**
     * The rain and snow parts of the half-day total, both mm. `getHalfDayPrecipitationFromHourlyList`
     * (CommonConverter.kt:978-1018) SUMS the hours that report each, and leaves it absent when none
     * does.
     */
    Maybe precipitationRain;
    Maybe precipitationSnow;
};

struct Daily {
    int64_t date = 0;
    HalfDay day;
    HalfDay night;
    Maybe uvIndex;
    Maybe sunshineHours;
    /** The day's air-quality index — the index OF the averaged reading below. */
    Maybe airQualityIndex;
    /**
     * Daily.airQuality — the MEAN of the day's hourly concentrations, which is what
     * `getDailyAirQualityFromHourlyList` (CommonConverter.kt:520-542) computes, and needs at
     * least 18 of the day's hours to be produced at all.
     */
    AirQuality airQuality;
    /**
     * Daily.degreeDay, in DECI-CELSIUS — the storage unit Android's `Temperature` uses, kept so
     * the display conversion is the same plain deviation divide (CommonConverter.kt:473-495).
     *
     * Open-Meteo does not supply these; `CommonConverter` computes them from the two half-day
     * temperatures whenever the source omits them (kt:405-413), so Android shows the row and the
     * port has to compute them too. Both 0 is `isValid` false and no row at all.
     */
    Maybe heatingDegreeDay;
    Maybe coolingDegreeDay;
};

/** One nowcast bucket — Open-Meteo returns 15-minute accumulations. */
struct Minutely {
    int64_t date = 0;
    int64_t endingDate = 0;
    /** Scaled to mm/h, which is the unit the thresholds are in. */
    double precipitationIntensity = 0.0;
};

/**
 * A weather warning. Open-Meteo publishes none, so `alertList` is always empty on this build and
 * the alert card never appears — the same as on the ArkTS port and on an Open-Meteo-only Android
 * build. The model and the card exist so a source that does publish them needs no new drawing.
 */
struct Alert {
    /** CAP `<identifier>` — which row the alert screen scrolls to. */
    std::string alertId;
    std::string headline;
    /** CAP `<description>`, plain text. */
    std::string description;
    /** CAP `<instruction>` — what to do about it. */
    std::string instruction;
    /** CAP `<senderName>` — printed as "Source: %s" (strings.xml `alert_source`). */
    std::string source;
    int64_t startDate = 0;
    int64_t endDate = 0;
    bool hasStart = false;
    bool hasEnd = false;
    uint32_t color = 0;
};

struct Weather {
    bool hasCurrent = false;
    Current current;
    std::vector<Minutely> minutelyForecast;
    std::vector<Alert> alertList;
    std::vector<Hourly> hourlyForecast;
    /** Weather.nextHourlyForecast — everything from the start of the current hour onwards. */
    std::vector<Hourly> nextHourlyForecast;
    std::vector<Daily> dailyForecast;
    /** Index within dailyForecast whose date is today AT THE LOCATION; -1 when absent. */
    int todayIndex = -1;
    /** When the response came back, epoch ms — drives the "x minutes ago" line. */
    int64_t refreshTime = 0;
};

}  // namespace breezy

#endif  // BREEZY_MODEL_WEATHER_H
