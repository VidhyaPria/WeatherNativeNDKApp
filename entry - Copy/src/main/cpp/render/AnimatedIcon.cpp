#include "render/AnimatedIcon.h"

#include <native_drawing/drawing_matrix.h>

#include <cmath>
#include <vector>

#include "render/AssetImages.h"

namespace breezy {
namespace {

/** One animator step. Mirrors IconStep in AnimatedWeatherIcon.ets field for field. */
struct IconStep {
    double duration;
    double translateX;
    double translateY;
    double scale;
    double rotate;
    double opacity;
    /** overshoot_interpolator on the Android side, linear otherwise. */
    bool overshoot;
};

constexpr IconStep MoveX(double duration, double x, bool overshoot) {
    return IconStep{duration, x, 0.0, 1.0, 0.0, 1.0, overshoot};
}
constexpr IconStep ScaleTo(double duration, double scale) {
    return IconStep{duration, 0.0, 0.0, scale, 0.0, 1.0, false};
}
constexpr IconStep FadeTo(double duration, double opacity) {
    return IconStep{duration, 0.0, 0.0, 1.0, 0.0, opacity, false};
}
constexpr IconStep RotateTo(double duration, double rotate) {
    return IconStep{duration, 0.0, 0.0, 1.0, rotate, 1.0, false};
}

/* --------------------------------------------------------------- animator sets */

const std::vector<IconStep> kPulse90 = {ScaleTo(900, 0.9), ScaleTo(900, 1.0)};

const std::vector<IconStep> kSpinAndPulse = {
    {900, 0, 0, 0.85, 180, 1, true},
    {900, 0, 0, 1.0, 360, 1, true}};

const std::vector<IconStep> kWobble = {
    RotateTo(480, -84), RotateTo(420, 72), RotateTo(360, -60), RotateTo(300, 48),
    RotateTo(240, -36), RotateTo(180, 24), RotateTo(120, -12), RotateTo(60, 0)};

const std::vector<IconStep> kDriftRight20 = {MoveX(900, 20, false), MoveX(900, 0, true)};
const std::vector<IconStep> kDriftRight10 = {MoveX(900, 10, false), MoveX(900, 0, true)};
const std::vector<IconStep> kDriftLeft20 = {MoveX(900, -20, false), MoveX(900, 0, true)};

const std::vector<IconStep> kFog1 = {
    {450, 20, 0, 1, 0, 0.7, false}, {900, 20, 0, 1, 0, 0.7, false}, {450, 0, 0, 1, 0, 1.0, false}};
const std::vector<IconStep> kFog2 = {
    {450, -10, 0, 1, 0, 0.7, false}, {900, -10, 0, 1, 0, 0.7, false},
    {450, 0, 0, 1, 0, 1.0, false}};
const std::vector<IconStep> kFog3 = {FadeTo(450, 0.3), FadeTo(900, 0.3), FadeTo(450, 1.0)};

const std::vector<IconStep> kHaze1 = {FadeTo(225, 1.0), FadeTo(675, 0.0), FadeTo(225, 0.0),
                                      FadeTo(675, 1.0)};
const std::vector<IconStep> kHaze2 = {FadeTo(225, 0.0), FadeTo(675, 1.0), FadeTo(675, 0.0),
                                      FadeTo(225, 1.0)};
const std::vector<IconStep> kHaze3 = {FadeTo(675, 0.0), FadeTo(225, 0.0), FadeTo(675, 1.0),
                                      FadeTo(225, 1.0)};

const std::vector<IconStep> kRainCloud = {ScaleTo(900, 1.05), ScaleTo(900, 0.95),
                                          ScaleTo(450, 1.0)};
const std::vector<IconStep> kRainDrop1 = {{1200, 0, -50, 0.0, 0, 1, false},
                                          {650, 0, 0, 1.0, 0, 1, false}};
const std::vector<IconStep> kRainDrop2 = {{1400, 0, -50, 0.0, 0, 1, false},
                                          {450, 0, 0, 1.0, 0, 1, false}};

const std::vector<IconStep> kThunderShake = {
    MoveX(225, 5, false),  MoveX(225, -5, false),  MoveX(110, 10, false), MoveX(115, -5, false),
    MoveX(110, 10, false), MoveX(115, -10, false), MoveX(110, 10, false), MoveX(115, -10, false),
    MoveX(110, 10, false), MoveX(115, 0, false)};
const std::vector<IconStep> kThunderBolt = {{450, 0, -50, 0.0, 0, 1, false},
                                            {900, 0, -50, 0.0, 0, 1, false},
                                            {450, 0, 0, 1.0, 0, 1, false}};

const std::vector<IconStep> kWind = {
    {100, 100, 0, 1, 0, 0.0, false},  {350, 100, 0, 1, 0, 0.0, false},
    {900, -100, 0, 1, 0, 0.0, false}, {350, 5, 0, 0.9, 0, 1.0, false},
    {100, 0, 0, 1.0, 0, 1.0, false}};

const std::vector<IconStep> kEmpty = {};

/** The layer set for a kind — layersFor() in AnimatedWeatherIcon.ets. */
const std::vector<const std::vector<IconStep>*> LayersFor(WeatherKind kind, bool daytime) {
    switch (kind) {
        case WeatherKind::Clear:
            return daytime ? std::vector<const std::vector<IconStep>*>{&kPulse90, &kSpinAndPulse}
                           : std::vector<const std::vector<IconStep>*>{&kWobble};
        case WeatherKind::Cloud:
            return daytime ? std::vector<const std::vector<IconStep>*>{&kDriftLeft20, &kPulse90,
                                                                       &kSpinAndPulse}
                           : std::vector<const std::vector<IconStep>*>{&kDriftRight20, &kWobble};
        case WeatherKind::Cloudy:
            return {&kDriftRight20, &kDriftRight10};
        case WeatherKind::Fog:
            return {&kFog1, &kFog2, &kFog3};
        case WeatherKind::Haze:
            return {&kHaze1, &kHaze2, &kHaze3};
        case WeatherKind::Rainy:
        case WeatherKind::Sleet:
        case WeatherKind::Snow:
        case WeatherKind::Hail:
            return {&kRainCloud, &kRainDrop1, &kRainDrop2};
        case WeatherKind::Thunder:
            return {&kThunderShake, &kThunderBolt};
        case WeatherKind::Thunderstorm:
            return {&kRainCloud, &kThunderBolt, &kRainDrop2};
        case WeatherKind::Wind:
            return {&kWind};
        default:
            return {};
    }
}

/**
 * OvershootInterpolator's default tension is 2.0:
 *   t -= 1; return t*t*((tension+1)*t + tension) + 1
 * ArkUI's Curve.ExtremeDeceleration is what the ArkTS side uses for the same steps; both overshoot
 * past the target and settle, which is the visible property being matched.
 */
double Overshoot(double t) {
    const double tension = 2.0;
    const double u = t - 1.0;
    return u * u * ((tension + 1.0) * u + tension) + 1.0;
}

double Mix(double from, double to, double t) { return from + (to - from) * t; }

}  // namespace

std::string AnimatedIconBaseName(WeatherKind kind, bool daytime) {
    switch (kind) {
        case WeatherKind::Clear: return daytime ? "weather_clear_day" : "weather_clear_night";
        case WeatherKind::Cloud:
            return daytime ? "weather_partly_cloudy_day" : "weather_partly_cloudy_night";
        case WeatherKind::Cloudy: return "weather_cloudy";
        case WeatherKind::Rainy: return "weather_rain";
        case WeatherKind::Snow: return "weather_snow";
        case WeatherKind::Sleet: return "weather_sleet";
        case WeatherKind::Hail: return "weather_hail";
        case WeatherKind::Fog: return "weather_fog";
        case WeatherKind::Haze: return "weather_haze";
        case WeatherKind::Thunder: return "weather_thunder";
        case WeatherKind::Thunderstorm: return "weather_thunderstorm";
        case WeatherKind::Wind: return "weather_wind";
        default: return "";
    }
}

int AnimatedIconLayerCount(WeatherKind kind, bool daytime) {
    return static_cast<int>(LayersFor(kind, daytime).size());
}

IconTransform AnimatedIconTransformAt(WeatherKind kind, bool daytime, int layer,
                                      double elapsedMs) {
    IconTransform result;
    const auto layers = LayersFor(kind, daytime);
    if (layer < 0 || layer >= static_cast<int>(layers.size())) {
        return result;
    }
    const std::vector<IconStep>& steps = *layers[static_cast<size_t>(layer)];
    if (steps.empty()) {
        return result;
    }

    double total = 0.0;
    for (const IconStep& step : steps) {
        total += step.duration;
    }
    if (total <= 0.0) {
        return result;
    }

    // The set restarts when it ends, so the layer's own total is the modulus.
    double t = std::fmod(elapsedMs, total);
    if (t < 0.0) {
        t += total;
    }

    size_t index = 0;
    while (index < steps.size() && t >= steps[index].duration) {
        t -= steps[index].duration;
        ++index;
    }
    if (index >= steps.size()) {
        index = steps.size() - 1;
        t = steps[index].duration;
    }

    // A step animates FROM the previous step's targets TO its own; the first animates from the
    // last, because the loop wraps rather than snapping back to rest.
    const IconStep& to = steps[index];
    const IconStep& from = index == 0 ? steps[steps.size() - 1] : steps[index - 1];
    const double raw = to.duration <= 0.0 ? 1.0 : t / to.duration;
    const double eased = to.overshoot ? Overshoot(raw) : raw;

    result.translateX = Mix(from.translateX, to.translateX, eased);
    result.translateY = Mix(from.translateY, to.translateY, eased);
    result.scale = Mix(from.scale, to.scale, eased);
    result.rotate = Mix(from.rotate, to.rotate, eased);
    result.opacity = Mix(from.opacity, to.opacity, eased);
    return result;
}

void DrawAnimatedIcon(OH_Drawing_Canvas* canvas, WeatherKind kind, bool daytime, double xPx,
                      double yPx, double sizePx, double elapsedMs) {
    const std::string base = AnimatedIconBaseName(kind, daytime);
    if (base.empty() || sizePx <= 0.0) {
        return;
    }
    const int layers = AnimatedIconLayerCount(kind, daytime);
    const double centerX = xPx + sizePx / 2.0;
    const double centerY = yPx + sizePx / 2.0;

    for (int i = 0; i < layers; ++i) {
        const IconTransform tf = AnimatedIconTransformAt(kind, daytime, i, elapsedMs);
        // A fully transparent or fully collapsed layer draws nothing — the rain drops and the
        // thunder bolt spend most of their loop in exactly that state.
        if (tf.opacity <= 0.001 || tf.scale <= 0.001) {
            continue;
        }
        OH_Drawing_CanvasSave(canvas);
        /*
         * The ArkTS side applies translate, then scale, then rotate as separate attributes, all
         * about the node's centre. The same order about the icon's centre here, with translate in
         * the icon's own vp units scaled to the drawn size — the step tables are written against
         * the 56 vp default the ArkTS component uses, so a 128 vp icon moves proportionally.
         */
        const double unit = sizePx / 56.0;
        OH_Drawing_CanvasTranslate(canvas, static_cast<float>(centerX + tf.translateX * unit),
                                   static_cast<float>(centerY + tf.translateY * unit));
        OH_Drawing_CanvasScale(canvas, static_cast<float>(tf.scale), static_cast<float>(tf.scale));
        OH_Drawing_CanvasRotate(canvas, static_cast<float>(tf.rotate), 0.0f, 0.0f);
        // The EXTENSION matters: AssetImages opens the rawfile by its exact name, so every call
        // site passes the full filename. The layered weather artwork is PNG.
        DrawAssetWithAlpha(canvas, base + "_" + std::to_string(i + 1) + ".png", -sizePx / 2.0,
                           -sizePx / 2.0, sizePx, tf.opacity);
        OH_Drawing_CanvasRestore(canvas);
    }
}

}  // namespace breezy
