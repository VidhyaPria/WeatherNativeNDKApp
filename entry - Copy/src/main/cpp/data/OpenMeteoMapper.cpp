#include "data/OpenMeteoMapper.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <vector>

#include "common/LocalTime.h"
#include "data/Astronomy.h"
#include "data/OpenMeteoCodes.h"
#include "json/Json.h"

namespace breezy {
namespace {

/** Reads index i of a possibly-absent series, collapsing anything non-numeric to nothing. */
Maybe At(const json::Value& series, size_t i) {
    const json::Value& value = series[i];
    return value.IsNumber() ? Maybe{value.AsDouble(0.0)} : Maybe{};
}

Maybe Number(const json::Value& value) {
    return value.IsNumber() ? Maybe{value.AsDouble(0.0)} : Maybe{};
}

Temperature MakeTemperature(Maybe value, Maybe feelsLike) {
    Temperature t;
    t.temperature = value;
    t.feelsLikeTemperature = feelsLike;
    return t;
}

/** WeatherRepository.ets:216-222 — outside the pair, and daylight when there is no pair. */
bool IsDaylightNow(const AstroPair& sun, int64_t nowMs) {
    if (!sun.hasRise || !sun.hasSet) {
        return true;
    }
    return nowMs > sun.rise && nowMs < sun.set;
}

/** OpenMeteoService.getHourlyList — kt:455-496. timeformat=unixtime gives epoch seconds. */
std::vector<Hourly> BuildHourlyList(const json::Value& hourly) {
    const json::Value& time = hourly["time"];
    std::vector<Hourly> list;
    list.reserve(time.Size());
    for (size_t i = 0; i < time.Size(); ++i) {
        Hourly hour;
        hour.date = time[i].AsLong(0) * kMillisPerSecond;
        const json::Value& code = hourly["weather_code"][i];
        const int hourCode = code.IsNumber() ? code.AsInt(-1) : -1;
        hour.weatherKind = WeatherKindForCode(hourCode);
        // The details charts' marker prints this; see Hourly.weatherText.
        hour.weatherText = WeatherTextForCode(hourCode);
        const Maybe isDay = At(hourly["is_day"], i);
        hour.daylight = !isDay.has_value() || *isDay > 0.0;
        hour.temperature = MakeTemperature(At(hourly["temperature_2m"], i),
                                           At(hourly["apparent_temperature"], i));
        hour.precipitationProbability = At(hourly["precipitation_probability"], i);
        hour.windSpeed = At(hourly["wind_speed_10m"], i);
        hour.windDegree = At(hourly["wind_direction_10m"], i);
        hour.windGusts = At(hourly["wind_gusts_10m"], i);
        hour.cloudCover = At(hourly["cloud_cover"], i);
        hour.visibility = At(hourly["visibility"], i);
        hour.uvIndex = At(hourly["uv_index"], i);
        hour.relativeHumidity = At(hourly["relative_humidity_2m"], i);
        hour.dewPoint = At(hourly["dew_point_2m"], i);
        hour.pressure = At(hourly["pressure_msl"], i);
        const Maybe precipitation = At(hourly["precipitation"], i);
        hour.precipitationTotal = precipitation.value_or(0.0);
        /*
         * kt:475-476 — rain is `rain + showers`, which in Kotlin is null only when BOTH are, and
         * snow is `snowfall` read as CENTIMETRES, so it is ×10 into this model's millimetres.
         */
        const Maybe rain = At(hourly["rain"], i);
        const Maybe showers = At(hourly["showers"], i);
        if (rain.has_value() || showers.has_value()) {
            hour.precipitationRain = rain.value_or(0.0) + showers.value_or(0.0);
        }
        const Maybe snowfallCm = At(hourly["snowfall"], i);
        if (snowfallCm.has_value()) {
            hour.precipitationSnow = *snowfallCm * 10.0;
        }
        list.push_back(hour);
    }
    return list;
}

/** One day's hours, already split into the two half-days. */
struct HalfDayBuckets {
    std::vector<const Hourly*> dayHours;
    std::vector<const Hourly*> nightHours;
};

/** The half-day shift from getHourlyListByHalfDay. */
constexpr int64_t kSixHoursMs = 6 * kMillisPerHour;

/**
 * Buckets the whole hourly series by half-day in a single pass.
 *
 * getHourlyListByHalfDay (CommonConverter.kt:634-661): every hour is shifted back by six hours
 * before being assigned to a day, so 00:00–05:59 lands on the PREVIOUS day — a day's "night" is
 * 18:00 through 05:59 the next morning, not the same calendar day's small hours. A shifted hour
 * below 12 means real 06:00–17:59, the day half; everything else is that day's night.
 */
std::map<int64_t, HalfDayBuckets> BucketHoursByDay(const std::vector<Hourly>& hourly,
                                                   int utcOffsetSeconds) {
    std::map<int64_t, HalfDayBuckets> buckets;
    for (const Hourly& hour : hourly) {
        const int64_t shifted = hour.date - kSixHoursMs;
        HalfDayBuckets& bucket = buckets[LocalStartOfDay(shifted, utcOffsetSeconds)];
        if (LocalHours(shifted, utcOffsetSeconds) < 12) {
            bucket.dayHours.push_back(&hour);
        } else {
            bucket.nightHours.push_back(&hour);
        }
    }
    return buckets;
}

/** How many of the half-day's hours carry a given kind. */
int CountKind(const std::vector<const Hourly*>& hours, WeatherKind kind) {
    int count = 0;
    for (const Hourly* hour : hours) {
        if (hour->weatherKind == kind) {
            ++count;
        }
    }
    return count;
}

/**
 * getHalfDayWeatherCodeFromHourlyList (CommonConverter.kt:794-902).
 *
 * Android is deliberately conservative: precipitation has to clear a real threshold, and even
 * then the choice is a COUNT of hours, not a single occurrence — take the severest kind in any
 * one hour and a single rainy hour turns the whole half-day rainy.
 *
 * Order, exactly as upstream:
 *   1. only consider precipitation at all if total >= 1 mm AND max probability > 30 %
 *      (Open-Meteo gives no per-type probabilities, so the isRain/isSnow/isIce/isThunder branch
 *      at kt:813-836 cannot fire and it falls through to the weather-code counts at kt:838-867)
 *   2. thunderstorm if more than one hour has it
 *   3. otherwise the largest of the rain / sleet / snow / hail counts, each needing > 1
 *   4. no precipitation: visibility < 1 km is fog, < 5 km is haze
 *   5. max wind > 10 m/s is windy
 *   6. otherwise by average cloud cover: > 87.5 % cloudy, > 37.5 % partly cloudy, else clear
 *   7. no data at all: the median hour's own code
 */
WeatherKind HalfDayWeatherKind(const std::vector<const Hourly*>& hours, double totalPrecipitation,
                               Maybe maxProbability, Maybe maxWind, Maybe averageCloudCover,
                               Maybe averageVisibility) {
    // kt:802-806
    constexpr double kMinPrecipMm = 1.0;
    constexpr double kMinPrecipProbability = 30.0;
    constexpr double kMaxVisibilityHazeM = 5000.0;
    constexpr double kMaxVisibilityFogM = 1000.0;
    constexpr double kMaxWindWindyMs = 10.0;
    // UnitExtensions.kt:408-410
    constexpr double kCloudCoverFew = 37.5;
    constexpr double kCloudCoverBkn = 87.5;

    // kt:810-812 — an absent probability is treated as 100 %.
    const double probability = maxProbability.value_or(100.0);
    if (totalPrecipitation >= kMinPrecipMm && probability > kMinPrecipProbability) {
        if (CountKind(hours, WeatherKind::Thunderstorm) > 1) {  // kt:839-841
            return WeatherKind::Thunderstorm;
        }
        const int rain = CountKind(hours, WeatherKind::Rainy);
        const int sleet = CountKind(hours, WeatherKind::Sleet);
        const int snow = CountKind(hours, WeatherKind::Snow);
        const int hail = CountKind(hours, WeatherKind::Hail);
        if (std::max(std::max(rain, sleet), std::max(snow, hail)) > 0) {  // kt:850-863
            if (CountKind(hours, WeatherKind::Thunder) > 1) {
                return WeatherKind::Thunderstorm;
            }
            if (hail > 1) {
                return WeatherKind::Hail;
            }
            if ((rain > 1 && snow > 1) || sleet > 1) {
                return WeatherKind::Sleet;
            }
            if (snow > 1) {
                return WeatherKind::Snow;
            }
            return WeatherKind::Rainy;
        }
        // kt:864-867 — precipitation is expected but no hour says what kind.
        return WeatherKind::Rainy;
    }

    if (averageVisibility.has_value() && *averageVisibility < kMaxVisibilityHazeM) {  // kt:870-874
        return *averageVisibility < kMaxVisibilityFogM ? WeatherKind::Fog : WeatherKind::Haze;
    }
    if (maxWind.has_value() && *maxWind > kMaxWindWindyMs) {  // kt:876-879
        return WeatherKind::Wind;
    }
    if (averageCloudCover.has_value()) {  // kt:881-886
        if (*averageCloudCover > kCloudCoverBkn) {
            return WeatherKind::Cloudy;
        }
        if (*averageCloudCover > kCloudCoverFew) {
            return WeatherKind::Cloud;
        }
        return WeatherKind::Clear;
    }
    // kt:888-901 — the median hour that has a code, else partly cloudy.
    std::vector<const Hourly*> coded;
    for (const Hourly* hour : hours) {
        if (hour->weatherKind != WeatherKind::None) {
            coded.push_back(hour);
        }
    }
    if (coded.empty()) {
        return WeatherKind::Cloud;
    }
    const size_t middle = coded.size() % 2 == 0 ? coded.size() / 2 : (coded.size() - 1) / 2;
    return coded[middle]->weatherKind;
}

/** The strings.xml label for a derived kind, so the half-day text matches its icon. */
std::string WeatherTextForKind(WeatherKind kind) {
    switch (kind) {
        case WeatherKind::Clear:        return "Clear sky";
        case WeatherKind::Cloud:        return "Partly cloudy";
        case WeatherKind::Cloudy:       return "Cloudy";
        case WeatherKind::Fog:          return "Fog";
        case WeatherKind::Haze:         return "Haze";
        case WeatherKind::Rainy:        return "Rain";
        case WeatherKind::Sleet:        return "Sleet";
        case WeatherKind::Snow:         return "Snow";
        case WeatherKind::Hail:         return "Hail";
        case WeatherKind::Thunder:      return "Thunder";
        case WeatherKind::Thunderstorm: return "Thunderstorm";
        case WeatherKind::Wind:         return "Windy";
        default: return {};
    }
}

/**
 * getHalfDayTemperatureFromHourlyList (CommonConverter.kt:921-948): the day half takes the MAX
 * of its hours, the night half the MIN — for the real and the feels-like temperature alike.
 */
HalfDay BuildHalfDay(const std::vector<const Hourly*>& hours, bool isDay) {
    HalfDay half;
    if (hours.empty()) {
        return half;
    }
    half.present = true;

    Maybe extremeTemp;
    Maybe extremeFeels;
    Maybe maxProbability;
    Maybe maxWind;
    Maybe windDegree;
    Maybe maxGusts;
    double precipitationTotal = 0.0;
    /*
     * getHalfDayPrecipitationFromHourlyList (CommonConverter.kt:978-1018): each part is the SUM of
     * the hours that report it, and absent when NONE of them does.
     */
    double rainTotal = 0.0;
    int rainCount = 0;
    double snowTotal = 0.0;
    int snowCount = 0;
    double cloudCoverTotal = 0.0;
    int cloudCoverCount = 0;
    double visibilityTotal = 0.0;
    int visibilityCount = 0;

    for (const Hourly* hour : hours) {
        if (hour->cloudCover.has_value()) {
            cloudCoverTotal += *hour->cloudCover;
            ++cloudCoverCount;
        }
        if (hour->visibility.has_value()) {
            visibilityTotal += *hour->visibility;
            ++visibilityCount;
        }
        const Maybe& t = hour->temperature.temperature;
        if (t.has_value() &&
            (!extremeTemp.has_value() || (isDay ? *t > *extremeTemp : *t < *extremeTemp))) {
            extremeTemp = t;
        }
        const Maybe& f = hour->temperature.feelsLikeTemperature;
        if (f.has_value() &&
            (!extremeFeels.has_value() || (isDay ? *f > *extremeFeels : *f < *extremeFeels))) {
            extremeFeels = f;
        }
        const Maybe& p = hour->precipitationProbability;
        if (p.has_value() && (!maxProbability.has_value() || *p > *maxProbability)) {
            maxProbability = p;
        }
        const Maybe& w = hour->windSpeed;
        if (w.has_value() && (!maxWind.has_value() || *w > *maxWind)) {
            maxWind = w;
            windDegree = hour->windDegree;
        }
        // getHalfDayWindFromHourlyList: the gust maximum is taken over the WHOLE half-day, not
        // from the hour that won the speed.
        const Maybe& g = hour->windGusts;
        if (g.has_value() && (!maxGusts.has_value() || *g > *maxGusts)) {
            maxGusts = g;
        }
        precipitationTotal += hour->precipitationTotal;
        if (hour->precipitationRain.has_value()) {
            rainTotal += *hour->precipitationRain;
            ++rainCount;
        }
        if (hour->precipitationSnow.has_value()) {
            snowTotal += *hour->precipitationSnow;
            ++snowCount;
        }
    }

    const Maybe averageCloudCover =
        cloudCoverCount > 0 ? Maybe{cloudCoverTotal / cloudCoverCount} : Maybe{};
    const Maybe averageVisibility =
        visibilityCount > 0 ? Maybe{visibilityTotal / visibilityCount} : Maybe{};

    half.weatherKind = HalfDayWeatherKind(hours, precipitationTotal, maxProbability, maxWind,
                                          averageCloudCover, averageVisibility);
    half.weatherText = WeatherTextForKind(half.weatherKind);
    half.temperature = MakeTemperature(extremeTemp, extremeFeels);
    half.precipitationProbability = maxProbability;
    half.windSpeed = maxWind;
    half.windDegree = windDegree;
    // No max speed means the Kotlin returns no Wind object at all, so there is nothing to hang a
    // gust on either.
    half.windGusts = maxWind.has_value() ? maxGusts : Maybe{};
    half.precipitationTotal = precipitationTotal;
    if (rainCount > 0) {
        half.precipitationRain = rainTotal;
    }
    if (snowCount > 0) {
        half.precipitationSnow = snowTotal;
    }
    return half;
}

/** HEATING_DEGREE_DAY_BASE_TEMP — 18.0 C in deci-celsius (CommonConverter.kt:473). */
constexpr double kHeatingDegreeDayBaseTemp = 180.0;
/** COOLING_DEGREE_DAY_BASE_TEMP — 21.0 C (kt:474). */
constexpr double kCoolingDegreeDayBaseTemp = 210.0;
/** DEGREE_DAY_TEMP_MARGIN — 3.0 C (kt:475), widening the comfortable band to 15.0..24.0 C. */
constexpr double kDegreeDayTempMargin = 30.0;

/**
 * `getDegreeDay` (CommonConverter.kt:477-495), computed here because Open-Meteo omits it and the
 * converter fills it in whenever the source does (kt:405-413).
 *
 * The caller passes `day.temperature` as `minTemp` and `night.temperature` as `maxTemp`, which
 * reads like an upstream naming slip — but the arithmetic is a plain mean of the two half-day
 * temperatures, so the result is identical and this mirrors it rather than "fixing" it.
 */
void ApplyDegreeDay(Daily& daily) {
    if (!daily.day.temperature.temperature.has_value() ||
        !daily.night.temperature.temperature.has_value()) {
        return; // kt:483 — a missing half-day leaves degreeDay null, so no row.
    }
    // Android holds temperatures as a rounded deci-celsius Long, so the mean is taken there too.
    const double meanTemp = (std::round(*daily.day.temperature.temperature * 10.0) +
                             std::round(*daily.night.temperature.temperature * 10.0)) / 2.0;
    if (meanTemp >= kHeatingDegreeDayBaseTemp - kDegreeDayTempMargin &&
        meanTemp <= kCoolingDegreeDayBaseTemp + kDegreeDayTempMargin) {
        daily.heatingDegreeDay = 0.0;
        daily.coolingDegreeDay = 0.0;
        return;
    }
    if (meanTemp < kHeatingDegreeDayBaseTemp) {
        daily.heatingDegreeDay = std::round(kHeatingDegreeDayBaseTemp - meanTemp);
        daily.coolingDegreeDay = 0.0;
    } else {
        daily.heatingDegreeDay = 0.0;
        daily.coolingDegreeDay = std::round(meanTemp - kCoolingDegreeDayBaseTemp);
    }
}

/**
 * OpenMeteoService.getDailyList — kt:406-453 — plus the half-day derivation above. The Kotlin
 * drops the final entry (`time.size - 1`, kt:412-413) because the last day is incomplete.
 */
std::vector<Daily> BuildDailyList(const json::Value& daily, const std::vector<Hourly>& hourly,
                                  int utcOffsetSeconds) {
    const std::map<int64_t, HalfDayBuckets> buckets = BucketHoursByDay(hourly, utcOffsetSeconds);
    const json::Value& time = daily["time"];
    std::vector<Daily> list;
    if (time.Size() == 0) {
        return list;
    }
    list.reserve(time.Size() - 1);

    for (size_t i = 0; i + 1 < time.Size(); ++i) {
        Daily day;
        // kt:414-419 truncates to midnight in the LOCATION's zone, not the device's.
        day.date = LocalStartOfDay(time[i].AsLong(0) * kMillisPerSecond, utcOffsetSeconds);
        day.uvIndex = At(daily["uv_index_max"], i);
        const Maybe sunshineSeconds = At(daily["sunshine_duration"], i);
        if (sunshineSeconds.has_value()) {
            day.sunshineHours = *sunshineSeconds / 3600.0;
        }

        auto bucket = buckets.find(day.date);
        if (bucket != buckets.end()) {
            day.day = BuildHalfDay(bucket->second.dayHours, true);
            day.night = BuildHalfDay(bucket->second.nightHours, false);
        }
        ApplyDegreeDay(day);
        list.push_back(day);
    }
    return list;
}

/** Weather.kt — the index within dailyForecast whose date is today AT THE LOCATION. */
int FindTodayIndex(const std::vector<Daily>& daily, int64_t nowMs, int utcOffsetSeconds) {
    const int64_t todayStart = LocalStartOfDay(nowMs, utcOffsetSeconds);
    for (size_t i = 0; i < daily.size(); ++i) {
        if (daily[i].date == todayStart) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

/**
 * The precipitation nowcast, trimmed to the coming hour.
 *
 * Open-Meteo returns 15-minute accumulations for the whole forecast range; the card only shows the
 * hour ahead, so this keeps the buckets covering [now, now + 1h). Intensity is the accumulation
 * scaled to mm/h (× 4 for a quarter-hour bucket).
 */
std::vector<Minutely> BuildMinutelyList(const json::Value& source, int64_t nowMs) {
    const json::Value& time = source["time"];
    const json::Value& precipitation = source["precipitation"];
    constexpr int64_t kBucketMs = 15 * kMillisPerMinute;
    const int64_t horizon = nowMs + kMillisPerHour;

    std::vector<Minutely> list;
    for (size_t i = 0; i < time.Size(); ++i) {
        const int64_t start = time[i].AsLong(0) * kMillisPerSecond;
        const int64_t end = start + kBucketMs;
        // Keep the bucket the current instant falls in, plus everything up to the horizon.
        if (end <= nowMs || start >= horizon) {
            continue;
        }
        Minutely bucket;
        bucket.date = start;
        bucket.endingDate = end;
        bucket.precipitationIntensity = At(precipitation, i).value_or(0.0) * 4.0;
        list.push_back(bucket);
    }
    return list;
}

/**
 * AstroViewHolder.kt:102-115 — before tonight's moonrise, show YESTERDAY's moon if it has not set.
 *
 * Only the moon gets this: the sun's rise and set both fall inside one calendar day, but the moon's
 * pair regularly straddles midnight, so between midnight and tonight's rise "today's moon" is a
 * pair entirely in the future. Without the rule the tile printed tonight's rise and tomorrow's set
 * (10:32 PM / 7:59 PM against the Android device's 10:20 PM / 6:23 PM) and, because `now` was
 * before the rise, the arc drew no progress and no moon at all — the marker simply never appeared.
 * ArkUIApp has carried this since WeatherRepository.ets:122-133; the NDK port did not.
 */
AstroPair ValidatedMoon(int64_t nowMs, double latitude, double longitude, int utcOffsetSeconds) {
    const AstroPair today = CalculatedMoon(nowMs, latitude, longitude, utcOffsetSeconds);
    if (today.hasRise && nowMs >= today.rise) {
        return today;
    }
    const AstroPair yesterday =
        CalculatedMoon(nowMs - kMillisPerDay, latitude, longitude, utcOffsetSeconds);
    if (yesterday.hasRise && yesterday.hasSet && yesterday.set > nowMs) {
        return yesterday;
    }
    return today;
}

Astro ToAstro(const AstroPair& pair) {
    Astro astro;
    astro.rise = pair.rise;
    astro.set = pair.set;
    astro.hasRise = pair.hasRise;
    astro.hasSet = pair.hasSet;
    return astro;
}

/**
 * MainAdapter.kt:122-137 — the precipitation tile shows the total for the half-day the current
 * hour falls in: before 05:00 yesterday's night, before 17:00 today's day, otherwise tonight.
 */
Maybe CurrentHalfDayPrecipitation(const std::vector<Daily>& daily, int todayIndex, int64_t nowMs,
                                  int utcOffsetSeconds) {
    if (todayIndex < 0 || todayIndex >= static_cast<int>(daily.size())) {
        return {};
    }
    const Daily& today = daily[static_cast<size_t>(todayIndex)];
    const int hour = LocalHours(nowMs, utcOffsetSeconds);
    if (hour < 5) {
        if (todayIndex == 0) {
            return {};
        }
        const HalfDay& night = daily[static_cast<size_t>(todayIndex - 1)].night;
        return night.present ? Maybe{night.precipitationTotal} : Maybe{};
    }
    if (hour < 17) {
        return today.day.present ? Maybe{today.day.precipitationTotal} : Maybe{};
    }
    return today.night.present ? Maybe{today.night.precipitationTotal} : Maybe{};
}

/** OpenMeteoService.getCurrent — kt:379-404. Speeds arrive in m·s⁻¹ (windspeed_unit=ms). */
Current BuildCurrent(const json::Value& source) {
    Current current;
    const json::Value& code = source["weather_code"];
    const int rawCode = code.IsNumber() ? code.AsInt(-1) : -1;
    current.weatherText = WeatherTextForCode(rawCode);
    current.weatherKind = WeatherKindForCode(rawCode);
    current.temperature = MakeTemperature(Number(source["temperature_2m"]),
                                          Number(source["apparent_temperature"]));
    current.windSpeed = Number(source["wind_speed_10m"]);
    current.windDegree = Number(source["wind_direction_10m"]);
    current.windGusts = Number(source["wind_gusts_10m"]);
    current.uvIndex = Number(source["uv_index"]);
    current.relativeHumidity = Number(source["relative_humidity_2m"]);
    current.dewPoint = Number(source["dew_point_2m"]);
    current.pressure = Number(source["pressure_msl"]);
    current.visibility = Number(source["visibility"]);
    return current;
}

}  // namespace

std::string WeatherTextForCode(int code) {
    switch (code) {
        case 0: return "Clear sky";
        case 1: return "Mostly clear";
        case 2: return "Partly cloudy";
        case 3: return "Overcast";
        case 45: return "Fog";
        case 48: return "Depositing rime fog";
        case 51: return "Light drizzle";
        case 53: return "Moderate drizzle";
        case 55: return "Heavy drizzle";
        case 56: return "Light freezing drizzle";
        case 57: return "Heavy freezing drizzle";
        case 61: return "Light rain";
        case 63: return "Moderate rain";
        case 65: return "Heavy rain";
        case 66: return "Light freezing rain";
        case 67: return "Heavy freezing rain";
        case 71: return "Light snow";
        case 73: return "Moderate snow";
        case 75: return "Heavy snow";
        case 77: return "Snow grains";
        case 80: return "Light rain showers";
        case 81: return "Moderate rain showers";
        case 82: return "Heavy rain showers";
        case 85: return "Light snow showers";
        case 86: return "Heavy snow showers";
        case 95: return "Thunderstorm slight or moderate";
        case 96: return "Thunderstorm with slight hail";
        case 99: return "Thunderstorm with heavy hail";
        default: return {};
    }
}

ForecastResult MapForecast(const std::string& body, double latitude, double longitude,
                           int64_t nowMs) {
    ForecastResult result;

    const json::Value root = json::Parse(body);
    if (root.GetType() != json::Value::Type::Object || root["error"].AsBool(false)) {
        return result;
    }

    result.ok = true;
    result.utcOffsetSeconds = root["utc_offset_seconds"].AsInt(0);

    Weather& weather = result.weather;
    weather.refreshTime = nowMs;
    weather.hourlyForecast = BuildHourlyList(root["hourly"]);
    // Weather.nextHourlyForecast — the hourly card starts at the current hour, not at past_days.
    const int64_t currentHourStart = LocalStartOfHour(nowMs, result.utcOffsetSeconds);
    for (const Hourly& hour : weather.hourlyForecast) {
        if (hour.date >= currentHourStart) {
            weather.nextHourlyForecast.push_back(hour);
        }
    }
    weather.dailyForecast =
        BuildDailyList(root["daily"], weather.hourlyForecast, result.utcOffsetSeconds);
    weather.todayIndex = FindTodayIndex(weather.dailyForecast, nowMs, result.utcOffsetSeconds);
    weather.minutelyForecast = BuildMinutelyList(root["minutely_15"], nowMs);
    // `alertList` stays empty: Open-Meteo publishes no warnings, so the alert card never appears.

    const AstroPair sun = CalculatedSun(nowMs, latitude, longitude, result.utcOffsetSeconds);
    const AstroPair moon = ValidatedMoon(nowMs, latitude, longitude, result.utcOffsetSeconds);

    const json::Value& current = root["current"];
    if (current.GetType() == json::Value::Type::Object) {
        weather.current = BuildCurrent(current);
        weather.current.precipitationTotal =
            CurrentHalfDayPrecipitation(weather.dailyForecast, weather.todayIndex, nowMs,
                                        result.utcOffsetSeconds);
        // Open-Meteo returns no sun or moon at all; CommonConverter.kt:402-423 computes both.
        weather.current.sun = ToAstro(sun);
        weather.current.moon = ToAstro(moon);
        weather.current.moon.phaseAngle = MoonPhaseAngle(nowMs, result.utcOffsetSeconds);
        weather.current.moon.phaseName = MoonPhaseName(weather.current.moon.phaseAngle);
        weather.hasCurrent = true;
    }

    result.daylight = IsDaylightNow(sun, nowMs);
    return result;
}

}  // namespace breezy
