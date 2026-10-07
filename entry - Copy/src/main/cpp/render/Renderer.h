/*
 * The surface the whole screen is drawn on.
 *
 * ArkUIApp gets a Canvas from ArkUI and paints into it. Here the equivalent is: XComponent hands
 * us an OHNativeWindow, we lock a buffer out of it, wrap that buffer's memory in an
 * OH_Drawing_Bitmap, draw with OH_Drawing_Canvas, and flush the buffer back. That round trip is
 * the whole reason this port exists â€” nothing between the app and the pixels but our own code.
 */
#ifndef BREEZY_RENDER_RENDERER_H
#define BREEZY_RENDER_RENDERER_H

#include <native_drawing/drawing_canvas.h>
#include <native_window/external_window.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <map>
#include <string>
#include <vector>

#include "input/Scroller.h"
#include "model/Weather.h"
#include "render/Theme.h"
#include "render/DetailBlockPainter.h"
#include "render/EnterAnimation.h"
#include "render/GravitySensor.h"
#include "render/PageCards.h"
#include "render/TrendCardPainter.h"
#include "render/WeatherImplementors.h"
#include "view/LocationListScreen.h"
#include "view/LocationSearchScreen.h"
#include "view/AppearanceScreen.h"
#include "view/UnitsScreen.h"
#include "view/AboutScreen.h"
#include "view/AlertListScreen.h"
#include "view/PreviewIconScreen.h"
#include "view/DependenciesScreen.h"
#include "view/PrivacyPolicyScreen.h"
#include "view/BackgroundUpdatesScreen.h"
#include "view/DebugScreen.h"
#include "view/ModulesScreen.h"
#include "view/WorkerInfoScreen.h"
#include "view/LocationScreen.h"
#include "view/MainScreenSettingsScreen.h"
#include "view/NotificationsScreen.h"
#include "view/DisplayManageScreen.h"
#include "view/DetailsScreen.h"
#include "view/PlaceholderScreen.h"
#include "view/SettingsScreen.h"
#include "view/WeatherSourcesScreen.h"
#include "view/Snackbar.h"
#include "view/TrendData.h"
#include "view/LocationPreferenceDialog.h"
#include "view/WeatherSourcesDialog.h"

namespace breezy {

/** Everything the screen currently shows; replaced wholesale by each refresh. */
struct SceneState {
    /** The toolbar title â€” the location's city (HomeFragment.kt:414-417). */
    std::string city;
    WeatherKind kind = WeatherKind::None;
    bool daylight = true;
    bool darkMode = false;
    int utcOffsetSeconds = 0;
    bool hasWeather = false;
    bool is24Hour = true;
    /**
     * The location's coordinates. Carried for screen 4's EphemerisChart alone, which computes the
     * sun's and the moon's altitude over the day — nothing else on a weather page needs them,
     * because the rise/set pair the astro block draws was already resolved at fetch time.
     */
    double latitude = 0.0;
    double longitude = 0.0;
    Weather weather;
};

/** One finger, as the XComponent reports it. */
struct TouchSample {
    enum class Phase { Down, Move, Up, Cancel };
    Phase phase = Phase::Down;
    double x = 0.0;
    double y = 0.0;
    /** Event time in milliseconds, for the fling velocity. */
    double timeMs = 0.0;
};

class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    /** XComponent surface lifecycle. */
    void OnSurfaceCreated(OHNativeWindow* window, uint64_t width, uint64_t height);
    void OnSurfaceChanged(uint64_t width, uint64_t height);
    void OnSurfaceDestroyed();

    /**
     * Installs one location's weather. `index` is its place in the saved-location list and
     * `count` how many there are â€” the Swiper's page set (MainScreen.ets:558).
     */
    void SetScene(int index, int count, SceneState scene);

    /** Where the shell should send a refresh request for the visible location. */
    using RefreshHandler = void (*)(int locationIndex);
    void SetRefreshHandler(RefreshHandler handler) { refresh_ = handler; }

    /**
     * The card order, as a comma-separated id list. Android keeps it in SettingsManager, so it
     * has to outlive the process; the shell owns the file, this owns the list.
     */
    using OrderHandler = void (*)(const std::string& order);
    void SetOrderHandler(OrderHandler handler) { orderChanged_ = handler; }
    void RestoreOrder(const std::string& order);

    /**
     * Where a tap wants to navigate. Every destination on this screen is a not-yet-ported screen,
     * so the shell shows its name â€” CLAUDE.md's placeholder rule.
     */
    using NavigationHandler = void (*)(const std::string& destination);
    void SetNavigationHandler(NavigationHandler handler) { navigate_ = handler; }

    /**
     * A URL the About screen wants opened outside the app — `AlertDialogLink`'s OK
     * (AboutScreen.kt:365-370). Only the shell can start a browsable ability, so it does the
     * confirming and the opening; ArkUIApp draws the same confirmation from its own page.
     */
    using LinkHandler = void (*)(const std::string& url);
    void SetLinkHandler(LinkHandler handler) { openLink_ = handler; }

    /** `BuildConfig.VERSION_NAME`, read from the bundle by the shell — the About header's line. */
    void SetAppVersion(const std::string& versionName) {
        about_.SetVersion("Release " + versionName);
    }

    /**
     * The system bar insets, in surface pixels. The window is laid out full screen, so the toolbar
     * starts below `top` and the footer keeps clear of `bottom` â€” the values Android's
     * `doOnApplyWindowInsets` supplies.
     */
    void SetInsets(double topPx, double bottomPx);

    /**
     * How far the soft keyboard reaches up the window, in surface pixels.
     *
     * Only screen 2b uses it, and only for its bottom bar: Android resizes its window for the IME
     * so SearchActivity's attribution line and Tune FAB ride above the keyboard, and a surface that
     * does not resize would leave both underneath it.
     */
    void SetKeyboardInset(double heightPx);

    /** Told when the sky's lightness changes, so the shell can restyle the status bar. */
    using ThemeHandler = void (*)(bool lightBackground);
    void SetThemeHandler(ThemeHandler handler);

    /** Feeds one finger sample in, in surface coordinates. */
    void OnTouch(const TouchSample& raw);

    /** Paints one frame. Safe to call before the surface exists â€” it simply does nothing. */
    void RenderFrame();

    /** Advances any fling in progress and repaints. Returns true while more frames are wanted. */
    bool OnVSync(double seconds);

    /** Clears the pull-to-refresh spinner when a refresh FAILS; success clears it via SetScene. */
    void EndRefresh();

    /**
     * The alerts for one location. They arrive after the forecast â€” the CAP feed is a second
     * source and a second round trip â€” so they are applied on top of a scene already on screen.
     */
    void SetAlerts(int locationIndex, std::vector<Alert> alerts);

    /** True while the background animation still has something to draw. */
    bool WantsFrames() const;

    /* ---------------------------------------------------------- screen 2b: add a location ----- */

