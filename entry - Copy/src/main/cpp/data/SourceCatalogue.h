/*
 * The source catalogue — SourceManager.kt's `sourceList`, metadata only.
 *
 * The C++ transcription of ArkUIApp's data/SourceCatalogue.ets, which is itself the transcription of
 * Android's 46 `*ServiceStub.kt` files. CLAUDE.md scopes the port to ONE live source (Open-Meteo)
 * but explicitly does NOT scope down the metadata: any screen that lists sources renders the full
 * catalogue, "including each source's supported features. Port the metadata table, not the clients."
 *
 * Kept deliberately parallel to the ArkTS file — same names, same order, same predicates — so the
 * two ports can be diffed, which is the point of building the app twice. The deviations documented
 * there apply here unchanged:
 *
 *   1. Display names are the ENGLISH CLDR forms.
 *   2. The Atmo regional predicates test admin1/admin2 NAMES only; Open-Meteo's geocoding returns
 *      no ISO admin codes. This can only under-recommend a French regional air-quality source.
 *   3. `configured` is read off the reference build, not guessed — see the field's note.
 *
 * ONE DIFFERENCE FROM THE ARKTS FILE, and it is only in how the same result is reached: the
 * national block is authored here already sorted, where ArkTS sorts it at load with
 * `localeCompare`. The order below was produced BY that comparator rather than by hand (the earlier
 * hand-ordering in ArkTS got it wrong and it showed), so both ports list the same sources in the
 * same places. Re-derive it, do not edit it by eye.
 */
#ifndef BREEZY_DATA_SOURCE_CATALOGUE_H
#define BREEZY_DATA_SOURCE_CATALOGUE_H

#include <functional>
#include <string>
#include <vector>

namespace breezy {

/** domain/.../source/SourceFeature.kt:22-29. */
enum class SourceFeature {
    Forecast = 0,
    Current,
    AirQuality,
    Pollen,
    Minutely,
    Alert,
    Normals,
    ReverseGeocoding
};
/** The eight rows of the weather-sources dialog, in the order it lists them. */
constexpr int kSourceFeatureCount = 8;

/** domain/.../source/SourceContinent.kt:22-28. */
enum class SourceContinent {
    Worldwide = 0,
    Africa,
    Asia,
    Europe,
    NorthAmerica,
    Oceania,
    SouthAmerica
};

/** FeatureSource.kt:70-77. */
constexpr int kPriorityHighest = 100;
constexpr int kPriorityHigh = 75;
constexpr int kPriorityMedium = 50;
constexpr int kPriorityLow = 25;
constexpr int kPriorityLowest = 0;
constexpr int kPriorityNone = -1;

/** BuildConfig.DEFAULT_FORECAST_SOURCE / DEFAULT_LOCATION_SEARCH_SOURCE (build.gradle.kts:225-229). */
extern const char* const kDefaultForecastSource;
extern const char* const kDefaultLocationSearchSource;

/** `weather_source_recommended` — the pseudo-group that outranks every continent. */
extern const char* const kGroupRecommended;

std::string SourceFeatureName(SourceFeature feature);
std::string SourceContinentName(SourceContinent continent);

/**
 * Everything the predicates test.
 *
 * Deliberately NOT the renderer's SceneState: what the catalogue reads is where a location IS, and
 * nothing about its weather. It is also what the ArkTS shell knows about a saved location, so the
 * same struct crosses for a search hit and for a saved row.
 */
struct SourceLocation {
    std::string countryCode;
    /** admin1 — Location.region. */
    std::string region;
    std::string admin2;
    double latitude = 0.0;
    double longitude = 0.0;
    bool isCurrentPosition = false;
};

/** The eight per-feature source ids a location carries (Location.kt's *Source fields). */
struct LocationSources {
    std::string forecast;
    std::string current;
    std::string airQuality;
    std::string pollen;
    std::string minutely;
    std::string alert;
    std::string normals;
    std::string reverseGeocoding;

