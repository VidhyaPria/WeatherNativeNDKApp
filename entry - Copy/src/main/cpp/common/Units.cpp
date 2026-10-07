#include "common/Units.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace breezy {
namespace {

bool InList(std::initializer_list<const char*> countries, const std::string& region) {
    for (const char* country : countries) {
        if (region == country) {
            return true;
        }
    }
    return false;
}

/* The per-quantity tables from Units.ets, each one a getDefaultUnit in the Kotlin. */
bool g_usesFahrenheit = false;
bool g_usesMiles = false;
bool g_usesInHg = false;
bool g_usesMph = false;
bool g_usesMetresPerSecond = false;
bool g_usesInches = false;

/* The stored overrides, "auto" until the shell pushes something else. */
std::string g_temperatureUnit = kUnitAuto;
std::string g_precipitationUnit = kUnitAuto;
std::string g_speedUnit = kUnitAuto;
std::string g_distanceUnit = kUnitAuto;
std::string g_pressureUnit = kUnitAuto;

/**
 * Groups the integer part with a comma every three digits.
 *
 * Android formats every measure through `android.icu.number.NumberFormatter` (WeatherValue.kt:20,
 * :42-49), and ICU groups by locale default — which is why the reference's pressure tile reads
 * "1,002.8 hPa" where both ports read "1002.8". Pressure is the only value in this app that reaches
 * four digits today, but the rule lives in the shared formatter rather than in FormatPressure:
 * grouping is a property of the number formatter in ICU, not of one quantity.
 *
 * Comma because CLAUDE.md scopes this port to English; ArkUIApp's `format` does exactly the same.
 */
std::string GroupThousands(const std::string& text) {
    const bool negative = !text.empty() && text.front() == '-';
    const std::string body = negative ? text.substr(1) : text;
    const size_t dot = body.find('.');
    const std::string whole = dot == std::string::npos ? body : body.substr(0, dot);
    const std::string fraction = dot == std::string::npos ? std::string() : body.substr(dot);
    std::string grouped;
    for (size_t i = 0; i < whole.size(); ++i) {
        // A separator before every digit whose distance from the end is a multiple of three.
        if (i > 0 && (whole.size() - i) % 3 == 0) {
            grouped += ',';
        }
        grouped += whole[i];
    }
    return (negative ? "-" : "") + grouped + fraction;
}

/** Rounds to `decimals` and drops trailing zeros, which is what toString() does in ArkTS. */
std::string Format(double value, int decimals) {
    const double factor = std::pow(10.0, decimals);
    const double rounded = std::round(value * factor) / factor;

    std::string text = std::to_string(rounded);  // always 6 decimals
    const size_t dot = text.find('.');
    if (dot == std::string::npos) {
        // Cannot happen with to_string's fixed form; grouped anyway so the two exits agree.
        return GroupThousands(text);
    }
    // Keep at most `decimals` places, then trim the zeros the fixed format left behind.
    size_t end = std::min(text.size(), dot + (decimals > 0 ? decimals + 1 : 0));
    text = text.substr(0, end);
    if (decimals > 0) {
        while (!text.empty() && text.back() == '0') {
            text.pop_back();
        }
        if (!text.empty() && text.back() == '.') {
            text.pop_back();
        }
    }
    if (text.empty()) {
        return "0";
    }
    return GroupThousands(text);
}

/* ---- the conversions, one per unit, all from the family's reference unit ---- */

double CelsiusFrom(double r) { return r / 10.0; }
double FahrenheitFrom(double r) { return 9.0 / 5.0 * (r / 10.0) + 32.0; }
double KelvinFrom(double r) { return 273.15 + r / 10.0; }

double MillimetreFrom(double r) { return r / 1000.0; }
double CentimetreFrom(double r) { return r / 10000.0; }
double InchFrom(double r) { return r / 25400.0; }

double MetrePerSecondFrom(double r) { return r / 100.0; }
double KilometrePerHourFrom(double r) { return r * 0.036; }
double MilePerHourFrom(double r) { return r / 44.704; }
double KnotFrom(double r) { return r * 0.036 / 1.852; }
double FootPerSecondFrom(double r) { return r / 30.48; }

/** SpeedUnit.kt:23-39 — the Beaufort cut-offs, in the reference unit (cm·s⁻¹). */
double BeaufortFrom(double r) {
    static const double kThresholds[] = {30.0,   160.0,  340.0,  550.0,  800.0,  1080.0,
                                         1390.0, 1720.0, 2080.0, 2450.0, 2850.0, 3270.0};
    for (int i = 0; i < 12; ++i) {
        if (r < kThresholds[i]) {
            return static_cast<double>(i);
        }
    }
    return 12.0;
}

double MetreFrom(double r) { return r; }
double KilometreFrom(double r) { return r / 1000.0; }
double MileFrom(double r) { return r / 1609.344; }
double NauticalMileFrom(double r) { return r / 1852.0; }
double FootFrom(double r) { return r * 3.28084; }

double HectopascalFrom(double r) { return r / 100.0; }
double KilopascalFrom(double r) { return r / 1000.0; }
double MillibarFrom(double r) { return r / 100.0; }
double AtmosphereFrom(double r) { return r / 101325.0; }
double MillimetreOfMercuryFrom(double r) { return r * (760.0 / 101325.0); }
double InchOfMercuryFrom(double r) { return r * (760.0 / 101325.0) / 25.4; }

/*
 * THE UNITS MISSING FROM THESE LISTS ARE EXCLUDED BY THE KOTLIN, not by this port.
 * UnitSettingsScreen.kt filters the reference unit out of four of the five lists — DECI_CELSIUS
 * (kt:70), MICROMETER (kt:106), CENTIMETER_PER_SECOND (kt:158), PASCAL (kt:228). Distance is the
 * one list with no filter (kt:193).
 *
 * The long names are ICU MeasureUnit at UnitWidth.LONG, which cannot be computed here; they were
 * read off the reference Android device's own dialogs. See ArkUIApp's model/UnitCatalogue.ets.
 */
const std::vector<UnitDef> kTemperature = {
    {"c", "degrees Celsius", "°C", 1, &CelsiusFrom},
    {"f", "degrees Fahrenheit", "°F", 0, &FahrenheitFrom},
    {"k", "kelvins", "K", 1, &KelvinFrom},
};

const std::vector<UnitDef> kPrecipitation = {
    {"mm", "millimeters", "mm", 1, &MillimetreFrom},
    {"cm", "centimeters", "cm", 2, &CentimetreFrom},
    {"in", "inches", "in", 2, &InchFrom},
    // Same magnitude as the millimetre — 1 mm of rain IS 1 L·m⁻², which is why they share a factor.
    {"lpsqm", "liters per square meter", "L/m²", 1, &MillimetreFrom},
};

const std::vector<UnitDef> kSpeed = {
    {"mps", "meters per second", "m/s", 1, &MetrePerSecondFrom, "meters/sec"},
    {"kph", "kilometers per hour", "km/h", 1, &KilometrePerHourFrom, "km/hour"},
    {"mph", "miles per hour", "mph", 1, &MilePerHourFrom, "miles/hour"},
    {"kn", "knots", "kn", 1, &KnotFrom},
    {"ftps", "feet per second", "ft/s", 1, &FootPerSecondFrom, "feet/sec"},
    // speed_bf_display_name_short is a literal "Bft" (weather-unit strings.xml), which is also the
    // Beaufort table's FIRST column heading.
    {"bf", "Beaufort", "Bft", 0, &BeaufortFrom},
};

const std::vector<UnitDef> kDistance = {
    {"m", "meters", "m", 0, &MetreFrom},
    {"km", "kilometers", "km", 1, &KilometreFrom},
    {"mi", "miles", "mi", 1, &MileFrom},
    {"nmi", "nautical miles", "nmi", 1, &NauticalMileFrom},
    {"ft", "feet", "ft", 0, &FootFrom},
};

const std::vector<UnitDef> kPressure = {
    {"hpa", "hectopascals", "hPa", 1, &HectopascalFrom},
    {"kpa", "kilopascals", "kPa", 2, &KilopascalFrom},
    {"mb", "millibars", "mbar", 1, &MillibarFrom},
    {"atm", "atmospheres", "atm", 2, &AtmosphereFrom},
    {"mmhg", "millimeters of mercury", "mmHg", 1, &MillimetreOfMercuryFrom},
    {"inhg", "inches of mercury", "inHg", 2, &InchOfMercuryFrom},
};

std::string* OverrideSlot(UnitFamily family) {
    switch (family) {
        case UnitFamily::Temperature:
            return &g_temperatureUnit;
        case UnitFamily::Precipitation:
            return &g_precipitationUnit;
        case UnitFamily::Speed:
            return &g_speedUnit;
        case UnitFamily::Distance:
            return &g_distanceUnit;
        case UnitFamily::Pressure:
        default:
            return &g_pressureUnit;
    }
}

/** Converts a model value into the active unit and formats it at that unit's SHORT decimals. */
std::string FormatIn(UnitFamily family, double modelValue) {
    const UnitDef& unit = ActiveUnit(family);
    return Format(unit.fromReference(ToReference(family, modelValue)), unit.decimals);
}

}  // namespace

