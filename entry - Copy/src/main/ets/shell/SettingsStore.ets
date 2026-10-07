/*
 * The app's persisted settings — SettingsManager (domain/settings/SettingsManager.kt).
 *
 * The shell owns every file in this port, and a preferences bag is a file, so it owns this one too;
 * C++ is told the values and applies ThemeManager.isLightTheme with them. `@ohos.data.preferences`
 * rather than a table, because SettingsManager is a SharedPreferences ConfigStore — a flat bag read
 * constantly, not something queried.
 *
 * KEYS and DEFAULTS are the Kotlin's verbatim, and ArkUIApp's SettingsStore.ets uses the same ones
 * so the two ports start identical:
 *   dark_mode                "system"  (SettingsManager.kt:174-181)
 *   day_night_mode_locations FALSE on anything modern (kt:185-190 — `SDK_INT < Q`)
 */

import { preferences } from '@kit.ArkData';
import { common } from '@kit.AbilityKit';

/** DarkMode.kt:24-32 — the ids are what is stored. */
export const DARK_MODE_SYSTEM: string = 'system';
export const DARK_MODE_LIGHT: string = 'light';
export const DARK_MODE_DARK: string = 'dark';

/** The sentinel SettingsManager stores when no unit is pinned (`value?.id ?: "auto"`). */
export const UNIT_AUTO: string = 'auto';

const STORE_NAME: string = 'breezy_settings';
const KEY_DARK_MODE: string = 'dark_mode';
const KEY_DAY_NIGHT_LOCATIONS: string = 'day_night_mode_locations';
/** SettingsManager.alternateCalendar — "none", "" for the regional default, or a calendar key. */
const KEY_ALTERNATE_CALENDAR: string = 'alternate_calendar';
/* Screen 3e - External modules. SettingsManager's own key names. */
const KEY_WEEK_ICON_MODE: string = 'week_icon_mode';
const KEY_MONOCHROME_ICONS: string = 'widget_monochrome_icons';
const KEY_NOTIFICATION_WIDGET: string = 'notification_widget_switch';
const KEY_NOTIFICATION_PERSISTENT: string = 'notification_widget_persistent_switch';
const KEY_NOTIFICATION_STYLE: string = 'notification_widget_style';
const KEY_NOTIFICATION_TEMP_ICON: string = 'notification_widget_temp_icon_switch';
const KEY_NOTIFICATION_FEELS_LIKE: string = 'notification_widget_feels_like';
/** Screen 3c's six — all four flags default TRUE, the animation mode to "system". */
const KEY_MAIN_BACKGROUND: string = 'main_screen_background';
const KEY_TREND_LINES: string = 'trend_horizontal_line_switch';
const KEY_BACKGROUND_ANIMATION: string = 'background_animation_mode';
const KEY_GRAVITY_SENSOR: string = 'gravity_sensor_switch';
const KEY_CARDS_FADE_IN: string = 'list_animation_switch';
const KEY_ELEMENT_ANIMATIONS: string = 'item_animation_switch';
/*
 * Screen 3d's six, plus the interval it only reads. The defaults are NOT uniform — see
 * SettingsManager.kt:148, :155, :381, :397 and :92-93.
 */
/** kt:109, default FALSE. The section around it IS shown on the reference build. */
const KEY_APP_UPDATE_CHECK: string = 'app_update_check_switch';
const KEY_ALERT_NOTIFICATION: string = 'alert_notification_switch';
const KEY_PRECIPITATION_NOTIFICATION: string = 'precipitation_notification_switch';
const KEY_TODAY_FORECAST: string = 'timing_forecast_switch_today';
const KEY_TOMORROW_FORECAST: string = 'timing_forecast_switch_tomorrow';
const KEY_TODAY_FORECAST_TIME: string = 'forecast_time_today';
const KEY_TOMORROW_FORECAST_TIME: string = 'forecast_time_tomorrow';
/** Screen 3c — Background updates. `SettingsManager.updateInterval`. */
const KEY_REFRESH_RATE: string = 'refresh_rate';
/** `SettingsManager.ignoreUpdatesWhenBatteryLow` — the screen's one switch. Default true. */
const KEY_SKIP_WHEN_BATTERY_LOW: string = 'ignore_updates_when_battery_low';