    /**
     * Told when the add-location screen opens or closes, so the shell can attach or detach the IME.
     *
     * The keyboard is the one thing on that screen the renderer cannot produce for itself: only
     * ArkTS can reach @ohos.inputMethod. Everything else â€” field, caret, rows â€” is drawn here.
     *
     * Four states, not two: a dialog over the screen has to drop the keyboard without ending the
     * edit session, or it would cover the dialog's own buttons â€” and re-attaching would throw the
     * typed query away.
     *   0  closed  detach
     *   1  open    attach and raise
     *   2  covered hide the keyboard, keep the session
     *   3  clear   raise it again
     */
    using SearchUiHandler = void (*)(int state);
    void SetSearchUiHandler(SearchUiHandler handler) { searchUi_ = handler; }

    /**
     * Told which hit the user picked and configured, as
     * `id|place|region|admin2|countryCode|latitude|longitude|eight,source,ids`.
     *
     * The shell has never parsed a response body in this port and does not start here: C++ read the
     * geocoding JSON and ran the source catalogue, so it hands back the fields saving a location
     * needs. The shell stores the source ids verbatim without reading them.
     */
    using PickHandler = void (*)(const std::string& encoded);
    void SetPickHandler(PickHandler handler) { pick_ = handler; }

    /**
     * The formattedIds already saved, newline separated â€” the forecast row's duplicate warning.
     *
     * Pushed rather than asked for: only the shell holds the location list, and a callback into
     * ArkTS could answer only asynchronously, while this decides a pixel.
     */
    void SetSavedLocationIds(const std::string& ids);

    /* ------------------------------------------------------- screen 2: editing the list ------- */

    /**
     * Told when the list itself changes, as `verb|payload`:
     *   `delete|<index>`            a row swiped right past the threshold
     *   `move|<from>|<to>`          a drag released on a different slot
     *   `sources|<index>|<ids>`     the weather-sources dialog saved on an existing row
     *
     * The shell owns the list, the file and the caches; the renderer owns only what is drawn. One
     * callback rather than three because all three are the same thing to the shell â€” a list write
     * followed by a persist.
     */
    using ListEditHandler = void (*)(const std::string& command);
    void SetListEditHandler(ListEditHandler handler) { listEdit_ = handler; }

    /**
     * Where a setting chosen on screen 3a goes to be written, as `<key>|<value>` — the shell owns
     * the preferences file, as it owns every file in this port.
     */
    using SettingsEditHandler = void (*)(const std::string& command);
    void SetSettingsEditHandler(SettingsEditHandler handler) { settingsEdit_ = handler; }

    /**
     * What each row of screen 2 needs that the scenes do not carry, one line per location:
     * `formattedId \t countryCode \t region \t admin2 \t latitude \t longitude \t sources \t city`.
     *
     * This is also the list the ROWS come from — a location with no forecast yet has no scene, and
     * still has to appear.
     *
     * The scenes hold what a WEATHER page draws. The weather-sources dialog needs where the
     * location is and which sources it is on, and only the shell has those.
     */
    void SetLocationDetails(const std::string& details);

    /**
     * MainActivityViewModel._locationListLoading (kt:98-99) — a list WRITE is in flight, which is
     * what the progress indicator at kt:281 shows. Only the shell knows; it owns the list.
     */
    void SetListLoading(bool loading);

    /**
     * The ability's colour mode. Screen 3 themes from THIS, not from the location's daylight —
     * SettingsActivity takes BreezyWeatherTheme's default `isSystemInDarkTheme()` (:107-111).
     */
    void SetSystemDarkMode(bool dark);

    /**
     * The Appearance settings, from the shell that owns the preferences file. `darkMode` is
     * DarkMode.kt's id — "system", "light" or "dark".
     */
    void SetDarkModeSettings(const std::string& darkMode, bool dayNightForLocations);
    /**
     * SettingsManager.alternateCalendar, pushed back after the shell stores it. The locale travels
     * with it because it decides the picker's "Regional preference (…)" row and C++ has none.
     */
    void SetAlternateCalendar(const std::string& value, const std::string& language,
                              const std::string& country);
    /** Screen 3e's seven values, pushed back after the shell stores them. */
    void SetModuleSettings(const std::string& weekIconMode, bool monochromeIcons,
                           bool notificationWidget, bool notificationPersistent,
                           const std::string& notificationStyle, bool notificationTempIcon,
                           bool notificationFeelsLike);

    /**
     * The five unit overrides, from the same shell-owned preferences bag, each a unit id or "auto".
     *
     * Arrives by the same round trip a choice takes: the Units screen hands `<key>|<value>` to the
     * shell, the shell writes it and pushes every value straight back through here, so the stored
     * value and the drawn one cannot diverge. Applying them invalidates the cached cards, because
     * every formatted string on a page can change.
     */
    void SetUnitSettings(const std::string& temperature, const std::string& precipitation,
                         const std::string& speed, const std::string& distance,
                         const std::string& pressure);

    /**
     * Screen 3c's six, from the same shell-owned bag. The four flags default to true and the
     * animation mode to "system" (SettingsManager.kt).
     *
     * `background`, `animationMode` and `gravity` are obeyed by the weather page. The remaining
     * three (threshold lines and the two animation switches) are still display-only — making them
     * bite is its own step, in both ports.
     */
    void SetMainScreenSettings(bool background, bool trendLines, const std::string& animationMode,
                               bool gravity, bool fadeIn, bool elements);

    /**
     * Screen 3d's six, plus the background-update interval it only READS (kt:135-145). Defaults
     * differ per key — see NotificationsScreen.h — so the shell sends all of them rather than
     * letting anything here assume.
     *
     * Display and storage only: the notification jobs behind these are background refresh, which
     * CLAUDE.md skips.
     */
    /**
     * Screen 3c's two values. `refreshRate` already arrives with the notification settings — screen
     * 3d greys its rows out when it is "never" — so it is forwarded from there rather than pushed
     * twice; the switch comes from the shell's own preferences bag.
     */
    void SetBackgroundUpdatesValues(const std::string& refreshRate, bool skipWhenBatteryLow);
    void SetNotificationSettings(bool appUpdates, bool alert, bool precipitation, bool today,
                                 bool tomorrow, const std::string& todayTime,
                                 const std::string& tomorrowTime, const std::string& refreshRate,
                                 bool twelveHour);

    /**
     * The time picker's TimeInput. `1` asks the shell to open a numeric edit session for the
     * focused field, `0` to close it — the same shape as SearchUiHandler, and separate from it so
     * the search field's own session is untouched.
     */
    using TimeEntryHandler = void (*)(int state);
    void SetTimeEntryHandler(TimeEntryHandler handler) {
        // Kept on the renderer as well: the edit dialog's custom-name field is not a SCREEN's, so
        // it has no object of its own to hand the handler to.
        timeEntry_ = handler;
        notifications_.SetTimeEntryHandler(handler);
        // Screen 3e's API key uses the same channel with state 2 — a TEXT session.
        location_.SetTextEntryHandler(handler);
        // Screen 3f's ~50 API-key and instance fields use it too.
        weatherSources_.SetTextEntryHandler(handler);
        /*
         * Both text screens pre-fill the shell's buffer with the value already stored, so the
         * first keystroke appends instead of replacing it — ArkUIApp's `TextInput` is seeded the
         * same way. It travels on the settings channel under a reserved key rather than a second
         * NAPI callback: the payload is one string and the channel already carries strings.
         */
        auto seed = [this](const std::string& text) {
            if (settingsEdit_ != nullptr) {
                settingsEdit_(std::string("__ime_seed__|") + text);
            }
        };
        location_.SetSeedHandler(seed);
        weatherSources_.SetSeedHandler(seed);
    }
    /**
     * What the shell has collected so far for whichever field is open. Routed by the current
     * screen: screen 3d's time fields, or screen 3e's API key.
     */
    void SetTimeEntryText(const std::string& text);