void SetRegion(const std::string& region) {
    // TemperatureUnit.getDefaultUnit (kt:210-213) and the siblings beside it.
    g_usesFahrenheit = InList({"BS", "BZ", "KY", "PR", "PW", "US"}, region);
    g_usesMiles = InList({"GB", "US"}, region);
    g_usesInHg = region == "US";
    g_usesMph = InList({"GB", "US"}, region);
    g_usesMetresPerSecond =
        InList({"CN", "DK", "FI", "JP", "KR", "NO", "NL", "PL", "RU", "SE"}, region);
    g_usesInches = region == "US";
}

const std::vector<UnitDef>& UnitsFor(UnitFamily family) {
    switch (family) {
        case UnitFamily::Temperature:
            return kTemperature;
        case UnitFamily::Precipitation:
            return kPrecipitation;
        case UnitFamily::Speed:
            return kSpeed;
        case UnitFamily::Distance:
            return kDistance;
        case UnitFamily::Pressure:
        default:
            return kPressure;
    }
}

const UnitDef* UnitById(UnitFamily family, const std::string& id) {
    for (const UnitDef& unit : UnitsFor(family)) {
        if (id == unit.id) {
            return &unit;
        }
    }
    return nullptr;
}

double ToReference(UnitFamily family, double modelValue) {
    switch (family) {
        case UnitFamily::Temperature:
            return modelValue * 10.0;    // °C  -> deci-celsius
        case UnitFamily::Precipitation:
            return modelValue * 1000.0;  // mm  -> µm
        case UnitFamily::Speed:
            return modelValue * 100.0;   // m/s -> cm/s
        case UnitFamily::Distance:
            return modelValue;           // m   -> m
        case UnitFamily::Pressure:
        default:
            return modelValue * 100.0;   // hPa -> Pa
    }
}