/**
 * `UpdateInterval.validity` in milliseconds, or -1 for INTERVAL_NEVER, whose Duration is null.
 *
 * `Weather.isValid` is measured against it, and that is what decides whether a location is
 * fetched at all (`checkToUpdateCurrentLocation`, MainActivityViewModel.kt:279-283). The ids are
 * the enum's own, "0:30" through "24:00" (UpdateInterval.kt). ArkUIApp's SettingsStore exports the
 * identical function.
 */
export function updateIntervalMs(id: string): number {
  if (id === 'never') {
    return -1;
  }
  const parts: string[] = id.split(':');
  if (parts.length !== 2) {
    return -1;
  }
  const hours: number = Number.parseInt(parts[0], 10);
  const minutes: number = Number.parseInt(parts[1], 10);
  if (Number.isNaN(hours) || Number.isNaN(minutes)) {
    return -1;
  }
  return (hours * 60 + minutes) * 60 * 1000;
}
/*
 * Screen 3e — Location. `location_service` is SettingsManager.kt:193-198, default `native`.
 *
 * Android keeps per-source config in its own file, `SourceConfigStore(context, id)`, under the bare
 * key "apikey" (BaiduIPLocationService.kt:88-93). This port has one bag, so the source id is folded
 * into the key — same value, one file. ArkUIApp uses the identical key.
 */
const KEY_LOCATION_SERVICE: string = 'location_service';
const KEY_BAIDU_IP_APIKEY: string = 'source_baidu_ip_apikey';

/**
 * Screen 3f — Weather sources. `default_weather_source` is SettingsManager.defaultForecastSource;
 * "auto" means the catalogue picks per location, which is what applyDefaultPreset already does.
 * ArkUIApp stores the identical key.
 */
const KEY_DEFAULT_FORECAST_SOURCE: string = 'default_weather_source';
/** Every key screen 3f can write, in the C++ table's order — see cpp/data/SourcePreferences.cpp. */
const SOURCE_PREF_KEYS: string[] = [
  'source_accu_portal', 'source_accu_apikey', 'source_accu_days', 'source_accu_hours',
  'source_aemet_apikey',
  'source_anambf_instance',
  'source_anamet_instance',
  'source_atmoaura_apikey',
  'source_atmofrance_apikey',
  'source_atmograndest_apikey',
  'source_atmohdf_apikey',
  'source_atmosud_apikey',
  'source_bmkg_apikey',
  'source_brightsky_instance',
  'source_cwa_apikey',
  'source_dccms_instance',
  'source_dmnne_instance',
  'source_dwrgm_instance',
  'source_eccc_apikey',
  'source_ethiomet_instance',
  'source_fpas_instance',
  'source_geonames_apikey',
  'source_gmet_instance',
  'source_igebu_instance',
  'source_infoplaza_apikey',
  'source_inmgb_instance',
  'source_knmi_instance',
  'source_malimeteo_instance',
  'source_metie_apikey',
  'source_metoffice_apikey',
  'source_meteobenin_instance',
  'source_meteotchad_instance',
  'source_mf_apikey',
  'source_mettelsat_instance',
  'source_msdzw_instance',
  'source_nominatim_instance',
  'source_openmeteo_instance_forecast',
  'source_openmeteo_instance_air_quality',
  'source_openmeteo_instance_geocoding',
  'source_openweather_apikey',
  'source_pirateweather_instance', 'source_pirateweather_apikey',
  'source_polleninfo_apikey',
  'source_smasc_instance',
  'source_smasu_instance',
  'source_ssms_instance'
];

/**
 * Screens 3h/3i/3j — the three ordered display lists (SettingsManager.kt:278-311). Stored as the
 * Kotlin stores them: ids joined by "&". ArkUIApp uses the identical keys and defaults.
 */
