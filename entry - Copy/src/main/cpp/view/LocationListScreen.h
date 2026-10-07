/*
 * Screen 2 — the saved-location list, drawn in C++.
 *
 * The ArkUI counterpart is pages/LocationList.ets + view/location/LocationCard.ets, ported from
 * ManagementFragment.kt's ContentView (:169-300), item_location_card.xml and LocationHolder.kt.
 *
 * This screen shares the XComponent surface with the main screen: Renderer keeps a mode and hands
 * paint and touch to whichever is active, exactly as MainActivity swaps HomeFragment for
 * ManagementFragment over one window rather than opening a second one.
 *
 * NOTHING NEW CROSSES THE NAPI BOUNDARY FOR THIS SCREEN. Every row is derived from the scenes the
 * renderer already holds — city, weather kind, daylight, the current condition, the alert list —
 * and the selection is the renderer's `currentLocation_`. The shell keeps owning HTTP and files,
 * and only gets involved when the list can be CHANGED (add, delete, reorder), which is the next
 * step.
 *
 * What is deliberately absent, matching ArkUIApp's list and for the same reasons:
 *   the debug-location FAB       gated on `BreezyWeather.instance.debugMode` (:222)
 *   notification permission card an Android runtime-permission prompt with no counterpart
 *   app-update check card        ties to an update channel this port does not have
 */
#ifndef BREEZY_VIEW_LOCATION_LIST_SCREEN_H
#define BREEZY_VIEW_LOCATION_LIST_SCREEN_H

#include <native_drawing/drawing_canvas.h>

#include <cstdint>
#include <string>
#include <vector>

#include "render/Theme.h"

namespace breezy {

/** One row's data — LocationModel.kt, the row model LocationHolder binds. */
struct LocationRow {
    /** kt:41 — "⊙ " prefixes a current-position row, as the toolbar title does. */
    std::string title;
    /** LocationModel.getWeatherText (kt:54-88): the condition, or the region, or "Active alert". */
    std::string body;
    WeatherKind kind = WeatherKind::None;
    bool daylight = true;
    bool selected = false;
    /** Non-zero puts the row on alert_background with alert_text (kt:78-95). */
    int alertCount = 0;
    /** True once a refresh has produced a weather code; before that no icon is drawn (kt:97-104). */
    bool hasWeather = false;
    /**
     * kt:60-64 — a current-position row reveals the tertiary pair on a left swipe, a saved one the
     * secondary pair. Always false for now: this port has no current-position entry.
     */
    bool currentPosition = false;
};

/** What a touch on this screen asked for. */
enum class LocationListAction {
    None,
    /** The back arrow, or a row — either way the main screen comes back. */
    Close,
    /** A row was chosen; `index` is the location to show. */
    Select,
    /** The + FAB — screen 2b opens over this one, as SearchActivity does over ManagementFragment. */
    Add,
    /**
     * The my-location FAB (kt:236-252). Like the + FAB it does not add anything by itself: it opens
     * the weather-sources dialog on a fresh `Location(isCurrentPosition = true)`, and the location
     * is saved only if that dialog is.
     */
    AddCurrentPosition,
    /** The gear — a destination this port has not built yet. */
    Navigate,
    /** A row swiped past the threshold to the RIGHT — ItemTouchHelper.END (kt:100-118). */
    Delete,
    /** A row swiped LEFT — ItemTouchHelper.START (kt:91-97), the weather-sources dialog. */
    OpenSources,
    /** A drag was released on a different slot; `index` is from and `dropIndex` is to. */
    Move,
    /** A refusal the screen decided for itself; `destination` is the message to show. */
    Refuse,
    /** Something moved and the frame has to be repainted, but nothing left the screen. */
    Redraw
};

struct LocationListHit {
    LocationListAction action = LocationListAction::None;
    int index = -1;
    int dropIndex = -1;
    /** Set for Navigate and Refuse — the placeholder name, or the message. */
    std::string destination;
};

class LocationListScreen {
public:
    /**
     * Replaces the rows, KEEPING the scroll position.
     *
     * A refresh landing while the list is open rebuilds the rows so a new condition or a new alert
     * colour reaches them, and resetting the scroll there would yank the list back to the top under
     * the user. Opening the screen calls ResetScroll() explicitly instead.
     */
    void SetRows(std::vector<LocationRow> rows) {
        rows_ = std::move(rows);
        damageAll_ = true;
    }

    void ResetScroll() {
        scroll_ = 0.0;
        damageAll_ = true;
    }

