/*
 * Screen 4 — the tile / trend detail screen, the NDK twin of ArkUIApp's pages/Details.ets.
 *
 * Source: ui/details/DetailsScreen.kt (:111-194 the scaffold, :196-260 the day pager, :262-349 the
 * FAB menu, :351-518 the per-tag dispatch) and ui/details/components/Details<Tag>.kt for each
 * header. NOT DailyWeatherActivity — the shipped APK's tiles open DetailsActivity, which
 * ref_screen4_details_pressure.png is what settled.
 *
 * Everything about WHAT is shown lives in ArkUIApp's view/details/DetailsData.ets and is
 * transcribed here rather than re-derived from the Kotlin, so the two ports cannot drift — the same
 * arrangement SourcePreferences.cpp has with the weather-sources catalogue.
 *
 * The chart is NOT in this increment; see MORNING.md. Both ports omit it, together.
 */
#ifndef BREEZY_VIEW_DETAILSSCREEN_H
#define BREEZY_VIEW_DETAILSSCREEN_H

#include <native_drawing/drawing_canvas.h>

#include <cstdint>
#include <string>
#include <vector>

#include "data/Pollutant.h"
#include "input/Scroller.h"
#include "model/Weather.h"

namespace breezy {

/** DetailScreen.kt:28-46 — declaration order, which is the order the FAB menu lists. */
enum class DetailTag {
    Conditions,
    FeelsLike,
    Wind,
    AirQuality,
    Pollen,
    UvIndex,
    Precipitation,
    Humidity,
    Pressure,
    CloudCover,
    Visibility,
    SunMoon
};

/** The enum's `id`, which is what a trend tab and a detail block carry. */
const char* DetailTagId(DetailTag tag);
/** The enum's `nameId`, resolved to English. */
const char* DetailTagName(DetailTag tag);
/** The enum's `iconId`, as a rawfile name. */
const char* DetailTagIcon(DetailTag tag);
/** Parses a stored id back to a tag; falls back to Conditions, as the Kotlin's valueOf default. */
DetailTag DetailTagFromId(const std::string& id);

/** DetailScreen.toDetailScreenList (kt:52-92). */
std::vector<DetailTag> DetailTagsFor(const Weather& weather, bool hasAirQualitySource,
                                     bool hasPollenSource);

enum class DetailsAction { None, Close };

struct DetailsHit {
    DetailsAction action = DetailsAction::None;
};

class DetailsScreen {
public:
    void SetGeometry(double widthPx, double heightPx, float density, double topInsetPx,
                     double bottomInsetPx);
    void SetDarkMode(bool dark) { dark_ = dark; }
    /** screen 3c's `trend_horizontal_line_switch`; the chart's key line is gated on it (kt:223). */
    void SetKeyLinesEnabled(bool enabled) { keyLinesEnabled_ = enabled; }

    /**
     * `dayIndex` is the pager page the tap landed on — DetailsViewModel's initialIndex.
     *
     * The coordinates are for the EphemerisChart alone: it computes the sun's and the moon's
     * altitude, which no other part of this screen needs.
     */
    void Open(const Weather& weather, int utcOffsetSeconds, DetailTag tag, int dayIndex,
              int64_t nowMs, bool hasAirQualitySource, bool hasPollenSource, double latitude = 0.0,
              double longitude = 0.0);

