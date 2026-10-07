/*
 * The Open-Meteo forecast request — the shell's only job besides hosting the surface.
 *
 * The native SDK ships no HTTP stack (no libcurl, no netstack under
 * sysroot/usr/lib/aarch64-linux-ohos), and vendoring one would add roughly a megabyte to this
 * HAP — which would show up in the very memory comparison the two ports exist to make. So the
 * request goes through @ohos.net.http and the response body is handed to C++ unread.
 *
 * The URL is byte-for-byte ArkUIApp's (data/openmeteo/OpenMeteoApi.ets), which is in turn
 * OpenMeteoForecastApi.kt:28-40 with the fixed arguments from OpenMeteoService.getWeather
 * (OpenMeteoService.kt:180-268) — including forecast_days=16, past_days=1 and windspeed_unit=ms.
 */

import { http } from '@kit.NetworkKit';

/** OpenMeteoService.kt:946 */
const FORECAST_BASE_URL: string = 'https://api.open-meteo.com/';
/** OpenMeteoService.kt:944 */
const AIR_QUALITY_BASE_URL: string = 'https://air-quality-api.open-meteo.com/';
/** OpenMeteoService.kt:945 */
const GEOCODING_BASE_URL: string = 'https://geocoding-api.open-meteo.com/';

/** OpenMeteoService.kt:640 — the geocoding request asks for 20 hits. */
const SEARCH_RESULT_COUNT: number = 20;

/** OpenMeteoWeatherModel.BEST_MATCH.id — the default `models` argument (ForecastApi.kt:32). */
const MODEL_BEST_MATCH: string = 'best_match';

/** OpenMeteoService.kt:180-202 */
const DAILY_FIELDS: string[] = [
  'temperature_2m_max',
  'temperature_2m_min',
  'apparent_temperature_max',
  'apparent_temperature_min',
  'sunshine_duration',
  'uv_index_max',
  'relative_humidity_2m_mean',
  'relative_humidity_2m_max',
  'relative_humidity_2m_min',
  'dew_point_2m_mean',
  'dew_point_2m_max',
  'dew_point_2m_min',
  'pressure_msl_mean',
  'pressure_msl_max',
  'pressure_msl_min',
  'cloud_cover_mean',
  'cloud_cover_max',
  'cloud_cover_min',
  'visibility_mean',
  'visibility_max',
  'visibility_min'
];

/** OpenMeteoService.kt:203-222 */
const HOURLY_FIELDS: string[] = [
  'temperature_2m',
  'apparent_temperature',
  'precipitation_probability',
  'precipitation',
  'rain',
  'showers',
  'snowfall',
  'weather_code',
  'wind_speed_10m',
  'wind_direction_10m',
  'wind_gusts_10m',
  'uv_index',
  'is_day',
  'relative_humidity_2m',
  'dew_point_2m',
  'pressure_msl',
  'cloud_cover',
  'visibility'
];

/** OpenMeteoService.kt:223-236 */
const CURRENT_FIELDS: string[] = [
  'temperature_2m',
  'apparent_temperature',
  'weather_code',
  'wind_speed_10m',
  'wind_direction_10m',
  'wind_gusts_10m',
  'uv_index',
  'relative_humidity_2m',
  'dew_point_2m',
  'pressure_msl',
  'cloud_cover',
  'visibility'
];

/** OpenMeteoService.kt:237-240 — precipitation_probability is commented out upstream too. */
const MINUTELY_FIELDS: string[] = ['precipitation'];

/** OpenMeteoService.kt:298-304 */
const AIR_QUALITY_FIELDS: string[] = [
  'pm10',
  'pm2_5',
  'carbon_monoxide',
  'nitrogen_dioxide',
  'sulphur_dioxide',
  'ozone'
];

/** OpenMeteoService.kt:310-316 — pollen shares the air-quality request. */
const POLLEN_FIELDS: string[] = [
  'alder_pollen',
  'birch_pollen',
  'grass_pollen',
  'mugwort_pollen',
  'olive_pollen',
  'ragweed_pollen'
];

/** OpenMeteoService.kt:266 */
const FORECAST_DAYS: number = 16;
/**
 * OpenMeteoService.kt:325 — the air-quality endpoint asks for 7, not 16.
 *
 * Not a stylistic difference: air-quality-api.open-meteo.com caps forecast_days at 7 and answers
 * anything larger with HTTP 400, so sending 16 makes every air-quality request fail silently.
 */
const AIR_QUALITY_FORECAST_DAYS: number = 7;
/** OpenMeteoService.kt:267 — yesterday is fetched so today's night half-day is complete. */
const PAST_DAYS: number = 1;

