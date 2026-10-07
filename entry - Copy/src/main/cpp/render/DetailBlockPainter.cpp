#include "render/DetailBlockPainter.h"

#include <native_drawing/drawing_brush.h>
#include <native_drawing/drawing_path.h>
#include <native_drawing/drawing_path_effect.h>
#include <native_drawing/drawing_pen.h>
#include <native_drawing/drawing_rect.h>
#include <native_drawing/drawing_round_rect.h>

#include <algorithm>
#include <cmath>

#include "common/LocalTime.h"
#include "render/AssetImages.h"
#include "render/Shapes.h"
#include "render/TextPainter.h"
#include "render/Theme.h"

namespace breezy {
namespace {

/** styles.xml:310-314 — Weather.TextAppearance.MainBlock.NumberDigit. */
constexpr double kNumberDigitTextSize = 45.0;
/** The unit sits noticeably smaller than the number. */
constexpr double kUnitTextSize = 20.0;
/** container_main_humidity.xml:85 — gap between the title icon and the title. */
constexpr double kTitleIconGap = 4.0;
/** Level dot beside each allergen row. */
constexpr double kPollenDotSize = 20.0;
/** ArcProgress: both layouts pin app:arc_angle="270", so the ring is open at the bottom. */
constexpr double kArcAngle = 270.0;
constexpr double kArcStrokeRatio = 0.075;
/** SunMoonView.kt:330-338 */
constexpr double kAstroArcAngle = 135.0;
constexpr double kAstroLineSize = 5.0;
/** MoonViewHolder.kt — colors.xml `colorTextLight2nd` / `colorTextDark2nd`, literal, not themed. */
constexpr uint32_t kMoonPhaseLight = 0xFF999999;
constexpr uint32_t kMoonPhaseDark = 0xFF666666;
/** SunMoonView.kt:333 — the dashed track is a HAIRLINE, a fifth of the elapsed arc's width. */
constexpr double kAstroDottedLineSize = 1.0;
constexpr double kAstroIconSize = 24.0;

constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

// The moon's terminator seam used a file-local `StrokeLine` that was a duplicate of the one in
// render/Shapes.h, already included above. Once `DrawMoonPhase` moved out of this anonymous
// namespace so the details screen could call it, the two became ambiguous — which is the whole
// argument against the copy. Deleted; Shapes.h's is the one.

/** A stroked arc inside `rect`, swept clockwise from `startAngle`. */
void StrokeArc(OH_Drawing_Canvas* canvas, double left, double top, double side, double startAngle,
               double sweepAngle, double strokeWidth, uint32_t color, bool round,
               bool dashed = false) {
    if (sweepAngle <= 0.0) {
        return;
    }
    OH_Drawing_Pen* pen = OH_Drawing_PenCreate();
    OH_Drawing_PenSetAntiAlias(pen, true);
    OH_Drawing_PenSetColor(pen, color);
    OH_Drawing_PenSetWidth(pen, static_cast<float>(strokeWidth));
    OH_Drawing_PenSetCap(pen, round ? LINE_ROUND_CAP : LINE_FLAT_CAP);

    // SunMoonView.kt:95-101 — DashPathEffect(3 dp, 2 × 3 dp) on the track only.
    OH_Drawing_PathEffect* dash = nullptr;
    if (dashed) {
        const float density = DisplayDensity();
        const float intervals[2] = {3.0f * density, 6.0f * density};
        dash = OH_Drawing_CreateDashPathEffect(const_cast<float*>(intervals), 2, 0.0f);
        OH_Drawing_PenSetPathEffect(pen, dash);
    }

    OH_Drawing_Rect* rect = OH_Drawing_RectCreate(
        static_cast<float>(left), static_cast<float>(top), static_cast<float>(left + side),
        static_cast<float>(top + side));
    OH_Drawing_CanvasAttachPen(canvas, pen);
    OH_Drawing_CanvasDrawArc(canvas, rect, static_cast<float>(startAngle),
                             static_cast<float>(sweepAngle));
    OH_Drawing_CanvasDetachPen(canvas);
    OH_Drawing_RectDestroy(rect);
    if (dash != nullptr) {
        OH_Drawing_PathEffectDestroy(dash);
    }
    OH_Drawing_PenDestroy(pen);
}

TextStyleSpec Style(double sizeVp, uint32_t color, bool bold, TextAlignment alignment) {
    TextStyleSpec style;
    style.sizeVp = sizeVp;
    style.color = color;
    style.bold = bold;
    style.alignment = alignment;
    style.singleLine = true;
    return style;
}

/**
 * ArcProgress.onDraw (kt:127-136) — the ring gauge behind the air-quality and pressure blocks.
 * startAngle = 270 - arcAngle / 2, i.e. it starts bottom-left and sweeps clockwise.
 */
void DrawArcGauge(OH_Drawing_Canvas* canvas, const DetailBlock& block, double left, double top,
                  double side) {
    if (block.arcProgress < 0.0) {
        return;
    }
    const double stroke = side * kArcStrokeRatio;
    const double inset = stroke / 2.0;
    const double startAngle = 270.0 - kArcAngle / 2.0;
    StrokeArc(canvas, left + inset, top + inset, side - stroke, startAngle, kArcAngle, stroke,
              block.arcBackground, false);
    const double fraction = std::clamp(block.arcProgress / block.arcMax, 0.0, 1.0);
    StrokeArc(canvas, left + inset, top + inset, side - stroke, startAngle, kArcAngle * fraction,
              stroke, block.arcColor, true);
}

/**
 * SunMoonView.kt:214-274 — a 135° slice of a circle, not a bezier dome. The radius is derived
 * from the available width so the arc's two ends land exactly on the left and right insets.
 */
void DrawAstroArc(OH_Drawing_Canvas* canvas, const DetailBlock& block, double left, double top,
                  double width) {
    const float density = DisplayDensity();
    const double delta = (180.0 - kAstroArcAngle) / 2.0 * kDegToRad;  // kt:216
    const double radius = width / 2.0 / std::cos(delta);              // kt:217
    const double height = radius - width / 2.0 * std::tan(delta);     // kt:218
    const double centreX = left + width / 2.0;
    const double centreY = top + radius;
    const double stroke = kAstroLineSize * density;

    const double startAngle = 180.0 + (180.0 - kAstroArcAngle) / 2.0;
    // kt:246-249 — the track is drawn with mDottedLineSize, not mLineSize; the port used the same
    // 5 dp for both, which made the dashes read as a row of fat dots instead of a hairline.
    StrokeArc(canvas, centreX - radius, centreY - radius, radius * 2.0, startAngle,
              kAstroArcAngle, kAstroDottedLineSize * density, block.astroTrackColor, true, true);

    if (block.astroProgress <= 0.0) {
        return;  // kt:267 — nothing but the track when the body has not risen.
    }
    const double sweep = kAstroArcAngle * std::min(block.astroProgress, 1.0);
    StrokeArc(canvas, centreX - radius, centreY - radius, radius * 2.0, startAngle, sweep, stroke,
              block.astroLineColor, true);

    // The icon rides the head of the elapsed arc (kt:266-273).
    const double headAngle = (startAngle + sweep) * kDegToRad;
    const double iconSize = kAstroIconSize * density;
    DrawAsset(canvas, block.astroIcon, centreX + radius * std::cos(headAngle) - iconSize / 2.0,
              centreY + radius * std::sin(headAngle) - iconSize / 2.0, iconSize, 0, false);
    (void)height;
}

/**
 * MoonPhaseView.kt — the lit fraction of the disc for a phase angle.
 *
 * A transcription of onDraw's eight cases, not an approximation. `surfaceAngle` is the head of the
 * lit surface measured clockwise: 0 new, 90 first quarter, 180 full, 270 last quarter. Android's
 * arc angles start at 3 o'clock and sweep clockwise, so `drawArc(90, 180)` is the LEFT half and
 * `drawArc(270, 180)` the RIGHT half — which is what decides the side the light is on, and the
 * port previously had it inverted.
 *
 * The half-disc is followed by an ellipse of half-width |cos| × radius, which is how a terminator
 * projects: it eats into the lit side for a crescent and extends it for a gibbous.
 */
}  // namespace

void DrawMoonPhase(OH_Drawing_Canvas* canvas, double left, double top, double size, int angle,
                   uint32_t light, uint32_t dark, uint32_t stroke) {
    if (angle < 0 || size <= 0.0) {
        return;
    }
    // onMeasure insets the disc by 4 dp inside its square.
    const double padding = 4.0 * DisplayDensity();
    const double discLeft = left + padding;
    const double discTop = top + padding;
    const double discSize = size - padding * 2.0;
    if (discSize <= 0.0) {
        return;
    }
    const double radius = discSize / 2.0;
    const double cx = discLeft + radius;
    const double cy = discTop + radius;
    const double a = static_cast<double>(angle % 360);

    OH_Drawing_Brush* brush = OH_Drawing_BrushCreate();
    OH_Drawing_BrushSetAntiAlias(brush, true);

    const auto fillDisc = [&](uint32_t color) {
        OH_Drawing_BrushSetColor(brush, color);
        OH_Drawing_CanvasAttachBrush(canvas, brush);
        OH_Drawing_Point* centre =
            OH_Drawing_PointCreate(static_cast<float>(cx), static_cast<float>(cy));
        OH_Drawing_CanvasDrawCircle(canvas, centre, static_cast<float>(radius));
        OH_Drawing_PointDestroy(centre);
    };
    /** `startAngle` 90 is the left half, 270 the right — Android's convention exactly. */
    const auto fillHalf = [&](uint32_t color, double startAngle, double halfWidth) {
        OH_Drawing_BrushSetColor(brush, color);
        OH_Drawing_CanvasAttachBrush(canvas, brush);
        OH_Drawing_Rect* rect = OH_Drawing_RectCreate(
            static_cast<float>(cx - halfWidth), static_cast<float>(discTop),
            static_cast<float>(cx + halfWidth), static_cast<float>(discTop + discSize));
        OH_Drawing_CanvasDrawArc(canvas, rect, static_cast<float>(startAngle), 180.0f);
        OH_Drawing_RectDestroy(rect);
    };

    if (a == 0.0) {                     // new
        fillDisc(dark);
    } else if (a < 90.0) {              // waxing crescent
        fillDisc(light);
        fillHalf(dark, 90.0, radius);
        fillHalf(dark, 270.0, radius * std::cos(a * kDegToRad));
    } else if (a == 90.0) {             // first quarter
        fillDisc(dark);
        fillHalf(light, 270.0, radius);
    } else if (a < 180.0) {             // waxing gibbous
        fillDisc(dark);
        fillHalf(light, 270.0, radius);
        fillHalf(light, 90.0, radius * std::sin((a - 90.0) * kDegToRad));
    } else if (a == 180.0) {            // full
        fillDisc(light);
    } else if (a < 270.0) {             // waning gibbous
        fillDisc(dark);
        fillHalf(light, 90.0, radius);
        fillHalf(light, 270.0, radius * std::cos((a - 180.0) * kDegToRad));
    } else if (a == 270.0) {            // last quarter
        fillDisc(dark);
        fillHalf(light, 90.0, radius);
    } else {                            // waning crescent
        fillDisc(light);
        fillHalf(dark, 270.0, radius);
        fillHalf(dark, 90.0, radius * std::cos((360.0 - a) * kDegToRad));
    }
    OH_Drawing_CanvasDetachBrush(canvas);
    OH_Drawing_BrushDestroy(brush);

    // The terminator seam, then the outline (kt:150-177).
    const double lineWidth = DisplayDensity();
    if (a < 90.0 || a > 270.0) {
        StrokeLine(canvas, cx, discTop, cx, discTop + discSize, lineWidth, dark);
    } else if (a > 90.0 && a < 270.0) {
        StrokeLine(canvas, cx, discTop, cx, discTop + discSize, lineWidth, light);
    }
    StrokeArc(canvas, discLeft, discTop, discSize, 0.0, 360.0, lineWidth, stroke, false);
}

namespace {

/** "5:11 AM" / "9:39 PM", or "05:11" on a 24-hour device. */
std::string ClockTime(int64_t instantMs, int utcOffsetSeconds, bool is24Hour) {
    const int hours = LocalHours(instantMs, utcOffsetSeconds);
    const int minutes = LocalMinutes(instantMs, utcOffsetSeconds);
    const std::string mm = minutes < 10 ? "0" + std::to_string(minutes) : std::to_string(minutes);
    if (is24Hour) {
        const std::string hh = hours < 10 ? "0" + std::to_string(hours) : std::to_string(hours);
        return hh + ":" + mm;
    }
    const int display = hours % 12 == 0 ? 12 : hours % 12;
    return std::to_string(display) + ":" + mm + (hours < 12 ? " AM" : " PM");
}

/* --- The Clock block (container_main_clock.xml) ---------------------------------------------- */

/**
 * container_main_clock.xml:57-99 - the two TextClocks.
 *
 * `autoSizeTextType="uniform"` over 50..130sp at 1sp granularity, in a box that is 80 % of the
 * square split in half. Rather than reimplement autosizing, the settled size was MEASURED off the
 * reference: the digits paint 138 px tall on a 552 px card, and Roboto's cap height is 0.711 em, so
 * 138 / 0.711 / 3.25 px-per-vp = 59.7 -> 60. That it lands on a whole step of the 1sp granularity
 * is the check that it is the size autosize actually chose, not merely a number that fits.
 */
constexpr double kClockDigitTextSizeVp = 60.0;

/*
 * The two hands of the AnalogClock (container_main_clock.xml:101-105).
 *
 * `android:dialTint="@android:color/transparent"` hides the framework's dial, so the only thing
 * that view contributes is a pair of hands over the shape and the digits.
 *
 * These are the PLATFORM's hands, so there is no app resource to read them from - every number
 * here was measured off the reference. At 05:51 on a 552 px card, with the clock centre at
 * (931.5, 2287.5): the minute hand's colour occupies a 178 x 145 bbox at (777, 2166) and the hour
 * hand's a 54 x 129 bbox at (910, 2295). Solving each against its known angle (51 min = 306 deg,
 * 5.85 h = 175.5 deg) for tip distance and cap radius gives the fractions below.
 *
 * The colours are NOT theme-derived: sampled in light AND dark mode they are identical, because
 * they belong to the framework drawable rather than the app palette. Hence plain constants and no
 * ColorPair. See ClockHandsCanvas.ets, which carries the same numbers.
 */
constexpr double kClockHandStrokeFraction = 0.083;
constexpr double kClockHourHandFraction = 0.205;
constexpr double kClockMinuteHandFraction = 0.294;
/** system_accent2_500, in both themes. */
constexpr uint32_t kClockMinuteHandColor = 0xFF657985;
/** system_accent1_700, in both themes. */
constexpr uint32_t kClockHourHandColor = 0xFF004C69;

/** One hand from the centre outwards. `turn` is 0..1 clockwise from 12, with a round cap. */
void DrawClockHand(OH_Drawing_Canvas* canvas, double cx, double cy, double turn, double length,
                   double strokeWidth, uint32_t color) {
    const double angle = turn * 2.0 * M_PI;
    OH_Drawing_Pen* pen = OH_Drawing_PenCreate();
    OH_Drawing_PenSetAntiAlias(pen, true);
    OH_Drawing_PenSetColor(pen, color);
    OH_Drawing_PenSetWidth(pen, static_cast<float>(strokeWidth));
    OH_Drawing_PenSetCap(pen, LINE_ROUND_CAP);
    OH_Drawing_CanvasAttachPen(canvas, pen);
    OH_Drawing_Path* path = OH_Drawing_PathCreate();
    OH_Drawing_PathMoveTo(path, static_cast<float>(cx), static_cast<float>(cy));
    OH_Drawing_PathLineTo(path, static_cast<float>(cx + std::sin(angle) * length),
                          static_cast<float>(cy - std::cos(angle) * length));
    OH_Drawing_CanvasDrawPath(canvas, path);
    OH_Drawing_PathDestroy(path);
    OH_Drawing_CanvasDetachPen(canvas);
    OH_Drawing_PenDestroy(pen);
}

/**
 * The whole clock content: the hour over the minute, then the hands on top.
 *
 * The scalloped shape is already painted by the caller through the ordinary `block.shape` path,
 * so this draws only what sits on it.
 */
void DrawClockBlock(OH_Drawing_Canvas* canvas, const DetailBlock& block, double left, double top,
                    double side, float density) {
    /*
     * The LinearLayout is `match_parent` WIDE and 0dp tall between the 10 % and 90 % guidelines, so
     * it ignores the 0.2/0.8 vertical guidelines it is also constrained to - a match_parent child
     * overrides its own side constraints. Both TextClocks carry layout_weight="1", so each takes
     * exactly half; the hour is gravity bottom and the minute gravity top, which is what closes the
     * gap between the two rows of digits.
     */
    const double boxTop = top + side * block.insetTopPercent;
    const double boxHeight = side * (1.0 - block.insetTopPercent - block.insetBottomPercent);
    const double halfHeight = boxHeight / 2.0;

    TextStyleSpec style;
    style.sizeVp = kClockDigitTextSizeVp;
    // ?attr/colorPrimary (container_main_clock.xml:75, :91), resolved by the builder.
    style.color = block.clockDigitColor;
    style.alignment = TextAlignment::Center;
    style.singleLine = true;

    const int displayHour = block.clockIs24Hour
                                ? block.clockHour
                                : (block.clockHour % 12 == 0 ? 12 : block.clockHour % 12);
    const std::string hh = displayHour < 10 ? "0" + std::to_string(displayHour)
                                            : std::to_string(displayHour);
    const std::string mm = block.clockMinute < 10 ? "0" + std::to_string(block.clockMinute)
                                                  : std::to_string(block.clockMinute);

    // Each digit row is drawn into its own half at a FIXED line height, so the pair sits exactly
    // where gravity bottom / gravity top put it rather than wherever the font's default leading
    // lands. This is the k*LineHeight discipline the rest of the port uses.
    DrawTextFixed(canvas, hh, left, boxTop, side, style, halfHeight);
    DrawTextFixed(canvas, mm, left, boxTop + halfHeight, side, style, halfHeight);

    // The AnalogClock sits LAST in the layout, so its hands are drawn over both the shape and the
    // digits - clearly visible crossing the "05" in the reference capture.
    const double cx = left + side / 2.0;
    const double cy = top + side / 2.0;
    const double stroke = side * kClockHandStrokeFraction;
    // Both hands sweep continuously, as AnalogClock's do: the hour hand is at (h + m/60)/12 of a
    // turn, which is why the reference's hour hand sits just past the 5 at 05:51.
    const double minuteTurn = block.clockMinute / 60.0;
    const double hourTurn = ((block.clockHour % 12) + block.clockMinute / 60.0) / 12.0;
    DrawClockHand(canvas, cx, cy, hourTurn, side * kClockHourHandFraction, stroke,
                  kClockHourHandColor);
    DrawClockHand(canvas, cx, cy, minuteTurn, side * kClockMinuteHandFraction, stroke,
                  kClockMinuteHandColor);
    (void)density;
}

}  // namespace

void DrawDetailBlock(OH_Drawing_Canvas* canvas, const DetailBlock& block, double left, double top,
                     double side, const DetailBlockColors& colors, int utcOffsetSeconds,
                     bool is24Hour) {
    const float density = DisplayDensity();

    if (block.hasCard) {
        // Corner.Full is a circle; Corner.ExtraLarge is the 28 dp card radius.
        const double radius = block.isCircle ? side / 2.0 : dimens::kCardCornerRadius * density;
        FillCard(canvas, left, top, left + side, top + side, radius, colors.cardBackground,
                 density);
    }

    // The vector fill sits behind the content, clipped to the card's own shape — the humidity
    // waves run to the card's bottom edge and would otherwise square off its rounded corners.
    if (!block.shape.empty()) {
        OH_Drawing_CanvasSave(canvas);
        if (block.hasCard) {
            OH_Drawing_Rect* bounds = OH_Drawing_RectCreate(
                static_cast<float>(left), static_cast<float>(top),
                static_cast<float>(left + side), static_cast<float>(top + side));
            const float radius = static_cast<float>(
                block.isCircle ? side / 2.0 : dimens::kCardCornerRadius * density);
            OH_Drawing_RoundRect* round = OH_Drawing_RoundRectCreate(bounds, radius, radius);
            OH_Drawing_CanvasClipRoundRect(canvas, round, INTERSECT, true);
            OH_Drawing_RoundRectDestroy(round);
            OH_Drawing_RectDestroy(bounds);
        }
        if (block.shapeFitsInside) {
            // The wind arrow and the UV sun must stay whole, so they are inset rather than cropped.
            const double inset = side * 0.18;
            const double shapeSide = side - inset * 2.0;
            if (block.shapeRotation != 0.0) {
                OH_Drawing_CanvasSave(canvas);
                OH_Drawing_CanvasTranslate(canvas, static_cast<float>(left + side / 2.0),
                                           static_cast<float>(top + side / 2.0));
                OH_Drawing_CanvasRotate(canvas, static_cast<float>(block.shapeRotation), 0.0f,
                                        0.0f);
                DrawAsset(canvas, block.shape, -shapeSide / 2.0, -shapeSide / 2.0, shapeSide,
                          block.shapeTint, block.shapeTint != 0);
                OH_Drawing_CanvasRestore(canvas);
            } else {
                DrawAsset(canvas, block.shape, left + inset, top + inset, shapeSide,
                          block.shapeTint, block.shapeTint != 0);
            }
        } else {
            DrawAsset(canvas, block.shape, left, top, side, block.shapeTint,
                      block.shapeTint != 0);
        }
        OH_Drawing_CanvasRestore(canvas);
    }

    // The clock shares the shape mechanism above and nothing else — no gauge, no title row, no
    // value line — so it takes over here rather than threading four exceptions through the rest.
    if (block.isClock) {
        DrawClockBlock(canvas, block, left, top, side, density);
        return;
    }

    DrawArcGauge(canvas, block, left, top, side);

    const double insetX = side * block.insetHorizontalPercent;
    const double insetTop = side * block.insetTopPercent;
    const double contentLeft = left + insetX;
    const double contentWidth = side - insetX * 2.0;
    // Circular blocks centre their title, value and secondary line; rectangular ones left-align.
    const TextAlignment align = block.isCircle ? TextAlignment::Center : TextAlignment::Left;

    double y = top + insetTop;

    // Title row — icon then title, the icon sized to the title text.
    const TextStyleSpec titleStyle =
        Style(dimens::kTitleTextSize, colors.content, true, TextAlignment::Left);
    const double titleHeight = MeasureTextHeight(block.title, contentWidth, titleStyle);
    const double titleWidth = MeasureTextWidth(block.title, titleStyle);
    const double iconSize = titleHeight;
    const double rowWidth = iconSize + kTitleIconGap * density + titleWidth;
    const double rowLeft = block.isCircle ? contentLeft + (contentWidth - rowWidth) / 2.0
                                          : contentLeft;
    DrawAsset(canvas, block.icon, rowLeft, y, iconSize, colors.content, true);
    DrawText(canvas, block.title, rowLeft + iconSize + kTitleIconGap * density, y,
             contentWidth, titleStyle);
    y += titleHeight;

    if (block.hasAstro) {
        // The arc fills the width between the guidelines, with the rise and set times beneath it.
        const double arcTop = y + 4.0 * density;
        DrawAstroArc(canvas, block, contentLeft, arcTop, contentWidth);

        /*
         * container_main_astro.xml — the phase name is the BOTTOM row: `description` is pinned by
         * `layout_constraintBaseline_toBottomOf="@id/block_bottom_guideline"`, and the rise and set
         * times sit above it, constrained `Top_toBottomOf` the SunMoonView. The port had the two
         * rows the other way round, which ArkUIApp does not.
         */
        const TextStyleSpec timeStyle =
            Style(dimens::kContentTextSize, colors.content, false, TextAlignment::Left);
        const double rowHeight = MeasureTextHeight("0", contentWidth, timeStyle);
        const double bottom = top + side - insetTop;

        double timesY = bottom - rowHeight;
        if (!block.astro.phaseName.empty()) {
            const TextStyleSpec phaseStyle =
                Style(dimens::kContentTextSize, colors.secondary, false, TextAlignment::Left);
            const double phaseHeight =
                MeasureTextHeight(block.astro.phaseName, contentWidth, phaseStyle);
            const double phaseY = bottom - phaseHeight;
            timesY = phaseY - rowHeight - 2.0 * density;

            const double discSize = phaseHeight;
            const double nameWidth = MeasureTextWidth(block.astro.phaseName, phaseStyle);
            const double groupLeft =
                contentLeft + (contentWidth - discSize - kTitleIconGap * density - nameWidth) / 2.0;
            // MoonViewHolder.kt — colorTextLight2nd lit, colorTextDark2nd unlit, bodyText outline.
            // Both fills are literal greys, not theme tokens; only the outline follows the theme.
            DrawMoonPhase(canvas, groupLeft, phaseY, discSize, block.astro.phaseAngle,
                          kMoonPhaseLight, kMoonPhaseDark, colors.secondary);
            DrawText(canvas, block.astro.phaseName,
                     groupLeft + discSize + kTitleIconGap * density, phaseY, contentWidth,
                     phaseStyle);
        }
        if (block.astro.hasRise) {
            DrawText(canvas, ClockTime(block.astro.rise, utcOffsetSeconds, is24Hour), contentLeft,
                     timesY, contentWidth, timeStyle);
        }
        if (block.astro.hasSet) {
            TextStyleSpec right = timeStyle;
            right.alignment = TextAlignment::Right;
            DrawText(canvas, ClockTime(block.astro.set, utcOffsetSeconds, is24Hour), contentLeft,
                     timesY, contentWidth, right);
        }
        return;
    }

    if (!block.pollenRows.empty()) {
        // Pollen lists its allergens instead of a big number: a level dot, the name, the level.
        const double dot = kPollenDotSize * density;
        const TextStyleSpec nameStyle =
            Style(dimens::kTitleTextSize, colors.content, false, TextAlignment::Left);
        const TextStyleSpec levelStyle =
            Style(dimens::kContentTextSize, colors.secondary, false, TextAlignment::Left);
        double rowY = y + 8.0 * density;
        for (const PollenEntry& row : block.pollenRows) {
            const double nameHeight = MeasureTextHeight(row.name, contentWidth, nameStyle);
            const double levelHeight = MeasureTextHeight(row.levelName, contentWidth, levelStyle);
            const double rowHeight = nameHeight + levelHeight;
            FillRoundRect(canvas, contentLeft, rowY + (rowHeight - dot) / 2.0, contentLeft + dot,
                          rowY + (rowHeight + dot) / 2.0, dot / 2.0,
                          kPollenLevelColors[std::max(0, std::min(row.level, 4))]);
            const double textLeft = contentLeft + dot + kTitleIconGap * density * 2.0;
            DrawText(canvas, row.name, textLeft, rowY, contentWidth, nameStyle);
            DrawText(canvas, row.levelName, textLeft, rowY + nameHeight, contentWidth, levelStyle);
            rowY += rowHeight + 8.0 * density;
        }
        return;
    }

    /*
     * The big number with its unit. The two are laid out as a row and placed as a group, so each
     * is measured and positioned by hand rather than aligned independently — except on pressure,
     * where container_main_pressure.xml puts the unit on its own line.
     */
    const TextStyleSpec valueStyle =
        Style(kNumberDigitTextSize, colors.content, false, TextAlignment::Left);
    const TextStyleSpec unitStyle =
        Style(kUnitTextSize, colors.content, false, TextAlignment::Left);
    const double valueHeight = MeasureTextHeight(block.value, contentWidth, valueStyle);
    const double valueWidth = MeasureTextWidth(block.value, valueStyle);
    const double unitWidth = block.unitOnOwnLine ? 0.0 : MeasureTextWidth(" " + block.unit,
                                                                          unitStyle);

    // The value block is centred in what is left between the title and the secondary row.
    const double secondaryStyleSize = dimens::kContentTextSize;
    const TextStyleSpec secondaryStyle =
        Style(secondaryStyleSize, colors.secondary, false, align);
    const double secondaryHeight =
        block.secondaryLabel.empty()
            ? 0.0
            : MeasureTextHeight(block.secondaryLabel, contentWidth, secondaryStyle);

    const double available = side - insetTop - side * block.insetBottomPercent - titleHeight;
    double valueY = y + std::max(0.0, (available - valueHeight - secondaryHeight) / 2.0);

    const double groupWidth = valueWidth + unitWidth;
    const double groupLeft = block.isCircle ? contentLeft + (contentWidth - groupWidth) / 2.0
                                            : contentLeft;
    DrawText(canvas, block.value, groupLeft, valueY, valueWidth + 1.0, valueStyle);
    if (!block.unit.empty() && !block.unitOnOwnLine) {
        // Sits on the number's baseline, which is the bottom of its box.
        const double unitHeight = MeasureTextHeight(block.unit, contentWidth, unitStyle);
        DrawText(canvas, " " + block.unit, groupLeft + valueWidth,
                 valueY + valueHeight - unitHeight, unitWidth + 1.0, unitStyle);
    }
    valueY += valueHeight;

    if (block.unitOnOwnLine && !block.unit.empty()) {
        TextStyleSpec ownLine = Style(dimens::kTitleTextSize, colors.content, false, align);
        valueY += DrawText(canvas, block.unit, contentLeft, valueY, contentWidth, ownLine);
    }

    if (!block.secondaryLabel.empty()) {
        if (block.secondaryIsChip && !block.secondaryValue.empty()) {
            /*
             * dew_point_circle: an oval solid-filled with ?attr/colorTertiary carrying the value
             * centred in ?attr/colorOnTertiary, then the label 8 dp further on in
             * ?attr/colorOnTertiaryContainer. Android sizes the circle 1:1 from the space left
             * under humidity_value; the value's own height plus the layout's 12 dp padding gives
             * the same circle without a second measure pass.
             */
            const double padding = dimens::kDewPointCirclePadding * density;
            TextStyleSpec chipStyle =
                Style(dimens::kTitleTextSize, colors.onTertiary, true, TextAlignment::Center);
            const double textHeight =
                MeasureTextHeight(block.secondaryValue, contentWidth, chipStyle);
            const double diameter = textHeight + padding * 2.0;

            FillCircle(canvas, contentLeft + diameter / 2.0, valueY + diameter / 2.0,
                       diameter / 2.0, colors.tertiary);
            DrawText(canvas, block.secondaryValue, contentLeft, valueY + padding, diameter,
                     chipStyle);

            const double labelLeft = contentLeft + diameter + dimens::kSmallMargin * density;
            TextStyleSpec labelStyle = Style(dimens::kContentTextSize, colors.onTertiaryContainer,
                                             false, TextAlignment::Left);
            const double labelHeight =
                MeasureTextHeight(block.secondaryLabel, contentWidth - diameter, labelStyle);
            // The label is centred on the circle, not aligned to its top.
            DrawText(canvas, block.secondaryLabel, labelLeft,
                     valueY + (diameter - labelHeight) / 2.0,
                     std::max(0.0, contentLeft + contentWidth - labelLeft), labelStyle);
        } else {
            const std::string text = block.secondaryValue.empty()
                                         ? block.secondaryLabel
                                         : block.secondaryValue + " " + block.secondaryLabel;
            DrawText(canvas, text, contentLeft, valueY, contentWidth, secondaryStyle);
        }
    }
}

double DrawDetailBlocks(OH_Drawing_Canvas* canvas, double topPx, double widthPx,
                        const std::vector<DetailBlock>& blocks, const DetailBlockColors& colors,
                        int utcOffsetSeconds, bool is24Hour) {
    if (blocks.empty()) {
        return 0.0;
    }
    const float density = DisplayDensity();
    // MainLayoutManager.getBlocksPerRow() — two on a phone. Each carries the same 8 dp margin the
    // full-width cards do, so the square side is half the width less two margins.
    const double margin = dimens::kSmallMargin * density;
    const double side = widthPx / 2.0 - margin * 2.0;

    double y = topPx;
    for (size_t i = 0; i < blocks.size(); ++i) {
        const bool leftColumn = i % 2 == 0;
        const double left = (leftColumn ? 0.0 : widthPx / 2.0) + margin;
        DrawDetailBlock(canvas, blocks[i], left, y + margin, side, colors, utcOffsetSeconds,
                        is24Hour);
        if (!leftColumn || i + 1 == blocks.size()) {
            y += side + margin * 2.0;
        }
    }
    return y - topPx;
}

}  // namespace breezy
