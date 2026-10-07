/*
 * Display units, ported from ArkUIApp's common/Units.ets and model/UnitCatalogue.ets.
 *
 * TWO LAYERS, and the order is the Kotlin's `?:` (SettingsManager.kt:230-265):
 *
 *   1. the STORED override for that quantity, if the user pinned one on the Units screen;
 *   2. otherwise `<Unit>.getDefaultUnit(currentLocale)`, the CLDR per-quantity table keyed on the
 *      device's REGION.
 *
 * Layer 2 is all this file used to be. Breezy picks each unit per quantity, not from one global
 * metric/imperial switch (TemperatureUnit.getDefaultUnit kt:210-213 and its siblings). ArkTS reads
 * the region from @ohos.i18n; there is no NDK equivalent, so the shell passes the region code in and
 * this decides everything from it — the decision itself stays on the C++ side. Layer 1 arrives the
 * same way: the shell owns the preferences file and pushes the five ids over NAPI.
 *
 * Each family converts through a REFERENCE unit rather than pairwise, exactly as the Kotlin does:
 *
 *   temperature    deci-celsius   TemperatureUnit.kt
 *   precipitation  micrometre     PrecipitationUnit.kt
 *   speed          cm·s⁻¹         SpeedUnit.kt
 *   distance       metre          DistanceUnit.kt
 *   pressure       pascal         PressureUnit.kt
 */
#ifndef BREEZY_COMMON_UNITS_H
#define BREEZY_COMMON_UNITS_H

#include <string>
#include <vector>

