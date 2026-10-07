#include "render/ColumnCache.h"

#include <native_drawing/drawing_color.h>
#include <native_drawing/drawing_rect.h>
#include <native_drawing/drawing_sampling_options.h>

#include <algorithm>
#include <cmath>
#include <list>
#include <map>
#include <utility>

namespace breezy {
namespace {

/**
 * Enough for both cards' visible windows with room to spare.
 *
 * A daily column on this device is about 218 x 780 px, or 680 KB. Twenty of those is ~13 MB, which
 * is real money in a project that exists to measure memory — so the cap is deliberately just past
 * what two lists can show at once (roughly six columns each plus the partially visible ones at both
 * edges), not a generous round number.
 */
constexpr size_t kMaxEntries = 24;

/*
 * OPAQUE, and that is the whole point of the caller painting the card's background into the column.
 *
 * A translucent column has to be blended pixel by pixel on the way out, and measured that was
 * 3.5 ms a frame for thirteen columns — as much as rasterising them from scratch, which made the
 * cache worthless. An opaque source of the same size at an integer offset is a copy.
 */
constexpr OH_Drawing_ColorFormat kColorFormat = COLOR_FORMAT_RGBA_8888;
constexpr OH_Drawing_AlphaFormat kAlphaFormat = ALPHA_FORMAT_OPAQUE;

struct Entry {
    OH_Drawing_Bitmap* bitmap = nullptr;
    OH_Drawing_Canvas* canvas = nullptr;
    int width = 0;
    int height = 0;
    uint64_t generation = 0;
    bool painted = false;
    std::list<std::pair<int, int>>::iterator order;
};

using Key = std::pair<int, int>;  // (owner, index)

std::map<Key, Entry>& Cache() {
    static std::map<Key, Entry> cache;
    return cache;
}

/** Front = most recently used. */
std::list<Key>& Order() {
    static std::list<Key> order;
    return order;
}

void Destroy(Entry& entry) {
    if (entry.canvas != nullptr) {
        OH_Drawing_CanvasDestroy(entry.canvas);
        entry.canvas = nullptr;
    }
    if (entry.bitmap != nullptr) {
        OH_Drawing_BitmapDestroy(entry.bitmap);
        entry.bitmap = nullptr;
    }
    entry.painted = false;
}

}  // namespace

CachedColumn::CachedColumn(OH_Drawing_Canvas* target, ColumnOwner owner, int index,
                           uint64_t generation, double left, double top, double width,
                           double height)
    : target_(target), left_(left), top_(top) {
    const int w = static_cast<int>(std::lround(width));
    const int h = static_cast<int>(std::lround(height));
    width_ = w;
    height_ = h;
    if (target == nullptr || w <= 0 || h <= 0) {
        // A measure pass (null canvas) or a degenerate column: draw straight through.
        canvas_ = target;
        needsPaint_ = true;
        return;
    }

    const Key key{static_cast<int>(owner), index};
    auto found = Cache().find(key);
    if (found != Cache().end()) {
        Entry& entry = found->second;
        if (entry.width == w && entry.height == h && entry.generation == generation &&
            entry.painted) {
            Order().splice(Order().begin(), Order(), entry.order);
            canvas_ = entry.canvas;
            bitmap_ = entry.bitmap;
            needsPaint_ = false;
            return;
        }
        // Same slot, different content or size — reuse the allocation when the size still fits.
        if (entry.width != w || entry.height != h) {
            Destroy(entry);
        }
        Order().splice(Order().begin(), Order(), entry.order);
    } else {
        Entry fresh;
        if (Cache().size() >= kMaxEntries) {
            const Key& oldest = Order().back();
            auto victim = Cache().find(oldest);
            if (victim != Cache().end()) {
                /*
                 * Hand the evicted column's allocation to the new one when it is the same shape,
                 * which during a fling it always is. Destroying and re-creating instead means a
                 * 680 KB malloc and free for every column that scrolls in — several a frame — and
                 * the fragmentation that comes with it. This way the cache allocates at most
                 * kMaxEntries bitmaps for the life of the surface, and its cost is a flat number
                 * rather than something that grows with how far the user has scrolled.
                 */
                if (victim->second.width == w && victim->second.height == h) {
                    fresh.bitmap = victim->second.bitmap;
                    fresh.canvas = victim->second.canvas;
                    fresh.width = w;
                    fresh.height = h;
                    victim->second.bitmap = nullptr;
                    victim->second.canvas = nullptr;
                }
                Destroy(victim->second);
                Cache().erase(victim);
            }
            Order().pop_back();
        }
        Order().push_front(key);
        fresh.order = Order().begin();
        Cache().emplace(key, fresh);
        found = Cache().find(key);
    }

    Entry& entry = found->second;
    if (entry.bitmap == nullptr) {
        entry.bitmap = OH_Drawing_BitmapCreate();
        OH_Drawing_BitmapFormat format{kColorFormat, kAlphaFormat};
        OH_Drawing_BitmapBuild(entry.bitmap, static_cast<uint32_t>(w), static_cast<uint32_t>(h),
                               &format);
        entry.canvas = OH_Drawing_CanvasCreate();
        OH_Drawing_CanvasBind(entry.canvas, entry.bitmap);
        entry.width = w;
        entry.height = h;
    }
    if (entry.canvas == nullptr) {
        // The allocation failed; fall back to drawing straight onto the page.
        canvas_ = target;
        needsPaint_ = true;
        return;
    }

    // The caller fills the whole box, but clear anyway so a partial paint cannot show stale pixels.
    OH_Drawing_CanvasClear(entry.canvas, OH_Drawing_ColorSetArgb(0xFF, 0x00, 0x00, 0x00));
    entry.generation = generation;
    entry.painted = true;
    canvas_ = entry.canvas;
    bitmap_ = entry.bitmap;
    needsPaint_ = true;
}

CachedColumn::~CachedColumn() {
    if (bitmap_ == nullptr || target_ == nullptr) {
        return;  // Bypassed: the caller drew onto the page directly.
    }
    /*
     * Blitted at WHOLE pixels, with nearest sampling and no filter.
     *
     * `columnLeft` is fractional for all but one frame of a scroll, and a bitmap drawn at a
     * fractional offset is not a blit at all — it is a resample, and the first version of this
     * measured 4.4 ms a frame for thirteen columns, giving back everything the cache had saved.
     * Rounding costs at most half a device pixel of position, 0.15 dp here, which is below what a
     * scrolling list can show; it is also what any compositor does with a translated layer.
     */
    const float left = static_cast<float>(std::floor(left_ + 0.5));
    const float top = static_cast<float>(std::floor(top_ + 0.5));
    OH_Drawing_Rect* dst = OH_Drawing_RectCreate(left, top, left + static_cast<float>(width_),
                                                 top + static_cast<float>(height_));
    OH_Drawing_SamplingOptions* sampling =
        OH_Drawing_SamplingOptionsCreate(FILTER_MODE_NEAREST, MIPMAP_MODE_NONE);
    OH_Drawing_CanvasDrawBitmapRect(target_, bitmap_, nullptr, dst, sampling);
    OH_Drawing_SamplingOptionsDestroy(sampling);
    OH_Drawing_RectDestroy(dst);
}

void ClearColumnCache() {
    for (auto& pair : Cache()) {
        Destroy(pair.second);
    }
    Cache().clear();
    Order().clear();
}

}  // namespace breezy
