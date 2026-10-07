/*
 * The saved-location list and the weather cache, in a relational store.
 *
 * CLAUDE.md maps SQLDelight to `relationalStore` (ArkTS) / the C database API (C++). ArkUIApp has
 * always used `relationalStore`; this port did NOT — `shell/WeatherCache.ets` and
 * `shell/Locations.ets` wrote plain text files through `fileIo`.
 *
 * WHY THAT MATTERED ENOUGH TO REWRITE: the whole point of this project is comparing ArkUI's memory
 * against the NDK's. `relationalStore` pulls in a database engine with its own caches and page
 * pool; `fileIo` does not. With one port on each, a slice of any PSS or native-heap delta was the
 * STORAGE LAYER rather than the UI framework — the one variable the comparison is supposed to
 * isolate. Both ports now sit on the same engine.
 *
 * It is the same engine, not merely an equivalent one: OHOS's `database/rdb` native API
 * (`libnative_rdb_ndk`) is the C surface of the very store `@ohos.data.relationalStore` exposes to
 * ArkTS. There is no `sqlite3.h` in the NDK sysroot to reach past it, and reaching past it would
 * have defeated the purpose anyway.
 *
 * Schema mirrors ArkUIApp's two tables (data/LocationTable.ets, data/WeatherCache.ets) so the two
 * ports store the same rows in the same shape:
 *
 *   locations(formattedId TEXT PRIMARY KEY, position INTEGER, payload TEXT)
 *   weather_cache(formattedId TEXT PRIMARY KEY, kind TEXT, payload TEXT, updatedAt INTEGER)
 *
 * `payload` is the same opaque string the shell used to write to its files, so the migration is a
 * change of container and not of format.
 */

#ifndef BREEZY_DATA_DATABASE_H
#define BREEZY_DATA_DATABASE_H

#include <cstdint>
#include <string>
#include <vector>

namespace breezy {

struct StoredLocation {
    std::string formattedId;
    /** Its place in the saved list; the list is read back ordered by this. */
    int position = 0;
    /** The tab-separated row the shell already used — opaque here. */
    std::string payload;
};

/**
 * Opens (creating if needed) the store under the app's own files directory.
 *
 * `filesDir` is what the shell passes down from `context.filesDir`; the store is created beside the
 * files the port used to write, so an install that had them can be migrated in place.
 * Returns false when the store could not be opened — every call below then no-ops rather than
 * crashing, which is the behaviour the file-based code had when a read failed.
 */
bool DatabaseOpen(const std::string& filesDir, const std::string& bundleName);
void DatabaseClose();
bool DatabaseIsOpen();

/* ---- locations ------------------------------------------------------------------------ */

std::vector<StoredLocation> DatabaseReadLocations();
/** Replaces the whole list in one transaction — the shell always writes it whole. */
bool DatabaseWriteLocations(const std::vector<StoredLocation>& locations);

/* ---- weather cache -------------------------------------------------------------------- */

/** `kind` separates the two payloads a location has: "forecast" and "alerts". */
std::string DatabaseReadWeather(const std::string& formattedId, const std::string& kind);
bool DatabaseWriteWeather(const std::string& formattedId, const std::string& kind,
                          const std::string& payload, int64_t updatedAtMs);
/** When the row was written, or 0 when there is none. */
int64_t DatabaseWeatherUpdatedAt(const std::string& formattedId, const std::string& kind);
bool DatabaseDeleteWeather(const std::string& formattedId);

/* ---- small single values (the card order, and anything else the shell parked in a file) -- */

std::string DatabaseReadSetting(const std::string& key);
bool DatabaseWriteSetting(const std::string& key, const std::string& value);

}  // namespace breezy

#endif  // BREEZY_DATA_DATABASE_H
