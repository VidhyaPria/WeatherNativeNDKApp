#include "SourcePreferences.h"

namespace breezy {
namespace {

const char* const kApiKeyTitle = "API key";    // strings.xml:714 `settings_source_api_key`
const char* const kInstanceTitle = "Instance"; // :735 `settings_source_instance`

/** One EditTextPreference for an API key — by far the commonest shape. */
SourcePrefSpec ApiKey() {
    SourcePrefSpec spec;
    spec.key = "apikey";
    spec.title = kApiKeyTitle;
    spec.kind = SourcePrefKind::Text;
    return spec;
}

/** ClimWebService.kt:372-391, and the same shape in Bright Sky, FPAS, KNMI, Nominatim, Pirate. */
SourcePrefSpec Instance(const char* baseUrl) {
    SourcePrefSpec spec;
    spec.key = "instance";
    spec.title = kInstanceTitle;
    spec.kind = SourcePrefKind::Text;
    spec.placeholder = baseUrl;
    spec.url = true;
    return spec;
}

/** A named instance row — Open-Meteo has three, one per API host. */
SourcePrefSpec NamedInstance(const char* key, const char* title, const char* baseUrl) {
    SourcePrefSpec spec = Instance(baseUrl);
    spec.key = key;
    spec.title = title;
    return spec;
}

SourcePrefSpec ListPref(const char* key, const char* title,
                        std::vector<SourcePrefOption> options, const char* fallback) {
    SourcePrefSpec spec;
    spec.key = key;
    spec.title = title;
    spec.kind = SourcePrefKind::List;
    spec.options = std::move(options);
    spec.fallback = fallback;
    return spec;
}

/** A section whose whole content is one API key. */
SourcePrefSection ApiKeySource(const char* sourceId) {
    SourcePrefSection section;
    section.sourceId = sourceId;
    section.prefs.push_back(ApiKey());
    return section;
}

/** A ClimWeb agency: one instance row, placeholder = its baseUrl. */
SourcePrefSection ClimWeb(const char* sourceId, const char* baseUrl) {
    SourcePrefSection section;
    section.sourceId = sourceId;
    section.prefs.push_back(Instance(baseUrl));
    return section;
}

SourcePrefSection OneInstance(const char* sourceId, const char* baseUrl) {
    return ClimWeb(sourceId, baseUrl);
}

std::vector<SourcePrefSection> Build() {
    std::vector<SourcePrefSection> table;

    /* AccuService.kt:894-936 — the only source with list preferences. */
    {
        SourcePrefSection accu;
        accu.sourceId = "accu";
        // The defaults are the reference device's: Enterprise / 15 days / 240 hours.
        accu.prefs.push_back(ListPref("portal", "Portal",
                                      // arrays.xml:240-247, strings.xml:412-413
                                      {{"developer", "Developer"}, {"enterprise", "Enterprise"}},
                                      "enterprise"));
        accu.prefs.push_back(ApiKey());
        accu.prefs.push_back(ListPref("days", "Days",
                                      // arrays.xml:249-260, strings.xml:415-418
                                      {{"1", "1 day"},
                                       {"5", "5 days"},
                                       {"10", "10 days"},
                                       {"15", "15 days"}},
                                      "15"));
        accu.prefs.push_back(ListPref("hours", "Hours",
                                      // arrays.xml:262-277, strings.xml:420-425
                                      {{"1", "1 hour"},
                                       {"12", "12 hours"},
                                       {"24", "24 hours"},
                                       {"72", "72 hours"},
                                       {"120", "120 hours"},
                                       {"240", "240 hours"}},
                                      "240"));
        table.push_back(std::move(accu));
    }

    table.push_back(ApiKeySource("aemet"));  // AemetService.kt:687
    table.push_back(ClimWeb("anambf", "https://www.meteoburkina.bf/"));
    table.push_back(ClimWeb("anamet", "https://www.anamet-togo.com/"));
    // The four regional Atmo agencies share AtmoService.kt:154-169.
    table.push_back(ApiKeySource("atmoaura"));
    table.push_back(ApiKeySource("atmofrance"));  // AtmoFranceService.kt:226
    table.push_back(ApiKeySource("atmograndest"));
    table.push_back(ApiKeySource("atmohdf"));
    table.push_back(ApiKeySource("atmosud"));
    table.push_back(ApiKeySource("bmkg"));  // BmkgService.kt:573
    table.push_back(OneInstance("brightsky", "https://api.brightsky.dev/"));  // BrightSky:350
    table.push_back(ApiKeySource("cwa"));  // CwaService.kt:998
    table.push_back(ClimWeb("dccms", "https://www.metmalawi.gov.mw/"));
    table.push_back(ClimWeb("dmnne", "https://www.niger-meteo.ne/"));
    table.push_back(ClimWeb("dwrgm", "https://meteogambia.org/"));
    table.push_back(ApiKeySource("eccc"));  // EcccService.kt:404
    table.push_back(ClimWeb("ethiomet", "https://www.ethiomet.gov.et/"));
    table.push_back(OneInstance("fpas", "https://alerts.kde.org/"));  // FpasService.kt:166
    table.push_back(ApiKeySource("geonames"));  // GeoNamesService.kt:134
    table.push_back(ClimWeb("gmet", "https://www.meteo.gov.gh/"));
    table.push_back(ClimWeb("igebu", "https://www.igebu.bi/"));
    table.push_back(ApiKeySource("infoplaza"));  // InfoplazaService.kt:346
    table.push_back(ClimWeb("inmgb", "https://www.meteoguinebissau.org/"));
    // KnmiService.kt:601-627. Its placeholder is debugMode-conditional; this is the release value.
    table.push_back(OneInstance("knmi", "https://api.app.knmi.cloud/"));
    table.push_back(ClimWeb("malimeteo", "https://malimeteo.ml/"));
    table.push_back(ApiKeySource("metie"));      // MetIeService.kt:435
    table.push_back(ApiKeySource("metoffice"));  // MetOfficeService.kt:230
    table.push_back(ClimWeb("meteobenin", "https://www.meteobenin.bj/"));
    table.push_back(ClimWeb("meteotchad", "https://www.meteotchad.org/"));
    table.push_back(ApiKeySource("mf"));  // MfService.kt:1102
    table.push_back(ClimWeb("mettelsat", "https://www.meteordcongo.cd/"));
    table.push_back(ClimWeb("msdzw", "https://www.weatherzw.org.zw/"));
    table.push_back(OneInstance("nominatim", "https://nominatim.openstreetmap.org/"));

    /* OpenMeteoService.kt:703-748 — three instances, one per API host. */
    {
        SourcePrefSection openMeteo;
        openMeteo.sourceId = "openmeteo";
        openMeteo.prefs.push_back(NamedInstance("instance_forecast", "Forecast instance",
                                                "https://api.open-meteo.com/"));
        openMeteo.prefs.push_back(NamedInstance("instance_air_quality", "Air quality instance",
                                                "https://air-quality-api.open-meteo.com/"));
        openMeteo.prefs.push_back(NamedInstance("instance_geocoding", "Geocoding instance",
                                                "https://geocoding-api.open-meteo.com/"));
        table.push_back(std::move(openMeteo));
    }

    table.push_back(ApiKeySource("openweather"));  // OpenWeatherService.kt:299

    /* PirateWeatherService.kt:429-475 — instance THEN key. */
    {
        SourcePrefSection pirate;
        pirate.sourceId = "pirateweather";
        pirate.prefs.push_back(Instance("https://api.pirateweather.net/"));
        pirate.prefs.push_back(ApiKey());
        table.push_back(std::move(pirate));
    }

    table.push_back(ApiKeySource("polleninfo"));  // PollenInfoService.kt:147
    table.push_back(ClimWeb("smasc", "https://www.meteo.sc/"));
    table.push_back(ClimWeb("smasu", "https://meteosudan.sd/"));
    table.push_back(ClimWeb("ssms", "https://meteosouthsudan.com.ss/"));

    return table;
}

}  // namespace

const char* const kSourceDefaultUnchanged = "Default (unchanged)";
const char* const kSourceInstanceInvalid = "Must be an https URL ending with a /";

const std::vector<SourcePrefSection>& SourcePreferences() {
    static const std::vector<SourcePrefSection> table = Build();
    return table;
}

std::string SourcePrefKey(const std::string& sourceId, const std::string& key) {
    return "source_" + sourceId + "_" + key;
}

}  // namespace breezy
