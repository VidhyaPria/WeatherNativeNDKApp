/*
 * Edge-to-edge, and the status bar's foreground colour.
 *
 * `BreezyActivity.onCreate` calls `enableEdgeToEdge()` and then `window.setSystemBarStyle(...)`
 * (BreezyActivity.kt:39-41), so the weather background paints THROUGH the status bar and the clock
 * and signal icons sit on top of it — which is why the Android screenshots show the orange sky
 * behind the time and the ports showed a white strip. HarmonyOS defaults the other way: the window
 * is laid out inside the avoid areas and the bar keeps the system's own background.
 *
 * `setSystemBarStyle` also flips the bar's FOREGROUND with the theme
 * (DisplayExtensions.kt:214-241, driven from MainActivity.updateSystemBarStyle), so a light sky
 * gets dark icons and a night sky gets light ones. That is a per-location, per-refresh decision
 * here, so the screen calls `applyStatusBarStyle` whenever its background lightness changes.
 *
 * The top inset the layout loses by going full screen is published through AppStorage, because the
 * toolbar still has to start below the status bar — Android gets the same value from
 * `doOnApplyWindowInsets`.
 */

import { window } from '@kit.ArkUI';

/** Height of the status bar in vp, 0 until the window has reported its avoid area. */
export const TOP_INSET_KEY: string = 'breezyTopInset';
/** Height of the navigation bar in vp — the footer keeps clear of it, as `insets.bottom` does. */
export const BOTTOM_INSET_KEY: string = 'breezyBottomInset';

let mainWindow: window.Window | null = null;
let lastLightStatusBar: boolean | null = null;
/** Latest insets in SURFACE PIXELS — what the C++ renderer draws in. */
let topInsetPx: number = 0;
let bottomInsetPx: number = 0;
/**
 * How far the soft keyboard reaches up the window, in surface pixels; 0 when it is down.
 *
 * Android resizes its window for the IME (`adjustResize`), so SearchActivity's bottom app bar — the
 * attribution line and the Tune FAB — rides above the keyboard. An XComponent surface does not
 * resize, so without this the bar was simply underneath it and could not be reached at all.
 */
let keyboardInsetPx: number = 0;
type InsetListener = (topPx: number, bottomPx: number, keyboardPx: number) => void;
let insetListener: InsetListener | null = null;

function publishInsets(win: window.Window): void {
  try {
    const area: window.AvoidArea = win.getWindowAvoidArea(window.AvoidAreaType.TYPE_SYSTEM);
    topInsetPx = area.topRect.height;
    bottomInsetPx = area.bottomRect.height;
    AppStorage.setOrCreate(TOP_INSET_KEY, px2vp(topInsetPx));
    AppStorage.setOrCreate(BOTTOM_INSET_KEY, px2vp(bottomInsetPx));
  } catch (error) {
    console.error('avoid area unavailable: ' + (error as Error).message);
  }
  try {
    const keyboard: window.AvoidArea = win.getWindowAvoidArea(window.AvoidAreaType.TYPE_KEYBOARD);
    keyboardInsetPx = keyboard.bottomRect.height;
  } catch (error) {
    keyboardInsetPx = 0;
  }
  if (insetListener !== null) {
    insetListener(topInsetPx, bottomInsetPx, keyboardInsetPx);
  }
}

/**
 * Subscribes to the insets in surface pixels and delivers the current pair straight away.
 *
 * The NDK shell needs this rather than the AppStorage keys: nothing of its page is an ArkUI
 * component, so there is no @StorageProp to re-render, and polling for a value that changes only
 * on rotation would be a cost this build is being measured on.
 */
export function onInsetsChanged(listener: InsetListener): void {
  insetListener = listener;
  listener(topInsetPx, bottomInsetPx, keyboardInsetPx);
}

/**
 * Lays the window out under the system bars and makes them transparent. Call once, from
 * onWindowStageCreate, before the content is loaded.
 */
export function enableEdgeToEdge(win: window.Window): void {
  mainWindow = win;
  try {
    win.setWindowLayoutFullScreen(true);
  } catch (error) {
    console.error('setWindowLayoutFullScreen failed: ' + (error as Error).message);
  }
  try {
    // Transparent, as `statusBarColor = Color.TRANSPARENT` — the page's own background shows.
    win.setWindowSystemBarProperties({
      statusBarColor: '#00000000',
      navigationBarColor: '#00000000'
    });
  } catch (error) {
    console.error('setWindowSystemBarProperties failed: ' + (error as Error).message);
  }
  publishInsets(win);
  // A rotation changes both insets, and the avoid area is only correct once the window has resized.
  try {
    win.on('windowSizeChange', () => publishInsets(win));
    win.on('avoidAreaChange', () => publishInsets(win));
  } catch (error) {
    console.error('inset listener failed: ' + (error as Error).message);
  }
}

/**
 * `isAppearanceLightStatusBars` — note the Kotlin's own comment at DisplayExtensions.kt:236:
 * "Contrary to the documentation FALSE applies a light foreground color and TRUE a dark foreground
 * color". So a LIGHT background wants DARK icons.
 */
export function applyStatusBarStyle(lightBackground: boolean): void {
  if (mainWindow === null || lastLightStatusBar === lightBackground) {
    return;
  }
  lastLightStatusBar = lightBackground;
  try {
    mainWindow.setWindowSystemBarProperties({
      statusBarColor: '#00000000',
      navigationBarColor: '#00000000',
      statusBarContentColor: lightBackground ? '#000000' : '#FFFFFF',
      navigationBarContentColor: lightBackground ? '#000000' : '#FFFFFF'
    });
  } catch (error) {
    console.error('status bar style failed: ' + (error as Error).message);
  }
}
