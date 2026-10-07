/*
 * The entire ArkTS side of the NDK port.
 *
 * One XComponent, whose surface C++ draws every pixel of, and one Open-Meteo request, whose body
 * goes to C++ unread. There is deliberately no ArkUI layout here: the point of this build is that
 * the screen is drawn by native code, so anything ArkUI renders would be weight the ArkTS build is
 * being measured against.
 *
 * The counterpart in ArkUIApp is view/main/MainScreen.ets, which is a full declarative tree.
 */

import {
  abilityAccessCtrl,
  bundleManager,
  common,
  ConfigurationConstant,
  PermissionRequestResult,
  Permissions
} from '@kit.AbilityKit';
import { display } from '@kit.ArkUI';
import { hilog } from '@kit.PerformanceAnalysisKit';
import { i18n } from '@kit.LocalizationKit';
import { promptAction } from '@kit.ArkUI';
import native from 'libentry.so';
import {
  buildLocations,
  coveredByFmi,
  decodeLocationList,
  decodePickedLocation,
  encodeLocationList,
  locationDetails,
  locationIds,
  ShellLocation,
  withPosition,
  withAddress,
  withPreferences,
  withSources
} from '../shell/Locations';
import {
  ensureLocationPermission,
  distanceMetres,
  PositionAddress,
  PositionError,
  PositionResult,
  requestCurrentPosition,
  REVERSE_GEOCODING_DISTANCE_LIMIT,
  reverseGeocode
} from '../shell/CurrentPosition';
import { applyStatusBarStyle, onInsetsChanged } from '../shell/SystemBars';
import {
  fetchAirQualityBody,
  fetchAlertFeed,
  fetchCapDocument,
  fetchForecastBody,
  fetchLocationSearchBody
} from '../shell/OpenMeteoApi';
import {
  CachedForecast,
  cacheAgeMs,
  readCache,
  readCapDocument,
  readCardOrder,
  openStore,
  readLocationList,
  writeCache,
  writeCapDocument,
  writeCardOrder,
  writeLocationList
} from '../shell/WeatherCache';
import { TextEntry } from '../shell/TextEntry';
import {
  loadSettings,
  ShellSettings,
  storeAlternateCalendar,
  storeDarkMode,
  storeModuleSetting,
  storeDayNightForLocations,
  storeUnit,
  storeMainScreenSetting,
  storeNotificationSetting,
  storeLocationSetting,
  storeSourceSetting,
  storeDisplayList,
  storeBackgroundUpdatesSetting,
  sourcePrefsBlob,
  updateIntervalMs
} from '../shell/SettingsStore';

const DOMAIN: number = 0x0000;
const TAG: string = 'BreezyShell';

/** Must match the id C++ keys its Renderer by â€” see napi_init.cpp RendererFor. */
const SURFACE_ID: string = 'weather';

/**
 * Hands C++ the five unit overrides, whatever the preferences bag currently holds.
 *
 * Called once at startup and again after every choice made on screen 3b. Always all five, never
 * just the one that changed: the formatters read them together, so pushing one at a time could
 * draw a page in a mix of old and new units.
 */
/**
 * The alternate calendar, with the device's locale.
 *
 * The locale travels with the value because it is what names the picker's
 * "Regional preference (…)" row (CalendarHelper.kt:118-143), and C++ has no locale of its own.
 * Guarded: `getSystemRegion` throws on some builds rather than returning empty.
 */
function pushAlternateCalendar(): void {
  let language: string = 'en';
  let country: string = '';
  try {
    language = i18n.System.getSystemLanguage();
    country = i18n.System.getSystemRegion();
  } catch (error) {
    // keep the defaults
  }
  native.setAlternateCalendar(SURFACE_ID, ShellSettings.alternateCalendar, language, country);
}

/** Screen 3e's seven, pushed together for the reason screen 3c's six are. */
function pushModuleSettings(): void {
  native.setModuleSettings(SURFACE_ID, ShellSettings.weekIconMode, ShellSettings.monochromeIcons,
    ShellSettings.notificationWidget, ShellSettings.notificationPersistent,
    ShellSettings.notificationStyle, ShellSettings.notificationTempIcon,
    ShellSettings.notificationFeelsLike);
}

function pushUnits(): void {
  native.setUnitSettings(SURFACE_ID, ShellSettings.temperatureUnit,
    ShellSettings.precipitationUnit, ShellSettings.speedUnit, ShellSettings.distanceUnit,
    ShellSettings.pressureUnit);
}

/** The same for screen 3c's six. Pushed together for the same reason. */
function pushMainScreenSettings(): void {
  native.setMainScreenSettings(SURFACE_ID, ShellSettings.mainScreenBackground,
    ShellSettings.trendHorizontalLines, ShellSettings.backgroundAnimation,
    ShellSettings.gravitySensor, ShellSettings.cardsFadeIn, ShellSettings.elementAnimations);
}

/**
 * The same for screen 3d's six, plus `refresh_rate`, which that screen only reads to decide whether
 * its two General rows are live.
 */
function pushNotificationSettings(): void {
  native.setNotificationSettings(SURFACE_ID, ShellSettings.appUpdateCheck,
    ShellSettings.alertNotification,
    ShellSettings.precipitationNotification, ShellSettings.todayForecast,
    ShellSettings.tomorrowForecast, ShellSettings.todayForecastTime,
    ShellSettings.tomorrowForecastTime, ShellSettings.refreshRate,
    // Context.is12Hour (DateExtensions.kt:42-43). Display only — the stored value stays "HH:mm".
    !i18n.System.is24HourClock());
}

/** Screen 3c's two stored values. */
function pushBackgroundUpdatesSettings(): void {
  native.setBackgroundUpdatesSettings(SURFACE_ID, ShellSettings.refreshRate,
    ShellSettings.skipWhenBatteryLow);
}

/** Screen 3e's two stored values. */
function pushLocationSettings(): void {
  native.setLocationSettings(SURFACE_ID, ShellSettings.locationService,
    ShellSettings.baiduIpApiKey);
}

/**
 * Screen 3f's stored values — the default source, and every source preference that has one.
 *
 * The default source is not display-only: it decides which sources a location added from the search
 * screen starts on, so it is pushed at startup even before that screen is opened.
 */
