/*
 * Screen 3e — Location. Port of LocationSettingsScreen.kt (:75-327).
 *
 *   General                   Location service
 *   Native API                Location access | Location access in background | Device's GPS usage
 *   Baidu IP location (China) API key
 *
 * Verified against the reference device before either port was written:
 * ref_screen3e_location_top.png / ref_screen3e_location_full.png — that is the whole screen.
 *
 * Real here: the source choice (`location_service`, default `native`), the two grantable permission
 * rows, and the API key. C++ cannot read the access token, so the shell pushes the grant state and
 * performs the request — the same split every other capability in this port uses.
 *
 * The BACKGROUND row renders but does not request: ohos.permission.LOCATION_IN_BACKGROUND exists,
 * but background refresh is scoped out by CLAUDE.md, so asking for it would request a permission
 * this port can never use. It leads to the placeholder. Its DISABLED-until-coarse-granted state
 * (kt:227) is reproduced.
 *
 * The freenet disclaimer (kt:109-160) is absent: the reference build is not that flavour.
 */

#ifndef BREEZY_VIEW_LOCATIONSCREEN_H
#define BREEZY_VIEW_LOCATIONSCREEN_H

#include <native_drawing/drawing_canvas.h>

#include <functional>
#include <string>
#include <vector>

namespace breezy {

enum class LocationSettingsAction {
    None,
    Close,
    /** A row that is not ported, or the About button; `destination` carries its name. */
    Navigate,
    /** A setting changed: `key` is the preferences key, `value` its new value. */
    Changed,
    /** Ask the shell to request a runtime permission; `key` is the permission name. */
    RequestPermission
};

struct LocationSettingsHit {
    LocationSettingsAction action = LocationSettingsAction::None;
    std::string destination;
    std::string key;
    std::string value;
};

class LocationScreen {
public:
    void SetGeometry(double widthPx, double heightPx, float density, double topInsetPx,
                     double bottomInsetPx);
    void SetDarkMode(bool dark) { dark_ = dark; }
    /** The stored values, from the shell that owns the preferences file. */
    void SetValues(const std::string& locationService, const std::string& baiduApiKey);
    /** The live grant state, which only ArkTS can read. */
    void SetPermissions(bool coarseGranted, bool preciseGranted);

    void Draw(OH_Drawing_Canvas* canvas);
    LocationSettingsHit HitTest(double x, double y);
    bool ScrollBy(double dy);
    void ResetScroll() { scroll_ = 0.0; }
    bool DialogOpen() const { return picker_ || editing_; }
    void CloseDialog();
    /** True while the API-key field is open, so the shell's TEXT session should be attached. */
    bool TextEntryOpen() const { return editing_; }
    /** What the shell has collected for the API-key field. Returns true if a repaint is needed. */
    bool SetEnteredText(const std::string& text);

    /** kt:210-213 — the snackbar for a row whose permission is already granted. */
    // std::function, not a bare pointer: the renderer binds its own snackbar, so this captures.
    using SnackbarHandler = std::function<void(const std::string& message)>;
    void SetSnackbarHandler(SnackbarHandler handler) { snackbar_ = handler; }
    /** Asks the shell to open or close a TEXT edit session. */
    using TextEntryHandler = void (*)(int state);
    void SetTextEntryHandler(TextEntryHandler handler) { textEntry_ = handler; }
    /** Pre-fills the shell's edit buffer — see WeatherSourcesScreen::SetSeedHandler. */
    using SeedHandler = std::function<void(const std::string& text)>;
    void SetSeedHandler(SeedHandler handler) { seed_ = handler; }

private:
    struct Item {
        bool header = false;
        std::string title;
        std::string summary;
        bool isFirst = false;
        bool isLast = false;
        bool endsGroup = false;
        bool rowEnabled = true;
        /** What a tap does: a picker, the key field, a permission request, or the placeholder. */
        enum class Tap { None, Picker, EditKey, RequestCoarse, RequestPrecise, Placeholder } tap =
            Tap::None;
        double top = 0.0;
        double height = 0.0;
    };

    struct Option {
        std::string value;
        std::string name;
    };

    void Layout();
    double ListTop() const;
    double MaxScroll() const;
    std::vector<Option> LocationOptions() const;
    std::string LocationServiceName() const;
    std::string ApiKeySummary() const;
    void DrawPicker(OH_Drawing_Canvas* canvas);
    void DrawEditor(OH_Drawing_Canvas* canvas);
    LocationSettingsHit HitPicker(double x, double y);
    LocationSettingsHit HitEditor(double x, double y);

    std::vector<Item> items_;
    double laidOutWidth_ = 0.0;
    float laidOutDensity_ = 0.0f;
    std::string laidOutValues_;

    std::string locationService_ = "native";
    std::string baiduApiKey_;
    bool coarseGranted_ = false;
    bool preciseGranted_ = false;

    double width_ = 0.0;
    double height_ = 0.0;
    float density_ = 1.0f;
    double topInset_ = 0.0;
    double bottomInset_ = 0.0;
    double scroll_ = 0.0;
    bool dark_ = false;

    bool picker_ = false;
    bool editing_ = false;
    /** The draft the field is showing; committed only on Confirm. */
    std::string draft_;
    SnackbarHandler snackbar_ = nullptr;
    TextEntryHandler textEntry_ = nullptr;
    SeedHandler seed_ = nullptr;

    double dialogLeft_ = 0.0;
    double dialogRight_ = 0.0;
    double dialogOptionTop_ = 0.0;
    double dialogOptionHeight_ = 0.0;
    double dialogButtonTop_ = 0.0;
    double dialogButtonBottom_ = 0.0;
    double dialogConfirmLeft_ = 0.0;
};

}  // namespace breezy

#endif  // BREEZY_VIEW_LOCATIONSCREEN_H
