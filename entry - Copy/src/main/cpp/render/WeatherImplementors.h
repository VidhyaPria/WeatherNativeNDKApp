/*
 * The procedural weather background painters.
 *
 * Ported 1:1 from ui-weather-view/.../materialWeatherView/implementor/ via ArkUIApp's
 * view/background/WeatherImplementors.ets, keeping every particle parameter as CLAUDE.md requires:
 *   SunImplementor.kt:30-98            clear day     — three counter-rotating squares
 *   CloudImplementor.kt:34-590         cloud / cloudy / fog / haze / thunder
 *   RainImplementor.kt:35-308          rain / sleet / thunderstorm
 *   SnowImplementor.kt:32-168          snow
 *   HailImplementor.kt:34-175          hail
 *   WindImplementor.kt:31-179          wind
 *   MeteorShowerImplementor.kt:35-269  clear night
 *
 * The two rotations come from the device's gravity sensor (GravitySensor.h), smoothed by a
 * DelayRotateController before they get here, exactly as MaterialPainterView.kt:251-283 feeds
 * them. rotation2D leans the whole scene; rotation3D parallaxes it in depth. Both are 0 when the
 * `gravity_sensor_switch` setting is off, which is what makes the shapes stand still.
 *
 * `scrollRate` is 0 at the top of the page and 1 once the cards have covered the background; at 1
 * the Kotlin skips drawing entirely, which is what keeps the effect cheap while scrolling.
 */
#ifndef BREEZY_RENDER_WEATHERIMPLEMENTORS_H
#define BREEZY_RENDER_WEATHERIMPLEMENTORS_H

#include <native_drawing/drawing_canvas.h>

#include <memory>

#include "render/Theme.h"

namespace breezy {

class WeatherImplementor {
public:
    virtual ~WeatherImplementor() = default;
    /** Advance the simulation by `intervalMs` milliseconds. */
    virtual void UpdateData(double width, double height, double intervalMs, double rotation2D,
                            double rotation3D) = 0;
    virtual void Draw(OH_Drawing_Canvas* canvas, double width, double height, double scrollRate,
                      double rotation2D, double rotation3D) = 0;
};

/** WeatherImplementorFactory.getWeatherImplementor — null for a kind with no animation. */
std::unique_ptr<WeatherImplementor> CreateImplementor(WeatherKind kind, bool daylight,
                                                      bool darkMode, double width, double height);

}  // namespace breezy

#endif  // BREEZY_RENDER_WEATHERIMPLEMENTORS_H
