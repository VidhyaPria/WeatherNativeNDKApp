#include "common/LocalTime.h"

namespace breezy {
namespace {

/** The instant shifted so that reading it as UTC gives the location's wall clock. */
int64_t Shifted(int64_t instantMs, int utcOffsetSeconds) {
    return instantMs + static_cast<int64_t>(utcOffsetSeconds) * kMillisPerSecond;
}

/** Floor division — C++ truncates toward zero, which is wrong for pre-1970 instants. */
int64_t FloorDiv(int64_t value, int64_t divisor) {
    const int64_t quotient = value / divisor;
    return (value % divisor != 0 && ((value < 0) != (divisor < 0))) ? quotient - 1 : quotient;
}

int64_t FloorMod(int64_t value, int64_t divisor) {
    return value - FloorDiv(value, divisor) * divisor;
}

/**
 * Civil date from a day number, days since 1970-01-01. Howard Hinnant's civil_from_days —
 * the same algorithm the standard library's own calendar support is built on.
 */
void CivilFromDays(int64_t days, int& year, int& month, int& day) {
    days += 719468;
    const int64_t era = FloorDiv(days, 146097);
    const int64_t dayOfEra = days - era * 146097;                                  // 0..146096
    const int64_t yearOfEra =
        (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;  // 0..399
    const int64_t y = yearOfEra + era * 400;
    const int64_t dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
    const int64_t mp = (5 * dayOfYear + 2) / 153;                                  // 0..11
    const int64_t d = dayOfYear - (153 * mp + 2) / 5 + 1;                          // 1..31
    const int64_t m = mp < 10 ? mp + 3 : mp - 9;                                   // 1..12
    year = static_cast<int>(y + (m <= 2 ? 1 : 0));
    month = static_cast<int>(m);
    day = static_cast<int>(d);
}

}  // namespace

int LocalHours(int64_t instantMs, int utcOffsetSeconds) {
    return static_cast<int>(FloorMod(Shifted(instantMs, utcOffsetSeconds), kMillisPerDay) /
                            kMillisPerHour);
}

int LocalMinutes(int64_t instantMs, int utcOffsetSeconds) {
    return static_cast<int>(FloorMod(Shifted(instantMs, utcOffsetSeconds), kMillisPerHour) /
                            kMillisPerMinute);
}

int LocalDayOfMonth(int64_t instantMs, int utcOffsetSeconds) {
    int year = 0;
    int month = 0;
    int day = 0;
    CivilFromDays(FloorDiv(Shifted(instantMs, utcOffsetSeconds), kMillisPerDay), year, month, day);
    return day;
}

int LocalMonth(int64_t instantMs, int utcOffsetSeconds) {
    int year = 0;
    int month = 0;
    int day = 0;
    CivilFromDays(FloorDiv(Shifted(instantMs, utcOffsetSeconds), kMillisPerDay), year, month, day);
    return month - 1;  // JavaScript's getUTCMonth is 0-based, and the ArkTS port matches it.
}

int LocalDayOfWeek(int64_t instantMs, int utcOffsetSeconds) {
    // 1970-01-01 was a Thursday, so day 0 is weekday 4 with Sunday as 0.
    const int64_t days = FloorDiv(Shifted(instantMs, utcOffsetSeconds), kMillisPerDay);
    return static_cast<int>(FloorMod(days + 4, 7));
}

int64_t LocalStartOfDay(int64_t instantMs, int utcOffsetSeconds) {
    const int64_t local = Shifted(instantMs, utcOffsetSeconds);
    const int64_t midnightLocal = FloorDiv(local, kMillisPerDay) * kMillisPerDay;
    return midnightLocal - static_cast<int64_t>(utcOffsetSeconds) * kMillisPerSecond;
}

int64_t LocalStartOfHour(int64_t instantMs, int utcOffsetSeconds) {
    const int64_t local = Shifted(instantMs, utcOffsetSeconds);
    const int64_t hourLocal = FloorDiv(local, kMillisPerHour) * kMillisPerHour;
    return hourLocal - static_cast<int64_t>(utcOffsetSeconds) * kMillisPerSecond;
}

}  // namespace breezy
