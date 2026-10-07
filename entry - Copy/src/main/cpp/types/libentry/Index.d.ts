/**
 * The whole ArkTSâ†”C++ surface.
 *
 * `applyForecast` takes the Open-Meteo response body verbatim; C++ parses it, maps the WMO code
 * to a weather kind, aggregates the half-days, computes the sun times, and repaints. The shell
 * decides nothing â€” it only supplies what native code cannot reach on its own.
 */

/**
 * @param density   display density, so the ported vp dimensions become surface pixels
 * @param region    what @ohos.i18n reports; C++ picks each display unit from it
 * @param is24Hour  the device's clock preference, for the hourly column labels
 */
export const setEnvironment: (density: number, region: string, is24Hour: boolean) => void;

/**
 * Hands over context.resourceManager so native code can open rawfile â€” where every icon and
 * weather image lives in this build, rawfile being the only resource directory the NDK can read.
 */
export const setResourceManager: (resmgr: Object) => void;

/**
 * Where a tap wants to go. Every destination on the main screen is a screen this port has not
 * reached yet, so the shell shows its name â€” CLAUDE.md's placeholder rule.
 */
export const setNavigationHandler: (handler: (destination: string) => void) => void;

/** The app typeface, read out of rawfile. Returns false if the engine rejected it. */
export const registerFont: (font: ArrayBuffer) => boolean;

/**
 * @param body            the forecast response, verbatim
 * @param airQualityBody  the air-quality response, verbatim; empty when that leg failed, which
 *                        must not cost the forecast (OpenMeteoService.kt:327-330)
 */
export const applyForecast: (
  xcomponentId: string,
  locationIndex: number,
  locationCount: number,
  city: string,
  body: string,
  airQualityBody: string,
  latitude: number,
  longitude: number,
  darkMode: boolean
) => void;

/** Pull-to-refresh: the renderer detects the gesture, the shell owns the HTTP. */
export const setRefreshHandler: (handler: (locationIndex: number) => void) => void;

/**
 * The About screen's AlertDialogLink OK. The renderer draws the confirmation, so this only ever
 * receives a link the user has agreed to; only the shell can start a browsable ability.
 */
export const setLinkHandler: (handler: (url: string) => void) => void;

/** `BuildConfig.VERSION_NAME` for the About header — only the shell can read the bundle. */
export const setAppVersion: (xcomponentId: string, versionName: string) => void;
/** Clears the spinner after a FAILED refresh; a successful one clears it via applyForecast. */
export const endRefresh: (xcomponentId: string) => void;

/** The CAP document URLs listed in FMI's RSS index. */
export const alertFeedLinks: (rss: string) => string[];
/** CAP documents joined by a form feed â€” one string, because each is ~650 KB. */
export const applyAlerts: (
  xcomponentId: string,
  locationIndex: number,
  region: string,
  capDocuments: string
) => void;

/**
 * The system bar insets in SURFACE PIXELS. The window is laid out full screen so the sky reaches
 * behind the status bar; the toolbar still starts below it, as Android's AppBar does from
 * `doOnApplyWindowInsets`. Call again on rotation â€” the insets are not the same in landscape.
 */
export const setInsets: (xcomponentId: string, topPx: number, bottomPx: number) => void;

/**
 * How far the soft keyboard reaches up the window, in SURFACE PIXELS; 0 when it is down.
 *
 * Only screen 2b uses it. Android resizes its window for the IME, so SearchActivity's bottom bar
 * rides above the keyboard; a surface that does not resize would leave it underneath.
 */
export const setKeyboardInset: (xcomponentId: string, heightPx: number) => void;

/**
 * Called with the sky's lightness whenever it changes â€” a refresh that flips day to night, or a
 * swipe to a location on the other side of it. The shell restyles the status bar's foreground,
 * which is MainActivity.updateSystemBarStyle's job on Android and only ArkTS can do here.
 */
