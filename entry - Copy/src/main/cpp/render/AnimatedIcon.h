/*
 * The animated weather icon — AnimatableIconView + res/animator/weather_*.xml.
 *
 * The NDK twin of ArkUIApp's view/common/AnimatedWeatherIcon.ets, and the step tables here are a
 * transcription of that file's: same durations, same targets, same interpolators, same layer sets.
 * Keep the two in lockstep — a divergence in a step table is exactly the kind of asymmetry that
 * quietly stops the memory comparison being like-for-like.
 *
 * The two ports drive it very differently, and that is intended rather than accidental:
 *
 *   ArkUI  each layer owns @Trace transform state, and `animateTo` plus a chained setTimeout walks
 *          it from step to step. The framework tweens.
 *   NDK    there is no retained state at all. The renderer already redraws every frame, so a layer's
 *          transform is a PURE FUNCTION of elapsed time: find which step the clock is inside, then
 *          interpolate from the previous step's targets to that step's. Nothing to start, stop or
 *          leak, and the icon is correct on the very first frame it appears.
 */
#ifndef BREEZY_RENDER_ANIMATEDICON_H
#define BREEZY_RENDER_ANIMATEDICON_H

#include <native_drawing/drawing_canvas.h>

#include <string>

#include "model/Weather.h"

namespace breezy {

/** One layer's resolved transform at an instant. */
struct IconTransform {
    double translateX = 0.0;
    double translateY = 0.0;
    double scale = 1.0;
    double rotate = 0.0;
    double opacity = 1.0;
};

/** How many stacked drawables this kind's icon has (1..3). Zero when it has no artwork. */
int AnimatedIconLayerCount(WeatherKind kind, bool daytime);

/**
 * The transform for `layer` (0-based) at `elapsedMs` into the loop.
 *
 * AnimatableIconView restarts its set when it ends, so each layer's own total duration is the
 * modulus — the layers are NOT resynchronised with each other, which is what makes the fog bands
 * and rain drops drift apart the way they do on Android.
 */
IconTransform AnimatedIconTransformAt(WeatherKind kind, bool daytime, int layer, double elapsedMs);

/**
 * Draws the whole stacked icon into a `sizePx` square at (xPx, yPx).
 *
 * `elapsedMs` is wall time since the icon appeared; pass 0 for the at-rest pose, which is what
 * `isElementsAnimationEnabled = false` shows.
 */
void DrawAnimatedIcon(OH_Drawing_Canvas* canvas, WeatherKind kind, bool daytime, double xPx,
                      double yPx, double sizePx, double elapsedMs);

/** The drawable base name — "weather_clear_day", "weather_partly_cloudy_night", … */
std::string AnimatedIconBaseName(WeatherKind kind, bool daytime);

}  // namespace breezy

#endif  // BREEZY_RENDER_ANIMATEDICON_H
