/*
 * Reading instants in the LOCATION's timezone rather than the device's.
 *
 * The ArkTS port (common/LocalTime.ets) explains the why: Android threads location.timeZone
 * through every date it touches, and Open-Meteo hands back utc_offset_seconds for the resolved
 * grid cell, so the whole conversion is arithmetic — shift the instant by the offset, then read
 * the UTC fields.
 *
 * In C++ an instant is an int64_t of milliseconds since the Unix epoch, which makes "truncate to
 * local midnight" a floor division rather than a Date.UTC round trip.
 */
#ifndef BREEZY_COMMON_LOCALTIME_H
#define BREEZY_COMMON_LOCALTIME_H

#include <cstdint>

namespace breezy {

constexpr int64_t kMillisPerSecond = 1000;
constexpr int64_t kMillisPerMinute = 60000;
constexpr int64_t kMillisPerHour = 3600000;
constexpr int64_t kMillisPerDay = 86400000;

/** Hour of day, 0..23, at the location. */
int LocalHours(int64_t instantMs, int utcOffsetSeconds);
/** Minute of the hour at the location. */
int LocalMinutes(int64_t instantMs, int utcOffsetSeconds);
/** Day of the month at the location. */
int LocalDayOfMonth(int64_t instantMs, int utcOffsetSeconds);
/** Month, 0..11, at the location. */
int LocalMonth(int64_t instantMs, int utcOffsetSeconds);
/** Day of the week, 0 = Sunday, at the location. */
int LocalDayOfWeek(int64_t instantMs, int utcOffsetSeconds);

/** The instant of local midnight starting the day this instant falls in. */
int64_t LocalStartOfDay(int64_t instantMs, int utcOffsetSeconds);
/** The instant of local midnight starting the hour this instant falls in. */
int64_t LocalStartOfHour(int64_t instantMs, int utcOffsetSeconds);

}  // namespace breezy

#endif  // BREEZY_COMMON_LOCALTIME_H
