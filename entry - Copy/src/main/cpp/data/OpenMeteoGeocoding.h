/*
 * The Open-Meteo geocoding response — what the add-location search draws its rows from.
 *
 * ArkUIApp's counterpart is data/openmeteo/OpenMeteoApi.fetchLocationSearch plus
 * view/location/SearchResultModel.ets; both are ports of OpenMeteoService.requestLocationSearch
 * (kt:634-655) and Location.kt's getPlace / administrationLevels.
 *
 * The shell fetches the body and hands it over unread, exactly as it does with the forecast: the
 * two ports can then disagree about pixels but never about what the response said.
 */
#ifndef BREEZY_DATA_OPEN_METEO_GEOCODING_H
#define BREEZY_DATA_OPEN_METEO_GEOCODING_H

#include <string>
#include <vector>

namespace breezy {

/** One hit, already reduced to the two lines a row draws plus what saving it needs. */
struct GeocodingResult {
    /** getPlace (Location.kt:29-43) — the headline. */
    std::string place;
    /** administrationLevels (Location.kt:229-259) — the supporting line. */
    std::string levels;
    /** admin1, which is what the FMI alert matching reads. */
    std::string region;
    /** The two fields the source catalogue's compatibility rules key off. */
    std::string countryCode;
    std::string admin2;
    double latitude = 0.0;
    double longitude = 0.0;
};

struct GeocodingResponse {
    /**
     * False for what the Kotlin raises LocationSearchException for.
     *
     * A missing `results` array does NOT mean "no matches" (:643-648): `generationtime_ms` present
     * and positive means the search genuinely found nothing, absent means the request failed.
     * Collapsing the two would show "can't find X" for what is really a broken response.
     */
    bool ok = false;
    std::vector<GeocodingResult> results;
};

GeocodingResponse ParseGeocoding(const std::string& body);

}  // namespace breezy

#endif  // BREEZY_DATA_OPEN_METEO_GEOCODING_H
