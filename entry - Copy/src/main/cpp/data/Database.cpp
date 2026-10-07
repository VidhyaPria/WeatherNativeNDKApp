#include "data/Database.h"

#include <database/rdb/oh_cursor.h>
#include <database/rdb/relational_store.h>
#include <hilog/log.h>

#include <cstring>
#include <mutex>

#undef LOG_TAG
#define LOG_TAG "BreezyNative"

namespace breezy {
namespace {

OH_Rdb_Store* g_store = nullptr;
std::mutex g_mutex;

/**
 * The store name. ArkUIApp's two tables live in `breezy.db` (LocationTable.ets:22-26); this uses
 * the same name in its own sandbox, so the two ports are directly comparable without sharing a
 * file — they are separate bundles and cannot see each other's data anyway.
 */
constexpr const char* kStoreName = "breezy.db";

/** Doubles every quote — the payloads are opaque and can contain anything. */
std::string Quote(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 2);
    out.push_back('\'');
    for (const char c : value) {
        if (c == '\'') {
            out.push_back('\'');
        }
        out.push_back(c);
    }
    out.push_back('\'');
    return out;
}

/** Reads column 0 of the first row as text, or "" when the query returned nothing. */
std::string FirstText(OH_Cursor* cursor) {
    if (cursor == nullptr) {
        return "";
    }
    std::string result;
    if (cursor->goToNextRow(cursor) == 0) {
        size_t size = 0;
        if (cursor->getSize(cursor, 0, &size) == 0 && size > 0) {
            // getSize reports the length INCLUDING the terminator, which is what getText wants.
            std::string buffer(size, '\0');
            if (cursor->getText(cursor, 0, buffer.data(), static_cast<int>(size)) == 0) {
                result.assign(buffer.c_str());
            }
        }
    }
    cursor->destroy(cursor);
    return result;
}

bool Exec(const std::string& sql) {
    if (g_store == nullptr) {
        return false;
    }
    const int rc = OH_Rdb_Execute(g_store, sql.c_str());
    if (rc != 0) {
        OH_LOG_ERROR(LOG_APP, "rdb exec failed (%{public}d)", rc);
        return false;
    }
    return true;
}

}  // namespace

bool DatabaseOpen(const std::string& filesDir, const std::string& bundleName) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_store != nullptr) {
        return true;
    }
    if (filesDir.empty()) {
        return false;
    }

    /*
     * V1 config and OH_Rdb_GetOrOpen, not the V2 pair: V2 is `introduced=14.0.0` and this port
     * targets the same SDK the rest of the tree does. `#pragma pack(1)` on the struct is why
     * selfSize has to be set — the library uses it to tell the versions apart.
     */
    OH_Rdb_Config config;
    std::memset(&config, 0, sizeof(config));
    config.selfSize = sizeof(OH_Rdb_Config);
    config.dataBaseDir = filesDir.c_str();
    config.storeName = kStoreName;
    config.bundleName = bundleName.c_str();
    config.moduleName = "entry";
    config.isEncrypt = false;
    config.securityLevel = 1;  // S1 — the same level a weather cache warrants on the ArkTS side.
    config.area = 0;

    int errCode = 0;
    g_store = OH_Rdb_GetOrOpen(&config, &errCode);
    if (g_store == nullptr) {
        OH_LOG_ERROR(LOG_APP, "rdb open failed (%{public}d)", errCode);
        return false;
    }

    Exec("CREATE TABLE IF NOT EXISTS locations (formattedId TEXT PRIMARY KEY, "
         "position INTEGER NOT NULL DEFAULT 0, payload TEXT NOT NULL DEFAULT '')");
    Exec("CREATE TABLE IF NOT EXISTS weather_cache (formattedId TEXT NOT NULL, "
         "kind TEXT NOT NULL, payload TEXT NOT NULL DEFAULT '', "
         "updatedAt INTEGER NOT NULL DEFAULT 0, PRIMARY KEY (formattedId, kind))");
    Exec("CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, value TEXT NOT NULL "
         "DEFAULT '')");
    return true;
}

void DatabaseClose() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_store != nullptr) {
        OH_Rdb_CloseStore(g_store);
        g_store = nullptr;
    }
}

bool DatabaseIsOpen() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_store != nullptr;
}