const TIMEOUT_MS: number = 20000;

/** Thrown for a transport failure, so the caller can surface it rather than show nothing. */
export class OpenMeteoError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'OpenMeteoError';
  }
}

/**
 * OpenMeteoForecastApi.kt:28-40. `timezone=auto` and `timeformat=unixtime` are baked into the
 * @GET path upstream, so they are fixed here too.
 *
 * Returns the body as-is. It is deliberately NOT parsed here — an `error: true` response is
 * C++'s to notice, along with everything else the body says.
 */
async function getBody(url: string): Promise<string> {
  const request = http.createHttp();
  try {
    const response = await request.request(url, {
      method: http.RequestMethod.GET,
      header: { 'Accept': 'application/json' } as Record<string, string>,
      connectTimeout: TIMEOUT_MS,
      readTimeout: TIMEOUT_MS,
      expectDataType: http.HttpDataType.STRING
    });
    if (response.responseCode !== 200) {
      throw new OpenMeteoError('HTTP ' + response.responseCode.toString());
    }
    return response.result as string;
  } finally {
    // The handle owns a native socket; ArkTS will not reclaim it for us.
    request.destroy();
  }
}

/**
 * FMI's CAP warning feed (FmiAlertsApi.kt) — Open-Meteo publishes no alerts, so Breezy's alert
 * card is fed by a second source. Two stages: this index, then one document per warning.
 */
const FMI_ALERTS_FEED: string = 'https://alerts.fmi.fi/cap/feed/rss_en-GB.rss';

export async function fetchAlertFeed(): Promise<string> {
  return getBody(FMI_ALERTS_FEED);
}

/** One CAP document. They are ~650 KB each, which is why the caller caches them by URL. */
export async function fetchCapDocument(url: string): Promise<string> {
  return getBody(url);
}

/** OpenMeteoAirQualityApi.kt — the pollutant series the Air quality tab plots. */
export async function fetchAirQualityBody(latitude: number, longitude: number): Promise<string> {
  return getBody(
    AIR_QUALITY_BASE_URL + 'v1/air-quality' +
    '?timezone=auto&timeformat=unixtime' +
    '&latitude=' + latitude.toString() +
    '&longitude=' + longitude.toString() +
    '&hourly=' + AIR_QUALITY_FIELDS.concat(POLLEN_FIELDS).join(',') +
    '&forecast_days=' + AIR_QUALITY_FORECAST_DAYS.toString() +
    '&past_days=' + PAST_DAYS.toString()
  );
}

/**
 * OpenMeteoGeocodingApi.getLocations + OpenMeteoService.requestLocationSearch (kt:634-655).
 *
 * The body comes back unread, as every other one does. The two rules the Kotlin applies to it —
 * `results` absent meaning a FAILURE rather than "no matches" unless `generationtime_ms` says
 * otherwise (:643-648), and dropping every hit with no country code (:651) — are in C++, in
 * ParseGeocoding, so the two ports cannot apply them differently.
 *
 * `format=json` is baked into the @GET path upstream, so it is fixed here too.
 */
export async function fetchLocationSearchBody(query: string, language: string): Promise<string> {
  return getBody(
    GEOCODING_BASE_URL + 'v1/search?format=json' +
    '&name=' + encodeURIComponent(query) +
    '&count=' + SEARCH_RESULT_COUNT.toString() +
    '&language=' + encodeURIComponent(language)
  );
}

export async function fetchForecastBody(
  latitude: number,
  longitude: number,
  /**
   * `getWeatherModels(location).joinToString(",")` (OpenMeteoService.kt:245) — the LOCATION's own
   * model selection. '' takes the source's default, which is `best_match` alone (kt:772), and is
   * what a location that has never been through the preferences dialog stores.
   */
  models: string = ''
): Promise<string> {
  const url: string = FORECAST_BASE_URL + 'v1/forecast' +
    '?timezone=auto&timeformat=unixtime' +
    '&latitude=' + latitude.toString() +
    '&longitude=' + longitude.toString() +
    '&models=' + (models.length > 0 ? models : MODEL_BEST_MATCH) +
    '&daily=' + DAILY_FIELDS.join(',') +
    '&hourly=' + HOURLY_FIELDS.join(',') +
    '&minutely_15=' + MINUTELY_FIELDS.join(',') +
    '&current=' + CURRENT_FIELDS.join(',') +
    '&forecast_days=' + FORECAST_DAYS.toString() +
    '&past_days=' + PAST_DAYS.toString() +
    '&windspeed_unit=ms';

  return getBody(url);
}
