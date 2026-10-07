/*
 * The staggered card entrance.
 *
 * AbstractMainViewHolder.checkEnterScreen (kt:61-79) runs an entrance the first time a card is
 * laid out within the host's height, and never again. MainModuleUtils.kt supplies the numbers:
 * each card starts 120 vp low at 1.025 scale and fully transparent, and every entrance already in
 * flight pushes the next one 200 ms later and 50 ms shorter — that cascade is the whole effect.
 *
 * ArkUI runs this through animateTo; here it is a value per card advanced by the same vsync loop
 * that drives the background, because there is nothing else to run it.
 */
#ifndef BREEZY_RENDER_ENTERANIMATION_H
#define BREEZY_RENDER_ENTERANIMATION_H

#include <native_drawing/drawing_canvas.h>

#include <string>
#include <unordered_map>

namespace breezy {

class EnterAnimation {
public:
    /**
     * screen 3c's `list_animation_switch` (MainScreenSettingsScreen.kt:210-227).
     *
     * AbstractMainViewHolder.onBindView (kt:53-55) only hides the card when the setting is on, and
     * checkEnterScreen (kt:71-75) calls onEnterScreen() directly instead of running the animator.
     * So off does not mean "play it instantly" — it means the card was never hidden, which is what
     * a Progress() of 1 and an Apply() that touches nothing reproduce.
     */
    void SetEnabled(bool enabled) { enabled_ = enabled; }

    /**
     * Claims a slot for `id` the first time it is asked for, and returns its progress in 0..1.
     * A card that has already finished returns 1 forever after — an entrance never replays.
     */
    double Progress(const std::string& id);

    /** Advances every entrance in flight. Returns true while any is still running. */
    bool Advance(double seconds);

    /** True when at least one card has an entrance still to play. */
    bool Running() const;

    /** Applies the card's translate/scale/alpha for this frame; returns the alpha to draw with. */
    double Apply(OH_Drawing_Canvas* canvas, const std::string& id, double centreX, double top,
                 float density);

    void Reset();

private:
    struct Entry {
        double delay = 0.0;
        double duration = 0.0;
        double elapsed = 0.0;
    };

    std::unordered_map<std::string, Entry> entries_;
    /** MainModuleUtils.kt:90-93's pendingAnimatorList, as a count. */
    int pending_ = 0;
    bool enabled_ = true;
};

}  // namespace breezy

#endif  // BREEZY_RENDER_ENTERANIMATION_H