std::vector<StoredLocation> DatabaseReadLocations() {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::vector<StoredLocation> result;
    if (g_store == nullptr) {
        return result;
    }
    OH_Cursor* cursor = OH_Rdb_ExecuteQuery(
        g_store, "SELECT formattedId, position, payload FROM locations ORDER BY position ASC");
    if (cursor == nullptr) {
        return result;
    }
    while (cursor->goToNextRow(cursor) == 0) {
        StoredLocation row;
        size_t size = 0;
        if (cursor->getSize(cursor, 0, &size) == 0 && size > 0) {
            std::string buffer(size, '\0');
            if (cursor->getText(cursor, 0, buffer.data(), static_cast<int>(size)) == 0) {
                row.formattedId.assign(buffer.c_str());
            }
        }
        int64_t position = 0;
        if (cursor->getInt64(cursor, 1, &position) == 0) {
            row.position = static_cast<int>(position);
        }
        size = 0;
        if (cursor->getSize(cursor, 2, &size) == 0 && size > 0) {
            std::string buffer(size, '\0');
            if (cursor->getText(cursor, 2, buffer.data(), static_cast<int>(size)) == 0) {
                row.payload.assign(buffer.c_str());
            }
        }
        if (!row.formattedId.empty()) {
            result.push_back(std::move(row));
        }
    }
    cursor->destroy(cursor);
    return result;
}

bool DatabaseWriteLocations(const std::vector<StoredLocation>& locations) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_store == nullptr) {
        return false;
    }
    /*
     * One transaction: the shell hands the list over whole, and a half-written list is worse than
     * the previous one — a crash between the DELETE and the last INSERT would otherwise lose
     * every saved location.
     */
    Exec("BEGIN TRANSACTION");
    bool ok = Exec("DELETE FROM locations");
    for (size_t i = 0; i < locations.size() && ok; ++i) {
        const StoredLocation& row = locations[i];
        ok = Exec("INSERT INTO locations (formattedId, position, payload) VALUES (" +
                  Quote(row.formattedId) + ", " + std::to_string(i) + ", " + Quote(row.payload) +
                  ")");
    }
    Exec(ok ? "COMMIT" : "ROLLBACK");
    return ok;
}

std::string DatabaseReadWeather(const std::string& formattedId, const std::string& kind) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_store == nullptr) {
        return "";
    }
    return FirstText(OH_Rdb_ExecuteQuery(
        g_store, ("SELECT payload FROM weather_cache WHERE formattedId = " + Quote(formattedId) +
                  " AND kind = " + Quote(kind))
                     .c_str()));
}

bool DatabaseWriteWeather(const std::string& formattedId, const std::string& kind,
                          const std::string& payload, int64_t updatedAtMs) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_store == nullptr) {
        return false;
    }
    return Exec("INSERT OR REPLACE INTO weather_cache (formattedId, kind, payload, updatedAt) "
                "VALUES (" +
                Quote(formattedId) + ", " + Quote(kind) + ", " + Quote(payload) + ", " +
                std::to_string(updatedAtMs) + ")");
}

int64_t DatabaseWeatherUpdatedAt(const std::string& formattedId, const std::string& kind) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_store == nullptr) {
        return 0;
    }
    OH_Cursor* cursor = OH_Rdb_ExecuteQuery(
        g_store, ("SELECT updatedAt FROM weather_cache WHERE formattedId = " + Quote(formattedId) +
                  " AND kind = " + Quote(kind))
                     .c_str());
    if (cursor == nullptr) {
        return 0;
    }
    int64_t value = 0;
    if (cursor->goToNextRow(cursor) == 0) {
        cursor->getInt64(cursor, 0, &value);
    }
    cursor->destroy(cursor);
    return value;
}

bool DatabaseDeleteWeather(const std::string& formattedId) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_store == nullptr) {
        return false;
    }
    return Exec("DELETE FROM weather_cache WHERE formattedId = " + Quote(formattedId));
}

std::string DatabaseReadSetting(const std::string& key) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_store == nullptr) {
        return "";
    }
    return FirstText(OH_Rdb_ExecuteQuery(
        g_store, ("SELECT value FROM settings WHERE key = " + Quote(key)).c_str()));
}

bool DatabaseWriteSetting(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_store == nullptr) {
        return false;
    }
    return Exec("INSERT OR REPLACE INTO settings (key, value) VALUES (" + Quote(key) + ", " +
                Quote(value) + ")");
}

}  // namespace breezy