export const setThemeHandler: (handler: (lightBackground: number) => void) => void;

/** Called with the new comma-separated card order whenever a drag commits. */
export const setOrderHandler: (handler: (order: string) => void) => void;
/** Puts a saved card order back before the first refresh builds the cards. */
export const restoreCardOrder: (xcomponentId: string, order: string) => void;

/* ------------------------------------------------------------ screen 2b: add a location ------- */

/**
 * The add-location screen's keyboard state: 0 closed, 1 open, 2 covered by one of its own dialogs,
 * 3 clear of it again.
 *
 * The shell drives the input method from this signal â€” the keyboard is the one thing on that screen
 * only ArkTS can produce. The field's glyphs, placeholder and caret are drawn natively
 * (view/LocationSearchScreen.cpp), so there is nothing here to lay out. 2 and 3 HIDE and SHOW; they
 * never detach, because re-attaching would throw the typed query away.
 */
export const setSearchUiHandler: (handler: (state: number) => void) => void;

/**
 * Called with the chosen hit as `id|place|region|admin2|countryCode|latitude|longitude`.
 *
 * C++ parsed the geocoding body and applied Location.kt's getPlace rule, so the fields arrive
 * ready; the shell still never reads a response. It does own the saved list, so it is the one that
 * refuses a duplicate â€” and calls closeSearch() only when the add really happened.
 */
export const setPickHandler: (handler: (encoded: string) => void) => void;

/** The field's contents after every edit, from the shell's IME listener. */
export const setSearchQuery: (xcomponentId: string, text: string) => void;

/** The geocoding response, verbatim, and the term it answers (for the empty-state message). */
export const applySearchResults: (
  xcomponentId: string,
  body: string,
  searched: string
) => void;

/** The linear progress bar: a request is out. Cleared by applySearchResults. */
export const setSearchLoading: (xcomponentId: string, loading: boolean) => void;

/** Finishes screen 2b onto the location list. */
export const closeSearch: (xcomponentId: string) => void;

/**
 * The saved formattedIds, newline separated â€” what the weather-sources dialog's duplicate warning
 * is tested against. Pushed because only the shell holds the list, and the warning decides a pixel.
 */
export const setSavedLocationIds: (xcomponentId: string, ids: string) => void;

/**
 * MainActivityViewModel._locationListLoading — a list WRITE is in flight, which is what the
 * progress indicator above the rows shows. Only the shell knows; it owns the list and the file.
 */
export const setListLoading: (xcomponentId: string, loading: boolean) => void;

/**
 * The ability's colour mode, on its own channel.
 *
 * It already crosses bundled with each forecast, because that is what picks the weather background
 * set. Screen 3 needs it WITHOUT data: SettingsActivity takes BreezyWeatherTheme's default
 * `isSystemInDarkTheme()` rather than the selected location's daylight, so the settings screen
 * themes from the system even before a forecast has landed.
 */
export const setSystemDarkMode: (xcomponentId: string, dark: boolean) => void;

/**
 * The two Appearance settings — SettingsManager's `dark_mode` ("system" | "light" | "dark") and
 * `day_night_mode_locations`. The shell owns the preferences file; C++ applies
 * ThemeManager.isLightTheme with them and re-derives every scene's theme.
 */
export const setDarkModeSettings: (
  xcomponentId: string,
  darkMode: string,
  dayNightForLocations: boolean
) => void;

/**
 * SettingsManager's `alternate_calendar` — "none", "" for the regional default, or a Unicode
 * calendar key. The locale travels with it because it is what decides the picker's
 * "Regional preference (…)" row (CalendarHelper.kt:118-143), and C++ has no locale of its own.
 */
export const setAlternateCalendar: (
  xcomponentId: string,
  value: string,
  language: string,
  country: string
) => void;

