#include "input/Scroller.h"

#include <algorithm>
#include <cmath>

namespace breezy {
namespace {

/**
 * Fling decay. Android's OverScroller uses a flywheel whose velocity falls off with the scroll
 * friction; an exponential with this coefficient tracks it closely over the range a thumb
 * actually produces, and unlike a fixed deceleration it never overshoots into a reversal.
 */
constexpr double kDecayPerSecond = 3.2;

/** Below this the fling is finished — less than a pixel per frame at 120 Hz. */
constexpr double kMinVelocity = 60.0;

/** A hard cap, so a flicked finger cannot throw the list the length of the forecast. */
constexpr double kMaxVelocity = 12000.0;

}  // namespace

void Scroller::SetRange(double maxPosition) {
    max_ = std::max(0.0, maxPosition);
    Clamp();
}

void Scroller::Drag(double delta) {
    velocity_ = 0.0;
    animating_ = false;
    position_ += delta;
    Clamp();
}

void Scroller::Fling(double velocity) {
    velocity_ = std::clamp(velocity, -kMaxVelocity, kMaxVelocity);
    animating_ = std::fabs(velocity_) >= kMinVelocity && max_ > 0.0;
}

void Scroller::Stop() {
    velocity_ = 0.0;
    animating_ = false;
}

void Scroller::Reset() {
    position_ = 0.0;
    Stop();
}

bool Scroller::Advance(double seconds) {
    if (!animating_) {
        return false;
    }
    position_ += velocity_ * seconds;
    velocity_ *= std::exp(-kDecayPerSecond * seconds);

    const double before = position_;
    Clamp();
    // Hitting either end ends the fling; there is no bounce on these lists.
    if (before != position_ || std::fabs(velocity_) < kMinVelocity) {
        Stop();
        return false;
    }
    return true;
}

void Scroller::Clamp() {
    position_ = std::clamp(position_, 0.0, max_);
}

}  // namespace breezy
