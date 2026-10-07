/*
 * Screen 3e — External modules. Port of ModulesSettingsScreen.kt (:104-380), the NDK twin of
 * ArkUIApp's pages/ModulesSettings.ets. Keep the two row lists identical.
 *
 *   General             Live wallpaper | Icon to display in weekly views | Monochrome icons
 *   Widgets in use      CONDITIONAL — one row per widget actually placed (kt:189-242)
 *   Notification widget Notification widget | Persistent | Style | Temperature as status bar icon
 *                       | Use feels like temperature
 *   Data sharing        one row per broadcast source (kt:363-378)
 *
 * Every module this screen configures is on CLAUDE.md's skip list. The SCREEN is still ported and
 * its values still stored, for the reason the source catalogue is ported without its clients: the
 * settings list is what Android renders. Only what would reach outside the app is placeheld — the
 * live wallpaper chooser and the broadcast package pickers. "Widgets in use" is absent because the
 * section is conditional on placed widgets and this port places none, which is also what the
 * reference shows on a device with none.
 *
 * Modelled on AppearanceScreen, which already has every ingredient: section headers, grouped card
 * rows, switch rows, disabled rows at half alpha, and a radio dialog.
 */

#ifndef BREEZY_VIEW_MODULESSCREEN_H
#define BREEZY_VIEW_MODULESSCREEN_H

#include <native_drawing/drawing_canvas.h>

#include <string>
#include <vector>

namespace breezy {

enum class ModulesAction { None, Close, Navigate, Changed };

struct ModulesHit {
    ModulesAction action = ModulesAction::None;
    std::string destination;
    std::string key;
    std::string value;
};

class ModulesScreen {
public:
    void SetGeometry(double widthPx, double heightPx, float density, double topInsetPx,
                     double bottomInsetPx);
    void SetDarkMode(bool dark) { dark_ = dark; }
    /** The seven stored values, pushed back by the shell after it writes them. */
    void SetValues(const std::string& weekIconMode, bool monochromeIcons, bool notificationWidget,
                   bool notificationPersistent, const std::string& notificationStyle,
                   bool notificationTempIcon, bool notificationFeelsLike);

    void Draw(OH_Drawing_Canvas* canvas);
    ModulesHit HitTest(double x, double y);
    bool ScrollBy(double dy);
    void ResetScroll() { scroll_ = 0.0; }
    bool DialogOpen() const { return dialog_ != Dialog::None; }
    void CloseDialog() { dialog_ = Dialog::None; }

private:
    enum class Dialog { None, WeekIconMode, NotificationStyle };

    struct Item {
        bool header = false;
        std::string title;
        std::string summary;
        bool isFirst = false;
        bool isLast = false;
        /** A largeSeparator follows, rather than a small one. */
        bool endsGroup = false;
        bool enabled = true;
        bool showSwitch = false;
        bool checked = false;
        /** Which dialog this row opens, or None. */
        Dialog opens = Dialog::None;
        /** The preference key a switch writes, empty when it is not a switch. */
        std::string key;
        /** Where a clickable row navigates, empty when it does not. */
        std::string destination;
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
    std::string KeyFor(Dialog dialog) const;
    void DrawDialog(OH_Drawing_Canvas* canvas);
    ModulesHit HitDialog(double x, double y);
    double ListTop() const;
    double ContentHeight() const;

    double width_ = 0.0;
    double height_ = 0.0;
    float density_ = 1.0f;
    double topInset_ = 0.0;
    double bottomInset_ = 0.0;
    bool dark_ = false;
    double scroll_ = 0.0;

    std::vector<Item> items_;

    std::string weekIconMode_ = "auto";
    bool monochromeIcons_ = false;
    bool notificationWidget_ = false;
    bool notificationPersistent_ = true;
    std::string notificationStyle_ = "daily";
    bool notificationTempIcon_ = false;
    bool notificationFeelsLike_ = false;

    Dialog dialog_ = Dialog::None;
    double dialogLeft_ = 0.0;
    double dialogRight_ = 0.0;
    double dialogOptionTop_ = 0.0;
    std::vector<double> dialogOptionHeights_;
};

}  // namespace breezy

#endif  // BREEZY_VIEW_MODULESSCREEN_H
