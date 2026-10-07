/*
 * Screen 3c — Background updates. Port of BackgroundUpdatesSettingsScreen.kt (:80-299).
 *
 * TWO sections:
 *   General       Refresh rate             a list preference over UpdateInterval's nine values
 *                 Skip when battery is low a switch, DISABLED while the rate is "Never" (kt:224)
 *   Troubleshoot  Disable battery optimization
 *                 Manufacturer specific instructions
 *                 Worker info
 *
 * What the rows DO differs from Android, and each difference is forced rather than chosen:
 *
 *   - Android follows every write with `WeatherUpdateJob.setupTask(context)`, which reschedules a
 *     WorkManager job. CLAUDE.md skips background refresh, so this port stores the value and
 *     nothing schedules anything. The refresh rate is still LIVE: `Weather.isValid` measures the
 *     stored forecast against it, so it decides whether selecting a location refetches at all.
 *   - "Disable battery optimization" fires ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS (kt:246);
 *     HarmonyOS has no equivalent intent an app can fire for itself.
 *   - "Manufacturer specific instructions" opens https://dontkillmyapp.com/ (kt:273).
 *   - "Worker info" starts WorkerInfoActivity (kt:291), which is not ported.
 *
 * Per CLAUDE.md a handler leading somewhere unported goes to the placeholder carrying its name.
 *
 * The "Never" confirmation (kt:139-215) is ported: three warning paragraphs and a Continue button
 * dead for ten seconds, the remaining count shown in its label. ArkUIApp's
 * BackgroundUpdatesSettings.ets is the same screen; the strings live in both.
 */

#ifndef BREEZY_VIEW_BACKGROUNDUPDATESSCREEN_H
#define BREEZY_VIEW_BACKGROUNDUPDATESSCREEN_H

#include <native_drawing/drawing_canvas.h>

#include <cstdint>
#include <string>
#include <vector>

namespace breezy {

enum class BackgroundAction {
    None,
    /** The back arrow. */
    Close,
    /** A row that is not ported, or the app-bar ⓘ; `destination` carries its name. */
    Navigate,
    /** A setting changed: `key` is the preferences key, `value` its new value as a string. */
    Changed
};

struct BackgroundHit {
    BackgroundAction action = BackgroundAction::None;
    std::string destination;
    std::string key;
    std::string value;
};

/** The nine `automatic_refresh_rate_values`, and the names the screen actually shows. */
const std::vector<std::string>& UpdateIntervalIds();
const std::vector<std::string>& UpdateIntervalNames();
/** The name for a stored id, falling back to the first entry as `indexOfFirst == -1` does. */
std::string UpdateIntervalName(const std::string& id);

class BackgroundUpdatesScreen {
public:
    void SetGeometry(double widthPx, double heightPx, float density, double topInsetPx,
                     double bottomInsetPx);
    void SetDarkMode(bool dark) { dark_ = dark; }
    /** The two values this screen edits, pushed in by the shell's settings bag. */
    void SetValues(const std::string& refreshRate, bool skipWhenBatteryLow);

    void Draw(OH_Drawing_Canvas* canvas);
    BackgroundHit HitTest(double x, double y);
    bool ScrollBy(double dy);
    void ResetScroll() { scroll_ = 0.0; }
    bool DialogOpen() const { return listOpen_ || neverOpen_; }
    void CloseDialog();
    /** Drives the Never dialog's countdown. True when the label needs a repaint. */
    bool Tick(int64_t nowMs);

private:
    struct Item {
        bool header = false;
        std::string title;
        std::string summary;
        bool isFirst = false;
        bool isLast = false;
        bool endsGroup = false;
        bool isSwitch = false;
        bool checked = false;
        bool rowEnabled = true;
        /** Empty for a row that just navigates; otherwise the preferences key it writes. */
        std::string key;
        /** For a navigating row, the placeholder name it carries. */
        std::string destination;
        double top = 0.0;
        double height = 0.0;
    };

    void Layout();
    double ListTop() const;
    double MaxScroll() const;
    void DrawListDialog(OH_Drawing_Canvas* canvas);
    void DrawNeverDialog(OH_Drawing_Canvas* canvas);

    double width_ = 0.0;
    double height_ = 0.0;
    float density_ = 1.0f;
    double topInset_ = 0.0;
    double bottomInset_ = 0.0;
    bool dark_ = false;
    double scroll_ = 0.0;

    std::string refreshRate_ = "1:30";
    bool skipWhenBatteryLow_ = true;

    std::vector<Item> items_;
    bool listOpen_ = false;
    bool neverOpen_ = false;
    /** Where the list dialog's options landed, so a tap can be matched without re-laying out. */
    double optionTop_ = 0.0;
    double optionHeight_ = 0.0;
    double dialogLeft_ = 0.0;
    double dialogRight_ = 0.0;
    /** kt:140-146 — ten, counted down once a second while the Never sheet is up. */
    int countdown_ = 0;
    int64_t neverOpenedAtMs_ = 0;
    double cancelLeft_ = 0.0;
    double continueLeft_ = 0.0;
    double buttonsTop_ = 0.0;
    double buttonsBottom_ = 0.0;
    double buttonWidth_ = 0.0;
};

}  // namespace breezy

#endif  // BREEZY_VIEW_BACKGROUNDUPDATESSCREEN_H
