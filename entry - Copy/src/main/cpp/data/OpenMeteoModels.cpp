#include "data/OpenMeteoModels.h"

#include <algorithm>

namespace breezy {
namespace {

/** strings.xml:768 — only `best_match` carries it. */
constexpr const char* kBestMatchDescription = "Combines the best weather models for this location";
/** strings.xml:770 — every id ending `_seamless` carries it (kt:118-119). */
constexpr const char* kSeamlessDescription =
    "Combines all weather models from this provider into a seamless prediction";

/*
 * The accented and degree characters are written as UTF-8 escapes rather than typed.
 *
 * A non-ASCII literal in this tree has reached the device as mojibake before — the toolbar bug
 * PROGRESS.md records — because a source file's encoding is not something the build asserts. An
 * escape cannot be re-encoded by whatever writes the file next.
 *   \xC2\xB0        U+00B0 DEGREE SIGN
 *   \xC3\xA9        U+00E9 LATIN SMALL LETTER E WITH ACUTE
 */
constexpr const char* kDegree = "\xC2\xB0";
constexpr const char* kMeteoFrance = "M\xC3\xA9t\xC3\xA9o-France";

std::vector<WeatherModelInfo> BuildCatalogue() {
    const std::string deg = kDegree;
    const std::string mf = kMeteoFrance;
    return {
        {"best_match", "Best match", kBestMatchDescription},

        {"ecmwf_ifs", "ECMWF IFS HRES 9 km", ""},
        {"ecmwf_ifs025", "ECMWF IFS 0.25" + deg, ""},
        {"ecmwf_aifs025_single", "ECMWF AIFS 0.25" + deg + " Single", ""},
        {"cma_grapes_global", "CMA GRAPES Global", ""},
        {"bom_access_global", "BOM ACCESS Global", ""},

        {"gfs_seamless", "NCEP GFS Seamless", kSeamlessDescription},
        {"gfs_global", "NCEP GFS Global 0.11" + deg + " / 0.25" + deg, ""},
        {"gfs_hrrr", "NCEP HRRR U.S. Conus", ""},
        {"ncep_nbm_conus", "NCEP NBM U.S. Conus", ""},
        {"ncep_nam_conus", "NCEP NAM U.S. Conus", ""},
        {"gfs_graphcast025", "GFS GraphCast", ""},

        {"jma_seamless", "JMA Seamless", kSeamlessDescription},
        {"jma_msm", "JMA MSM", ""},
        {"jma_gsm", "JMA GSM", ""},

        {"kma_seamless", "KMA Seamless", kSeamlessDescription},
        {"kma_ldps", "KMA LDPS", ""},
        {"kma_gdps", "KMA GDPS", ""},

        {"icon_seamless", "DWD ICON Seamless", kSeamlessDescription},
        {"icon_global", "DWD ICON Global", ""},
        {"icon_eu", "DWD ICON EU", ""},
        {"icon_d2", "DWD ICON D2", ""},

        {"gem_seamless", "GEM Seamless", kSeamlessDescription},
        {"gem_global", "GEM Global", ""},
        {"gem_regional", "GEM Regional", ""},
        {"gem_hrdps_continental", "GEM HRDPS Continental", ""},

        {"meteofrance_seamless", mf + " Seamless", kSeamlessDescription},
        {"meteofrance_arpege_world", mf + " ARPEGE World", ""},
        {"meteofrance_arpege_europe", mf + " ARPEGE Europe", ""},
        {"meteofrance_arome_france", mf + " AROME France", ""},
        {"meteofrance_arome_france_hd", mf + " AROME France HD", ""},

        {"italia_meteo_arpae_icon_2i", "ItaliaMeteo ARPAE ICON 2I", ""},

        {"metno_seamless", "MET Norway Nordic (with ECMWF) Seamless", kSeamlessDescription},
        {"metno_nordic", "MET Norway Nordic", ""},

        {"knmi_seamless", "KNMI (with ECMWF) Seamless", kSeamlessDescription},
        {"knmi_harmonie_arome_europe", "KNMI HARMONIE-AROME Europe", ""},
        {"knmi_harmonie_arome_netherlands", "KNMI HARMONIE-AROME Netherlands", ""},

        {"dmi_seamless", "DMI (with ECMWF) Seamless", kSeamlessDescription},
        {"dmi_harmonie_arome_europe", "DMI HARMONIE-AROME Europe", ""},

        {"ukmo_seamless", "UK Met Office Seamless", kSeamlessDescription},
        {"ukmo_global_deterministic_10km", "UK Met Office Global 10 km", ""},
        {"ukmo_uk_deterministic_2km", "UK Met Office UK 2 km", ""},

        {"meteoswiss_icon_seamless", "MeteoSwiss ICON Seamless", kSeamlessDescription},
        {"meteoswiss_icon_ch1", "MeteoSwiss ICON CH1", ""},
        {"meteoswiss_icon_ch2", "MeteoSwiss ICON CH2", ""},
    };
}

}  // namespace

const char* const kModelBestMatch = "best_match";

const std::vector<WeatherModelInfo>& WeatherModels() {
    // Built once; the strings are concatenations, so a namespace-scope initialiser would run
    // before main and this is the cheaper guarantee.
    static const std::vector<WeatherModelInfo> catalogue = BuildCatalogue();
    return catalogue;
}

const WeatherModelInfo* FindWeatherModel(const std::string& id) {
    for (const WeatherModelInfo& model : WeatherModels()) {
        if (model.id == id) {
            return &model;
        }
    }
    return nullptr;
}

std::vector<std::string> SelectedWeatherModels(const std::string& stored) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= stored.size()) {
        const size_t comma = stored.find(',', start);
        const size_t end = comma == std::string::npos ? stored.size() : comma;
        std::string id = stored.substr(start, end - start);
        // Trim, so a stored "a, b" is read the same as "a,b".
        while (!id.empty() && id.front() == ' ') {
            id.erase(id.begin());
        }
        while (!id.empty() && id.back() == ' ') {
            id.pop_back();
        }
        if (!id.empty() && FindWeatherModel(id) != nullptr) {
            out.push_back(id);
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    if (out.empty()) {
        out.emplace_back(kModelBestMatch);
    }
    return out;
}

std::string WeatherModelsParam(const std::string& stored) {
    const std::vector<std::string> ids = SelectedWeatherModels(stored);
    std::string out;
    for (size_t i = 0; i < ids.size(); ++i) {
        if (i > 0) {
            out += ',';
        }
        out += ids[i];
    }
    return out;
}

std::string WeatherModelsSummary(const std::string& stored) {
    std::vector<std::string> names;
    for (const std::string& id : SelectedWeatherModels(stored)) {
        const WeatherModelInfo* model = FindWeatherModel(id);
        if (model != nullptr) {
            names.push_back(model->name);
        }
    }
    std::sort(names.begin(), names.end());
    std::string out;
    for (size_t i = 0; i < names.size(); ++i) {
        if (i > 0) {
            // `locale_separator` is ", " in English.
            out += ", ";
        }
        out += names[i];
    }
    return out;
}

}  // namespace breezy
