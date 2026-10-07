/*
 * The alert screen — AlertActivity.kt + AlertScreen.kt:74-228, the NDK twin of ArkUIApp's
 * pages/AlertList.ets.
 *
 * A list of EVERY alert on the location, not just the ones in force — the main card's two variants
 * filter, this screen does not. One fully-rounded card per alert, 16 vp apart, inside 16 vp of
 * horizontal padding.
 */
#ifndef BREEZY_VIEW_ALERTLISTSCREEN_H
#define BREEZY_VIEW_ALERTLISTSCREEN_H

#include <native_drawing/drawing_canvas.h>

#include <string>
#include <vector>

#include "input/Scroller.h"
#include "model/Weather.h"

namespace breezy {

enum class AlertListAction { None, Close };

struct AlertListHit {
    AlertListAction action = AlertListAction::None;
};

class AlertListScreen {
public:
    void SetGeometry(double widthPx, double heightPx, float density, double topInsetPx,
                     double bottomInsetPx);
    void SetDarkMode(bool dark) { dark_ = dark; }
    /** The location's alerts, its UTC offset (alert times print in ITS zone) and the clock format. */
    void SetAlerts(std::vector<Alert> alerts, int utcOffsetSeconds, bool is24Hour);
    /**
     * Which alert to open on — AlertScreen.kt:88-102. Empty, or an id that is not in the list,
     * opens at the top.
     */
    void SetInitialAlert(const std::string& alertId);

    void Draw(OH_Drawing_Canvas* canvas);
    AlertListHit HitTest(double x, double y);
    bool ScrollBy(double deltaY);
    bool Advance(double seconds);
    void StopScrolling();
    void Reset();

private:
    double ContentTop() const;
    /** `Alert.getFormattedDates` (Alert.kt:27-56). */
    std::string FormattedDates(const Alert& alert) const;
    /**
     * Lays the cards out, drawing them only when `canvas` is non-null.
     *
     * Stops before card `stopAt` and returns that card's distance below the content top; with
     * `stopAt == alerts_.size()` it lays out everything and returns the total content height.
     * Measuring and painting share one function because the card heights depend on how the prose
     * wraps, and scrolling to a given alert needs exactly the measurement the drawing does.
     */
    double Layout(OH_Drawing_Canvas* canvas, size_t stopAt);

    double width_ = 0.0;
    double height_ = 0.0;
    float density_ = 1.0f;
    double topInset_ = 0.0;
    double bottomInset_ = 0.0;
    bool dark_ = false;

    std::vector<Alert> alerts_;
    int utcOffsetSeconds_ = 0;
    bool is24Hour_ = false;
    /** Set by SetInitialAlert, consumed by the first Draw once the card tops are known. */
    std::string pendingAlertId_;

    Scroller scroll_;
    double contentHeight_ = 0.0;
};

}  // namespace breezy

#endif  // BREEZY_VIEW_ALERTLISTSCREEN_H
