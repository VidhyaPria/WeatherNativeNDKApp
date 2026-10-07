#include "data/Pollutant.h"

#include <algorithm>
#include <cmath>

#include "render/Theme.h"

namespace breezy {
namespace {

/** strings.xml:167-182, indexed by the level from aqiThresholds. */
const char* const kAqiLevelNames[] = {"Excellent", "Fair",           "Poor",
                                      "Unhealthy", "Very unhealthy", "Dangerous"};

/** `air_quality_level_N_description` — the Health information card's text. */
const char* const kAqiLevelDescriptions[] = {
    "The air quality is ideal for most individuals; enjoy your normal outdoor activities.",
    "The air quality is generally acceptable for most individuals. However, sensitive groups may "
    "experience minor to moderate symptoms from long-term exposure.",
    "The air has reached a high level of pollution and is unhealthy for sensitive groups. Reduce "
    "time spent outside if you are feeling symptoms such as difficulty breathing or throat "
    "irritation.",
    "Health effects can be immediately felt by sensitive groups. Healthy individuals may "
    "experience difficulty breathing and throat irritation with prolonged exposure. Limit outdoor "
    "activity.",
    "Health effects will be immediately felt by sensitive groups and should avoid outdoor "
    "activity. Healthy individuals are likely to experience difficulty breathing and throat "
    "irritation; consider staying indoors and rescheduling outdoor activities.",
    "Any exposure to the air, even for a few minutes, can lead to serious health effects on "
    "everybody. Avoid outdoor activities."};

/** `air_quality_level_N_harmless_exposure` — the scale table's middle column. */
const char* const kAqiHarmlessExposures[] = {"> 1 year", "< 1 year",        "< 1 day",
                                             "< 1 hour", "< a few minutes", "< 1 minute"};

/** strings.xml:275 — the two particulate pollutants share one sources paragraph. */
constexpr const char* kPmSources =
    "Main sources are combustion processes (e.g. power plants, indoor heating, car exhausts, "
    "wildfires), mechanical processes (e.g. construction, mineral dust, agriculture) and "
    "biological particles (e.g. pollen, mold, bacteria, viruses).";

/** `validPollutants` order (AirQuality.kt:23-30). */
const char* const kValidPollutantOrder[] = {"no2", "o3", "pm10", "pm25", "so2", "co"};

/** kt:191-199 — linear interpolation between two band edges. */
double Interpolate(double value, int bandLow, int bandHigh, int indexLow, int indexHigh) {
    return std::round((static_cast<double>(indexHigh) - indexLow) /
                          (static_cast<double>(bandHigh) - bandLow) * (value - bandLow) +
                      indexLow);
}

}  // namespace

const std::vector<int> kAqiBands = {0, 20, 50, 100, 150, 250};

const std::vector<PollutantDef>& Pollutants() {
    static const std::vector<PollutantDef> table = {
        {"pm25",
         {0, 5, 15, 30, 60, 150},
         60,
         "PM2.5",
         "Fine particulate matter (< 2.5 µm)",
         "About fine particulate matter (PM2.5)",
         "Air quality index scale for PM2.5",
         kPmSources,
         false},
        {"pm10",
         {0, 15, 45, 80, 160, 400},
         160,
         "PM10",
         "Inhalable particulate matter (< 10 µm)",
         "About inhalable particulate matter (PM10)",
         "Air quality index scale for PM10",
         kPmSources,
         false},
        {"o3",
         {0, 50, 100, 160, 240, 480},
         240,
         "O3",
         "Ground-level ozone (O3)",
         "About ground-level ozone (O3)",
         "Air quality index scale for O3",
         "Ozone is created in a chemical reaction between atmospheric oxygen, nitrogen oxides, "
         "carbon monoxide and organic compounds, in the presence of sunlight.",
         false},
        {"no2",
         {0, 10, 25, 200, 400, 1000},
         200,
         "NO2",
         "Nitrogen dioxide (NO2)",
         "About nitrogen dioxide (NO2)",
         "Air quality index scale for NO2",
         "Main sources are fuel burning processes, such as those used in industry and "
         "transportation.",
         false},
        {"so2",
         {0, 20, 40, 270, 500, 960},
         270,
         "SO2",
         "Sulfur dioxide (SO2)",
         "About sulfur dioxide (SO2)",
         "Air quality index scale for SO2",
         "Main sources are burning processes of sulfur-containing fuel in industry, transportation "
         "and power plants.",
         false},
        {"co",
         {0, 2, 4, 35, 100, 230},
         35,
         "CO",
         "Carbon monoxide (CO)",
         "About carbon monoxide (CO)",
         "Air quality index scale for CO",
         "Typically originates from incomplete combustion of carbon fuels, such as that which "
         "occurs in car engines and power plants.",
         true}};
    return table;
}

const PollutantDef* PollutantById(const std::string& id) {
    if (id.empty()) {
        return nullptr;
    }
    for (const PollutantDef& def : Pollutants()) {
        if (id == def.id) {
            return &def;
        }
    }
    return nullptr;
}

Maybe ConcentrationOf(const AirQuality& air, const std::string& id) {
    if (id == "pm25") {
        return air.pm25;
    }
    if (id == "pm10") {
        return air.pm10;
    }
    if (id == "o3") {
        return air.o3;
    }
    if (id == "no2") {
        return air.no2;
    }
    if (id == "so2") {
        return air.so2;
    }
    if (id == "co") {
        return air.co;
    }
    return Maybe{};
}

int LevelOf(double value, const std::vector<int>& thresholds) {
    int level = -1;
    for (size_t i = 0; i < thresholds.size(); ++i) {
        if (value >= thresholds[i]) {
            level = static_cast<int>(i);
        }
    }
    return level;
}

Maybe PollutantIndexOf(const PollutantDef& def, Maybe concentration) {
    if (!concentration.has_value()) {
        return Maybe{};
    }
    const double value = *concentration;
    const int level = LevelOf(value, def.thresholds);
    if (level < 0) {
        return Maybe{};
    }
    if (level < static_cast<int>(def.thresholds.size()) - 1) {
        return Interpolate(value, def.thresholds[static_cast<size_t>(level)],
                           def.thresholds[static_cast<size_t>(level) + 1],
                           kAqiBands[static_cast<size_t>(level)],
                           kAqiBands[static_cast<size_t>(level) + 1]);
    }
    // kt:212 — above the last band the index keeps rising linearly.
    return std::round(value * kAqiBands.back() / def.thresholds.back());
}

Maybe OverallIndexOf(const AirQuality& air) {
    Maybe worst;
    for (const char* id : {"o3", "no2", "pm10", "pm25"}) {
        const PollutantDef* def = PollutantById(id);
        if (def == nullptr) {
            continue;
        }
        const Maybe value = PollutantIndexOf(*def, ConcentrationOf(air, id));
        if (value.has_value() && (!worst.has_value() || *value > *worst)) {
            worst = value;
        }
    }
    return worst;
}

std::vector<const PollutantDef*> ValidPollutants(const AirQuality& air) {
    std::vector<const PollutantDef*> out;
    for (const char* id : kValidPollutantOrder) {
        const PollutantDef* def = PollutantById(id);
        if (def != nullptr && ConcentrationOf(air, id).has_value()) {
            out.push_back(def);
        }
    }
    return out;
}

std::string AqiLevelName(double aqi) {
    const int level = LevelOf(aqi, kAqiBands);
    return level >= 0 && level < 6 ? kAqiLevelNames[level] : "";
}

std::string AqiLevelDescription(double aqi) {
    const int level = LevelOf(aqi, kAqiBands);
    return level >= 0 && level < 6 ? kAqiLevelDescriptions[level] : "";
}

std::string AqiHarmlessExposureAt(int level) {
    return level >= 0 && level < 6 ? kAqiHarmlessExposures[level] : "";
}

std::string ConcentrationUnitLabel(const PollutantDef& def) {
    return def.milligram ? "mg/m³" : "µg/m³";
}

std::string FormatConcentration(const PollutantDef& def, double value) {
    return std::to_string(static_cast<long long>(std::llround(value))) + " " +
           ConcentrationUnitLabel(def);
}

}  // namespace breezy
