#include "data/Astronomy.h"

#include <cmath>

#include "common/LocalTime.h"

namespace breezy {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kPi2 = 2.0 * kPi;
constexpr double kDeg = kPi / 180.0;
/** Radians per arcsecond, the unit the lunar series is tabulated in. */
constexpr double kArcs = 3600.0 * 180.0 / kPi;
/** Modified Julian Date of the Unix epoch. */
constexpr double kMjdUnixEpoch = 40587.0;
constexpr double kEarthRadiusKm = 6371.0;
constexpr double kMoonRadiusKm = 1737.1;
constexpr double kSunDistanceKm = 149598000.0;

/**
 * The standard visual horizon: -0.833°, the solar semi-diameter plus refraction. Equivalent to
 * commons-suncalc's default SunTimes.Twilight.VISUAL, which getCalculatedAstroSun uses (kt:507).
 */
constexpr double kSunHorizon = -0.833 * kDeg;

double Frac(double x) {
    return x - std::floor(x);
}

double ToMjd(int64_t instantMs) {
    return static_cast<double>(instantMs) / static_cast<double>(kMillisPerDay) + kMjdUnixEpoch;
}

int64_t FromMjd(double mjd) {
    return static_cast<int64_t>(std::llround((mjd - kMjdUnixEpoch) *
                                             static_cast<double>(kMillisPerDay)));
}

/** Julian centuries since J2000. */
double JulianCentury(double mjd) {
    return (mjd - 51544.5) / 36525.0;
}

/** Mean obliquity of the ecliptic, radians. */
double Obliquity(double t) {
    return (23.43929111 - (46.8150 + (0.00059 - 0.001813 * t) * t) * t / 3600.0) * kDeg;
}

/** Rotates ecliptic (longitude, latitude) into equatorial coordinates. */
EquatorialPosition EclipticToEquatorial(double t, double longitude, double latitude,
                                        double distanceKm) {
    const double eps = Obliquity(t);
    const double cosB = std::cos(latitude);
    const double sinB = std::sin(latitude);
    const double x = cosB * std::cos(longitude);
    const double y = cosB * std::sin(longitude) * std::cos(eps) - sinB * std::sin(eps);
    const double z = cosB * std::sin(longitude) * std::sin(eps) + sinB * std::cos(eps);
    return EquatorialPosition{std::atan2(y, x), std::asin(z), distanceKm};
}

/** Greenwich mean sidereal time, radians (commons-suncalc JulianDate). */
double GreenwichMeanSiderealTime(double mjd) {
    constexpr double kSecondsPerDay = 86400.0;
    const double mjd0 = std::floor(mjd);
    const double ut = (mjd - mjd0) * kSecondsPerDay;
    const double t0 = (mjd0 - 51544.5) / 36525.0;
    const double t = (mjd - 51544.5) / 36525.0;
    const double gmst = 24110.54841 + 8640184.812866 * t0 + 1.0027379093 * ut +
                        (0.093104 - 6.2e-6 * t) * t * t;
    const double wrapped = std::fmod(std::fmod(gmst, kSecondsPerDay) + kSecondsPerDay,
                                     kSecondsPerDay);
    return (kPi2 / kSecondsPerDay) * wrapped;
}

/** Geometric altitude above the horizon, radians. */
double AltitudeOf(const EquatorialPosition& position, double mjd, double latitude,
                  double longitude) {
    const double hourAngle =
        GreenwichMeanSiderealTime(mjd) + longitude * kDeg - position.rightAscension;
    const double phi = latitude * kDeg;
    return std::asin(std::sin(phi) * std::sin(position.declination) +
                     std::cos(phi) * std::cos(position.declination) * std::cos(hourAngle));
}

double SunHeight(double mjd, double latitude, double longitude) {
    return AltitudeOf(SunPosition(mjd), mjd, latitude, longitude) - kSunHorizon;
}

/**
 * `SunTimes.Twilight.CIVIL` — the sun 6 degrees below the horizon.
 *
 * `Daily.twilight` is `getCalculatedAstroSun(..., SunTimes.Twilight.CIVIL)`
 * (CommonConverter.kt), and it is the second row of the Sun & moon card: dawn and dusk.
 */
constexpr double kCivilTwilightHorizon = -6.0 * kDeg;

double CivilTwilightHeight(double mjd, double latitude, double longitude) {
    return AltitudeOf(SunPosition(mjd), mjd, latitude, longitude) - kCivilTwilightHorizon;
}

/**
 * The moon's threshold is distance-dependent: horizontal parallax lifts it, refraction and its
 * own semi-diameter lower it (commons-suncalc MoonTimes.correctedMoonHeight).
 */
double MoonHeight(double mjd, double latitude, double longitude) {
    const EquatorialPosition position = MoonPosition(mjd);
    const double parallax = std::asin(kEarthRadiusKm / position.distanceKm);
    const double refraction = 0.5667 * kDeg;
    const double semiDiameter = std::asin(kMoonRadiusKm / position.distanceKm);
    return AltitudeOf(position, mjd, latitude, longitude) - (parallax - refraction - semiDiameter);
}

using HeightFn = double (*)(double mjd, double latitude, double longitude);

/**
 * Scans the 24 hours from `startMs` for horizon crossings.
 *
 * Each odd hour is the centre of a three-point window; fitting a quadratic through the altitudes
 * at h-1, h, h+1 and solving for its roots gives the crossing to well under a minute. Unlike a
 * closed-form solution it copes with a body whose declination moves appreciably during the day —
 * the moon always, and the sun at high latitudes.
 */
RiseSet ScanRiseSet(int64_t startMs, double latitude, double longitude, HeightFn heightAt) {
    RiseSet result;
    const double startMjd = ToMjd(startMs);
    bool foundRise = false;
    bool foundSet = false;
    double riseHour = 0.0;
    double setHour = 0.0;
    double lastYe = 0.0;

    double yMinus = heightAt(startMjd, latitude, longitude);
    for (int hour = 1; hour <= 24; hour += 2) {
        const double y0 = heightAt(startMjd + hour / 24.0, latitude, longitude);
        const double yPlus = heightAt(startMjd + (hour + 1) / 24.0, latitude, longitude);

        const double a = (yPlus + yMinus) / 2.0 - y0;
        const double b = (yPlus - yMinus) / 2.0;
        const double xe = -b / (2.0 * a);
        const double ye = (a * xe + b) * xe + y0;
        lastYe = ye;

        const double discriminant = b * b - 4.0 * a * y0;
        int roots = 0;
        double x1 = 0.0;
        double x2 = 0.0;
        if (discriminant >= 0.0) {
            const double dx = std::sqrt(discriminant) / (std::fabs(a) * 2.0);
            x1 = xe - dx;
            x2 = xe + dx;
            if (std::fabs(x1) <= 1.0) {
                ++roots;
            }
            if (std::fabs(x2) <= 1.0) {
                ++roots;
            }
            if (x1 < -1.0) {
                x1 = x2;
            }
        }

        if (roots == 1) {
            if (yMinus < 0.0) {
                if (!foundRise) {
                    riseHour = hour + x1;
                    foundRise = true;
                }
            } else if (!foundSet) {
                setHour = hour + x1;
                foundSet = true;
            }
        } else if (roots == 2) {
            if (!foundRise) {
                riseHour = hour + (ye < 0.0 ? x2 : x1);
                foundRise = true;
            }
            if (!foundSet) {
                setHour = hour + (ye < 0.0 ? x1 : x2);
                foundSet = true;
            }
        }

        if (foundRise && foundSet) {
            break;
        }
        yMinus = yPlus;
    }

    if (foundRise) {
        result.rise = FromMjd(startMjd + riseHour / 24.0);
        result.hasRise = true;
    }
    if (foundSet) {
        result.set = FromMjd(startMjd + setHour / 24.0);
        result.hasSet = true;
    }
    if (!foundRise && !foundSet) {
        if (lastYe > 0.0) {
            result.alwaysUp = true;
        } else {
            result.alwaysDown = true;
        }
    }
    return result;
}

/**
 * getCalculatedAstroSun / getCalculatedAstroMoon (kt:503-616) — identical fallback ladders:
 *   - never sets            → 00:00 to 23:59:59.999
 *   - no rise but a set     → 00:00 as the rise ("leaving midnight sun season", kt:518)
 *   - set before rise       → recompute the set by scanning forward from the rise INSTANT
 *   - a rise but no set     → same recompute, the set is beyond the window
 */
AstroPair Resolve(const RiseSet& times, int64_t dayStartMs, double latitude, double longitude,
                  HeightFn heightAt) {
    AstroPair pair;

    if (times.alwaysUp) {
        pair.rise = dayStartMs;
        pair.set = dayStartMs + kMillisPerDay - 1;
        pair.hasRise = true;
        pair.hasSet = true;
        return pair;
    }
    if (!times.hasRise && times.hasSet) {
        pair.rise = dayStartMs;
        pair.set = times.set;
        pair.hasRise = true;
        pair.hasSet = true;
        return pair;
    }
    // kt:526-543 / kt:584-600 and kt:547-554 / kt:604-610 — both branches scan forward from the
    // rise itself, not from that day's midnight, or the rescan returns the set it just rejected.
    if (times.hasRise && (!times.hasSet || times.set < times.rise)) {
        const RiseSet next = ScanRiseSet(times.rise, latitude, longitude, heightAt);
        pair.rise = times.rise;
        pair.hasRise = true;
        pair.set = next.set;
        pair.hasSet = next.hasSet;
        return pair;
    }

    pair.rise = times.rise;
    pair.hasRise = times.hasRise;
    pair.set = times.set;
    pair.hasSet = times.hasSet;
    return pair;
}

}  // namespace

