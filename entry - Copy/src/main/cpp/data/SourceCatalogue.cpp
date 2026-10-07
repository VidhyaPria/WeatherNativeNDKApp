#include "data/SourceCatalogue.h"

#include <algorithm>

namespace breezy {

const char* const kDefaultForecastSource = "openmeteo";
const char* const kDefaultLocationSearchSource = "openmeteo";
const char* const kGroupRecommended = "Recommended";

namespace {

/* ------------------------------------------------------------------ bounding boxes ------------ */

/**
 * `com.google.maps.android.model.LatLngBounds`, reduced to the one operation the stubs use.
 *
 * Android has two spellings — the constructor takes (southwest, northeast) as LatLng pairs, while
 * `LatLngBounds.parse` takes (west, south, east, north). Both are normalised to this one shape, and
 * each constant is commented with the spelling it came from.
 */
struct Bbox {
    double west;
    double south;
    double east;
    double north;

    bool Contains(double latitude, double longitude) const {
        return latitude >= south && latitude <= north && longitude >= west && longitude <= east;
    }
};

/** CAMS' European pollen domain — shared by Open-Meteo and AccuWeather (OpenMeteoService.kt). */
constexpr Bbox kCopernicusPollen{-25.0, 30.0, 45.0, 72.0};
/** AccuServiceStub — LatLngBounds(LatLng(23.542547919, -125.734607238), LatLng(50.369494121, -65.977324999)). */
constexpr Bbox kContiguousUsStates{-125.734607238, 23.542547919, -65.977324999, 50.369494121};
/** FmiServiceStub:82-85 — LatLngBounds(LatLng(53.9, 4.88877), LatLng(72.269067, 32.931465)). */
constexpr Bbox kFmiForecast{4.888770, 53.900000, 32.931465, 72.269067};
/** FmiServiceStub:86-89 — the SILAM air-quality domain. */
constexpr Bbox kFmiSilam{-24.950000, 30.050000, 45.049999, 72.049999};
/** NlscServiceStub — parse(west, south, east, north). Shared by CWA, NCDR and NLSC. */
constexpr Bbox kTaiwan{119.99690416, 21.756143532, 122.10915909, 25.633378776};
constexpr Bbox kPenghu{119.314301816, 23.186561404, 119.726986388, 23.810692086};
constexpr Bbox kKinmen{118.137979837, 24.160255444, 118.505977425, 24.534228163};
constexpr Bbox kWuqiu{119.443195363, 24.97760013, 119.479213453, 24.999614154};
constexpr Bbox kMatsu{119.908905081, 25.940995457, 120.511750672, 26.385275262};
/** GeoSphereAtServiceStub — parse(west, south, east, north). */
constexpr Bbox kGeoSphereHourly{5.49, 42.98, 22.1, 51.82};
constexpr Bbox kGeoSphereAirQuality9km{-53.73, 15.59, 80.33, 74.39};
constexpr Bbox kGeoSphereNowcast{8.1, 45.5, 17.74, 49.48};

/* ------------------------------------------------------------------ the predicates ------------ */

char UpperAscii(char value) {
    return (value >= 'a' && value <= 'z') ? static_cast<char>(value - 'a' + 'A') : value;
}

/** `location.countryCode.equals(code, ignoreCase = true)`. Country codes are ASCII. */
bool IsCountry(const SourceLocation& location, const char* code) {
    const std::string& country = location.countryCode;
    size_t i = 0;
    for (; i < country.size() && code[i] != '\0'; ++i) {
        if (UpperAscii(country[i]) != UpperAscii(code[i])) {
            return false;
        }
    }
    return i == country.size() && code[i] == '\0';
}

/** `arrayOf(...).any { location.countryCode.equals(it, ignoreCase = true) }`. */
bool IsCountryIn(const SourceLocation& location, const std::vector<std::string>& codes) {
    for (const std::string& code : codes) {
        if (IsCountry(location, code.c_str())) {
            return true;
        }
    }
    return false;
}

/** The overwhelmingly common stub body: supported exactly in these countries, for every feature. */
SupportFn CountriesOnly(std::vector<std::string> codes) {
    return [codes](const SourceLocation& location, SourceFeature) {
        return IsCountryIn(location, codes);
    };
}

/**
 * The equally common priority body:
 *   `isFeatureSupportedForLocation(location, feature) -> PRIORITY_HIGHEST else PRIORITY_NONE`.
 * Passed the source's own support function so the two stay in step, as the Kotlin's does.
 */
PriorityFn HighestWhenSupported(SupportFn support) {
    return [support](const SourceLocation& location, SourceFeature feature) {
        return support(location, feature) ? kPriorityHighest : kPriorityNone;
    };
}

/** A source that is recommended in these countries whatever its support rule says. */
PriorityFn HighestInCountries(std::vector<std::string> codes) {
    return [codes](const SourceLocation& location, SourceFeature) {
        return IsCountryIn(location, codes) ? kPriorityHighest : kPriorityNone;
    };
}

bool InTaiwan(const SourceLocation& location) {
    return IsCountry(location, "TW") ||
           kTaiwan.Contains(location.latitude, location.longitude) ||
           kPenghu.Contains(location.latitude, location.longitude) ||
           kKinmen.Contains(location.latitude, location.longitude) ||
           kWuqiu.Contains(location.latitude, location.longitude) ||
           kMatsu.Contains(location.latitude, location.longitude);
}

/* ------------------------------------------------------- per-source support predicates -------- */

/** AccuServiceStub — `portal` is the free portal on a stock build. */
bool AccuSupport(const SourceLocation& location, SourceFeature feature) {
    if (feature == SourceFeature::Pollen) {
        return kCopernicusPollen.Contains(location.latitude, location.longitude) ||
               (IsCountry(location, "US") &&
                kContiguousUsStates.Contains(location.latitude, location.longitude)) ||
               IsCountry(location, "CA");
    }
    // portal == ENTERPRISE is false on a stock build, so the rest are the four listed.
    return feature == SourceFeature::Forecast || feature == SourceFeature::Current ||
           feature == SourceFeature::Alert || feature == SourceFeature::ReverseGeocoding;
}

/** OpenMeteoService — worldwide except pollen, which is CAMS Europe. */
bool OpenMeteoSupport(const SourceLocation& location, SourceFeature feature) {
    if (feature == SourceFeature::Pollen) {
        return kCopernicusPollen.Contains(location.latitude, location.longitude);
    }
    return true;
}

/** FmiServiceStub:66-72 — two of the five features are bbox-gated, the rest are FI/AX. */
bool FmiSupport(const SourceLocation& location, SourceFeature feature) {
    if (feature == SourceFeature::Forecast) {
        return kFmiForecast.Contains(location.latitude, location.longitude);
    }
    if (feature == SourceFeature::AirQuality) {
        return kFmiSilam.Contains(location.latitude, location.longitude);
    }
    return IsCountryIn(location, {"FI", "AX"});
}

/** MetNoServiceStub — worldwide forecast, everything else Nordic-gated. */
bool MetNoSupport(const SourceLocation& location, SourceFeature feature) {
    switch (feature) {
        case SourceFeature::Current:
        case SourceFeature::Minutely:
            return IsCountryIn(location, {"NO", "SE", "FI", "DK"});
        case SourceFeature::AirQuality:
            return IsCountry(location, "NO");
        case SourceFeature::Alert:
            return IsCountryIn(location, {"NO", "SJ"});
        case SourceFeature::Forecast:
            return true;
        default:
            return false;
    }
}

int MetNoPriority(const SourceLocation& location, SourceFeature feature) {
    if (IsCountry(location, "NO")) {
        return kPriorityHighest;
    }
    if (IsCountryIn(location, {"SJ", "SE", "FI", "DK"}) && MetNoSupport(location, feature)) {
        return kPriorityHigh;
    }
    return kPriorityNone;
}

/** MfServiceStub — forecast and reverse geocoding worldwide, the rest by territory. */
bool MfSupport(const SourceLocation& location, SourceFeature feature) {
    switch (feature) {
        case SourceFeature::Current:
        case SourceFeature::Minutely:
            return IsCountry(location, "FR");
        case SourceFeature::Alert:
            return IsCountryIn(location, {"FR", "AD", "BL", "GF", "GP", "MF", "MQ", "NC", "PF",
                                          "PM", "RE", "WF", "YT"});
        case SourceFeature::Normals:
            return IsCountryIn(location, {"FR", "AD", "MC", "BL", "GF", "GP", "MF", "MQ", "NC",
                                          "PF", "PM", "RE", "WF", "YT"});
        case SourceFeature::Forecast:
        case SourceFeature::ReverseGeocoding:
            return true;  // Main source available worldwide
        default:
            return false;
    }
}

/** kt:903-919 — forecast and reverse geocoding borrow NORMALS' criteria, being worldwide. */
int MfPriority(const SourceLocation& location, SourceFeature feature) {
    const SourceFeature effective =
        (feature == SourceFeature::Forecast || feature == SourceFeature::ReverseGeocoding)
            ? SourceFeature::Normals
            : feature;
    return MfSupport(location, effective) ? kPriorityHighest : kPriorityNone;
}

/** DmiServiceStub — everything worldwide EXCEPT alerts, which are DK/FO/GL. */
bool DmiSupport(const SourceLocation& location, SourceFeature feature) {
    return feature != SourceFeature::Alert || IsCountryIn(location, {"DK", "FO", "GL"});
}

/** kt:247-256 — "Always use the same criterias as alert". */
int DmiPriority(const SourceLocation& location, SourceFeature) {
    return IsCountryIn(location, {"DK", "FO", "GL"}) ? kPriorityHighest : kPriorityNone;
}

/** GeoSphereAtServiceStub — three bboxes and one country test. */
bool GeoSphereSupport(const SourceLocation& location, SourceFeature feature) {
    switch (feature) {
        case SourceFeature::Forecast:
            return kGeoSphereHourly.Contains(location.latitude, location.longitude);
        case SourceFeature::AirQuality:
            return kGeoSphereAirQuality9km.Contains(location.latitude, location.longitude);
        case SourceFeature::Minutely:
            return kGeoSphereNowcast.Contains(location.latitude, location.longitude);
        case SourceFeature::Alert:
            return IsCountry(location, "AT");
        default:
            return false;
    }
}

int GeoSpherePriority(const SourceLocation& location, SourceFeature feature) {
    if (IsCountry(location, "AT") && feature != SourceFeature::Forecast) {
        return kPriorityHighest;
    }
    if (feature == SourceFeature::Minutely && GeoSphereSupport(location, feature)) {
        return kPriorityHigh;
    }
    return kPriorityNone;
}

/** CwaServiceStub:213-225 — alerts drop to HIGH deliberately, so NCDR is used in priority. */
int CwaPriority(const SourceLocation& location, SourceFeature feature) {
    if (!InTaiwan(location)) {
        return kPriorityNone;
    }
    return feature == SourceFeature::Alert ? kPriorityHigh : kPriorityHighest;
}

/** PollenInfoServiceStub — supported in 13 countries but recommended only in Austria. */
int PollenInfoPriority(const SourceLocation& location, SourceFeature feature) {
    return (feature == SourceFeature::Pollen && IsCountry(location, "AT")) ? kPriorityHigh
                                                                           : kPriorityNone;
}

/**
 * AtmoServiceStub — France AND inside the agency's own region.
 *
 * See deviation 2 in the header: the Kotlin also accepts admin1Code / admin2Code, which
 * Open-Meteo's geocoding does not return.
 */
SupportFn AtmoRegion(std::vector<std::string> admin1Names, std::vector<std::string> admin2Names) {
    return [admin1Names, admin2Names](const SourceLocation& location, SourceFeature feature) {
        if (feature != SourceFeature::AirQuality || !IsCountry(location, "FR")) {
            return false;
        }
        for (const std::string& name : admin1Names) {
            if (name == location.region) {
                return true;
            }
        }
        for (const std::string& name : admin2Names) {
            if (name == location.admin2) {
                return true;
            }
        }
        return false;
    };
}

/**
 * ClimWebService — one country each, and NORMALS additionally needs a `cityClimatePageId`, which
 * only six of the seventeen instances define.
 */
SupportFn ClimWeb(std::string countryCode, bool hasNormals) {
    return [countryCode, hasNormals](const SourceLocation& location, SourceFeature feature) {
        if (feature == SourceFeature::Alert) {
            return IsCountry(location, countryCode.c_str());
        }
        if (feature == SourceFeature::Normals) {
            return IsCountry(location, countryCode.c_str()) && hasNormals;
        }
        return false;
    };
}

/* ------------------------------------------------------------------- table builders ----------- */

SourceInfo Make(std::string id, std::string name, SourceContinent continent,
                std::vector<SourceFeature> features) {
    SourceInfo source;
    source.id = std::move(id);
    source.name = std::move(name);
    source.continent = continent;
    source.features = std::move(features);
    return source;
}

/** One regional Atmo agency — France plus its own departments, air quality only. */
SourceInfo AtmoSource(std::string id, std::string name, std::vector<std::string> admin1Names,
                      std::vector<std::string> admin2Names) {
    SourceInfo source = Make(std::move(id), std::move(name), SourceContinent::Europe,
                             {SourceFeature::AirQuality});
    source.support = AtmoRegion(std::move(admin1Names), std::move(admin2Names));
    source.priority = HighestWhenSupported(source.support);
    return source;
}

/** Every ClimWeb instance declares both features; support is what narrows them. */
SourceInfo ClimWebSource(std::string id, std::string name, std::string countryCode,
                         bool hasNormals) {
    SourceInfo source = Make(std::move(id), std::move(name), SourceContinent::Africa,
                             {SourceFeature::Alert, SourceFeature::Normals});
    source.support = ClimWeb(std::move(countryCode), hasNormals);
    source.priority = HighestWhenSupported(source.support);
    return source;
}

/** The plain national pattern: these countries, every declared feature, recommended when supported. */
SourceInfo NationalSource(std::string id, std::string name, SourceContinent continent,
                          std::vector<SourceFeature> features, std::vector<std::string> countries) {
    SourceInfo source = Make(std::move(id), std::move(name), continent, std::move(features));
    source.support = CountriesOnly(std::move(countries));
    source.priority = HighestWhenSupported(source.support);
    return source;
}

/* ------------------------------------------------------------------------ the table ----------- */

std::vector<SourceInfo> BuildSources() {
    using F = SourceFeature;
    using C = SourceContinent;
    std::vector<SourceInfo> list;

    /* ---- locationSourceList (:210-214). Not FeatureSources: no feature row ever lists them. ---- */
    list.push_back(Make("native", "Android", C::Worldwide, {}));
    list.push_back(Make("ipsb", "IP.SB", C::Worldwide, {}));
    list.push_back(Make("baidu_ip", "Baidu IP location (China)", C::Asia, {}));

    /* ---- locationSearchSourceList (:217-219) ---- */
    {
        SourceInfo geonames = Make("geonames", "GeoNames", C::Worldwide, {});
        geonames.searchAttribution = "GeoNames (CC BY 4.0)";
        list.push_back(std::move(geonames));
    }

    /* ---- reverseGeocodingSourceList (:222-226) ---- */
    // naturalearth is filtered out of every feature list by SourceManager.kt:451 itself.
    list.push_back(Make("naturalearth", "Natural Earth", C::Worldwide, {F::ReverseGeocoding}));
    {
        SourceInfo native = Make("nativegeocoder", "Android", C::Worldwide, {F::ReverseGeocoding});
        // `Geocoder.isPresent()` — there is no platform geocoder on HarmonyOS.
        native.support = [](const SourceLocation&, SourceFeature) { return false; };
        list.push_back(std::move(native));
    }
    {
        SourceInfo nominatim = Make("nominatim", "Nominatim", C::Worldwide, {F::ReverseGeocoding});
        nominatim.searchAttribution =
            "Nominatim \xE2\x80\xA2 Data \xC2\xA9 OpenStreetMap contributors, ODbL 1.0. "
            "https://osm.org/copyright";
        list.push_back(std::move(nominatim));
    }

    /* ---- worldwideWeatherSourceList (:230-240) ---- */
    {
        SourceInfo openMeteo = Make("openmeteo", "Open-Meteo", C::Worldwide,
                                    {F::Forecast, F::Current, F::AirQuality, F::Pollen, F::Minutely});
        openMeteo.support = &OpenMeteoSupport;
        openMeteo.searchAttribution = "Open-Meteo (CC BY 4.0) \xE2\x80\xA2 GeoNames";
        openMeteo.implemented = true;
        list.push_back(std::move(openMeteo));
    }
    {
        SourceInfo accu = Make("accu", "AccuWeather", C::Worldwide,
                               {F::Forecast, F::Current, F::AirQuality, F::Pollen, F::Minutely,
                                F::Alert, F::Normals, F::ReverseGeocoding});
        accu.support = &AccuSupport;
        accu.searchAttribution = "AccuWeather";
        // Configured on the reference build — its picker rows carry no "(not configured)" suffix.
        list.push_back(std::move(accu));
    }
    list.push_back(Make("fpas", "FOSS Public Alert Server (beta)", C::Worldwide, {F::Alert}));
    {
        SourceInfo infoplaza = Make("infoplaza", "Infoplaza", C::Worldwide,
                                    {F::Forecast, F::Current, F::Minutely, F::Normals});
        infoplaza.configured = false;
        list.push_back(std::move(infoplaza));
    }
    {
        // kt:798 "The only exception here" — a national agency filed as worldwide.
        SourceInfo metno = Make("metno", "MET Norway", C::Worldwide,
                                {F::Forecast, F::Current, F::Minutely, F::AirQuality, F::Alert});
        metno.support = &MetNoSupport;
        metno.priority = &MetNoPriority;
        list.push_back(std::move(metno));
    }
    {
        SourceInfo ncei = Make("ncei", "NCEI", C::Worldwide, {F::Normals});
        ncei.priority = HighestInCountries({"US", "PR", "VI", "MP", "GU"});
        list.push_back(std::move(ncei));
    }
    // Configured on the reference build, as its unsuffixed picker row shows.
    list.push_back(Make("openweather", "OpenWeather", C::Worldwide,
                        {F::Forecast, F::Current, F::AirQuality}));
    {
        SourceInfo pirate = Make("pirateweather", "PirateWeather", C::Worldwide,
                                 {F::Forecast, F::Current, F::Minutely, F::Alert});
        pirate.configured = false;
        list.push_back(std::move(pirate));
    }
    list.push_back(Make("wmosevereweather", "WMO Severe Weather Information Centre", C::Worldwide,
                        {F::Alert}));

    /*
     * ---- nationalWeatherSourceList (:243-303), ALREADY SORTED ----
     *
     * kt:324-327 sorts this block at runtime with a Collator on the DISPLAY NAME, and the display
     * name often has nothing to do with the id — which is why "Icelandic Met Office" (id veduris)
     * sits between HKO and IGEBU, and "Servizio Meteo AM" (id meteoam) after Recosanté. The order
     * here was produced by that comparator, not by eye; see the header.
     */
    list.push_back(NationalSource("aemet", "AEMET (Spain)", C::Europe,
                                  {F::Forecast, F::Current, F::Normals}, {"ES"}));
    list.push_back(ClimWebSource("anambf", "ANAM-BF (Burkina Faso)", "BF", false));
    list.push_back(ClimWebSource("anamet", "ANAMET (Togo)", "TG", false));
    list.push_back(AtmoSource("atmoaura", "Atmo Auvergne-Rh\xC3\xB4ne-Alpes (France)",
                              {"Auvergne-Rh\xC3\xB4ne-Alpes"},
                              {"Ain", "Allier", "Ard\xC3\xA8""che", "Cantal", "Dr\xC3\xB4me",
                               "Is\xC3\xA8re", "Loire", "Haute-Loire", "Puy-de-D\xC3\xB4me",
                               "Rh\xC3\xB4ne", "Savoie", "Haute-Savoie"}));
    {
        SourceInfo atmoFrance = Make("atmofrance", "Atmo France", C::Europe, {F::Pollen});
        atmoFrance.support = [](const SourceLocation& location, SourceFeature feature) {
            return feature == SourceFeature::Pollen && IsCountry(location, "FR");
        };
        atmoFrance.priority = HighestWhenSupported(atmoFrance.support);
        list.push_back(std::move(atmoFrance));
    }
    list.push_back(AtmoSource("atmograndest", "ATMO GrandEst (France)",
                              {"Grand Est", "Grand-Est"},
                              {"Ardennes", "Aube", "Marne", "Haute-Marne", "Meurthe-et-Moselle",
                               "Meuse", "Moselle", "Bas-Rhin", "Haut-Rhin", "Vosges"}));
    list.push_back(AtmoSource("atmohdf", "Atmo Hauts-de-France (France)",
                              {"Hauts-de-France", "Hauts de France"},
                              {"Aisne", "Nord", "North", "Oise", "Pas-de-Calais", "Pas de Calais",
                               "Somme"}));
    list.push_back(AtmoSource("atmosud", "AtmoSud (France)",
                              {"Provence-Alpes-C\xC3\xB4te d'Azur",
                               "Provence Alpes C\xC3\xB4te d'Azur"},
                              {"Alpes-de-Haute-Provence", "Hautes-Alpes", "Alpes-Maritimes",
                               "Bouches-du-Rh\xC3\xB4ne", "Var", "Vaucluse"}));
    list.push_back(NationalSource("bmd", "BMD (Bangladesh)", C::Asia,
                                  {F::Forecast, F::ReverseGeocoding}, {"BD"}));
    list.push_back(NationalSource("bmkg", "BMKG (Indonesia)", C::Asia,
                                  {F::Forecast, F::Current, F::AirQuality, F::Alert,
                                   F::ReverseGeocoding},
                                  {"ID"}));
    list.push_back(NationalSource("brightsky", "Bright Sky (DWD) (Germany)", C::Europe,
                                  {F::Forecast, F::Current, F::Alert}, {"DE"}));
    {
        // China is a LocationSearchSource as well as a weather source — the one national source
        // that is, which is why it closes the search screen's five-row chooser.
        SourceInfo china = Make("china", "China", C::Asia,
                                {F::Forecast, F::Current, F::AirQuality, F::Minutely, F::Alert,
                                 F::ReverseGeocoding});
        china.support = CountriesOnly({"CN"});
        china.priority = HighestWhenSupported(china.support);
        china.searchAttribution =
            "\xE5\x8C\x97\xE4\xBA\xAC\xE5\xA4\xA9\xE6\xB0\x94\xE3\x80\x81"
            "\xE5\xBD\xA9\xE4\xBA\x91\xE5\xA4\xA9\xE6\xB0\x94\xE3\x80\x81"
            "\xE4\xB8\xAD\xE5\x9B\xBD\xE7\x8E\xAF\xE5\xA2\x83\xE7\x9B\x91\xE6\xB5\x8B"
            "\xE6\x80\xBB\xE7\xAB\x99";
        list.push_back(std::move(china));
    }
    {
        // "CWA (${currentLocale.getCountryName("TW")})" — the reference device renders TW as
        // "Taiwan, China". Read off its Weather sources screen, not guessed.
        SourceInfo cwa = Make("cwa", "CWA (Taiwan, China)", C::Asia,
                              {F::Forecast, F::Current, F::AirQuality, F::Alert, F::Normals,
                               F::ReverseGeocoding});
        cwa.support = [](const SourceLocation& location, SourceFeature) {
            return InTaiwan(location);
        };
        cwa.priority = &CwaPriority;
        cwa.configured = false;
        list.push_back(std::move(cwa));
    }
    list.push_back(ClimWebSource("dccms", "DCCMS (Malawi)", "MW", true));
    {
        SourceInfo dmi = Make("dmi", "DMI (Denmark)", C::Europe,
                              {F::Forecast, F::Alert, F::ReverseGeocoding});
        dmi.support = &DmiSupport;
        dmi.priority = &DmiPriority;
        list.push_back(std::move(dmi));
    }
    list.push_back(ClimWebSource("dmnne", "DMN (Niger)", "NE", true));
    list.push_back(ClimWebSource("dwrgm", "DWR (Gambia)", "GM", false));
    list.push_back(NationalSource("eccc", "ECCC (Canada)", C::NorthAmerica,
                                  {F::Forecast, F::Current, F::Alert, F::Normals,
                                   F::ReverseGeocoding},
                                  {"CA"}));
    list.push_back(NationalSource("ekuk", "EKUK (Estonia)", C::Europe,
                                  {F::AirQuality, F::Pollen}, {"EE"}));
    list.push_back(ClimWebSource("ethiomet", "EMI (Ethiopia)", "ET", true));
    list.push_back(NationalSource("epdhk", "EPD (Hong Kong SAR China)", C::Asia,
                                  {F::AirQuality}, {"HK"}));
    {
        SourceInfo fmi = Make("fmi", "FMI (Finland)", C::Europe,
                              {F::Forecast, F::Current, F::AirQuality, F::Alert, F::Normals});
        fmi.support = &FmiSupport;
        fmi.priority = HighestInCountries({"FI", "AX"});
        list.push_back(std::move(fmi));
    }
    {
        SourceInfo geosphere = Make("geosphereat", "GeoSphere Austria", C::Europe,
                                    {F::Forecast, F::AirQuality, F::Minutely, F::Alert});
        geosphere.support = &GeoSphereSupport;
        geosphere.priority = &GeoSpherePriority;
        list.push_back(std::move(geosphere));
    }
    list.push_back(ClimWebSource("gmet", "GMet (Ghana)", "GH", false));
    list.push_back(NationalSource("hko", "HKO (Hong Kong SAR China)", C::Asia,
                                  {F::Forecast, F::Current, F::Alert, F::Normals,
                                   F::ReverseGeocoding},
                                  {"HK"}));
    list.push_back(NationalSource("veduris", "Icelandic Met Office", C::Europe,
                                  {F::Forecast, F::Current, F::Alert, F::ReverseGeocoding},
                                  {"IS"}));
    list.push_back(ClimWebSource("igebu", "IGEBU (Burundi)", "BI", false));
    list.push_back(NationalSource("ilmateenistus", "Ilmateenistus (Estonia)", C::Europe,
                                  {F::Forecast, F::ReverseGeocoding}, {"EE"}));
    list.push_back(NationalSource("imd", "IMD (India)", C::Asia, {F::Forecast}, {"IN"}));
    list.push_back(NationalSource("ims", "IMS (Israel)", C::Asia,
                                  {F::Forecast, F::Current, F::Alert, F::ReverseGeocoding},
                                  {"IL", "PS"}));
    list.push_back(ClimWebSource("inmgb", "INM-GB (Guinea-Bissau)", "GW", false));
    list.push_back(NationalSource("ipma", "IPMA (Portugal)", C::Europe,
                                  {F::Forecast, F::Alert, F::ReverseGeocoding}, {"PT"}));
    list.push_back(NationalSource("jma", "JMA (Japan)", C::Asia,
                                  {F::Forecast, F::Current, F::Alert, F::Normals,
                                   F::ReverseGeocoding},
                                  {"JP"}));
    {
        SourceInfo knmi = Make("knmi",
                               "Koninklijk Nederlands Meteorologisch Instituut (Netherlands)",
                               C::Europe, {F::Forecast, F::Current, F::Alert, F::Normals});
        // KnmiService — alerts additionally need a recognised admin1; the port has the NAME only.
        knmi.support = [](const SourceLocation& location, SourceFeature) {
            return IsCountry(location, "NL");
        };
        knmi.priority = HighestWhenSupported(knmi.support);
        list.push_back(std::move(knmi));
    }
    list.push_back(NationalSource("lhmt", "LHMT (Lithuania)", C::Europe,
                                  {F::Forecast, F::Current, F::Alert, F::ReverseGeocoding},
                                  {"LT"}));
    list.push_back(NationalSource("lvgmc", "LV\xC4\xA2MC (Latvia)", C::Europe,
                                  {F::Forecast, F::Current, F::AirQuality, F::ReverseGeocoding},
                                  {"LV"}));
    list.push_back(ClimWebSource("malimeteo", "Mali-M\xC3\xA9t\xC3\xA9o", "ML", false));
    list.push_back(NationalSource("metie", "MET \xC3\x89ireann (Ireland)", C::Europe,
                                  {F::Forecast, F::Alert, F::ReverseGeocoding}, {"IE"}));
    {
        SourceInfo metOffice = Make("metoffice", "Met Office (United Kingdom)", C::Europe,
                                    {F::Forecast});
        metOffice.priority = HighestInCountries({"GB", "GG", "IM", "JE", "GI", "FK"});
        metOffice.configured = false;
        list.push_back(std::move(metOffice));
    }
    list.push_back(ClimWebSource("meteobenin", "M\xC3\xA9t\xC3\xA9o B\xC3\xA9nin (Benin)", "BJ",
                                 true));
    list.push_back(ClimWebSource("meteotchad", "M\xC3\xA9t\xC3\xA9o Tchad (Chad)", "TD", true));
    {
        SourceInfo mf = Make("mf", "M\xC3\xA9t\xC3\xA9o-France", C::Europe,
                             {F::Forecast, F::Current, F::Minutely, F::Alert, F::Normals,
                              F::ReverseGeocoding});
        mf.support = &MfSupport;
        mf.priority = &MfPriority;
        // Configured on the reference build — it appears unsuffixed in the Europe group.
        list.push_back(std::move(mf));
    }
    list.push_back(NationalSource("meteolux", "MeteoLux (Luxembourg)", C::Europe,
                                  {F::Forecast, F::Current, F::Alert, F::ReverseGeocoding},
                                  {"LU"}));
    list.push_back(ClimWebSource("mettelsat", "Mettelsat RDC (Congo - Kinshasa)", "CD", false));
    list.push_back(NationalSource("mgm", "MGM (T\xC3\xBCrkiye)", C::Asia,
                                  {F::Forecast, F::Current, F::Alert, F::Normals,
                                   F::ReverseGeocoding},
                                  {"TR"}));
    list.push_back(ClimWebSource("msdzw", "MSD (Zimbabwe)", "ZW", false));
    list.push_back(NationalSource("namem", "NAMEM (Mongolia)", C::Asia,
                                  {F::Forecast, F::Current, F::AirQuality, F::Normals,
                                   F::ReverseGeocoding},
                                  {"MN"}));
    {
        SourceInfo ncdr = Make("ncdr", "NCDR (Taiwan)", C::Asia, {F::Alert});
        ncdr.support = [](const SourceLocation& location, SourceFeature) {
            return InTaiwan(location);
        };
        ncdr.priority = [](const SourceLocation& location, SourceFeature) {
            return InTaiwan(location) ? kPriorityHighest : kPriorityNone;
        };
        list.push_back(std::move(ncdr));
    }
    {
        SourceInfo nlsc = Make("nlsc", "NLSC (Taiwan)", C::Asia, {F::ReverseGeocoding});
        nlsc.support = [](const SourceLocation& location, SourceFeature) {
            return InTaiwan(location);
        };
        nlsc.priority = [](const SourceLocation& location, SourceFeature) {
            return InTaiwan(location) ? kPriorityHighest : kPriorityNone;
        };
        list.push_back(std::move(nlsc));
    }
    list.push_back(NationalSource("nws", "NWS (United States)", C::NorthAmerica,
                                  {F::Forecast, F::Current, F::Alert, F::ReverseGeocoding},
                                  {"US", "GU", "MP", "PR", "VI"}));
    list.push_back(NationalSource("pagasa", "PAGASA (Philippines)", C::Asia,
                                  {F::Forecast, F::Current}, {"PH"}));
    {
        SourceInfo pollen = Make("polleninfo", "Pollen Information Service (Austria)", C::Europe,
                                 {F::Pollen});
        pollen.support = CountriesOnly({"AT", "CH", "DE", "ES", "FR", "GB", "IT", "LT", "LV", "PL",
                                        "SE", "TR", "UA"});
        pollen.priority = &PollenInfoPriority;
        list.push_back(std::move(pollen));
    }
    {
        SourceInfo recosante = Make("recosante", "Recosant\xC3\xA9 (France)", C::Europe,
                                    {F::Pollen});
        recosante.support = CountriesOnly({"FR"});
        // kt:1189-1197 — MEDIUM, not HIGHEST: Atmo France outranks it.
        recosante.priority = [](const SourceLocation& location, SourceFeature) {
            return IsCountry(location, "FR") ? kPriorityMedium : kPriorityNone;
        };
        list.push_back(std::move(recosante));
    }
    {
        SourceInfo meteoam = Make("meteoam", "Servizio Meteo AM (Italy)", C::Europe,
                                  {F::Forecast, F::Current, F::ReverseGeocoding});
        meteoam.priority = HighestInCountries({"IT", "SM", "VA"});
        list.push_back(std::move(meteoam));
    }
    list.push_back(ClimWebSource("smasc", "SMA (Seychelles)", "SC", true));
    list.push_back(ClimWebSource("smasu", "SMA (Sudan)", "SD", false));
    list.push_back(NationalSource("smg", "SMG (Macao SAR China)", C::Asia,
                                  {F::Forecast, F::Current, F::AirQuality, F::Alert, F::Normals},
                                  {"MO"}));
    list.push_back(NationalSource("smhi", "SMHI (Sweden)", C::Europe, {F::Forecast}, {"SE"}));
    list.push_back(ClimWebSource("ssms", "SSMS (South Sudan)", "SS", false));

    return list;
}

/** Location.isUsable (Location.kt) — a current position with no fix yet has none. */
bool IsUsable(const SourceLocation& location) {
    return location.latitude != 0.0 || location.longitude != 0.0;
}

/** The `groupComparator` at LocationPreferences.kt:300-320, transcribed branch for branch. */
bool GroupLess(const std::string& a, const std::string& b) {
    const std::string worldwide = SourceContinentName(SourceContinent::Worldwide);
    if (a == kGroupRecommended && b == kGroupRecommended) {
        return false;
    }
    if (a == kGroupRecommended) {
        return true;
    }
    if (b == kGroupRecommended) {
        return false;
    }
    if (a == worldwide && b == worldwide) {
        return false;
    }
    if (a == worldwide) {
        return true;
    }
    if (b == worldwide) {
        return false;
    }
    // The continent names are plain ASCII, so a byte compare is the collation.
    return a < b;
}

}  // namespace

std::string SourceFeatureName(SourceFeature feature) {
    switch (feature) {
        case SourceFeature::Forecast:
            return "Forecast";                  // strings.xml `forecast`
        case SourceFeature::Current:
            return "Current weather";           // `current_weather`
        case SourceFeature::AirQuality:
            return "Air quality";               // `air_quality`
        case SourceFeature::Pollen:
            return "Pollen";                    // `pollen`
        case SourceFeature::Minutely:
            return "Precipitation nowcasting";  // `precipitation_nowcasting`
        case SourceFeature::Alert:
            return "Alerts";                    // `alerts`
        case SourceFeature::Normals:
            return "Temperature normals";       // `temperature_normals`
        default:
            // `location_reverse_geocoding` — the user-facing name is "Address lookup".
            return "Address lookup";
    }
}

std::string SourceContinentName(SourceContinent continent) {
    switch (continent) {
        case SourceContinent::Worldwide:
            return "Worldwide";
        case SourceContinent::Africa:
            return "Africa";
        case SourceContinent::Asia:
            return "Asia";
        case SourceContinent::Europe:
            return "Europe";
        case SourceContinent::NorthAmerica:
            return "North America";
        case SourceContinent::Oceania:
            return "Oceania";
        default:
            return "South America";
    }
}

const std::string& LocationSources::For(SourceFeature feature) const {
    switch (feature) {
        case SourceFeature::Forecast:   return forecast;
        case SourceFeature::Current:    return current;
        case SourceFeature::AirQuality: return airQuality;
        case SourceFeature::Pollen:     return pollen;
        case SourceFeature::Minutely:   return minutely;
        case SourceFeature::Alert:      return alert;
        case SourceFeature::Normals:    return normals;
        default:                        return reverseGeocoding;
    }
}

void LocationSources::Set(SourceFeature feature, std::string id) {
    switch (feature) {
        case SourceFeature::Forecast:   forecast = std::move(id); break;
        case SourceFeature::Current:    current = std::move(id); break;
        case SourceFeature::AirQuality: airQuality = std::move(id); break;
        case SourceFeature::Pollen:     pollen = std::move(id); break;
        case SourceFeature::Minutely:   minutely = std::move(id); break;
        case SourceFeature::Alert:      alert = std::move(id); break;
        case SourceFeature::Normals:    normals = std::move(id); break;
        default:                        reverseGeocoding = std::move(id); break;
    }
}

bool SourceInfo::Declares(SourceFeature feature) const {
    return std::find(features.begin(), features.end(), feature) != features.end();
}

bool SourceInfo::Supports(const SourceLocation& location, SourceFeature feature) const {
    return support ? support(location, feature) : true;
}

int SourceInfo::Priority(const SourceLocation& location, SourceFeature feature) const {
    return priority ? priority(location, feature) : kPriorityNone;
}

std::string SourceInfo::Group(const SourceLocation& location, SourceFeature feature) const {
    if (Priority(location, feature) >= 0) {
        return kGroupRecommended;
    }
    return SourceContinentName(continent);
}

std::string SourceInfo::DisplayName() const {
    // strings.xml `parameter_not_configured` — "%s (not configured)".
    return configured ? name : name + " (not configured)";
}

const std::vector<SourceInfo>& Sources() {
    static const std::vector<SourceInfo> list = BuildSources();
    return list;
}

const SourceInfo* GetSource(const std::string& id) {
    for (const SourceInfo& source : Sources()) {
        if (source.id == id) {
            return &source;
        }
    }
    return nullptr;
}

std::vector<const SourceInfo*> GetSupportedFeatureSources(SourceFeature feature,
                                                          const SourceLocation& location,
                                                          const std::string& sourceException) {
    std::vector<const SourceInfo*> result;
    // kt:460 — a current position with no fix yet keeps every source, since nothing can be tested.
    const bool usable = !(location.isCurrentPosition && !IsUsable(location));
    for (const SourceInfo& source : Sources()) {
        if (source.id == "naturalearth") {
            continue;  // Excluded by id upstream (kt:451).
        }
        if (!sourceException.empty() && source.id == sourceException) {
            // The currently selected source is kept even when it no longer qualifies, so the caller
            // can render it as "(unavailable)" rather than silently dropping it.
            result.push_back(&source);
            continue;
        }
        if (source.Declares(feature) && (!usable || source.Supports(location, feature))) {
            result.push_back(&source);
        }
    }
    return result;
}

std::vector<SourceGroup> GetCompatibleSources(SourceFeature feature,
                                              const SourceLocation& location,
                                              const std::string& selectedSource) {
    const std::vector<const SourceInfo*> supported =
        GetSupportedFeatureSources(feature, location, selectedSource);

    std::vector<SourceGroup> groups;
    for (const SourceInfo* source : supported) {
        const std::string title = source->Group(location, feature);
        auto found = std::find_if(groups.begin(), groups.end(),
                                  [&title](const SourceGroup& g) { return g.title == title; });
        if (found == groups.end()) {
            groups.push_back(SourceGroup{title, {source}});
        } else {
            found->sources.push_back(source);
        }
    }

    // stable_sort so that groups the comparator calls equal keep the order they were met in.
    std::stable_sort(groups.begin(), groups.end(),
                     [](const SourceGroup& a, const SourceGroup& b) {
                         return GroupLess(a.title, b.title);
                     });

    for (SourceGroup& group : groups) {
        if (group.title == kGroupRecommended && feature != SourceFeature::ReverseGeocoding) {
            // kt:329-332 — the Recommended group is additionally ordered by priority DESCENDING.
            std::stable_sort(group.sources.begin(), group.sources.end(),
                             [&location, feature](const SourceInfo* a, const SourceInfo* b) {
                                 return a->Priority(location, feature) >
                                        b->Priority(location, feature);
                             });
        }
    }
    return groups;
}

const SourceInfo* GetBestSourceForFeature(SourceFeature feature, const SourceLocation& location) {
    const std::vector<const SourceInfo*> candidates =
        GetSupportedFeatureSources(feature, location, "");
    const SourceInfo* best = nullptr;
    int bestPriority = kPriorityNone;
    for (const SourceInfo* source : candidates) {
        // kt:474-480: not removed, supported here, actually recommended, and configured.
        if (!source->configured || !source->Supports(location, feature)) {
            continue;
        }
        const int priority = source->Priority(location, feature);
        if (priority <= kPriorityNone) {
            continue;
        }
        // `maxByOrNull` returns the FIRST maximal element, so ties keep sourceList order.
        if (best == nullptr || priority > bestPriority) {
            best = source;
            bestPriority = priority;
        }
    }
    return best;
}

const SourceInfo* GetDefaultSourceForFeature(SourceFeature feature,
                                             const SourceLocation& location) {
    switch (feature) {
        case SourceFeature::AirQuality:
            return IsCountry(location, "IN") ? nullptr : GetSource("openmeteo");
        case SourceFeature::Pollen: {
            const SourceInfo* openMeteo = GetSource("openmeteo");
            if (openMeteo != nullptr && openMeteo->Supports(location, feature)) {
                return openMeteo;
            }
            const SourceInfo* accu = GetSource("accu");
            return (accu != nullptr && accu->Supports(location, feature)) ? accu : nullptr;
        }
        case SourceFeature::Alert:
            return GetSource("accu");
        case SourceFeature::Normals:
            return IsCountry(location, "CN") ? nullptr : GetSource("accu");
        case SourceFeature::ReverseGeocoding:
            // BuildConfig.DEFAULT_GEOCODING_SOURCE is naturalearth, but kt:526 names nominatim
            // explicitly for this fallback.
            return GetSource("nominatim");
        default:
            return GetSource("openmeteo");
    }
}

namespace {

/**
 * SourceManager.getBestSourceForFeatureOrDefault (kt:488-496).
 *
 * Reverse geocoding is only preset for a current position — a saved city already has its address.
 */
const SourceInfo* BestOrDefault(SourceFeature feature, const SourceLocation& location) {
    if (feature != SourceFeature::ReverseGeocoding || location.isCurrentPosition) {
        const SourceInfo* best = GetBestSourceForFeature(feature, location);
        return best != nullptr ? best : GetDefaultSourceForFeature(feature, location);
    }
    return nullptr;
}

std::string PresetId(SourceFeature feature, const SourceLocation& location) {
    const SourceInfo* source = BestOrDefault(feature, location);
    return source != nullptr ? source->id : std::string();
}

}  // namespace

LocationSources ApplyDefaultPreset(const SourceLocation& location) {
    LocationSources sources;

    const SourceInfo* forecast = BestOrDefault(SourceFeature::Forecast, location);
    sources.forecast = forecast != nullptr ? forecast->id : std::string(kDefaultForecastSource);

    /*
     * The current-weather line reads backwards and is transcribed as written (Location.kt:59-64):
     * when no source is RECOMMENDED for CURRENT, the default is kept only if it equals the forecast
     * source, and is otherwise dropped to "" — which the dialog renders as "Forecast", i.e. take the
     * current conditions from the forecast source rather than pinning a second one.
     */
    const SourceInfo* bestCurrent = GetBestSourceForFeature(SourceFeature::Current, location);
    if (bestCurrent != nullptr) {
        sources.current = bestCurrent->id;
    } else {
        const SourceInfo* fallback = GetDefaultSourceForFeature(SourceFeature::Current, location);
        sources.current = (fallback != nullptr && fallback->id == sources.forecast)
                              ? fallback->id
                              : std::string();
    }

    sources.airQuality = PresetId(SourceFeature::AirQuality, location);
    sources.pollen = PresetId(SourceFeature::Pollen, location);
    sources.minutely = PresetId(SourceFeature::Minutely, location);
    sources.alert = PresetId(SourceFeature::Alert, location);
    sources.normals = PresetId(SourceFeature::Normals, location);
    sources.reverseGeocoding = PresetId(SourceFeature::ReverseGeocoding, location);
    return sources;
}

namespace {

/** kt:116-181 — the chosen source if it declares AND supports the feature here, else empty. */
std::string Pinned(const SourceInfo& source, SourceFeature feature,
                   const SourceLocation& location) {
    return source.Declares(feature) && source.Supports(location, feature) ? source.id
                                                                          : std::string();
}

}  // namespace

LocationSources ApplyDefaultSourcePreference(const SourceLocation& location,
                                             const std::string& defaultForecastSource) {
    const SourceInfo* chosen =
        defaultForecastSource == "auto" ? nullptr : GetSource(defaultForecastSource);
    if (chosen == nullptr) {
        return ApplyDefaultPreset(location);
    }

    LocationSources sources;
    sources.forecast = chosen->id;
    sources.current = Pinned(*chosen, SourceFeature::Current, location);
    sources.airQuality = Pinned(*chosen, SourceFeature::AirQuality, location);
    sources.pollen = Pinned(*chosen, SourceFeature::Pollen, location);
    sources.minutely = Pinned(*chosen, SourceFeature::Minutely, location);
    sources.alert = Pinned(*chosen, SourceFeature::Alert, location);
    sources.normals = Pinned(*chosen, SourceFeature::Normals, location);
    /*
     * Untouched by the Kotlin's `copy(...)`, so it keeps what the preset would have left. The search
     * flow builds the location from a geocoding hit that already carries its own reverse-geocoding
     * source, so it is taken from the plain preset rather than pinned to the chosen source.
     */
    sources.reverseGeocoding = ApplyDefaultPreset(location).reverseGeocoding;
    return sources;
}

const std::vector<const SourceInfo*>& LocationSearchSources() {
    static const std::vector<const SourceInfo*> list = []() {
        std::vector<const SourceInfo*> result;
        for (const SourceInfo& source : Sources()) {
            if (source.IsLocationSearchSource()) {
                result.push_back(&source);
            }
        }
        return result;
    }();
    return list;
}

const SourceInfo* LocationSearchSourceOrDefault(const std::string& id) {
    const std::vector<const SourceInfo*>& list = LocationSearchSources();
    for (const SourceInfo* source : list) {
        if (source->id == id) {
            return source;
        }
    }
    for (const SourceInfo* source : list) {
        if (source->id == kDefaultLocationSearchSource) {
            return source;
        }
    }
    return list.empty() ? nullptr : list[0];
}

}  // namespace breezy