    void Draw(OH_Drawing_Canvas* canvas);
    DetailsHit HitTest(double x, double y);
    /** Returns true when the position actually moved, so the caller knows to repaint. */
    bool ScrollBy(double deltaY);
    bool ScrollTabsBy(double deltaX);
    /**
     * Lifting the finger hands the gesture to the flywheel. `velocity` is px/s in the same sign
     * convention Scroller::Drag uses, which is the negated finger delta.
     */
    void FlingTabs(double velocity);
    void FlingContent(double velocity);
    /** Advances both flings; true while either still has somewhere to go. */
    bool Advance(double seconds);
    void SetElementAnimations(bool enabled) { elementAnimations_ = enabled; }
    /** True while the condition icon is animating, so the renderer keeps asking for frames. */
    bool AnimatingConditionIcon() const;
    void StopScrolling();
    /**
     * A touch over the plot snaps the marker to the nearest plotted hour; lifting clears it, which
     * is `onHidden`. Returns true when the marker moved, so the caller repaints.
     */
    bool MoveMarker(double x, double y);
    bool ClearMarker();
    /** Where the day pager ends, so the caller can tell a sideways drag from a downward one. */
    double TabRowBottom() const;
    /** True while the day pager is still gliding. */
    bool TabsAnimating() const { return tabScroll_.IsAnimating(); }
    /**
     * The band a tab-row scroll dirties — everything from under the app bar to the row's divider.
     *
     * A sideways fling moves ONE tenth of the surface, and without this the frame still paid the
     * full 13.7 MB scratch-to-DMA copy: measured at 3.42 ms of an 8.33 ms budget at 120 Hz, which
     * is what made the pager feel rough. Same mechanism screens 1 and 2 already use.
     */
    bool TabBand(double* top, double* bottom) const;
    /** The FAB menu eats Back before the screen does, as BackHandler(fabMenuExpanded) does. */
    bool MenuOpen() const { return menuOpen_; }
    void CloseMenu() { menuOpen_ = false; }

private:
    /**
     * One column of the summary block — the shape every `<Tag>Item` composable draws.
     *
     * ONE column for the scalar tags; TWO, Daytime and Nighttime, for the four that summarise a
     * half-day each (WindSummary kt:224-262, PrecipitationSummary kt:299-337,
     * WeatherConditionSummary kt:494-551). None at all for Pollen and Sun & moon, whose screens
     * lead with a card instead.
     */
    struct SummaryColumn {
        std::string label;
        std::string value;
        std::string unit;
        std::string description;
        uint32_t dotColor = 0;
        /**
         * The weather icon drawn BEFORE `description`, empty for the columns that have none.
         *
         * `WeatherCondition` (DetailsConditions.kt:718-766) is a Row of a
         * `small_weather_icon_size` icon, a `small_margin` gap and the condition text in BOLD
         * onSurface — not the plain onSurfaceVariant line every other tag's description is. Only
         * the Conditions and Feels-like headers have it; this port used to draw the text alone.
         * ArkUIApp carries the same two fields.
         */
        std::string iconAsset;
    /**
     * The same icon as a kind, for the ANIMATED path (DetailsConditions.kt:732-748). `iconAsset` is
     * the flattened single-layer PNG the static branch draws; the animated one stacks
     * `<base>_1/_2/_3` and needs the kind to look up its animator set.
     */
    WeatherKind iconKind = WeatherKind::None;
    bool iconDaylight = true;
        /**
         * What follows the unit, back at displaySmall. Wind is the only tag with one: `WindItem`
         * appends `" " + arrow` AFTER the unit span (DetailsWind.kt:283-286), so the arrow is the
         * same size as the speed. This port drew it as its own line, where the gusts belong.
         */
        std::string valueSuffix;
        /**
         * The LEVEL NAME, appended to the value at headlineSmall in onSurfaceVariant.
         *
         * `AirQualityItem` (DetailsAirQuality.kt:402-433) and `UVItem` build ONE displaySmall
         * `TextFixedHeight` of "27" + " " + a `withStyle(headlineSmall, onSurfaceVariant)` span
         * reading "Fair", so the reference's node is a single 143 px line. Both ports put the
         * level on a THIRD line, making those two headers 52 px taller than Android's.
         */
        std::string valueLevel;
        /**
         * The third line's FIXED height in dp, or 0 when the tag has no third line.
         *
         * Every `<Tag>Item` is a stack of `TextFixedHeight`s and the line is reserved whether or
         * not it has text (kt:318-324). 16 is labelMedium (cloud cover, visibility, conditions),
         * 20 labelLarge (wind). ArkUIApp's `SummaryColumn.descriptionLine` is the same field.
         */
        float descriptionLine = 0.0f;
    };
    struct Summary {
        std::vector<SummaryColumn> columns;
    };
    /** One line of a card that lists rather than narrates — the ephemeris times, the pollen grid. */
    struct Row {
        std::string label;
        std::string value;
        uint32_t dotColor = 0;
        /**
         * A THIRD column between the label and the value, empty for a two-column row.
         *
         * The air-quality scale is Description | Harmless exposure | AQI
         * (DetailsAirQuality.kt:725-797) where every other scale table is two columns; the
         * pollutant-details grid reuses it for the progress bar's fraction, 0..1.
         *
         * Appended at the END of the struct deliberately: inserting a field in the middle once
         * already re-pointed every brace initialiser silently, which hid an About card.
         */
        std::string middle;
        /**
         * A `ListItem`'s leading image, as a rawfile name, or "" for a row that has none.
         *
         * Only the Sun & moon card has them: each of its rows is a `ListItem` with a 24 dp leading
         * Image (DetailsSunMoon.kt:339-558). ArkUIApp's `DetailsRow.icon` is the same field.
         */
        std::string icon;
        /**
         * `DailyMoonPhase`'s leading content is a `MoonPhaseView` at this angle, not an image
         * (kt:534-548). Negative means the row is not the moon-phase row.
         */
        double moonPhaseAngle = -1.0;
    };
    struct Section {
        std::string heading;
        std::string subtitle;
        std::vector<std::string> paragraphs;
        std::vector<Row> rows;
        /**
         * A scale table's bold header row - "Description | UV index" (DetailsUV.kt:331-345). Empty
         * when the card is prose or a plain row list.
         *
         * Appended at the END of this struct deliberately: inserting a field in the middle once
         * already re-pointed every brace initialiser silently, which hid an About card.
         */
        std::string headerLabel;
        std::string headerValue;
        /** The header cell above `Row::middle`; empty for a two-column table. */
        std::string headerMiddle;
        /**
         * `AirQualityDetails` (DetailsAirQuality.kt:656-682) — a two-per-row grid of a bold
         * pollutant name, a progress bar in its colour and the concentration under it, rather
         * than the ordinary list of rows. The bar's fraction rides in `Row::middle`.
         */
        bool pollutantGrid = false;
        /**
         * `PollenGrid` (AllergenComposables.kt:54-131) — also two per row, but a different CELL: a
         * material-icon-sized dot in the allergen's colour, then its name in titleMedium over
         * "12/m³ – Low" in bodyMedium. The name is the row's `label`, the subtitle its `value`.
         */
        bool pollenGrid = false;
        /**
         * `true` for the Sun & moon card, whose rows are Material3 `ListItem`s rather than
         * scale-table lines: a fixed 56 dp each, 16 dp of horizontal padding of their own, and no
         * padding on the card. Measured on the reference: 728 px for four rows, 182 apiece.
         */
        bool listItemRows = false;
        /*
         * A scale table's column weights, which are NOT the same in the four tables:
         *   UV                 2   / -   / 1     (DetailsUV.kt:336-342)
         *   cloud, visibility  1.5 / -   / 1     (kt:363-369 / kt:372-386)
         *   Beaufort           1   / 1.5 / 1.5   (DetailsWind.kt:520-535)
         *   air quality        2   / 1.5 / 1.5   (DetailsAirQuality.kt:750-769)
         * This port used to hard-code the middle column at 0.4/0.3 of the card. ArkUIApp carries
         * the same three fields.
         */
        float labelWeight = 1.5f;
        float middleWeight = 1.5f;
        float valueWeight = 1.0f;
    };
    /**
     * A `DetailsItem` row (DetailsCommon.kt:82-135): an optional leading icon, a bold bodyLarge
     * headline with an optional help affordance beside it, and a supporting line under.
     *
     * The only one on the screen so far is the degree day, which the Kotlin draws bare in the list
     * between the chart's explanation and the sections below it (DetailsConditions.kt:284-360).
     */
    struct Item {
        std::string headline;
        std::string supporting;
        /** A rawfile asset name, e.g. `ic_mode_heat.svg`. Empty for no icon. */
        std::string icon;
        /** The PlainTooltip's text. Empty means no help affordance and no tooltip. */
        std::string explanation;
        /**
         * The `Spacer` the Kotlin puts ABOVE this row, in dp — not the same on the two tags that
         * have a `DetailsItem`: the degree day takes a small_margin (DetailsConditions.kt:286-288)
         * and the sunshine duration a normal_margin (DetailsCloudCover.kt:145-147). ArkUIApp's
         * `DetailsItemModel.spacerAbove` is the same field.
         */
        float spacerAbove = dimens::kSmallMargin;
    };
    /**
     * BreezyLineChart's inputs, resolved to display units — the transcription of ArkUIApp's
     * `DetailsChartSpec`.
     *
     * The gradient is the unusual part: ScaleLineFill / ScaleAreaFill (BreezyLineChart.kt:251-325)
     * paint BOTH the stroke and the area with ONE vertical shader built from the tag's value scale,
     * stops at `1 - (y - minY) / (maxY - minY)`. The line's colour at any height is therefore the
     * colour that height means, and the area is the same shader at alpha 128.
     */
    struct ChartStop {
        double value = 0.0;
        uint32_t color = 0;
    };
    struct ChartSpec {
        bool valid = false;
        std::vector<double> xs;
        std::vector<double> ys;
        /**
         * A SECOND line, drawn without an area and in a flat colour. Only Feels like has one: the
         * Kotlin puts the ACTUAL temperature behind the feels-like line in grey at alpha 160
         * (DetailsConditions.kt:869-872).
         */
        std::vector<double> xs2;
        std::vector<double> ys2;
        uint32_t secondaryColor = 0xA0808080;
        double minX = 0.0;
        double maxX = 0.0;
        double minY = 0.0;
        double maxY = 0.0;
        double step = 1.0;
        std::string unitLabel;
        int decimals = 0;
        /**
         * `SpecificVerticalAxisItemPlacer` — a label at each of THESE values instead of on a
         * uniform step. Only the air-quality chart uses one: its right-hand labels sit on the AQI
         * or the pollutant's band edges (DetailsAirQuality.kt:526-538), which are not evenly
         * spaced. Empty everywhere else, and the draw falls back to `step`.
         */
        std::vector<double> axisValues;
        std::vector<ChartStop> stops;
        /** The trendHorizontalLines; wind has TWO, a Beaufort rule at each end of the scale. */
        struct KeyLine {
            double y = 0.0;
            std::string label;
        };
        std::vector<KeyLine> keyLines;
        /** The top axis's trend arrows — Pressure only (DetailsPressure.kt:298-334). */
        std::vector<double> arrowXs;
        /** -1 down, 0 equal, +1 up. */
        std::vector<int> arrowKinds;
        /**
         * The top axis's WEATHER ICONS — the conditions chart's equivalent of Pressure's arrows.
         *
         * `TemperatureChart` (DetailsConditions.kt:834-905) gives the chart a
         * `topAxisSize = BaseAxis.Size.fixed(23.dp)` whose every label is an 18 dp icon for that
         * hour. Same `TimeTopAxisItemPlacer` as the arrows, so the same thinning rule decides how
         * many fit. Non-empty makes the top axis draw icons instead of arrows; no chart has both.
         * ArkUIApp carries the same three parallel lists.
         */
        std::vector<double> topIconXs;
        std::vector<std::string> topIconAssets;
        /*
         * The four fields below are appended at the END for the same reason `Row::middle` was:
         * inserting one in the middle silently re-points every brace initialiser.
         */
        /**
         * The second series' own value scale. Non-empty makes it a FULL series — gradient stroke
         * and area — instead of the flat line above. Only the EphemerisChart has one, where the
         * moon and the sun are two independent lines (DetailsSunMoon.kt:182-193).
         */
        std::vector<ChartStop> stops2;
        /**
         * The area's own gradient when it differs from the line's. `ScaleLineFill` and
         * `ScaleAreaFill` take separate colour maps (BreezyLineChart.kt:251-310); they are the same
         * map everywhere except the ephemeris, whose line fades to 50 % below the horizon where its
         * area fades to nothing. Empty means "use `stops`".
         */
        std::vector<ChartStop> areaStops;
        std::vector<ChartStop> areaStops2;
        /** `PointConnector.cubic(curvature = (PI - 2) / PI)` — the ephemeris chart alone curves. */
        bool curved = false;
        /**
         * A COLUMN layer rather than a line one — `BreezyBarChart` (kt:162-173). Only
         * Precipitation's quantity chart is one.
         */
        bool bars = false;
        /** `barColorFill` — one flat colour for every column (DetailsPrecipitation.kt:377). */
        uint32_t barColor = 0;
    };