function pushSourceSettings(): void {
  native.setSourceSettings(SURFACE_ID, ShellSettings.defaultForecastSource, sourcePrefsBlob());
}

/** Screens 3h/3i/3j's three ordered lists, which screen 3c also summarises. */
function pushDisplayLists(): void {
  native.setDisplayLists(SURFACE_ID, ShellSettings.cardDisplay, ShellSettings.dailyTrendDisplay,
    ShellSettings.hourlyTrendDisplay);
}

/**
 * Screen 3e's live grant state. Only ArkTS can read the access token, so C++ is told rather than
 * asking — the same split every other capability in this port uses.
 */
function pushLocationPermissions(): void {
  native.setLocationPermissions(SURFACE_ID, permissionGranted(PERM_COARSE),
    permissionGranted(PERM_PRECISE));
}

const PERM_COARSE: Permissions = 'ohos.permission.APPROXIMATELY_LOCATION';
const PERM_PRECISE: Permissions = 'ohos.permission.LOCATION';

/** PermissionStatus.Granted (LocationSettingsScreen.kt:189, :255). */
function permissionGranted(permission: Permissions): boolean {
  try {
    const info: bundleManager.BundleInfo = bundleManager.getBundleInfoForSelfSync(
      bundleManager.BundleFlag.GET_BUNDLE_INFO_WITH_APPLICATION);
    const manager: abilityAccessCtrl.AtManager = abilityAccessCtrl.createAtManager();
    return manager.checkAccessTokenSync(info.appInfo.accessTokenId, permission) ===
      abilityAccessCtrl.GrantStatus.PERMISSION_GRANTED;
  } catch (error) {
    hilog.error(DOMAIN, TAG, 'could not read a permission: %{public}s', (error as Error).message);
    return false;
  }
}

/**
 * Screen 3e's two permission rows. Android's launcher is `rememberLauncherForActivityResult`
 * (LocationSettingsScreen.kt:196-204); the row is only tappable while the state is not Granted, and
 * the answer refreshes the row rather than being acted on.
 */
async function requestLocationPermission(
  context: common.UIAbilityContext,
  permission: Permissions
): Promise<void> {
  const manager: abilityAccessCtrl.AtManager = abilityAccessCtrl.createAtManager();
  try {
    const result: PermissionRequestResult =
      await manager.requestPermissionsFromUser(context, [permission]);
    hilog.info(DOMAIN, TAG, '%{public}s answered %{public}d', permission, result.authResults[0]);
  } catch (error) {
    hilog.error(DOMAIN, TAG, 'permission request failed: %{public}s', (error as Error).message);
  }
  pushLocationPermissions();
}

/** The same typeface ArkUIApp bundles, so both ports render identical glyphs. */
/*
 * Despite the name this is the 2.3 MB VARIABLE Roboto from the reference device's /system/fonts,
 * carrying a `wght` axis. It replaces `RobotoStatic-Regular.ttf`, a Regular-only cut that was
 * bundled to keep the HAP small and had no bold face at all â€” so every weight 500 and weight 700
 * run fell back to 400, most visibly the header temperature, which
 * container_main_header.xml:82-83 asks for at `textFontWeight="700"`. Android renders its bold
 * from this same file. Both ports carry it, so the comparison between them is unaffected.
 */
const FONT_FILES: string[] = ['Roboto-Regular.ttf', 'Roboto-Medium.ttf', 'Roboto-Bold.ttf'];

@Entry
@ComponentV2
struct Index {
  private locations: ShellLocation[] = buildLocations();
  private filesDir: string = '';
  /**
   * The keyboard for screen 2b. Attached only while that screen is up â€” see shell/TextEntry.ets for
   * why this is an input-method session and not an ArkUI TextInput.
   */
  /**
   * Screen 3d's TimeInput. A SECOND session rather than a shared one: the search field's is
   * TEXT + Search and may be open behind a dialog, and re-configuring it would throw its query
   * away. Numeric, and its Done key just closes the keyboard — Confirm still lives on the dialog.
   */
  private timeEntry: TextEntry = new TextEntry(
    (text: string) => {
      native.setTimeEntryText(SURFACE_ID, text);
    },
    (text: string) => {
      this.timeEntry.setKeyboardVisible(false);
    },
    true
  );
  /** Screen 3e's API key — the TEXT variant of the same channel. */
  private valueEntry: TextEntry = new TextEntry(
    (text: string) => {
      native.setTimeEntryText(SURFACE_ID, text);
    },
    (text: string) => {
      this.valueEntry.setKeyboardVisible(false);
    },
    false
  );
  private textEntry: TextEntry = new TextEntry(
    (text: string) => {
      native.setSearchQuery(SURFACE_ID, text);
    },
    (text: string) => {
      this.runSearch(text);
    }
  );
  /**
   * Guards against an out-of-order reply, as ArkUIApp's `requestSeq` does: a slow first search must
   * not overwrite the results of a later one. `mRepository.cancel()` (SearchViewModel.kt:60) is how
   * the Kotlin does it, by disposing the RxJava subscription; @ohos.net.http has no cancel, so the
   * reply is matched against the request that is current when it lands.
   */
  private requestSeq: number = 0;
  /** The value C++ wants the next TEXT session pre-filled with — see the `__ime_seed__` branch. */
  private pendingSeed: string = '';

