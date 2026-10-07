/*
 * PollutantIndex — domain/weather/index/PollutantIndex.kt:31-253.
 *
 * The six pollutants the app knows, each with its own concentration bands, and the shared Plume
 * 2023 AQI scale those bands map onto. Everything that needs a pollutant — the mapper, the air
 * quality detail screen's picker, chart, sections and scale table — reads this one table, so the
 * screen and the data can never disagree about a threshold. ArkUIApp's model/Pollutant.ets is the
 * same table.
 *
 * Concentrations are in µg/m³ for every pollutant EXCEPT carbon monoxide, which Android reads in
 * mg/m³ (AirQuality.kt getConcentration) because that is the unit its thresholds are written in.
 */

#ifndef BREEZY_DATA_POLLUTANT_H
#define BREEZY_DATA_POLLUTANT_H

#include <cstdint>
#include <string>
#include <vector>

#include "model/Weather.h"

namespace breezy {

/** PollutantIndex.kt:133 — Plume 2023. */
extern const std::vector<int> kAqiBands;

/** One entry of the PollutantIndex enum. */
struct PollutantDef {
    /** `id`, and the value the detail screen's picker carries. "" is the AQI itself. */
    const char* id;
    /** The concentration bands, in this pollutant's own unit. */
    std::vector<int> thresholds;
    /** `maxY` — the chart's floor for its own axis maximum. */
    int maxY;
    /** `shortName`, e.g. "PM2.5". */
    const char* shortName;
    /** `fullName` with its placeholder filled. */
    const char* fullName;
    /** `aboutPollutant`, the section heading when this pollutant is picked. */
    const char* aboutHeading;
    /** `aboutIndex`, the scale card's heading when this pollutant is picked. */
    const char* scaleHeading;
    /** `sources`, the card under the About heading. */
    const char* sources;
    /** CO alone is read and shown in mg/m³. */
    bool milligram;
};

/** The enum's declaration order (kt:43-128). */
const std::vector<PollutantDef>& Pollutants();

/** Null when `id` is empty or unknown, which means the AQI itself. */
const PollutantDef* PollutantById(const std::string& id);

/** `AirQuality.getConcentration` (AirQuality.kt:50-57). */
Maybe ConcentrationOf(const AirQuality& air, const std::string& id);

/** `thresholds.indexOfLast { value >= it }` (kt:236). */
int LevelOf(double value, const std::vector<int>& thresholds);

/** `PollutantIndex.getIndex(cp)` (kt:201-214, 234-238) — one pollutant's sub-index. */
Maybe PollutantIndexOf(const PollutantDef& def, Maybe concentration);

/** `AirQuality.getIndex(null)` (AirQuality.kt:32-48) — the worst of O3, NO2, PM10 and PM2.5. */
Maybe OverallIndexOf(const AirQuality& air);

/** `validPollutants` — the ones this reading carries, in the Kotlin's order (AirQuality.kt:23-30). */
std::vector<const PollutantDef*> ValidPollutants(const AirQuality& air);

/** `getAqiToName` (kt:160-164). */
std::string AqiLevelName(double aqi);
/** `getAqiToDescription` (kt:166-170) — the Health information card. */
std::string AqiLevelDescription(double aqi);
/** `getAqiToHarmlessExposure` (kt:172-180) — the scale table's middle column, by LEVEL. */
std::string AqiHarmlessExposureAt(int level);

/** `PollutantConcentrationUnit`'s short label — "µg/m³", or "mg/m³" for carbon monoxide. */
std::string ConcentrationUnitLabel(const PollutantDef& def);
/** `formatMeasure(SHORT)` — no decimals in either unit, then a space and the unit. */
std::string FormatConcentration(const PollutantDef& def, double value);

}  // namespace breezy

#endif  // BREEZY_DATA_POLLUTANT_H