EquatorialPosition SunPosition(double mjd) {
    const double t = JulianCentury(mjd);
    const double m = kPi2 * Frac(0.993133 + 99.997361 * t);
    const double dL = 6893.0 * std::sin(m) + 72.0 * std::sin(2.0 * m);
    const double longitude = kPi2 * Frac(0.7859453 + m / kPi2 + (6191.2 * t + dL) / 1296.0e3);
    const double distance = kSunDistanceKm * (1.0 - 0.016718 * std::cos(m));
    return EclipticToEquatorial(t, longitude, 0.0, distance);
}

EquatorialPosition MoonPosition(double mjd) {
    const double t = JulianCentury(mjd);
    const double l0 = Frac(0.606433 + 1336.855225 * t);        // mean longitude, revolutions
    const double l = kPi2 * Frac(0.374897 + 1325.552410 * t);  // moon's mean anomaly
    const double ls = kPi2 * Frac(0.993133 + 99.997361 * t);   // sun's mean anomaly
    const double d = kPi2 * Frac(0.827361 + 1236.853086 * t);  // mean elongation
    const double f = kPi2 * Frac(0.259086 + 1342.227825 * t);  // argument of latitude
    const double d2 = 2.0 * d;
    const double l2 = 2.0 * l;
    const double f2 = 2.0 * f;

    // Perturbations in ecliptic longitude, arcseconds.
    const double dL = 22640.0 * std::sin(l)
        - 4586.0 * std::sin(l - d2)
        + 2370.0 * std::sin(d2)
        + 769.0 * std::sin(l2)
        - 668.0 * std::sin(ls)
        - 412.0 * std::sin(f2)
        - 212.0 * std::sin(l2 - d2)
        - 206.0 * std::sin(l + ls - d2)
        + 192.0 * std::sin(l + d2)
        - 165.0 * std::sin(ls - d2)
        - 125.0 * std::sin(d)
        - 110.0 * std::sin(l + ls)
        + 148.0 * std::sin(l - ls)
        - 55.0 * std::sin(f2 - d2);

    const double s = f + (dL + 412.0 * std::sin(f2) + 541.0 * std::sin(ls)) / kArcs;
    const double h = f - d2;
    const double n = -526.0 * std::sin(h)
        + 44.0 * std::sin(l + h)
        - 31.0 * std::sin(-l + h)
        - 23.0 * std::sin(ls + h)
        + 11.0 * std::sin(-ls + h)
        - 25.0 * std::sin(-l2 + f)
        + 21.0 * std::sin(-l + f);

    const double longitude = kPi2 * Frac(l0 + dL / 1296.0e3);
    const double latitude = (18520.0 * std::sin(s) + n) / kArcs;
    const double distance = 385000.5768
        - 20905.355 * std::cos(l)
        - 3699.111 * std::cos(d2 - l)
        - 2955.968 * std::cos(d2)
        - 569.925 * std::cos(l2);

    return EclipticToEquatorial(t, longitude, latitude, distance);
}