    /** Screen 3e's two stored values, from the shell that owns the preferences file. */
    void SetLocationSettings(const std::string& locationService, const std::string& baiduApiKey);
    /** The live grant state, which only ArkTS can read. */
    void SetLocationPermissions(bool coarseGranted, bool preciseGranted);
    /**
     * Where a permission request goes. The shell prompts and pushes the new state back through
     * SetLocationPermissions — C++ never decides the answer itself.
     */
    using PermissionHandler = void (*)(const std::string& permission);
    void SetPermissionHandler(PermissionHandler handler) { permission_ = handler; }

    /**
     * Screen 3f's stored values: the default source for new locations, and every source preference
     * that has one, as `<storageKey>\t<value>\n` lines.
     *
     * The default source is not display-only — SearchActivityRepository.kt:101-140 applies it to a
     * location added from the search screen, so the renderer keeps it for that path too.
     */
    void SetSourceSettings(const std::string& defaultForecastSource, const std::string& prefs);

    /**
     * The three ordered display lists screen 3c edits — `card_display`, `daily_trend_display` and
     * `hourly_trend_display`, as the "&"-joined id strings Android stores.
     */
    void SetDisplayLists(const std::string& cards, const std::string& daily,
                         const std::string& hourly);

    /**
     * A list change the SHELL originated, as `insert|<index>` â€” which is only ever the Undo of a
     * delete. Everything else starts here, and is applied here (see RemoveScene).
     */
    void OnListChanged(const std::string& command);

    /** The field's contents, from the shell's IME listener. */
    void SetSearchQuery(const std::string& query);
    /** The geocoding response body, verbatim. `searched` is the term it answers. */
    void SetSearchResults(const std::string& body, const std::string& searched);
    /** A request has gone out (or failed before it could) â€” the linear progress bar. */
    void SetSearchLoading(bool loading);

    /** Screen 2b finishes onto the list. The shell calls it once an add really happened. */
    void CloseSearchScreen();

    /**
     * The system back gesture. Returns true when an overlay screen consumed it.
     *
     * ManagementFragment and SearchActivity both close on back; without this the gesture left the
     * app instead, which is the one navigation Android never does from either.
     */
    bool OnBackPressed();

private:
    /**
     * Which screen owns the surface.
     *
     * MainActivity swaps HomeFragment for ManagementFragment over ONE window rather than opening a
     * second one, so the port keeps one XComponent and routes paint and touch by mode.
     */
    enum class Screen {
        Main,
        LocationList,
        LocationSearch,
        Settings,
        /** Screen 3c — Background updates. */
        BackgroundUpdates,
        Appearance,
        Units,
        MainSettings,
        Notifications,
        Debug,
        /** Worker info, opened from Background updates ▸ Troubleshoot (WorkerInfoActivity). */
        WorkerInfo,
        /** Screen 3e - External modules (ModulesSettingsScreen). */
        Modules,
        Location,
        WeatherSources,
        /** Screens 3h/3i/3j — one screen, three lists; `displayManage_` holds which. */
        DisplayManage,
        /** Screen 4 — the tile / trend detail screen (DetailsActivity). */
        Details,
        /** The About screen, opened by the settings root's ⓘ (AboutActivity). */
        About,
        /** The Privacy policy list, opened from About (PrivacyPolicyActivity). */
        PrivacyPolicy,
        /** The Dependencies list, opened from About (DependenciesActivity). */
        Dependencies,
        /** The alert list, opened from the main screen's alert card (AlertActivity). */
        AlertList,
        /** The icon preview, opened from Appearance's icon-packs dialog (PreviewIconActivity). */
        PreviewIcon,
        /** Every destination that is not ported, named — ArkUIApp's Placeholder page. */
        Placeholder
    };

    /** Screen 4, opened by a tile or a trend column with the tag that tile carries. */
    void OpenDetailsLocked(DetailTag tag, int dayIndex);
    void CloseDetails();
    void OnDetailsTouch(const TouchSample& raw);

    /** Screen 3's root. Opened by the list's gear, and it covers the list as an Activity does. */
    void OpenSettings();
    void CloseSettings();
    void OnSettingsTouch(const TouchSample& raw);
    /** Screen 3a, opened from the root's Appearance row. */
    void OpenAppearance();
    void CloseAppearance();
    void OnAppearanceTouch(const TouchSample& raw);
    /** Screen 3b, opened from Appearance's Units row. Closes back onto Appearance, not the root. */
    void OpenUnits();
    void CloseUnits();
    void OnUnitsTouch(const TouchSample& raw);
    /** Screen 3c, opened from the settings root's Main screen row. */
    void OpenMainSettings();
    void CloseMainSettings();
    void OnMainSettingsTouch(const TouchSample& raw);
    /** Screen 3d, opened from the settings root's Notifications row. */
    void OpenNotifications();
    void CloseNotifications();
    void OnNotificationsTouch(const TouchSample& raw);
    /** Screen 3g, opened from the settings root's Debug row. */
    void OpenDebug();
    void CloseDebug();
    void OpenModules();
    void CloseModules();
    void OnModulesTouch(const TouchSample& raw);
    void OpenWorkerInfo();
    void CloseWorkerInfo();
    void OnWorkerInfoTouch(const TouchSample& raw);
    void OnDebugTouch(const TouchSample& raw);
    void OpenBackgroundUpdates();
    void CloseBackgroundUpdates();
    void OnBackgroundUpdatesTouch(const TouchSample& raw);
    /** The About screen, opened from the settings root's ⓘ action. */
    void OpenAbout();
    void CloseAbout();
    void OnAboutTouch(const TouchSample& raw);
    /** The Privacy policy list, opened from About's "Privacy policy" row. */
    void OpenPrivacyPolicy();
    void ClosePrivacyPolicy();
    void OnPrivacyPolicyTouch(const TouchSample& raw);
    /** The Dependencies list, opened from About's "Dependencies" row. */
    void OpenDependencies();
    void CloseDependencies();
    void OnDependenciesTouch(const TouchSample& raw);
    /**
     * The alert list, opened from the main screen's alert card. `alertId` is empty when the whole
     * card was tapped and set when one of its rows was (AlertViewHolder.kt:80 vs :149).
     */
    void OpenAlertListLocked(const std::string& alertId);
    void CloseAlertList();
    void OnAlertListTouch(const TouchSample& raw);
    /** The icon preview, opened from the Appearance screen's icon-packs dialog. */
    void OpenPreviewIcon();
    void ClosePreviewIcon();
    void OnPreviewIconTouch(const TouchSample& raw);
    /** Screen 3e, opened from the settings root's Location row. */
    void OpenLocation();
    void CloseLocation();
    void OnLocationTouch(const TouchSample& raw);
    /** Screen 3f, opened from the settings root's Weather sources row. */
    void OpenWeatherSources();
    void CloseWeatherSources();
    void OnWeatherSourcesTouch(const TouchSample& raw);
    /** Screens 3h/3i/3j, opened from screen 3c's three Displayed-data rows and from a card's gear. */
    /**
     * Opens the placeholder over whatever is showing, naming the screen that would have opened.
     *
     * This replaces every `navigate_(...)` call: the shell's toast said the same thing but was not
     * the same surface as ArkUIApp's Placeholder page, so the two ports differed on every unported
     * tap. `navigate_` is still called, so the shell can log or toast if it wants to.
     */
    void Navigate(const std::string& destination, const std::string& detail = std::string());
    /**
     * The same, for callers that ALREADY hold the mutex — HandleTap runs inside OnTouch's locked
     * region, and taking it again would deadlock. It only sets state; the caller renders.
     */
    void NavigateLocked(const std::string& destination,
                        const std::string& detail = std::string());
    void ClosePlaceholder();