    /**
     * `variant` is which chart of the screen this is: Precipitation draws TWO — the quantity bar
     * chart, then the probability line chart under its own heading (kt:166-213). 0 is the first,
     * 1 the second, and every other tag ignores it.
     */
    ChartSpec BuildChart(int variant = 0) const;
    /**
     * The temperature gradient, degrees Celsius converted to the display unit
     * (DetailsConditions.kt:853-866).
     *
     * Shared, because `DewPointChart` uses the SAME ramp — the Kotlin says so in a TODO of its own
     * ("Duplicate of temperature colors", DetailsHumidity.kt:495). One table, two callers.
     */
    static std::vector<ChartStop> TemperatureStops();
    /** The ephemeris chart's two gradients for one body — see the definition. */
    static void ScaleFill(std::vector<ChartStop>* lineStops, std::vector<ChartStop>* areaStops,
                          uint32_t color);
    /** `getMappedAstroValues` (DetailsSunMoon.kt:226-316) — the noon/nadir altitude samples. */
    void MappedAstroValues(bool isSun, int64_t startMs, int64_t endMs, std::vector<double>* xs,
                           std::vector<double>* ys) const;
    /** Returns the height it occupied, so the content column can stack under it. */
    /* Not const: it records the plot extent so a touch can be mapped back to an hour. */
    double DrawChart(OH_Drawing_Canvas* canvas, const ChartSpec& spec, double left, double top,
                     double width, int variant = 0);
    double ChartHeight() const;

