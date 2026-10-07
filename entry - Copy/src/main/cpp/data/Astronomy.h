/*
 * Sun/moon rise and set times, and the moon phase.
 *
 * Straight port of ArkUIApp's data/Astronomy.ets, which in turn follows commons-suncalc:
 * Montenbruck & Pfleger's abbreviated solar and lunar series ("MiniSun" / "MiniMoon"), with
 * rise/set found by scanning the day hour by hour and interpolating each three-point window with
 * a quadratic. Open-Meteo returns none of these values, so both ports compute them, and they must
 * compute them the same way or the two apps disagree on screen.
 *
 * Kotlin origins: CommonConverter.kt getCalculatedAstroSun kt:503-560, getCalculatedAstroMoon
 * kt:562-616, getCalculatedMoonPhase kt:621-632.
 */
#ifndef BREEZY_DATA_ASTRONOMY_H
#define BREEZY_DATA_ASTRONOMY_H

#include <cstdint>
#include <string>

namespace breezy {

/** An equatorial position: right ascension and declination in radians, distance in km. */
struct EquatorialPosition {
    double rightAscension;
    double declination;
    double distanceKm;
};

/** The outcome of a rise/set search: instants in epoch ms, or a flag that the body never crosses. */
struct RiseSet {
    int64_t rise = 0;
    int64_t set = 0;
    bool hasRise = false;
    bool hasSet = false;
    bool alwaysUp = false;
    bool alwaysDown = false;
};

/** A resolved rise/set pair after the Android fallbacks have been applied. */
struct AstroPair {
    int64_t rise = 0;
    int64_t set = 0;
    bool hasRise = false;
    bool hasSet = false;
};

EquatorialPosition SunPosition(double mjd);
EquatorialPosition MoonPosition(double mjd);

AstroPair CalculatedSun(int64_t instantMs, double latitude, double longitude,
                        int utcOffsetSeconds);
AstroPair CalculatedMoon(int64_t instantMs, double latitude, double longitude,
                         int utcOffsetSeconds);
/** `Daily.twilight` — the sun at the CIVIL horizon (-6 deg): dawn is its rise, dusk its set. */
AstroPair CalculatedCivilTwilight(int64_t instantMs, double latitude, double longitude,
                                  int utcOffsetSeconds);

/**
 * The raw 24-hour crossing scan for a given LOCAL MIDNIGHT, without the display fallbacks
 * `CalculatedSun` / `CalculatedMoon` apply.
 *
 * Screen 4's EphemerisChart walks several consecutive days' rise and set instants to place its
 * noon/nadir samples (DetailsSunMoon.kt:243-263), which is a different question from "what pair
 * should today's card show".
 */
RiseSet SunRiseSetOn(int64_t dayStartMs, double latitude, double longitude);
RiseSet MoonRiseSetOn(int64_t dayStartMs, double latitude, double longitude);

/** The body's GEOMETRIC altitude above the horizon, in degrees — the ephemeris chart's y value. */
double SunAltitudeDegrees(int64_t instantMs, double latitude, double longitude);
double MoonAltitudeDegrees(int64_t instantMs, double latitude, double longitude);

/** 0..360, 0 = new moon, 90 = first quarter, 180 = full, 270 = last quarter. */
int MoonPhaseAngle(int64_t instantMs, int utcOffsetSeconds);
/** strings.xml's ephemeris_moon_phase_* values. */
std::string MoonPhaseName(int angle);

}  // namespace breezy

#endif  // BREEZY_DATA_ASTRONOMY_H
