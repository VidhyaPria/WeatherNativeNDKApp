#include "DisplayCatalogue.h"

#include <cstddef>

namespace breezy {
namespace {

/** CardDisplay.kt:32-53. */
std::vector<DisplayItem> BuildCards() {
    return {
        {"nowcast", "Precipitation nowcasting", ""},
        {"daily_forecast", "Daily forecast", "daily"},
        {"hourly_forecast", "Hourly forecast", "hourly"},
        {"precipitation", "Precipitation", ""},
        {"wind", "Wind", ""},
        {"air_quality", "Air quality", ""},
        {"pollen", "Pollen", ""},
        {"humidity", "Humidity", ""},
        {"uv", "UV index", ""},
        {"visibility", "Visibility", ""},
        {"pressure", "Pressure", ""},
        {"sun", "Sun", ""},
        {"moon", "Moon", ""},
        {"clock", "Clock", ""},
    };
}

/** DailyTrendDisplay.kt:29-36. */
std::vector<DisplayItem> BuildDaily() {
    return {
        {"temperature", "Conditions", ""}, {"air_quality", "Air quality", ""},
        {"wind", "Wind", ""},              {"uv_index", "UV index", ""},
        {"precipitation", "Precipitation", ""}, {"sunshine", "Sunshine", ""},
        {"feels_like", "Feels like", ""},
    };
}

/** HourlyTrendDisplay.kt:29-39. */
std::vector<DisplayItem> BuildHourly() {
    return {
        {"temperature", "Conditions", ""},      {"air_quality", "Air quality", ""},
        {"wind", "Wind", ""},                   {"uv_index", "UV index", ""},
        {"precipitation", "Precipitation", ""}, {"feels_like", "Feels like", ""},
        {"humidity", "Humidity / Dew point", ""}, {"pressure", "Pressure", ""},
        {"cloud_cover", "Cloud cover", ""},     {"visibility", "Visibility", ""},
    };
}

}  // namespace

const std::vector<DisplayItem>& DisplayCatalogue(DisplayList list) {
    static const std::vector<DisplayItem> cards = BuildCards();
    static const std::vector<DisplayItem> daily = BuildDaily();
    static const std::vector<DisplayItem> hourly = BuildHourly();
    if (list == DisplayList::Blocks) {
        return cards;
    }
    return list == DisplayList::Daily ? daily : hourly;
}

std::string DisplayListTitle(DisplayList list) {
    if (list == DisplayList::Blocks) {
        return "Blocks";
    }
    return list == DisplayList::Daily ? "Daily trends" : "Hourly trends";
}

std::string DisplayListDefault(DisplayList list) {
    if (list == DisplayList::Blocks) {
        // SettingsManager.kt:61-73 — `clock` is absent, which is why it starts as a chip.
        return "nowcast&daily_forecast&hourly_forecast&precipitation&wind&air_quality&pollen"
               "&humidity&uv&visibility&pressure&sun&moon";
    }
    if (list == DisplayList::Daily) {
        return "temperature&air_quality&wind&uv_index&precipitation&sunshine&feels_like";  // :74-80
    }
    return "temperature&air_quality&wind&uv_index&precipitation&feels_like&humidity&pressure"
           "&cloud_cover&visibility";  // :81-90
}

std::string DisplayListKey(DisplayList list) {
    if (list == DisplayList::Blocks) {
        return "card_display";
    }
    return list == DisplayList::Daily ? "daily_trend_display" : "hourly_trend_display";
}

std::string DisplayListId(DisplayList list) {
    if (list == DisplayList::Blocks) {
        return "blocks";
    }
    return list == DisplayList::Daily ? "daily" : "hourly";
}

DisplayList DisplayListFromId(const std::string& id) {
    if (id == "daily") {
        return DisplayList::Daily;
    }
    return id == "hourly" ? DisplayList::Hourly : DisplayList::Blocks;
}

std::vector<DisplayItem> ParseDisplayList(DisplayList list, const std::string& value) {
    const std::vector<DisplayItem>& catalogue = DisplayCatalogue(list);
    std::vector<DisplayItem> chosen;
    if (value.empty()) {
        // Blocks: an empty value is an empty list; the trend lists: every entry. See the header.
        return list == DisplayList::Blocks ? chosen : catalogue;
    }
    size_t start = 0;
    while (start <= value.size()) {
        size_t end = value.find('&', start);
        if (end == std::string::npos) {
            end = value.size();
        }
        const std::string id = value.substr(start, end - start);
        for (const DisplayItem& item : catalogue) {
            if (item.id == id) {
                chosen.push_back(item);
                break;
            }
        }
        if (end == value.size()) {
            break;
        }
        start = end + 1;
    }
    return chosen;
}

std::string DisplayListToValue(const std::vector<DisplayItem>& items) {
    std::string value;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i > 0) {
            value += "&";
        }
        value += items[i].id;
    }
    return value;
}

std::vector<DisplayItem> DisplayListRemainder(DisplayList list,
                                              const std::vector<DisplayItem>& items) {
    std::vector<DisplayItem> rest;
    for (const DisplayItem& candidate : DisplayCatalogue(list)) {
        bool used = false;
        for (const DisplayItem& item : items) {
            if (item.id == candidate.id) {
                used = true;
                break;
            }
        }
        if (!used) {
            rest.push_back(candidate);
        }
    }
    return rest;
}

std::string DisplayListSummary(const std::vector<DisplayItem>& items) {
    std::string summary;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i > 0) {
            summary += ", ";
        }
        summary += items[i].name;
    }
    return summary;
}

}  // namespace breezy
