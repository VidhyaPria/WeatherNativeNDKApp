/*
 * The Privacy policy screen — PrivacyPolicyActivity.kt:60-146, the NDK twin of ArkUIApp's
 * pages/PrivacyPolicy.ets.
 *
 * One card group: the app's own row, then one row per HTTP source that declares a policy, sorted by
 * name. Each row is `title = name`, `summary = the URL`, and tapping raises the same confirmation
 * the About screen uses before handing the URL to the shell.
 *
 * The rows come from `PrivacyPolicyRows()`, which pairs the URL table with the shared source
 * catalogue's names.
 */
#ifndef BREEZY_VIEW_PRIVACYPOLICYSCREEN_H
#define BREEZY_VIEW_PRIVACYPOLICYSCREEN_H

#include <native_drawing/drawing_canvas.h>

#include <string>
#include <vector>

#include "input/Scroller.h"

namespace breezy {

enum class PrivacyAction { None, Close, OpenLink };

struct PrivacyHit {
    PrivacyAction action = PrivacyAction::None;
    std::string destination;
};

class PrivacyPolicyScreen {
public:
    void SetGeometry(double widthPx, double heightPx, float density, double topInsetPx,
                     double bottomInsetPx);
    void SetDarkMode(bool dark) { dark_ = dark; }

    void Draw(OH_Drawing_Canvas* canvas);
    PrivacyHit HitTest(double x, double y);
    bool ScrollBy(double deltaY);
    bool Advance(double seconds);
    void StopScrolling();
    void Reset();

private:
    double ContentTop() const;

    double width_ = 0.0;
    double height_ = 0.0;
    float density_ = 1.0f;
    double topInset_ = 0.0;
    double bottomInset_ = 0.0;
    bool dark_ = false;

    Scroller scroll_;
    double contentHeight_ = 0.0;

    struct Band {
        double top = 0.0;
        double bottom = 0.0;
        std::string url;
    };
    std::vector<Band> bands_;

    /** The open confirmation, drawn by the screen so both ports show the same bubble. */
    std::string pendingLink_;
    struct Rect {
        double left = 0.0;
        double top = 0.0;
        double right = 0.0;
        double bottom = 0.0;
    };
    Rect okBand_;
};

}  // namespace breezy

#endif  // BREEZY_VIEW_PRIVACYPOLICYSCREEN_H