  /**
   * The surface exists by the time onLoad fires, so this is the earliest point at which C++ has
   * somewhere to draw. Everything native needs but cannot reach â€” density, region, the font â€” is
   * handed over before the first paint.
   */
  private async start(): Promise<void> {
    const context = this.getUIContext().getHostContext() as common.UIAbilityContext;
    native.setEnvironment(
      display.getDefaultDisplaySync().densityPixels,
      this.region(),
      this.is24Hour()
    );
    // Every icon and weather image is read from rawfile by C++, not resolved by ArkUI.
    native.setResourceManager(context.resourceManager);
    /*
     * The window is laid out full screen (EntryAbility â†’ SystemBars.enableEdgeToEdge), so the
     * surface reaches under the status and navigation bars and the renderer needs to know how far
     * in to lay the page out. Delivered immediately and again on every rotation.
     */
    onInsetsChanged((topPx: number, bottomPx: number, keyboardPx: number) => {
      native.setInsets(SURFACE_ID, topPx, bottomPx);
      // Screen 2b's bottom bar rides above the keyboard, as Android's does by resizing its window.
      native.setKeyboardInset(SURFACE_ID, keyboardPx);
    });
    // MainActivity.updateSystemBarStyle â€” only ArkTS can restyle the bar, so C++ reports the sky.
    native.setThemeHandler((lightBackground: number) => {
      applyStatusBarStyle(lightBackground !== 0);
    });
    /*
     * The ability's colour mode, which screen 3 themes from.
     *
     * It also crosses bundled with every forecast (that is what picks the weather background set),
     * but the settings screen has to theme itself with no forecast in hand: SettingsActivity takes
     * BreezyWeatherTheme's default `isSystemInDarkTheme()` rather than the location's daylight.
     */
    native.setSystemDarkMode(SURFACE_ID, this.isDarkMode());
    /*
     * The stored Appearance settings, before the first forecast.
     *
     * The theme rule needs all three inputs â€” the dark-mode setting, day/night-for-locations and
     * the system mode â€” and C++ derives every scene's theme from them (SetScene), so publishing
     * them late would theme the first frame from the defaults and then flip it.
     */
    await loadSettings(context);
    native.setDarkModeSettings(SURFACE_ID, ShellSettings.darkMode,
      ShellSettings.dayNightForLocations);
    pushAlternateCalendar();
    pushModuleSettings();
    pushUnits();
    pushMainScreenSettings();
    pushNotificationSettings();
    pushBackgroundUpdatesSettings();
    pushLocationSettings();
    pushSourceSettings();
    pushDisplayLists();
    pushLocationPermissions();
    native.setPermissionHandler((permission: string) => {
      requestLocationPermission(context, permission as Permissions);
    });
    /*
     * A choice made on screen 3a comes back here to be written, then goes straight out again.
     *
     * The round trip is deliberate: C++ does not apply the change itself, so the stored value and
     * the drawn one cannot diverge â€” whatever the file ends up holding is what gets pushed back.
     */
    native.setSettingsEditHandler((command: string) => {
      const parts: string[] = command.split('|');
      if (parts.length < 2) {
        return;
      }
      /*
       * A reserved key, not a setting: C++ is pre-filling the edit buffer with a value it already
       * holds, so the first keystroke in an API-key or instance field appends rather than replaces.
       * `attach()` clears the buffer, which is right for the time fields and wrong for these.
       */
      if (parts[0] === '__ime_seed__') {
        /*
         * PARKED, not applied here. C++ sends the seed and then asks for the session, and
         * `attach()` clears the buffer — seeding first was simply overwritten a moment later
         * (measured: a field holding "a", typing "b", stored "b" instead of "ab"). It is applied
         * once the attach has resolved; see the `state === 2` branch below.
         */
        this.pendingSeed = command.substring(command.indexOf('|') + 1);
        return;
      }
      if (parts[0] === 'dark_mode') {
        storeDarkMode(parts[1]).then(() => {
          native.setDarkModeSettings(SURFACE_ID, ShellSettings.darkMode,
            ShellSettings.dayNightForLocations);
        });
      } else if (parts[0] === 'alternate_calendar') {
        // The value is a Unicode calendar key, "none", or "" for the regional default. An empty
        // second field is meaningful here, which is why this splits on the FIRST separator rather
        // than trusting parts[1] to be non-empty.
        storeAlternateCalendar(command.substring(command.indexOf('|') + 1)).then(() => {
          pushAlternateCalendar();
        });
      } else if (parts[0] === 'week_icon_mode' || parts[0].startsWith('notification_widget') ||
        parts[0] === 'widget_monochrome_icons') {
        storeModuleSetting(parts[0], parts[1]).then((stored: boolean) => {
          if (stored) {
            pushModuleSettings();
          }
        });
      } else if (parts[0] === 'day_night') {
        // DarkModeLocation.kt:28-29 â€” the id `day_night` is the TRUE value; `dark` is false.
        storeDayNightForLocations(parts[1] === 'day_night').then(() => {
          native.setDarkModeSettings(SURFACE_ID, ShellSettings.darkMode,
            ShellSettings.dayNightForLocations);
        });
      } else {
        // Screen 3b sends the preferences key itself, so the five unit rows need no branch here â€”
        // `storeUnit` reports whether the key was one of them.
        storeUnit(parts[0], parts[1]).then((stored: boolean) => {
          if (stored) {
            pushUnits();
            return;
          }
          // Not a unit key, so it is one of screen 3c's six — or, failing that, one of 3d's.
          storeMainScreenSetting(parts[0], parts[1]).then((kept: boolean) => {
            if (kept) {
              pushMainScreenSettings();
              return;
            }
            storeBackgroundUpdatesSetting(parts[0], parts[1]).then((background: boolean) => {
              if (background) {
                // Both screens read the refresh rate, so both are pushed.
                pushBackgroundUpdatesSettings();
                pushNotificationSettings();
                return;
              }
            storeNotificationSetting(parts[0], parts[1]).then((noted: boolean) => {
              if (noted) {
                pushNotificationSettings();
                return;
              }
              // Screen 3e last. Its API key is free text, so take everything past the FIRST
              // separator rather than parts[1] — a key containing '|' would otherwise be truncated.
              const cut: number = command.indexOf('|');
              const free: string = command.substring(cut + 1);
              storeLocationSetting(parts[0], free).then((set: boolean) => {
                if (set) {
                  pushLocationSettings();
                  return;
                }
                // Screen 3f last. Its API keys and instance URLs are free text too, so the same
                // first-separator rule applies.
                storeSourceSetting(parts[0], free).then((kept: boolean) => {
                  if (kept) {
                    pushSourceSettings();
                    return;
                  }
                  // Screens 3h/3i/3j last — the three ordered display lists.
                  storeDisplayList(parts[0], free).then((listed: boolean) => {
                    if (listed) {
                      pushDisplayLists();
                    }
                  });
                });
              });
            });
            });
          });
        });
      }
    });
    // Every tap target on this screen leads somewhere not yet ported, so each one announces its
    // destination rather than doing nothing â€” CLAUDE.md's placeholder rule.
    native.setNavigationHandler((destination: string) => {
      promptAction.showToast({ message: destination, duration: 1500 });
    });
    // Pull-to-refresh: the renderer detects the gesture, but the shell owns the HTTP.
    native.setRefreshHandler((locationIndex: number) => {
      this.refresh(locationIndex);
    });
    /*
     * The About screen's AlertDialogLink OK. The renderer draws the confirmation itself, so this
     * only ever receives a link the user has already agreed to; starting a browsable ability is the
     * one part of it the shell has to own.
     */
    native.setLinkHandler((url: string) => {
      const context = getContext(this) as common.UIAbilityContext;
      /*
       * The icon preview's `action_about` — `startApplicationDetailsActivity`
       * (PreviewIconActivity.kt:121-129). It is not a URL, so it takes the settings ability rather
       * than a browsable one; it shares this channel because ability starts all belong to the shell.
       */
      if (url === 'app-details:') {
        context.startAbility({
          bundleName: 'com.huawei.hmos.settings',
          abilityName: 'com.huawei.hmos.settings.MainAbility',
          uri: 'application_info_entry',
          parameters: { pushParams: 'com.example.myapplication' }
        }).catch((error: Error) => {
          hilog.error(DOMAIN, TAG, 'could not open app details: %{public}s', error.message);
        });
        return;
      }
      context.startAbility({
        action: 'ohos.want.action.viewData',
        entities: ['entity.system.browsable'],
        uri: url
      }).catch((error: Error) => {
        hilog.error(DOMAIN, TAG, 'could not open %{public}s: %{public}s', url, error.message);
      });
    });
    // `BuildConfig.VERSION_NAME` for the About header — only the shell can read the bundle.
    try {
      const bundleInfo: bundleManager.BundleInfo =
        bundleManager.getBundleInfoForSelfSync(bundleManager.BundleFlag.GET_BUNDLE_INFO_DEFAULT);
      native.setAppVersion(SURFACE_ID, bundleInfo.versionName);
    } catch (error) {
      hilog.error(DOMAIN, TAG, 'could not read the bundle version: %{public}s',
        (error as Error).message);
    }
    this.loadFont();

    /*
     * Screen 2b. `open` crosses as 0/1, the same marshaller the theme signal uses.
     *
     * The keyboard is the only part of that screen the shell owns; its field, rows and FAB are
     * drawn natively, so nothing here has to lay anything out.
     */
    /*
     * Screen 3d's TimeInput. `1` opens a fresh numeric session for the focused field — fresh
     * because attach() clears the buffer, which is exactly what moving focus between the hour and
     * minute boxes needs.
     */
    native.setTimeEntryHandler((state: number) => {
      // 1 numeric (screen 3d's time fields), 2 text (screen 3e's API key), 0 close both.
      if (state === 1) {
        this.valueEntry.detach();
        this.timeEntry.detach().then(() => {
          this.timeEntry.attach();
        });
      } else if (state === 2) {
        this.timeEntry.detach();
        this.valueEntry.detach().then(() => {
          this.valueEntry.attach().then(() => {
            // Only now: attach() clears the buffer, so the stored value has to land after it.
            if (this.pendingSeed.length > 0) {
              this.valueEntry.seed(this.pendingSeed);
              this.pendingSeed = '';
            }
          });
        });
      } else {
        this.timeEntry.detach();
        this.valueEntry.detach();
      }
    });
    native.setSearchUiHandler((state: number) => {
      // 0 closed, 1 open, 2 covered by a dialog, 3 clear of it again.
      if (state === 1) {
        this.textEntry.attach();
      } else if (state === 0) {
        this.textEntry.detach();
      } else {
        this.textEntry.setKeyboardVisible(state === 3);
      }
    });
    native.setPickHandler((encoded: string) => {
      this.onLocationPicked(encoded);
    });
    // Screen 2's swipe-delete, drag-reorder and per-location source edit all land here.
    native.setListEditHandler((command: string) => {
      this.onListEdit(command);
    });

    this.filesDir = context.filesDir;
    /*
     * The store has to be open before ANY read below — the location list is the first of them.
     * A failure is not fatal: every read then returns "" and the seed list stands in, which is what
     * a missing file used to do.
     */
    openStore(this.filesDir, context.applicationInfo.name);
    // Whatever the user's list has become; the seed only stands in for an empty store.
    const stored: ShellLocation[] = decodeLocationList(readLocationList(this.filesDir));
    if (stored.length > 0) {
      this.locations = stored;
    } else {
      this.locations = this.presetSeeds(this.locations);
    }
    this.publishLocationIds();
    // The arrangement the user last dragged the cards into, restored before anything is drawn.
    native.setOrderHandler((order: string) => {
      writeCardOrder(this.filesDir, order);
    });
    const savedOrder: string = readCardOrder(this.filesDir);
    if (savedOrder.length > 0) {
      native.restoreCardOrder(SURFACE_ID, savedOrder);
    }
    this.loadCached();
    await this.refresh();
  }

