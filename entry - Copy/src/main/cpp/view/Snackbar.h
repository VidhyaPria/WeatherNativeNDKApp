/*
 * The app's snackbar — a port of org.breezyweather.common.snackbar, not Material's.
 *
 * Breezy Weather ships its own, so there is a concrete layout, duration and animation to port:
 *
 *   SnackbarHelper.kt:36                Snackbar.LENGTH_LONG, always with cardStyle = true
 *                                       (BreezyActivity.kt:82, BreezyFragment.kt:42)
 *   SnackbarManager.kt:203              LENGTH_LONG = 3000 ms, scheduled once the entrance has
 *                                       finished (Snackbar.kt:252-255 -> onShown -> scheduleTimeout)
 *   Snackbar.kt:510-511                 ANIMATION_DURATION 450 ms, ANIMATION_FADE_DURATION 200 ms
 *   container_snackbar_layout_inner_card.xml   the card layout reproduced in Draw()
 *
 * WHY IT IS DRAWN IN C++. The undo was a `promptAction.showDialog` in the ArkTS shell — modal and
 * indefinite, where Android's blocks nothing and expires by itself. Rebuilding it as an ArkTS
 * overlay would have been easier, and wrong for this project: it would add ArkUI nodes to the port
 * whose whole premise is that its UI is rendered natively, and the memory comparison would measure
 * that difference rather than the two runtimes.
 *
 * NOT EXTRACTED FROM THE VENDORED SOURCE, and flagged as such: the `design_snackbar_*` dimensions
 * the layout references live in the Material Components library, which is not part of this
 * repository. The library's published values are used below and marked.
 */

#ifndef BREEZY_VIEW_SNACKBAR_H
#define BREEZY_VIEW_SNACKBAR_H

#include <native_drawing/drawing_canvas.h>

#include <string>

namespace breezy {

class Snackbar {
public:
    /** SnackbarHelper.showSnackbar(content, action, ...). An empty action means no button. */
    void Show(const std::string& message, const std::string& action);

    /** Starts the exit animation early — what tapping the action does. */
    void Leave();

    /** Anything to draw at all: entering, shown, or leaving. */
    bool Visible() const { return phase_ != Phase::Hidden; }

    /**
     * Advances the entrance, the timeout and the exit. Returns true while more frames are wanted.
     *
     * The bar has no other clock: the NDK's frame loop stops when nothing is moving, so this is
     * driven from OnVSync and reported by WantsFrames — the same wiring the pane's spring-back
     * needed, and the same bug if it is left out.
     */
    bool Advance(double seconds);

    void SetGeometry(double widthPx, double heightPx, float density, double bottomInsetPx,
                     bool darkMode);

    void Draw(OH_Drawing_Canvas* canvas);

    /** True when (x, y) is on the action, which is the only part of the bar that takes a touch. */
    bool HitAction(double x, double y) const;

private:
    enum class Phase { Hidden, Entering, Shown, Leaving };

    /** Where the card sits vertically, 0 at rest and +height fully off the bottom. */
    double Offset() const;
    /** The children's alpha; they fade in over the tail of the entrance. */
    double ContentAlpha() const;
    double CardHeight() const;
    double CardTop() const;

    Phase phase_ = Phase::Hidden;
    double elapsedMs_ = 0.0;

    std::string message_;
    std::string action_;

    double width_ = 0.0;
    double height_ = 0.0;
    float density_ = 1.0f;
    double bottomInset_ = 0.0;
    bool darkMode_ = false;

    /** Filled by Draw so the hit test uses the box that was actually painted. */
    double actionLeft_ = 0.0;
    double actionRight_ = 0.0;
    double actionTop_ = 0.0;
    double actionBottom_ = 0.0;
};

}  // namespace breezy

#endif  // BREEZY_VIEW_SNACKBAR_H