    /** Which nowcast bucket a touch is over — the marker's hit test. Lock held. */
    void UpdateNowcastMarker(double x, double y);
    /** MainActivity.kt:897-904 — the toolbar's edit icon. Called with the lock held. */
    void OpenEditDialogLocked();
    void OnPrefsDialogTouch(const TouchSample& sample);

    /** Writes the open list if it changed — kt:186-199's `onStop`. */
    void FlushDisplayManage();
    void OpenDisplayManage(DisplayList list);
    void CloseDisplayManage();
    void OnDisplayManageTouch(const TouchSample& raw);

    /** The two trend cards' tab sets, decided by the Daily/Hourly trends manage screens. */
    std::vector<TrendTab> TabsFor(DisplayList list, const std::vector<TrendTab>& all,
                                  const std::string& stored) const;
    std::vector<TrendTab> DailyTabs() const;
    std::vector<TrendTab> HourlyTabs() const;

    /** `card_display` in order, minus the cards this weather cannot fill (MainAdapter.kt:110-171). */
    std::vector<std::string> BuildCardOrder() const;
    /** A drop on the weather screen writes the new order back to `card_display` (kt:296-311). */
    void PersistCardOrder();

    /** Builds the list rows from the scenes already held, without changing which screen is up. */
    void RebuildLocationRows();
    /** Those rows, then the list itself. */
    void OpenLocationList();
    /** Back to the weather, optionally selecting a location first. */
    void CloseLocationList();
    /** Screen 2 owns the whole gesture while it is open â€” a vertical drag scrolls, a tap acts. */
    void OnLocationListTouch(const TouchSample& sample);

    /** The + FAB: screen 2b over the list, as SearchActivity opens over ManagementFragment. */
    void OpenLocationSearch();
    /** Back to the list, which is where SearchActivity's `finish()` lands. */
    void CloseLocationSearch();
    void OnLocationSearchTouch(const TouchSample& sample);
    /** The shared dialog owns the whole gesture while it is up, over either screen. */
    void OnSourcesDialogTouch(const TouchSample& sample);
    /** Screen 2's swipe-left: the dialog for a location that is already saved. */
    void OpenSourcesForRow(int index);

    /*
     * The scene list follows the location list.
     *
     * `scenes_` is indexed by position, so a delete or a move renumbers it â€” and the renderer is
     * where both originate, so it is the renderer that applies them. Having the shell re-push every
     * scene instead was tried and is wrong twice over: it leaves a stale scene behind for any
     * location with no cached body, and it costs a full re-map of every response. None of the three
     * lock; each is called from an already-locked context.
     */
    void RemoveScene(int index);
    void MoveScene(int from, int to);
    void InsertScene(int index);
    /** Everything the dialog needs about row `index`, from the shell's details lines. */
    bool DetailsFor(int index, SourceLocation* location, LocationSources* sources) const;
    /** One tab-separated field of a details line — the single copy of that walk. */
    std::string DetailField(int index, int field) const;
    /** Location.customName (field 10) and the Open-Meteo model selection (field 11). */
    std::string CustomNameFor(int index) const;
    std::string WeatherModelsFor(int index) const;
    /** getPlace (Location.kt:29-43): the custom name if there is one, else "city, district". */
    std::string PlaceFor(int index, const std::string& city) const;
    /** What row `index` is called, which area it is in, and whether it is the current position. */
    bool DetailsNameFor(int index, std::string* city, std::string* region,
                        bool* currentPosition) const;
    /** Row `index`'s district, which rides the details line rather than the scene. */
    std::string DistrictFor(int index) const;
    /** Whether row `index` is the current position (field 8 of the details line). */
    bool IsCurrentPositionAt(int index) const;

    /** Which axis the current gesture claimed, decided once past the touch slop. */
    enum class Axis { Undecided, Vertical, Horizontal };

    /** Total content height, so the page scroll knows how far it may travel. */
    double ContentHeight() const;
    /** The surface height less the system bar insets â€” the page's own viewport. */
    double ViewportHeight() const;

    /* ------------------------------------------------------ the landscape two-pane layout ------ */

    /**
     * Whether the location list rides BESIDE the weather page instead of over it.
     *
     * `res/layout-w640dp/activity_main.xml` swaps the lone HomeFragment for a DrawerLayout holding
     * ManagementFragment AND HomeFragment at `unfold="true"`, and DrawerLayout.kt:53 ANDs that with
     * `isLandscape` — so BOTH have to hold. On the reference phone that is 837 dp across in
     * landscape and 388 dp in portrait, so only landscape qualifies.
     *
     * Everything two-pane is gated on this, which is what keeps portrait byte-identical.
     */
    bool TwoPane() const;
    /**
     * The list pane's width, or 0 when there is no pane.
     *
     * DrawerLayout.onMeasure:79-87 with DisplayExtensions.kt:178-190 —
     *   adaptive    = min(width, 512 dp)                  (600 dp on a tablet; this is a phone)
     *   drawerWidth = clamp(width - adaptive, 280 dp, 320 dp)
     * On this phone in landscape that is 2720 - 1664 = 1056 px, clamped to the 320 dp maximum.
     */
    double PaneWidth() const;
    /** What the weather page gets: the surface less the pane. It is NARROWER, not shifted. */
    double ContentWidth() const;
    /** FitTabletRecyclerView.kt:33-34 — the main list's own 8 dp horizontal padding. */
    double PageInset() const;
    /** The main list's content column: `ContentWidth()` less that padding on both sides. */
    double PageContentWidth() const;

