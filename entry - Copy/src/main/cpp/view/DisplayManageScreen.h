/*
 * Screens 3h / 3i / 3j — Blocks, Daily trends, Hourly trends. The NDK twin of ArkUIApp's
 * pages/DisplayManage.ets.
 *
 * ONE screen for all three, because Android is three Activities over one layout and one adapter
 * shape (CardDisplayManageActivity.kt / DailyTrendDisplayManageActivity.kt /
 * HourlyTrendDisplayManageActivity.kt — the same file three times, differing only in the enum).
 *
 * References captured first: ref_screen3h_blocks.png, ref_screen3i_daily.png,
 * ref_screen3j_hourly.png.
 *
 *   toolbar      a SMALL toolbar — back arrow + title on a 6 dp elevated surface, NOT the
 *                LargeTopAppBar the rest of screen 3 uses; these are View activities.
 *   list         one 56 dp row per enabled item, divided by a hairline colorOutline rule:
 *                  drag handle | title | optional gear | ✕
 *   bottom bar   a horizontal row of Assist chips, one per disabled item; tapping one appends it.
 *
 * The drag is hand-rolled here and starts on touch-DOWN on the handle, which is exactly what
 * Android does (CardDisplayAdapter.kt:52-57). The ArkTS port could not do that — inside a scrollable
 * List the framework cancels the child's touch at slop — and had to use the List's own long-press
 * item drag. That trigger difference is the one asymmetry between the two ports on this screen, and
 * it is a framework constraint rather than a choice; see PROGRESS.md.
 */

#ifndef BREEZY_VIEW_DISPLAYMANAGESCREEN_H
#define BREEZY_VIEW_DISPLAYMANAGESCREEN_H

#include <native_drawing/drawing_canvas.h>

#include <string>
#include <vector>

#include "../data/DisplayCatalogue.h"

namespace breezy {

enum class DisplayManageAction {
    None,
    Close,
    /** A card's gear — `destination` is "daily" or "hourly". */
    OpenList,
    /** The list changed: `key` is the preferences key, `value` the "&"-joined ids. */
    Changed
};

struct DisplayManageHit {
    DisplayManageAction action = DisplayManageAction::None;
    std::string destination;
    std::string key;
    std::string value;
};

class DisplayManageScreen {
public:
    void SetGeometry(double widthPx, double heightPx, float density, double topInsetPx,
                     double bottomInsetPx);
    void SetDarkMode(bool dark) { dark_ = dark; }
    /** Opens the screen on one list, with the stored value the shell holds. */
    void Open(DisplayList list, const std::string& value);
    DisplayList List() const { return list_; }
    /** The current order, for the write on the way out. */
    std::string Value() const { return DisplayListToValue(items_); }
    /** Whether anything has actually changed since Open — kt:188-190 writes only if it has. */
    bool Dirty() const { return Value() != opened_; }
    void MarkClean() { opened_ = Value(); }

    void Draw(OH_Drawing_Canvas* canvas);
    /** The whole gesture: a handle drag reorders, a horizontal drag deletes, a tap acts. */
    DisplayManageHit OnTouch(int phase, double x, double y);
    bool ScrollBy(double dy);
    void ResetScroll() { scroll_ = 0.0; }
    /** True while a row is being dragged or swiped, so the renderer keeps painting frames. */
    bool Animating() const { return dragIndex_ >= 0 || swipeIndex_ >= 0; }

private:
    void Layout();
    double ListTop() const;
    double RowTop(int index) const;
    double MaxScroll() const;
    int RowAt(double y) const;
    int ChipAt(double x, double y) const;
    void RemoveAt(int index);
    void RestoreChip(int index);
    void MoveItem(int from, int to);
    void DrawRow(OH_Drawing_Canvas* canvas, int index, double top, double dx, bool raised);
    void DrawChips(OH_Drawing_Canvas* canvas);

    DisplayList list_ = DisplayList::Blocks;
    std::vector<DisplayItem> items_;
    std::vector<DisplayItem> chips_;
    std::string opened_;

    double width_ = 0.0;
    double height_ = 0.0;
    float density_ = 1.0f;
    double topInset_ = 0.0;
    double bottomInset_ = 0.0;
    double scroll_ = 0.0;
    bool dark_ = false;

    /** The row under the finger for a REORDER, and how far it has travelled. */
    int dragIndex_ = -1;
    int dragFrom_ = -1;
    double dragDY_ = 0.0;
    /** The row under the finger for a SWIPE-delete, and its horizontal offset. */
    int swipeIndex_ = -1;
    double swipeDX_ = 0.0;

    double downX_ = 0.0;
    double downY_ = 0.0;
    double lastY_ = 0.0;
    bool onHandle_ = false;
    bool scrolled_ = false;
    /** Where the chips were drawn, so a tap can find one. */
    std::vector<double> chipLeft_;
    std::vector<double> chipRight_;
    double chipTop_ = 0.0;
    double chipBottom_ = 0.0;
};

}  // namespace breezy

#endif  // BREEZY_VIEW_DISPLAYMANAGESCREEN_H
