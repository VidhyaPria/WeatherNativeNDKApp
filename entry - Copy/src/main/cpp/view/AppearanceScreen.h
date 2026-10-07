/*
 * Screen 3a — Appearance. Port of AppearanceSettingsScreen.kt (:88-353).
 *
 *   Language and region   Language | Units | Alternate calendar
 *   Theme                 App dark mode | Location-based dark mode | Icon pack
 *
 * Real here: App dark mode and Location-based dark mode. They are stored by the shell and re-theme
 * every screen through Renderer::IsLightTheme. The rest are shown with the value they would have
 * and lead to the placeholder — Language because this port is English-only, Units because it is its
 * own screen, Icon pack because CLAUDE.md scopes external icon packs out entirely.
 *
 * The single-choice dialog lives here rather than in its own file: it is opened only by this
 * screen's two rows, and it is the same radio list for both (ListPreference.kt:241-313 — a
 * headlineSmall title, one radio per value where CHOOSING commits and closes, and a lone "Cancel").
 */

#ifndef BREEZY_VIEW_APPEARANCESCREEN_H
#define BREEZY_VIEW_APPEARANCESCREEN_H

#include <native_drawing/drawing_canvas.h>

#include <string>
#include <vector>

namespace breezy {

enum class AppearanceAction {
    None,
    /** The back arrow. */
    Close,
    /** A row that is not ported; `destination` carries its name. */
    Navigate,
    /** A setting was chosen: `key` is "dark_mode" or "day_night", `value` its new value. */
    Changed,
    /** The icon-packs dialog's magnifier — open the icon preview (kt:322-338). */
    Preview
};

struct AppearanceHit {
    AppearanceAction action = AppearanceAction::None;
    std::string destination;
    std::string key;
    std::string value;
};

class AppearanceScreen {
public:
    void SetGeometry(double widthPx, double heightPx, float density, double topInsetPx,
                     double bottomInsetPx);
    void SetDarkMode(bool dark) { dark_ = dark; }
    /**
     * The SYSTEM's night mode, which is not the same thing as `dark_` (the app's effective mode).
     * Needed only to name it in the "Follow system (…)" option — see OptionsFor.
     */
    void SetSystemDark(bool dark) { systemDark_ = dark; }
    /** The two live settings, so the rows can show what is stored. */
    void SetValues(const std::string& darkMode, bool dayNightForLocations);
    /** SettingsManager.alternateCalendar — "none", "" for the regional default, or a key. */
    void SetAlternateCalendar(const std::string& value) { alternateCalendar_ = value; }
    /** The device locale, which decides the "Regional preference (…)" row's name. */
    void SetLocale(const std::string& language, const std::string& country) {
        language_ = language;
        country_ = country;
    }

    void Draw(OH_Drawing_Canvas* canvas);
    AppearanceHit HitTest(double x, double y);
    bool ScrollBy(double dy);
    void ResetScroll() { scroll_ = 0.0; }
    /** True while the choice dialog is up — it takes every touch. */
    bool DialogOpen() const { return dialog_ != Dialog::None; }
    void CloseDialog() { dialog_ = Dialog::None; }

private:
    /**
     * kt:130-131 — `CalendarPreferenceView` is an inline list preference, NOT a screen. This row
     * used to carry `Dialog::None`, which on this screen means "does nothing at all"; ArkUIApp's
     * equivalent sent it to the placeholder. Both were wrong in the same place.
     */
    enum class Dialog { None, DarkMode, DarkModeLocations, Language, IconPack, AlternateCalendar };

    struct Item {
        bool header = false;
        std::string title;
        std::string summary;
        bool isFirst = false;
        bool isLast = false;
        /** A largeSeparator follows, rather than a small one. */
        bool endsGroup = false;
        bool enabled = true;
        /** Which dialog this row opens, or None for a placeholder row. */
        Dialog opens = Dialog::None;
        double top = 0.0;
        double height = 0.0;
    };

    struct Option {
        std::string value;
        std::string name;
    };

    void Layout();
    std::vector<Option> OptionsFor(Dialog dialog) const;
    std::string SelectedFor(Dialog dialog) const;
    double ListTop() const;
    double MaxScroll() const;
    void DrawDialog(OH_Drawing_Canvas* canvas);
    /** The "Icon packs" dialog is a provider list, not a radio list — its own draw and hit. */
    void DrawIconPackDialog(OH_Drawing_Canvas* canvas);
    AppearanceHit HitIconPackDialog(double x, double y);
    AppearanceHit HitDialog(double x, double y);

    std::vector<Item> items_;
    double laidOutWidth_ = 0.0;
    float laidOutDensity_ = 0.0f;
    /** Re-laid out when a value changes, because a summary's height can change with it. */
    std::string laidOutDarkMode_;
    bool laidOutDayNight_ = false;
    bool laidOutSystemDark_ = false;

    std::string darkMode_ = "system";
    std::string alternateCalendar_ = "none";
    /** Per-row heights of the open dialog, since a wrapped label makes its row taller. */
    std::vector<double> dialogOptionHeights_;
    std::string language_ = "en";
    std::string country_ = "";
    bool dayNightForLocations_ = false;

    double width_ = 0.0;
    double height_ = 0.0;
    float density_ = 1.0f;
    double topInset_ = 0.0;
    double bottomInset_ = 0.0;
    double scroll_ = 0.0;
    bool dark_ = false;
    bool systemDark_ = false;
    Dialog dialog_ = Dialog::None;
    /** The dialog's option rows, in surface coordinates, filled by DrawDialog for the hit test. */
    double dialogLeft_ = 0.0;
    double dialogRight_ = 0.0;
    double dialogOptionTop_ = 0.0;
    double dialogOptionHeight_ = 0.0;
    double dialogCancelTop_ = 0.0;
    double dialogCancelBottom_ = 0.0;
    /** The icon-packs dialog's provider row and its trailing magnifier, in surface coordinates. */
    double packRowTop_ = 0.0;
    double packRowBottom_ = 0.0;
    double packSearchLeft_ = 0.0;
};

}  // namespace breezy

#endif  // BREEZY_VIEW_APPEARANCESCREEN_H
