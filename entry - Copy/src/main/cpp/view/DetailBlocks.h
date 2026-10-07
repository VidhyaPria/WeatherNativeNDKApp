/*
 * The half-size detail blocks below the hourly card.
 *
 * Order comes from DEFAULT_CARD_DISPLAY (SettingsManager.kt:61-73): precipitation, wind,
 * air_quality, pollen, humidity, uv, visibility, pressure, sun, moon. ViewType.isHalfSizeableBlock
 * (ViewType.kt:39-45) marks all of them half-width, so they lay out two per row on a phone.
 *
 * Each block is gated on its value being present, exactly as MainAdapter.kt:122-170 does — a card
 * whose value is null is not added to the list at all.
 *
 * Ported from ArkUIApp's view/main/DetailBlocks.ets.
 */
#ifndef BREEZY_VIEW_DETAILBLOCKS_H
#define BREEZY_VIEW_DETAILBLOCKS_H

#include <cstdint>
#include <string>
#include <vector>

#include "model/Weather.h"

namespace breezy {

struct DetailBlock {
    std::string id;
    /** rawfile name of the title icon. */
    std::string icon;
    std::string title;
    /** Big 45sp number (Weather.TextAppearance.MainBlock.NumberDigit, styles.xml:310-314). */
    std::string value;
    /** Unit, rendered smaller beside the number — "mm", "m/s", "%". */
    std::string unit;
    std::string secondaryLabel;
    std::string secondaryValue;
    /**
     * Draw `secondaryValue` inside a filled circle instead of as plain text — the humidity block's
     * dew-point badge (container_main_humidity.xml `dew_point_circle`). No other block has one.
     */
    bool secondaryIsChip = false;

    /**
     * Circle vs rounded rectangle is a property of the card TYPE, not of its position: each
     * layout declares app:shapeAppearance — Corner.Full on wind, air quality, visibility and
     * pressure; Corner.ExtraLarge on precipitation, pollen, humidity and astro.
     */
    bool isCircle = false;
    /**
     * container_main_uv.xml has no MaterialCardView at all — its root is a bare ConstraintLayout,
     * so the UV block draws only its sun graphic with no card behind it.
     */
    bool hasCard = true;

    /**
     * Guideline percentages, read off each container_main_*.xml. There is no shared value:
     *   air_quality, pressure, visibility, wind   0.2 / 0.8
     *   uv                                       0.2 / 0.77 vertical, 0.2 / 0.8 horizontal
     *   astro, pollen                            0.1 / 0.9
     *   humidity, precipitation                  0.08 / 0.92
     */
    double insetTopPercent = 0.08;
    double insetBottomPercent = 0.08;
    double insetHorizontalPercent = 0.08;
    /**
     * container_main_pressure.xml:99-126 is the only block that puts its unit on its own line,
     * centred, rather than beside the number.
     */
    bool unitOnOwnLine = false;

    /** Vector fill behind the content — the humidity waves, visibility blob, wind arrow. */
    std::string shape;
    uint32_t shapeTint = 0;
    /** Cover crops to fill; the wind arrow and the UV sun need to stay whole. */
    bool shapeFitsInside = false;
    double shapeRotation = 0.0;

    /** Ring gauge (ArcProgress). Negative means the block has no gauge. */
    double arcProgress = -1.0;
    double arcMax = 100.0;
    uint32_t arcColor = 0;
    uint32_t arcBackground = 0;

    /** Allergen rows — when present the block lists these instead of a big number. */
    std::vector<PollenEntry> pollenRows;

    /**
     * The Clock block (container_main_clock.xml), which shares nothing with the other tiles: no
     * title row, no value line, no unit â€” a scalloped shape with the hour over the minute and a
     * pair of analog hands on top. The painter branches on this rather than trying to express it
     * through the value/secondary fields.
     */
    bool isClock = false;
    /** Local hour 0-23 and minute 0-59 in the LOCATION's zone (ClockViewHolder.kt:61-62). */
    int clockHour = 0;
    int clockMinute = 0;
    /** `format12Hour="hh"` vs `format24Hour="HH"` (container_main_clock.xml:79-80). */
    bool clockIs24Hour = true;
    /** ?attr/colorPrimary â€” both TextClocks' textColor (container_main_clock.xml:75, :91). */
    uint32_t clockDigitColor = 0;

    /** Astro block: rise and set beneath an arc, plus the moon's phase. */
    bool hasAstro = false;
    Astro astro;
    bool astroIsSun = true;
    std::string astroIcon;
    uint32_t astroLineColor = 0;
    uint32_t astroTrackColor = 0;
    double astroProgress = -1.0;
};

std::vector<DetailBlock> BuildDetailBlocks(const Weather& weather, bool darkMode, int64_t nowMs,
                                           int utcOffsetSeconds, bool is24Hour);

}  // namespace breezy

#endif  // BREEZY_VIEW_DETAILBLOCKS_H
