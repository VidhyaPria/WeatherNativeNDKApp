/*
 * The About screen — AboutActivity / AboutScreen.kt:86-372, the NDK twin of ArkUIApp's
 * pages/About.ets.
 *
 * Measured off the reference device (ref_about_top.png, ref_about_contributors.png):
 *
 *   app bar        the LargeTopAppBar every screen-3 page uses — a 64 vp icon row with the back
 *                  arrow, then the headlineMedium title below it
 *   header         the launcher icon at 72 vp, "Breezy Weather" in headlineSmall, and the version
 *                  line in labelMedium onSurfaceVariant, the block padded 24 vp (kt:375-401)
 *   update row     a lone card, "Check for app updates" with ic_sync
 *   Contact        a labelMedium section title, then a grouped card of Source code / Matrix room
 *   About app      the same, with Privacy policy / Dependencies
 *   Contributors   one row per contributor, name in titleMedium with an optional bodyMedium
 *                  contribution line under it (kt:474-509)
 *   Translators    the same rows, filtered to the device language
 *
 * Long enough to need the shared `Scroller`, unlike the other screen-3 pages.
 */
#ifndef BREEZY_VIEW_ABOUTSCREEN_H
#define BREEZY_VIEW_ABOUTSCREEN_H

#include <native_drawing/drawing_canvas.h>

#include <string>
#include <vector>

#include "input/Scroller.h"

namespace breezy {

enum class AboutAction { None, Close, Navigate, OpenLink };

struct AboutHit {
    AboutAction action = AboutAction::None;
    /** The placeholder screen name for Navigate, the URL for OpenLink. */
    std::string destination;
};

class AboutScreen {
public:
    void SetGeometry(double widthPx, double heightPx, float density, double topInsetPx,
                     double bottomInsetPx);
    void SetDarkMode(bool dark) { dark_ = dark; }
    /**
     * `versionFormatted` (kt:413-417). The shell reads it from the bundle, as BuildConfig.VERSION_NAME
     * is — so it shows THIS port's version, matching ArkUIApp's.
     */
    void SetVersion(const std::string& version) { version_ = version; }

    void Draw(OH_Drawing_Canvas* canvas);
    AboutHit HitTest(double x, double y);
    /** Returns true when the position actually moved, so the caller knows to repaint. */
    bool ScrollBy(double deltaY);
    void Fling(double velocity);
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
    std::string version_;

    Scroller scroll_;
    double contentHeight_ = 0.0;
    /**
     * `AlertDialogLink` (kt:365-370) — the confirmation naming the link, drawn by the SCREEN so it
     * is the same bubble ArkUIApp's page draws. Only OK reaches the shell, which owns the ability
     * that opens a URL. Empty when no dialog is up.
     */
    std::string pendingLink_;
    struct Rect {
        double left = 0.0;
        double top = 0.0;
        double right = 0.0;
        double bottom = 0.0;
    };
    Rect cancelBand_;
    Rect okBand_;

    /** Where a tappable row landed last frame, so a tap matches without a second layout pass. */
    struct Band {
        double top = 0.0;
        double bottom = 0.0;
        AboutAction action = AboutAction::None;
        std::string destination;
    };
    std::vector<Band> bands_;
};

}  // namespace breezy

#endif  // BREEZY_VIEW_ABOUTSCREEN_H
