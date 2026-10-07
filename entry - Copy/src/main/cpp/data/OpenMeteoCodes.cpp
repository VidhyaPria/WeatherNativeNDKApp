#include "data/OpenMeteoCodes.h"

namespace breezy {

WeatherKind WeatherKindForCode(int code) {
    if (code < 0) {
        return WeatherKind::None;
    }
    if (code == 0 || code == 1) {
        return WeatherKind::Clear;
    }
    if (code == 2) {
        return WeatherKind::Cloud;
    }
    if (code == 3) {
        return WeatherKind::Cloudy;
    }
    if (code == 45 || code == 48) {
        return WeatherKind::Fog;
    }
    // 51/53/55 drizzle, 56/57 freezing drizzle, 61/63/65 rain, 66/67 freezing rain,
    // 80/81/82 rain showers (kt:618-623).
    if ((code >= 51 && code <= 57) || (code >= 61 && code <= 67) || (code >= 80 && code <= 82)) {
        return WeatherKind::Rainy;
    }
    if ((code >= 71 && code <= 75) || code == 85 || code == 86) {
        return WeatherKind::Snow;
    }
    if (code == 77) {
        return WeatherKind::Sleet;
    }
    if (code == 95 || code == 96 || code == 99) {
        return WeatherKind::Thunderstorm;
    }
    return WeatherKind::None;
}

}  // namespace breezy
