#include "data/FmiAlerts.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "common/LocalTime.h"

namespace breezy {
namespace {

/*
 * A tag scanner, not an XML parser.
 *
 * CAP documents are machine-generated, namespace-free at the levels we read, and 650 KB of which
 * ~640 KB is <polygon> coordinate soup we never touch. Walking them by element name costs one
 * pass and no allocation per node; a DOM would materialise every polygon just to throw it away.
 */

/** Finds `<name>` after `from`. Returns false when absent; otherwise the inner text range. */
bool FindElement(const std::string& doc, const std::string& name, size_t from, size_t& innerStart,
                 size_t& innerEnd, size_t& afterClose) {
    const std::string open = "<" + name;
    const std::string close = "</" + name + ">";
    size_t at = from;
    while (true) {
        at = doc.find(open, at);
        if (at == std::string::npos) {
            return false;
        }
        // Guard against <areaDesc> matching <area>: the next char must end the tag name.
        const char next = at + open.size() < doc.size() ? doc[at + open.size()] : '\0';
        if (next == '>' || next == ' ' || next == '\t' || next == '\n' || next == '/') {
            break;
        }
        at += open.size();
    }
    const size_t tagEnd = doc.find('>', at);
    if (tagEnd == std::string::npos) {
        return false;
    }
    if (tagEnd > 0 && doc[tagEnd - 1] == '/') {  // <name/> — empty element.
        innerStart = innerEnd = tagEnd;
        afterClose = tagEnd + 1;
        return true;
    }
    const size_t closeAt = doc.find(close, tagEnd);
    if (closeAt == std::string::npos) {
        return false;
    }
    innerStart = tagEnd + 1;
    innerEnd = closeAt;
    afterClose = closeAt + close.size();
    return true;
}

std::string Trim(const std::string& text) {
    size_t start = 0;
    size_t end = text.size();
    while (start < end && std::isspace(static_cast<unsigned char>(text[start]))) {
        ++start;
    }
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return text.substr(start, end - start);
}

/** The text of the first `<name>` inside [from, limit), or empty. */
std::string TextOf(const std::string& doc, const std::string& name, size_t from, size_t limit) {
    size_t innerStart = 0;
    size_t innerEnd = 0;
    size_t afterClose = 0;
    if (!FindElement(doc, name, from, innerStart, innerEnd, afterClose) || innerStart >= limit) {
        return {};
    }
    return Trim(doc.substr(innerStart, innerEnd - innerStart));
}

/** Replaces every occurrence of `from` with `to`, in place. */
void ReplaceAll(std::string& text, const std::string& from, const std::string& to) {
    if (from.empty()) {
        return;
    }
    size_t at = 0;
    while ((at = text.find(from, at)) != std::string::npos) {
        text.replace(at, from.size(), to);
        at += to.size();
    }
}

/**
 * CAP text → what the alert screen prints. The twin of ArkUIApp's `decodeCapText`.
 *
 * Android runs `AnnotatedString.fromHtml(text.replace("\n", "<br />"))` (AlertScreen.kt:182-184),
 * so the description is HTML. For FMI it is plain prose, but the CAP payload still arrives
 * XML-escaped, and a source that does emit markup must not leak raw tags into the text. So: block
 * tags become line breaks, every other tag is dropped, then the entities are decoded.
 *
 * `formatAlertText` (CapAlert.kt:245-280) also has an NWS and a CWA branch; both are keyed off
 * `senderName` and neither can fire on the FMI feed, the only alert source either port implements.
 */
std::string DecodeCapText(const std::string& raw) {
    std::string out;
    out.reserve(raw.size());

    // Strip tags, turning the block-level ones into line breaks.
    for (size_t i = 0; i < raw.size();) {
        if (raw[i] != '<') {
            out.push_back(raw[i]);
            ++i;
            continue;
        }
        const size_t close = raw.find('>', i);
        if (close == std::string::npos) {
            out.push_back(raw[i]);
            ++i;
            continue;
        }
        std::string tag = raw.substr(i + 1, close - i - 1);
        if (!tag.empty() && tag[0] == '/') {
            tag.erase(0, 1);
        }
        const size_t space = tag.find_first_of(" \t\n/");
        if (space != std::string::npos) {
            tag = tag.substr(0, space);
        }
        for (char& c : tag) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        if (tag == "br" || tag == "p" || tag == "div" || tag == "li" || tag == "h1" ||
            tag == "h2" || tag == "h3" || tag == "h4" || tag == "h5" || tag == "h6") {
            out.push_back('\n');
        }
        i = close + 1;
    }

    ReplaceAll(out, "&lt;", "<");
    ReplaceAll(out, "&gt;", ">");
    ReplaceAll(out, "&quot;", "\"");
    ReplaceAll(out, "&apos;", "'");
    ReplaceAll(out, "&#39;", "'");
    ReplaceAll(out, "&nbsp;", " ");
    // Last: an escaped ampersand must not re-open one of the entities above.
    ReplaceAll(out, "&amp;", "&");
    return Trim(out);
}

bool EqualsIgnoreCase(const std::string& a, const char* b) {
    size_t i = 0;
    for (; i < a.size() && b[i] != '\0'; ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return i == a.size() && b[i] == '\0';
}

/**
 * An ISO-8601 instant with an offset, e.g. 2026-08-07T11:00:00+03:00 — CAP's onset/expires form.
 * Returns 0 when unparseable, which the caller treats as absent.
 */
int64_t ParseIso8601(const std::string& text) {
    if (text.size() < 19) {
        return 0;
    }
    const auto number = [&text](size_t at, size_t length) {
        return std::atoi(text.substr(at, length).c_str());
    };
    const int year = number(0, 4);
    const int month = number(5, 2);
    const int day = number(8, 2);
    const int hour = number(11, 2);
    const int minute = number(14, 2);
    const int second = number(17, 2);

    // days_from_civil — Howard Hinnant's algorithm, the inverse of LocalTime's CivilFromDays.
    const int y = year - (month <= 2 ? 1 : 0);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const int yoe = y - era * 400;
    const int mp = month > 2 ? month - 3 : month + 9;
    const int doy = (153 * mp + 2) / 5 + day - 1;
    const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const int64_t days = static_cast<int64_t>(era) * 146097 + doe - 719468;

    int64_t ms = days * kMillisPerDay + hour * kMillisPerHour + minute * kMillisPerMinute +
                 second * kMillisPerSecond;

    // Trailing zone: Z, or ±HH:MM which has to be subtracted to reach UTC.
    const size_t zone = text.find_first_of("Z+-", 19);
    if (zone != std::string::npos && text[zone] != 'Z' && zone + 5 < text.size()) {
        const int offsetHours = std::atoi(text.substr(zone + 1, 2).c_str());
        const int offsetMinutes = std::atoi(text.substr(zone + 4, 2).c_str());
        const int64_t offset = offsetHours * kMillisPerHour + offsetMinutes * kMillisPerMinute;
        ms += text[zone] == '+' ? -offset : offset;
    }
    return ms;
}

/** AlertSeverity → the triangle's tint. colors.xml level ramp, worst first (FmiService kt). */
uint32_t SeverityColor(const std::string& severity) {
    if (EqualsIgnoreCase(severity, "Extreme")) {
        return 0xFFE52F35;
    }
    if (EqualsIgnoreCase(severity, "Severe")) {
        return 0xFFFFA726;
    }
    if (EqualsIgnoreCase(severity, "Moderate")) {
        return 0xFFFFCA28;
    }
    return 0xFF72D572;  // Minor / Unknown
}

}  // namespace

std::vector<std::string> ParseAlertFeed(const std::string& rss) {
    std::vector<std::string> links;
    size_t at = 0;
    while (true) {
        size_t itemStart = 0;
        size_t itemEnd = 0;
        size_t afterItem = 0;
        if (!FindElement(rss, "item", at, itemStart, itemEnd, afterItem)) {
            break;
        }
        const std::string link = TextOf(rss, "link", itemStart, itemEnd);
        if (!link.empty()) {
            links.push_back(link);
        }
        at = afterItem;
    }
    return links;
}

std::vector<Alert> ParseCapAlert(const std::string& cap, const std::string& region,
                                 int64_t nowMs) {
    std::vector<Alert> alerts;
    // kt — a Cancel message withdraws a warning rather than raising one.
    if (EqualsIgnoreCase(TextOf(cap, "msgType", 0, cap.size()), "Cancel")) {
        return alerts;
    }

    // One <info> per language; take the English one, as getInfoForContext does for an en locale.
    size_t at = 0;
    while (true) {
        size_t infoStart = 0;
        size_t infoEnd = 0;
        size_t afterInfo = 0;
        if (!FindElement(cap, "info", at, infoStart, infoEnd, afterInfo)) {
            break;
        }
        at = afterInfo;

        const std::string language = TextOf(cap, "language", infoStart, infoEnd);
        if (language.rfind("en", 0) != 0) {
            continue;
        }

        // The warning applies here only if one of its areas names this region.
        bool matches = false;
        size_t areaAt = infoStart;
        while (!matches) {
            size_t areaStart = 0;
            size_t areaEnd = 0;
            size_t afterArea = 0;
            if (!FindElement(cap, "area", areaAt, areaStart, areaEnd, afterArea) ||
                areaStart >= infoEnd) {
                break;
            }
            areaAt = afterArea;
            const std::string areaDesc = TextOf(cap, "areaDesc", areaStart, areaEnd);
            if (!areaDesc.empty() && !region.empty() &&
                areaDesc.find(region) != std::string::npos) {
                matches = true;
            }
        }
        if (!matches) {
            continue;
        }

        Alert alert;
        // <event> is the short name Breezy shows ("Wind warning for land areas"); <headline> is
        // the feed's long title with the region and times baked in, which the card would truncate.
        alert.headline = DecodeCapText(TextOf(cap, "event", infoStart, infoEnd));
        if (alert.headline.empty()) {
            alert.headline = DecodeCapText(TextOf(cap, "headline", infoStart, infoEnd));
        }
        /*
         * `capAlert.identifier` (FmiService.kt:396). The document-level <identifier> sits OUTSIDE
         * <info>, so it is searched from 0, not from the info block.
         */
        alert.alertId = TextOf(cap, "identifier", 0, cap.size());
        alert.description = DecodeCapText(TextOf(cap, "description", infoStart, infoEnd));
        alert.instruction = DecodeCapText(TextOf(cap, "instruction", infoStart, infoEnd));
        alert.source = DecodeCapText(TextOf(cap, "senderName", infoStart, infoEnd));
        alert.startDate = ParseIso8601(TextOf(cap, "onset", infoStart, infoEnd));
        alert.endDate = ParseIso8601(TextOf(cap, "expires", infoStart, infoEnd));
        alert.hasStart = alert.startDate != 0;
        alert.hasEnd = alert.endDate != 0;
        const std::string severity = TextOf(cap, "severity", infoStart, infoEnd);
        alert.color = SeverityColor(severity);
        if (alert.alertId.empty()) {
            // `Objects.hash(title, severity, start)` (kt:396) — any stable per-alert key will do.
            alert.alertId = alert.headline + "|" + severity + "|" + std::to_string(alert.startDate);
        }

        // An expired warning is not shown; MainAdapter.kt:107-109 gates the card on that too.
        if (alert.hasEnd && alert.endDate <= nowMs) {
            continue;
        }
        if (!alert.headline.empty()) {
            alerts.push_back(std::move(alert));
        }
        break;  // One English info block per document.
    }
    return alerts;
}

}  // namespace breezy
