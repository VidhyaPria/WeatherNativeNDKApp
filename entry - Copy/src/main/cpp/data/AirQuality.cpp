#include "data/AirQuality.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "common/LocalTime.h"
#include "data/Pollutant.h"
#include "json/Json.h"
#include "render/Theme.h"

namespace breezy {
namespace {

/*
 * The pollutant table, the AQI scale and every derivation off them now live in data/Pollutant.h,
 * because the air-quality DETAIL screen needs the same thresholds, names and colours the mapper
 * does. A second copy here is what made the screen and the data disagree about a boundary once.
 */

Maybe At(const json::Value& series, size_t i) {
    const json::Value& value = series[i];
    return value.IsNumber() ? Maybe{value.AsDouble(0.0)} : Maybe{};
}

/**
 * One hour's six concentrations — OpenMeteoService.getAirQuality (kt:498-518).
 *
 * Every series is µg/m³ on the wire; carbon monoxide is divided by 1000 because Android reads it
 * in mg/m³, which is the unit its thresholds are written in.
 */
AirQuality ConcentrationsAt(const json::Value& hourly, size_t i) {
    AirQuality air;
    air.pm25 = At(hourly["pm2_5"], i);
    air.pm10 = At(hourly["pm10"], i);
    air.o3 = At(hourly["ozone"], i);
    air.no2 = At(hourly["nitrogen_dioxide"], i);
    air.so2 = At(hourly["sulphur_dioxide"], i);
    const Maybe carbonMonoxide = At(hourly["carbon_monoxide"], i);
    if (carbonMonoxide.has_value()) {
        air.co = *carbonMonoxide / 1000.0;
    }
    return air;
}

/** getDailyAirQualityFromHourlyList (CommonConverter.kt:520-542) needs 18 hours to answer. */
constexpr size_t kDailyAirQualityMinHours = 18;

/** The mean of the readings that HAVE this pollutant, or nothing when none of them do. */
Maybe MeanOf(const std::vector<double>& values) {
    if (values.empty()) {
        return Maybe{};
    }
    double total = 0.0;
    for (double value : values) {
        total += value;
    }
    return total / static_cast<double>(values.size());
}

/** strings.xml:301-305 */
const char* const kPollenLevelNames[5] = {"Very low", "Low", "Moderate", "High", "Very high"};

/** One allergen the endpoint reports, with its Atmo France bands (PollenIndex.kt:34-61). */
struct PollenSpecies {
    const char* name;
    const char* field;
    double thresholds[5];
};

const PollenSpecies kPollenSpecies[6] = {
    {"Alder", "alder_pollen", {0, 10, 60, 100, 500}},
    {"Birch", "birch_pollen", {0, 10, 60, 100, 500}},
    {"Grass", "grass_pollen", {0, 3, 30, 50, 250}},
    {"Mugwort", "mugwort_pollen", {0, 3, 30, 50, 250}},
    {"Olive", "olive_pollen", {0, 20, 100, 200, 500}},
    {"Ragweed", "ragweed_pollen", {0, 3, 30, 50, 250}},
};

int LevelOf5(double value, const double (&thresholds)[5]) {
    int level = -1;
    for (int i = 0; i < 5; ++i) {
        if (value >= thresholds[i]) {
            level = i;
        }
    }
    return level;
}

/** The entry in the hourly series covering `now`, or -1. */
int CurrentHourIndex(const json::Value& times, int64_t nowMs) {
    const int64_t nowSeconds = nowMs / 1000;
    int best = -1;
    for (size_t i = 0; i < times.Size(); ++i) {
        if (times[i].AsLong(0) <= nowSeconds) {
            best = static_cast<int>(i);
        }
    }
    return best;
}

}  // namespace

void ApplyCurrentAirQuality(const std::string& body, Current& current, int64_t nowMs) {
    const json::Value root = json::Parse(body);
    if (root.GetType() != json::Value::Type::Object || root["error"].AsBool(false)) {
        return;
    }
    const json::Value& hourly = root["hourly"];
    const int index = CurrentHourIndex(hourly["time"], nowMs);
    if (index < 0) {
        return;
    }
    const size_t i = static_cast<size_t>(index);
    current.airQuality = ConcentrationsAt(hourly, i);
    current.airQualityIndex = OverallIndexOf(current.airQuality);

    /*
     * `Pollen.validPollens` (Pollen.kt:27-30) — every allergen the hour REPORTS, concentration not
     * null. The "> 0" filter is `pollensWithConcentration` (kt:32-38) and belongs to the main
     * screen's block alone; the detail screen's PollenGrid lists the valid ones, so a species
     * reading exactly 0 belongs there and used to be dropped before either consumer saw it.
     */
    for (const PollenSpecies& species : kPollenSpecies) {
        const Maybe concentration = At(hourly[species.field], i);
        if (!concentration.has_value()) {
            continue;
        }
        const int level = LevelOf5(*concentration, species.thresholds);
        if (level < 0) {
            continue;
        }
        current.pollen.push_back(
            PollenEntry{species.name, level, kPollenLevelNames[level], *concentration});
    }
}

std::vector<HourlyAirQuality> ParseAirQualitySeries(const std::string& body) {
    std::vector<HourlyAirQuality> out;
    const json::Value root = json::Parse(body);
    if (root.GetType() != json::Value::Type::Object || root["error"].AsBool(false)) {
        return out;
    }
    const json::Value& hourly = root["hourly"];
    const json::Value& time = hourly["time"];
    out.reserve(time.Size());
    for (size_t i = 0; i < time.Size(); ++i) {
        HourlyAirQuality entry;
        entry.startMs = time[i].AsLong(0) * kMillisPerSecond;
        entry.concentrations = ConcentrationsAt(hourly, i);
        entry.index = OverallIndexOf(entry.concentrations);
        out.push_back(entry);
    }
    return out;
}

void ApplyAirQualitySeries(Weather& weather, const std::vector<HourlyAirQuality>& series,
                           int utcOffsetSeconds) {
    if (series.empty()) {
        return;
    }
    std::map<int64_t, const HourlyAirQuality*> byHour;
    for (const HourlyAirQuality& entry : series) {
        byHour[entry.startMs] = &entry;
    }

    const auto attach = [&byHour](std::vector<Hourly>& hours) {
        for (Hourly& hour : hours) {
            auto it = byHour.find(hour.date);
            if (it != byHour.end()) {
                hour.airQualityIndex = it->second->index;
                hour.airQuality = it->second->concentrations;
            }
        }
    };
    attach(weather.hourlyForecast);
    attach(weather.nextHourlyForecast);

    /*
     * The day is the MEAN of its hourly concentrations, indexed afterwards — not the worst hour,
     * which is what this used to compute and which read high on any day with one bad hour.
     */
    std::map<int64_t, std::vector<const AirQuality*>> byDay;
    for (const auto& entry : byHour) {
        byDay[LocalStartOfDay(entry.first, utcOffsetSeconds)].push_back(
            &entry.second->concentrations);
    }
    for (Daily& day : weather.dailyForecast) {
        auto it = byDay.find(day.date);
        if (it == byDay.end() || it->second.size() < kDailyAirQualityMinHours) {
            continue;
        }
        std::vector<double> pm25;
        std::vector<double> pm10;
        std::vector<double> o3;
        std::vector<double> no2;
        std::vector<double> so2;
        std::vector<double> co;
        for (const AirQuality* air : it->second) {
            if (air->pm25.has_value()) {
                pm25.push_back(*air->pm25);
            }
            if (air->pm10.has_value()) {
                pm10.push_back(*air->pm10);
            }
            if (air->o3.has_value()) {
                o3.push_back(*air->o3);
            }
            if (air->no2.has_value()) {
                no2.push_back(*air->no2);
            }
            if (air->so2.has_value()) {
                so2.push_back(*air->so2);
            }
            if (air->co.has_value()) {
                co.push_back(*air->co);
            }
        }
        AirQuality average;
        average.pm25 = MeanOf(pm25);
        average.pm10 = MeanOf(pm10);
        average.o3 = MeanOf(o3);
        average.no2 = MeanOf(no2);
        average.so2 = MeanOf(so2);
        average.co = MeanOf(co);
        day.airQuality = average;
        day.airQualityIndex = OverallIndexOf(average);
    }
}

}  // namespace breezy