AstroPair CalculatedSun(int64_t instantMs, double latitude, double longitude,
                        int utcOffsetSeconds) {
    const int64_t dayStart = LocalStartOfDay(instantMs, utcOffsetSeconds);
    return Resolve(ScanRiseSet(dayStart, latitude, longitude, SunHeight), dayStart, latitude,
                   longitude, SunHeight);
}

/** `Daily.twilight` — the same ladder at the civil horizon. Dawn is its rise, dusk its set. */
AstroPair CalculatedCivilTwilight(int64_t instantMs, double latitude, double longitude,
                                  int utcOffsetSeconds) {
    const int64_t dayStart = LocalStartOfDay(instantMs, utcOffsetSeconds);
    return Resolve(ScanRiseSet(dayStart, latitude, longitude, CivilTwilightHeight), dayStart,
                   latitude, longitude, CivilTwilightHeight);
}

AstroPair CalculatedMoon(int64_t instantMs, double latitude, double longitude,
                         int utcOffsetSeconds) {
    const int64_t dayStart = LocalStartOfDay(instantMs, utcOffsetSeconds);
    return Resolve(ScanRiseSet(dayStart, latitude, longitude, MoonHeight), dayStart, latitude,
                   longitude, MoonHeight);
}

/*
 * The plain 24-hour scan for a given local midnight, WITHOUT the Android fallbacks `Resolve`
 * applies — screen 4's EphemerisChart needs the raw crossings of several consecutive days to walk
 * its noon/nadir list (DetailsSunMoon.kt:243-263), not a display pair for one of them.
 */