const KEY_CARD_DISPLAY: string = 'card_display';
const KEY_DAILY_TREND_DISPLAY: string = 'daily_trend_display';
const KEY_HOURLY_TREND_DISPLAY: string = 'hourly_trend_display';
/** SettingsManager.kt:61-73 — note `clock` is absent, which is why it starts as a chip. */
const DEFAULT_CARD_DISPLAY: string =
  'nowcast&daily_forecast&hourly_forecast&precipitation&wind&air_quality&pollen&humidity&uv' +
  '&visibility&pressure&sun&moon';
/** kt:74-80. */
const DEFAULT_DAILY_TREND_DISPLAY: string =
  'temperature&air_quality&wind&uv_index&precipitation&sunshine&feels_like';
/** kt:81-90. */
const DEFAULT_HOURLY_TREND_DISPLAY: string =
  'temperature&air_quality&wind&uv_index&precipitation&feels_like&humidity&pressure&cloud_cover' +
  '&visibility';

/** Screen 3b's five overrides — SettingsManager.kt's `temperature_unit` … `pressure_unit`. */
const KEY_TEMPERATURE_UNIT: string = 'temperature_unit';
const KEY_PRECIPITATION_UNIT: string = 'precipitation_unit';
const KEY_SPEED_UNIT: string = 'speed_unit';
const KEY_DISTANCE_UNIT: string = 'distance_unit';
const KEY_PRESSURE_UNIT: string = 'pressure_unit';

/** The live values. Plain fields: nothing in this port's UI observes them — C++ draws. */
export class ShellSettings {
  static darkMode: string = DARK_MODE_SYSTEM;
  static dayNightForLocations: boolean = false;
  static alternateCalendar: string = 'none';
  /* Screen 3e. Defaults are SettingsManager's. */
  static weekIconMode: string = 'auto';
  static monochromeIcons: boolean = false;
  static notificationWidget: boolean = false;
  static notificationPersistent: boolean = true;
  static notificationStyle: string = 'daily';
  static notificationTempIcon: boolean = false;
  static notificationFeelsLike: boolean = false;
  static temperatureUnit: string = UNIT_AUTO;
  static precipitationUnit: string = UNIT_AUTO;
  static speedUnit: string = UNIT_AUTO;
  static distanceUnit: string = UNIT_AUTO;
  static pressureUnit: string = UNIT_AUTO;
  static mainScreenBackground: boolean = true;
  static trendHorizontalLines: boolean = true;
  static backgroundAnimation: string = 'system';
  static gravitySensor: boolean = true;
  static cardsFadeIn: boolean = true;
  static elementAnimations: boolean = true;
  static appUpdateCheck: boolean = false;
  static alertNotification: boolean = true;
  /** kt:155 — this one defaults to FALSE, unlike the alert switch above it. */
  static precipitationNotification: boolean = false;
  static todayForecast: boolean = false;
  static tomorrowForecast: boolean = false;
  static todayForecastTime: string = '07:00';
  static tomorrowForecastTime: string = '21:00';
  static refreshRate: string = '1:30';
  static skipWhenBatteryLow: boolean = true;
  static locationService: string = 'native';
  static baiduIpApiKey: string = '';
  static defaultForecastSource: string = 'auto';
  static cardDisplay: string = DEFAULT_CARD_DISPLAY;
  static dailyTrendDisplay: string = DEFAULT_DAILY_TREND_DISPLAY;
  static hourlyTrendDisplay: string = DEFAULT_HOURLY_TREND_DISPLAY;
  /**
   * Screen 3f's per-source values, keyed by the storage key. A Map rather than ~50 named fields,
   * because Android does not have named fields for these either — each source owns a
   * `SourceConfigStore(id)` and the screen renders whatever `getPreferences` returns.
   */
  static readonly sourcePrefs: Map<string, string> = new Map<string, string>();
}

/** The blob C++ is handed: one `<key>\t<value>` per SET preference. Absent means "on its default". */
export function sourcePrefsBlob(): string {
  let blob: string = '';
  ShellSettings.sourcePrefs.forEach((value: string, key: string) => {
    blob += key + '\t' + value + '\n';
  });
  return blob;
}