    /**
     * Called every frame with the same values, so it must only mark damage when one actually
     * changes — otherwise no frame is ever partial.
     */
    void SetGeometry(double widthPx, double heightPx, double density, double topInsetPx,
                     double bottomInsetPx) {
        if (width_ != widthPx || height_ != heightPx || density_ != density ||
            topInset_ != topInsetPx || bottomInset_ != bottomInsetPx) {
            damageAll_ = true;
        }
        width_ = widthPx;
        height_ = heightPx;
        density_ = density;
        topInset_ = topInsetPx;
        bottomInset_ = bottomInsetPx;
    }

    void SetDarkMode(bool dark) {
        if (dark_ != dark) {
            damageAll_ = true;
        }
        dark_ = dark;
    }

    /**
     * True while this is the LEFT PANE of the landscape two-pane layout rather than a screen of its
     * own (res/layout-w640dp/activity_main.xml).
     *
     * The only difference it makes to the drawing is the back arrow: ManagementFragment.kt:198-202
     * passes a null `onBackPressed` while the drawer is visible, because there is nothing to go back
     * from — the weather page is already on screen beside it.
     */
    void SetPaneMode(bool pane) {
        if (paneMode_ != pane) {
            damageAll_ = true;
        }
        paneMode_ = pane;
    }
    bool PaneMode() const { return paneMode_; }

    /**
     * Whether a current-position entry is already saved.
     *
     * kt:236 offers the my-location FAB only while none exists, so this both hides the button and
     * shortens the list's bottom padding back to one FAB's worth.
     */
    void SetHasCurrentPosition(bool has) {
        if (hasCurrentPosition_ != has) {
            damageAll_ = true;
        }
        hasCurrentPosition_ = has;
    }

    /**
     * MainActivityViewModel._locationListLoading (kt:98-99) — set around the list WRITES, and what
     * gates the progress indicator at kt:281. NOT the same thing as `refreshing_`, which is the
     * weather refresh and refuses gestures.
     */
    void SetListLoading(bool loading) {
        if (listLoading_ != loading) {
            damageAll_ = true;
        }
        listLoading_ = loading;
    }

    /**
     * True while a refresh is in flight, which refuses all four gestures
     * (LocationItemTouchCallback.kt:60-67, 77-79, 106).
     */
    void SetRefreshing(bool refreshing) {
        if (refreshing_ != refreshing) {
            damageAll_ = true;
        }
        refreshing_ = refreshing;
    }

    /** Paints the whole screen: surface, toolbar, rows, FAB. */
    void Draw(OH_Drawing_Canvas* canvas) const;

    /* ---------------------------------------------------------------- partial frames ----------- */

    /**
     * The band this screen's pixels have changed in since the last ClearDamage, in surface pixels.
     * False means the whole surface has to be repainted.
     *
     * Screen 2 used to redraw the full 1260 x 2720 surface on every touch move, and the copy of it
     * into the DMA buffer alone measured 3.3 ms of a 7.0 ms frame — for a swipe in which one row
     * moves. Every mutator below marks the rows it touched instead.
     *
     * The band is a SUPERSET of what changed, never a subset: the cost of marking too much is a
     * redundant copy, the cost of marking too little is a stale pixel left on screen.
     */
    bool DirtyBand(double* top, double* bottom) const;

    /** Called by the renderer once the band it was given has been painted. */
    void ClearDamage() {
        damageAll_ = false;
        damageTop_ = 0.0;
        damageBottom_ = 0.0;
    }

    /** Routes a tap. Coordinates are in surface pixels. */
    LocationListHit HitTest(double x, double y) const;

    /** Vertical drag, in surface pixels; clamped to the content. */
    void ScrollBy(double dy);

    /* ------------------------------------------------------------------- gestures ------------- */

    /**
     * Which gesture a press on (x, y) is allowed to become.
     *
     * The drag handle claims the press outright — LocationHolder.kt:129-135 hands the drag to the
     * ItemTouchHelper on ACTION_DOWN, with no long press — so a press there can never scroll the
     * list. Anywhere else on a row may become a horizontal swipe or a vertical scroll, decided by
     * the first 8 dp of travel.
     */
    bool OnHandle(double x, double y) const;
    /** The row under (x, y), or -1. */
    int RowAt(double x, double y) const;

    /** ItemTouchHelper's swipe: the row follows the finger horizontally. */
    void BeginSwipe(int index);
    void UpdateSwipe(double dx);
    /** Releases it: past half the width the action commits, short of it the row springs back. */
    LocationListHit EndSwipe();

