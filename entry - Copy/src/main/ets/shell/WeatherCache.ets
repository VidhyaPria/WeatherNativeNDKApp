/*
 * The response cache.
 *
 * ArkUIApp stores a mapped Weather in relationalStore (data/WeatherCache.ets) so the page has
 * something to draw before the network answers. This port caches the RAW response bodies instead
 * and lets C++ re-map them, because the mapping already lives there — storing a mapped model
 * would mean a second serialiser on the C++ side and a second chance for the two to disagree.
 *
 * STORAGE — this used to be plain files, and the comment here used to justify that by saying a
 * database would be weight the memory comparison would pick up. That reasoning was backwards, and
 * it is why this was rewritten:
 *
 * ArkUIApp stores its cache in `relationalStore`, as CLAUDE.md's SQLDelight mapping requires. With
 * this port on flat files, the two ports differed by a whole database engine — its caches and its
 * page pool — and that difference landed in exactly the PSS and native-heap numbers the project
 * exists to compare. Avoiding the database here did not make the comparison cleaner; it put the
 * storage layer INTO the measurement. Both ports now sit on the same engine, so what is left of any
 * delta is the UI framework, which is the variable being isolated.
 *
 * The rows still hold the RAW response bodies and C++ still re-maps them — that part was right and
 * is unchanged. Only the container moved. See cpp/data/Database.h.
 */

import { hilog } from '@kit.PerformanceAnalysisKit';
import native from 'libentry.so';

const DOMAIN: number = 0x0000;
const TAG: string = 'BreezyShell';

/** How long a cached response may still be shown. RefreshHelper treats older data as absent. */
const MAX_AGE_MS: number = 3 * 60 * 60 * 1000;

/** The two payloads a location has. `kind` is the second half of the cache row's primary key. */
const KIND_FORECAST: string = 'forecast';
const KIND_AIR: string = 'air';

export class CachedForecast {
  readonly forecast: string;
  readonly airQuality: string;

  constructor(forecast: string, airQuality: string) {
    this.forecast = forecast;
    this.airQuality = airQuality;
  }
}

/**
 * A cache read that still honours MAX_AGE_MS.
 *
 * The file version got this from the file's mtime; the row carries `updatedAt` instead, written at
 * the same instant as the payload. Keeping the gate matters: RefreshHelper treats older data as
 * ABSENT, so dropping it would have made stale forecasts render as if fresh.
 */
function readFresh(locationId: string, kind: string): string {
  const written: number = native.dbWeatherUpdatedAt(locationId, kind);
  if (written <= 0 || new Date().getTime() - written > MAX_AGE_MS) {
    return '';
  }
  return native.dbReadWeather(locationId, kind);
}

/**
 * Opens the store. Called once, from the shell's startup, before anything reads.
 *
 * A failure is not fatal and is not thrown: every read then returns "" and every write is dropped,
 * which is exactly how the file version behaved when a read or write failed — the app runs, it
 * just has no cache.
 */
export function openStore(filesDir: string, bundleName: string): void {
  if (!native.dbOpen(filesDir, bundleName)) {
    hilog.warn(DOMAIN, TAG, 'could not open the store; running without a cache');
  }
}

/**
 * How old this location's cached forecast is, in ms, or -1 when there is none.
 *
 * Stands in for `Weather.refreshTime`: the file's mtime is when the response was written. It is
 * what `checkToUpdateCurrentLocation` (MainActivityViewModel.kt:279-283) tests before deciding
 * whether to fetch at all — "if already valid, just return".
 */
export function cacheAgeMs(filesDir: string, locationId: string): number {
  const written: number = native.dbWeatherUpdatedAt(locationId, KIND_FORECAST);
  return written > 0 ? new Date().getTime() - written : -1;
}

/** Returns empty strings when there is no usable entry, which the caller treats as a miss. */
export function readCache(filesDir: string, locationId: string): CachedForecast {
  return new CachedForecast(
    readFresh(locationId, KIND_FORECAST),
    readFresh(locationId, KIND_AIR)
  );
}

/**
 * The card order, persisted the way SettingsManager persists it on Android — it has to survive
 * the process, or every relaunch would undo the user's arrangement.
 */
export function readCardOrder(filesDir: string): string {
  return native.dbReadSetting('card_order');
}

export function writeCardOrder(filesDir: string, order: string): void {
  native.dbWriteSetting('card_order', order);
}

/**
 * The saved-location list itself, one location per line as `id\tcity\tregion\tlat\tlon`.
 *
 * ArkUIApp keeps this in relationalStore (data/LocationTable.ets) because its rows carry the eight
 * per-feature source ids and a country code, and it queries them. This port's ShellLocation is five
 * fields with no queries over it — the same reasoning that keeps the response cache in flat files.
 *
 * A tab is the separator because a location name may contain a comma (and does: "Washington, D.C.")
 * but never a tab.
 */
export function readLocationList(filesDir: string): string {
  return native.dbReadLocations();
}

/**
 * The list is written WHOLE, in one transaction.
 *
 * The rows go over as two newline-joined, index-aligned strings — the ids so the table has a
 * primary key, and the payloads verbatim so the tab-separated format above is unchanged. The
 * FIRST field of each line is the id, which is what makes the split cheap on this side.
 */
export function writeLocationList(filesDir: string, text: string): void {
  const lines: string[] = text.length > 0 ? text.split('\n') : [];
  const ids: string[] = [];
  const payloads: string[] = [];
  for (const line of lines) {
    if (line.length === 0) {
      continue;
    }
    const tab: number = line.indexOf('\t');
    ids.push(tab < 0 ? line : line.substring(0, tab));
    payloads.push(line);
  }
  native.dbWriteLocations(ids.join('\n'), payloads.join('\n'));
}

/**
 * CAP documents, cached by URL.
 *
 * A CAP URL carries the issue timestamp and a content hash, so the document behind it never
 * changes — a hit is always valid, and at ~650 KB apiece that is the difference between a few
 * megabytes per refresh and none. The file name is a hash of the URL because the URL itself is
 * far too long, and contains slashes.
 */
function keyFor(url: string): string {
  let hash: number = 5381;
  for (let i = 0; i < url.length; i++) {
    hash = ((hash * 33) ^ url.charCodeAt(i)) >>> 0;
  }
  return 'cap-' + hash.toString(16);
}

export function readCapDocument(filesDir: string, url: string): string {
  return native.dbReadSetting(keyFor(url));
}

export function writeCapDocument(filesDir: string, url: string, document: string): void {
  native.dbWriteSetting(keyFor(url), document);
}

export function writeCache(
  filesDir: string,
  locationId: string,
  forecast: string,
  airQuality: string
): void {
  const now: number = new Date().getTime();
  native.dbWriteWeather(locationId, KIND_FORECAST, forecast, now);
  if (airQuality.length > 0) {
    native.dbWriteWeather(locationId, KIND_AIR, airQuality, now);
  }
}