let store: preferences.Preferences | null = null;

/** Opens the bag and loads it. A failure leaves the Kotlin's defaults in place. */
export async function loadSettings(context: common.UIAbilityContext): Promise<void> {
  try {
    store = await preferences.getPreferences(context, STORE_NAME);
    ShellSettings.darkMode = await store.get(KEY_DARK_MODE, DARK_MODE_SYSTEM) as string;
    ShellSettings.alternateCalendar = await store.get(KEY_ALTERNATE_CALENDAR, 'none') as string;
    ShellSettings.weekIconMode = await store.get(KEY_WEEK_ICON_MODE, 'auto') as string;
    ShellSettings.monochromeIcons = await store.get(KEY_MONOCHROME_ICONS, false) as boolean;
    ShellSettings.notificationWidget = await store.get(KEY_NOTIFICATION_WIDGET, false) as boolean;
    ShellSettings.notificationPersistent =
      await store.get(KEY_NOTIFICATION_PERSISTENT, true) as boolean;
    ShellSettings.notificationStyle = await store.get(KEY_NOTIFICATION_STYLE, 'daily') as string;
    ShellSettings.notificationTempIcon =
      await store.get(KEY_NOTIFICATION_TEMP_ICON, false) as boolean;
    ShellSettings.notificationFeelsLike =
      await store.get(KEY_NOTIFICATION_FEELS_LIKE, false) as boolean;
    ShellSettings.dayNightForLocations =
      await store.get(KEY_DAY_NIGHT_LOCATIONS, false) as boolean;
    ShellSettings.temperatureUnit = await store.get(KEY_TEMPERATURE_UNIT, UNIT_AUTO) as string;
    ShellSettings.precipitationUnit = await store.get(KEY_PRECIPITATION_UNIT, UNIT_AUTO) as string;
    ShellSettings.speedUnit = await store.get(KEY_SPEED_UNIT, UNIT_AUTO) as string;
    ShellSettings.distanceUnit = await store.get(KEY_DISTANCE_UNIT, UNIT_AUTO) as string;
    ShellSettings.pressureUnit = await store.get(KEY_PRESSURE_UNIT, UNIT_AUTO) as string;
    ShellSettings.mainScreenBackground = await store.get(KEY_MAIN_BACKGROUND, true) as boolean;
    ShellSettings.trendHorizontalLines = await store.get(KEY_TREND_LINES, true) as boolean;
    ShellSettings.backgroundAnimation =
      await store.get(KEY_BACKGROUND_ANIMATION, 'system') as string;
    ShellSettings.gravitySensor = await store.get(KEY_GRAVITY_SENSOR, true) as boolean;
    ShellSettings.cardsFadeIn = await store.get(KEY_CARDS_FADE_IN, true) as boolean;
    ShellSettings.elementAnimations = await store.get(KEY_ELEMENT_ANIMATIONS, true) as boolean;
    ShellSettings.appUpdateCheck = await store.get(KEY_APP_UPDATE_CHECK, false) as boolean;
    ShellSettings.alertNotification = await store.get(KEY_ALERT_NOTIFICATION, true) as boolean;
    ShellSettings.precipitationNotification =
      await store.get(KEY_PRECIPITATION_NOTIFICATION, false) as boolean;
    ShellSettings.todayForecast = await store.get(KEY_TODAY_FORECAST, false) as boolean;
    ShellSettings.tomorrowForecast = await store.get(KEY_TOMORROW_FORECAST, false) as boolean;
    ShellSettings.todayForecastTime =
      await store.get(KEY_TODAY_FORECAST_TIME, '07:00') as string;
    ShellSettings.tomorrowForecastTime =
      await store.get(KEY_TOMORROW_FORECAST_TIME, '21:00') as string;
    ShellSettings.refreshRate = await store.get(KEY_REFRESH_RATE, '1:30') as string;
  ShellSettings.skipWhenBatteryLow =
    await store.get(KEY_SKIP_WHEN_BATTERY_LOW, true) as boolean;
    ShellSettings.locationService = await store.get(KEY_LOCATION_SERVICE, 'native') as string;
    ShellSettings.baiduIpApiKey = await store.get(KEY_BAIDU_IP_APIKEY, '') as string;
    ShellSettings.defaultForecastSource =
      await store.get(KEY_DEFAULT_FORECAST_SOURCE, 'auto') as string;
    ShellSettings.cardDisplay = await store.get(KEY_CARD_DISPLAY, DEFAULT_CARD_DISPLAY) as string;
    ShellSettings.dailyTrendDisplay =
      await store.get(KEY_DAILY_TREND_DISPLAY, DEFAULT_DAILY_TREND_DISPLAY) as string;
    ShellSettings.hourlyTrendDisplay =
      await store.get(KEY_HOURLY_TREND_DISPLAY, DEFAULT_HOURLY_TREND_DISPLAY) as string;
    /*
     * Read key by key rather than with getAll(), so the map holds exactly the keys screen 3f knows
     * about. Only the ones that HAVE a value go in: an absent key is a row on its default, which is
     * how Android clears one (`config.edit().remove(...)` — ClimWebService.kt:366-368).
     */
    ShellSettings.sourcePrefs.clear();
    for (const key of SOURCE_PREF_KEYS) {
      const value = await store.get(key, '') as string;
      if (value.length > 0) {
        ShellSettings.sourcePrefs.set(key, value);
      }
    }
  } catch (error) {
    console.error('settings unavailable: ' + (error as Error).message);
  }
}

