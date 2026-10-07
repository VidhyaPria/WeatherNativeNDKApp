/*
 * Text the header shows, ported from ArkUIApp's common/Formatter.ets and
 * viewmodel/MainViewModel.ets.
 */
#ifndef BREEZY_COMMON_FORMATTER_H
#define BREEZY_COMMON_FORMATTER_H

#include <cstdint>
#include <string>

#include "model/Weather.h"

namespace breezy {

/** strings.xml:1300 */
extern const char* const kColonSeparator;
/** strings.xml:1301 — a bullet padded with spaces. */
extern const char* const kDotSeparator;

/** TextRelativeClock.kt — the "x minutes ago" line at the top of the header. */
std::string FormatRelativeTime(int64_t instantMs, int64_t nowMs);

/** DateExtensions.kt:198-205 — "9 PM" on a 12-hour device, "21:00" on a 24-hour one. */
std::string HourLabel(int64_t instantMs, int utcOffsetSeconds, bool is24Hour);
/** The same clock with its minutes — "5:11 AM", "05:11". */
std::string ClockTime(int64_t instantMs, int utcOffsetSeconds, bool is24Hour);

/**
 * `Duration.formatTime` at its default smallest unit — "16h 2m".
 *
 * The Sun & moon card appends the daylight duration to its sunrise/sunset row
 * (DetailsSunMoon.kt:360).
 */
std::string FormatDurationShort(int64_t millis);

/** `getFormattedMediumDayAndMonth` — "Aug 7", the day half of an alert's date range. */
std::string MediumDayAndMonth(int64_t instantMs, int utcOffsetSeconds);
/** `locale_separator` — ", " in English. */
extern const char* const kLocaleSeparator;

/**
 * Weather.kt:94-160 — "Day: 30° • Night: 14°", empty when there is nothing to show.
 * Before 06:00 shows yesterday night then today day; before 18:00 today day then night;
 * otherwise today night then tomorrow day. The boundaries are the LOCATION's.
 */
std::string TemperatureRangeSummary(const Weather& weather, int64_t nowMs, int utcOffsetSeconds);

/**
 * HeaderViewHolder.kt:109-135 — "Feels like 21°", empty when it rounds to the same value as the
 * actual temperature IN THE DISPLAY UNIT.
 */
std::string FeelsLikeText(const Temperature& temperature);

}  // namespace breezy

#endif  // BREEZY_COMMON_FORMATTER_H