/*
 * The relational store — the saved-location list and the response cache.
 *
 * The C surface of the same engine ArkUIApp reaches through @ohos.data.relationalStore, so both
 * ports sit on one database rather than one database and a pile of flat files. See
 * cpp/data/Database.h on why that matters for the memory comparison.
 */
export const dbOpen: (filesDir: string, bundleName: string) => boolean;
export const dbReadLocations: () => string;
/** Newline-joined and index-aligned; written whole, in one transaction. */
export const dbWriteLocations: (formattedIds: string, payloads: string) => void;
export const dbReadWeather: (formattedId: string, kind: string) => string;
export const dbWriteWeather: (
  formattedId: string,
  kind: string,
  payload: string,
  updatedAtMs: number
) => void;
export const dbWeatherUpdatedAt: (formattedId: string, kind: string) => number;
export const dbDeleteWeather: (formattedId: string) => void;
export const dbReadSetting: (key: string) => string;
export const dbWriteSetting: (key: string, value: string) => void;

/** Screen 3e's seven values — External modules (ModulesSettingsScreen.kt). */
export const setModuleSettings: (
  xcomponentId: string,
  weekIconMode: string,
  monochromeIcons: boolean,
  notificationWidget: boolean,
  notificationPersistent: boolean,
  notificationStyle: string,
  notificationTempIcon: boolean,
  notificationFeelsLike: boolean
) => void;

/**
 * Screen 3b's five unit overrides — SettingsManager's `temperature_unit` … `pressure_unit`, each a
 * unit id ("c", "mph", "inhg", …) or "auto" for the region's own default.
 *
 * All five in one call: every formatter reads them together, so a partial push would draw a page in
 * a mix of old and new units for a frame.
 */
export const setUnitSettings: (
  xcomponentId: string,
  temperature: string,
  precipitation: string,
  speed: string,
  distance: string,
  pressure: string
) => void;

/**
 * A setting chosen on screen 3a, as `<key>|<value>` — key is "dark_mode" or "day_night".
 *
 * C++ owns no files, so it hands the choice over to be stored; the shell writes it and pushes the
 * values straight back through setDarkModeSettings, which is what re-themes every scene.
 */
export const setSettingsEditHandler: (handler: (command: string) => void) => void;

/**
 * Location.applyDefaultPreset, as the eight per-feature source ids comma separated.
 *
 * The source catalogue â€” ~75 sources and their per-location predicates â€” lives only in C++, so the
 * shell asks for the preset rather than computing it. Used for the seeded locations; a location
 * added through the search screen gets its ids from the weather-sources dialog instead.
 */
/**
 * The list itself changed, as `verb|payload`:
 *   `delete|<index>`         a row swiped right past the threshold
 *   `move|<from>|<to>`       a drag released on a different slot
 *   `sources|<index>|<ids>`  the weather-sources dialog saved on an existing row
 *
 * One callback rather than three: all three are the same thing here â€” a write to the location list,
 * then a persist. The renderer owns only what is drawn.
 */
export const setListEditHandler: (handler: (command: string) => void) => void;

/**
 * One line per location, tab separated:
 * `formattedId, countryCode, region, admin2, latitude, longitude, sources`.
 *
 * The scenes carry what a WEATHER page draws; the weather-sources dialog a left swipe opens needs
 * where the location is and which sources it is on, and only the shell has those.
 */
export const setLocationDetails: (xcomponentId: string, details: string) => void;

/**
 * A list change the SHELL originated, as `insert|<index>` â€” which is only ever the Undo of a
 * delete. A delete and a move start in the renderer and are applied there, scenes included.
 */
export const listChanged: (xcomponentId: string, command: string) => void;

export const defaultSources: (
  countryCode: string,
  region: string,
  admin2: string,
  latitude: number,
  longitude: number,
  isCurrentPosition: boolean
) => string;

/**
 * The system back gesture. Returns true when an overlay screen consumed it.
 *
 * ManagementFragment and SearchActivity both close on back, and neither ever leaves the app.
 */
