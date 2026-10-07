/*
 * The destination for every action whose real screen is not ported yet — the NDK twin of
 * ArkUIApp's pages/Placeholder.ets.
 *
 * Naming the target screen is what keeps the click-handler mapping verifiable (CLAUDE.md): tapping
 * the toolbar's list icon must land on "Location list", a trend column on its tab's detail screen,
 * and so on. This port used to announce that with a toast from the shell, which said the same thing
 * but was not the same surface as ArkUIApp's page — the two ports differed on every unported tap.
 *
 * Deliberately drawn in the LIGHT palette regardless of the app's dark mode, because ArkUIApp's page
 * does the same (`AppColors.BACKGROUND.light`), and the two have to match.
 */

#ifndef BREEZY_VIEW_PLACEHOLDERSCREEN_H
#define BREEZY_VIEW_PLACEHOLDERSCREEN_H

#include <native_drawing/drawing_canvas.h>

#include <string>

namespace breezy {

class PlaceholderScreen {
public:
    void SetGeometry(double widthPx, double heightPx, float density);
    /** `destination` is the screen that would have opened; `detail` is optional extra context. */
    void Open(const std::string& destination, const std::string& detail);

    void Draw(OH_Drawing_Canvas* canvas);
    /** True when the Back button was hit — the only target on the screen. */
    bool HitBack(double x, double y) const;

private:
    std::string destination_;
    std::string detail_;

    double width_ = 0.0;
    double height_ = 0.0;
    float density_ = 1.0f;

    /** Where the button landed, filled by Draw for the hit test. */
    double buttonLeft_ = 0.0;
    double buttonRight_ = 0.0;
    double buttonTop_ = 0.0;
    double buttonBottom_ = 0.0;
};

}  // namespace breezy

#endif  // BREEZY_VIEW_PLACEHOLDERSCREEN_H
