/*
 * Air quality, from the Open-Meteo air-quality endpoint.
 *
 * Ports PollutantIndex (domain/weather/index/PollutantIndex.kt:31-253) and AirQuality.getIndex
 * (AirQuality.kt:36-48) via ArkUIApp's data/openmeteo/AirQualityMapper.ets.
 *
 * The overall AQI is the worst of the O3, NO2, PM10 and PM2.5 sub-indices, each a piecewise-linear
 * interpolation of the pollutant's concentration bands onto the shared thresholds
 * [0, 20, 50, 100, 150, 250] — the Plume 2023 scale, not the US EPA one.
 */
#ifndef BREEZY_DATA_AIRQUALITY_H
#define BREEZY_DATA_AIRQUALITY_H

#include <cstdint>
#include <string>
#include <vector>

#include "model/Weather.h"

namespace breezy {

/** One hour's air quality, keyed by the instant the hour starts. */
struct HourlyAirQuality {
    int64_t startMs = 0;
    Maybe index;
    AirQuality concentrations;
};

/**
 * Parses the air-quality response into an index per hour. Returns an empty series when the body
 * is unusable — a failed air-quality leg must not cost the forecast (OpenMeteoService.kt:327-330).
 */
std::vector<HourlyAirQuality> ParseAirQualitySeries(const std::string& body);

/* `AqiLevelName` now lives in data/Pollutant.h, beside the scale it reads. */

/**
 * Folds the series into the weather: the concentrations and index per hourly entry, and the MEAN
 * of each day's hourly concentrations, which is what `getDailyAirQualityFromHourlyList`
 * (CommonConverter.kt:520-542) computes and DailyAirQualityAdapter then bars.
 */
void ApplyAirQualitySeries(Weather& weather, const std::vector<HourlyAirQuality>& series,
                           int utcOffsetSeconds);

/**
 * The current index and the allergens in season, for the two tiles.
 *
 * Pollen.pollensWithConcentration (Pollen.kt:34-38) keeps only allergens above zero, which is why
 * the Android card lists just the two that are actually in season.
 */
void ApplyCurrentAirQuality(const std::string& body, Current& current, int64_t nowMs);

}  // namespace breezy

#endif  // BREEZY_DATA_AIRQUALITY_H