    /** Reports the sky's lightness to the shell when it changes, for the status bar's foreground. */
    void ReportTheme();
    /** The horizontal scroller under (x, y), or nullptr when the point is not on a list. */
    Scroller* ListAt(double x, double y);
    /** True while a trend list is under the finger or still gliding. */
    bool ListScrolling() const;
    /** getBlocksPerRow â€” how many half-size detail blocks fit across the page at this width. */
    int BlocksPerRow() const;
    /** Selects the tab a tap landed on, or reports a navigation. Returns true if anything changed. */
    bool HandleTap(double x, double y);
    /** Rebuilds both cards' series for their current tabs. */
    void RebuildSeries();
    /** Tells the surface what size its buffers should be, and that we draw them on the CPU. */
    void ApplyGeometry(uint64_t width, uint64_t height);
    /** Releases the scratch bitmap and its canvas. Safe to call when there is none. */
    void DestroyScratch();

    /** One scene's gradient plus its particles â€” called once, or twice during a cross-fade. */
    void PaintScene(OH_Drawing_Canvas* canvas, WeatherKind kind, bool daylight, bool darkMode,
                    WeatherImplementor* implementor) const;
    /**
     * The two halves of a scene, split because only ONE of them moves.
     *
     * The sky is a full-surface gradient that depends on nothing that changes between animation
     * ticks, so `RenderFrame` caches it in `sky_`; the particles are redrawn on every tick.
     */
    void PaintSky(OH_Drawing_Canvas* canvas, WeatherKind kind, bool daylight, bool darkMode) const;
    void PaintParticles(OH_Drawing_Canvas* canvas, WeatherImplementor* implementor) const;
    /** Fills the page with the weather_background_* gradient, then its particles. */
    void DrawBackground(OH_Drawing_Canvas* canvas) const;
    /** The delete Undo, drawn last and at the surface origin so it floats over either layout. */
    void DrawSnackbar(OH_Drawing_Canvas* canvas);
    /** Snapshots the current scene as the fade-out side of a 300 ms background cross-fade. */
    void BeginBackgroundSwitch();
    /** HeaderViewHolder.kt:98-104 â€” arms the temperature count-up toward the new value. */
    void StartTemperatureCountUp();
    /** The big number as currently animated, in the display unit. */
    double DisplayedTemperature() const;
    /** The toolbar's â‹® menu â€” one item, "Open in another app" (activity_main.xml:11-16). */
    void DrawToolbarMenu(OH_Drawing_Canvas* canvas);
    /** 0 at the top of the page, 1 once the cards have covered the background. */
    double BackgroundScrollRate() const;
    /** fragment_home.xml:23-35 â€” the city title over the background. */
    void DrawToolbar(OH_Drawing_Canvas* canvas) const;
    /**
     * container_main_header.xml â€” refresh time, condition, temperature, feels like, range.
     * Returns the y the header ended at, which is where the first card starts.
     */
    double DrawHeader(OH_Drawing_Canvas* canvas) const;
    /** One reorderable row of the page: a full-width card or a half-width block. */
    struct PageItem {
        std::string id;
        bool halfWidth = false;
        double left = 0.0;
        double top = 0.0;
        double width = 0.0;
        double height = 0.0;
    };

    /** The scrolling column â€” header then cards. Returns its total height. */
    double DrawContent(OH_Drawing_Canvas* canvas);
    /**
     * Draws one item at the given slot and returns its height. `canvas` may be null, in which
     * case nothing is drawn and only the height is measured â€” which is how the layout pass
     * knows where the drop targets are before anything is painted.
     */
    double DrawItem(OH_Drawing_Canvas* canvas, const std::string& id, double left, double top,
                    double width);
    /** Updates a trend card's touch band from the layout, for a card that is culled this frame. */
    void NoteTrendBands(const PageItem& item);
    /** The order with the dragged item lifted out and reinserted at its drop index. */
    std::vector<std::string> EffectiveOrder() const;
    /** Rebuilds `layout_` from `EffectiveOrder()`, laying half-width items two to a row. */
    void LayOutItems(double topPx);
    /** Which slot the finger is over, for the drop. */
    int DropIndexAt(double x, double pageY) const;
    void StartDrag(double x, double pageY);
    void EndDrag();
    /** Swipes to another saved location, keeping this one's scroll position. */
    void SelectLocation(int index);
    /** Puts the daily list back on today, absolutely rather than by a relative drag. */
    void ScrollDailyToToday();
    /** HomeFragment's page dots (InkPageIndicator) â€” one per saved location. */
    void DrawIndicator(OH_Drawing_Canvas* canvas) const;
    /** The pull-to-refresh spinner, while the page is over-scrolled or a refresh is in flight. */
    void DrawRefreshIndicator(OH_Drawing_Canvas* canvas) const;
    /** The colours both trend cards resolve from the theme and the current weather. */
    TrendCardColors CardColors() const;

    /** md_theme_inverseSurface â€” every header and toolbar text (styles.xml:29). */
    uint32_t ContentColor() const;

    OHNativeWindow* window_ = nullptr;
    uint64_t width_ = 0;
    uint64_t height_ = 0;
    /**
     * One scene per saved location. ArkUIApp keeps a Swiper page per location and per-location
     * Scrollers; the same state lives here, with `scene_` a copy of the visible one so every
     * painter keeps reading a single scene.
     */
    std::vector<SceneState> scenes_;
    std::vector<double> pageScrollByLocation_;
    int currentLocation_ = 0;
    int locationCount_ = 1;
    SceneState scene_{};
    RefreshHandler refresh_ = nullptr;
    OrderHandler orderChanged_ = nullptr;
    /** The saved order, held until the first refresh builds the ids it names. */
    std::vector<std::string> restoredOrder_;
    /** Over-scroll past the top, which is what arms pull-to-refresh. */
    double overscroll_ = 0.0;
    bool refreshing_ = false;

    /** The page, and one horizontal scroller per trend list. */
    Scroller pageScroll_;
    Scroller dailyScroll_;
    Scroller hourlyScroll_;

    /* Gesture state. */
    Axis axis_ = Axis::Undecided;
    Scroller* activeList_ = nullptr;
    double lastX_ = 0.0;
    double lastY_ = 0.0;
    double lastTimeMs_ = 0.0;
    double downX_ = 0.0;
    double downY_ = 0.0;
    /** Exponentially smoothed pointer velocity, px/s, for the fling. */
    double velocityX_ = 0.0;
    double velocityY_ = 0.0;

    /** Where each card ended up last frame, so touches can be routed to the right list. */
    double dailyListTop_ = 0.0;
    double dailyListBottom_ = 0.0;
    double hourlyListTop_ = 0.0;
    double hourlyListBottom_ = 0.0;
    /** Measured by drawing, so the scroll range can never disagree with what is on screen. */
    double contentHeight_ = 0.0;
    /** Where the alert card starts and ends, so a tap on it can open the alerts screen. */
    double alertTop_ = 0.0;
    double alertBottom_ = 0.0;

