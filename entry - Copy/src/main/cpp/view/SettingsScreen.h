/*
 * Screen 3 — the root settings screen. Port of RootSettingsScreen.kt's RootSettingsView (:38-153).
 *
 * Eight rows in five grouped cards, in the Kotlin's order. A group is ONE rounded block: the outer
 * corners of its first and last row carry 28 dp and every corner between them 2 dp
 * (Material3Widgets.kt:204-225, dimens.xml:21), with a 2 dp gap between rows of a group and 16 dp
 * between groups (dimens.xml:22-23).
 *
 * The bar is a Material3 LargeTopAppBar (FitSystemBarComposeWrappers.kt:34): navigation and action
 * icons on their own row, then the title as a large headline BELOW them — not screen 2's
 * centre-aligned bar.
 *
 * THEME: the system colour mode, NOT the selected location's daylight. `SettingsActivity.onCreate`
 * calls `BreezyWeatherTheme { }` with no argument (:107-111) and so takes
 * `darkTheme = isSystemInDarkTheme()`, where MainActivity passes the location's
 * `!ThemeManager.isLightTheme(...)`. On device the location list is dark for a night location while
 * settings behind it is light (`ref_screen3_settings.png`).
 */

#ifndef BREEZY_VIEW_SETTINGSSCREEN_H
#define BREEZY_VIEW_SETTINGSSCREEN_H

#include <native_drawing/drawing_canvas.h>

#include <string>
#include <vector>

namespace breezy {

enum class SettingsAction {
    None,
    /** The back arrow — finishes the activity. */
    Close,
    /** A row, or the About action; `destination` carries the name to announce. */
    Navigate
};

struct SettingsHit {
    SettingsAction action = SettingsAction::None;
    std::string destination;
};

class SettingsScreen {
public:
    void SetGeometry(double widthPx, double heightPx, float density, double topInsetPx,
                     double bottomInsetPx);
    void SetDarkMode(bool dark) { dark_ = dark; }

    void Draw(OH_Drawing_Canvas* canvas);

    /** A tap. Returns what it landed on. */
    SettingsHit HitTest(double x, double y) const;

    /** Drags the list. Returns true when the position actually moved. */
    bool ScrollBy(double dy);
    void ResetScroll() { scroll_ = 0.0; }

private:
    /** One row's laid-out geometry, recomputed whenever the width or the density changes. */
    struct Row {
        const char* title;
        const char* summary;
        const char* icon;
        bool isFirst;
        bool isLast;
        /** A largeSeparator follows this row rather than a small one. */
        bool endsGroup;
        double top = 0.0;
        double height = 0.0;
    };

    void Layout();
    double ContentHeight() const;
    double MaxScroll() const;
    /** The y the list starts at: the whole expanded bar, below the status bar. */
    double ListTop() const;

    std::vector<Row> rows_;
    double laidOutWidth_ = 0.0;
    float laidOutDensity_ = 0.0f;

    double width_ = 0.0;
    double height_ = 0.0;
    float density_ = 1.0f;
    double topInset_ = 0.0;
    double bottomInset_ = 0.0;
    double scroll_ = 0.0;
    bool dark_ = false;
};

}  // namespace breezy

#endif  // BREEZY_VIEW_SETTINGSSCREEN_H