    const std::string& For(SourceFeature feature) const;
    void Set(SourceFeature feature, std::string id);
};

using SupportFn = std::function<bool(const SourceLocation&, SourceFeature)>;
using PriorityFn = std::function<int(const SourceLocation&, SourceFeature)>;

class SourceInfo {
public:
    std::string id;
    std::string name;
    SourceContinent continent = SourceContinent::Worldwide;
    std::vector<SourceFeature> features;
    /** LocationSearchSource.locationSearchAttribution — only the search sources carry one. */
    std::string searchAttribution;
    /** Whether this port has a client for it. Only Open-Meteo does. */
    bool implemented = false;
    /**
     * ConfigurableSource.isConfigured. LocationPreferences.kt:341-344 makes a row unselectable when
     * a source is configurable and NOT configured, and `SourceExtensions.getName` renames it to
     * "%s (not configured)". Which sources that covers is a property of the BUILD, so it was read
     * off the reference device: Infoplaza, PirateWeather, Met Office and CWA are suffixed there.
     */
    bool configured = true;
    SupportFn support;
    PriorityFn priority;

    /** `supportedFeatures.containsKey(feature)`. */
    bool Declares(SourceFeature feature) const;
    /** LocationSearchSource — the search screen's own chooser lists exactly these. */
    bool IsLocationSearchSource() const { return !searchAttribution.empty(); }
    /** FeatureSource.isFeatureSupportedForLocation — default true (FeatureSource.kt:52-55). */
    bool Supports(const SourceLocation& location, SourceFeature feature) const;
    /** FeatureSource.getFeaturePriorityForLocation — default PRIORITY_NONE (kt:65-68). */
    int Priority(const SourceLocation& location, SourceFeature feature) const;
    /** Source.getGroup (Source.kt:44-54) — Recommended, else the source's continent. */
    std::string Group(const SourceLocation& location, SourceFeature feature) const;
    /** `SourceExtensions.getName` — the "(not configured)" suffix a picker row carries. */
    std::string DisplayName() const;
};

/** `sourceList` (SourceManager.kt:314-330), in order. Built once, on first use. */
const std::vector<SourceInfo>& Sources();

const SourceInfo* GetSource(const std::string& id);

/** SourceManager.getSupportedFeatureSources (kt:444-466). */
std::vector<const SourceInfo*> GetSupportedFeatureSources(SourceFeature feature,
                                                          const SourceLocation& location,
                                                          const std::string& sourceException);

/** One group of the picker: a heading and the sources under it. */
struct SourceGroup {
    std::string title;
    std::vector<const SourceInfo*> sources;
};

/** getCompatibleSources (LocationPreferences.kt:293-349). */
std::vector<SourceGroup> GetCompatibleSources(SourceFeature feature,
                                              const SourceLocation& location,
                                              const std::string& selectedSource);

/** SourceManager.getBestSourceForFeature (kt:470-486). */
const SourceInfo* GetBestSourceForFeature(SourceFeature feature, const SourceLocation& location);
/** SourceManager.getDefaultSourceForFeature (kt:498-528). */
const SourceInfo* GetDefaultSourceForFeature(SourceFeature feature, const SourceLocation& location);

/**
 * Location.applyDefaultPreset (Location.kt:54-73) — what Android runs on EVERY location it creates,
 * so a new city arrives with all eight sources already chosen rather than eight "None"s.
 *
 * CONSEQUENCE, and it is deliberate: this port's refresh fetches Open-Meteo plus FMI alerts whatever
 * these fields say, so a location can display "Alerts: AccuWeather" while the alert it shows came
 * from FMI. That is Android's stored state faithfully reproduced. Honouring the fields in the
 * refresh is what closes the gap, and that needs the clients CLAUDE.md scopes out.
 */
LocationSources ApplyDefaultPreset(const SourceLocation& location);

/**
 * SearchActivityRepository.getLocationWithAppliedPreference (kt:101-186) — what a location added
 * from the SEARCH screen gets, as opposed to the plain preset the my-location FAB uses
 * (ManagementFragment.kt:241-243).
 *
 * `defaultForecastSource` is screen 3f's General row. "auto", or an id no longer in the catalogue,
 * falls through to ApplyDefaultPreset; anything else pins that source for the forecast and for each
 * feature it actually supports here, leaving the rest empty. It does NOT fall back per feature —
 * kt:116-181 assigns the chosen source or nothing.
 */
LocationSources ApplyDefaultSourcePreference(const SourceLocation& location,
                                             const std::string& defaultForecastSource);

/** getLocationSearchSources() (SourceManager.kt:364-366), in the master list's order. */
const std::vector<const SourceInfo*>& LocationSearchSources();
/** getLocationSearchSourceOrDefault (SourceManager.kt:371-372). */
const SourceInfo* LocationSearchSourceOrDefault(const std::string& id);

}  // namespace breezy

#endif  // BREEZY_DATA_SOURCE_CATALOGUE_H
