#include "render/WeatherImplementors.h"

#include <native_drawing/drawing_brush.h>
#include <native_drawing/drawing_path.h>
#include <native_drawing/drawing_pen.h>
#include <native_drawing/drawing_point.h>
#include <native_drawing/drawing_rect.h>
#include <native_drawing/drawing_round_rect.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>
#include <vector>

namespace breezy {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;

/**
 * The sentinel every falling-particle implementor starts its `lastRotation3D_` at (kt:291 and
 * friends, INITIAL_ROTATION_3D = 1000f). The first frame after a scene is built has no previous
 * angle to difference against, so it moves the particles by 0 instead of by a full 1000°.
 */
constexpr double kInitialRotation3D = 1000.0;

/** One generator for the whole background, seeded once so a rebuild is not deterministic. */
std::mt19937& Rng() {
    static std::mt19937 rng(static_cast<unsigned>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    return rng;
}

/** Math.random() — [0, 1). */
double Random() {
    static std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(Rng());
}

/**
 * CloudImplementor.getRandomFactor (kt:534-536) — `from + nextFloat() % (to - from)`. The modulo
 * on a value already in [0, 1) is upstream's, not a typo: for a range under 1 it narrows the
 * spread, which is why it is kept rather than "corrected" to a plain lerp.
 */
double RandomFactor(double from, double to) {
    return from + std::fmod(Random(), to - from);
}

/** java.util.Random.nextInt(bound) — an integer in [0, bound). */
double NextInt(double bound) {
    return bound <= 0.0 ? 0.0 : std::floor(Random() * bound);
}

/**
 * The square canvas every falling-particle implementor works in: the view's diagonal
 * (RainImplementor.kt:73 and friends). Particles live in this space and are shifted into view
 * coordinates when drawn, so a rotated canvas never shows an empty corner.
 */
double DiagonalCanvasSize(double width, double height) {
    return std::floor(std::sqrt(width * width + height * height));
}

/** 0xAARRGGBB from an RGB triple and a 0..1 alpha. */
uint32_t Rgba(int r, int g, int b, double alpha) {
    const uint32_t a = static_cast<uint32_t>(std::lround(std::clamp(alpha, 0.0, 1.0) * 255.0));
    return (a << 24) | (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) |
           static_cast<uint32_t>(b);
}

struct Rgb {
    int r;
    int g;
    int b;
};

uint32_t Rgba(const Rgb& c, double alpha) {
    return Rgba(c.r, c.g, c.b, alpha);
}

/** canvas.rotate(degrees, px, py) — composed, since the NDK form has no pivot. */
void RotateAbout(OH_Drawing_Canvas* canvas, double degrees, double px, double py) {
    OH_Drawing_CanvasTranslate(canvas, static_cast<float>(px), static_cast<float>(py));
    OH_Drawing_CanvasRotate(canvas, static_cast<float>(degrees), 0.0f, 0.0f);
    OH_Drawing_CanvasTranslate(canvas, static_cast<float>(-px), static_cast<float>(-py));
}

/**
 * A brush held for the length of a draw call, so each particle is not a fresh allocation.
 *
 * OH_Drawing_CanvasAttachBrush takes a `const OH_Drawing_Brush*` and COPIES it into the canvas —
 * changing the brush afterwards has no effect on what is drawn. The first version of this class
 * attached once in the constructor and then only called BrushSetColor, so every particle was
 * painted with the brush's default colour: opaque black. That is what turned the sun rosette, the
 * clouds and every other scene into a black silhouette over the gradient. SetColor now re-attaches,
 * which re-copies the updated state.
 */
class ScopedBrush {
public:
    explicit ScopedBrush(OH_Drawing_Canvas* canvas) : canvas_(canvas) {
        brush_ = OH_Drawing_BrushCreate();
        OH_Drawing_BrushSetAntiAlias(brush_, true);
        OH_Drawing_CanvasAttachBrush(canvas_, brush_);
    }
    ~ScopedBrush() {
        OH_Drawing_CanvasDetachBrush(canvas_);
        OH_Drawing_BrushDestroy(brush_);
    }
    ScopedBrush(const ScopedBrush&) = delete;
    ScopedBrush& operator=(const ScopedBrush&) = delete;

    void SetColor(uint32_t color) {
        OH_Drawing_BrushSetColor(brush_, color);
        OH_Drawing_CanvasAttachBrush(canvas_, brush_);
    }

private:
    OH_Drawing_Canvas* canvas_;
    OH_Drawing_Brush* brush_;
};

void FillRect(OH_Drawing_Canvas* canvas, double left, double top, double right, double bottom) {
    OH_Drawing_Rect* rect = OH_Drawing_RectCreate(
        static_cast<float>(left), static_cast<float>(top), static_cast<float>(right),
        static_cast<float>(bottom));
    OH_Drawing_CanvasDrawRect(canvas, rect);
    OH_Drawing_RectDestroy(rect);
}

void FillRoundRect(OH_Drawing_Canvas* canvas, double left, double top, double right, double bottom,
                   double radius) {
    OH_Drawing_Rect* rect = OH_Drawing_RectCreate(
        static_cast<float>(left), static_cast<float>(top), static_cast<float>(right),
        static_cast<float>(bottom));
    const float r = static_cast<float>(
        std::max(0.0, std::min(radius, std::min((right - left) / 2.0, (bottom - top) / 2.0))));
    OH_Drawing_RoundRect* round = OH_Drawing_RoundRectCreate(rect, r, r);
    OH_Drawing_CanvasDrawRoundRect(canvas, round);
    OH_Drawing_RoundRectDestroy(round);
    OH_Drawing_RectDestroy(rect);
}

void FillCircle(OH_Drawing_Canvas* canvas, double cx, double cy, double radius) {
    OH_Drawing_Point* centre =
        OH_Drawing_PointCreate(static_cast<float>(cx), static_cast<float>(cy));
    OH_Drawing_CanvasDrawCircle(canvas, centre, static_cast<float>(radius));
    OH_Drawing_PointDestroy(centre);
}

/* ------------------------------------------------------------------ clear day */

/**
 * SunImplementor.kt:30-98.
 *
 * Three nested squares at 0.5×0.47×width, ×1.7794 and ×3.0594, each rotating a full 90° every
 * 3000/4000/5000 ms and drawn four times at 22.5° apart — which is what makes the soft rosette.
 */
class SunImplementor : public WeatherImplementor {
public:
    void UpdateData(double, double, double intervalMs, double, double) override {
        for (int i = 0; i < 3; ++i) {  // kt:53-55
            angles_[i] = std::fmod(angles_[i] + 90.0 / (3000.0 + 1000.0 * i) * intervalMs, 90.0);
        }
    }

    void Draw(OH_Drawing_Canvas* canvas, double width, double, double scrollRate,
              double rotation2D, double rotation3D) override {
        if (scrollRate >= 1.0) {
            return;
        }
        const double base = 0.5 * 0.47 * width;
        const double unitSizes[3] = {base, 1.7794 * base, 3.0594 * base};
        static constexpr double kAlphas[3] = {0.16, 0.08, 0.04};
        /** kt:89 — moved down from the old 0.0333 to clear the toolbar icons. */
        constexpr double kSunPosition = 0.5;

        OH_Drawing_CanvasSave(canvas);
        // kt:66-68 — the sun hangs off the top-right corner and slides up to 0.3 × width away from
        // it as the phone tilts. Both deltas scale by canvasSizes[0], the width, including the
        // vertical one.
        const double deltaX = std::sin(rotation2D * kDeg) * 0.3 * width;
        const double deltaY = std::sin(rotation3D * kDeg) * -0.3 * width;
        OH_Drawing_CanvasTranslate(canvas, static_cast<float>(width + deltaX),
                                   static_cast<float>(kSunPosition * width + deltaY));
        ScopedBrush brush(canvas);
        for (int index = 0; index < 3; ++index) {
            brush.SetColor(Rgba(255, 255, 255, (1.0 - scrollRate) * kAlphas[index]));
            OH_Drawing_CanvasRotate(canvas, static_cast<float>(angles_[index]), 0.0f, 0.0f);
            for (int i = 0; i < 4; ++i) {
                FillRect(canvas, -unitSizes[index], -unitSizes[index], unitSizes[index],
                         unitSizes[index]);
                OH_Drawing_CanvasRotate(canvas, 22.5f, 0.0f, 0.0f);
            }
            OH_Drawing_CanvasRotate(canvas, static_cast<float>(-90.0 - angles_[index]), 0.0f,
                                    0.0f);
        }
        OH_Drawing_CanvasRestore(canvas);
    }

private:
    double angles_[3] = {0.0, 0.0, 0.0};
};

/* --------------------------------------------------------------------- clouds */

/** CloudImplementor.Cloud (kt:53-87). */
class Cloud {
public:
    Cloud(double cx, double cy, double radius, double scaleRatio, double moveFactor, Rgb color,
          double alpha, double duration)
        : initCX_(cx), initCY_(cy), initRadius_(radius), scaleRatio_(scaleRatio),
          moveFactor_(moveFactor), duration_(duration), color(color), alpha(alpha), centerX(cx),
          centerY(cy), radius(radius) {
        ComputeRadius();
    }

    /**
     * kt:73-78 — the centre is re-derived from the initial one every frame, offset by the tilt.
     * `radius` is last frame's breathed radius, so a cloud drifts further at the top of its breath.
     */
    void Move(double intervalMs, double rotation2D, double rotation3D) {
        centerX = initCX_ + std::sin(rotation2D * kDeg) * 0.40 * radius * moveFactor_;
        centerY = initCY_ - std::sin(rotation3D * kDeg) * 0.50 * radius * moveFactor_;
        progress_ = std::fmod(progress_ + intervalMs, duration_);
        ComputeRadius();
    }

    Rgb color;
    double alpha;
    double centerX;
    double centerY;
    double radius;

private:
    /** kt:80-86 — grow to scaleRatio over the first half, shrink back over the second. */
    void ComputeRadius() {
        const double half = 0.5 * duration_;
        radius = progress_ < half
                     ? initRadius_ * (1.0 + (scaleRatio_ - 1.0) * progress_ / half)
                     : initRadius_ * (scaleRatio_ -
                                      (scaleRatio_ - 1.0) * (progress_ - half) / half);
    }

    double initCX_;
    double initCY_;
    double initRadius_;
    double scaleRatio_;
    /** kt:58 — how far this cloud slides per degree of tilt. Higher bands drift further. */
    double moveFactor_;
    double duration_;
    double progress_ = 0.0;
};

/** A twinkling star — CloudImplementor.Star (kt:89-124) and MeteorShower's (kt:124-153). */
class Star {
public:
    Star(double cx, double cy, double radius, Rgb color, double duration, bool compressed)
        : centerX(cx), centerY(cy), color(color), duration_(duration), compressed_(compressed) {
        // kt:102 — each star keeps 70..100 % of the shared radius.
        this->radius = radius * (0.7 + 0.3 * Random());
        ComputeAlpha();
    }

    void Shine(double intervalMs) {
        progress_ = std::fmod(progress_ + intervalMs, duration_);
        ComputeAlpha();
    }

    double centerX;
    double centerY;
    double radius = 0.0;
    Rgb color;
    double alpha = 0.0;

private:
    /** kt:117-123 — a linear fade up then down. MeteorShower compresses it so none vanishes. */
    void ComputeAlpha() {
        const double half = 0.5 * duration_;
        const double raw = progress_ < half ? progress_ / half
                                            : 1.0 - (progress_ - half) / half;
        alpha = compressed_ ? raw * 0.66 + 0.33 : raw;
    }

    double duration_;
    bool compressed_;
    double progress_ = 0.0;
};

/** A four-step flash, then a pause — CloudImplementor.Thunder (kt:126-169). */
class Thunder {
public:
    explicit Thunder(double minDelay) : minDelay_(minDelay) {
        Reset();
        ComputeFrame();
    }

    void Shine(double intervalMs) {
        progress_ += intervalMs;
        if (progress_ > duration_ + delay_) {
            Reset();
        }
        ComputeFrame();
    }

    double alpha = 0.0;

private:
    void Reset() {
        progress_ = 0.0;
        delay_ = NextInt(5000.0) + minDelay_;
    }

    void ComputeFrame() {
        const double d = duration_;
        const double p = progress_;
        if (p >= d) {
            alpha = 0.0;
        } else if (p < 0.25 * d) {
            alpha = p / (0.25 * d);
        } else if (p < 0.5 * d) {
            alpha = 1.0 - (p - 0.25 * d) / (0.25 * d);
        } else if (p < 0.75 * d) {
            alpha = (p - 0.5 * d) / (0.25 * d);
        } else {
            alpha = 1.0 - (p - 0.75 * d) / (0.25 * d);
        }
    }

    double minDelay_;
    double duration_ = 300.0;
    double progress_ = 0.0;
    double delay_ = 0.0;
};

enum class CloudType { Cloud, Cloudy, Thunder, Fog, Haze };

/** The ten-colour palette shared by CloudImplementor's stars and MeteorShower (kt:489-518). */
const Rgb kStarPalette[10] = {{210, 247, 255}, {208, 233, 255}, {175, 201, 228}, {164, 194, 220},
                              {97, 171, 220},  {74, 141, 193},  {54, 66, 119},   {34, 48, 74},
                              {236, 234, 213}, {240, 220, 151}};

/** CloudImplementor.kt:34-590. */
class CloudImplementor : public WeatherImplementor {
public:
    CloudImplementor(double width, double height, CloudType type, bool daylight, bool darkMode) {
        const bool lit = daylight && !darkMode;

        if (type == CloudType::Fog || type == CloudType::Haze) {
            // kt:175-205 — one flat colour for all three bands, all at 0.3 alpha.
            const Rgb color = type == CloudType::Fog ? (lit ? Rgb{147, 185, 255} : Rgb{42, 84, 118})
                                                     : (lit ? Rgb{255, 221, 161}
                                                            : Rgb{77, 51, 20});
            ThreeBandClouds(width, color, color, color, 0.3, 0.3, 0.3);
        } else if (type == CloudType::Cloudy || type == CloudType::Thunder) {
            // kt:308-341
            const Rgb color = type == CloudType::Cloudy
                                  ? (lit ? Rgb{160, 179, 191} : Rgb{95, 104, 108})
                                  : (lit ? Rgb{171, 144, 219} : Rgb{44, 28, 77});
            TwoBandClouds(width, color, color, 0.3, 0.3);
        } else {
            // TYPE_CLOUD — kt:411-485
            const Rgb color = lit ? Rgb{203, 245, 255} : Rgb{151, 168, 202};
            PartlyCloudyClouds(width, color);
            if (!daylight) {
                BuildStars(width, height);
            }
        }
        if (type == CloudType::Thunder) {
            thunder_ = std::make_unique<Thunder>(2000.0);
        }
    }

    void UpdateData(double, double, double intervalMs, double rotation2D,
                    double rotation3D) override {
        for (Cloud& cloud : clouds_) {
            cloud.Move(intervalMs, rotation2D, rotation3D);
        }
        for (Star& star : stars_) {
            star.Shine(intervalMs);
        }
        if (thunder_ != nullptr) {
            thunder_->Shine(intervalMs);
        }
    }

    /** kt:553-582 — thunder wash first, then stars, then clouds. */
    /**
     * The rotations reach the clouds through `Move` above; this half of the scene is not rotated
     * as a whole (kt:556-582 takes both angles and uses neither).
     */
    void Draw(OH_Drawing_Canvas* canvas, double width, double height, double scrollRate, double,
              double) override {
        if (scrollRate >= 1.0) {
            return;
        }
        const double visibility = 1.0 - scrollRate;
        ScopedBrush brush(canvas);

        if (thunder_ != nullptr) {
            brush.SetColor(Rgba(81, 67, 168, visibility * thunder_->alpha * 0.66));
            FillRect(canvas, 0.0, 0.0, width, height);
        }
        for (const Star& star : stars_) {
            brush.SetColor(Rgba(star.color, visibility * star.alpha));
            FillCircle(canvas, star.centerX, star.centerY, star.radius);
        }
        for (const Cloud& cloud : clouds_) {
            brush.SetColor(Rgba(cloud.color, visibility * cloud.alpha));
            FillCircle(canvas, cloud.centerX, cloud.centerY, cloud.radius);
        }
    }

private:
    /** kt:206-306 — the nine-cloud arrangement shared by fog and haze. */
    void ThreeBandClouds(double w, Rgb c0, Rgb c1, Rgb c2, double a0, double a1, double a2) {
        clouds_.emplace_back(w * 1.0699, w * (1.1900 * 0.2286 + 0.11), w * (0.4694 * 0.9), 1.1, RandomFactor(1.3, 1.8), c0, a0, 9000);
        clouds_.emplace_back(w * 0.4866, w * (0.4866 * 0.6064 + 0.085), w * (0.3946 * 0.9), 1.1, RandomFactor(1.3, 1.8), c0, a0, 10500);
        clouds_.emplace_back(w * 0.0351, w * (0.1701 * 1.4327 + 0.11), w * (0.4627 * 0.9), 1.1, RandomFactor(1.3, 1.8), c0, a0, 9000);
        clouds_.emplace_back(w * 0.8831, w * (1.0270 * 0.1671 + 0.07), w * (0.3238 * 0.9), 1.15, RandomFactor(1.6, 2.0), c1, a1, 7000);
        clouds_.emplace_back(w * 0.4663, w * (0.4663 * 0.3520 + 0.050), w * (0.2906 * 0.9), 1.15, RandomFactor(1.6, 2.0), c1, a1, 8500);
        clouds_.emplace_back(w * 0.1229, w * (0.0234 * 5.7648 + 0.07), w * (0.2972 * 0.9), 1.15, RandomFactor(1.6, 2.0), c1, a1, 7000);
        clouds_.emplace_back(w * 0.9250, w * (0.9250 * 0.0249 + 0.1500), w * 0.3166, 1.15, RandomFactor(1.8, 2.2), c2, a2, 7000);
        clouds_.emplace_back(w * 0.4694, w * (0.4694 * 0.0489 + 0.1500), w * 0.3166, 1.15, RandomFactor(1.8, 2.2), c2, a2, 8200);
        clouds_.emplace_back(w * 0.0250, w * (0.0250 * 0.6820 + 0.1500), w * 0.3166, 1.15, RandomFactor(1.8, 2.2), c2, a2, 7700);
    }

    /** kt:342-409 — the six-cloud arrangement shared by cloudy and thunder. */
    void TwoBandClouds(double w, Rgb c0, Rgb c1, double a0, double a1) {
        clouds_.emplace_back(w * 1.0699, w * (1.1900 * 0.2286 + 0.11), w * (0.4694 * 0.9), 1.1, RandomFactor(1.3, 1.8), c0, a0, 9000);
        clouds_.emplace_back(w * 0.4866, w * (0.4866 * 0.6064 + 0.085), w * (0.3946 * 0.9), 1.1, RandomFactor(1.3, 1.8), c0, a0, 10500);
        clouds_.emplace_back(w * 0.0351, w * (0.1701 * 1.4327 + 0.11), w * (0.4627 * 0.9), 1.1, RandomFactor(1.3, 1.8), c0, a0, 9000);
        clouds_.emplace_back(w * 0.8831, w * (1.0270 * 0.1671 + 0.07), w * (0.3238 * 0.9), 1.15, RandomFactor(1.6, 2.0), c1, a1, 7000);
        clouds_.emplace_back(w * 0.4663, w * (0.4663 * 0.3520 + 0.050), w * (0.2906 * 0.9), 1.15, RandomFactor(1.6, 2.0), c1, a1, 8500);
        clouds_.emplace_back(w * 0.1229, w * (0.0234 * 5.7648 + 0.07), w * (0.2972 * 0.9), 1.15, RandomFactor(1.6, 2.0), c1, a1, 7000);
    }

    /** kt:418-485 — three near clouds at 0.40 alpha, three far ones at 0.10. */
    void PartlyCloudyClouds(double w, Rgb c) {
        clouds_.emplace_back(w * 0.1529, w * 0.1529 * 0.5568 + w * 0.050, w * 0.2649, 1.20, RandomFactor(1.5, 1.8), c, 0.40, 7000);
        clouds_.emplace_back(w * 0.4793, w * 0.4793 * 0.2185 + w * 0.050, w * 0.2426, 1.20, RandomFactor(1.5, 1.8), c, 0.40, 8500);
        clouds_.emplace_back(w * 0.8531, w * 0.8531 * 0.1286 + w * 0.050, w * 0.2970, 1.20, RandomFactor(1.5, 1.8), c, 0.40, 7050);
        clouds_.emplace_back(w * 0.0551, w * 0.0551 * 2.8600 + w * 0.050, w * 0.4125, 1.15, RandomFactor(1.3, 1.5), c, 0.10, 9500);
        clouds_.emplace_back(w * 0.4928, w * 0.4928 * 0.3897 + w * 0.050, w * 0.3521, 1.15, RandomFactor(1.3, 1.5), c, 0.10, 10500);
        clouds_.emplace_back(w * 1.0499, w * 1.0499 * 0.1875 + w * 0.050, w * 0.4186, 1.15, RandomFactor(1.3, 1.5), c, 0.10, 9000);
    }

    /** kt:489-518 — 50 stars scattered over the rotated canvas bounds. */
    void BuildStars(double width, double height) {
        const double canvasSize = std::sqrt(width * width + height * height);
        const double fieldHeight = (canvasSize - height) * 0.5 + width * 1.1111;
        const double radius = 0.00125 * canvasSize * (0.5 + Random());
        for (int i = 0; i < 50; ++i) {
            stars_.emplace_back(Random() * canvasSize - 0.5 * (canvasSize - width),
                                Random() * fieldHeight - 0.5 * (canvasSize - height), radius,
                                kStarPalette[i % 10], 2500.0 + Random() * 2500.0, false);
        }
    }

    std::vector<Cloud> clouds_;
    std::vector<Star> stars_;
    std::unique_ptr<Thunder> thunder_;
};

/* --------------------------------------------------------- rain / sleet / storm */

/** RainImplementor.TypeRule (kt:50-51, 292-294). */
enum class RainType { Rain, Thunderstorm, Sleet };

/** RainImplementor.Rain (kt:52-116). */
class RainDrop {
public:
    RainDrop(double viewWidth, double viewHeight, Rgb color, double scale, RainType type)
        : viewWidth_(viewWidth), viewHeight_(viewHeight),
          canvasSize_(DiagonalCanvasSize(viewWidth, viewHeight)), color(color), scale_(scale) {
        // kt:74-79 — sleet falls slower and draws fatter, shorter drops.
        const double velocity = type == RainType::Sleet ? 3.0 : 5.0;
        speed_ = canvasSize_ / (1000.0 * (1.75 + Random())) * velocity;
        maxWidth_ = (type == RainType::Sleet ? 0.006 : 0.003) * canvasSize_;
        minWidth_ = (type == RainType::Sleet ? 0.004 : 0.002) * canvasSize_;
        maxHeight_ = maxWidth_ * 10.0;
        minHeight_ = minWidth_ * 6.0;
        Init(true);
    }

    /** kt:102-115. */
    void Move(double intervalMs, double deltaRotation3D) {
        y_ += speed_ * intervalMs *
              (std::pow(scale_, 1.5) -
               5.0 * std::sin(deltaRotation3D * kDeg) * std::cos(8.0 * kDeg));
        x_ -= speed_ * intervalMs * 5.0 * std::sin(deltaRotation3D * kDeg) *
              std::sin(8.0 * kDeg);
        if (y_ >= canvasSize_) {
            Init(false);
        } else {
            BuildRect();
        }
    }

    Rgb color;
    double width = 0.0;
    double left = 0.0;
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;

private:
    /** kt:83-94 */
    void Init(bool firstTime) {
        x_ = NextInt(canvasSize_);
        y_ = firstTime ? NextInt(std::floor(canvasSize_ - maxHeight_)) - canvasSize_
                       : -maxHeight_ * (1.0 + 2.0 * Random());
        width = minWidth_ + Random() * (maxWidth_ - minWidth_);
        height_ = minHeight_ + Random() * (maxHeight_ - minHeight_);
        BuildRect();
    }

    /** kt:96-100 — shift from the diagonal canvas into view coordinates. */
    void BuildRect() {
        const double x = x_ - (canvasSize_ - viewWidth_) * 0.5;
        const double y = y_ - (canvasSize_ - viewHeight_) * 0.5;
        left = x;
        top = y;
        right = x + width * scale_;
        bottom = y + height_ * scale_;
    }

    double viewWidth_;
    double viewHeight_;
    double canvasSize_;
    double scale_;
    double speed_ = 0.0;
    double maxWidth_ = 0.0;
    double minWidth_ = 0.0;
    double maxHeight_ = 0.0;
    double minHeight_ = 0.0;
    double x_ = 0.0;
    double y_ = 0.0;
    double height_ = 0.0;
};

/** RainImplementor.kt:35-308. */
class RainImplementor : public WeatherImplementor {
public:
    RainImplementor(double width, double height, RainType type, bool daylight) {
        // kt:167-221 — three colour bands, three scales, per type and daylight.
        Rgb colors[3];
        int count = 75;  // RAIN_COUNT
        if (type == RainType::Sleet) {
            count = 45;  // SLEET_COUNT
            colors[0] = daylight ? Rgb{128, 197, 255} : Rgb{40, 102, 155};
            colors[1] = daylight ? Rgb{185, 222, 255} : Rgb{99, 144, 182};
        } else if (type == RainType::Thunderstorm) {
            thunder_ = std::make_unique<Thunder>(3000.0);
            // kt:191 — the daylight middle band is the literal -0x93aa6e = #6C5592.
            colors[0] = Rgb{182, 142, 82};
            colors[1] = daylight ? Rgb{108, 85, 146} : Rgb{88, 92, 113};
        } else {
            colors[0] = daylight ? Rgb{223, 179, 114} : Rgb{182, 142, 82};
            colors[1] = daylight ? Rgb{152, 175, 222} : Rgb{88, 92, 113};
        }
        colors[2] = Rgb{255, 255, 255};

        static constexpr double kScales[3] = {0.6, 0.8, 1.0};
        for (int i = 0; i < count; ++i) {
            // kt:227-228 — `i * 3 / count` buckets the drops into the three bands.
            const int band = i * 3 / count;
            drops_.emplace_back(width, height, colors[band], kScales[band], type);
        }
    }

    void UpdateData(double, double, double intervalMs, double, double rotation3D) override {
        // kt:247-254 — the drops are pushed sideways by the CHANGE in the depth angle, not its
        // value.
        const double delta =
            lastRotation3D_ == kInitialRotation3D ? 0.0 : rotation3D - lastRotation3D_;
        for (RainDrop& drop : drops_) {
            drop.Move(intervalMs, delta);
        }
        if (thunder_ != nullptr) {
            thunder_->Shine(intervalMs);
        }
        lastRotation3D_ = rotation3D;
    }

    void Draw(OH_Drawing_Canvas* canvas, double width, double height, double scrollRate,
              double rotation2D, double) override {
        if (scrollRate >= 1.0) {
            return;
        }
        const double alpha = 1.0 - scrollRate;
        OH_Drawing_CanvasSave(canvas);
        // kt:264-271 — rain always leans 8° off vertical, and tilts further with the phone.
        RotateAbout(canvas, rotation2D + 8.0, width * 0.5, height * 0.5);
        {
            ScopedBrush brush(canvas);
            for (const RainDrop& drop : drops_) {
                brush.SetColor(Rgba(drop.color, alpha));
                FillRoundRect(canvas, drop.left, drop.top, drop.right, drop.bottom,
                              drop.width / 2.0);
            }
        }
        OH_Drawing_CanvasRestore(canvas);

        // kt:277-286 — the flash is a full-canvas wash, drawn unrotated.
        if (thunder_ != nullptr && thunder_->alpha > 0.0) {
            ScopedBrush brush(canvas);
            brush.SetColor(Rgba(81, 67, 168, alpha * thunder_->alpha * 0.66));
            FillRect(canvas, 0.0, 0.0, width, height);
        }
    }

private:
    std::vector<RainDrop> drops_;
    std::unique_ptr<Thunder> thunder_;
    double lastRotation3D_ = kInitialRotation3D;
};

/* ------------------------------------------------------------------------ snow */

/** SnowImplementor.Snow (kt:45-97). */
class SnowFlake {
public:
    SnowFlake(double viewWidth, double viewHeight, Rgb color, double scale)
        : viewWidth_(viewWidth), viewHeight_(viewHeight),
          canvasSize_(DiagonalCanvasSize(viewWidth, viewHeight)), color(color), scale_(scale) {
        // kt:62-63
        radius = canvasSize_ * (0.005 + Random() * 0.007) * scale;
        speedY_ = canvasSize_ / (1000.0 * (2.5 + Random())) * 1.5;
        Init(true);
    }

    /** kt:87-96 */
    void Move(double intervalMs, double deltaRotation3D) {
        cx_ += speedX_ * intervalMs * std::pow(scale_, 1.5);
        cy_ += speedY_ * intervalMs *
               (std::pow(scale_, 1.5) - 5.0 * std::sin(deltaRotation3D * kDeg));
        if (centerY >= canvasSize_) {
            Init(false);
        } else {
            ComputeCenter();
        }
    }

    Rgb color;
    double radius = 0.0;
    double centerX = 0.0;
    double centerY = 0.0;

private:
    /** kt:67-80 */
    void Init(bool firstTime) {
        cx_ = NextInt(canvasSize_);
        cy_ = firstTime ? NextInt(std::floor(canvasSize_ - radius)) - canvasSize_ : -radius;
        // kt:76-78 — the bound is floored at 2 so low-resolution screens cannot pass 0 to nextInt.
        speedX_ = NextInt(std::max(std::floor(2.0 * speedY_), 2.0)) - speedY_;
        ComputeCenter();
    }

    /** kt:82-85 — truncated to whole pixels upstream, which this keeps. */
    void ComputeCenter() {
        centerX = std::trunc(cx_ - (canvasSize_ - viewWidth_) * 0.5);
        centerY = std::trunc(cy_ - (canvasSize_ - viewHeight_) * 0.5);
    }

    double viewWidth_;
    double viewHeight_;
    double canvasSize_;
    double scale_;
    double speedY_ = 0.0;
    double speedX_ = 0.0;
    double cx_ = 0.0;
    double cy_ = 0.0;
};

/** SnowImplementor.kt:32-168. */
class SnowImplementor : public WeatherImplementor {
public:
    SnowImplementor(double width, double height, bool daylight) {
        // kt:100-112
        const Rgb colors[3] = {daylight ? Rgb{190, 225, 255} : Rgb{111, 133, 155},
                               daylight ? Rgb{211, 233, 255} : Rgb{140, 161, 182},
                               Rgb{255, 255, 255}};
        static constexpr double kScales[3] = {0.6, 0.8, 1.0};
        constexpr int kCount = 50;
        for (int i = 0; i < kCount; ++i) {
            const int band = i * 3 / kCount;
            flakes_.emplace_back(width, height, colors[band], kScales[band]);
        }
    }

    void UpdateData(double, double, double intervalMs, double, double rotation3D) override {
        // kt:131-134
        const double delta =
            lastRotation3D_ == kInitialRotation3D ? 0.0 : rotation3D - lastRotation3D_;
        for (SnowFlake& flake : flakes_) {
            flake.Move(intervalMs, delta);
        }
        lastRotation3D_ = rotation3D;
    }

    void Draw(OH_Drawing_Canvas* canvas, double width, double height, double scrollRate,
              double rotation2D, double) override {
        if (scrollRate >= 1.0) {
            return;
        }
        const double alpha = 1.0 - scrollRate;
        OH_Drawing_CanvasSave(canvas);
        // kt:145-149 — the whole snowfall leans with the phone.
        RotateAbout(canvas, rotation2D, width * 0.5, height * 0.5);
        {
            ScopedBrush brush(canvas);
            for (const SnowFlake& flake : flakes_) {
                brush.SetColor(Rgba(flake.color, alpha));
                FillCircle(canvas, flake.centerX, flake.centerY, flake.radius);
            }
        }
        OH_Drawing_CanvasRestore(canvas);
    }

private:
    std::vector<SnowFlake> flakes_;
    double lastRotation3D_ = kInitialRotation3D;
};

/* ------------------------------------------------------------------------ hail */

/** HailImplementor.Hail (kt:47-103). */
class HailStone {
public:
    HailStone(double viewWidth, double viewHeight, Rgb color, double scale)
        : canvasSize_(DiagonalCanvasSize(viewWidth, viewHeight)), color(color), scale_(scale) {
        // kt:67-68
        size_ = 0.0324 * std::min(viewWidth, viewHeight) * 0.8;
        speedY_ = std::min(viewWidth, viewHeight) / 150.0;
        Init(true);
        BuildRect();
    }

    /** kt:91-102 */
    void Move(double intervalMs, double deltaRotation3D) {
        cx += speedX_ * intervalMs * std::pow(scale_, 1.5);
        cy += speedY_ * intervalMs *
              (std::pow(scale_, 1.5) - 5.0 * std::sin(deltaRotation3D * kDeg));
        rotation = std::fmod(rotation + speedRotation_ * intervalMs, 360.0);
        if (cy - size_ >= canvasSize_) {
            Init(false);
        }
        BuildRect();
    }

    Rgb color;
    double cx = 0.0;
    double cy = 0.0;
    double rotation = 0.0;
    double left = 0.0;
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;

private:
    /** kt:72-84 */
    void Init(bool firstTime) {
        cx = NextInt(canvasSize_);
        cy = firstTime ? NextInt(std::floor(canvasSize_ - size_)) - canvasSize_ : -size_;
        rotation = 360.0 * Random();
        speedRotation_ = 360.0 / 500.0 * Random();
        speedX_ = 0.75 * (Random() * speedY_ * (Random() < 0.5 ? 1.0 : -1.0));
    }

    /**
     * kt:101 — the rect is built around cx/cy, the DIAGONAL-canvas position, and drawn there too
     * (kt:159-161). Upstream also computes a view-space centre but never draws with it, so only
     * the leftmost slice of the hail field is ever on screen. Kept as-is: the point of this port
     * is to match what the Android build renders, quirks included.
     */
    void BuildRect() {
        left = cx - size_ * scale_;
        top = cy - size_ * scale_;
        right = cx + size_ * scale_;
        bottom = cy + size_ * scale_;
    }

    double canvasSize_;
    double scale_;
    double size_ = 0.0;
    double speedY_ = 0.0;
    double speedX_ = 0.0;
    double speedRotation_ = 0.0;
};

/** HailImplementor.kt:34-175. */
class HailImplementor : public WeatherImplementor {
public:
    HailImplementor(double width, double height, bool daylight) {
        // kt:106-118 — the same palette as sleet.
        const Rgb colors[3] = {daylight ? Rgb{128, 197, 255} : Rgb{40, 102, 155},
                               daylight ? Rgb{185, 222, 255} : Rgb{99, 144, 182},
                               Rgb{255, 255, 255}};
        static constexpr double kScales[3] = {0.6, 0.8, 1.0};
        constexpr int kCount = 51;
        for (int i = 0; i < kCount; ++i) {
            const int band = i * 3 / kCount;
            stones_.emplace_back(width, height, colors[band], kScales[band]);
        }
    }

    void UpdateData(double, double, double intervalMs, double, double rotation3D) override {
        // kt:137-140
        const double delta =
            lastRotation3D_ == kInitialRotation3D ? 0.0 : rotation3D - lastRotation3D_;
        for (HailStone& stone : stones_) {
            stone.Move(intervalMs, delta);
        }
        lastRotation3D_ = rotation3D;
    }

    void Draw(OH_Drawing_Canvas* canvas, double width, double height, double scrollRate,
              double rotation2D, double) override {
        if (scrollRate >= 1.0) {
            return;
        }
        const double alpha = 1.0 - scrollRate;
        OH_Drawing_CanvasSave(canvas);
        // kt:151-155 — the fall leans with the phone; each stone then spins about its own centre.
        RotateAbout(canvas, rotation2D, width * 0.5, height * 0.5);
        for (const HailStone& stone : stones_) {
            // kt:159-161 — each square spins about its own centre.
            OH_Drawing_CanvasSave(canvas);
            RotateAbout(canvas, stone.rotation, stone.cx, stone.cy);
            {
                ScopedBrush brush(canvas);
                brush.SetColor(Rgba(stone.color, alpha));
                FillRect(canvas, stone.left, stone.top, stone.right, stone.bottom);
            }
            OH_Drawing_CanvasRestore(canvas);
        }
        OH_Drawing_CanvasRestore(canvas);
    }

private:
    std::vector<HailStone> stones_;
    double lastRotation3D_ = kInitialRotation3D;
};

/* ------------------------------------------------------------------------ wind */

/** WindImplementor.Wind (kt:45-107) — the same shape as a rain drop, laid on its side. */
class WindStreak {
public:
    WindStreak(double viewWidth, double viewHeight, Rgb color, double scale)
        : viewWidth_(viewWidth), viewHeight_(viewHeight),
          canvasSize_(DiagonalCanvasSize(viewWidth, viewHeight)), color(color), scale_(scale) {
        // kt:66-70 — wide and flat, and much faster than rain.
        speed_ = canvasSize_ / (1000.0 * (0.5 + Random())) * 6.0;
        maxHeight_ = 0.007 * canvasSize_;
        minHeight_ = 0.005 * canvasSize_;
        maxWidth_ = maxHeight_ * 10.0;
        minWidth_ = minHeight_ * 6.0;
        Init(true);
    }

    /** kt:93-106 */
    void Move(double intervalMs, double deltaRotation3D) {
        x_ += speed_ * intervalMs *
              (std::pow(scale_, 1.5) +
               5.0 * std::sin(deltaRotation3D * kDeg) * std::cos(16.0 * kDeg));
        y_ -= speed_ * intervalMs * 5.0 * std::sin(deltaRotation3D * kDeg) *
              std::sin(16.0 * kDeg);
        if (x_ >= canvasSize_) {
            Init(false);
        } else {
            BuildRect();
        }
    }

    Rgb color;
    double left = 0.0;
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;

private:
    /** kt:74-85 — streaks enter from the left, so x is the axis that resets. */
    void Init(bool firstTime) {
        y_ = NextInt(canvasSize_);
        x_ = firstTime ? NextInt(std::floor(canvasSize_ - maxHeight_)) - canvasSize_
                       : -maxHeight_;
        width_ = minWidth_ + Random() * (maxWidth_ - minWidth_);
        height_ = minHeight_ + Random() * (maxHeight_ - minHeight_);
        BuildRect();
    }

    void BuildRect() {
        const double x = x_ - (canvasSize_ - viewWidth_) * 0.5;
        const double y = y_ - (canvasSize_ - viewHeight_) * 0.5;
        left = x;
        top = y;
        right = x + width_ * scale_;
        bottom = y + height_ * scale_;
    }

    double viewWidth_;
    double viewHeight_;
    double canvasSize_;
    double scale_;
    double speed_ = 0.0;
    double maxWidth_ = 0.0;
    double minWidth_ = 0.0;
    double maxHeight_ = 0.0;
    double minHeight_ = 0.0;
    double x_ = 0.0;
    double y_ = 0.0;
    double width_ = 0.0;
    double height_ = 0.0;
};

/** WindImplementor.kt:31-179. */
class WindImplementor : public WeatherImplementor {
public:
    WindImplementor(double width, double height, bool daylight) {
        // kt:110-122 — the hex literals, not the commented-out rgb() values above them.
        const Rgb colors[3] = {daylight ? Rgb{194, 228, 202} : Rgb{49, 62, 58},
                               daylight ? Rgb{178, 224, 186} : Rgb{82, 155, 115},
                               daylight ? Rgb{210, 240, 218} : Rgb{99, 129, 112}};
        static constexpr double kScales[3] = {0.6, 0.8, 1.0};
        constexpr int kCount = 160;
        for (int i = 0; i < kCount; ++i) {
            const int band = i * 3 / kCount;
            streaks_.emplace_back(width, height, colors[band], kScales[band]);
        }
    }

    void UpdateData(double, double, double intervalMs, double, double rotation3D) override {
        // kt:141-144
        const double delta =
            lastRotation3D_ == kInitialRotation3D ? 0.0 : rotation3D - lastRotation3D_;
        for (WindStreak& streak : streaks_) {
            streak.Move(intervalMs, delta);
        }
        lastRotation3D_ = rotation3D;
    }

    void Draw(OH_Drawing_Canvas* canvas, double width, double height, double scrollRate,
              double rotation2D, double) override {
        if (scrollRate >= 1.0) {
            return;
        }
        const double alpha = 1.0 - scrollRate;
        OH_Drawing_CanvasSave(canvas);
        /*
         * kt:154-161 computes `rotation2Dc = rotation2D - 16f` and then rotates by the UNMODIFIED
         * rotation2D. The -16° never reaches the canvas — it is dead upstream, so this port rotates
         * by rotation2D too rather than "fixing" a lean the Android build does not have.
         */
        RotateAbout(canvas, rotation2D, width * 0.5, height * 0.5);
        {
            ScopedBrush brush(canvas);
            for (const WindStreak& streak : streaks_) {
                brush.SetColor(Rgba(streak.color, alpha));
                FillRect(canvas, streak.left, streak.top, streak.right, streak.bottom);
            }
        }
        OH_Drawing_CanvasRestore(canvas);
    }

private:
    std::vector<WindStreak> streaks_;
    double lastRotation3D_ = kInitialRotation3D;
};

/* ------------------------------------------------------------- clear night sky */

/** MeteorShowerImplementor.Meteor (kt:49-122). */
class Meteor {
public:
    Meteor(double viewWidth, double viewHeight, Rgb color, double scale)
        : viewWidth_(viewWidth), viewHeight_(viewHeight),
          canvasSize_(DiagonalCanvasSize(viewWidth, viewHeight)), color(color), scale_(scale) {
        // kt:72-75
        width = viewWidth * 0.0088 * scale;
        speed_ = viewWidth / 200.0;
        maxHeight_ = 1.1 * viewWidth / std::cos(60.0 * kDeg);
        minHeight_ = maxHeight_ * 0.7;
        Init(true);
    }

    /** kt:103-121 — off the bottom, a meteor waits out its delay before respawning. */
    void Update(double intervalMs, double deltaRotation3D) {
        if (y_ > canvasSize_) {
            progress_ += intervalMs;
            if (progress_ > delay_) {
                Init(false);
            }
            return;
        }
        // kt:113-121 — note the exponent here is 0.5, not the 1.5 the other particles use.
        x_ -= speed_ * intervalMs * 5.0 * std::sin(deltaRotation3D * kDeg) *
              std::cos(60.0 * kDeg);
        y_ += speed_ * intervalMs *
              (std::pow(scale_, 0.5) -
               5.0 * std::sin(deltaRotation3D * kDeg) * std::sin(60.0 * kDeg));
        BuildRect();
    }

    Rgb color;
    double width = 0.0;
    double centerX = 0.0;
    double top = 0.0;
    double bottom = 0.0;

private:
    /** kt:80-95 — METEOR_REVIVE_SEC_MIN 5 .. MAX 25 (kt:262-263). */
    void Init(bool firstTime) {
        progress_ = 0.0;
        delay_ = (NextInt(25.0 - 5.0) + 5.0) * 1000.0;
        x_ = NextInt(canvasSize_);
        // kt:86-91 — the first generation starts far below the canvas so they do not all streak
        // at once.
        y_ = firstTime ? canvasSize_ * 2.0
                       : NextInt(canvasSize_) - maxHeight_ - canvasSize_;
        height_ = minHeight_ + Random() * (maxHeight_ - minHeight_);
        BuildRect();
    }

    void BuildRect() {
        const double x = x_ - (canvasSize_ - viewWidth_) * 0.5;
        const double y = y_ - (canvasSize_ - viewHeight_) * 0.5;
        centerX = x + width / 2.0;
        top = y;
        bottom = y + height_;
    }

    double viewWidth_;
    double viewHeight_;
    double canvasSize_;
    double scale_;
    double speed_ = 0.0;
    double maxHeight_ = 0.0;
    double minHeight_ = 0.0;
    double x_ = 0.0;
    double y_ = 0.0;
    double height_ = 0.0;
    double progress_ = 0.0;
    double delay_ = 0.0;
};

/** MeteorShowerImplementor.kt:35-269 — the clear NIGHT scene. */
class MeteorShowerImplementor : public WeatherImplementor {
public:
    MeteorShowerImplementor(double width, double height) {
        // kt:172-179 — ten meteors, each a random colour and a random scale in 0..1.
        for (int i = 0; i < 10; ++i) {
            meteors_.emplace_back(width, height,
                                  kStarPalette[static_cast<int>(NextInt(10.0))], Random());
        }
        // kt:180-195 — 70 stars spread over a band wider and taller than the view.
        const double canvasSize = std::floor(std::sqrt(width * width + height * height));
        const double spreadHeight = std::floor((canvasSize - height) * 0.5 + width * 1.1111);
        const double radius = 0.00125 * canvasSize * (0.5 + Random());
        for (int i = 0; i < 70; ++i) {
            stars_.emplace_back(std::trunc(NextInt(canvasSize) - 0.5 * (canvasSize - width)),
                                std::trunc(NextInt(spreadHeight) - 0.5 * (canvasSize - height)),
                                radius, kStarPalette[i % 10], 2500.0 + Random() * 2500.0, true);
        }
    }

    void UpdateData(double, double, double intervalMs, double, double rotation3D) override {
        // kt:206-214
        const double delta =
            lastRotation3D_ == kInitialRotation3D ? 0.0 : rotation3D - lastRotation3D_;
        for (Meteor& meteor : meteors_) {
            meteor.Update(intervalMs, delta);
        }
        for (Star& star : stars_) {
            star.Shine(intervalMs);
        }
        lastRotation3D_ = rotation3D;
    }

    void Draw(OH_Drawing_Canvas* canvas, double width, double height, double scrollRate,
              double rotation2D, double) override {
        if (scrollRate >= 1.0) {
            return;
        }
        const double fade = 1.0 - scrollRate;
        OH_Drawing_CanvasSave(canvas);
        // kt:224-237 — the star field leans with the phone.
        RotateAbout(canvas, rotation2D, width * 0.5, height * 0.5);
        {
            ScopedBrush brush(canvas);
            for (const Star& star : stars_) {
                brush.SetColor(Rgba(star.color, fade * star.alpha));
                FillCircle(canvas, star.centerX, star.centerY, star.radius);
            }
        }

        // kt:238-256 — the meteors get a further 60° on top of the lean, which is what makes them
        // streak diagonally across the tilted star field.
        OH_Drawing_CanvasSave(canvas);
        RotateAbout(canvas, 60.0, width * 0.5, height * 0.5);
        OH_Drawing_Pen* pen = OH_Drawing_PenCreate();
        OH_Drawing_PenSetAntiAlias(pen, true);
        OH_Drawing_PenSetCap(pen, LINE_ROUND_CAP);
        for (const Meteor& meteor : meteors_) {
            OH_Drawing_PenSetColor(pen, Rgba(meteor.color, fade));
            OH_Drawing_PenSetWidth(pen, static_cast<float>(meteor.width));
            // AttachPen copies, so it has to follow every state change (see ScopedBrush).
            OH_Drawing_CanvasAttachPen(canvas, pen);
            OH_Drawing_Path* path = OH_Drawing_PathCreate();
            OH_Drawing_PathMoveTo(path, static_cast<float>(meteor.centerX),
                                  static_cast<float>(meteor.top));
            OH_Drawing_PathLineTo(path, static_cast<float>(meteor.centerX),
                                  static_cast<float>(meteor.bottom));
            OH_Drawing_CanvasDrawPath(canvas, path);
            OH_Drawing_PathDestroy(path);
        }
        OH_Drawing_CanvasDetachPen(canvas);
        OH_Drawing_PenDestroy(pen);
        OH_Drawing_CanvasRestore(canvas);
        OH_Drawing_CanvasRestore(canvas);
    }

private:
    std::vector<Meteor> meteors_;
    std::vector<Star> stars_;
    double lastRotation3D_ = kInitialRotation3D;
};

}  // namespace

std::unique_ptr<WeatherImplementor> CreateImplementor(WeatherKind kind, bool daylight,
                                                      bool darkMode, double width,
                                                      double height) {
    switch (kind) {
        case WeatherKind::Clear:
            // WeatherImplementorFactory.kt — the clear scene is the sun by day and the meteor
            // shower by night, which is a different implementor entirely, not a recolour.
            if (daylight) {
                return std::make_unique<SunImplementor>();
            }
            return std::make_unique<MeteorShowerImplementor>(width, height);
        case WeatherKind::Cloud:
            return std::make_unique<CloudImplementor>(width, height, CloudType::Cloud, daylight,
                                                      darkMode);
        case WeatherKind::Cloudy:
            return std::make_unique<CloudImplementor>(width, height, CloudType::Cloudy, daylight,
                                                      darkMode);
        case WeatherKind::Fog:
            return std::make_unique<CloudImplementor>(width, height, CloudType::Fog, daylight,
                                                      darkMode);
        case WeatherKind::Haze:
            return std::make_unique<CloudImplementor>(width, height, CloudType::Haze, daylight,
                                                      darkMode);
        case WeatherKind::Thunder:
            return std::make_unique<CloudImplementor>(width, height, CloudType::Thunder, daylight,
                                                      darkMode);
        case WeatherKind::Rainy:
            return std::make_unique<RainImplementor>(width, height, RainType::Rain, daylight);
        case WeatherKind::Thunderstorm:
            return std::make_unique<RainImplementor>(width, height, RainType::Thunderstorm,
                                                     daylight);
        case WeatherKind::Sleet:
            return std::make_unique<RainImplementor>(width, height, RainType::Sleet, daylight);
        case WeatherKind::Snow:
            return std::make_unique<SnowImplementor>(width, height, daylight);
        case WeatherKind::Hail:
            return std::make_unique<HailImplementor>(width, height, daylight);
        case WeatherKind::Wind:
            return std::make_unique<WindImplementor>(width, height, daylight);
        default:
            return nullptr;
    }
}

}  // namespace breezy
