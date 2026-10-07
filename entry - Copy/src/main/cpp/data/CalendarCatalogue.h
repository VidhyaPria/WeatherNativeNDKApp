/*
 * The alternate-calendar catalogue — CalendarHelper.kt:55-160, the NDK twin of ArkUIApp's
 * model/CalendarCatalogue.ets. Keep the two lists identical.
 *
 * Android builds this at runtime from ICU: `supportedCalendars` mapped through
 * `ULocale.getDisplayKeywordValue`, sorted BY DISPLAY NAME, then "None" inserted at 0 and
 * "Regional preference (…)" at 1. Neither port can reach ICU's keyword display names, so the
 * English names were read off the reference's own dialog with `uiautomator dump` rather than
 * invented — all twelve rows verbatim.
 */
#ifndef BREEZY_DATA_CALENDARCATALOGUE_H
#define BREEZY_DATA_CALENDARCATALOGUE_H

#include <string>
#include <vector>

namespace breezy {

struct CalendarOption {
    /** The stored value: "none", "" for the regional preference, or a Unicode calendar key. */
    std::string id;
    std::string name;
};

/**
 * The twelve rows, in CalendarHelper's order.
 *
 * `language` and `country` are the device's locale, which decides what the second row's
 * parenthesised name is — legitimately different between devices, like the geocoder's district.
 */
std::vector<CalendarOption> CalendarOptions(const std::string& language,
                                            const std::string& country);

/** The row summary for a stored value. */
std::string CalendarSummary(const std::string& stored, const std::string& language,
                            const std::string& country);

}  // namespace breezy

#endif  // BREEZY_DATA_CALENDARCATALOGUE_H
