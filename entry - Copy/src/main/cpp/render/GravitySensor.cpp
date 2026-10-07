#include "render/GravitySensor.h"

#include <algorithm>
#include <cmath>

#include <hilog/log.h>

namespace breezy {
namespace {

constexpr unsigned int kLogDomain = 0xD003D01;
constexpr char kLogTag[] = "BreezyNative";

constexpr double kDegrees = 180.0 / M_PI;

/**
 * Android registers at SENSOR_DELAY_FASTEST. The angles are only ever read once per drawn frame
 * (kt:252-253) and this port's scene runs at 30 fps, so sampling faster than the frame rate
 * changes nothing visible while costing callbacks the measurement has to carry. The native API
 * takes nanoseconds; 20 ms (50 Hz) stays comfortably above the loop.
 */
constexpr int64_t kSamplingIntervalNs = 20000000;

/**
 * Sensor_EventCallback is a plain C function pointer with no user-data slot, so the one live
 * subscriber is reached through this. Exactly one GravitySensor exists (the Renderer owns it);
 * the pointer is published before OH_Sensor_Subscribe and cleared after OH_Sensor_Unsubscribe.
 */
std::atomic<GravitySensor*> g_active{nullptr};

}  // namespace

/* --------------------------------------------------------- DelayRotateController */

DelayRotateController::DelayRotateController(double initRotation)
    : targetRotation_(RotationInScope(initRotation)), currentRotation_(targetRotation_) {}

void DelayRotateController::UpdateRotation(double rotation, double intervalMs) {
    targetRotation_ = RotationInScope(rotation);
    const double rotationDiff = targetRotation_ - currentRotation_;

    // no need to move
    if (rotationDiff == 0.0) {
        acceleration_ = 0.0;
        velocity_ = 0.0;
        return;
    }

    const double accelSign = targetRotation_ > currentRotation_ ? 1.0 : -1.0;
    const double oldVelocity = velocity_;

    if (velocity_ == 0.0 || rotationDiff * velocity_ < 0.0) {
        // start or turn around.
        acceleration_ = accelSign * kDefaultAbsAcceleration;
        velocity_ = acceleration_ * intervalMs;
    } else if (velocity_ * velocity_ / (2.0 * kDefaultAbsAcceleration) < std::fabs(rotationDiff)) {
        // speed up
        acceleration_ = accelSign * kDefaultAbsAcceleration;
        velocity_ += acceleration_ * intervalMs;
    } else {
        // slow down
        acceleration_ = -1.0 * accelSign * velocity_ * velocity_ / (2.0 * std::fabs(rotationDiff));
        velocity_ += acceleration_ * intervalMs;
    }

    const double distance = oldVelocity * intervalMs + acceleration_ * intervalMs * intervalMs / 2.0;

    if (std::fabs(distance) > std::fabs(rotationDiff)) {
        acceleration_ = 0.0;
        currentRotation_ = targetRotation_;
        velocity_ = 0.0;
    } else {
        currentRotation_ += distance;
    }
}

double DelayRotateController::RotationInScope(double rotationP) {
    const double rotation = std::fmod(rotationP, 180.0);
    if (std::fabs(rotation) <= 90.0) {
        return rotation;
    }
    return rotation > 0.0 ? 90.0 - (rotation - 90.0) : -90.0 - (rotation + 90.0);
}

/* ------------------------------------------------------------------ GravitySensor */

GravitySensor::~GravitySensor() {
    Stop();
}

void GravitySensor::SetEnabled(bool enabled) {
    if (enabled_ == enabled) {
        return;
    }
    enabled_ = enabled;
    if (enabled) {
        Start();
    } else {
        Stop();
    }
}

void GravitySensor::EventCallback(Sensor_Event* event) {
    GravitySensor* self = g_active.load(std::memory_order_acquire);
    if (self == nullptr || event == nullptr) {
        return;
    }
    float* data = nullptr;
    uint32_t length = 0;
    if (OH_SensorEvent_GetData(event, &data, &length) != SENSOR_SUCCESS || data == nullptr ||
        length < 3) {
        return;
    }
    self->OnSample(data[0], data[1], data[2]);
}

void GravitySensor::Start() {
    if (subscribed_ || !enabled_) {
        return;
    }
    r2D_.store(0.0, std::memory_order_relaxed);
    r3D_.store(0.0, std::memory_order_relaxed);

    id_ = OH_Sensor_CreateSubscriptionId();
    attribute_ = OH_Sensor_CreateSubscriptionAttribute();
    subscriber_ = OH_Sensor_CreateSubscriber();
    if (id_ == nullptr || attribute_ == nullptr || subscriber_ == nullptr) {
        OH_LOG_Print(LOG_APP, LOG_ERROR, kLogDomain, kLogTag,
                     "GravitySensor: could not allocate subscription");
        Stop();
        return;
    }

    OH_SensorSubscriptionId_SetType(id_, SENSOR_TYPE_GRAVITY);
    OH_SensorSubscriptionAttribute_SetSamplingInterval(attribute_, kSamplingIntervalNs);
    OH_SensorSubscriber_SetCallback(subscriber_, EventCallback);

    g_active.store(this, std::memory_order_release);
    const Sensor_Result result = OH_Sensor_Subscribe(id_, attribute_, subscriber_);
    if (result != SENSOR_SUCCESS) {
        // A device with no gravity sensor behaves like the setting being off, which is what
        // getDefaultSensor returning null does upstream.
        OH_LOG_Print(LOG_APP, LOG_ERROR, kLogDomain, kLogTag,
                     "GravitySensor: subscribe failed, result %{public}d",
                     static_cast<int>(result));
        g_active.store(nullptr, std::memory_order_release);
        Stop();
        return;
    }
    subscribed_ = true;
}

void GravitySensor::Stop() {
    if (subscribed_) {
        OH_Sensor_Unsubscribe(id_, subscriber_);
        subscribed_ = false;
    }
    g_active.store(nullptr, std::memory_order_release);
    if (subscriber_ != nullptr) {
        OH_Sensor_DestroySubscriber(subscriber_);
        subscriber_ = nullptr;
    }
    if (attribute_ != nullptr) {
        OH_Sensor_DestroySubscriptionAttribute(attribute_);
        attribute_ = nullptr;
    }
    if (id_ != nullptr) {
        OH_Sensor_DestroySubscriptionId(id_);
        id_ = nullptr;
    }
    r2D_.store(0.0, std::memory_order_relaxed);
    r3D_.store(0.0, std::memory_order_relaxed);
}

/**
 * kt:106-158.
 *   x : (+) fall to the left / (-) fall to the right.
 *   y : (+) stand / (-) head stand.
 *   z : (+) look down / (-) look up.
 */
void GravitySensor::OnSample(double aX, double aY, double aZ) {
    UpdateDeviceOrientation(aX, aY, aZ);

    const double g2D = std::sqrt(aX * aX + aY * aY);
    const double g3D = std::sqrt(aX * aX + aY * aY + aZ * aZ);

    const double cos2D = std::max(-1.0, std::min(1.0, aY / g2D));
    const double cos3D = std::max(-1.0, std::min(1.0, g2D / g3D));

    double rotation2D = std::acos(cos2D) * kDegrees * (aX >= 0.0 ? 1.0 : -1.0);
    const double rotation3D = std::acos(cos3D) * kDegrees * (aZ >= 0.0 ? 1.0 : -1.0);

    /*
     * A perfectly flat, perfectly still device makes g2D zero and both angles NaN — upstream
     * divides the same way and has the same hazard, but there a NaN is transient. Here it would
     * poison the chaser's accumulator permanently (NaN + distance is NaN forever), so the sample
     * is dropped instead of stored. Real sensor noise means this effectively never fires.
     */
    if (!std::isfinite(rotation2D) || !std::isfinite(rotation3D)) {
        return;
    }

    // kt:126-150 — the phone's own rotation is taken out of the 2D angle.
    switch (deviceOrientation_) {
        case DeviceOrientation::Top:
            break;
        case DeviceOrientation::Left:
            rotation2D -= 90.0;
            break;
        case DeviceOrientation::Right:
            rotation2D += 90.0;
            break;
        case DeviceOrientation::Bottom:
            rotation2D = rotation2D > 0.0 ? rotation2D - 180.0 : rotation2D + 180.0;
            break;
    }

    // kt:151-153 — near flat, the 2D angle stops meaning anything, so it is damped out.
    if (60.0 < std::fabs(rotation3D) && std::fabs(rotation3D) < 120.0) {
        rotation2D *= std::fabs(std::fabs(rotation3D) - 90.0) / 30.0;
    }

    r2D_.store(rotation2D, std::memory_order_relaxed);
    r3D_.store(rotation3D, std::memory_order_relaxed);
}

/**
 * HarmonyOS has no OrientationEventListener, so the 0-359° device angle is derived here from the
 * same vector — which is what the Android listener does internally from the accelerometer: negate
 * the axes, reject the sample when the in-plane magnitude is small next to z (the phone is too
 * flat for the angle to mean anything), then `90 - round(atan2(-y, x))`.
 *
 * The mapping onto TOP/LEFT/BOTTOM/RIGHT is kt:172-186 unchanged.
 */
void GravitySensor::UpdateDeviceOrientation(double aX, double aY, double aZ) {
    const double x = -aX;
    const double y = -aY;
    const double z = -aZ;
    const double magnitude = x * x + y * y;
    if (magnitude * 4.0 < z * z) {
        // Not trustworthy — keep the orientation we had, exactly as the listener does.
        return;
    }
    double orientation = 90.0 - std::round(std::atan2(-y, x) * kDegrees);
    while (orientation >= 360.0) {
        orientation -= 360.0;
    }
    while (orientation < 0.0) {
        orientation += 360.0;
    }

    if (landscape_.load(std::memory_order_relaxed)) {
        deviceOrientation_ = (orientation >= 1.0 && orientation <= 179.0)
                                 ? DeviceOrientation::Right
                                 : DeviceOrientation::Left;
    } else {
        deviceOrientation_ = (orientation > 270.0 || orientation < 90.0)
                                 ? DeviceOrientation::Top
                                 : DeviceOrientation::Bottom;
    }
}

}  // namespace breezy
