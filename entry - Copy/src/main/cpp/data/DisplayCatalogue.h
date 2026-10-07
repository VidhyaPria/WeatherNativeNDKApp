/*
 * The three ordered display lists screen 3c's "Displayed data" section edits — the C++ twin of
 * ArkUIApp's model/DisplayCatalogue.ets.
 *
 *   Blocks         CardDisplay.kt:32-53          — which cards screen 1 stacks, in order
 *   Daily trends   DailyTrendDisplay.kt:29-36    — the daily trend card's tabs, in order
 *   Hourly trends  HourlyTrendDisplay.kt:29-39   — the hourly trend card's tabs, in order
 *
 * Ids and defaults are the Kotlin's verbatim (SettingsManager.kt:61-90); names are strings.xml's.
 * Kept identical to the ArkTS table so both ports store the same "&"-joined strings under the same
 * keys and can be diffed.
 *
 * One Kotlin quirk that differs per list: an EMPTY stored value means "no cards" for Blocks
 * (CardDisplay.kt:58-70) but "every tab" for the two trend lists (DailyTrendDisplay.kt:40-52).
 */

#ifndef BREEZY_DATA_DISPLAY_CATALOGUE_H
#define BREEZY_DATA_DISPLAY_CATALOGUE_H

#include <string>
#include <vector>

namespace breezy {

enum class DisplayList { Blocks = 0, Daily = 1, Hourly = 2 };

struct DisplayItem {
    std::string id;
    std::string name;
    /** CardDisplay.configure (kt:29) — the gear. "daily" / "hourly", or empty for no gear. */
    std::string configures;
};

const std::vector<DisplayItem>& DisplayCatalogue(DisplayList list);
/** strings.xml:805-807. */
std::string DisplayListTitle(DisplayList list);
/** SettingsManager.kt:61-90. */
std::string DisplayListDefault(DisplayList list);
/** The preferences key: card_display / daily_trend_display / hourly_trend_display. */
std::string DisplayListKey(DisplayList list);
/** The id the shell and C++ pass this list by: "blocks" / "daily" / "hourly". */
std::string DisplayListId(DisplayList list);
DisplayList DisplayListFromId(const std::string& id);

/** `value.split("&").mapNotNull { entries.firstOrNull { it.id == id } }`. */
std::vector<DisplayItem> ParseDisplayList(DisplayList list, const std::string& value);
/** `list.joinToString("&") { it.id }`. */
std::string DisplayListToValue(const std::vector<DisplayItem>& items);
/** The catalogue entries NOT in `items`, in catalogue order — the chip row (kt:144-156). */
std::vector<DisplayItem> DisplayListRemainder(DisplayList list,
                                              const std::vector<DisplayItem>& items);
/** getSummary (kt:78-82) — the names joined by ", ". */
std::string DisplayListSummary(const std::vector<DisplayItem>& items);

}  // namespace breezy

#endif  // BREEZY_DATA_DISPLAY_CATALOGUE_H
