/*
 * Worker info — WorkerInfoActivity.kt:69-131, the NDK twin of ArkUIApp's pages/WorkerInfo.ets.
 * Reached from Background updates ▸ Troubleshoot.
 *
 * Three sections, each a header over one card holding a MONOSPACE text block padded normal_margin
 * (kt:100-113, SectionText at kt:132-140): Enqueued, Finished, Running.
 *
 * The content of each is WorkManager's job list for those states, built by `constructString`
 * (kt:175-205). That function has an explicit empty case: a single "-" when the list is empty.
 *
 * WHY EVERY SECTION READS "-" HERE, AND WHY THAT IS NOT A STUB:
 * CLAUDE.md skips background refresh entirely, so neither port ever enqueues a job. There is no
 * WorkManager to query and nothing that could be in any of the three lists — so "-" is not a
 * placeholder standing in for content we failed to fetch, it is the value the Kotlin itself
 * produces for an empty list. The screen is complete; its data is legitimately empty, the same way
 * the alert card is empty because Open-Meteo declares no ALERTS feature.
 */

#ifndef BREEZY_VIEW_WORKERINFOSCREEN_H
#define BREEZY_VIEW_WORKERINFOSCREEN_H

#include <native_drawing/drawing_canvas.h>

#include <string>

namespace breezy {

enum class WorkerInfoAction { None, Close, Navigate };

struct WorkerInfoHit {
    WorkerInfoAction action = WorkerInfoAction::None;
    std::string destination;
};

class WorkerInfoScreen {
public:
    void SetGeometry(double widthPx, double heightPx, float density, double topInsetPx,
                     double bottomInsetPx);
    void SetDarkMode(bool dark) { dark_ = dark; }

    void Draw(OH_Drawing_Canvas* canvas);
    WorkerInfoHit HitTest(double x, double y);

private:
    double ListTop() const;

    double width_ = 0.0;
    double height_ = 0.0;
    float density_ = 1.0f;
    double topInset_ = 0.0;
    double bottomInset_ = 0.0;
    bool dark_ = false;
};

}  // namespace breezy

#endif  // BREEZY_VIEW_WORKERINFOSCREEN_H
