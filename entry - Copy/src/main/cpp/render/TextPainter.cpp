#include "render/TextPainter.h"

#include <hilog/log.h>
#include <native_drawing/drawing_font_collection.h>
#include <native_drawing/drawing_register_font.h>

#include <cmath>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "render/Theme.h"

#undef LOG_TAG
#define LOG_TAG "BreezyNative"

namespace breezy {
namespace {

/** Matches ArkUIApp's APP_FONT_FAMILY, so the ported layout names the same family. */
constexpr const char* kFontFamily = "Roboto";

float g_density = 1.0f;
bool g_fontRegistered = false;

/**
 * The engine's font collection. Shared rather than per-draw: creating one parses the system font
 * config, which is far too expensive to do per frame, and a registered font lives on the
 * collection — a fresh one would not have Roboto in it.
 */
OH_Drawing_FontCollection* FontCollection() {
    static OH_Drawing_FontCollection* collection = OH_Drawing_CreateSharedFontCollection();
    return collection;
}

/**
 * Which of the reference's TWO faces the current draw is in. See SystemFontScope in the header.
 *
 * Not thread-safe by design: everything this port draws runs on the render thread, one screen at a
 * time. The value is folded into the layout cache key rather than read only at paint time.
 */
bool g_systemFont = false;

}  // namespace

bool UsingSystemFont();

namespace {

/** One built typography, torn down by the destructor so no early return can leak it. */
class Typography {
public:
    Typography(const std::string& text, double widthPx, const TextStyleSpec& spec) {
        style_ = OH_Drawing_CreateTypographyStyle();
        int align = TEXT_ALIGN_LEFT;
        if (spec.alignment == TextAlignment::Center) {
            align = TEXT_ALIGN_CENTER;
        } else if (spec.alignment == TextAlignment::Right) {
            align = TEXT_ALIGN_RIGHT;
        }
        OH_Drawing_SetTypographyTextAlign(style_, align);
        if (spec.singleLine) {
            OH_Drawing_SetTypographyTextMaxLines(style_, 1);
        }

        textStyle_ = OH_Drawing_CreateTextStyle();
        OH_Drawing_SetTextStyleColor(textStyle_, spec.color);
        OH_Drawing_SetTextStyleFontSize(textStyle_, spec.sizeVp * g_density);
        /*
         * Material's line box, as a multiple of the font size — 24 sp on a 16 sp body, 44 on a
         * 36 sp displaySmall. Without it the engine uses the font's own leading, about 1.17x, so
         * every stacked-text layout came out roughly 20 % short of Android's and drifted further
         * with each line. Half leading splits the extra evenly above and below, which is what
         * Compose does for a Text with an explicit lineHeight.
         */
        if (spec.lineHeightVp > 0.0 && spec.sizeVp > 0.0) {
            OH_Drawing_SetTextStyleFontHeight(textStyle_, spec.lineHeightVp / spec.sizeVp);
            OH_Drawing_SetTextStyleHalfLeading(textStyle_, true);
        }
        if (spec.letterSpacingVp != 0.0) {
            OH_Drawing_SetTextStyleLetterSpacing(textStyle_, spec.letterSpacingVp * g_density);
        }
        OH_Drawing_SetTextStyleFontWeight(
            textStyle_,
            spec.bold ? FONT_WEIGHT_700 : (spec.medium ? FONT_WEIGHT_500 : FONT_WEIGHT_400));
        // Leaving the family unset is what selects the SYSTEM face — HarmonyOS Sans on both the
        // reference device and this one. See UsingSystemFont().
        if (g_fontRegistered && !UsingSystemFont()) {
            const char* families[] = {kFontFamily};
            OH_Drawing_SetTextStyleFontFamilies(textStyle_, 1, families);
        }
        if (spec.singleLine) {
            OH_Drawing_SetTextStyleEllipsis(textStyle_, "…");
            OH_Drawing_SetTextStyleEllipsisModal(textStyle_, ELLIPSIS_MODAL_TAIL);
        }

        handler_ = OH_Drawing_CreateTypographyHandler(style_, FontCollection());
        OH_Drawing_TypographyHandlerPushTextStyle(handler_, textStyle_);
        OH_Drawing_TypographyHandlerAddText(handler_, text.c_str());
        typography_ = OH_Drawing_CreateTypography(handler_);
        OH_Drawing_TypographyLayout(typography_, widthPx);
    }

    ~Typography() {
        OH_Drawing_DestroyTypography(typography_);
        OH_Drawing_DestroyTypographyHandler(handler_);
        OH_Drawing_DestroyTextStyle(textStyle_);
        OH_Drawing_DestroyTypographyStyle(style_);
    }

