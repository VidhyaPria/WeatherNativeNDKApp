#include "data/CalendarCatalogue.h"

#include <algorithm>
#include <cctype>

namespace breezy {
namespace {

/**
 * The ten real calendars, in the order ICU's display names sort into
 * (CalendarHelper.kt:98 `.sortedBy { it.displayName }`).
 */
const std::vector<CalendarOption>& Supported() {
    static const std::vector<CalendarOption> kSupported = {
        {"chinese", "Chinese Calendar"},
        {"dangi", "Dangi Calendar"},
        {"hebrew", "Hebrew Calendar"},
        {"indian", "Indian National Calendar"},
        {"islamic", "Islamic Calendar"},
        {"islamic-rgsa", "Islamic Calendar (Saudi Arabia, sighting)"},
        {"islamic-umalqura", "Islamic Calendar (Umm al-Qura)"},
        {"islamic-tbla", "Islamic Calendar (tabular, astronomical epoch)"},
        {"islamic-civil", "Islamic Calendar (tabular, civil epoch)"},
        {"persian", "Persian Calendar"}};
    return kSupported;
}

std::string Lower(const std::string& value) {
    std::string out = value;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string Upper(const std::string& value) {
    std::string out = value;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}

/**
 * `getCalendarPreferenceForLocale` (CalendarHelper.kt:118-143), rule for rule. Everything it does
 * not name falls through to Gregorian, which the list represents as "None".
 */
std::string RegionalPreferenceId(const std::string& language, const std::string& country) {
    const std::string lang = Lower(language);
    const std::string region = Upper(country);
    if (region == "CN" || region == "HK" || region == "MO" || region == "TW" || lang == "zh") {
        return "chinese";
    }
    if (region == "KP" || region == "KR" || lang == "ko") {
        return "dangi";
    }
    if (lang == "he" || lang == "iw") {
        return "hebrew";
    }
    if (region == "IN") {
        return "indian";
    }
    if (region == "SA") {
        return "islamic-rgsa";
    }
    if (region == "IR" || lang == "fa") {
        return "persian";
    }
    // "Looks like all locales default to Gregorian" (kt:139-141).
    return "";
}

std::string NameFor(const std::string& id) {
    for (const CalendarOption& option : Supported()) {
        if (option.id == id) {
            return option.name;
        }
    }
    return "";
}

}  // namespace

std::vector<CalendarOption> CalendarOptions(const std::string& language,
                                            const std::string& country) {
    const std::string regional = RegionalPreferenceId(language, country);
    const std::string regionalName = regional.empty() ? "None" : NameFor(regional);

    std::vector<CalendarOption> options;
    options.push_back({"none", "None"});  // strings.xml settings_none
    // strings.xml `parenthesis` = "%1$s (%2$s)", with settings_regional_preference.
    options.push_back({"", "Regional preference (" + regionalName + ")"});
    for (const CalendarOption& option : Supported()) {
        options.push_back(option);
    }
    return options;
}

std::string CalendarSummary(const std::string& stored, const std::string& language,
                            const std::string& country) {
    for (const CalendarOption& option : CalendarOptions(language, country)) {
        if (option.id == stored) {
            return option.name;
        }
    }
    return "None";
}

}  // namespace breezy