    Summary BuildSummary(int variant = 0) const;
    /**
     * The header for ONE hour, when the chart marker is up. Returns false when the tag has no
     * per-hour reading, which leaves the day summary in place.
     */
    bool ActiveColumn(const std::vector<Hourly>& hours, SummaryColumn* out, int variant = 0) const;
    SummaryColumn HalfDayWind(const HalfDay& half, const char* label) const;
    SummaryColumn HalfDayPrecipitation(const HalfDay& half, const char* label) const;
    SummaryColumn HalfDayPrecipitationProbability(const HalfDay& half, const char* label) const;
    /**
     * One row of a two-column detail block: the same label with the daytime value beside the
     * nighttime one — `PrecipitationDetails` (kt:418-475) and its siblings, which build a Daytime
     * column and a Nighttime column of `DetailsItem`s from the SAME list of labels.
     */
    struct Pair {
        std::string label;
        std::string dayValue;
        std::string nightValue;
    };
    std::vector<Pair> PrecipitationPairs(int variant) const;
    /** Returns the height it occupied, so the content column can stack under it. */
    double DrawPairs(OH_Drawing_Canvas* canvas, const std::vector<Pair>& pairs, double left,
                     double top, double width) const;
    /** The probability group's gate (kt:178-181). */
    bool HasProbability() const;
    SummaryColumn HalfDayCondition(const HalfDay& half, const char* label, bool showRealTemp,
                                   bool daylight) const;
    std::vector<Section> Sections() const;
    std::vector<Item> Items() const;
    /** The PlainTooltip bubble, drawn above the row whose help glyph was tapped. */
    void DrawTooltip(OH_Drawing_Canvas* canvas, const std::string& text, double left,
                     double anchorTop, double width) const;
    std::vector<Hourly> HoursForDay(int dayIndex) const;
    std::string DayFullLabel(int dayIndex) const;
    double TabRowHeight() const;
    double ContentTop() const;