    Typography(const Typography&) = delete;
    Typography& operator=(const Typography&) = delete;

    double Height() const { return OH_Drawing_TypographyGetHeight(typography_); }
    double LongestLine() const { return OH_Drawing_TypographyGetLongestLine(typography_); }

    void Paint(OH_Drawing_Canvas* canvas, double x, double y) const {
        OH_Drawing_TypographyPaint(typography_, canvas, x, y);
    }

private:
    OH_Drawing_TypographyStyle* style_ = nullptr;
    OH_Drawing_TextStyle* textStyle_ = nullptr;
    OH_Drawing_TypographyCreate* handler_ = nullptr;
    OH_Drawing_Typography* typography_ = nullptr;
};

/*
 * ---------------------------------------------------------------- the layout cache
 *
 * Building a Typography means creating a typography style, a text style and a handler, pushing the
 * run, then laying it out — and the old code did all of that for EVERY label on EVERY frame, twice
 * over wherever a caller measured before drawing. A traced horizontal fling put the whole
 * RenderFrame at 37 ms average and 84 ms worst, all of it self time in this file's callers.
 *
 * ArkUI does not re-layout a Text component whose content and constraints have not changed, and
 * neither does Android: TextView keeps its Layout until something invalidates it. So the port keeps
 * the laid-out typography, keyed by everything that can change the result, and a frame that draws
 * the same labels again only pays for the paint.
 *
 * The cache is bounded so it cannot quietly become a leak — this project exists to measure memory.
 * Entries are evicted least-recently-used.
 */
struct CacheKey {
    std::string text;
    double widthPx = 0.0;
    double sizeVp = 0.0;
    double letterSpacingVp = 0.0;
    double lineHeightVp = 0.0;
    uint32_t color = 0;
    bool bold = false;
    bool medium = false;
    bool singleLine = false;
    bool systemFamily = false;
    TextAlignment alignment = TextAlignment::Left;