std::string RegionalDefaultId(UnitFamily family) {
    switch (family) {
        case UnitFamily::Temperature:
            return g_usesFahrenheit ? "f" : "c";
        case UnitFamily::Precipitation:
            return g_usesInches ? "in" : "mm";
        case UnitFamily::Speed:
            if (g_usesMph) {
                return "mph";
            }
            return g_usesMetresPerSecond ? "mps" : "kph";
        case UnitFamily::Distance:
            return g_usesMiles ? "mi" : "km";
        case UnitFamily::Pressure:
        default:
            return g_usesInHg ? "inhg" : "hpa";
    }
}

std::string RegionalDefaultSnowfallId() {
    return g_usesInches ? "in" : "cm";
}

const UnitDef& ActiveUnit(UnitFamily family) {
    const std::string& stored = *OverrideSlot(family);
    if (stored != kUnitAuto) {
        const UnitDef* pinned = UnitById(family, stored);
        if (pinned != nullptr) {
            return *pinned;
        }
    }
    const UnitDef* fallback = UnitById(family, RegionalDefaultId(family));
    // Cannot miss for an id this file produces; the guard keeps the reference valid regardless.
    return fallback != nullptr ? *fallback : UnitsFor(family)[0];
}

std::string UnitOverride(UnitFamily family) {
    return *OverrideSlot(family);
}

void SetUnitOverride(UnitFamily family, const std::string& value) {
    *OverrideSlot(family) = value.empty() ? kUnitAuto : value;
}

std::string UnitKeyName(UnitFamily family) {
    switch (family) {
        case UnitFamily::Temperature:
            return "temperature_unit";
        case UnitFamily::Precipitation:
            return "precipitation_unit";
        case UnitFamily::Speed:
            return "speed_unit";
        case UnitFamily::Distance:
            return "distance_unit";
        case UnitFamily::Pressure:
        default:
            return "pressure_unit";
    }
}

bool UnitFamilyForKey(const std::string& key, UnitFamily* out) {
    const UnitFamily families[] = {UnitFamily::Temperature, UnitFamily::Precipitation,
                                   UnitFamily::Speed, UnitFamily::Distance, UnitFamily::Pressure};
    for (UnitFamily family : families) {
        if (UnitKeyName(family) == key) {
            if (out != nullptr) {
                *out = family;
            }
            return true;
        }
    }
    return false;
}

double ValueIn(UnitFamily family, double modelValue) {
    const UnitDef& unit = ActiveUnit(family);
    return unit.fromReference(ToReference(family, modelValue));
}

double TemperatureValue(double celsius) {
    return ValueIn(UnitFamily::Temperature, celsius);
}

double SpeedChartStep() {
    const std::string id = ActiveUnit(UnitFamily::Speed).id;
    if (id == "mps") {
        return 0.5;
    }
    if (id == "kph" || id == "ftps") {
        return 15.0;
    }
    if (id == "mph" || id == "kn") {
        return 10.0;
    }
    if (id == "bf") {
        return 2.0;
    }
    return 50.0;  // cm/s, the reference unit
}

