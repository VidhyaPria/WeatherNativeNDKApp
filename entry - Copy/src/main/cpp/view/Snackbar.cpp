#include "Snackbar.h"

#include <algorithm>
#include <cmath>

#include "../render/Shapes.h"
#include "../render/TextPainter.h"
// dimens:: and the colour pairs both live here; there is no separate Dimens.h.
#include "../render/Theme.h"

namespace breezy {
namespace {

/** SnackbarManager.kt:203 — LENGTH_LONG, and it starts only once the entrance has finished. */
constexpr double kDurationMs = 3000.0;
/** Snackbar.kt:510-511. */
constexpr double kAnimationMs = 450.0;
constexpr double kFadeMs = 200.0;
/** getFloatingOvershotEnterAnimators()'s default tension (DisplayExtensions.kt:255). */
constexpr double kOvershootTension = 1.5;
/** The card enters at 1.1 and settles to 1 (SnackbarAnimationUtils.kt:33-34). */
constexpr double kEnterScale = 1.1;
/** The inner LinearLayout's padding (container_snackbar_layout_inner_card.xml:15). */
constexpr double kInnerPadding = 4.0;
/** Material Components library values — see the note in the header. */
constexpr double kSnackbarPaddingVertical = 14.0;
constexpr double kSnackbarPaddingHorizontal = 12.0;

/**
 * OvershootInterpolator(tension), verbatim from the AOSP formula:
 *   t' = (t-1)^2 * ((tension+1)*(t-1) + tension) + 1
 * It passes 1 and comes back, which is the whole character of the entrance.
 */
double Overshoot(double fraction) {
    const double t = fraction - 1.0;
    return t * t * ((kOvershootTension + 1.0) * t + kOvershootTension) + 1.0;
}

/** DecelerateInterpolator(1f) — 1 - (1-t)^2. Drives the scale. */
double Decelerate(double fraction) {
    const double inv = 1.0 - fraction;
    return 1.0 - inv * inv;
}

/**
 * FastOutSlowInInterpolator, the exit curve (Snackbar.kt:269). Its cubic-bezier control points are
 * (0.4, 0) and (0.2, 1); solved for y by bisection on x, which is exact enough at 450 ms and avoids
 * carrying a spline solver for one curve.
 */
double FastOutSlowIn(double fraction) {
    const double clamped = std::max(0.0, std::min(1.0, fraction));
    double lo = 0.0;
    double hi = 1.0;
    for (int i = 0; i < 24; ++i) {
        const double mid = (lo + hi) / 2.0;
        const double inv = 1.0 - mid;
        const double x = 3.0 * inv * inv * mid * 0.4 + 3.0 * inv * mid * mid * 0.2 + mid * mid * mid;
        if (x < clamped) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    const double t = (lo + hi) / 2.0;
    const double inv = 1.0 - t;
    return 3.0 * inv * inv * t * 0.0 + 3.0 * inv * t * t * 1.0 + t * t * t;
}

}  // namespace

void Snackbar::Show(const std::string& message, const std::string& action) {
    message_ = message;
    action_ = action;
    phase_ = Phase::Entering;
    elapsedMs_ = 0.0;
}

void Snackbar::Leave() {
    if (phase_ == Phase::Hidden || phase_ == Phase::Leaving) {
        return;
    }
    phase_ = Phase::Leaving;
    elapsedMs_ = 0.0;
}

void Snackbar::SetGeometry(double widthPx, double heightPx, float density, double bottomInsetPx,
                           bool darkMode) {
    width_ = widthPx;
    height_ = heightPx;
    density_ = density;
    bottomInset_ = bottomInsetPx;
    darkMode_ = darkMode;
}

bool Snackbar::Advance(double seconds) {
    if (phase_ == Phase::Hidden) {
        return false;
    }
    elapsedMs_ += seconds * 1000.0;
    switch (phase_) {
        case Phase::Entering:
            if (elapsedMs_ >= kAnimationMs) {
                // onShown -> scheduleTimeout: the three seconds start HERE, not at Show().
                phase_ = Phase::Shown;
                elapsedMs_ = 0.0;
            }
            return true;
        case Phase::Shown:
            if (elapsedMs_ >= kDurationMs) {
                phase_ = Phase::Leaving;
                elapsedMs_ = 0.0;
            }
            // Frames are still wanted: the timeout has to be able to fire.
            return true;
        case Phase::Leaving:
            if (elapsedMs_ >= kAnimationMs) {
                phase_ = Phase::Hidden;
                elapsedMs_ = 0.0;
                message_.clear();
                action_.clear();
                return false;
            }
            return true;
        case Phase::Hidden:
        default:
            return false;
    }
}

double Snackbar::CardHeight() const {
    TextStyleSpec style;
    style.sizeVp = dimens::kTitleTextSize;
    const double textHeight = MeasureTextHeight("Ag", width_, style);
    return (kInnerPadding * 2.0 + kSnackbarPaddingVertical * 2.0) * density_ + textHeight;
}

double Snackbar::CardTop() const {
    // material_card's bottom margin is small_margin (styles.xml:98), clear of the navigation bar.
    const double bottomMargin = dimens::kSmallMargin * density_ + bottomInset_;
    return height_ - bottomMargin - CardHeight();
}

double Snackbar::Offset() const {
    const double travel = CardHeight() + dimens::kSmallMargin * density_ + bottomInset_;
    switch (phase_) {
        case Phase::Entering: {
            const double f = std::min(1.0, elapsedMs_ / kAnimationMs);
            // translationY runs from its own height to 0.
            return travel * (1.0 - Overshoot(f));
        }
        case Phase::Leaving: {
            const double f = std::min(1.0, elapsedMs_ / kAnimationMs);
            return travel * FastOutSlowIn(f);
        }
        case Phase::Shown:
        case Phase::Hidden:
        default:
            return 0.0;
    }
}

double Snackbar::ContentAlpha() const {
    if (phase_ != Phase::Entering) {
        return 1.0;
    }
    // animateChildrenIn(ANIMATION_DURATION - ANIMATION_FADE_DURATION, ANIMATION_FADE_DURATION)
    // (Snackbar.kt:246-249) — the text fades in over the tail of the slide, not with it.
    const double start = kAnimationMs - kFadeMs;
    if (elapsedMs_ <= start) {
        return 0.0;
    }
    return std::min(1.0, (elapsedMs_ - start) / kFadeMs);
}

void Snackbar::Draw(OH_Drawing_Canvas* canvas) {
    if (phase_ == Phase::Hidden || width_ <= 0.0 || height_ <= 0.0) {
        actionLeft_ = actionRight_ = actionTop_ = actionBottom_ = 0.0;
        return;
    }

    const double margin = dimens::kSmallMargin * density_;
    const double cardHeight = CardHeight();
    const double top = CardTop() + Offset();
    const double left = margin;
    const double right = width_ - margin;

    /*
     * The 1.1 -> 1 scale, about the card's own centre. Applied as a canvas transform rather than by
     * inflating the rectangle, so the text scales with the card as the View animation does.
     */
    double scale = 1.0;
    if (phase_ == Phase::Entering) {
        const double f = std::min(1.0, elapsedMs_ / kAnimationMs);
        scale = kEnterScale + (1.0 - kEnterScale) * Decelerate(f);
    }
    const double centreX = (left + right) / 2.0;
    const double centreY = top + cardHeight / 2.0;

    OH_Drawing_CanvasSave(canvas);
    OH_Drawing_CanvasTranslate(canvas, static_cast<float>(centreX), static_cast<float>(centreY));
    OH_Drawing_CanvasScale(canvas, static_cast<float>(scale), static_cast<float>(scale));
    OH_Drawing_CanvasTranslate(canvas, static_cast<float>(-centreX), static_cast<float>(-centreY));

    // cardBackgroundColor = ?attr/colorSurfaceInverse, corner material3_card_list_item_corner_radius.
    FillRoundRect(canvas, left, top, right, top + cardHeight,
                  dimens::kNormalMargin * density_,
                  darkMode_ ? kSurfaceInverseDark : kSurfaceInverseLight);

    const double alpha = ContentAlpha();
    const uint32_t messageColor = WithAlpha(kOnSurfaceInverse.Resolve(darkMode_), alpha);
    // android:textColor="?attr/colorTertiaryContainer" (layout :46) — the container role, which is
    // the pale gold on light and the deep brown on dark.
    const uint32_t actionColor = WithAlpha(kTertiaryContainer.Resolve(darkMode_), alpha);

    const double innerLeft = left + kInnerPadding * density_;
    const double innerRight = right - kInnerPadding * density_;
    const double textTop = top + (kInnerPadding + kSnackbarPaddingVertical) * density_;

    // The action is measured first: the message takes what is left, as layout_weight=1 does.
    double actionWidth = 0.0;
    TextStyleSpec actionStyle;
    actionStyle.sizeVp = dimens::kTitleTextSize;
    actionStyle.color = actionColor;
    actionStyle.bold = true;   // android:textStyle="bold" (layout :47)
    actionStyle.singleLine = true;
    if (!action_.empty()) {
        actionWidth = MeasureTextWidth(action_, actionStyle) +
                      kSnackbarPaddingHorizontal * 2.0 * density_;
    }

    TextStyleSpec messageStyle;
    messageStyle.sizeVp = dimens::kTitleTextSize;
    messageStyle.color = messageColor;
    messageStyle.singleLine = true;
    const double messageLeft = innerLeft + kSnackbarPaddingHorizontal * density_;
    const double messageWidth =
        std::max(0.0, innerRight - actionWidth - messageLeft - kSnackbarPaddingHorizontal * density_);
    DrawText(canvas, message_, messageLeft, textTop, messageWidth, messageStyle);

    if (!action_.empty()) {
        const double actionBoxLeft = innerRight - actionWidth;
        DrawText(canvas, action_, actionBoxLeft + kSnackbarPaddingHorizontal * density_, textTop,
                 actionWidth, actionStyle);
        // Recorded in SURFACE coordinates, before the scale is unwound — the hit test runs against
        // the resting position, which is where the finger will land once it has settled.
        actionLeft_ = actionBoxLeft;
        actionRight_ = innerRight;
        actionTop_ = top;
        actionBottom_ = top + cardHeight;
    } else {
        actionLeft_ = actionRight_ = actionTop_ = actionBottom_ = 0.0;
    }

    OH_Drawing_CanvasRestore(canvas);
}

bool Snackbar::HitAction(double x, double y) const {
    // Only while it is actually up: a bar on its way out has already had its action spent.
    if (phase_ != Phase::Entering && phase_ != Phase::Shown) {
        return false;
    }
    if (actionRight_ <= actionLeft_) {
        return false;
    }
    return x >= actionLeft_ && x <= actionRight_ && y >= actionTop_ && y <= actionBottom_;
}

}  // namespace breezy