export const onBackPressed: (xcomponentId: string) => boolean;

/**
 * Screen 3c's six settings — SettingsManager's `main_screen_background`,
 * `trend_horizontal_line_switch`, `background_animation_mode` ("system" | "enabled" | "disabled"),
 * `gravity_sensor_switch`, `list_animation_switch` and `item_animation_switch`.
 *
 * All six in one call, for the reason setUnitSettings takes all five.
 */
export const setMainScreenSettings: (
  xcomponentId: string,
  background: boolean,
  trendLines: boolean,
  animationMode: string,
  gravity: boolean,
  fadeIn: boolean,
  elements: boolean
) => void;

/**
 * Screen 3d's six settings — SettingsManager's `alert_notification_switch`,
 * `precipitation_notification_switch`, `timing_forecast_switch_today`,
 * `timing_forecast_switch_tomorrow`, `forecast_time_today` and `forecast_time_tomorrow` (both
 * "HH:mm") — plus `refresh_rate`, which the screen only READS to decide whether the two General
 * rows are live (NotificationsSettingsScreen.kt:135-145).
 *
 * All in one call, for the reason setUnitSettings takes all five.
 */
export const setNotificationSettings: (
  xcomponentId: string,
  /** `app_update_check_switch` — the App updates section, which the reference build DOES show. */
  appUpdates: boolean,
  alert: boolean,
  precipitation: boolean,
  today: boolean,
  tomorrow: boolean,
  todayTime: string,
  tomorrowTime: string,
  refreshRate: string,
  /** `Context.is12Hour` (DateExtensions.kt:42-43) — display only; storage is always "HH:mm". */
  twelveHour: boolean
) => void;

/**
 * Screen 3c's two stored values — `SettingsManager.updateInterval` and
 * `ignoreUpdatesWhenBatteryLow`, whose default is TRUE.
 *
 * The rate also reaches screen 3d through setNotificationSettings, which greys its rows out when
 * it is "never"; both are pushed after a change so the two screens cannot disagree.
 */
export const setBackgroundUpdatesSettings: (
  xcomponentId: string,
  refreshRate: string,
  skipWhenBatteryLow: boolean
) => void;

/**
 * Screen 3d's TimeInput — the time picker's keyboard button (TimePickerPreference.kt:213-217).
 *
 * `1` asks for a NUMERIC edit session for whichever field is focused, `0` closes it. Separate from
 * setSearchUiHandler so the search field's own session is untouched.
 */
export const setTimeEntryHandler: (handler: (state: number) => void) => void;

/**
 * What the shell has collected so far for whichever field is open — screen 3d's time fields or
 * screen 3e's API key. One channel, routed by the current screen.
 */
export const setTimeEntryText: (xcomponentId: string, text: string) => void;

/** Screen 3e's two stored values — `location_service` and the Baidu API key. */
export const setLocationSettings: (
  xcomponentId: string,
  locationService: string,
  baiduApiKey: string
) => void;

/** Screen 3e's live grant state. Only ArkTS can read the access token. */
export const setLocationPermissions: (
  xcomponentId: string,
  coarseGranted: boolean,
  preciseGranted: boolean
) => void;

/**
 * Screen 3f's stored values: the default source for new locations, and every source preference that
 * has one as `<storageKey>\t<value>` lines. Keys with no value are simply absent.
 */
export const setSourceSettings: (
  xcomponentId: string,
  defaultForecastSource: string,
  prefs: string
) => void;

/** Screens 3h/3i/3j's three ordered lists, as the "&"-joined id strings Android stores. */
export const setDisplayLists: (
  xcomponentId: string,
  cardDisplay: string,
  dailyTrendDisplay: string,
  hourlyTrendDisplay: string
) => void;

/**
 * Where a permission request goes. The shell prompts and pushes the new state back through
 * setLocationPermissions — C++ never decides the answer.
 */
export const setPermissionHandler: (handler: (permission: string) => void) => void;
