/*
 * Text on the native canvas.
 *
 * ArkUI hands its Text components to the text engine for you; here the engine has to be driven
 * directly — a typography style, a text style, a handler that accepts the string, then Layout and
 * Paint. This wraps that sequence so the drawing code reads like the ArkTS it is ported from.
 *
 * TWO faces, because the reference uses two: Roboto (registered from a buffer the shell reads out
 * of rawfile, the same file ArkUIApp bundles) for the main weather screen, and the system face for
 * every settings screen. See SystemFontScope below for why, and for how it is selected.
 */
#ifndef BREEZY_RENDER_TEXTPAINTER_H
#define BREEZY_RENDER_TEXTPAINTER_H

#include <native_drawing/drawing_canvas.h>
#include <native_drawing/drawing_text_typography.h>

#include <cstdint>
#include <string>

namespace breezy {

enum class TextAlignment { Left, Center, Right };

struct TextStyleSpec {
    double sizeVp = 14.0;
    uint32_t color = 0xFF000000;
    bool bold = false;
    /**
     * Weight 500, which is a THIRD weight and not "slightly bold": Material's titleMedium and
     * labelLarge are 500 where this struct's `bold` is 700. A settings row title drawn at 700 inked
     * 715 px against the reference's 699; at 500 it lands on it. `bold` wins if both are set.
     */
    bool medium = false;
    TextAlignment alignment = TextAlignment::Left;
    /** Single-line text ellipsises rather than wrapping, like .maxLines(1) does in ArkUI. */
    bool singleLine = false;
    /** textAppearanceTitleMedium carries 0.15 of letter spacing; most text carries none. */
    double letterSpacingVp = 0.0;
    /**
     * Material's line box, in vp — 24 for a 16 sp body, 44 for a 36 sp displaySmall. 0 leaves the
     * font's own leading, which is about 1.17x the size where Material's is 1.4 to 1.5.
     *
     * Set it through `DrawTextFixed` rather than by hand; the tokens live in render/Theme.h.
     */
    double lineHeightVp = 0.0;
};

/**
 * The reference renders in TWO faces, and which one depends on the screen:
 *
 *   * the main weather screen is classic Views, themed with `android:fontFamily=@font/robotoflex`
 *     (styles.xml:316, values-v26 and values-v28 alike) — the app's own bundled Roboto;
 *   * every settings screen is Compose, and its typography is `fontFamily = FontFamily.Default`
 *     (Type.kt:28, :36, :43) — the SYSTEM sans-serif.
 *
 * On the reference device `sans-serif` is HarmonyOS_Sans.ttf, not Roboto: /system/etc/fonts.xml
 * names it in the first <family>, under the comment "first font is default". So the settings text
 * there is HarmonyOS Sans, which is what this device's default is too.
 *
 * Both ports used to render everything in Roboto, on the belief that Roboto was "the platform
 * default there". Measured on 2026-08-11 against the reference's own uiautomator bounds, that made
 * every settings label about 8 % narrow — "Notifications of app updates" 650 px against 708, and
 * "Disabled" 168 against 185 — which is enough to change where a title wraps.
 *
 * Construct one of these at the top of a settings screen's Draw. It restores the previous face on
 * destruction, so an early return cannot leak the setting into the next screen.
 */
class SystemFontScope {
public:
    SystemFontScope();
    ~SystemFontScope();
    SystemFontScope(const SystemFontScope&) = delete;
    SystemFontScope& operator=(const SystemFontScope&) = delete;

private:
    bool previous_;
};

/** Whether the current draw is in the system face. */
bool UsingSystemFont();

/**
 * Registers the app typeface. Call once, before anything is drawn; a second call is ignored.
 * `data` is the raw .ttf. Returns false if the engine rejected it, in which case text still
 * renders, in the system font.
 */
bool RegisterAppFont(const uint8_t* data, size_t length);

/** Density scale, so vp in the ported layout become px on this display. */
void SetDisplayDensity(float density);
float DisplayDensity();

/**
 * Draws `text` into a box `widthPx` wide whose top-left is (xPx, yPx), and returns the height it
 * occupied. Returns 0 for empty text, so callers can lay out by accumulating the return value the
 * way a Column stacks its children.
 */
double DrawText(OH_Drawing_Canvas* canvas, const std::string& text, double xPx, double yPx,
                double widthPx, const TextStyleSpec& style);

/** The height `text` would occupy, without drawing it. */
double MeasureTextHeight(const std::string& text, double widthPx, const TextStyleSpec& style);

/** How many lines `text` wraps to in a box `widthPx` wide. */
int MeasureLineCount(const std::string& text, double widthPx, const TextStyleSpec& style);

/**
 * `TextFixedHeight` (DetailsCommon.kt:346-393) — draws into a box of `lineHeightPx` PER LINE with
 * the glyphs centred in it, and returns that box height rather than the painter's own.
 *
 * Material's line box is 1.4-1.5x the font size and this engine's is about 1.17x, so any layout
 * that stacks text and expects Android's height has to be driven by the type scale, not by what
 * the painter measures. Measured against the reference on 2026-08-11: a 36 sp displaySmall value
 * is 143 px there and 137 here, a 22 sp titleLarge 91 against 84, a 16 sp body line 78 against 61.
 * The tokens live in render/Theme.h.
 */
double DrawTextFixed(OH_Drawing_Canvas* canvas, const std::string& text, double xPx, double yPx,
                     double widthPx, const TextStyleSpec& style, double lineHeightPx);

/**
 * `SectionHeader` (Section.kt:31-46) — a Box padded `normal_margin` all round with a bodyMedium
 * SemiBold `primary` Text inside, so its height is 16 + 20n + 16 dp.
 *
 * ONE copy. Five settings screens each measured this inline with the painter's own line box, which
 * is 53 px where bodyMedium's 20 sp line is 65 — a header 157 px tall against the reference's 169,
 * injecting 12 px of drift at every section boundary.
 */
double SectionHeaderHeight(const std::string& title, double widthPx, const TextStyleSpec& style,
                           float density);

/**
 * One settings row's HEIGHT — `PreferenceView` (Preference.kt:136-227) inside a
 * Material3ExpressiveCardListItem.
 *
 * ONE copy, for the same reason `SectionHeaderHeight` is. Nine of this port's twelve row-drawing
 * screens laid their rows out inline as `title + 8 + summary + 8 dp a side`, measuring both with
 * the painter's own line box. That is the defect the ArkTS `PreferenceRow` was fixed for and it
 * survived here because only SettingsScreen and BackgroundUpdatesScreen were converted: the Units
 * screen's rows measured 193 px on device against the reference's 286.
 *
 * The height comes from the Material LINE TOKENS (titleMedium 24, bodyMedium 20) and
 * `kPreferenceRowVPadding` a side; only the line COUNT comes from the painter, since it is what
 * decides where this font wraps. A row with no summary falls back to the one-line ListItem floor.
 *
 * BOTH strings are measured. The title wraps too — "Notifications of severe weather alerts" takes
 * two lines at this width — and a first version of this helper that assumed one drew the summary
 * straight through the title's second line.
 */
double PreferenceRowHeight(const std::string& title, const std::string& summary, double textWidthPx,
                           const TextStyleSpec& titleStyle, const TextStyleSpec& summaryStyle,
                           float density, bool switchRow = false);

/**
 * `switchRow` selects the OTHER row, which is a different composable rather than a variant.
 *
 * The card form of `SwitchPreferenceView` (SwitchPreference.kt:103-152) is not a ListItem: it is a
 * plain Row with `.padding(normal_margin)` — 16 dp a side (kt:122) — around a weighted Column and a
 * Switch separated by a `small_margin` Spacer (kt:143). So it takes none of the ListItem reasoning
 * above: no 8 dp inner padding, no ListItemDefaults, no one-line floor.
 *
 * Measured on the reference's Notifications screen: its switch cards run 273 px where a plain row
 * is 286, and 273 less 169 of content is 104 — 16 dp a side exactly.
 */

/**
 * The same row's TEXT, drawn at the offsets that height was built from, so the two cannot drift
 * apart. `rowTopPx` is the card's top edge. A summary-less row centres its title in the floor.
 */
void DrawPreferenceRowText(OH_Drawing_Canvas* canvas, const std::string& title,
                           const std::string& summary, double textLeftPx, double rowTopPx,
                           double rowHeightPx, double textWidthPx, const TextStyleSpec& titleStyle,
                           const TextStyleSpec& summaryStyle, float density,
                           bool switchRow = false);

/** The width the text actually occupies — for placing something beside it. */
double MeasureTextWidth(const std::string& text, const TextStyleSpec& style);

}  // namespace breezy

#endif  // BREEZY_RENDER_TEXTPAINTER_H
