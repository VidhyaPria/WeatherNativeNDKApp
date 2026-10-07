/*
 * One rendered trend column, kept as a bitmap.
 *
 * A horizontal fling does not change a single pixel INSIDE a column — the day name, the icons, the
 * polyline and the histogram are all fixed by the item; only the column's x moves. Rasterising them
 * again every frame was 4.2 ms of the 12.9 ms fling frame (3.5 ms of charts and 0.7 ms of icons,
 * measured), which is what keeps this renderer at 60 fps against Android's and ArkUI's 120.
 *
 * This is the same trade RecyclerView gets for free: a scrolled view is not redrawn, its display
 * list is replayed by the GPU. There is no GPU here, so the replay is a blit of a cached bitmap.
 *
 * Correctness rests entirely on the generation. Anything that can change what a column looks like —
 * a refresh, a tab, a location, the theme — bumps it, and a mismatch re-renders. The column's own
 * size is checked separately, so a rotation cannot serve a stale shape. This is the same class of
 * bug ArkUIApp hit when @ReusableV2 recycling handed a column its neighbour's canvas; keeping the
 * invalidation to one integer is what makes it reviewable.
 */
#ifndef BREEZY_RENDER_COLUMNCACHE_H
#define BREEZY_RENDER_COLUMNCACHE_H

#include <native_drawing/drawing_bitmap.h>
#include <native_drawing/drawing_canvas.h>

#include <cstdint>

namespace breezy {

/** Which list a column belongs to — the two cards cache independently. */
enum class ColumnOwner { Daily, Hourly };

/**
 * A scoped render-to-cache. Construct it for a column; if `NeedsPaint()` the caller draws into
 * `Canvas()` in column-local coordinates, and the destructor blits the result at (left, top).
 * If the cache already holds the column, nothing is drawn and the destructor just blits.
 */
class CachedColumn {
public:
    CachedColumn(OH_Drawing_Canvas* target, ColumnOwner owner, int index, uint64_t generation,
                 double left, double top, double width, double height);
    ~CachedColumn();

    CachedColumn(const CachedColumn&) = delete;
    CachedColumn& operator=(const CachedColumn&) = delete;

    /** True when the column has to be drawn; false when the cached bitmap is already good. */
    bool NeedsPaint() const { return needsPaint_; }
    /** The canvas to draw the column into, with (0, 0) at the column's top-left. */
    OH_Drawing_Canvas* Canvas() const { return canvas_; }
    /** True when the cache could not be used at all and the caller must draw onto the target. */
    bool Bypassed() const { return canvas_ == target_; }

    /**
     * The bitmap's own size in whole pixels, which is the column's fractional size ROUNDED.
     *
     * Anything meant to cover the column edge to edge has to use these and not the fractional
     * width: filling 217.75 px of a 218 px bitmap leaves a quarter-pixel of the cleared background
     * down one side, and with an opaque blit that showed up as a grey seam between every pair of
     * columns.
     */
    int Width() const { return width_; }
    int Height() const { return height_; }

private:
    OH_Drawing_Canvas* target_ = nullptr;
    OH_Drawing_Canvas* canvas_ = nullptr;
    OH_Drawing_Bitmap* bitmap_ = nullptr;
    double left_ = 0.0;
    double top_ = 0.0;
    int width_ = 0;
    int height_ = 0;
    bool needsPaint_ = true;
};

/** Drops every cached column. Called when the surface goes away. */
void ClearColumnCache();

}  // namespace breezy

#endif  // BREEZY_RENDER_COLUMNCACHE_H
