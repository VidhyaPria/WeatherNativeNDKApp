/*
 * The gravity-sensor parallax behind the weather background.
 *
 * Port of two Android pieces:
 *   MaterialPainterView.kt:104-187   the TYPE_GRAVITY listener and the OrientationEventListener
 *                                    that together produce rotation2D / rotation3D
 *   DelayRotateController.kt         the accel/decel chaser that smooths them before they reach
 *                                    the implementors
 *
 * SettingsManager.isGravitySensorEnabled does not gate a feature flag upstream — it gates the
 * SensorManager itself (ContextExtensions.kt:80-85 returns null when the setting is off), so
 * registration silently no-ops and the rotations stay at 0. SetEnabled(false) here does the same
 * by unsubscribing and zeroing, which lands where the listener's `else` branch (kt:154-157) does.
 *
 * Unlike the ArkTS port, this side subscribes from C++ (libohsensor) rather than through the
 * shell, so the shell stays one XComponent and one fetch.
 */

#ifndef BREEZY_RENDER_GRAVITY_SENSOR_H
#define BREEZY_RENDER_GRAVITY_SENSOR_H

#include <atomic>
#include <functional>
#include <mutex>

#include <sensors/oh_sensor.h>
#include <sensors/oh_sensor_type.h>

namespace breezy {

/**
 * DelayRotateController.kt — the rotation the implementors see does not jump to the sensor's
 * value, it accelerates toward it at a fixed rate and brakes so it arrives without overshoot.
 * That is what stops the scene twitching on every sensor sample.
 */
class DelayRotateController {
public:
    explicit DelayRotateController(double initRotation);

    double Rotation() const { return currentRotation_; }

    /** kt:34-74. */
    void UpdateRotation(double rotation, double intervalMs);

private:
    /** kt:79-91 — fold the angle into [-90, 90] by reflecting it back at the poles. */
    static double RotationInScope(double rotation);

    /** kt:94. */
    static constexpr double kDefaultAbsAcceleration = 90.0 / 200.0 / 800.0;

    double targetRotation_;
    double currentRotation_;
    double velocity_ = 0.0;
    double acceleration_ = 0.0;
};

/**
 * The raw sensor half: subscribes to SENSOR_TYPE_GRAVITY and turns each sample into the two
 * angles the scene leans by.
 *
 * The callback arrives on the sensor service's own thread, so the two angles are atomics — the
 * render thread reads them once per frame and never blocks the sampler.
 */
class GravitySensor {
public:
    GravitySensor() = default;
    ~GravitySensor();

    GravitySensor(const GravitySensor&) = delete;
    GravitySensor& operator=(const GravitySensor&) = delete;

    /** (+) anticlockwise / (-) clockwise. */
    double Rotation2D() const { return r2D_.load(std::memory_order_relaxed); }
    /** (+) look down / (-) look up. */
    double Rotation3D() const { return r3D_.load(std::memory_order_relaxed); }

    /**
     * `Context.isLandscape`, which the orientation mapping branches on. The background is
     * full-bleed, so the surface being wider than it is tall is the same predicate.
     */
    void SetLandscape(bool landscape) { landscape_.store(landscape, std::memory_order_relaxed); }

    /** SettingsManager.isGravitySensorEnabled. */
    void SetEnabled(bool enabled);
    bool IsEnabled() const { return enabled_; }

private:
    /** MaterialPainterView.DeviceOrientation (kt:97-102). */
    enum class DeviceOrientation { Top, Left, Bottom, Right };

    /** kt:299-307 — resetDrawer zeroes both angles before registering. */
    void Start();
    /** kt:90 — the painter unregisters as soon as it stops being drawable. */
    void Stop();

    /** kt:106-158. */
    void OnSample(double aX, double aY, double aZ);
    /** The OrientationEventListener equivalent — see the .cpp for why it is derived here. */
    void UpdateDeviceOrientation(double aX, double aY, double aZ);

    static void EventCallback(Sensor_Event* event);

    bool enabled_ = false;
    bool subscribed_ = false;
    std::atomic<bool> landscape_{false};
    std::atomic<double> r2D_{0.0};
    std::atomic<double> r3D_{0.0};
    DeviceOrientation deviceOrientation_ = DeviceOrientation::Top;

    Sensor_SubscriptionId* id_ = nullptr;
    Sensor_SubscriptionAttribute* attribute_ = nullptr;
    Sensor_Subscriber* subscriber_ = nullptr;
};

}  // namespace breezy

#endif  // BREEZY_RENDER_GRAVITY_SENSOR_H
