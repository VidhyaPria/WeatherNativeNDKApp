#include "render/Theme.h"

#include <algorithm>
#include <cmath>

namespace breezy {
namespace {

constexpr BackgroundGradient G(uint32_t top, uint32_t middle, uint32_t bottom) {
    return BackgroundGradient{top, middle, bottom, true};
}
constexpr BackgroundGradient kNone{0, 0, 0, false};

}  // namespace

BackgroundGradient WeatherBackgroundGradient(WeatherKind kind, bool daylight, bool darkMode) {
    switch (kind) {
        case WeatherKind::Clear:
            if (daylight) {
                // weather_background_clear_day — it HAS a night variant, so a clear day in dark
                // mode is the muted orange, not the bright one.
                return darkMode ? G(0xFF592B19, 0xFFCC6143, 0xFFD18268)
                                : G(0xFFFFBA5E, 0xFFFFC67E, 0xFFFFF0E2);
            }
            // weather_background_clear_night — no night variant, shared.
            return G(0xFF171D52, 0xFF3F4DBA, 0xFF5E68BD);
        case WeatherKind::Cloud:  // weather_background_partly_cloudy
            return darkMode ? G(0xFF1C2F75, 0xFF585D6D, 0xFF747B99)
                            : G(0xFF64DBFF, 0xFFD6DEE0, 0xFFE2F0F6);
        case WeatherKind::Cloudy:
            return darkMode ? G(0xFF23262C, 0xFF525D66, 0xFF6B8394)
                            : G(0xFFAFBCC7, 0xFFD2D6DB, 0xFFEBEFF8);
        case WeatherKind::Fog:
            return darkMode ? G(0xFF131B45, 0xFF2A5476, 0xFF918EAF)
                            : G(0xFFA2C3FF, 0xFFB3AFD1, 0xFFE7BEB0);
        case WeatherKind::Haze:
            return darkMode ? G(0xFF4D3314, 0xFF755125, 0xFFC5AF9D)
                            : G(0xFFFFDDA1, 0xFFD1C3AF, 0xFFB5E8EF);
        case WeatherKind::Hail:
            return darkMode ? G(0xFF335A7E, 0xFF435A6F, 0xFF303742)
                            : G(0xFFDDF7FF, 0xFFE1E5E9, 0xFFA0AFC1);
        case WeatherKind::Rainy:
            return darkMode ? G(0xFF233361, 0xFF2F4997, 0xFF252731)
                            : G(0xFF97B6D1, 0xFFA6BBCE, 0xFF97A8D7);
        case WeatherKind::Sleet:
            return darkMode ? G(0xFF23306B, 0xFF3549A4, 0xFF23262F)
                            : G(0xFFB4D3ED, 0xFFC5D2DC, 0xFFADC3DE);
        case WeatherKind::Snow:
            return darkMode ? G(0xFF353A47, 0xFF455762, 0xFF414F83)
                            : G(0xFF93A2AD, 0xFFE3E7EB, 0xFFEDF2F6);
        // WeatherImplementorFactory.kt:170 — both thunder kinds share one drawable.
        case WeatherKind::Thunder:
        case WeatherKind::Thunderstorm:
            return darkMode ? G(0xFF2F2B38, 0xFF50406D, 0xFF2C1C4D)
                            : G(0xFFC697D7, 0xFFC1A3CE, 0xFFAB90DB);
        case WeatherKind::Wind:
            return darkMode ? G(0xFF313E3A, 0xFF529B73, 0xFF638170)
                            : G(0xFF96D0A3, 0xFFDFFAE7, 0xFFE8F4EE);
        default:
            return kNone;
    }
}

uint32_t WithAlpha(uint32_t color, double alpha) {
    const uint32_t a = static_cast<uint32_t>(std::lround(std::clamp(alpha, 0.0, 1.0) * 255.0));
    return (a << 24) | (color & 0x00FFFFFF);
}

uint32_t BrighterColor(uint32_t color) {
    const double r = static_cast<double>((color >> 16) & 0xFF) / 255.0;
    const double g = static_cast<double>((color >> 8) & 0xFF) / 255.0;
    const double b = static_cast<double>(color & 0xFF) / 255.0;

    const double max = std::max(r, std::max(g, b));
    const double min = std::min(r, std::min(g, b));
    const double delta = max - min;

    double h = 0.0;
    if (delta != 0.0) {
        if (max == r) {
            h = 60.0 * std::fmod((g - b) / delta, 6.0);
        } else if (max == g) {
            h = 60.0 * ((b - r) / delta + 2.0);
        } else {
            h = 60.0 * ((r - g) / delta + 4.0);
        }
    }
    if (h < 0.0) {
        h += 360.0;
    }

    // Android clamps the HSV components when rebuilding the colour.
    const double s = std::clamp((max == 0.0 ? 0.0 : delta / max) - 0.25, 0.0, 1.0);
    const double v = std::clamp(max + 0.25, 0.0, 1.0);

    const double c = v * s;
    const double x = c * (1.0 - std::fabs(std::fmod(h / 60.0, 2.0) - 1.0));
    const double m = v - c;

    double rr = 0.0;
    double gg = 0.0;
    double bb = 0.0;
    if (h < 60.0) {
        rr = c; gg = x;
    } else if (h < 120.0) {
        rr = x; gg = c;
    } else if (h < 180.0) {
        gg = c; bb = x;
    } else if (h < 240.0) {
        gg = x; bb = c;
    } else if (h < 300.0) {
        rr = x; bb = c;
    } else {
        rr = c; bb = x;
    }

    const auto channel = [](double value) {
        return static_cast<uint32_t>(std::lround(std::clamp(value, 0.0, 1.0) * 255.0));
    };
    return (color & 0xFF000000) | (channel(rr + m) << 16) | (channel(gg + m) << 8) |
           channel(bb + m);
}

namespace {

/**
 * MaterialWeatherThemeDelegate.kt:46-71 — the accent per kind. Not the page background: see
 * WeatherBackgroundGradient for that.
 */
uint32_t WeatherBackgroundColor(WeatherKind kind, bool daylight) {
    switch (kind) {
        case WeatherKind::Clear:  return daylight ? 0xFFFDBC4C : 0xFF141C2C;
        case WeatherKind::Cloud:  return daylight ? 0xFF00A5D9 : 0xFF222D43;
        case WeatherKind::Cloudy: return daylight ? 0xFF9DAFC1 : 0xFF263238;
        case WeatherKind::Fog:    return daylight ? 0xFFA3AEC2 : 0xFF4F5D68;
        case WeatherKind::Haze:   return daylight ? 0xFFE1C899 : 0xFF6C5C49;
        case WeatherKind::Thunder:
        case WeatherKind::Thunderstorm: return daylight ? 0xFFB296BD : 0xFF231739;
        case WeatherKind::Hail:
        case WeatherKind::Snow:
        case WeatherKind::Sleet:  return daylight ? 0xFF68BAFF : 0xFF1A5B92;
        case WeatherKind::Rainy:  return daylight ? 0xFF4297E7 : 0xFF264E8F;
        case WeatherKind::Wind:   return daylight ? 0xFFEACDA3 : 0xFF958675;
        default: return 0x00000000;
    }
}

}  // namespace

uint32_t WeatherThemeAccent(WeatherKind kind, bool daylight) {
    const uint32_t color = WeatherBackgroundColor(kind, daylight);
    return daylight ? color : BrighterColor(color);
}

int BeaufortLevel(double metresPerSecond) {
    // SpeedUnit.kt:311-322 in cm/s, divided by 100.
    static const double kThresholds[12] = {0.3, 1.6, 3.4, 5.5,  8.0,  10.8,
                                           13.9, 17.2, 20.8, 24.5, 28.5, 32.7};
    int level = 0;
    for (int i = 0; i < 12; ++i) {
        if (metresPerSecond >= kThresholds[i]) {
            level = i + 1;
        }
    }
    return level;
}

uint32_t WindColor(double metresPerSecond) {
    const int bf = BeaufortLevel(metresPerSecond);
    if (bf < 4)  return kLevelColors[0];
    if (bf < 6)  return kLevelColors[1];
    if (bf < 8)  return kLevelColors[2];
    if (bf < 10) return kLevelColors[3];
    if (bf < 12) return kLevelColors[4];
    return kLevelColors[5];
}

uint32_t UvColor(double index) {
    if (index < 3.0)  return kLevelColors[0];
    if (index < 6.0)  return kLevelColors[1];
    if (index < 8.0)  return kLevelColors[2];
    if (index < 11.0) return kLevelColors[3];
    return kLevelColors[4];
}

uint32_t HalfDayPrecipitationColor(double millimetres) {
    // PRECIPITATION_HALF_DAY_LIGHT/MEDIUM/HEAVY/RAINSTORM (domain Precipitation.kt:36-39).
    if (millimetres <= 7.75)   return kLevelColors[0];
    if (millimetres <= 32.25)  return kLevelColors[1];
    if (millimetres <= 57.75)  return kLevelColors[2];
    if (millimetres <= 102.25) return kLevelColors[3];
    return kLevelColors[4];
}

uint32_t HourlyPrecipitationColor(double millimetres) {
    // PRECIPITATION_HOURLY_LIGHT/MEDIUM/HEAVY/RAINSTORM (domain Precipitation.kt:42-45).
    if (millimetres <= 5.0)  return kLevelColors[0];
    if (millimetres <= 10.0) return kLevelColors[1];
    if (millimetres <= 15.0) return kLevelColors[2];
    if (millimetres <= 20.0) return kLevelColors[3];
    return kLevelColors[4];
}

uint32_t CloudCoverColor(double percent) {
    if (percent < 37.5)  return kLevelColors[0];
    if (percent <= 62.5) return kLevelColors[1];
    return kLevelColors[2];
}

uint32_t AqiLevelColor(double index) {
    int level = -1;
    for (int i = 0; i < 6; ++i) {
        if (index >= kAqiThresholds[i]) {
            level = i;
        }
    }
    return kLevelColors[std::max(0, std::min(level, 5))];
}

const char* IconBaseName(WeatherKind kind, bool daytime) {
    switch (kind) {
        case WeatherKind::Clear:
            return daytime ? "weather_clear_day" : "weather_clear_night";
        case WeatherKind::Cloud:
            return daytime ? "weather_partly_cloudy_day" : "weather_partly_cloudy_night";
        case WeatherKind::Cloudy:       return "weather_cloudy";
        case WeatherKind::Rainy:        return "weather_rain";
        case WeatherKind::Snow:         return "weather_snow";
        case WeatherKind::Sleet:        return "weather_sleet";
        case WeatherKind::Hail:         return "weather_hail";
        case WeatherKind::Fog:          return "weather_fog";
        case WeatherKind::Haze:         return "weather_haze";
        case WeatherKind::Thunder:      return "weather_thunder";
        case WeatherKind::Thunderstorm: return "weather_thunderstorm";
        case WeatherKind::Wind:         return "weather_wind";
        default: return "";
    }
}

}  // namespace breezy