    /**
     * The selected tab is CARD state, exactly as DailyViewHolder keeps `selectedIndex` on its own
     * trendAdapter rather than on the activity.
     */
    int dailyTab_ = 0;
    int hourlyTab_ = 0;
    bool dailyOverflowOpen_ = false;
    bool hourlyOverflowOpen_ = false;
    TrendSeries dailySeries_;
    TrendSeries hourlySeries_;
    /**
     * Bumped whenever the series are rebuilt. The column bitmap cache keys off it, so anything
     * that reaches RebuildSeries â€” a refresh, a tab, a location, a theme flip â€” invalidates the
     * cached columns and nothing else has to remember to.
     */
    uint64_t seriesGeneration_ = 0;
    /** Where the chips landed last frame, so a tap can be matched without re-laying-out. */
    std::vector<ChipBounds> dailyChips_;
    std::vector<ChipBounds> hourlyChips_;
    /**
     * The open overflow menu, held back so it can be drawn after every card.
     *
     * Drawn inside DrawTrendCard it lands UNDERNEATH the cards that follow, and with seven hidden
     * tabs the menu is taller than the card, so the detail blocks below painted most of it away. It
     * is z-order, not clipping. `pendingOverflow_` is set during the page's draw loop and consumed
     * once at the end of DrawContent, in the same canvas transform the cards were drawn in.
     */
    bool pendingOverflow_ = false;
    bool pendingOverflowDaily_ = true;
    TrendCardSpec pendingOverflowSpec_;
    TrendCardColors pendingOverflowColors_{};
    ChipBounds pendingOverflowAnchor_;
    double pendingOverflowWidth_ = 0.0;
    std::vector<DetailBlock> detailBlocks_;
    /** The particle painter for the current weather; null for a kind with no animation. */
    std::unique_ptr<WeatherImplementor> implementor_;

    /* The 300 ms background cross-fade (WeatherBackground.ets switchProgress). */
    WeatherKind prevKind_ = WeatherKind::None;
    bool prevDaylight_ = true;
    bool prevDarkMode_ = false;
    bool prevValid_ = false;
    std::unique_ptr<WeatherImplementor> prevImplementor_;
    double switchProgress_ = 1.0;

    /* The header count-up (NumberAnimTextView), in rounded display-unit degrees. */
    double tempFromDisp_ = 0.0;
    double tempToDisp_ = 0.0;
    double tempAnimMs_ = 0.0;
    double tempAnimDurationMs_ = 0.0;

    /** Spins the refresh ring while a refresh is in flight. */
    double spinnerAngle_ = 0.0;

    /* The toolbar's â‹® menu and its one item's bounds, for the tap. */
    bool toolbarMenuOpen_ = false;
    double menuItemLeft_ = 0.0;
    double menuItemTop_ = 0.0;
    double menuItemRight_ = 0.0;
    double menuItemBottom_ = 0.0;
    Screen screen_ = Screen::Main;
    LocationListScreen locationList_;
    LocationSearchScreen locationSearch_;
    SettingsScreen settings_;
    AppearanceScreen appearance_;
    double appearanceDownY_ = 0.0;
    double appearanceLastY_ = 0.0;
    bool appearanceDragged_ = false;

    UnitsScreen units_;
    double unitsDownY_ = 0.0;
    double unitsLastY_ = 0.0;
    bool unitsDragged_ = false;

    /*
     * Screen 3c's three scene settings, held here as well as on the screen that edits them,
     * because the WEATHER page is what obeys them: `main_screen_background` decides whether the
     * scene is drawn at all, `background_animation_mode` whether it moves, and
     * `gravity_sensor_switch` whether it leans with the phone. The other three belong to parts of
     * the page that do not read them yet.
     */
    bool mainScreenBackground_ = true;
    std::string backgroundAnimationMode_ = "system";
    /** screen 3c's `trend_horizontal_line_switch` — read by CardColors into TrendCardColors. */
    bool trendHorizontalLines_ = true;
    /**
     * screen 3c's `list_animation_switch` and `item_animation_switch`.
     *
     * `cardsFadeIn_` gates the staggered card entrance (AbstractMainViewHolder.kt:53-55, 71-75);
     * `elementAnimations_` gates the header's temperature count-up, which is the only per-element
     * animation this port has — the progress arcs are painted at their final value rather than
     * swept, so there is nothing else for it to turn off.
     */
    bool cardsFadeIn_ = true;
    bool elementAnimations_ = true;

    /**
     * MaterialPainterView's gravity half (kt:53-63). The sensor publishes raw angles from its own
     * thread; the two chasers smooth them on the render thread and are reseeded whenever the
     * implementor is rebuilt (kt:336-343).
     */
    GravitySensor gravitySensor_;
    DelayRotateController rotator2D_{0.0};
    DelayRotateController rotator3D_{0.0};
    /** The smoothed angles the last scene advance used, so a repaint between advances holds. */
    double sceneRotation2D_ = 0.0;
    double sceneRotation3D_ = 0.0;
    /**
     * MaterialPainterView.hasDrawn (kt:94, 256-262). With animations off the scene is advanced by
     * a real interval exactly once and by 0 thereafter — but frames keep coming, because the
     * gravity parallax is not an animation and still has to track the phone.
     */
    bool sceneHasDrawn_ = false;
    /**
     * kt:327-343 — a new implementor gets new rotators, seeded at the angle the phone is at now so
     * the scene does not swing in from level. Callers must hold `mutex_`.
     */
    void ReseedRotators();

    MainScreenSettingsScreen mainSettings_;
    double mainSettingsDownY_ = 0.0;
    double mainSettingsLastY_ = 0.0;
    bool mainSettingsDragged_ = false;

    NotificationsScreen notifications_;
    double notificationsDownY_ = 0.0;
    double notificationsLastY_ = 0.0;
    bool notificationsDragged_ = false;

    /** Screen 3g is one row, so it neither scrolls nor needs drag state. */
    DebugScreen debug_;
    WorkerInfoScreen workerInfo_;
    ModulesScreen modules_;
    double modulesDownY_ = 0.0;
    double modulesLastY_ = 0.0;
    bool modulesDragged_ = false;
    /** Screen 3c — Background updates (BackgroundUpdatesSettingsScreen). */
    BackgroundUpdatesScreen backgroundUpdates_;
    std::string backgroundUpdatesRate_ = "1:30";
    /** `ignoreUpdatesWhenBatteryLow` — SettingsManager's default is true. */
    bool backgroundUpdatesSkipBatteryLow_ = true;

    /** About is long enough to scroll, so it carries the same drag state the other lists do. */
    AboutScreen about_;
    double aboutDownY_ = 0.0;
    double aboutLastY_ = 0.0;
    bool aboutDragged_ = false;

    PrivacyPolicyScreen privacy_;
    double privacyDownY_ = 0.0;
    double privacyLastY_ = 0.0;
    bool privacyDragged_ = false;

    DependenciesScreen dependencies_;
    double dependenciesDownY_ = 0.0;
    double dependenciesLastY_ = 0.0;
    bool dependenciesDragged_ = false;

    AlertListScreen alertList_;
    double alertListDownY_ = 0.0;
    double alertListLastY_ = 0.0;
    bool alertListDragged_ = false;

    PreviewIconScreen previewIcon_;
    double previewIconDownY_ = 0.0;
    double previewIconLastY_ = 0.0;
    bool previewIconDragged_ = false;