/**
 * Screen 3e's seven values. One entry point keyed by the preference name, the way screen 3b's
 * units are: the screen already sends the key, so a branch per row would be seven copies.
 * Returns whether the key was one of this screen's.
 */
export async function storeModuleSetting(key: string, value: string): Promise<boolean> {
  const asBool: boolean = value === 'true';
  if (key === KEY_WEEK_ICON_MODE) {
    ShellSettings.weekIconMode = value;
  } else if (key === KEY_NOTIFICATION_STYLE) {
    ShellSettings.notificationStyle = value;
  } else if (key === KEY_MONOCHROME_ICONS) {
    ShellSettings.monochromeIcons = asBool;
  } else if (key === KEY_NOTIFICATION_WIDGET) {
    ShellSettings.notificationWidget = asBool;
  } else if (key === KEY_NOTIFICATION_PERSISTENT) {
    ShellSettings.notificationPersistent = asBool;
  } else if (key === KEY_NOTIFICATION_TEMP_ICON) {
    ShellSettings.notificationTempIcon = asBool;
  } else if (key === KEY_NOTIFICATION_FEELS_LIKE) {
    ShellSettings.notificationFeelsLike = asBool;
  } else {
    return false;
  }
  if (store === null) {
    return true;
  }
  try {
    // The two list preferences store a STRING; the five switches store a boolean.
    const isSwitch: boolean = key !== KEY_WEEK_ICON_MODE && key !== KEY_NOTIFICATION_STYLE;
    await store.put(key, isSwitch ? asBool : value);
    await store.flush();
  } catch (error) {
    console.error('could not store ' + key + ': ' + (error as Error).message);
  }
  return true;
}

export async function storeAlternateCalendar(value: string): Promise<void> {
  ShellSettings.alternateCalendar = value;
  if (store === null) {
    return;
  }
  try {
    await store.put(KEY_ALTERNATE_CALENDAR, value);
    await store.flush();
  } catch (error) {
    console.error('could not store alternate_calendar: ' + (error as Error).message);
  }
}

export async function storeDarkMode(value: string): Promise<void> {
  ShellSettings.darkMode = value;
  if (store === null) {
    return;
  }
  try {
    await store.put(KEY_DARK_MODE, value);
    await store.flush();
  } catch (error) {
    console.error('could not store dark_mode: ' + (error as Error).message);
  }
}

/**
 * Stores one unit override. `key` is the preferences key C++ sent back in `<key>|<value>`, so the
 * screen that made the choice and the bag that holds it never have to agree on an enum.
 *
 * Returns false when the key is not one of the five, which is how the edit handler tells a unit
 * choice from a dark-mode one without parsing the value.
 */
