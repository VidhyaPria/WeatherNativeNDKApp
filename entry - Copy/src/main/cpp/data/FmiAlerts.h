/*
 * FMI weather warnings, from the Common Alerting Protocol feed.
 *
 * Open-Meteo publishes no alerts — OpenMeteoService.kt declares FORECAST, CURRENT, AIR_QUALITY,
 * POLLEN and MINUTELY and no SourceFeature.ALERT — so Breezy's alert card is fed by a SECOND
 * source. On the reference Android device that source is FMI, which is why its home page shows
 * "Upcoming alerts, tap here to see them" and the HarmonyOS ports did not.
 *
 * Ported from src_nonfreenet/.../fmi/FmiService.kt (getAlertList) plus FmiAlertsApi:
 *   1. GET alerts.fmi.fi/cap/feed/rss_en-GB.rss   — an index of <item><link> CAP documents
 *   2. GET each link                              — one CAP XML per warning
 *   3. skip msgType=Cancel, take the English <info>, match the location by <areaDesc>
 *
 * The shell does both fetches because it owns the HTTP; this parses what comes back. CAP files
 * are ~650 KB each (mostly area polygons), so the shell caches them by URL — the URL carries an
 * issue timestamp and a hash, so a given document never changes.
 */
#ifndef BREEZY_DATA_FMIALERTS_H
#define BREEZY_DATA_FMIALERTS_H

#include <cstdint>
#include <string>
#include <vector>

#include "model/Weather.h"

namespace breezy {

/** The CAP document URLs listed in the RSS index, in feed order. */
std::vector<std::string> ParseAlertFeed(const std::string& rss);

/**
 * One CAP document → zero or more alerts for this location.
 *
 * `region` is the location's admin area (Location.admin1 on Android); an alert applies when one
 * of its <areaDesc> entries names that region, which is the fast path FmiService takes before
 * falling back to polygon containment.
 */
std::vector<Alert> ParseCapAlert(const std::string& cap, const std::string& region,
                                 int64_t nowMs);

}  // namespace breezy

#endif  // BREEZY_DATA_FMIALERTS_H
