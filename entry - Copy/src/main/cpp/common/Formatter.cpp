#include "common/Formatter.h"

#include <cmath>
#include <vector>

#include "common/LocalTime.h"
#include "common/Units.h"

namespace breezy {

const char* const kColonSeparator = ": ";
const char* const kDotSeparator = " • ";
const char* const kLocaleSeparator = ", ";

namespace {

/** Abbreviated month names — the `MMM` of a medium date skeleton, English only for now. */
constexpr const char* kShortMonths[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

std::string Plural(long long count, const char* singular, const char* plural) {
    return count == 1 ? std::string("1 ") + singular
                      : std::to_string(count) + " " + plural;
}

/** The half-day temperature, or nothing when that half is absent or has none. */
Maybe HalfDayTemperature(const HalfDay& half) {
    return half.present ? half.temperature.temperature : Maybe{};
}

}  // namespace

std::string MediumDayAndMonth(int64_t instantMs, int utcOffsetSeconds) {
    const int month = LocalMonth(instantMs, utcOffsetSeconds);
    const int day = LocalDayOfMonth(instantMs, utcOffsetSeconds);
    if (month < 0 || month > 11) {
        return std::to_string(day);
    }
    return std::string(kShortMonths[month]) + " " + std::to_string(day);
}

std::string FormatRelativeTime(int64_t instantMs, int64_t nowMs) {
    const int64_t minutes = (nowMs - instantMs) / kMillisPerMinute;
    if (minutes < 1) {
        return "Just now";
    }
    if (minutes < 60) {
        return Plural(minutes, "minute ago", "minutes ago");
    }
    const int64_t hours = minutes / 60;
    if (hours < 24) {
        return Plural(hours, "hour ago", "hours ago");
    }
    return Plural(hours / 24, "day ago", "days ago");
}

std::string HourLabel(int64_t instantMs, int utcOffsetSeconds, bool is24Hour) {
    const int hours = LocalHours(instantMs, utcOffsetSeconds);
    if (is24Hour) {
        const int minutes = LocalMinutes(instantMs, utcOffsetSeconds);
        return std::to_string(hours) + ":" + (minutes < 10 ? "0" : "") + std::to_string(minutes);
    }
    const int display = hours % 12 == 0 ? 12 : hours % 12;
    return std::to_string(display) + (hours < 12 ? " AM" : " PM");
}

std::string ClockTime(int64_t instantMs, int utcOffsetSeconds, bool is24Hour) {
    const int hours = LocalHours(instantMs, utcOffsetSeconds);
    const int minutes = LocalMinutes(instantMs, utcOffsetSeconds);
    const std::string mm = (minutes < 10 ? "0" : "") + std::to_string(minutes);
    if (is24Hour) {
        return (hours < 10 ? "0" : "") + std::to_string(hours) + ":" + mm;
    }
    const int display = hours % 12 == 0 ? 12 : hours % 12;
    return std::to_string(display) + ":" + mm + (hours < 12 ? " AM" : " PM");
}

std::string FormatDurationShort(int64_t millis) {
    const long long totalMinutes = static_cast<long long>(std::llround(
        static_cast<double>(millis) / 60000.0));
    const long long hours = totalMinutes / 60;
    const long long minutes = totalMinutes - hours * 60;
    if (hours == 0) {
        return std::to_string(minutes) + "m";
    }
    return std::to_string(hours) + "h " + std::to_string(minutes) + "m";
}

std::string TemperatureRangeSummary(const Weather& weather, int64_t nowMs, int utcOffsetSeconds) {
    if (weather.todayIndex < 0 ||
        weather.todayIndex >= static_cast<int>(weather.dailyForecast.size())) {
        return {};
    }
    const Daily& today = weather.dailyForecast[static_cast<size_t>(weather.todayIndex)];

    const int currentHour = LocalHours(nowMs, utcOffsetSeconds);
    bool isDayFirst = false;
    Maybe first;
    Maybe second;

    const auto dayAt = [&weather](int index) -> const Daily* {
        if (index < 0 || index >= static_cast<int>(weather.dailyForecast.size())) {
            return nullptr;
        }
        return &weather.dailyForecast[static_cast<size_t>(index)];
    };

    if (currentHour < 6) {
        isDayFirst = false;
        const Daily* yesterday = dayAt(weather.todayIndex - 1);
        first = yesterday != nullptr ? HalfDayTemperature(yesterday->night) : Maybe{};
        second = HalfDayTemperature(today.day);
    } else if (currentHour < 18) {
        isDayFirst = true;
        first = HalfDayTemperature(today.day);
        second = HalfDayTemperature(today.night);
    } else {
        isDayFirst = false;
        first = HalfDayTemperature(today.night);
        const Daily* tomorrow = dayAt(weather.todayIndex + 1);
        second = tomorrow != nullptr ? HalfDayTemperature(tomorrow->day) : Maybe{};
    }

    std::vector<std::string> parts;
    if (first.has_value()) {
        parts.push_back(std::string(isDayFirst ? "Day" : "Night") + kColonSeparator +
                        FormatTemperatureNarrow(*first));
    }
    if (second.has_value()) {
        parts.push_back(std::string(isDayFirst ? "Night" : "Day") + kColonSeparator +
                        FormatTemperatureNarrow(*second));
    }
    if (parts.empty()) {
        return {};
    }
    if (parts.size() == 1) {
        return parts[0];
    }
    return parts[0] + kDotSeparator + parts[1];
}

std::string FeelsLikeText(const Temperature& temperature) {
    if (!temperature.feelsLikeTemperature.has_value()) {
        return {};
    }
    // The comparison happens in the DISPLAY unit: two Celsius values a degree apart can round to
    // the same Fahrenheit value, and the Kotlin hides the line whenever the shown numbers match.
    const long long shown =
        std::llround(TemperatureValue(*temperature.feelsLikeTemperature));
    if (temperature.temperature.has_value() &&
        std::llround(TemperatureValue(*temperature.temperature)) == shown) {
        return {};
    }
    // strings.xml:56 — "Feels like %s"
    return "Feels like " + std::to_string(shown) + "°";
}

}  // namespace breezy
