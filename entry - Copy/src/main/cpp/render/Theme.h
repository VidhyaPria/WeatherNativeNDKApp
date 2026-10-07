/*
 * Colours and dimensions, ported from ArkUIApp's theme/AppColors.ets and theme/Dimens.ets — which
 * are themselves the Android res/values. Keeping the same names on both sides makes the two ports
 * diffable, which is the point of building the app twice.
 *
 * Colours are 0xAARRGGBB, the order OH_Drawing_ColorSetArgb produces and the same order the
 * ArkTS side writes them as "#AARRGGBB" strings.
 */
#ifndef BREEZY_RENDER_THEME_H
#define BREEZY_RENDER_THEME_H

#include <cmath>

#include <cstdint>
#include <string>

namespace breezy {

/** WeatherView.kt's weather kinds. */
enum class WeatherKind : int {
    None = 0,
    Clear = 1,
    Cloud = 2,
    Cloudy = 3,
    Rainy = 4,
    Snow = 5,
    Sleet = 6,
    Hail = 7,
    Fog = 8,
    Haze = 9,
    Thunder = 10,
    Thunderstorm = 11,
    Wind = 12,
};

/** The three stops of a weather_background_* drawable, top to bottom. */
struct BackgroundGradient {
    uint32_t top;
    uint32_t middle;
    uint32_t bottom;
    bool valid;
};

/**
 * ui-weather-view/res/drawable[-night]/weather_background_*.xml — the same 24 gradients the ArkTS
 * port carries. `valid == false` stands for weather_background_default, which is transparent.
 */
BackgroundGradient WeatherBackgroundGradient(WeatherKind kind, bool daylight, bool darkMode);

/** dimens.xml, in vp. The renderer multiplies by the display density. */
namespace dimens {
constexpr float kNormalMargin = 16.0f;
constexpr float kSmallMargin = 8.0f;
constexpr float kLargeMargin = 20.0f;
constexpr float kMaterialIconSize = 24.0f;
/*
 * Material 3 ListTokens container heights. A `ListItem` has a fixed height, not one derived from
 * its content plus padding, and AlertViewHolder wraps its rows in nothing else.
 *   ListItemOneLineContainerHeight = 56 dp — headline + leading (UpcomingAlerts)
 *   ListItemTwoLineContainerHeight = 72 dp — headline + supporting + leading (AlertList)
 */
/** container_main_humidity.xml dew_point_circle — android:padding="12dp". */
constexpr float kDewPointCirclePadding = 12.0f;
constexpr float kListItemOneLineHeight = 56.0f;
constexpr float kListItemTwoLineHeight = 72.0f;
/** ListItemThreeLineContainerHeight — what a two-line SUPPORTING text grows the row to. */
constexpr float kListItemThreeLineHeight = 88.0f;
/** bodyLarge: 16 sp on a 24 sp line — a ListItem headline. */
constexpr float kBodyLargeLineHeight = 24.0f;
/** bodyMedium: 14 sp on a 20 sp line — a ListItem's supporting text. */
constexpr float kBodyMediumLineHeight = 20.0f;
/** titleMedium: 16 sp on a 24 sp line — a settings row's headline. */
constexpr float kTitleMediumLineHeight = 24.0f;
/** labelMedium: 12 sp on a 16 sp line — every detail header's "Daytime" / "Average". */
constexpr float kLabelMediumLineHeight = 16.0f;
/** labelLarge: 14 sp on a 20 sp line — Wind's gusts line. */
constexpr float kLabelLargeLineHeight = 20.0f;
/** bodySmall: 12 sp on a 16 sp line. */
constexpr float kBodySmallLineHeight = 16.0f;
/** titleLarge: 22 sp on a 28 sp line — `DetailsSectionHeader`. */
constexpr float kTitleLargeLineHeight = 28.0f;
/** headlineSmall: 24 sp on a 32 sp line — the in-page group headings. */
constexpr float kHeadlineSmallLineHeight = 32.0f;
/** displaySmall: 36 sp on a 44 sp line — every detail header's value. */
constexpr float kDisplaySmallLineHeight = 44.0f;
/**
 * A settings row's vertical padding, MEASURED on the reference rather than assembled.
 *
 * Rows inside one card sit 293 px apart with a one-line summary and 358 px with a two-line one,
 * against a content height of 24 + 8 + 20n dp — 38 dp of padding in both cases, 19 dp a side.
 * Preference.kt:148 accounts for 8 dp of it and ListItemDefaults for another 8; the remaining
 * 3 dp a side is `Material3ExpressiveCardListItem`'s, whose internals are a library detail this
 * tree does not contain. ArkUIApp carries the same constant.
 */
/**
 * CORRECTED 2026-08-11, from 19. The earlier value came from the row PITCH (293 px) minus the
 * 169 px of content, but the pitch INCLUDES the 7 px small separator, so that folded the separator
 * into the padding. A pixel scan down the reference's Units screen reads the CARD runs directly:
 * 286 px twice, independently — 286 − 169 = 117 → 58.5 a side → 18 dp. Keep in lockstep with
 * ArkUIApp's ROW_V_PADDING, which carries the same measurement.
 */
constexpr float kPreferenceRowVPadding = 18.0f;
constexpr float kCardCornerRadius = 28.0f;
constexpr float kMainTitleTextSize = 140.0f;
constexpr float kTitleTextSize = 16.0f;
constexpr float kContentTextSize = 14.0f;
constexpr float kTrendItemWidth = 67.0f;
constexpr float kDailyTrendItemHeight = 298.0f;
constexpr float kHourlyTrendItemHeight = 206.0f;
/** ?attr/actionBarSize (dimens.xml:36). */
constexpr float kActionBarHeight = 64.0f;
/** LargeTopAppBar — a 64 dp row of icons plus an 88 dp title band beneath it. */
constexpr float kLargeAppBarRowHeight = 64.0f;
constexpr float kLargeAppBarHeight = 152.0f;
/** Material 3 `LargeTitleBottomPadding` — the title's BASELINE above the bar's bottom edge. */
constexpr float kLargeTitleBottomPadding = 28.0f;
/** How much of a Roboto line box sits above the baseline — ascent / (ascent + descent + leading). */
constexpr double kBaselineShareOfLineBox = 0.79;
/*
 * The settings list's grouped cards (dimens.xml:21-23). A group is one rounded block: the OUTER
 * corners of its first and last row carry the full radius and every corner between them is 2 dp,
 * which is what makes a group read as one card rather than a stack.
 */
constexpr float kSettingsCardCornerRadius = 28.0f;
constexpr float kSettingsCardInnerCornerRadius = 2.0f;
constexpr float kSettingsSmallSeparator = 2.0f;
constexpr float kSettingsLargeSeparator = 16.0f;
/** textAppearanceHeadline6 is 20sp. */
constexpr float kToolbarTitleTextSize = 20.0f;
/** textAppearanceTitleLarge is 22sp in Material 3. */
constexpr float kTitleLargeTextSize = 22.0f;
/** textAppearanceTitleMedium is 16sp in Material 3. */
constexpr float kTitleMediumTextSize = 16.0f;
/** container_main_header.xml:97 — the unit next to the big number. */
constexpr float kTemperatureUnitTextSize = 50.0f;
constexpr float kSubtitleTextSize = 12.0f;
}  // namespace dimens

/**
 * md_theme_inverseSurface (styles.xml:29) — ?attr/colorSurfaceInverse, what every toolbar and
 * header text is tinted with. Not onSurface.
 */
constexpr uint32_t kSurfaceInverseLight = 0xFF191C1E;  // neutral1_900
constexpr uint32_t kSurfaceInverseDark = 0xFFE1E3E5;   // neutral1_100

/**
 * MaterialWeatherThemeDelegate.isLightBackground (kt:89-97): light only when it is daytime AND
 * the system is not in night mode.
 */
constexpr bool IsLightBackground(bool daylight, bool darkMode) {
    return daylight && !darkMode;
}

/** A colour that differs between the light and dark themes. */
struct ColorPair {
    uint32_t light;
    uint32_t dark;

