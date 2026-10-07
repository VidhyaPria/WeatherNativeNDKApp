#include "data/OpenMeteoGeocoding.h"

#include <cstdio>

#include "json/Json.h"

namespace breezy {
namespace {

/**
 * getPlace (Location.kt:29-43).
 *
 * The full rule is customName → cityAndDistrict → "Current location" → "lat, lon". A geocoding hit
 * has no custom name and is not a current position, and convertLocation (OpenMeteoService.kt:657-673)
 * sets no district, so it reduces to the city with the coordinates as the nameless-hit fallback.
 */
std::string PlaceOf(const std::string& city, double latitude, double longitude) {
    if (!city.empty()) {
        return city;
    }
    char buffer[64] = {};
    std::snprintf(buffer, sizeof(buffer), "%g, %g", latitude, longitude);
    return std::string(buffer);
}

void Append(std::string& text, const std::string& part) {
    if (part.empty()) {
        return;
    }
    if (!text.empty()) {
        text += ", ";
    }
    text += part;
}

}  // namespace

GeocodingResponse ParseGeocoding(const std::string& body) {
    GeocodingResponse response;
    const json::Value root = json::Parse(body);
    const json::Value& results = root["results"];

    if (results.GetType() != json::Value::Type::Array) {
        // See GeocodingResponse::ok — only a positive generation time means "found nothing".
        response.ok = root["generationtime_ms"].AsDouble(0.0) > 0.0;
        return response;
    }
    response.ok = true;

    for (size_t i = 0; i < results.Size(); ++i) {
        const json::Value& hit = results[i];
        // kt:651 drops every hit with no country code BEFORE conversion, because convertLocation
        // force-unwraps that field.
        const std::string countryCode = hit["country_code"].AsString("");
        if (countryCode.empty()) {
            continue;
        }
        const std::string city = hit["name"].AsString("");
        const std::string admin1 = hit["admin1"].AsString("");
        const std::string admin2 = hit["admin2"].AsString("");
        const std::string admin3 = hit["admin3"].AsString("");
        const std::string admin4 = hit["admin4"].AsString("");

        GeocodingResult result;
        result.latitude = hit["latitude"].AsDouble(0.0);
        result.longitude = hit["longitude"].AsDouble(0.0);
        result.place = PlaceOf(city, result.latitude, result.longitude);
        result.region = admin1;
        result.countryCode = countryCode;
        result.admin2 = admin2;

        /*
         * administrationLevels (Location.kt:229-259) — country, then admin1..4, joined with ", ".
         *
         * admin3 and admin4 carry an extra condition each: admin4 is printed only when it differs
         * from the city, admin3 only when there IS an admin4 or when admin3 itself differs from the
         * city. Both exist to stop the row repeating the name already in the headline.
         */
        Append(result.levels, hit["country"].AsString(""));
        Append(result.levels, admin1);
        Append(result.levels, admin2);
        if (!admin3.empty() && (!admin4.empty() || admin3 != city)) {
            Append(result.levels, admin3);
        }
        if (!admin4.empty() && admin4 != city) {
            Append(result.levels, admin4);
        }
        response.results.push_back(std::move(result));
    }
    return response;
}

}  // namespace breezy
