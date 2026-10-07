/*
 * Every configurable source's own settings — the data behind screen 3f (Weather sources).
 *
 * The C++ transcription of ArkUIApp's data/SourcePreferences.ets. TRANSCRIBED FROM THAT FILE, not
 * re-derived from the Kotlin: the section ORDER is data rather than a sort (see below), so the two
 * ports can only agree by copying, and the whole point of building the app twice is that the two
 * halves are comparable.
 *
 * On Android each source class answers `getPreferences(context)` with its own list and the screen
 * renders whatever comes back (WeatherSourcesSettingsScreen.kt:196-233). There are no source CLIENTS
 * in this port, so there is nothing to ask; the lists are the transcription instead, one entry per
 * `getPreferences` override in `src/main` + `src_nonfreenet` (the reference build is not the freenet
 * flavour).
 *
 * Only two kinds exist, and both already have a dialog from screen 3e:
 *
 *   Text   EditTextPreference. An API key, or an instance URL.
 *   List   ListPreference. Only AccuWeather has any.
 *
 * ORDER IS DATA, NOT A SORT. The Kotlin sorts the sections with
 * `Collator.getInstance(context.currentLocale)` (kt:187-190) — full ICU collation, which puts
 * "Mali-Météo" before "MET Éireann" before "Met Office" before "Météo Bénin". C++ has no ICU here at
 * all, so the order was read off the reference device (ref_screen3f_sources_top.png through the
 * bottom of the list) and stored. 40 sections, AccuWeather through SSMS.
 *
 * Baidu IP is absent on purpose: kt:185 excludes `LocationSource`, which sends it to screen 3e.
 * GeoNames and Nominatim are geocoders rather than location sources, so they DO appear here.
 */

#ifndef BREEZY_DATA_SOURCE_PREFERENCES_H
#define BREEZY_DATA_SOURCE_PREFERENCES_H

#include <string>
#include <vector>

namespace breezy {

/** strings.xml:713 — what an empty API key reads as. An empty instance shows its default URL. */
extern const char* const kSourceDefaultUnchanged;
/** :736 — EditTextPreference.URL_REGEX's error, shown under an instance field that fails it. */
extern const char* const kSourceInstanceInvalid;

enum class SourcePrefKind { Text = 0, List };

struct SourcePrefOption {
    std::string value;
    std::string name;
};

struct SourcePrefSpec {
    /**
     * The key inside the source's own config. Android gives each source a `SourceConfigStore(id)`,
     * so these repeat across sources ("apikey" many times over); this port has one bag, so the
     * storage key is `source_<sourceId>_<key>` — the convention screen 3e already set.
     */
    std::string key;
    std::string title;
    SourcePrefKind kind = SourcePrefKind::Text;
    /** List only, in the Kotlin array's order. */
    std::vector<SourcePrefOption> options;
    /** List: the selected key on a fresh install. Text: always empty. */
    std::string fallback;
    /**
     * Text only. An instance row's placeholder is the source's baseUrl, and its summary falls back
     * to the same URL rather than to "Default (unchanged)" (ClimWebService.kt:376-382). An API-key
     * row has neither.
     */
    std::string placeholder;
    /** Text only — URL_REGEX applies to instance rows, so a bad value is refused (kt:383). */
    bool url = false;
};

struct SourcePrefSection {
    /** Keys into SourceCatalogue, which already carries the display name shown as the header. */
    std::string sourceId;
    std::vector<SourcePrefSpec> prefs;
};

/** The table, built once on first use. */
const std::vector<SourcePrefSection>& SourcePreferences();

/** The one preferences key a value is stored under. See SourcePrefSpec::key. */
std::string SourcePrefKey(const std::string& sourceId, const std::string& key);

}  // namespace breezy

#endif  // BREEZY_DATA_SOURCE_PREFERENCES_H