double TemperatureChartStep() {
    return std::string(ActiveUnit(UnitFamily::Temperature).id) == "k" ? 10.0 : 5.0;
}

std::string TemperatureUnitLabel() {
    return ActiveUnit(UnitFamily::Temperature).shortLabel;
}

std::string FormatTemperatureNarrow(double celsius) {
    return std::to_string(static_cast<long long>(std::llround(TemperatureValue(celsius)))) + "°";
}

std::string FormatTemperatureShortNarrow(double celsius) {
    const UnitDef& unit = ActiveUnit(UnitFamily::Temperature);
    return Format(TemperatureValue(celsius), unit.decimals) + "°";
}

std::string FormatTemperatureDeviation(double deciCelsius) {
    const UnitDef& unit = ActiveUnit(UnitFamily::Temperature);
    const double converted =
        std::string(unit.id) == "f" ? 9.0 / 5.0 * (deciCelsius / 10.0) : deciCelsius / 10.0;
    return Format(converted, unit.decimals) + unit.shortLabel;
}

std::string FormatSpeed(double metresPerSecond) {
    return FormatIn(UnitFamily::Speed, metresPerSecond);
}

std::string SpeedUnitLabel() {
    return ActiveUnit(UnitFamily::Speed).shortLabel;
}

std::string SpeedUnitDisplayName() {
    const UnitDef& unit = ActiveUnit(UnitFamily::Speed);
    return unit.displayName != nullptr ? unit.displayName : unit.shortLabel;
}

double SpeedValue(double metresPerSecond) {
    return ValueIn(UnitFamily::Speed, metresPerSecond);
}

std::string FormatSpeedValue(double converted) {
    return Format(converted, ActiveUnit(UnitFamily::Speed).decimals);
}

std::string FormatPressure(double hectopascals) {
    return FormatIn(UnitFamily::Pressure, hectopascals);
}

std::string PressureUnitLabel() {
    return ActiveUnit(UnitFamily::Pressure).shortLabel;
}

std::string FormatDistance(double metres) {
    return FormatIn(UnitFamily::Distance, metres);
}

std::string DistanceUnitLabel() {
    return ActiveUnit(UnitFamily::Distance).shortLabel;
}

std::string DistanceUnitDisplayName() {
    const UnitDef& unit = ActiveUnit(UnitFamily::Distance);
    return unit.displayName != nullptr ? unit.displayName : unit.shortLabel;
}

double DistanceValue(double metres) {
    return ValueIn(UnitFamily::Distance, metres);
}

int DistanceDecimals() {
    return ActiveUnit(UnitFamily::Distance).decimals;
}

std::string FormatDistanceValue(double converted) {
    return Format(converted, ActiveUnit(UnitFamily::Distance).decimals);
}

std::string FormatPlainNumber(double value) {
    return Format(value, 1);
}

std::string FormatPrecipitation(double millimetres) {
    return FormatIn(UnitFamily::Precipitation, millimetres);
}

std::string PrecipitationUnitLabel() {
    return ActiveUnit(UnitFamily::Precipitation).shortLabel;
}

double PrecipitationChartStep() {
    const std::string id = ActiveUnit(UnitFamily::Precipitation).id;
    if (id == "cm") {
        return 0.5;
    }
    if (id == "in") {
        return 0.2;
    }
    return 5.0;  // mm and L·m⁻²
}

namespace {

/** `getSnowfallUnit` — the pinned precipitation unit, else the regional SNOWFALL default. */
const UnitDef& SnowfallUnit() {
    const std::string stored = UnitOverride(UnitFamily::Precipitation);
    if (stored != kUnitAuto) {
        const UnitDef* pinned = UnitById(UnitFamily::Precipitation, stored);
        if (pinned != nullptr) {
            return *pinned;
        }
    }
    const UnitDef* fallback =
        UnitById(UnitFamily::Precipitation, RegionalDefaultSnowfallId());
    return fallback != nullptr ? *fallback : ActiveUnit(UnitFamily::Precipitation);
}

}  // namespace

std::string FormatSnowfall(double millimetres) {
    const UnitDef& unit = SnowfallUnit();
    return Format(unit.fromReference(ToReference(UnitFamily::Precipitation, millimetres)),
                  unit.decimals);
}

std::string SnowfallUnitLabel() {
    return SnowfallUnit().shortLabel;
}

std::string FormatPercent(double value) {
    return std::to_string(static_cast<long long>(std::llround(value))) + "%";
}

std::string FormatIndex(double value) {
    return std::to_string(static_cast<long long>(std::llround(value)));
}

std::string FormatDecimals(double value, int decimals) {
    return Format(value, decimals);
}

std::string FormatHours(double hours) {
    return Format(hours, 1) + " h";
}

}  // namespace breezy
