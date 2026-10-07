/*
 * The icon preview screen — PreviewIconActivity.kt:44-160, the NDK twin of ArkUIApp's
 * pages/PreviewIcon.ets.
 *
 * A 4-column grid of every icon the default resource provider can produce, in seven sections
 * (kt:62-109): full day, full night, minimal day, minimal night, shortcuts day, shortcuts night,
 * and the two Ephemeris drawables. Titles and rules span all four columns; icon cells are square
 * with the image inset by `large_margin`.
 *
 * SCOPE: external icon packs are out per CLAUDE.md, so the provider is always the default one —
 * the artwork both ports bundle, and what Android shows with no pack installed.
 */
#ifndef BREEZY_VIEW_PREVIEWICONSCREEN_H
#define BREEZY_VIEW_PREVIEWICONSCREEN_H

#include <native_drawing/drawing_canvas.h>

#include "input/Scroller.h"
#include "model/Weather.h"

namespace breezy {

enum class PreviewIconAction { None, Close, AppDetails };

struct PreviewIconHit {
    PreviewIconAction action = PreviewIconAction::None;
};

/**
 * Which of the three dialogs is open, if any (PreviewIconActivity.kt:180-211).
 *
 * The section a tapped cell belongs to is exactly what decides the dialog, so this mirrors the
 * IconSet the grid is already laid out by. Ephemeris is absent on purpose: SunIcon.onItemClicked
 * is "// do nothing." (kt:222).
 */
enum class IconDialogKind { None, Animatable, Minimal, Adaptive };

class PreviewIconScreen {
public:
    void SetGeometry(double widthPx, double heightPx, float density, double topInsetPx,
                     double bottomInsetPx);
    void SetDarkMode(bool dark) { dark_ = dark; }

    void Draw(OH_Drawing_Canvas* canvas);
    PreviewIconHit HitTest(double x, double y);
    bool ScrollBy(double deltaY);
    bool Advance(double seconds);
    void StopScrolling();
    void Reset();

    /** True while a dialog is up: the renderer keeps animating, and Back closes it first. */
    bool DialogOpen() const { return dialog_ != IconDialogKind::None; }
    void CloseDialog() { dialog_ = IconDialogKind::None; }

private:
    double ContentTop() const;
    /** Lays the sections out, drawing only when `canvas` is non-null. Returns the content height. */
    double Layout(OH_Drawing_Canvas* canvas);
    /**
     * Walks the grid to find the cell under (x, y) and opens that section's dialog.
     * Returns true when a cell was hit.
     */
    bool OpenDialogAt(double x, double y);
    void DrawDialog(OH_Drawing_Canvas* canvas);
    /** The open dialog's container height; 0 when none is open. */
    double DialogHeight() const;

    IconDialogKind dialog_ = IconDialogKind::None;
    WeatherKind dialogKind_ = WeatherKind::None;
    bool dialogDaytime_ = true;
    /** Rolled once when the adaptive dialog opens — kt:48 does the same and must not re-roll. */
    int dialogMask_ = 0;
    /** Seconds the open animatable dialog has been showing, for the icon's loop. */
    double dialogElapsed_ = 0.0;

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

#endif  // BREEZY_VIEW_PREVIEWICONSCREEN_H