namespace breezy {

enum class UnitFamily { Temperature, Precipitation, Speed, Distance, Pressure };

/** The sentinel SettingsManager stores when no unit is pinned. */
inline constexpr const char* kUnitAuto = "auto";

struct UnitDef {
    /** The id SettingsManager persists, e.g. "c", "mph", "inhg". */
    const char* id;
    /** UnitWidth.LONG — the dialog row and the row summary. */
    const char* longName;
    /** UnitWidth.SHORT — the suffix beside a value on the weather screen. */
    const char* shortLabel;
    /** UnitDecimals(short = …). */
    int decimals;
    /** Reference unit → this unit. */
    double (*fromReference)(double);
    /**
     * `getDisplayName(useMeasureFormat = true)` — the unit named as a COLUMN HEADING, which is not
     * the suffix that rides beside a value.
     *
     * Screen 4's scale tables put it above their value column: the reference's Beaufort table is
     * headed "meters/sec" where a value reads "0.3 m/s", and its visibility table "km". That is
     * ICU's short unit display name.
     *
     * MEASURED on the reference: `mps` -> "meters/sec" and `km` -> "km", the two the device's own
     * settings select. The rest are CLDR's short display names for the same units and have not
     * been checked against the reference. Null means "same as shortLabel". ArkUIApp's `UnitDef`
     * carries the same field.
     */
    const char* displayName = nullptr;
};

/** Call once per refresh with what @ohos.i18n reported, e.g. "CN", "US", "GB". */
void SetRegion(const std::string& region);

/* ---------------------------------------------------------------- the catalogue ------------- */

const std::vector<UnitDef>& UnitsFor(UnitFamily family);
/** Null when `id` is not in that family — including for "auto". */
const UnitDef* UnitById(UnitFamily family, const std::string& id);
/** Lifts the value the model stores (°C, mm, m·s⁻¹, m, hPa) into the family's reference unit. */
double ToReference(UnitFamily family, double modelValue);
/** `<Unit>.getDefaultUnit(currentLocale)` — what "auto" resolves to on this device. */
std::string RegionalDefaultId(UnitFamily family);
/**
 * PrecipitationUnit.getDefaultSnowfallUnit — snow is reported in the coarser unit, so a metric
 * region pairs mm with cm. Exists so the Units screen can render "millimeters/centimeters".
 */
std::string RegionalDefaultSnowfallId();
/** The unit in force: the stored override, else the regional default. */
const UnitDef& ActiveUnit(UnitFamily family);

/* ------------------------------------------------------------------ the overrides ----------- */

/** The stored id, or "auto". */
std::string UnitOverride(UnitFamily family);
/** `value` may be a unit id or "auto". */
void SetUnitOverride(UnitFamily family, const std::string& value);
/** The preferences key SettingsManager uses, e.g. "temperature_unit". */
std::string UnitKeyName(UnitFamily family);
/** Maps a preferences key back to its family; false when the key is not a unit key. */
bool UnitFamilyForKey(const std::string& key, UnitFamily* out);

/* ------------------------------------------------------------------- the formatters --------- */

/**
 * The model value converted into the active unit, WITHOUT formatting.
 *
 * Screen 4's charts plot in the display unit — the axis bounds, the label step and the gradient
 * stops are all expressed there — so they need the number, not the string. ArkUIApp's Units.ets
 * exports the same thing as `valueIn`.
 */
double ValueIn(UnitFamily family, double modelValue);

/** TemperatureUnit.kt — the active unit's value for a model Celsius reading. */
double TemperatureValue(double celsius);
/**
 * TemperatureUnit.chartStep — 5 for Celsius and Fahrenheit, 10 for Kelvin. Screen 4's temperature
 * chart rounds its bounds out to this.
 */
double TemperatureChartStep();
/** The degree unit beside the big header number (container_main_header.xml:94-106). */
std::string TemperatureUnitLabel();
/** UnitWidth.NARROW, e.g. "70°" — the big header value and every trend label. */
std::string FormatTemperatureNarrow(double celsius);
/**
 * The reading at the unit's SHORT decimals but with the NARROW unit — "18.2°".
 *
 * `formatMeasure(context, unit, unitWidth = UnitWidth.NARROW)` (DetailsConditions.kt:578) leaves
 * `valueWidth` at its SHORT default and overrides only the UNIT's width, so the value keeps
 * Celsius's one decimal (`UnitDecimals(narrow = 0, short = 1, long = 1)`, TemperatureUnit.kt:77)
 * while the unit collapses to a bare degree sign. Screen 4's conditions header is the one place
 * that combination appears; the trend labels and the main header pass `valueWidth = NARROW` too
 * and so round. ArkUIApp exports the same thing as `formatTemperatureShortNarrow`.
 */
std::string FormatTemperatureShortNarrow(double celsius);
/**
 * `Temperature.toDoubleDeviation` (Temperature.kt:131-133) formatted at the unit's SHORT decimals.
 *
 * A DEVIATION, not a reading: 1 °C of degree day is 1.8 °F, not 33.8 °F, so the Fahrenheit
 * conversion drops the +32 offset (TemperatureUnit.kt:92) and Kelvin is a plain divide. ArkUIApp's
 * Units.ets exports the same thing as `formatTemperatureDeviation`.
 */
std::string FormatTemperatureDeviation(double deciCelsius);

std::string FormatSpeed(double metresPerSecond);
std::string SpeedUnitLabel();
/** The scale table's column heading — "meters/sec", not the "m/s" that rides beside a value. */
std::string SpeedUnitDisplayName();
/** The speed in the active unit, unformatted. */
double SpeedValue(double metresPerSecond);
/** Formats a value ALREADY in the display unit, at that unit's decimals. */
std::string FormatSpeedValue(double converted);
/**
 * SpeedUnit.chartStep. It is a MINIMUM spacing, not a fixed interval: Vico's
 * `VerticalAxis.ItemPlacer.step` takes the smallest multiple of it that fits the axis, which is why
 * 0.5 for metres per second does not mean thirty gridlines over a 15 m/s range.
 */
double SpeedChartStep();

std::string FormatPressure(double hectopascals);
std::string PressureUnitLabel();

std::string FormatDistance(double metres);
std::string DistanceUnitLabel();
/** The scale table's column heading — `getDisplayName`, not the value suffix. See UnitDef. */
std::string DistanceUnitDisplayName();
/** The distance in the active unit, unformatted. */
double DistanceValue(double metres);
/** UnitDecimals.short for distance — the visibility scale needs the step it implies. */
int DistanceDecimals();
/** Formats a value ALREADY in the display unit, at that unit's decimals. */
std::string FormatDistanceValue(double converted);
/** A bare number at up to one decimal, trailing zero dropped — `Ratio.formatValue`. */
std::string FormatPlainNumber(double value);

std::string FormatPrecipitation(double millimetres);
std::string PrecipitationUnitLabel();
/**
 * PrecipitationUnit.chartStep — 5 mm, 0.5 cm, 0.2 in, 5 L·m⁻² (PrecipitationUnit.kt:74-123). As
 * everywhere else this is Vico's MINIMUM label spacing, not a fixed interval.
 */
double PrecipitationChartStep();
/**
 * `SettingsManager.getSnowfallUnit` (kt:241-243) — the PINNED precipitation unit when there is one,
 * else `getDefaultSnowfallUnit(locale)`, the coarser member of the regional pair: centimetres where
 * rain is millimetres, inches where rain is inches.
 */
std::string FormatSnowfall(double millimetres);
std::string SnowfallUnitLabel();

/** Unit-free quantities, formatted the way the adapters label their bars. */
std::string FormatPercent(double value);
std::string FormatIndex(double value);
/**
 * `Double.format(decimals)` for a unitless quantity — rounds to `decimals` places and DROPS a
 * trailing zero, which is why the reference's UV header reads "5" where `format(decimals = 1)`
 * was asked for (DetailsUV.kt:223).
 */
std::string FormatDecimals(double value, int decimals);
std::string FormatHours(double hours);

}  // namespace breezy

#endif  // BREEZY_COMMON_UNITS_H