    Weather weather_;
    int utcOffsetSeconds_ = 0;
    DetailTag tag_ = DetailTag::Conditions;
    int dayIndex_ = 0;
    int64_t nowMs_ = 0;
    /** The EphemerisChart's only inputs beyond the day itself. */
    double latitude_ = 0.0;
    double longitude_ = 0.0;
    bool hasAirQualitySource_ = false;
    bool hasPollenSource_ = false;
    bool menuOpen_ = false;

    double width_ = 0.0;
    double height_ = 0.0;
    float density_ = 1.0f;
    double topInset_ = 0.0;
    double bottomInset_ = 0.0;
    bool dark_ = false;
    /**
     * SettingsManager.isElementsAnimationEnabled — DetailsConditions.kt:732 branches the condition
     * icon on it. Pushed in with the other main-screen settings.
     */
    bool elementAnimations_ = true;
    /** Wall seconds since this screen opened; the condition icon's pose is a function of it. */
    double conditionIconElapsed_ = 0.0;
    /** Latched by Draw — whether the current tag actually has a condition icon to animate. */
    bool hasConditionIcon_ = false;
    bool keyLinesEnabled_ = true;
    /**
     * The chart MARKER's hour, or -1 (BreezyLineChart's marker + markerVisibilityListener,
     * kt:145-174). Every `<Tag>Header` opens with `if (activeItem != null)`; without this the port
     * could never take that branch and all twelve sat in their day-summary fallback.
     */
    int64_t activeHourMs_ = -1;
    /**
     * The SECOND chart's marker. Precipitation is the one screen with two charts, and the Kotlin
     * gives each its own `activeQuantityItem` / `activeProbabilityItem` (kt:103-151), so touching
     * one leaves the other's header on its day summary.
     */
    int64_t activeHourMs2_ = -1;
    /** The plot's extent from the last draw, so a touch maps back to an hour. */
    double plotLeft_ = 0.0;
    double plotRight_ = 0.0;
    double chartTop_ = 0.0;
    double chartBottom_ = 0.0;
    double chart2Top_ = 0.0;
    double chart2Bottom_ = 0.0;

