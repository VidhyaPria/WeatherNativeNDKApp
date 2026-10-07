#include "render/EnterAnimation.h"

#include <algorithm>
#include <cmath>

namespace breezy {
namespace {

/** MainModuleUtils.kt:34 — dpToPx(120f) */
constexpr double kTranslationY = 120.0;
/** MainModuleUtils.kt:35-36 */
constexpr double kScaleFrom = 1.025;
/** MainModuleUtils.kt:29 */
constexpr double kBaseDuration = 500.0;
/** MainModuleUtils.kt:46 */
constexpr double kDelayPerPending = 200.0;
/** MainModuleUtils.kt:45 */
constexpr double kDurationReductionPerPending = 50.0;
/** MainModuleUtils.kt:33 — OvershootInterpolator's default tension. */
constexpr double kOvershootTension = 2.0;

/** android.view.animation.OvershootInterpolator — overshoots the target, then settles back. */
double Overshoot(double t) {
    const double x = t - 1.0;
    return x * x * ((kOvershootTension + 1.0) * x + kOvershootTension) + 1.0;
}

/** DecelerateInterpolator, which the fade and the scale use. */
double Decelerate(double t) {
    return 1.0 - (1.0 - t) * (1.0 - t);
}

}  // namespace

double EnterAnimation::Progress(const std::string& id) {
    // Settings off: the card was never hidden, so it is already fully entered. No slot is claimed,
    // which also keeps `pending_` at zero so nothing inherits a stagger delay.
    if (!enabled_) {
        return 1.0;
    }
    auto it = entries_.find(id);
    if (it == entries_.end()) {
        Entry entry;
        // `pending_` is the number of entrances already claimed — that is what produces the
        // cascade rather than every visible card animating at once.
        entry.delay = pending_ * kDelayPerPending;
        entry.duration = std::max(kBaseDuration - pending_ * kDurationReductionPerPending,
                                  kBaseDuration / 2.0);
        ++pending_;
        it = entries_.emplace(id, entry).first;
    }
    const Entry& entry = it->second;
    if (entry.elapsed <= entry.delay) {
        return 0.0;
    }
    return std::clamp((entry.elapsed - entry.delay) / entry.duration, 0.0, 1.0);
}

bool EnterAnimation::Advance(double seconds) {
    if (!enabled_) {
        return false;
    }
    bool running = false;
    for (auto& pair : entries_) {
        Entry& entry = pair.second;
        if (entry.elapsed < entry.delay + entry.duration) {
            entry.elapsed += seconds * 1000.0;
            running = true;
        }
    }
    return running;
}

bool EnterAnimation::Running() const {
    if (!enabled_) {
        return false;
    }
    for (const auto& pair : entries_) {
        const Entry& entry = pair.second;
        if (entry.elapsed < entry.delay + entry.duration) {
            return true;
        }
    }
    return false;
}

double EnterAnimation::Apply(OH_Drawing_Canvas* canvas, const std::string& id, double centreX,
                             double top, float density) {
    const double t = Progress(id);
    if (t >= 1.0) {
        return 1.0;
    }
    // The translate runs on the overshoot curve while the fade and scale decelerate — two separate
    // animateTo calls on the ArkTS side, and the reason the card settles with a small bounce.
    const double offsetY = kTranslationY * density * (1.0 - Overshoot(t));
    const double eased = Decelerate(t);
    const double scale = kScaleFrom + (1.0 - kScaleFrom) * eased;

    OH_Drawing_CanvasTranslate(canvas, 0.0f, static_cast<float>(offsetY));
    // Scaling about the card's own top-centre, which is where the ArkUI transform origin sits.
    OH_Drawing_CanvasTranslate(canvas, static_cast<float>(centreX), static_cast<float>(top));
    OH_Drawing_CanvasScale(canvas, static_cast<float>(scale), static_cast<float>(scale));
    OH_Drawing_CanvasTranslate(canvas, static_cast<float>(-centreX), static_cast<float>(-top));
    return eased;
}

void EnterAnimation::Reset() {
    entries_.clear();
    pending_ = 0;
}

}  // namespace breezy