  private region(): string {
    try {
      return i18n.System.getSystemRegion();
    } catch (error) {
      hilog.warn(DOMAIN, TAG, 'no system region, defaulting to metric');
      return '';
    }
  }

  private is24Hour(): boolean {
    try {
      return i18n.System.is24HourClock();
    } catch (error) {
      return true;
    }
  }

  /**
   * Registers all three faces into the one family C++ draws with.
   *
   * THREE static faces, not one variable font, and it must stay in step with ArkUIApp — both ports
   * bundle the same typeface so the comparison between them is unaffected, and that invariant is
   * the reason this changed here at the same time. See ArkUIApp's `pages/Index.ets` for the
   * measurement: asking a VARIABLE font for a non-default weight makes the engine instance it per
   * text node, and these three are instanced from that same variable font at wght 400/500/700, so
   * the glyphs are identical and only the timing changes.
   *
   * `OH_Drawing_RegisterFontBuffer` adds a face to the collection under the family it is given, so
   * three calls with the same family is how a weight becomes a face lookup here too.
   */
  private loadFont(): void {
    for (const file of FONT_FILES) {
      try {
        const context = this.getUIContext().getHostContext() as common.UIAbilityContext;
        const bytes: Uint8Array = context.resourceManager.getRawFileContentSync(file);
        // Hand over the exact bytes, not the view's backing store: getRawFileContentSync may return
        // a Uint8Array over a larger buffer, and the engine would then read past the font.
        const registered: boolean = native.registerFont(
          bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength) as ArrayBuffer
        );
        if (!registered) {
          hilog.warn(DOMAIN, TAG, 'font rejected: %{public}s', file);
        }
      } catch (error) {
        hilog.warn(DOMAIN, TAG, 'could not read %{public}s', file);
      }
    }
  }

  /** Hands one location's bodies to C++, cached or fresh. */
  private apply(index: number, forecast: string, airQuality: string): void {
    const location: ShellLocation = this.locations[index];
    native.applyForecast(
      SURFACE_ID,
      index,
      this.locations.length,
      location.city,
      forecast,
      airQuality,
      location.latitude,
      location.longitude,
      this.isDarkMode()
    );
  }

  /**
   * The cached bodies, drawn before any request goes out â€” ArkUIApp's loadCached()
   * (MainScreen.ets:485) does the same, so a cold start is never a blank page.
   */
  private loadCached(): void {
    for (let i = 0; i < this.locations.length; i++) {
      const cached: CachedForecast = readCache(this.filesDir, this.locations[i].formattedId);
      if (cached.forecast.length > 0) {
        this.apply(i, cached.forecast, cached.airQuality);
      }
    }
  }

  /**
   * FMI's warnings for one location: fetch the RSS index, then each CAP document it names.
   *
   * The documents are ~650 KB each and roughly a dozen are in force, so they are cached on disk
   * by URL â€” a CAP URL carries an issue timestamp and a content hash, so a given document is
   * immutable and a refresh re-downloads only what is genuinely new.
   */
  private async refreshAlerts(index: number): Promise<void> {
    const location: ShellLocation = this.locations[index];
    if (!coveredByFmi(location)) {
      return;  // FMI covers Finland only; elsewhere there is nothing to ask for.
    }
    try {
      const links: string[] = native.alertFeedLinks(await fetchAlertFeed());
      const documents: string[] = [];
      for (let i = 0; i < links.length; i++) {
        let document: string = readCapDocument(this.filesDir, links[i]);
        if (document.length === 0) {
          document = await fetchCapDocument(links[i]);
          writeCapDocument(this.filesDir, links[i], document);
        }
        documents.push(document);
      }
      // Joined by a form feed: one string across the boundary rather than a dozen, because at
      // ~650 KB each the peak matters more than the parsing convenience.
      native.applyAlerts(SURFACE_ID, index, location.region, documents.join('\f'));
      hilog.info(DOMAIN, TAG, 'alerts: %{public}d documents for %{public}s', documents.length,
        location.city);
    } catch (error) {
      // A failed alert leg must not disturb the forecast already on screen â€” the same rule
      // OpenMeteoService.kt:327-330 applies to a failed feature.
      hilog.warn(DOMAIN, TAG, 'alert refresh failed for %{public}s', location.city);
    }
  }

  /**
   * Refreshes every saved location, or just one when the pull-to-refresh asks for it.
   *
   * A location whose cached forecast is still VALID is skipped unless the user pulled:
   * `checkToUpdateCurrentLocation` (MainActivityViewModel.kt:279-283) returns at once when
   * `Weather.isValid(updateInterval.validity)` — the refresh time within the background update
   * interval, 1:30 by default. Only the pull path (`triggeredByUser`, MainActivity.kt:733) goes
   * straight to the fetch.
   *
   * Without this the shell fetched all N locations in sequence on every cold start, which is N
   * round trips before the first one is even on screen. ArkUIApp applies the same rule in
   * `MainViewModel.refresh`.
   */
  private async refresh(only: number = -1): Promise<void> {
    const validityMs: number = updateIntervalMs(ShellSettings.refreshRate);
    for (let i = 0; i < this.locations.length; i++) {
      if (only >= 0 && i !== only) {
        continue;
      }
      if (only < 0 && validityMs >= 0) {
        const age: number = cacheAgeMs(this.filesDir, this.locations[i].formattedId);
        if (age >= 0 && age < validityMs) {
          continue;
        }
      } else if (only < 0 && validityMs < 0) {
        // INTERVAL_NEVER: `pollingIntervalHours == null` makes every stored forecast valid.
        if (cacheAgeMs(this.filesDir, this.locations[i].formattedId) >= 0) {
          continue;
        }
      }
      await this.refreshOne(i);
    }
  }

  private async refreshOne(index: number): Promise<void> {
    /*
     * STEP 1 first, for the current position (RefreshHelper.kt:191-243).
     *
     * A current-position entry is saved with NO coordinates, so the fix has to land before the
     * forecast request goes out or it would ask Open-Meteo about 0Â°N 0Â°E. `updateCurrentPosition`
     * returns false when the fix fails, and the refresh stops there rather than fetching nonsense.
     */
    if (this.locations[index].isCurrentPosition) {
      const located: boolean = await this.updateCurrentPosition(index);
      if (!located) {
        native.endRefresh(SURFACE_ID);
        return;
      }
    } else {
      await this.geocodeSavedLocation(index);
    }
    const location: ShellLocation = this.locations[index];
    try {
      // The two calls hit different hosts and neither depends on the other, so they go out
      // together â€” OpenMeteoService.kt:180-330 zips the two Observables rather than chaining
      // them, so Android pays one round trip, not two.
      //
      // The air-quality leg is caught separately: a failure there must not reject the pair and
      // lose the forecast with it (kt:327-330 records a failed feature and keeps going).
      // The model selection is per LOCATION (OpenMeteoService.kt:245), so it rides the request
      // rather than being baked into the API module.
      const forecast: Promise<string> =
        fetchForecastBody(location.latitude, location.longitude, location.weatherModels);
      const airQuality: Promise<string> =
        fetchAirQualityBody(location.latitude, location.longitude).catch(() => '');

      const bodies: string[] = await Promise.all([forecast, airQuality]);
      this.apply(index, bodies[0], bodies[1]);
      writeCache(this.filesDir, location.formattedId, bodies[0], bodies[1]);
      hilog.info(DOMAIN, TAG, '%{public}s: %{public}d bytes', location.city, bodies[0].length);
      // Alerts are a second source and a second round trip, so they land after the forecast
      // rather than holding it up.
      this.refreshAlerts(index);
    } catch (error) {
      // RefreshHelper surfaces the failure rather than leaving a blank screen; the same applies
      // here, and all the more so because a blank screen and a broken renderer look identical.
      const failure = error as Error;
      const message: string =
        failure !== null && failure.message !== undefined ? failure.message : 'Weather refresh failed';
      hilog.error(DOMAIN, TAG, 'refresh failed: %{public}s', message);
      promptAction.showToast({ message: message, duration: 3000 });
      // Without this a failed pull-to-refresh would leave the spinner parked forever â€” a
      // successful refresh clears it through applyForecast, a failed one has to say so.
      native.endRefresh(SURFACE_ID);
    }
  }

  /**
   * RefreshHelper.updateCurrentCoordinates + getLocation STEP 1/STEP 2, for one location.
   *
   * The fix and the reverse geocode are the shell's because `geoLocationManager` is an ArkTS kit
   * with no NDK counterpart. What crosses to C++ is only the resulting fields, through the same
   * `locationDetails` line every other row uses.
   *
   * On a coordinate CHANGE the Kotlin clears the address it had cached (kt:215-233) â€” "Don't keep
   * old data as the user can have changed position" â€” which is what makes the geocode below
   * authoritative rather than additive.
   */
  /**
   * RefreshHelper STEP 2 for a location that is NOT the current position (kt:242-294).
   *
   * The step is gated on `needsGeocodeRefresh` ALONE. The doc comment above `getLocation` says
   * "Reverse geocoding (if current location) / On non-current location, just returns the location",
   * and that comment is what both ports were built from — but the CODE at kt:243 tests only the
   * flag, and STEP 1 is the part that is current-position-only. So Android reverse-geocodes a SAVED
   * location too, which is the only way any location gets a `district`: Open-Meteo's geocoding
   * never fills one, so a searched or seeded city has none until this runs.
   *
   * ArkUIApp does the same in `MainViewModel.geocodeSavedLocation`; the two are kept identical.
   */
  private async geocodeSavedLocation(index: number): Promise<void> {
    const location: ShellLocation = this.locations[index];
    if (!location.needsGeocodeRefresh) {
      return;
    }
    const key: string = location.latitude.toString() + ',' + location.longitude.toString();
    // Same non-retry rule as the current position's: the platform geocoder is a service extension
    // and a failed attempt is not free.
    if (this.geocodeFailedFor === key) {
      return;
    }
    const address: PositionAddress = await reverseGeocode(location.latitude, location.longitude);
    if (address.city.length === 0) {
      // kt:295-305's catch — the location is kept and the failure recorded, not surfaced.
      this.geocodeFailedFor = key;
      return;
    }
    /*
     * kt:250-266 — a hit further than REVERSE_GEOCODING_DISTANCE_LIMIT (50 km, kt:1478) from the
     * requested point is REJECTED and the old address kept. It cannot fire with this port's
     * geocoder, which answers about the point it was given rather than snapping to a nearest
     * station; ported anyway, and skipped when the platform returns no coordinates to compare.
     */
    if (address.latitude !== 0 && address.longitude !== 0 &&
      distanceMetres(location.latitude, location.longitude, address.latitude, address.longitude) >
        REVERSE_GEOCODING_DISTANCE_LIMIT) {
      hilog.warn(DOMAIN, TAG, 'reverse geocoding hit too far from the requested point');
      return;
    }
    this.locations[index] = withAddress(location, address.city, address.district, address.admin1,
      address.countryCode);
    writeLocationList(this.filesDir, encodeLocationList(this.locations));
    this.publishLocationIds();
  }

  /** The coordinates whose reverse geocode already failed this session. */
  private geocodeFailedFor: string = '';

  private async updateCurrentPosition(index: number): Promise<boolean> {
    const fix: PositionResult = await requestCurrentPosition();
    if (fix.error !== PositionError.NONE) {
      const message: string = fix.error === PositionError.PERMISSION_MISSING
        ? 'Location permission is required'
        : (fix.error === PositionError.LOCATION_OFF
          ? 'Location access is off'
          : 'Could not determine your position');
      promptAction.showToast({ message: message, duration: 3000 });
      return false;
    }
    const address: PositionAddress = await reverseGeocode(fix.latitude, fix.longitude);
    this.locations[index] = withPosition(this.locations[index], fix.latitude, fix.longitude,
      address.city, address.district, address.admin1, address.countryCode);
    writeLocationList(this.filesDir, encodeLocationList(this.locations));
    // The row's title, region and country all live on the details line, so C++ only learns the
    // position resolved when this goes across.
    this.publishLocationIds();
    return true;
  }

  /* ---------------------------------------------------------- screen 2b: add a location ------- */

  /**
   * The seeds, run through Location.applyDefaultPreset.
   *
   * A seed stands in for a location the user added, and Android presets every Location it creates â€”
   * so a seeded Helsinki carries FMI for forecast, current, air quality, alerts and normals rather
   * than eight blanks. The rule lives in C++ with the rest of the catalogue; the shell only stores
   * what comes back.
   */
  private presetSeeds(seeds: ShellLocation[]): ShellLocation[] {
    const preset: ShellLocation[] = [];
    for (let i = 0; i < seeds.length; i++) {
      const seed: ShellLocation = seeds[i];
      const sources: string = native.defaultSources(
        seed.countryCode, seed.region, '', seed.latitude, seed.longitude, false);
      preset.push(new ShellLocation(seed.formattedId, seed.city, seed.latitude, seed.longitude,
        seed.region, seed.countryCode, sources));
    }
    return preset;
  }

  /**
   * Everything about the list that C++ needs but does not draw: the ids the duplicate warning is
   * tested against, and the per-location details the weather-sources dialog reads.
   */
  private publishLocationIds(): void {
    native.setSavedLocationIds(SURFACE_ID, locationIds(this.locations));
    native.setLocationDetails(SURFACE_ID, locationDetails(this.locations));
  }

  /* -------------------------------------------------------------- screen 2: editing the list -- */

  /**
   * The three ways the list itself can change, as C++ encodes them.
   *
   * Only the shell can do any of this: it owns the array, the file it is persisted in, and the
   * response caches keyed by formattedId.
   */
  private onListEdit(command: string): void {
    const parts: string[] = command.split('|');
    if (parts[0] === 'delete' && parts.length >= 2) {
      this.deleteLocation(Number.parseInt(parts[1], 10));
    } else if (parts[0] === 'undo' && parts.length >= 2) {
      // The snackbar's action was tapped before it expired; C++ draws the bar, this side owns the
      // list it would put the row back into.
      this.undoDelete(Number.parseInt(parts[1], 10));
    } else if (parts[0] === 'move' && parts.length >= 3) {
      this.moveLocation(Number.parseInt(parts[1], 10), Number.parseInt(parts[2], 10));
    } else if (parts[0] === 'sources' && parts.length >= 3) {
      this.updateSources(Number.parseInt(parts[1], 10), parts[2]);
    } else if (parts[0] === 'prefs' && parts.length >= 4) {
      // The main screen's edit dialog saved its per-location preferences.
      this.updatePreferences(Number.parseInt(parts[1], 10), parts[2], parts[3]);
    }
  }

  /**
   * The file and C++'s view of the list â€” everything a change has to reach besides the array.
   *
   * The SCENES are deliberately not re-pushed: a delete or a move renumbers them, and C++ applies
   * that renumbering itself because it is where both gestures originate. Handing every scene back
   * instead re-maps every cached response for nothing, and leaves a stale scene behind for any
   * location whose body is not cached.
   */
  private commitLocations(): void {
    writeLocationList(this.filesDir, encodeLocationList(this.locations));
    this.publishLocationIds();
  }

  /**
   * ItemTouchHelper.END (LocationItemTouchCallback.kt:100-118) â€” delete, with an Undo that puts the
   * row back where it was.
   *
   * The two refusals (last row, mid-refresh) are C++'s: it decides them before the swipe commits,
   * so this is only ever called for a delete that goes through.
   */
  private deleteLocation(index: number): void {
    if (index < 0 || index >= this.locations.length) {
      return;
    }
    /*
     * The row is REMEMBERED, not announced.
     *
     * The Undo is a snackbar the RENDERER draws â€” Android's floats over the list, blocks nothing
     * and expires after three seconds, and this used to raise a `promptAction.showDialog` here,
     * which is modal and waits indefinitely. Rebuilding it in ArkTS would have added ArkUI nodes to
     * the port whose premise is that its UI is native, so C++ owns the bar and calls back with
     * `undo|<index>` if its action is tapped. All this side keeps is what to put back.
     */
    this.pendingUndo = this.locations[index];
    this.pendingUndoIndex = index;
    this.locations.splice(index, 1);
    this.commitLocations();
  }

  /** The row a visible snackbar would restore, held only until it expires or is used. */
  private pendingUndo: ShellLocation | null = null;
  private pendingUndoIndex: number = -1;

  /** CancelDeleteListener (kt:138-145) â€” addLocation(location, index), back in its old slot. */
  private undoDelete(index: number): void {
    const removed: ShellLocation | null = this.pendingUndo;
    if (removed === null || index < 0 || index > this.locations.length) {
      return;
    }
    this.pendingUndo = null;
    this.pendingUndoIndex = -1;
    this.locations.splice(index, 0, removed);
    /*
     * THE INSERT COMES FIRST, and the order is not cosmetic.
     *
     * `setLocationDetails` grows C++'s scene array to match the details it is given, so publishing
     * first left scenes at N and then `insert` added one MORE â€” N+1 scenes against N details, and
     * `RebuildLocationRows` walks the longer of the two. The symptom was an extra row at the end of
     * the list with a drag handle and nothing in it, seen on device after a delete/undo.
     *
     * Inserting first puts scenes at N, and the publish that follows then matches rather than grows.
     */
    native.listChanged(SURFACE_ID, 'insert|' + index.toString());
    // The one list change the shell originates, so the one C++ has to be told about; the cached
    // body then fills the slot it opened.
    this.commitLocations();
    const cached: CachedForecast = readCache(this.filesDir, removed.formattedId);
    if (cached.forecast.length > 0) {
      this.apply(index, cached.forecast, cached.airQuality);
    }
  }

  /** `swapLocations` is a MOVE despite its name (`valid.add(to, valid.removeAt(from))`). */
  private moveLocation(from: number, to: number): void {
    if (from < 0 || from >= this.locations.length || to < 0 || to >= this.locations.length) {
      return;
    }
    const moved: ShellLocation[] = this.locations.splice(from, 1);
    this.locations.splice(to, 0, moved[0]);
    this.commitLocations();
  }

  /**
   * The weather-sources dialog's Save on a location the list already holds (kt:848-877).
   *
   * The eight ids are opaque here, as they are everywhere else in the shell. The formattedId is NOT
   * recomputed: C++ refuses a forecast-source change that would collide, and a change that would
   * not is still a change of id â€” closing that properly means rekeying the caches, which is a
   * bigger piece than this step.
   */
  private updateSources(index: number, sources: string): void {
    if (index < 0 || index >= this.locations.length) {
      return;
    }
    this.locations[index] = withSources(this.locations[index], sources);
    this.commitLocations();
  }

  /**
   * The per-location preferences dialog's Confirm (LocationPreferences.kt:234-256).
   *
   * Both values are opaque here, as `sources` is. A MODEL change is a data change — it is what the
   * forecast request asks for — so it re-fetches; a NAME change is only a label, and re-fetching
   * for it would be the "will trigger a full refresh which we should avoid" the Kotlin's own TODO
   * at kt:239-240 complains about.
   */
  private updatePreferences(index: number, customName: string, weatherModels: string): void {
    if (index < 0 || index >= this.locations.length) {
      return;
    }
    const before: ShellLocation = this.locations[index];
    /*
     * Compared with '' NORMALISED to the default.
     *
     * A location that has never been configured stores '', and C++ always hands back a RESOLVED
     * list — `best_match` when nothing else is on. The same selection written two ways: a raw
     * compare called that a change and refetched, so renaming a location cost a network round trip.
     * That is exactly the "will trigger a full refresh which we should avoid" the Kotlin's own TODO
     * complains about (LocationPreferences.kt:239-240), reintroduced by the port.
     *
     * The shell cannot resolve a model list itself — C++ owns the catalogue — so this normalises
     * the ONE case it can know about, which is the only one that occurs: '' against the default.
     */
    const stored: string = before.weatherModels.length > 0 ? before.weatherModels : 'best_match';
    const chosen: string = weatherModels.length > 0 ? weatherModels : 'best_match';
    const modelsChanged: boolean = stored !== chosen;
    this.locations[index] = withPreferences(before, customName, weatherModels);
    this.commitLocations();
    if (modelsChanged) {
      // `refreshOne` clears the spinner itself on both paths; the promise is not awaited because
      // the dialog's Confirm must not block on a network round trip.
      this.refreshOne(index);
    }
  }

  /**
   * requestLocationList (SearchViewModel.kt:59-99), reduced to what the shell owns: the request.
   *
   * `str.trim()` is the Kotlin's (:63); an all-whitespace query reaches the API as empty and is
   * refused, so it is dropped here as `onSearch` already drops an empty one (:176). Everything the
   * body then says â€” whether it is a failure or a genuine miss, and how each hit's admin levels
   * read â€” is decided in C++ (data/OpenMeteoGeocoding.cpp).
   */
  private runSearch(raw: string): void {
    const query: string = raw.trim();
    if (query.length === 0) {
      return;
    }
    native.setSearchLoading(SURFACE_ID, true);
    this.requestSeq++;
    const seq: number = this.requestSeq;
    // `context.currentLocale.code` (OpenMeteoService.kt:641). English only, per CLAUDE.md.
    fetchLocationSearchBody(query, 'en')
      .then((body: string) => {
        if (seq !== this.requestSeq) {
          return;
        }
        native.applySearchResults(SURFACE_ID, body, query);
      })
      .catch((error: Error) => {
        if (seq !== this.requestSeq) {
          return;
        }
        native.setSearchLoading(SURFACE_ID, false);
        // SnackbarHelper (SearchViewModel.kt:66-76) â€” the failure surfaces, it is not swallowed.
        promptAction.showToast({ message: 'Could not reach Open-Meteo', duration: 2000 });
      });
  }

  /**
   * The hit the user chose, as C++ encoded it â€” SearchViewModel.setSelectedLocation (:106-117)
   * followed by ManagementFragment's addLocation (:471-476).
   *
   * A location the list already holds is REFUSED, with `location_message_already_exists`, and the
   * search screen stays open so another hit can be picked. Treating it as a success and popping
   * back would look exactly like adding a second copy had worked.
   */
  private onLocationPicked(encoded: string): void {
    const picked: ShellLocation | null = decodePickedLocation(encoded);
    if (picked === null) {
      return;
    }
    for (let i = 0; i < this.locations.length; i++) {
      if (this.locations[i].formattedId === picked.formattedId) {
        promptAction.showToast({
          message: 'Location list already contains this location with this forecast source.',
          duration: 2000
        });
        return;
      }
    }
    /*
     * A current-position entry needs the permission before it is worth saving.
     *
     * ArkUIApp asks the moment the FAB is tapped, before the dialog; here the FAB is handled in C++,
     * and asking there would mean a new callback into ArkTS purely to open a system prompt. Asking
     * on Save keeps the same property that matters â€” a row that could never resolve is never
     * saved â€” at the cost of the prompt arriving one dialog later.
     */
    if (picked.isCurrentPosition) {
      const context = this.getUIContext().getHostContext() as common.UIAbilityContext;
      // kt:496-514 â€” `_locationListLoading` is up for the length of the write, which is what puts
      // the progress indicator above the rows in place of its spacer.
      native.setListLoading(SURFACE_ID, true);
      ensureLocationPermission(context).then((granted: boolean) => {
        if (!granted) {
          native.setListLoading(SURFACE_ID, false);
          // RefreshErrorType.ACCESS_LOCATION_PERMISSION_MISSING's short message.
          promptAction.showToast({ message: 'Location permission is required', duration: 3000 });
          return;
        }
        this.saveAddedLocation(picked);
        native.setListLoading(SURFACE_ID, false);
      });
      return;
    }
    this.saveAddedLocation(picked);
  }

  /** The add itself, once anything that had to be asked first has been. */
  private saveAddedLocation(picked: ShellLocation): void {
    this.locations.push(picked);
    writeLocationList(this.filesDir, encodeLocationList(this.locations));
    this.publishLocationIds();
    promptAction.showToast({ message: 'Location added', duration: 2000 });  // `location_message_added`
    native.closeSearch(SURFACE_ID);
    /*
     * NO refresh here.
     *
     * This used to fetch the new location's forecast immediately, on the belief that it was "what a
     * freshly added location looks like on Android too". It is not: `addLocation`
     * (MainActivityViewModel.kt:486-517) writes the list and returns, and ManagementFragment
     * (:470-476) follows it with nothing but a snackbar. The weather arrives later from the
     * background WeatherUpdateJob, which CLAUDE.md skips entirely.
     *
     * So a new row shows its region until it is selected, exactly as ArkUIApp's does and exactly as
     * Android's does before its background job runs. Fetching here was also the one behavioural
     * difference that made the two ports hold different amounts of cached weather, which quietly
     * biased the memory comparison in step e.
     */
  }

  /**
   * The system back gesture.
   *
   * ManagementFragment and SearchActivity both close on back; without this the gesture left the app
   * instead, which is the one navigation Android never does from either. C++ answers whether an
   * overlay screen consumed it.
   */
  onBackPress(): boolean {
    return native.onBackPressed(SURFACE_ID);
  }

  /**
   * Screen 3e's two permission rows again on every resume.
   *
   * A permission can also be granted from system Settings, with the app in the background; Android
   * re-reads its state on recomposition, and ArkUIApp does it in LocationSettings.onPageShow. This
   * port has one page, so the same hook fires when the app itself comes back.
   */
  onPageShow(): void {
    pushLocationPermissions();
  }

  /** Which of the two weather_background_* drawable sets C++ should paint from. */
  private isDarkMode(): boolean {
    const context = this.getUIContext().getHostContext() as common.UIAbilityContext;
    return context.config.colorMode === ConfigurationConstant.ColorMode.COLOR_MODE_DARK;
  }

  build() {
    Stack() {
      XComponent({
        id: SURFACE_ID,
        type: XComponentType.SURFACE,
        libraryname: 'entry'
      })
        .onLoad(() => {
          this.start();
        })
        .width('100%')
        .height('100%')
    }
    .width('100%')
    .height('100%')
  }
}
