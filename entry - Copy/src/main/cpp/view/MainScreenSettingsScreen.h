/*
 * Screen 3c — Main screen. Port of MainScreenSettingsScreen.kt (:55-256).
 *
 *   Displayed data   Blocks | Daily trends | Hourly trends
 *   Options          Weather-based background | Threshold lines on charts
 *   Animations       Background animation | Gravity sensor | Blocks fade in |
 *                    Other element animations
 *
 * Real here: the four switches and the animation mode, all stored under SettingsManager's own keys
 * through the shell round trip. The three Displayed-data rows open MANAGE screens
 * (kt:90-120 IntentHelper.start*ManageActivity) that are their own screens in the migration order
 * and are not ported, so each leads to the placeholder — but their SUMMARIES are real, which is
 * what those rows are for.
 *
 * This is the first screen in this port with a SWITCH row. SwitchPreferenceView
 * (SwitchPreference.kt:86-230) is the same row with trailing content, so it is a flag on Item here
 * rather than a second layout path, and the WHOLE ROW toggles (kt:116-120).
 */

#ifndef BREEZY_VIEW_MAINSCREENSETTINGSSCREEN_H
#define BREEZY_VIEW_MAINSCREENSETTINGSSCREEN_H

#include <native_drawing/drawing_canvas.h>

#include <string>
#include <vector>

namespace breezy {

enum class MainSettingsAction {
    None,
    /** The back arrow. */
    Close,
    /** A row that is not ported; `destination` carries its name. */
    Navigate,
    /** A setting changed: `key` is the preferences key, `value` its new value as a string. */
    Changed
};

struct MainSettingsHit {
    MainSettingsAction action = MainSettingsAction::None;
    std::string destination;
    std::string key;
    std::string value;
};

class MainScreenSettingsScreen {
public:
    void SetGeometry(double widthPx, double heightPx, float density, double topInsetPx,
                     double bottomInsetPx);
    void SetDarkMode(bool dark) { dark_ = dark; }
    /** The six live values, so the rows can show what is stored. */
    void SetValues(bool background, bool trendLines, const std::string& animationMode,
                   bool gravity, bool fadeIn, bool elements);
    /** The three ordered lists, so the Displayed-data rows summarise what is actually stored. */
    void SetDisplayLists(const std::string& cards, const std::string& daily,
                         const std::string& hourly);

    void Draw(OH_Drawing_Canvas* canvas);
    MainSettingsHit HitTest(double x, double y);
    bool ScrollBy(double dy);
    void ResetScroll() { scroll_ = 0.0; }
    bool DialogOpen() const { return dialogOpen_; }
    void CloseDialog() { dialogOpen_ = false; }

private:
    struct Item {
        bool header = false;
        std::string title;
        std::string summary;
        bool isFirst = false;
        bool isLast = false;
        /** A largeSeparator follows, rather than a small one. */
        bool endsGroup = false;
        /** Draws a trailing Switch and toggles instead of navigating. */
        bool isSwitch = false;
        bool checked = false;
        /** Opens the background-animation dialog. */
        bool opensDialog = false;
        /** The preferences key this row writes, empty for a navigation row. */
        std::string key;
        double top = 0.0;
        double height = 0.0;
    };

    struct Option {
        std::string value;
        std::string name;
    };

    void Layout();
    std::vector<Option> AnimationOptions() const;
    std::string AnimationName() const;
    double ListTop() const;
    double MaxScroll() const;
    void DrawDialog(OH_Drawing_Canvas* canvas);
    MainSettingsHit HitDialog(double x, double y);

    std::vector<Item> items_;
    double laidOutWidth_ = 0.0;
    float laidOutDensity_ = 0.0f;
    /** Re-laid out when a value changes, because a summary's height can change with it. */
    std::string laidOutValues_;

    bool background_ = true;
    bool trendLines_ = true;
    std::string animationMode_ = "system";
    bool gravity_ = true;
    bool fadeIn_ = true;
    bool elements_ = true;
    std::string cardDisplay_;
    std::string dailyDisplay_;
    std::string hourlyDisplay_;

    double width_ = 0.0;
    double height_ = 0.0;
    float density_ = 1.0f;
    double topInset_ = 0.0;
    double bottomInset_ = 0.0;
    double scroll_ = 0.0;
    bool dark_ = false;

    bool dialogOpen_ = false;
    double dialogLeft_ = 0.0;
    double dialogRight_ = 0.0;
    double dialogOptionTop_ = 0.0;
    double dialogOptionHeight_ = 0.0;
    double dialogCancelTop_ = 0.0;
    double dialogCancelBottom_ = 0.0;
};

}  // namespace breezy

#endif  // BREEZY_VIEW_MAINSCREENSETTINGSSCREEN_H