    constexpr uint32_t Resolve(bool darkMode) const { return darkMode ? dark : light; }
};

/*
 * The MATERIAL YOU palette, via ArkUIApp's theme/AppColors.ets — NOT colors.xml's static one.
 *
 * res/values-v31/colors.xml and values-night-v31/colors.xml rebind every md_theme_* colour to an
 * @android:color/system_* resource on SDK >= 31. The reference phone is Android 12, so colors.xml's
 * #205DAF family never applies there; it is the pre-v31 fallback. Both ports carried that fallback
 * until it was corrected, which is why every screen read blue against the reference's teal.
 *
 * Extracted exactly, not sampled: adb pull /system/framework/framework-res.apk, then
 * `aapt2 dump resources`. No wallpaper overlay is applied on this device
 * (`theme_customization_overlay_packages` is null), so these are the static framework defaults.
 * Each pair names the system_* resource it came from. Keep in lockstep with AppColors.ets.
 */
constexpr ColorPair kPrimary{0xFF00668B, 0xFF76D1FF};  // accent1_600 / accent1_200
/** colorOutline — note the deliberately low alpha in both themes. */
constexpr ColorPair kOutline{0x10000000, 0x10FFFFFF};
/**
 * Compose's own `colorScheme.outline`, which is opaque — unlike the view-system `colorOutline`
 * above. Used where a Material composable draws a visible border, such as the time picker's AM/PM
 * container. Mirrors ArkUIApp's AppColors.COMPOSE_OUTLINE.
 */
constexpr ColorPair kComposeOutline{0xFF70777C, 0xFF8A9297};  // neutral2_500 / neutral2_400
constexpr ColorPair kTitleText{0xFF000000, 0xFFFFFFFF};
constexpr ColorPair kBodyText{0xFF666666, 0xFF999999};
/** dark value resolves through colorTextLight2nd (colors.xml:79). */
constexpr ColorPair kCaptionText{0xFFB2B2B2, 0xFF999999};
/**
 * The main screen's cards. This used to read `colorMainCardBackground` (colors.xml:85, #FDFCFF),
 * but that attribute is DEAD: declared in attrs.xml, valued in colors.xml, bound in styles.xml:54,
 * and referenced by no layout in the app. What the cards actually take is styles.xml:101 —
 * `<item name="cardBackgroundColor">?attr/colorSurface</item>` — which is why the reference's tiles
 * measure #FCFCFF (neutral1_10). Defined as the literal rather than `= kSurface`, because kSurface
 * is declared further down this header.
 */
constexpr ColorPair kMainCardBackground{0xFFFCFCFF, 0xFF191C1E};
/** colorPrecipitationProbability = md_theme_primary (colors.xml:86) */
constexpr ColorPair kPrecipitationProbability = kPrimary;
/**
 * @color/alert_background (colors.xml / values-night) — the alert card is a filled pill.
 *
 * Its TEXT is not alert_text: AlertViewHolder colours the headline and the warning icon with
 * MaterialTheme.colorScheme.onSurface and the dates with onSurfaceVariant, which is why Android's
 * pill reads near-white on maroon rather than salmon-on-maroon.
 */
constexpr ColorPair kAlertBackground{0xFFFFB4AB, 0xFF410002};
constexpr ColorPair kOnSurface{0xFF191C1E, 0xFFE1E3E5};  // neutral1_900 / neutral1_100
/** colorTextGrey / colorTextGrey2nd — the nowcast's axis labels. */
constexpr ColorPair kTextGrey{0xFF4D4D4D, 0xFF999999};
/* The trend chips are Material buttons: a filled container, not bare text. */
constexpr ColorPair kOnPrimary{0xFFFFFFFF, 0xFF003549};        // accent1_0 / accent1_800
constexpr ColorPair kSurfaceVariant{0xFFDCE3E9, 0xFF40484D};   // neutral2_100 / neutral2_700
constexpr ColorPair kOnSurfaceVariant{0xFF40484D, 0xFFC0C7CD}; // neutral2_700 / neutral2_200

/*
 * The container roles the LOCATION LIST uses (LocationHolder.kt:74-95). A row is primaryContainer
 * when it is the selected location, alert_background when it has active warnings, and the 2 dp
 * elevated surface otherwise; `alert_text` is that background's companion for the body line.
 */
/**
 * The Compose `outlineVariant` role — what `HorizontalDivider` draws with, and the only place
 * either port needs it (the alert screen's rules between description, instruction and source).
 * BreezyWeatherTheme overrides only primary/secondary/tertiary, so this is the Material 3
 * baseline, NOT the XML `colorOutline` (which is a 6 %-alpha bar track).
 */
constexpr ColorPair kOutlineVariant{0xFFC0C7CD, 0xFF40484D};    // neutral2_200 / neutral2_700
constexpr ColorPair kPrimaryContainer{0xFFC1E8FF, 0xFF004C69};  // accent1_100 / accent1_700
constexpr ColorPair kOnPrimaryContainer{0xFF001E2C, 0xFFC1E8FF};// accent1_900 / accent1_100
constexpr ColorPair kAlertText{0xFFBA1A1A, 0xFFFF897D};
/** md_theme_surface — the list screen is a plain Material surface, not a weather background. */
constexpr ColorPair kSurface{0xFFFCFCFF, 0xFF191C1E};  // neutral1_10 / neutral1_900

/**
 * The settings list card — getWidgetSurfaceColor(2.dp, surface) (Material3Widgets.kt:68-80, :227),
 * NOT plain kSurface.
 *
 * Material 3 tints an elevated surface instead of lightening it: surfaceTint (which defaults to
 * primary) over surface at alpha = ((4.5 * ln(elevation + 1)) + 2) / 100, which is 0.069438 at the
 * cards' 2 dp. Precomputed from kPrimary over kSurface so the two ports agree exactly:
 *   light  #00668B over #FCFCFF -> #EAF2F7
 *   dark   #76D1FF over #191C1E -> #1F292E
 */
constexpr ColorPair kSettingsCard{0xFFEAF2F7, 0xFF1F292E};

/**
 * A dialog's container. NOT kSettingsCard — a dialog is `surfaceContainerHigh`, a different role
 * from a 2 dp elevated settings card, and the two measure differently on the reference.
 *
 * There are TWO, because Breezy raises dialogs through two stacks that do NOT resolve alike:
 *
 *   kDialogSurface       Compose `AlertDialog` — every list/edit/time preference and the
 *                        alternate-calendar picker. Compose derives surfaceContainerHigh from the
 *                        DYNAMIC neutral palette.
 *   kAlertDialogSurface  View `MaterialAlertDialogBuilder` — the three icon-preview dialogs.
 *                        styles.xml overrides no surfaceContainer* attribute, so these stay at the
 *                        Material 3 BASELINE, the faintly purple #ECE6F0.
 *
 * All four sampled off the reference in both modes:
 *   Compose  light #E2E9F0  dark #242B30      View  light #ECE6F0  dark #2B2930
 * Keep in lockstep with ArkUIApp's AppColors.DIALOG_SURFACE / ALERT_DIALOG_SURFACE.
 */
constexpr ColorPair kDialogSurface{0xFFE2E9F0, 0xFF242B30};
constexpr ColorPair kAlertDialogSurface{0xFFECE6F0, 0xFF2B2930};

/**
 * The three manage screens' toolbar — `getWidgetSurfaceColor(6f, colorPrimary, colorSurface)`
 * (CardDisplayManageActivity.kt:107-113): the same overlay as kSettingsCard but at 6 dp, so
 * alpha = ((4.5 * ln(7)) + 2) / 100 = 0.10757. Precomputed so both ports agree exactly.
 */
constexpr ColorPair kElevatedSurface6{0xFFE1ECF3, 0xFF232F36};
/** `?android:attr/colorBackground` — the chip bar behind those screens' bottom row.
 *  neutral1_0 / neutral1_1000. */
constexpr ColorPair kBackground{0xFFFFFFFF, 0xFF000000};

/**
 * ColorUtils.getWidgetSurfaceColor — a raised surface is the surface with colorPrimary composited
 * over it at (4.5·ln(dp+1) + 2)/100, not a separate token. ArkUIApp ports the same function as
 * AppColors.elevatedSurface.
 */
inline uint32_t ElevatedSurface(double elevationDp, uint32_t tint, uint32_t surface) {
    const double alpha = (4.5 * std::log(elevationDp + 1.0) + 2.0) / 100.0;
    const double tr = static_cast<double>((tint >> 16) & 0xFF);
    const double tg = static_cast<double>((tint >> 8) & 0xFF);
    const double tb = static_cast<double>(tint & 0xFF);
    const double sr = static_cast<double>((surface >> 16) & 0xFF);
    const double sg = static_cast<double>((surface >> 8) & 0xFF);
    const double sb = static_cast<double>(surface & 0xFF);
    const auto mix = [alpha](double top, double bottom) {
        return static_cast<uint32_t>(top * alpha + bottom * (1.0 - alpha) + 0.5);
    };
    return 0xFF000000u | (mix(tr, sr) << 16) | (mix(tg, sg) << 8) | mix(tb, sb);
}

/*
 * Pollen level dots.
 *
 * The `pollen_level_colors` array (arrays.xml) is pollenLevel_1 … pollenLevel_5 — it does NOT
 * start at pollenLevel_0. getPollenIndexToColor (PollenIndex.kt:76-84) reaches the grey
 * pollenLevel_0 only via its `if (pollenIndex == 0)` early return, i.e. a concentration of exactly
 * zero, which is also the only case that reads "None" rather than a level name.
 *
 * So the five levels "Very low" … "Very high" are indices 0…4 HERE, and a "Very low" reading is
 * the teal #08C286. An earlier fix prepended the grey and indexed by level, which shifted every
 * dot one band down and made "Very low" render as "None".
 */
constexpr uint32_t kPollenLevelColors[5] = {0xFF08C286, 0xFF6AD555, 0xFFFFD741,
                                            0xFFFFAB40, 0xFFFF3B30};

/** colors.xml pollenLevel_0 — a zero concentration only. */
constexpr uint32_t kPollenLevelNoneColor = 0xFFBFBFBF;

/*
 * colorTertiary / colorOnTertiary / colorOnTertiaryContainer — the humidity block's dew-point
 * badge. humidity_dew_point_circle.xml is an oval solid-filled with ?attr/colorTertiary, with the
 * value centred on it in ?attr/colorOnTertiary and the label beside it in onTertiaryContainer.
 */
constexpr ColorPair kTertiary{0xFF605A7C, 0xFFCAC1EA};            // accent3_600 / accent3_200
constexpr ColorPair kOnTertiary{0xFFFFFFFF, 0xFF322C4C};          // accent3_0 / accent3_800
constexpr ColorPair kOnTertiaryContainer{0xFF1D1736, 0xFFE6DEFF}; // accent3_900 / accent3_100

/*
 * The containers SlidingItemContainerLayout reveals under a swiped location row
 * (LocationHolder.kt:56-72): errorContainer for the delete edge, and the secondary pair — tertiary
 * for a current-position row — for the settings edge.
 */
constexpr ColorPair kErrorContainer{0xFFF9DEDC, 0xFF8C1D18};
constexpr ColorPair kOnErrorContainer{0xFF410E0B, 0xFFF9DEDC};
/** colorScheme.error — the outlined field's border and helper text when a value fails its regex. */
constexpr ColorPair kError{0xFFB3261E, 0xFFF2B8B5};
constexpr ColorPair kSecondaryContainer{0xFFD1E5F4, 0xFF374955};   // accent2_100 / accent2_700
constexpr ColorPair kOnSecondaryContainer{0xFF091E28, 0xFFD1E5F4}; // accent2_900 / accent2_100
constexpr ColorPair kTertiaryContainer{0xFFE6DEFF, 0xFF484264};    // accent3_100 / accent3_700

/**
 * md_theme_inverseOnSurface (styles.xml:28; colors.xml:129, values-night/colors.xml:47) —
 * ?attr/colorOnSurfaceInverse. Declared here, beside the other pairs, rather than next to
 * kSurfaceInverse: ColorPair is not defined until further down this header.
 *
 * The snackbar's message is the only thing in this port that reads AGAINST kSurfaceInverse rather
 * than being tinted with it.
 */
constexpr ColorPair kOnSurfaceInverse{0xFFE1E3E5, 0xFF191C1E};  // neutral1_100 / neutral1_900

/** Replaces a colour's alpha, `alpha` being 0..1. */
uint32_t WithAlpha(uint32_t color, double alpha);

/**
 * MaterialWeatherThemeDelegate.kt:38-44 — getBrighterColor(). Converts to HSV, drops saturation
 * by 0.25 and raises value by 0.25.
 */
uint32_t BrighterColor(uint32_t color);

/**
 * The theme ACCENT for a weather kind — the trend chart lines and the nowcast columns, not the
 * page background. MaterialWeatherThemeDelegate.getThemeColors (kt:73-87) brightens it at night.
 */
uint32_t WeatherThemeAccent(WeatherKind kind, bool daylight);

/** The drawable base name, matching the weather_*.png assets in rawfile. */
const char* IconBaseName(WeatherKind kind, bool daytime);

/**
 * `Location.cityAndDistrict` (Location.kt:261-274) — the city, then the district after a comma when
 * there is one and it differs from the city.
 *
 * Only the reverse geocoder fills a district, so a searched location reduces to its city. This is
 * what makes Android's toolbar read "Helsinki, Kotkavuori" for a current position.
 */
inline std::string CityAndDistrict(const std::string& city, const std::string& district) {
    std::string out = city;
    if (!district.empty() && district != city) {
        if (!out.empty()) {
            out += ", ";
        }
        out += district;
    }
    return out;
}

/**
 * Where a LargeTopAppBar's headline box starts, measured DOWN FROM THE BAR'S TOP.
 *
 * Material 3's `TwoRowsTopAppBar` places the title's BASELINE `LargeTitleBottomPadding = 28.dp`
 * above the bar's bottom edge — not its box, and not centred in anything. For a Roboto line box the
 * baseline sits about 0.79 of the way down, so the box top is `152 - 28 - 0.79h`.
 *
 * Neither of the two obvious centrings is right, and the two ports had one each: centring in the
 * 88 vp title band puts the headline ~6 vp high (what ArkUIApp did) and centring in a 64 vp row at
 * the bar's bottom puts it ~6 vp low (what this port did), so they disagreed by ~12 vp on every
 * screen with a large app bar. Measured on the reference device the Android headline's centre is
 * ~113 vp below the bar top; this rule puts both ports at ~112.7. ArkUIApp applies the same rule as
 * a top padding on its title band — see LARGE_TITLE_TOP_PADDING in theme/Dimens.ets.
 */
inline double LargeTitleTop(double headlineHeightPx, float density) {
    return (dimens::kLargeAppBarHeight - dimens::kLargeTitleBottomPadding) * density -
           dimens::kBaselineShareOfLineBox * headlineHeightPx;
}

/**
 * PolylineAndHistogramView.kt:513-524, DailyTrendItemView.kt:252-256, TrendRecyclerView.kt:191-196.
 * All in vp.
 */
namespace trend {
constexpr float kChartMarginTop = 24.0f;
constexpr float kChartMarginBottom = 36.0f;
constexpr float kPolylineSize = 5.0f;
constexpr float kPolylineTextSize = 14.0f;
constexpr float kHistogramWidth = 4.5f;
constexpr float kHistogramTextSize = 12.0f;
constexpr float kChartLineSize = 1.0f;
constexpr float kChartTextMargin = 2.0f;
constexpr float kShadowAlphaLight = 0.15f;
constexpr float kShadowAlphaDark = 0.3f;
constexpr float kItemIconSize = 32.0f;
constexpr float kItemTextMargin = 2.0f;
constexpr float kItemIconMargin = 8.0f;
constexpr float kItemMarginBottom = 16.0f;
}  // namespace trend

/** AbstractMainCardViewHolder.kt:47-51 — card elevation is 2dp. */
constexpr float kCardElevation = 2.0f;

/**
 * colorLevel_1..6 (colors.xml:21-26) — the shared severity ramp behind wind, UV, precipitation,
 * cloud cover and air quality.
 */
constexpr uint32_t kLevelColors[6] = {0xFF72D572, 0xFFFFCA28, 0xFFFFA726,
                                      0xFFE52F35, 0xFF99004C, 0xFF7E0023};
/** PollutantIndex.kt:133 — the Plume 2023 scale, not the US EPA one. */
constexpr double kAqiThresholds[6] = {0.0, 20.0, 50.0, 100.0, 150.0, 250.0};
/** R.color.sunshine (colors.xml:70) — the daily Sunshine tab's bars. */
constexpr uint32_t kSunshineColor = 0xFFFDBC4C;

/** SpeedUnit.BEAUFORT_SCALE (kt:167-182) against WIND_SPEED_1..12 (kt:311-322). */
int BeaufortLevel(double metresPerSecond);

/* Per-bar colours. Each returns 0 when the value is absent, so the caller can fall back to the
 * card's own colour the way the Kotlin does with a null drawable tint. */

/** Wind.getColor (Wind.kt:39-51) — colorLevel_1..6 by Beaufort band. */
uint32_t WindColor(double metresPerSecond);
/** UV.getUVColor (UV.kt:68-78) against UV_INDEX_LOW/MIDDLE/HIGH/EXCESSIVE = 3/6/8/11. */
uint32_t UvColor(double index);
/** Precipitation.getHalfDayPrecipitationColor (Precipitation.kt:27-60). */
uint32_t HalfDayPrecipitationColor(double millimetres);
/** Precipitation.getHourlyPrecipitationColor (Precipitation.kt:62-95). */
uint32_t HourlyPrecipitationColor(double millimetres);
/** Ratio.getCloudCoverColor (UnitExtensions.kt:413-420) against 37.5 / 62.5 percent. */
uint32_t CloudCoverColor(double percent);
/** PollutantIndex.getAqiToLevel — the last threshold at or below the value. */
uint32_t AqiLevelColor(double index);

}  // namespace breezy

#endif  // BREEZY_RENDER_THEME_H
