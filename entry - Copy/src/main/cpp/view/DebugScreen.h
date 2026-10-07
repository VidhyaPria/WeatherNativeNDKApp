/*
 * Screen 3g — Debug. Port of DebugSettingsScreen.kt (:56-190).
 *
 * ONE card with ONE row on a release build: "Share crash logs".
 *
 * The Kotlin's other three sections — force weather update, the two unit-formatting switches and
 * the RefreshErrorType list — are ALL inside `if (BreezyWeather.instance.debugMode)` (kt:122-189),
 * and `debugMode` is `applicationInfo.flags and FLAG_DEBUGGABLE` (BreezyWeather.kt:72-74), false on
 * a release APK. Confirmed against the reference device, not inferred: ref_screen3g_debug.png.
 *
 * The permission banner (kt:81-107) is absent for the reason screen 3d's is: HarmonyOS has no
 * equivalent runtime notification grant to be missing.
 *
 * The row's ACTION is not ported — CrashLogUtils.dumpLogs shells out to logcat and hands the file
 * to a FileProvider share sheet, neither of which an app can reach here for its own faultlogs. Per
 * CLAUDE.md it leads to the placeholder carrying its name.
 */

#ifndef BREEZY_VIEW_DEBUGSCREEN_H
#define BREEZY_VIEW_DEBUGSCREEN_H

#include <native_drawing/drawing_canvas.h>

#include <string>

namespace breezy {

enum class DebugAction { None, Close, Navigate };

struct DebugHit {
    DebugAction action = DebugAction::None;
    std::string destination;
};

class DebugScreen {
public:
    void SetGeometry(double widthPx, double heightPx, float density, double topInsetPx,
                     double bottomInsetPx);
    void SetDarkMode(bool dark) { dark_ = dark; }

    void Draw(OH_Drawing_Canvas* canvas);
    DebugHit HitTest(double x, double y);

private:
    double ListTop() const;

    double width_ = 0.0;
    double height_ = 0.0;
    float density_ = 1.0f;
    double topInset_ = 0.0;
    double bottomInset_ = 0.0;
    bool dark_ = false;
    /** Where the single row landed, so a tap can be matched without laying it out again. */
    double rowTop_ = 0.0;
    double rowBottom_ = 0.0;
};

}  // namespace breezy

#endif  // BREEZY_VIEW_DEBUGSCREEN_H
