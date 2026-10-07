/*
 * Open-Meteo forecast response → the weather model.
 *
 * Ported from ArkUIApp's data/openmeteo/OpenMeteoMapper.ets and data/WeatherRepository.ets, which
 * follow OpenMeteoService.kt / CommonConverter.kt.
 *
 * The ArkTS shell hands over the response body verbatim — it does no interpreting of its own, so
 * the two ports cannot drift apart in how they read the same bytes.
 */
#ifndef BREEZY_DATA_OPENMETEOMAPPER_H
#define BREEZY_DATA_OPENMETEOMAPPER_H

#include <cstdint>
#include <string>

#include "model/Weather.h"

namespace breezy {

struct ForecastResult {
    /** False when the body was unparseable or carried Open-Meteo's `error: true`. */
    bool ok = false;
    /** utc_offset_seconds for the resolved grid cell — every local time is read through this. */
    int utcOffsetSeconds = 0;
    /**
     * Whether the sun is currently up, from the computed sun times rather than a field — Open-Meteo
     * returns no sunrise/sunset, so both ports calculate it (WeatherRepository.ets isDaylightNow).
     */
    bool daylight = true;
    Weather weather;
};

ForecastResult MapForecast(const std::string& body, double latitude, double longitude,
                           int64_t nowMs);

/** OpenMeteoService.kt:568-607 — the description printed under the big temperature. */
std::string WeatherTextForCode(int code);

}  // namespace breezy

#endif  // BREEZY_DATA_OPENMETEOMAPPER_H