    /** onSelectedChanged(ACTION_STATE_DRAG) — refused outright while a refresh is in flight. */
    LocationListHit BeginDrag(int index);
    void UpdateDrag(double dy);
    /** onSelectedChanged(ACTION_STATE_IDLE) — the reorder commits once, on release. */
    LocationListHit EndDrag();

    void CancelGestures();
    bool Dragging() const { return dragIndex_ >= 0; }
    bool Swiping() const { return swipeIndex_ >= 0; }

    /* ------------------------------------------------------------------ spring-back ------------ */

    /**
     * DefaultItemAnimator's move duration — the row travels home over 250 ms rather than snapping.
     *
     * Both actions end with the row back in place: the settings branch calls notifyItemChanged
     * (kt:91) and a refused delete does the same (kt:100). This used to jump straight to 0 because
     * screen 2 runs no frame loop; `Renderer::OnVSync` now pumps one for exactly as long as this
     * says it is settling.
     */
    bool Settling() const { return settleIndex_ >= 0; }
    /** Advances the spring-back. Returns true while more frames are wanted. */
    bool AdvanceSettle(double seconds);

private:
    /** ?attr/actionBarSize, as on screen 1. */
    double BarHeight() const { return 64.0 * density_; }
    /** Material3 FAB: 56 dp. */
    double FabSize() const { return 56.0 * density_; }
    /** A two-line row is ListItemTwoLineContainerHeight plus the card's own vertical margins. */
    double RowHeight(const LocationRow& row) const;
    double RowSpacing() const { return dimens::kSmallMargin * density_; }
    /** Where the list starts: below the toolbar, plus kt:289's 4 dp + small_margin spacer. */
    double ListTop() const;
    double ContentHeight() const;
    /** The top of row `index` in surface pixels, scroll and drag displacement included. */
    double RowTop(size_t index) const;
    /** Where a row sits vertically while a drag is in progress. */
    double DragOffsetFor(size_t index) const;

    /* ------------------------------------------------------------------ damage ----------------- */

    /** Unions [top, bottom) into the band this frame will repaint. */
    void Damage(double top, double bottom);
    /** Everything below the toolbar — a scroll moves every row but never the bar above them. */
    void DamageList();
    /** One row's slot, its spacing included. */
    void DamageRow(int index);
    /**
     * The rows a drag is currently disturbing: the dragged row where it is now, and every slot
     * between the row it came from and the one it would land in. Called both BEFORE and AFTER the
     * drag state moves, so the band covers where the row was as well as where it went — repainting
     * only the new position would leave the old one behind.
     */
    void DamageDragSpan();

    /** ItemTouchHelper.getSwipeThreshold's default — half the item's width commits the swipe. */
    static constexpr double kSwipeThreshold = 0.5;

    std::vector<LocationRow> rows_;
    /** The row being swiped and how far, in surface pixels; -1 and 0 at rest. */
    int swipeIndex_ = -1;
    double swipeX_ = 0.0;
    /** The row being dragged, where it would land, and its live finger travel. */
    int dragIndex_ = -1;
    int dropIndex_ = -1;
    double dragDY_ = 0.0;
    bool refreshing_ = false;
    bool listLoading_ = false;
    bool paneMode_ = false;
    /** LinearProgressIndicator's track height, and the Spacer that stands in for it (kt:289). */
    static constexpr double kProgressHeight = 4.0;
    double width_ = 0.0;
    double height_ = 0.0;
    double density_ = 1.0;
    double topInset_ = 0.0;
    double bottomInset_ = 0.0;
    double scroll_ = 0.0;
    bool dark_ = false;
    bool hasCurrentPosition_ = false;
    /** How many FABs the column holds right now — one, or two while my-location is offered. */
    int FabCount() const { return hasCurrentPosition_ ? 1 : 2; }

    /** True until the screen has painted once, and whenever a change moves more than known rows. */
    bool damageAll_ = true;
    double damageTop_ = 0.0;
    double damageBottom_ = 0.0;

    /** The row springing back, where it started, and how far through the 250 ms it is. */
    int settleIndex_ = -1;
    double settleFrom_ = 0.0;
    double settleMs_ = 0.0;
    /** DefaultItemAnimator.getMoveDuration(). */
    static constexpr double kSettleMs = 250.0;
};

}  // namespace breezy

#endif  // BREEZY_VIEW_LOCATION_LIST_SCREEN_H