RiseSet SunRiseSetOn(int64_t dayStartMs, double latitude, double longitude) {
    return ScanRiseSet(dayStartMs, latitude, longitude, SunHeight);
}

RiseSet MoonRiseSetOn(int64_t dayStartMs, double latitude, double longitude) {
    return ScanRiseSet(dayStartMs, latitude, longitude, MoonHeight);
}

/**
 * `SunPosition.compute().on(date).at(lat, lon).execute().altitude` in DEGREES (kt:324), and the
 * lunar equivalent (kt:326). The GEOMETRIC altitude, with no horizon correction — that belongs to
 * a rise/set search, not to plotting where the body is.
 */
double SunAltitudeDegrees(int64_t instantMs, double latitude, double longitude) {
    const double mjd = ToMjd(instantMs);
    return AltitudeOf(SunPosition(mjd), mjd, latitude, longitude) / kDeg;
}

double MoonAltitudeDegrees(int64_t instantMs, double latitude, double longitude) {
    const double mjd = ToMjd(instantMs);
    return AltitudeOf(MoonPosition(mjd), mjd, latitude, longitude) / kDeg;
}

int MoonPhaseAngle(int64_t instantMs, int utcOffsetSeconds) {
    // kt:626 — the illumination is evaluated at MIDDAY, in the LOCATION's zone.
    const double mjd = ToMjd(LocalStartOfDay(instantMs, utcOffsetSeconds)) + 0.5;
    const EquatorialPosition sun = SunPosition(mjd);
    const EquatorialPosition moon = MoonPosition(mjd);

    // Geocentric elongation of the moon from the sun.
    const double elongation = std::acos(
        std::sin(sun.declination) * std::sin(moon.declination) +
        std::cos(sun.declination) * std::cos(moon.declination) *
            std::cos(sun.rightAscension - moon.rightAscension));
    // Phase angle at the moon: sun–moon–observer.
    const double inc = std::atan2(sun.distanceKm * std::sin(elongation),
                                  moon.distanceKm - sun.distanceKm * std::cos(elongation));
    // Sign: which limb is lit, i.e. waxing or waning.
    const double limb = std::atan2(
        std::cos(sun.declination) * std::sin(sun.rightAscension - moon.rightAscension),
        std::sin(sun.declination) * std::cos(moon.declination) -
            std::cos(sun.declination) * std::sin(moon.declination) *
                std::cos(sun.rightAscension - moon.rightAscension));

    const double fraction = 0.5 + 0.5 * inc * (limb < 0.0 ? -1.0 : 1.0) / kPi;
    const int degrees = static_cast<int>(std::llround(fraction * 360.0));
    return ((degrees % 360) + 360) % 360;
}

std::string MoonPhaseName(int angle) {
    // The boundaries are the half-degrees of commons-suncalc's Phase.toPhase; doubling keeps them
    // exact in integers rather than rounding a bucket edge onto the wrong phase.
    const int twice = 2 * (((angle % 360) + 360) % 360);
    if (twice < 45) {
        return "New moon";
    }
    if (twice < 135) {
        return "Waxing crescent";
    }
    if (twice < 225) {
        return "First quarter";
    }
    if (twice < 315) {
        return "Waxing gibbous";
    }
    if (twice < 405) {
        return "Full moon";
    }
    if (twice < 495) {
        return "Waning gibbous";
    }
    if (twice < 585) {
        return "Last quarter";
    }
    if (twice < 675) {
        return "Waning crescent";
    }
    return "New moon";
}

}  // namespace breezy