    bool operator==(const CacheKey& other) const {
        return text == other.text && widthPx == other.widthPx && sizeVp == other.sizeVp &&
               letterSpacingVp == other.letterSpacingVp && lineHeightVp == other.lineHeightVp &&
               color == other.color && bold == other.bold && medium == other.medium &&
               singleLine == other.singleLine &&
               systemFamily == other.systemFamily && alignment == other.alignment;
    }
};

struct CacheKeyHash {
    size_t operator()(const CacheKey& key) const {
        size_t hash = std::hash<std::string>()(key.text);
        const auto mix = [&hash](size_t value) {
            hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
        };
        mix(std::hash<double>()(key.widthPx));
        mix(std::hash<double>()(key.sizeVp));
        mix(std::hash<double>()(key.letterSpacingVp));
        mix(std::hash<double>()(key.lineHeightVp));
        mix(std::hash<uint32_t>()(key.color));
        mix(static_cast<size_t>(key.bold) | (static_cast<size_t>(key.medium) << 1) |
            (static_cast<size_t>(key.singleLine) << 2) |
            (static_cast<size_t>(key.systemFamily) << 3) |
            (static_cast<size_t>(key.alignment) << 4));
        return hash;
    }
};

/**
 * Enough for every label the page can show at once with room for a tab switch and a swipe to a
 * neighbouring location, and small enough that the whole cache is well under a megabyte.
 */
constexpr size_t kMaxEntries = 512;

struct CacheEntry {
    std::unique_ptr<Typography> typography;
    std::list<CacheKey>::iterator order;
};

std::mutex g_cacheMutex;
std::unordered_map<CacheKey, CacheEntry, CacheKeyHash> g_cache;
/** Front = most recently used. */
std::list<CacheKey> g_order;

/**
 * A width the layout cannot distinguish. Callers hand in whatever the layout arithmetic produced,
 * which drifts in the last few bits from frame to frame; rounding to a whole pixel keys on the
 * value the engine actually laid out to.
 */
double QuantizeWidth(double widthPx) {
    if (!(widthPx > 0.0)) {
        return 0.0;
    }
    // MeasureTextWidth's "very wide box" must not be rounded into the ordinary range.
    return std::floor(widthPx + 0.5);
}

CacheKey KeyFor(const std::string& text, double widthPx, const TextStyleSpec& spec) {
    CacheKey key;
    key.text = text;
    key.widthPx = QuantizeWidth(widthPx);
    key.sizeVp = spec.sizeVp;
    key.letterSpacingVp = spec.letterSpacingVp;
    key.lineHeightVp = spec.lineHeightVp;
    key.color = spec.color;
    key.bold = spec.bold;
    key.medium = spec.medium;
    key.singleLine = spec.singleLine;
    // Part of the KEY, not a global read at paint time: the same string at the same size in the
    // two faces lays out to different widths, and a cache that could not tell them apart would
    // hand a settings screen the main screen's Roboto metrics.
    key.systemFamily = UsingSystemFont();
    key.alignment = spec.alignment;
    return key;
}

/** The laid-out typography for this text and style, building it only on a miss. */
const Typography* Lookup(const std::string& text, double widthPx, const TextStyleSpec& spec) {
    const CacheKey key = KeyFor(text, widthPx, spec);
    std::lock_guard<std::mutex> lock(g_cacheMutex);

    auto found = g_cache.find(key);
    if (found != g_cache.end()) {
        g_order.splice(g_order.begin(), g_order, found->second.order);
        return found->second.typography.get();
    }

    if (g_cache.size() >= kMaxEntries) {
        const CacheKey& oldest = g_order.back();
        g_cache.erase(oldest);
        g_order.pop_back();
    }

    CacheEntry entry;
    entry.typography = std::make_unique<Typography>(text, key.widthPx, spec);
    g_order.push_front(key);
    entry.order = g_order.begin();
    const Typography* result = entry.typography.get();
    g_cache.emplace(key, std::move(entry));
    return result;
}

/**
 * Everything laid out so far is invalid once the typeface or the density changes, because both
 * feed the layout the cache is keyed against but neither is part of the key.
 */
void DropCache() {
    std::lock_guard<std::mutex> lock(g_cacheMutex);
    g_cache.clear();
    g_order.clear();
}

}  // namespace

bool RegisterAppFont(const uint8_t* data, size_t length) {
    if (g_fontRegistered) {
        return true;
    }
    if (data == nullptr || length == 0) {
        return false;
    }
    // The engine copies the buffer, but the signature is non-const, so it needs a mutable one.
    static std::vector<uint8_t> owned;
    owned.assign(data, data + length);
    const uint32_t status =
        OH_Drawing_RegisterFontBuffer(FontCollection(), kFontFamily, owned.data(), owned.size());
    if (status != 0) {
        OH_LOG_ERROR(LOG_APP, "RegisterFontBuffer failed: %{public}u", status);
        owned.clear();
        owned.shrink_to_fit();
        return false;
    }
    g_fontRegistered = true;
    DropCache();
    OH_LOG_INFO(LOG_APP, "registered %{public}s (%{public}zu bytes)", kFontFamily, length);
    return true;
}

void SetDisplayDensity(float density) {
    if (density > 0.0f && density != g_density) {
        g_density = density;
        DropCache();
    }
}

float DisplayDensity() {
    return g_density;
}

double DrawText(OH_Drawing_Canvas* canvas, const std::string& text, double xPx, double yPx,
                double widthPx, const TextStyleSpec& style) {
    if (text.empty()) {
        return 0.0;
    }
    const Typography* typography = Lookup(text, widthPx, style);
    typography->Paint(canvas, xPx, yPx);
    return typography->Height();
}

int MeasureLineCount(const std::string& text, double widthPx, const TextStyleSpec& style) {
    if (text.empty()) {
        return 0;
    }
    const double one = Lookup("Wg", 1.0e6, style)->Height();
    if (!(one > 0.0)) {
        return 1;
    }
    const double total = Lookup(text, widthPx, style)->Height();
    return std::max(1, static_cast<int>(total / one + 0.5));
}

double DrawTextFixed(OH_Drawing_Canvas* canvas, const std::string& text, double xPx, double yPx,
                     double widthPx, const TextStyleSpec& style, double lineHeightPx) {
    /*
     * The line height belongs to the STYLE, not to a box drawn around the finished paragraph: a
     * five-line card has to put 24 dp between every pair of lines, not stack five 1.17x lines and
     * centre the block. Setting it on the spec lets the engine lay each line out at Material's
     * height, and the returned typography height is then already lines x lineHeight.
     */
    TextStyleSpec spec = style;
    spec.lineHeightVp = lineHeightPx / DisplayDensity();
    if (text.empty()) {
        return lineHeightPx;
    }
    const Typography* typography = Lookup(text, widthPx, spec);
    typography->Paint(canvas, xPx, yPx);
    return typography->Height();
}

double SectionHeaderHeight(const std::string& title, double widthPx, const TextStyleSpec& style,
                           float density) {
    const int lines = std::max(1, MeasureLineCount(title, widthPx, style));
    return dimens::kBodyMediumLineHeight * static_cast<double>(lines) * density +
           dimens::kNormalMargin * 2.0 * density;
}

/**
 * The title's own block height. It is `n` line TOKENS tall, where n is what the painter says this
 * font wraps to — a title CAN wrap ("Notifications of severe weather alerts" does, at this width),
 * and a version of this helper that assumed one line drew the summary straight through the title's
 * second line.
 */
/**
 * The Material 3 type scale's TRACKING, which the row styles do not carry themselves: titleMedium
 * 0.15 sp and bodyMedium 0.25. Applied here rather than at every call site so the measure and the
 * paint cannot disagree about it, and so one edit covers all twelve row-drawing screens.
 *
 * Worth 13.7 px on a 28-character title at this density, which is most of what still separated the
 * port from the reference after the typeface was corrected.
 */
constexpr double kTitleTracking = 0.15;
constexpr double kBodyTracking = 0.25;

static TextStyleSpec Tracked(const TextStyleSpec& style, double trackingVp) {
    TextStyleSpec tracked = style;
    tracked.letterSpacingVp = trackingVp;
    return tracked;
}

/** titleMedium is weight 500. Every settings screen was asking this style for `bold`, i.e. 700. */
static TextStyleSpec TitleMedium(const TextStyleSpec& style) {
    TextStyleSpec title = Tracked(style, kTitleTracking);
    title.bold = false;
    title.medium = true;
    return title;
}

static double TitleBlockHeight(const std::string& title, double textWidthPx,
                               const TextStyleSpec& titleStyle, float density) {
    const int lines = std::max(1, MeasureLineCount(title, textWidthPx, titleStyle));
    return dimens::kTitleMediumLineHeight * static_cast<double>(lines) * density;
}

bool UsingSystemFont() {
    return g_systemFont;
}

SystemFontScope::SystemFontScope() : previous_(g_systemFont) {
    g_systemFont = true;
}

SystemFontScope::~SystemFontScope() {
    g_systemFont = previous_;
}

double PreferenceRowHeight(const std::string& title, const std::string& summary, double textWidthPx,
                           const TextStyleSpec& titleStyle, const TextStyleSpec& summaryStyle,
                           float density, bool switchRow) {
    const TextStyleSpec titleTracked = TitleMedium(titleStyle);
    const TextStyleSpec summaryTracked = Tracked(summaryStyle, kBodyTracking);
    double content = TitleBlockHeight(title, textWidthPx, titleTracked, density);
    if (!summary.empty()) {
        const int lines = std::max(1, MeasureLineCount(summary, textWidthPx, summaryTracked));
        content += dimens::kSmallMargin * density +
                   dimens::kBodyMediumLineHeight * static_cast<double>(lines) * density;
    }
    if (switchRow) {
        // A plain Row of normal_margin; no ListItem, so no floor either.
        return content + dimens::kNormalMargin * 2.0 * density;
    }
    return std::max(content + dimens::kPreferenceRowVPadding * 2.0 * density,
                    (dimens::kListItemOneLineHeight + dimens::kSmallMargin * 2.0) * density);
}

void DrawPreferenceRowText(OH_Drawing_Canvas* canvas, const std::string& title,
                           const std::string& summary, double textLeftPx, double rowTopPx,
                           double rowHeightPx, double textWidthPx, const TextStyleSpec& titleStyle,
                           const TextStyleSpec& summaryStyle, float density, bool switchRow) {
    const TextStyleSpec titleStyleT = TitleMedium(titleStyle);
    const TextStyleSpec summaryStyleT = Tracked(summaryStyle, kBodyTracking);
    const double titleH = TitleBlockHeight(title, textWidthPx, titleStyleT, density);
    if (summary.empty()) {
        // Nothing but a title, so it sits in the middle of the floor rather than at the padding.
        DrawTextFixed(canvas, title, textLeftPx, rowTopPx + (rowHeightPx - titleH) / 2.0,
                      textWidthPx, titleStyleT, dimens::kTitleMediumLineHeight * density);
        return;
    }
    const double contentTop =
        rowTopPx + (switchRow ? dimens::kNormalMargin : dimens::kPreferenceRowVPadding) * density;
    DrawTextFixed(canvas, title, textLeftPx, contentTop, textWidthPx, titleStyleT,
                  dimens::kTitleMediumLineHeight * density);
    DrawTextFixed(canvas, summary, textLeftPx, contentTop + titleH + dimens::kSmallMargin * density,
                  textWidthPx, summaryStyleT, dimens::kBodyMediumLineHeight * density);
}

double MeasureTextHeight(const std::string& text, double widthPx, const TextStyleSpec& style) {
    if (text.empty()) {
        return 0.0;
    }
    return Lookup(text, widthPx, style)->Height();
}

double MeasureTextWidth(const std::string& text, const TextStyleSpec& style) {
    if (text.empty()) {
        return 0.0;
    }
    // A very wide box so the line never wraps; LongestLine is then the natural width.
    return Lookup(text, 1.0e6, style)->LongestLine();
}

}  // namespace breezy
