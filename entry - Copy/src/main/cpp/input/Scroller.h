/*
 * Scroll position and fling, for one axis.
 *
 * ArkUI hands you Scroll and List and the physics comes with them. Here there is nothing between
 * the touch events and the pixels, so the flywheel has to be written out: a drag moves the
 * position directly, a lift converts the recent pointer velocity into a fling, and the fling
 * decays exponentially until it is slow enough to stop.
 *
 * Deceleration matches Android's ViewConfiguration/OverScroller defaults so the two ports feel the
 * same under the thumb, which is the only way a side-by-side comparison means anything.
 */
#ifndef BREEZY_INPUT_SCROLLER_H
#define BREEZY_INPUT_SCROLLER_H

namespace breezy {

class Scroller {
public:
    /** Sets the travel limits; the position is re-clamped into the new range. */
    void SetRange(double maxPosition);

    double Position() const { return position_; }
    bool IsAnimating() const { return animating_; }

    /** Moves by a finger delta, stopping any fling in progress. */
    void Drag(double delta);

    /** `velocity` is in pixels per second, in the same sign convention as Drag. */
    void Fling(double velocity);

    void Stop();

    /**
     * Back to the top, with any fling cancelled.
     *
     * Screen 4 needs it: changing the day or the tag replaces the whole content, and a scroll
     * position carried over from the previous one would leave the new page opening part-way down.
     */
    void Reset();

    /** Advances the fling. Returns true while it still has somewhere to go. */
    bool Advance(double seconds);

private:
    void Clamp();

    double position_ = 0.0;
    double max_ = 0.0;
    double velocity_ = 0.0;
    bool animating_ = false;
};

}  // namespace breezy

#endif  // BREEZY_INPUT_SCROLLER_H