    LocationScreen location_;
    double locationDownY_ = 0.0;
    double locationLastY_ = 0.0;
    bool locationDragged_ = false;

    PlaceholderScreen placeholder_;
    /** The screen the placeholder was opened over, so Back returns exactly there. */
    Screen placeholderReturn_ = Screen::Main;
    /** Which screen opened About, so Back returns there rather than always to Settings. */
    Screen aboutReturn_ = Screen::Settings;

    /** Screen 4, opened by every tile and trend column. */
    DetailsScreen details_;
    double detailsDownX_ = 0.0;
    double detailsDownY_ = 0.0;
    double detailsLastX_ = 0.0;
    double detailsLastY_ = 0.0;
    bool detailsDragged_ = false;
    /** True while the drag started inside the day pager, which scrolls sideways instead. */
    bool detailsDragTabs_ = false;
    double detailsLastTimeMs_ = 0.0;
    double detailsVelocityX_ = 0.0;
    double detailsVelocityY_ = 0.0;

    DisplayManageScreen displayManage_;
    /** The stored value of each of the three lists, which screen 3c summarises. */
    std::string cardDisplay_;
    std::string dailyTrendDisplay_;
    std::string hourlyTrendDisplay_;
    /** Where a gear on the Blocks screen returns to, so back walks the stack Android walks. */
    std::vector<DisplayList> displayManageStack_;

    WeatherSourcesScreen weatherSources_;
    double weatherSourcesDownY_ = 0.0;
    double weatherSourcesLastY_ = 0.0;
    bool weatherSourcesDragged_ = false;
    /**
     * Screen 3f's General row, kept here because the SEARCH screen needs it: a location added there
     * gets `applyDefaultSourcePreference` rather than the plain preset (SearchActivityRepository.kt).
     */
    std::string defaultForecastSource_ = "auto";
    /**
     * Where a chosen setting goes to be WRITTEN, as `<key>|<value>`.
     *
     * C++ owns no files in this port, so a choice made here is handed to the shell, which stores it
     * and hands the new values straight back through SetDarkModeSettings.
     */
    std::function<void(const std::string&)> settingsEdit_;
    /** The ability's colour mode, published by the shell. */
    bool systemDarkMode_ = false;
    /** DarkMode.kt's three values. SettingsManager's default is SYSTEM. */
    enum class DarkModeSetting { System, Light, Dark };
    DarkModeSetting darkModeSetting_ = DarkModeSetting::System;
    /**
     * SettingsManager.dayNightModeForLocations. The Kotlin's default is `SDK_INT < Q` — FALSE on
     * anything modern, which reads the opposite way round to the name.
     */
    bool dayNightForLocations_ = false;

    /** ThemeManager.isLightTheme. Callers must hold `mutex_`. */
    bool IsLightTheme(bool daylight) const;
    /** The app's effective dark mode, which the settings screens theme from. Holds `mutex_`. */
    bool AppIsDark() const;
    /** Re-derives every scene's theme after a setting changed. Callers must hold `mutex_`. */
    void RefreshThemes();
    /** Where a settings drag started, and whether it has passed the slop. */
    double settingsDownY_ = 0.0;
    double settingsLastY_ = 0.0;
    bool settingsDragged_ = false;
    /**
     * The weather-sources dialog, which belongs to NEITHER screen: screen 2 opens it by swiping a
     * row left and screen 2b by picking a search hit, and it sits above whichever one is showing.
     */
    WeatherSourcesDialog sourcesDialog_;
    /** The main screen's "Edit this location" dialog (MainActivity.kt:481-607). */
    LocationPreferenceDialog prefsDialog_;
    /**
     * The GLOBAL `location_service` setting, mirrored here.
     *
     * The Location settings SCREEN owns its own copy, but the edit dialog's first row shows and
     * writes the same setting without that screen being on screen at all.
     */
    std::string locationService_ = "native";
    /** SetTimeEntryHandler's handler, for the one field that belongs to no screen. */
    TimeEntryHandler timeEntry_ = nullptr;

    /**
     * The nowcast chart's marker (PrecipitationNowcastViewHolder.kt:163-183).
     *
     * The bucket under the finger, or -1. It exists only while a touch is down — Vico's marker is
     * bound to the chart's touch handler, not to a selection that persists. `nowcastChart_` is where
     * the card put its plot last frame, which is what the hit test needs; the card decides its own
     * geometry from the text it measures, so the bounds are an output of the draw.
     */
    int nowcastMarker_ = -1;
    NowcastChartBounds nowcastChart_;
    /**
     * The delete Undo. Drawn on the SURFACE rather than inside the list, because that is where
     * Android puts it — the container is the activity's content view (BreezyActivity.kt:81), so in
     * two-pane it spans the window rather than the drawer pane.
     */
    Snackbar snackbar_;
    /** The row a visible snackbar would put back, and the slot it came from. */
    int undoIndex_ = -1;
    /** One line per location, as SetLocationDetails documents. */
    std::vector<std::string> locationDetails_;
    /* The list screen's own gesture state: a tap is a press that never passed the slop. Shared with
     * the search screen, which has the same two gestures and is never open at the same time. */
    double listDownX_ = 0.0;
    double listDownY_ = 0.0;
    double listLastY_ = 0.0;
    bool listDragged_ = false;
    /**
     * A finger is on the LIST, as `touchDown_` is for the page. Feeds ListScrolling(), which holds
     * the weather scene still for the length of a gesture — in two-pane the list is scrolled while
     * the page behind it is still animating, and without this the two contend every frame.
     */
    bool listTouchDown_ = false;
    /** Which of the three things a press on the location list turned into, once the slop is past. */
    enum class ListGesture { Undecided, Scroll, Swipe, Drag };
    ListGesture listGesture_ = ListGesture::Undecided;
    EnterAnimation enter_;
    NavigationHandler navigate_ = nullptr;
    LinkHandler openLink_ = nullptr;
    PermissionHandler permission_ = nullptr;
    SearchUiHandler searchUi_ = nullptr;
    PickHandler pick_ = nullptr;
    ListEditHandler listEdit_ = nullptr;
    ThemeHandler themeChanged_ = nullptr;
    /** The lightness last reported to the shell, so the bar is only restyled when it changes. */
    int reportedLightBackground_ = -1;

    /** System bar insets in surface pixels â€” the page is laid out inside them. */
    double topInset_ = 0.0;
    double keyboardInset_ = 0.0;
    double bottomInset_ = 0.0;

    /*
     * Drag-to-reorder. MainAdapter.isDraggable (kt:274-285) makes the trend cards and the
     * half-size blocks reorderable and excludes the alert card, the header and the footer, so
     * only the ids in `order_` participate.
     */
    std::vector<std::string> order_;
    std::vector<PageItem> layout_;
    /** Index into `order_` of the item under the finger, or -1. */
    int draggingIndex_ = -1;
    /** Where it would land if the finger lifted now. */
    int dropIndex_ = -1;
    double dragDX_ = 0.0;
    double dragDY_ = 0.0;
    /** Set at touch-down; the long press fires from the frame loop, not from a move. */
    bool touchDown_ = false;
    double touchDownAtMs_ = 0.0;
    double heldMs_ = 0.0;