    /*
     * Both axes go through the shared flywheel rather than a bare offset.
     *
     * A bare offset is what the first cut used, and it made the day pager feel dead: the row moved
     * only while the finger was on it and stopped the instant it lifted, where ArkUIApp's `Scroll`
     * carries a fling. `Scroller` is the same class the main screen's page and trend lists use, so
     * the deceleration curve — and therefore the feel — is identical in both ports.
     */
    Scroller tabScroll_;
    Scroller contentScroll_;
    double contentHeight_ = 0.0;
    double tabRowWidth_ = 0.0;

    /** Where things landed last frame, so a tap matches without a second layout pass. */
    struct Band {
        double left = 0.0;
        double top = 0.0;
        double right = 0.0;
        double bottom = 0.0;
        int index = 0;
    };
    std::vector<Band> dayBands_;
    std::vector<Band> menuBands_;
    /** The Actual / Feels like buttons; `index` is the DetailTag each one selects. */
    std::vector<Band> switcherBands_;
    /** The DetailsItem rows that carry a tooltip; `index` is the row's position in Items(). */
    std::vector<Band> itemBands_;
    /**
     * The open PlainTooltip's text, empty when none is up (DetailsConditions.kt:291-299).
     *
     * The Kotlin's TooltipBox dismisses on the next touch anywhere, which is what the hit test
     * does: any Down clears it before considering anything else.
     */
    std::string tooltipText_;
    /** Which Items() row it belongs to, so the bubble follows the row as the content scrolls. */
    int tooltipItem_ = -1;

    /** `selectedPollutant` — the air-quality picker's choice, "" for the index itself. */
    std::string pollutantId_;
    /** The picker's buttons; `index` is the entry's position, 0 being the AQI. */
    std::vector<Band> pollutantBands_;
    /** The ⋮ overflow, and whether its menu is open. */
    Band pollutantOverflowBand_;
    bool pollutantMenuOpen_ = false;
    std::vector<Band> pollutantMenuBands_;

    /** The picker's entries beyond the AQI — `validPollutants` of the day's reading. */
    std::vector<const PollutantDef*> PollutantChoices() const;
    /** Entry 0 is the AQI itself (`air_quality_index_short`); the rest are pollutants. */
    std::string PollutantLabel(int index) const;
    std::string PollutantIdAt(int index) const;
    int SelectedPollutantIndex() const;
    void SelectPollutant(const std::string& id);
    /** `selectedAirQuality` (kt:160-166): the day's average, else the current hour. */
    const AirQuality* SelectedAirQuality() const;
    SummaryColumn AirQualityColumn(const std::string& label, const AirQuality* air) const;
    std::vector<Section> AirQualitySections(const AirQuality* air) const;
    Band fabBand_;
    Band backBand_;
};

}  // namespace breezy

#endif  // BREEZY_VIEW_DETAILSSCREEN_H
