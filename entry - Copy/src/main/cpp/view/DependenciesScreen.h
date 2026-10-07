/*
 * The Dependencies screen — DependenciesActivity.kt:36-69, the NDK twin of ArkUIApp's
 * pages/Dependencies.ets.
 *
 * AboutLibraries' `LibrariesContainer` row: the name with the version right-aligned on the same
 * line, the author underneath, then one filled pill per licence. Plain rows on the surface, no card.
 *
 * The rows are non-tappable in both ports. AboutLibraries' default click opens a dialog with the
 * full licence TEXT, which neither port ships — a row that raised an empty dialog would be worse
 * than one that does nothing.
 */
#ifndef BREEZY_VIEW_DEPENDENCIESSCREEN_H
#define BREEZY_VIEW_DEPENDENCIESSCREEN_H

#include <native_drawing/drawing_canvas.h>

#include "input/Scroller.h"

namespace breezy {

enum class DependenciesAction { None, Close };

struct DependenciesHit {
    DependenciesAction action = DependenciesAction::None;
};

class DependenciesScreen {
public:
    void SetGeometry(double widthPx, double heightPx, float density, double topInsetPx,
                     double bottomInsetPx);
    void SetDarkMode(bool dark) { dark_ = dark; }

    void Draw(OH_Drawing_Canvas* canvas);
    DependenciesHit HitTest(double x, double y);
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
};

}  // namespace breezy

#endif  // BREEZY_VIEW_DEPENDENCIESSCREEN_H
