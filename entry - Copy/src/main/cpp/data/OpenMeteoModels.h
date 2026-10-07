/*
 * Open-Meteo's weather models — OpenMeteoWeatherModel.kt:30-123.
 *
 * The C++ half of ArkUIApp's model/OpenMeteoModels.ets; the two carry the same 45 entries in the
 * same order, because Android pairs its enum with a parallel string-array by INDEX
 * (arrays.xml:280-326) and a divergence in either port would rename models silently.
 *
 * Transcribed rather than fetched: Android hard-codes the list too ("Up-to-date as of 2025-10-10",
 * kt:28) — Open-Meteo publishes no models endpoint.
 */
#ifndef BREEZY_DATA_OPEN_METEO_MODELS_H
#define BREEZY_DATA_OPEN_METEO_MODELS_H

#include <string>
#include <vector>

namespace breezy {

struct WeatherModelInfo {
    /** The `id` — what `&models=` carries. */
    std::string id;
    /** `open_meteo_weather_models[i]`. */
    std::string name;
    /**
     * `getDescription` (kt:116-122): only `best_match` and the `_seamless` models explain
     * themselves. Empty for the rest.
     */
    std::string description;
};

/** The id Android falls back to when a location has no stored selection (kt:772). */
extern const char* const kModelBestMatch;

/** The catalogue, in the enum's declaration order — which is the order the dialog lists. */
const std::vector<WeatherModelInfo>& WeatherModels();

/** `OpenMeteoWeatherModel.getInstance` (kt:95-99) — nullptr for an id this build does not know. */
const WeatherModelInfo* FindWeatherModel(const std::string& id);

/**
 * `getWeatherModels` (kt:764-773) — the stored comma list, dropping unknown ids and falling back
 * to `best_match` alone when nothing survives.
 */
std::vector<std::string> SelectedWeatherModels(const std::string& stored);

/** The stored list as the request wants it: comma-joined, never empty. */
std::string WeatherModelsParam(const std::string& stored);

/**
 * The row's summary — the enabled models' names, collated, joined by `locale_separator`.
 *
 * kt:805-815 sorts with a `Collator` "because there are now a lot of sources".
 */
std::string WeatherModelsSummary(const std::string& stored);

}  // namespace breezy

#endif  // BREEZY_DATA_OPEN_METEO_MODELS_H
