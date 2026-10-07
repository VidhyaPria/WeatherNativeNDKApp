/*
 * A Material AlertDialog holding a scrolling list, drawn in C++.
 *
 * Both source dialogs are this shape — SearchActivity's location-search chooser (kt:255-351) and
 * SecondarySourcesPreference's feature rows and per-feature pickers (LocationPreferences.kt:351-916)
 * — so the chrome is written once: scrim, rounded panel, headline title, scrolling body, trailing
 * text buttons.
 *
 * The ArkUI port builds the same thing out of a Stack + Column + List (view/location/
 * WeatherSourcesDialog.ets). It is deliberately NOT a system dialog on either side: the scrim and
 * dismiss-on-tap-outside are what a dialog contributes behaviourally, and both are here.
 */
#ifndef BREEZY_VIEW_MODAL_LIST_H
#define BREEZY_VIEW_MODAL_LIST_H

#include <native_drawing/drawing_canvas.h>

#include <string>
#include <vector>

#include "render/Theme.h"

namespace breezy {

/** One line of a dialog body. */
struct ModalEntry {
    /** The id this row stands for; empty for a heading, and for the "None" pseudo-entry. */
    std::string id;
    std::string label;
    /**
     * A second line under the label, which turns the row into a PreferenceView (Preference.kt:154-226)
     * rather than a radio row: that is how the eight feature rows are drawn.
     */
    std::string sub;
    /** A centred group heading (ListPreference.kt:335-342). Not selectable. */
    bool header = false;
    /** `.alpha(if (enabled) 1f else 0.5f)` — a listed but unselectable source. */
    bool enabled = true;
    bool selected = false;
    /**
     * Draw a trailing Material Switch instead of a leading radio, and let `sub` coexist with it.
     *
     * This is the non-card `SwitchPreferenceView` (SwitchPreference.kt:155-230) — a bare ListItem
     * with the switch as trailing content — which is what the Open-Meteo weather-models dialog
     * lists (OpenMeteoService.kt:846-853). `selected` carries its on/off state, so a toggle row
     * and a radio row differ only in how that one flag is DRAWN.
     */
    bool toggle = false;
    /**
     * A `HorizontalDivider()` above this row.
     *
     * The models dialog fences its two combining entries off with one: a rule above every
     * `_seamless` model (kt:843-845) and one below "Best match" (kt:880-882), which as a list of
     * "rule above row N" is a rule above the row AFTER Best match.
     */
    bool dividerAbove = false;
    /**
     * Let `sub` WRAP, and grow the row to fit it.
     *
     * The eight feature rows of the sources dialog do not need this — their summary is a source
     * name — so they keep the fixed 72 dp token and a single clipped line. The edit dialog's rows
     * carry real sentences: "Customize weather data sources used for this location" is two lines
     * on the reference and in ArkUIApp, and was being cut off at "used for" here. Opt-in rather
     * than always-on, because a measured height cannot be cached the way a token can.
     */
    bool wrapSub = false;
};

class ModalList {
public:
    void SetTitle(std::string title) { title_ = std::move(title); }
    /** Replaces the body. Does NOT reset the scroll; ResetScroll is explicit, as on screen 2. */
    void SetEntries(std::vector<ModalEntry> entries) { entries_ = std::move(entries); }
    /** Trailing text buttons, left to right. `disabledIndex` is drawn at half opacity and inert. */
    void SetButtons(std::vector<std::string> labels, int disabledIndex = -1) {
        buttons_ = std::move(labels);
        disabledButton_ = disabledIndex;
    }
    /** A line above the list, in the alert colour — the forecast row's duplicate warning. */
    void SetWarning(std::string warning) { warning_ = std::move(warning); }
    /** A filled note above the list — "More weather sources may become available…" (kt:495-513). */
    void SetNote(std::string note) { note_ = std::move(note); }

    void SetGeometry(double widthPx, double heightPx, double density, bool dark,
                     double widthFraction) {
        width_ = widthPx;
        height_ = heightPx;
        density_ = density;
        dark_ = dark;
        widthFraction_ = widthFraction;
    }

    void ResetScroll() { scroll_ = 0.0; }
    void ScrollBy(double dy);

    void Draw(OH_Drawing_Canvas* canvas) const;

    enum class HitKind { None, Scrim, Entry, Button };
    struct Hit {
        HitKind kind = HitKind::None;
        int index = -1;
    };
    Hit HitTest(double x, double y) const;

    const std::vector<ModalEntry>& Entries() const { return entries_; }

private:
    /** `MaterialTheme.typography.headlineSmall` — the dialog titles (kt:442-448). */
    static constexpr double kHeadlineSmall = 24.0;
    /** `titleSmall`, the centred group headings inside the picker (ListPreference.kt:274). */
    static constexpr double kTitleSmall = 14.0;
    /** ListPreference.kt's RadioButton row — a 40 dp target around the 24 dp control. */
    static constexpr double kRadioRowHeight = 56.0;
    /**
     * Material3's ListItem is a FIXED 72 dp for a two-line item, whatever its text measures, and
     * PreferenceView (Preference.kt:148-166) wraps it in `PaddingValues(vertical = 8.dp)`.
     *
     * This row used to be derived from the text metrics instead and came out at 59 dp against the
     * reference build's 88 — visibly tighter, and the whole sources dialog with it.
     */
    static constexpr double kListItemTwoLine = 72.0;
    /** ListItemOneLineContainerHeight — a switch row with no supporting line. */
    static constexpr double kListItemOneLine = 56.0;
    /** `HorizontalDivider()`'s thickness. */
    static constexpr double kDividerHeight = 1.0;
    /** AlertDialogNoPadding's own inset: large_margin + 4. */
    static constexpr double kPanelPadding = 24.0;
    /** The panel never grows past this share of the window; the list inside takes the squeeze. */
    static constexpr double kMaxPanelHeightFraction = 0.85;
    static constexpr double kScrim = 0x52 / 255.0;

    double EntryHeight(size_t index) const;
    double BodyHeight() const;
    double PanelWidth() const { return width_ * widthFraction_; }
    double PanelLeft() const { return (width_ - PanelWidth()) / 2.0; }
    double TitleHeight() const;
    double ButtonsHeight() const;
    double NoteHeight() const;
    double WarningHeight() const;
    double PanelHeight() const;
    double PanelTop() const { return (height_ - PanelHeight()) / 2.0; }
    double BodyTop() const;
    double BodyViewport() const;

    std::string title_;
    std::string warning_;
    std::string note_;
    std::vector<ModalEntry> entries_;
    std::vector<std::string> buttons_;
    int disabledButton_ = -1;
    double width_ = 0.0;
    double height_ = 0.0;
    double density_ = 1.0;
    double widthFraction_ = 0.9;
    double scroll_ = 0.0;
    bool dark_ = false;
};

}  // namespace breezy

#endif  // BREEZY_VIEW_MODAL_LIST_H