export async function storeUnit(key: string, value: string): Promise<boolean> {
  if (key === KEY_TEMPERATURE_UNIT) {
    ShellSettings.temperatureUnit = value;
  } else if (key === KEY_PRECIPITATION_UNIT) {
    ShellSettings.precipitationUnit = value;
  } else if (key === KEY_SPEED_UNIT) {
    ShellSettings.speedUnit = value;
  } else if (key === KEY_DISTANCE_UNIT) {
    ShellSettings.distanceUnit = value;
  } else if (key === KEY_PRESSURE_UNIT) {
    ShellSettings.pressureUnit = value;
  } else {
    return false;
  }
  if (store === null) {
    return true;
  }
  try {
    await store.put(key, value);
    await store.flush();
  } catch (error) {
    console.error('could not store ' + key + ': ' + (error as Error).message);
  }
  return true;
}

/**
 * Stores one screen-3c setting. C++ sends `<key>|<value>` with the value already stringified, so
 * the boolean keys arrive as "true"/"false" and the animation mode as its id.
 *
 * Returns false when the key is not one of the six, which is how the edit handler recognises it.
 */
export async function storeMainScreenSetting(key: string, value: string): Promise<boolean> {
  const flag: boolean = value === 'true';
  if (key === KEY_MAIN_BACKGROUND) {
    ShellSettings.mainScreenBackground = flag;
  } else if (key === KEY_TREND_LINES) {
    ShellSettings.trendHorizontalLines = flag;
  } else if (key === KEY_GRAVITY_SENSOR) {
    ShellSettings.gravitySensor = flag;
  } else if (key === KEY_CARDS_FADE_IN) {
    ShellSettings.cardsFadeIn = flag;
  } else if (key === KEY_ELEMENT_ANIMATIONS) {
    ShellSettings.elementAnimations = flag;
  } else if (key === KEY_BACKGROUND_ANIMATION) {
    ShellSettings.backgroundAnimation = value;
  } else {
    return false;
  }
  if (store === null) {
    return true;
  }
  try {
    if (key === KEY_BACKGROUND_ANIMATION) {
      await store.put(key, value);
    } else {
      await store.put(key, flag);
    }
    await store.flush();
  } catch (error) {
    console.error('could not store ' + key + ': ' + (error as Error).message);
  }
  return true;
}

/**
 * Screen 3d's writes, same shape as storeMainScreenSetting: the four flags and the two times share
 * one entry point because the C++ side sends every edit as `<key>|<value>`.
 *
 * Returns false when the key is not one of the six, which is how the edit handler recognises it.
 */
/**
 * Screen 3c's two values — the refresh rate and the battery switch.
 *
 * Tried BEFORE the notification keys because screen 3d reads the refresh rate too (it greys its
 * rows out when it is "never") and would otherwise never see a change to it.
 */
export async function storeBackgroundUpdatesSetting(key: string, value: string): Promise<boolean> {
  if (key === KEY_REFRESH_RATE) {
    ShellSettings.refreshRate = value;
  } else if (key === KEY_SKIP_WHEN_BATTERY_LOW) {
    ShellSettings.skipWhenBatteryLow = value === 'true';
  } else {
    return false;
  }
  if (store === null) {
    return true;
  }
  try {
    if (key === KEY_REFRESH_RATE) {
      await store.put(key, value);
    } else {
      await store.put(key, value === 'true');
    }
    await store.flush();
  } catch (error) {
    console.warn('could not store ' + key + ': ' + (error as Error).message);
  }
  return true;
}