    /** Guards the scroll state, which the vsync thread reads while the UI thread writes it. */
    mutable std::mutex mutex_;

    /**
     * Serialises RenderFrame itself.
     *
     * Painting used to happen on the UI thread as well, straight out of DispatchTouchEvent, so a
     * touch move and a vsync could be inside OH_NativeWindow_LockBuffer at the same time. Touch
     * now only marks state and the frame loop paints, but the shell can still ask for a frame from
     * the UI thread (a forecast arriving, alerts, a failed refresh), and two producers on one
     * surface is not something the buffer queue is meant to see.
     */
    mutable std::mutex renderMutex_;

    /**
     * The bitmap every frame is drawn into before being copied to the locked buffer, kept for the
     * life of the surface rather than rebuilt per frame (see RenderFrame for why the copy stays).
     */
    OH_Drawing_Bitmap* scratch_ = nullptr;
    OH_Drawing_Canvas* scratchCanvas_ = nullptr;
    int32_t scratchWidth_ = 0;
    int32_t scratchHeight_ = 0;

    /*
     * The painted weather background, kept between frames.
     *
     * Instrumenting the frame put 73 % of it â€” 21 ms of a 28 ms frame â€” in DrawBackground alone.
     * The scene is a full-screen gradient plus, for a clear day, twelve antialiased translucent
     * squares up to 1812 px across: about 30 megapixels of blended, antialiased fill. Android pays
     * none of that on the CPU, because MaterialPainterView is an ordinary hardware-accelerated
     * View; this renderer draws into a CPU-locked buffer and has no GPU path at all.
     *
     * It does not need repainting per frame either way. MaterialWeatherView drives its own
     * invalidate loop and a trend list scrolling sideways does not touch it, so the port keeps the
     * painted scene and copies it in â€” a straight streaming memcpy â€” repainting only when the
     * particles actually advance, the scene changes, or the scroll fade moves.
     */
    OH_Drawing_Bitmap* background_ = nullptr;
    OH_Drawing_Canvas* backgroundCanvas_ = nullptr;
    bool backgroundValid_ = false;
    double backgroundRate_ = -1.0;
    /**
     * The SKY alone — the gradient with no particles on it, at full surface size.
     *
     * The scene repaint is driven by the animation tick, but its expensive half is a three-stop
     * gradient over 3.4 M pixels that depends only on the kind, the daylight flag, the theme and
     * the geometry. Regenerating it ~30 times a second produced identical pixels every time; it is
     * painted here once and memcpy'd back per tick instead. Output is unchanged.
     */
    OH_Drawing_Bitmap* sky_ = nullptr;
    OH_Drawing_Canvas* skyCanvas_ = nullptr;
    bool skyValid_ = false;
    WeatherKind skyKind_ = WeatherKind::None;
    bool skyDaylight_ = false;
    bool skyDarkMode_ = false;
    double skyPaneWidth_ = -1.0;
    /** Bumped whenever the painted scene is out of date; compared against what was last painted. */
    uint64_t backgroundTick_ = 0;
    uint64_t backgroundPaintedTick_ = 0;
    /** Time banked towards the next particle step, so the animation runs at its own rate. */
    double backgroundAccumMs_ = 0.0;
    /** Frames drawn since the scene was last repainted, so a repaint cannot re-arm itself. */
    int backgroundFramesSince_ = 0;

    /*
     * Partial frames.
     *
     * While a trend list is the only thing moving, every pixel outside its band is already correct
     * in the scratch from the frame before â€” the page has not scrolled, no card has moved, the
     * scene is held still, nothing is animating. So the frame is clipped to that band, and the copy
     * out to the display buffer is clipped to it too.
     *
     * This is safe in a way an ordinary dirty-rect scheme is not, because the scratch is the whole
     * truth: it always holds a complete, correct frame. The only question is which ROWS of a given
     * display buffer are stale, and that is answered exactly â€” buffers rotate, so a buffer is stale
     * in everything that has changed since it was last written. `partialRunStart_` is the frame
     * index of the last full frame; a buffer written on or after it needs only the union of the
     * bands drawn since, and anything older is copied whole.
     */
    uint64_t frameIndex_ = 0;
    uint64_t partialRunStart_ = 0;
    double runDirtyTop_ = 0.0;
    double runDirtyBottom_ = 0.0;
    /** The band drawn this frame, in surface pixels; the whole surface on a full frame. */
    double frameDirtyTop_ = 0.0;
    double frameDirtyBottom_ = 0.0;
    /** Which frame each display buffer was last written on, keyed by its mapped address. */
    std::map<const void*, uint64_t> bufferFrame_;

    /*
     * The band each of the last few frames drew.
     *
     * `runDirty*` â€” the union since the last FULL frame â€” is only a usable answer while full frames
     * keep happening, which on the main screen they do. Screen 2 can run indefinitely without one,
     * and then that union grows to the whole surface and never shrinks again: every copy goes back
     * to 13.7 MB, now with the clip's cost on top. Measured, that made screen 2 WORSE than the
     * unconditional full frame it replaced.
     *
     * A buffer is stale in exactly what was drawn since it was last written, so keeping the last
     * few frames' bands answers it directly. Buffers rotate two or three deep, so a short history
     * covers every live buffer; anything older than the history is copied whole.
     */
    struct FrameBand {
        uint64_t frame = 0;
        double top = 0.0;
        double bottom = 0.0;
    };
    static constexpr size_t kFrameBandHistory = 8;
    std::vector<FrameBand> frameBands_;
    /** Records this frame's band and drops anything older than the history. */
    void RecordFrameBand();
    /**
     * The rows of a buffer last written on `bufferFrameIndex` that are now stale. False when the
     * history does not reach that far back and the buffer has to be copied whole.
     */
    bool StaleBandFor(uint64_t bufferFrameIndex, double* top, double* bottom) const;
    /** Page state the partial-frame test compares against, and the scroll it was captured at. */
    uint64_t lastPageSignature_ = 0;
    bool lastFrameWasComplete_ = false;

    /*
     * Which screen the scratch bitmap currently holds a complete frame of.
     *
     * `lastFrameWasComplete_` alone cannot gate an overlay screen's partial frame: the last
     * complete frame may have been the main screen we just came from, in which case the scratch
     * holds a weather page and a banded repaint would leave most of it on screen.
     */
    Screen scratchScreen_ = Screen::Main;
    bool scratchValid_ = false;
    /** Whether the sources dialog was up last frame â€” the frame that erases it must be full. */
    bool dialogWasOpen_ = false;

    /** Everything that changes pixels OUTSIDE the trend lists, as one comparable value. */
    uint64_t PageSignature() const;
    /** The band a list-only frame has to redraw, or false when the whole page must be drawn. */
    bool PartialBand(double* top, double* bottom) const;
};

}  // namespace breezy

#endif  // BREEZY_RENDER_RENDERER_H