export async function storeNotificationSetting(key: string, value: string): Promise<boolean> {
  const flag: boolean = value === 'true';
  const isTime: boolean = key === KEY_TODAY_FORECAST_TIME || key === KEY_TOMORROW_FORECAST_TIME;
  if (key === KEY_APP_UPDATE_CHECK) {
    ShellSettings.appUpdateCheck = flag;
  } else if (key === KEY_ALERT_NOTIFICATION) {
    ShellSettings.alertNotification = flag;
  } else if (key === KEY_PRECIPITATION_NOTIFICATION) {
    ShellSettings.precipitationNotification = flag;
  } else if (key === KEY_TODAY_FORECAST) {
    ShellSettings.todayForecast = flag;
  } else if (key === KEY_TOMORROW_FORECAST) {
    ShellSettings.tomorrowForecast = flag;
  } else if (key === KEY_TODAY_FORECAST_TIME) {
    ShellSettings.todayForecastTime = value;
  } else if (key === KEY_TOMORROW_FORECAST_TIME) {
    ShellSettings.tomorrowForecastTime = value;
  } else {
    return false;
  }
  if (store === null) {
    return true;
  }
  try {
    if (isTime) {
      await store.put(key, value);
    } else {
      await store.put(key, flag);
    }
    await store.flush();
  } catch (error) {
    console.error('could not store ' + key + ': ' + (error as Error).message);
  }
  return true;
}

/**
 * Screen 3e's writes. Same shape as the other two: C++ sends `<key>|<value>` and this reports
 * whether the key was one of its own.
 */
export async function storeLocationSetting(key: string, value: string): Promise<boolean> {
  if (key === KEY_LOCATION_SERVICE) {
    ShellSettings.locationService = value;
  } else if (key === KEY_BAIDU_IP_APIKEY) {
    ShellSettings.baiduIpApiKey = value;
  } else {
    return false;
  }
  if (store === null) {
    return true;
  }
  try {
    await store.put(key, value);
    await store.flush();
  } catch (error) {
    console.error('could not store ' + key + ': ' + (error as Error).message);
  }
  return true;
}

/**
 * Screen 3f's writes: the default source, and any `source_*` key the table knows about.
 *
 * An EMPTY value REMOVES the key, which is how Android clears a config back to the bundled default
 * (ClimWebService.kt:366-368) — so the row goes back to showing its own placeholder.
 *
 * Returns false when the key is not one of its own, which is how the edit handler recognises it.
 */
export async function storeSourceSetting(key: string, value: string): Promise<boolean> {
  if (key === KEY_DEFAULT_FORECAST_SOURCE) {
    ShellSettings.defaultForecastSource = value;
  } else if (SOURCE_PREF_KEYS.includes(key)) {
    if (value.length > 0) {
      ShellSettings.sourcePrefs.set(key, value);
    } else {
      ShellSettings.sourcePrefs.delete(key);
    }
  } else {
    return false;
  }
  if (store === null) {
    return true;
  }
  try {
    if (key !== KEY_DEFAULT_FORECAST_SOURCE && value.length === 0) {
      await store.delete(key);
    } else {
      await store.put(key, value);
    }
    await store.flush();
  } catch (error) {
    console.error('could not store ' + key + ': ' + (error as Error).message);
  }
  return true;
}

/**
 * Screens 3h/3i/3j's writes — one of the three ordered lists, as the "&"-joined id string.
 *
 * Returns false when the key is not one of the three, which is how the edit handler recognises it.
 */
export async function storeDisplayList(key: string, value: string): Promise<boolean> {
  if (key === KEY_CARD_DISPLAY) {
    ShellSettings.cardDisplay = value;
  } else if (key === KEY_DAILY_TREND_DISPLAY) {
    ShellSettings.dailyTrendDisplay = value;
  } else if (key === KEY_HOURLY_TREND_DISPLAY) {
    ShellSettings.hourlyTrendDisplay = value;
  } else {
    return false;
  }
  if (store === null) {
    return true;
  }
  try {
    await store.put(key, value);
    await store.flush();
  } catch (error) {
    console.error('could not store ' + key + ': ' + (error as Error).message);
  }
  return true;
}

export async function storeDayNightForLocations(value: boolean): Promise<void> {
  ShellSettings.dayNightForLocations = value;
  if (store === null) {
    return;
  }
  try {
    await store.put(KEY_DAY_NIGHT_LOCATIONS, value);
    await store.flush();
  } catch (error) {
    console.error('could not store day_night_mode_locations: ' + (error as Error).message);
  }
}
