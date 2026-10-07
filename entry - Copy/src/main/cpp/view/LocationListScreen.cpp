#include "view/LocationListScreen.h"

#include <native_drawing/drawing_brush.h>
#include <native_drawing/drawing_path.h>
#include <native_drawing/drawing_rect.h>
#include <native_drawing/drawing_round_rect.h>

#include <algorithm>

#include "render/AssetImages.h"
#include "render/TextPainter.h"

namespace breezy {
namespace {

/** dimens.xml small_weather_icon_size. */
constexpr double kWeatherIconSize = 42.0;
/** styles.xml material_image_button — 40 dp; item_location_card.xml:30 zeroes its 8 dp margin. */
constexpr double kDragButtonSize = 40.0;
/**
 * ShapeAppearance.Material3.Corner.Full on a wrap_content card is a stadium, so the radius is half
 * the row's height. Anything at or past that half-height gives the same shape.
 */
constexpr double kRowCornerRadius = 48.0;
/** BWCenterAlignedTopAppBar's title — dimens.xml large_title_text_size. */
constexpr double kLargeTitleTextSize = 20.0;

void FillRoundRect(OH_Drawing_Canvas* canvas, double left, double top, double right, double bottom,
                   double radius, uint32_t color) {
    OH_Drawing_Brush* brush = OH_Drawing_BrushCreate();
    OH_Drawing_BrushSetColor(brush, color);
    OH_Drawing_BrushSetAntiAlias(brush, true);
    OH_Drawing_Rect* rect = OH_Drawing_RectCreate(
        static_cast<float>(left), static_cast<float>(top),
        static_cast<float>(right), static_cast<float>(bottom));
    OH_Drawing_RoundRect* round = OH_Drawing_RoundRectCreate(
        rect, static_cast<float>(radius), static_cast<float>(radius));
    OH_Drawing_CanvasAttachBrush(canvas, brush);
    OH_Drawing_CanvasDrawRoundRect(canvas, round);
    OH_Drawing_CanvasDetachBrush(canvas);
    OH_Drawing_RoundRectDestroy(round);
    OH_Drawing_RectDestroy(rect);
    OH_Drawing_BrushDestroy(brush);
}

void FillRect(OH_Drawing_Canvas* canvas, double left, double top, double right, double bottom,
              uint32_t color) {
    OH_Drawing_Brush* brush = OH_Drawing_BrushCreate();
    OH_Drawing_BrushSetColor(brush, color);
    OH_Drawing_Rect* rect = OH_Drawing_RectCreate(
        static_cast<float>(left), static_cast<float>(top),
        static_cast<float>(right), static_cast<float>(bottom));
    OH_Drawing_CanvasAttachBrush(canvas, brush);
    OH_Drawing_CanvasDrawRect(canvas, rect);
    OH_Drawing_CanvasDetachBrush(canvas);
    OH_Drawing_RectDestroy(rect);
    OH_Drawing_BrushDestroy(brush);
}

}  // namespace

double LocationListScreen::RowHeight(const LocationRow& row) const {
    // The content block carries normal_margin top and bottom; a row with a body is two lines.
    const double lines = row.body.empty() ? 1.0 : 2.0;
    const double text = lines == 1.0
        ? dimens::kTitleTextSize * 1.45
        : dimens::kTitleTextSize * 1.45 + dimens::kContentTextSize * 1.45;
    const double byText = text + dimens::kNormalMargin * 2.0;
    // Never shorter than the drag button, which is what sets the minimum row. It carries no
    // margin here — item_location_card.xml:30 zeroes the style's.
    const double byButton = kDragButtonSize;
    return std::max(byText, byButton) * density_;
}

double LocationListScreen::ListTop() const {
    // kt:288-290 — a `4.dp + small_margin` spacer stands where the progress bar would be, so the
    // rows do not jump when a list write starts.
    return topInset_ + BarHeight() + (4.0 + dimens::kSmallMargin) * density_;
}

double LocationListScreen::ContentHeight() const {
    double total = 0.0;
    for (size_t i = 0; i < rows_.size(); ++i) {
        total += RowHeight(rows_[i]);
        if (i + 1 < rows_.size()) {
            total += RowSpacing();
        }
    }
    // Clear of the FAB column so the last row can always be reached — which is one button taller
    // while my-location is still on offer.
    return total + FabSize() * FabCount() +
           dimens::kNormalMargin * density_ * (FabCount() == 2 ? 4.0 : 3.0);
}

void LocationListScreen::ScrollBy(double dy) {
    const double viewport = height_ - ListTop() - bottomInset_;
    const double maxScroll = std::max(0.0, ContentHeight() - viewport);
    const double before = scroll_;
    scroll_ = std::max(0.0, std::min(maxScroll, scroll_ - dy));
    if (scroll_ != before) {
        DamageList();
    }
}

/* ---------------------------------------------------------------------- damage ----------------- */

void LocationListScreen::Damage(double top, double bottom) {
    if (bottom <= top) {
        return;
    }
    if (damageBottom_ <= damageTop_) {
        damageTop_ = top;
        damageBottom_ = bottom;
        return;
    }
    damageTop_ = std::min(damageTop_, top);
    damageBottom_ = std::max(damageBottom_, bottom);
}

void LocationListScreen::DamageList() {
    // The toolbar above ListTop() never moves with the rows, so a scroll leaves it alone.
    Damage(ListTop() - RowSpacing(), height_);
}

void LocationListScreen::DamageRow(int index) {
    if (index < 0 || index >= static_cast<int>(rows_.size())) {
        return;
    }
    const double top = RowTop(static_cast<size_t>(index));
    const double spacing = RowSpacing();
    Damage(top - spacing, top + RowHeight(rows_[static_cast<size_t>(index)]) + spacing);
}

void LocationListScreen::DamageDragSpan() {
    if (dragIndex_ < 0 || rows_.empty()) {
        return;
    }
    const int lo = std::max(0, std::min(dragIndex_, dropIndex_ < 0 ? dragIndex_ : dropIndex_));
    const int hi = std::min(static_cast<int>(rows_.size()) - 1,
                            std::max(dragIndex_, dropIndex_ < 0 ? dragIndex_ : dropIndex_));
    for (int i = lo; i <= hi; ++i) {
        DamageRow(i);
    }
    // The lifted row follows the finger and can be outside every slot it passes over.
    DamageRow(dragIndex_);
}

bool LocationListScreen::DirtyBand(double* top, double* bottom) const {
    if (damageAll_) {
        return false;
    }
    *top = std::max(0.0, std::floor(damageTop_));
    *bottom = std::min(height_, std::ceil(damageBottom_));
    if (*bottom < *top) {
        *bottom = *top;
    }
    return true;
}

double LocationListScreen::RowTop(size_t index) const {
    double y = ListTop() - scroll_;
    for (size_t i = 0; i < index && i < rows_.size(); ++i) {
        y += RowHeight(rows_[i]) + RowSpacing();
    }
    return y + DragOffsetFor(index);
}

/**
 * Where a row sits while a drag is in progress.
 *
 * The lifted one follows the finger; the rows between its old and new slots shift by one to open
 * the gap, which is what notifyItemMoved animates on Android. Every row is the same height here, so
 * one row's height drives the arithmetic.
 */
double LocationListScreen::DragOffsetFor(size_t index) const {
    if (dragIndex_ < 0 || rows_.empty()) {
        return 0.0;
    }
    const double step = RowHeight(rows_[0]) + RowSpacing();
    const int i = static_cast<int>(index);
    if (i == dragIndex_) {
        return dragDY_;
    }
    if (dropIndex_ > dragIndex_ && i > dragIndex_ && i <= dropIndex_) {
        return -step;
    }
    if (dropIndex_ < dragIndex_ && i >= dropIndex_ && i < dragIndex_) {
        return step;
    }
    return 0.0;
}

bool LocationListScreen::OnHandle(double x, double y) const {
    const int index = RowAt(x, y);
    if (index < 0) {
        return false;
    }
    // The 40 dp button sits flush at the row's leading edge (item_location_card.xml:30 zeroes
    // the style's margin), so the hit strip starts at the card's own edge.
    const double left = dimens::kNormalMargin * density_;
    return x >= left && x <= left + kDragButtonSize * density_;
}

int LocationListScreen::RowAt(double x, double y) const {
    if (y < ListTop() || y > height_) {
        return -1;
    }
    double rowY = ListTop() - scroll_;
    for (size_t i = 0; i < rows_.size(); ++i) {
        const double rowHeight = RowHeight(rows_[i]);
        if (y >= rowY && y <= rowY + rowHeight) {
            return static_cast<int>(i);
        }
        rowY += rowHeight + RowSpacing();
    }
    return -1;
}

void LocationListScreen::BeginSwipe(int index) {
    swipeIndex_ = index;
    swipeX_ = 0.0;
    DamageRow(index);
}

void LocationListScreen::UpdateSwipe(double dx) {
    if (swipeIndex_ >= 0) {
        swipeX_ = dx;
        // The row only travels horizontally, so its own slot is the whole of what changed.
        DamageRow(swipeIndex_);
    }
}

/**
 * ItemTouchHelper's default getSwipeThreshold is half the item's width; past it the swipe is
 * committed, short of it the row springs back.
 *
 * Both actions end with the row back in place — the settings branch calls notifyItemChanged (kt:91)
 * and a refused delete does the same (kt:100) — so the offset is cleared either way. The row is not
 * animated home: this screen runs no frame loop, and a 250 ms spring would need one.
 */
LocationListHit LocationListScreen::EndSwipe() {
    LocationListHit hit;
    const int index = swipeIndex_;
    const double travelled = swipeX_;
    // Marked while the row is still swiped, so the band covers where it has to be erased from.
    DamageRow(index);
    swipeIndex_ = -1;
    swipeX_ = 0.0;
    /*
     * The row travels home over 250 ms rather than snapping.
     *
     * It keeps its offset for the animation's sake — `swipeX_` is 0 now, so the drawing reads the
     * settle instead — and the renderer pumps vsync while `Settling()` is true.
     */
    if (index >= 0 && travelled != 0.0) {
        settleIndex_ = index;
        settleFrom_ = travelled;
        settleMs_ = 0.0;
    }
    if (index < 0 || index >= static_cast<int>(rows_.size())) {
        return hit;
    }
    const double rowWidth = width_ - dimens::kNormalMargin * density_ * 2.0;
    if (std::fabs(travelled) <= rowWidth * kSwipeThreshold) {
        hit.action = LocationListAction::Redraw;
        return hit;
    }
    hit.index = index;
    /*
     * LocationHolder.kt:56-72 — in LTR the container reveals delete at the START edge and settings
     * at the END, so dragging RIGHT deletes and dragging LEFT opens the sources dialog. (The
     * `location_swipe_to_delete` talkback string says the opposite; the code is what the app does.)
     */
    if (travelled > 0.0) {
        // kt:100-104 — the "cannot be empty" refusal is checked BEFORE the refresh one, so the last
        // row reports why it survived even mid-refresh.
        if (rows_.size() <= 1) {
            hit.action = LocationListAction::Refuse;
            hit.destination = "Location list cannot be empty";  // `location_message_list_cannot_be_empty`
            return hit;
        }
        if (refreshing_) {
            hit.action = LocationListAction::Refuse;
            hit.destination = "Please wait for refresh to complete";  // `message_please_wait_refresh`
            return hit;
        }
        hit.action = LocationListAction::Delete;
        return hit;
    }
    if (refreshing_) {
        hit.action = LocationListAction::Refuse;
        hit.destination = "Please wait for refresh to complete";
        return hit;
    }
    hit.action = LocationListAction::OpenSources;
    return hit;
}

LocationListHit LocationListScreen::BeginDrag(int index) {
    LocationListHit hit;
    // kt:60-67 — refused outright while a refresh is in flight. Because the drag never starts, the
    // row does not follow the finger either and the message fires once rather than per move event.
    if (refreshing_) {
        hit.action = LocationListAction::Refuse;
        hit.destination = "Please wait for refresh to complete";
        return hit;
    }
    dragIndex_ = index;
    dropIndex_ = index;
    dragDY_ = 0.0;
    DamageDragSpan();
    hit.action = LocationListAction::Redraw;
    return hit;
}

/** onMove — the drop target is recomputed from how many row-heights the finger has travelled. */
void LocationListScreen::UpdateDrag(double dy) {
    if (dragIndex_ < 0 || rows_.empty()) {
        return;
    }
    // Where the drag is leaving, then where it has moved to — the union of the two is what has to
    // be repainted, and marking only the second would leave the row's old position on screen.
    DamageDragSpan();
    dragDY_ = dy;
    const double step = RowHeight(rows_[0]) + RowSpacing();
    const int shift = static_cast<int>(std::lround(dragDY_ / step));
    dropIndex_ = std::max(0, std::min(static_cast<int>(rows_.size()) - 1, dragIndex_ + shift));
    DamageDragSpan();
}

/**
 * The reorder is committed once, on release, not on every crossing. `swapLocations` is a MOVE
 * despite its name (`valid.add(to, valid.removeAt(from))`).
 */
LocationListHit LocationListScreen::EndDrag() {
    LocationListHit hit;
    const int from = dragIndex_;
    const int to = dropIndex_;
    // Marked while the drag state still describes where the rows are being lifted from.
    DamageDragSpan();
    dragIndex_ = -1;
    dropIndex_ = -1;
    dragDY_ = 0.0;
    if (from >= 0 && to >= 0 && from != to) {
        hit.action = LocationListAction::Move;
        hit.index = from;
        hit.dropIndex = to;
        return hit;
    }
    hit.action = LocationListAction::Redraw;
    return hit;
}

/**
 * Curve.Friction — the decelerate the ArkUI port animates the same spring-back with, so both
 * settle on the same easing rather than one linear and one curved.
 */
bool LocationListScreen::AdvanceSettle(double seconds) {
    if (settleIndex_ < 0) {
        return false;
    }
    DamageRow(settleIndex_);
    settleMs_ += seconds * 1000.0;
    if (settleMs_ >= kSettleMs) {
        settleIndex_ = -1;
        settleFrom_ = 0.0;
        settleMs_ = 0.0;
        return false;
    }
    DamageRow(settleIndex_);
    return true;
}

void LocationListScreen::CancelGestures() {
    settleIndex_ = -1;
    settleFrom_ = 0.0;
    settleMs_ = 0.0;
    // A cancel can arrive from anywhere (a dialog opening over the screen, the window losing the
    // pointer), so the cheap and always-correct answer is to repaint the lot.
    damageAll_ = true;
    swipeIndex_ = -1;
    swipeX_ = 0.0;
    dragIndex_ = -1;
    dropIndex_ = -1;
    dragDY_ = 0.0;
}

void LocationListScreen::Draw(OH_Drawing_Canvas* canvas) const {
    const uint32_t surface = kSurface.Resolve(dark_);
    const uint32_t onSurface = kOnSurface.Resolve(dark_);
    FillRect(canvas, 0.0, 0.0, width_, height_, surface);

    const double iconSize = dimens::kMaterialIconSize * density_;
    const double normalMargin = dimens::kNormalMargin * density_;

    /* ---- BWCenterAlignedTopAppBar (kt:180-217): back, centred title, settings ---- */
    const double barCentre = topInset_ + BarHeight() / 2.0;
    // kt:198-202 — `onBackPressed` is null while the drawer is visible, so the two-pane layout has
    // no back arrow: there is nothing to go back FROM, the weather page is already beside it.
    if (!paneMode_) {
        DrawAsset(canvas, "ic_back.svg", normalMargin, barCentre - iconSize / 2.0, iconSize,
                  onSurface, true);
    }
    DrawAsset(canvas, "ic_settings.svg", width_ - normalMargin - iconSize,
              barCentre - iconSize / 2.0, iconSize, onSurface, true);

    TextStyleSpec titleStyle;
    titleStyle.sizeVp = kLargeTitleTextSize;
    titleStyle.color = onSurface;
    titleStyle.singleLine = true;
    // strings.xml `locations`
    const double titleWidth = MeasureTextWidth("Locations", titleStyle);
    DrawText(canvas, "Locations", (width_ - titleWidth) / 2.0,
             barCentre - kLargeTitleTextSize * density_ * 0.7, titleWidth + density_, titleStyle);

    /*
     * The list-write indicator (kt:281-291).
     *
     * `_locationListLoading` is set around the list WRITES — storing an added location and the
     * reverse geocoding that precedes it — and the Kotlin swaps a LinearProgressIndicator for the
     * `4.dp + small_margin` Spacer that otherwise holds its place. `ListTop()` already reserves
     * that height either way, so the rows do not jump when a write starts.
     *
     * Drawn as an empty 4 dp track rather than an animated indeterminate sweep, which is what
     * ArkUIApp's `Progress({ value: 0, … })` also produces — the two ports match, and in this port
     * the flag is only ever up for the instant an add takes.
     */
    if (listLoading_) {
        const double trackTop = topInset_ + BarHeight();
        const double trackHeight = kProgressHeight * density_;
        FillRoundRect(canvas, normalMargin, trackTop, width_ - normalMargin,
                      trackTop + trackHeight, trackHeight / 2.0, kSurfaceVariant.Resolve(dark_));
    }

    /* ---- the rows (kt:265-300) ---- */
    OH_Drawing_CanvasSave(canvas);
    OH_Drawing_Rect* clip = OH_Drawing_RectCreate(
        0.0f, static_cast<float>(ListTop()),
        static_cast<float>(width_), static_cast<float>(height_));
    OH_Drawing_CanvasClipRect(canvas, clip, OH_Drawing_CanvasClipOp::INTERSECT, true);
    OH_Drawing_RectDestroy(clip);

    // kt:274-279 — normal_margin at the sides only; no horizontal padding is added in landscape.
    const double left = normalMargin;
    const double right = width_ - normalMargin;

    /*
     * The dragged row is painted LAST, over its neighbours — onChildDraw raises it by touch_rise_z
     * and puts it above them. Everything else is painted in list order.
     */
    for (int pass = 0; pass < 2; ++pass) {
    for (size_t i = 0; i < rows_.size(); ++i) {
        const bool lifted = static_cast<int>(i) == dragIndex_;
        if ((pass == 0) == lifted) {
            continue;
        }
        const LocationRow& row = rows_[i];
        const double rowHeight = RowHeight(row);
        double y = RowTop(i);
        // The swipe travel, applied to the row's content only — the revealed action stays put.
        // While the finger is down that is `swipeX_`; after it lifts the row is springing home.
        double swipe = static_cast<int>(i) == swipeIndex_ ? swipeX_ : 0.0;
        if (static_cast<int>(i) == settleIndex_) {
            // Curve.Friction, as ArkUIApp's animateTo uses — a decelerate, not a linear ramp.
            const double t = std::min(1.0, settleMs_ / kSettleMs);
            const double eased = 1.0 - (1.0 - t) * (1.0 - t);
            swipe = settleFrom_ * (1.0 - eased);
        }
        if (y + rowHeight >= ListTop() && y <= height_) {
            /*
             * SlidingItemContainerLayout's revealed action (kt:119-149).
             *
             * The container's OWN background is the revealed colour, with one icon in front of the
             * item: delete on errorContainer at the start edge, settings on the secondary pair — or
             * the tertiary pair for a current position — at the end edge.
             */
            if (swipe != 0.0) {
                const bool deleting = swipe > 0.0;
                uint32_t actionBackground;
                uint32_t actionTint;
                if (deleting) {
                    actionBackground = kErrorContainer.Resolve(dark_);
                    actionTint = kOnErrorContainer.Resolve(dark_);
                } else if (row.currentPosition) {
                    actionBackground = kTertiaryContainer.Resolve(dark_);
                    actionTint = kOnTertiaryContainer.Resolve(dark_);
                } else {
                    actionBackground = kSecondaryContainer.Resolve(dark_);
                    actionTint = kOnSecondaryContainer.Resolve(dark_);
                }
                FillRoundRect(canvas, left, y, right, y + rowHeight,
                              kRowCornerRadius * density_, actionBackground);
                const double iconX = deleting ? left + normalMargin
                                              : right - normalMargin - iconSize;
                DrawAsset(canvas, deleting ? "ic_delete.svg" : "ic_settings.svg", iconX,
                          y + (rowHeight - iconSize) / 2.0, iconSize, actionTint, true);
            }
            OH_Drawing_CanvasSave(canvas);
            OH_Drawing_CanvasTranslate(canvas, static_cast<float>(swipe), 0.0f);
            /*
             * LocationHolder.kt:74-83 — the row's background:
             *   selected    primaryContainer
             *   has alerts  alert_background
             *   otherwise   the 2 dp elevated surface (getWidgetSurfaceColor)
             */
            uint32_t background;
            if (row.selected) {
                background = kPrimaryContainer.Resolve(dark_);
            } else if (row.alertCount > 0) {
                background = kAlertBackground.Resolve(dark_);
            } else {
                background = ElevatedSurface(2.0, kPrimary.Resolve(dark_), surface);
            }
            FillRoundRect(canvas, left, y, right, y + rowHeight,
                          kRowCornerRadius * density_, background);

            /* kt:85-95 — the drag handle's tint follows the same three-way choice. */
            uint32_t dragTint;
            if (row.selected) {
                dragTint = kOnPrimaryContainer.Resolve(dark_);
            } else if (row.alertCount > 0) {
                dragTint = kAlertText.Resolve(dark_);
            } else {
                dragTint = kPrimary.Resolve(dark_);
            }
            const double handleSize = kDragButtonSize * density_;
            /*
             * FLUSH against the card's leading edge, with no margin either side.
             *
             * `material_image_button` carries an 8 dp margin, but item_location_card.xml:30
             * OVERRIDES it with `android:layout_margin="0dp"`, and the content LinearLayout after
             * it has no start margin of its own. Taking the style's default instead put the handle
             * 8 dp in and opened another 8 dp before the weather icon, so every title sat 16 dp
             * (52 px) right of Android's. Measured on the reference: handle [52,432], icon x=182,
             * title x=345.
             */
            const double handleLeft = left;
            // The glyph sits inside the 40 dp button's own 8 dp padding.
            DrawAsset(canvas, "ic_drag.svg", handleLeft + dimens::kSmallMargin * density_,
                      y + (rowHeight - iconSize) / 2.0, iconSize, dragTint, true);

            double contentLeft = handleLeft + handleSize;

            // The icon is hidden entirely until a refresh has produced a weather code (kt:97-104).
            if (row.hasWeather) {
                const double weatherIcon = kWeatherIconSize * density_;
                std::string asset = std::string(IconBaseName(row.kind, row.daylight)) + ".png";
                DrawAsset(canvas, asset, contentLeft, y + (rowHeight - weatherIcon) / 2.0,
                          weatherIcon, 0, false);
                contentLeft += weatherIcon + dimens::kSmallMargin * density_;
            }

            /* kt:105-121 — the two text colours. */
            TextStyleSpec titleText;
            titleText.sizeVp = dimens::kTitleTextSize;
            titleText.singleLine = true;
            titleText.color = row.selected ? kOnPrimaryContainer.Resolve(dark_)
                                           : kTitleText.Resolve(dark_);

            TextStyleSpec bodyText;
            bodyText.sizeVp = dimens::kContentTextSize;
            bodyText.singleLine = true;
            bodyText.color = row.alertCount > 0 ? kAlertText.Resolve(dark_)
                                                : kBodyText.Resolve(dark_);

            const double textWidth = right - normalMargin - contentLeft;
            if (row.body.empty()) {
                // title2 is GONE when the body is empty (kt:113-115), not just blank.
                DrawText(canvas, row.title, contentLeft,
                         y + (rowHeight - dimens::kTitleTextSize * density_ * 1.3) / 2.0,
                         textWidth, titleText);
            } else {
                const double titleH = dimens::kTitleTextSize * density_ * 1.45;
                const double bodyH = dimens::kContentTextSize * density_ * 1.45;
                const double blockTop = y + (rowHeight - titleH - bodyH) / 2.0;
                DrawText(canvas, row.title, contentLeft, blockTop, textWidth, titleText);
                DrawText(canvas, row.body, contentLeft, blockTop + titleH, textWidth, bodyText);
            }
            OH_Drawing_CanvasRestore(canvas);
        }
    }
    }
    OH_Drawing_CanvasRestore(canvas);

    /* ---- the FAB column (kt:219-264) ---- */
    /*
     * kt:224 sets containerColor = colorScheme.tertiary — the TERTIARY role, not tertiaryContainer,
     * so it is the deep gold with white content. Both buttons share it, separated by normal_margin
     * as the Column's Spacer does (kt:234, 251).
     *
     * A rounded SQUARE: Material3's FAB is a 56 dp container with a 16 dp corner, and the circle
     * this used to draw is the Material2 shape. The search screen's Tune FAB already used
     * normalMargin for exactly this reason.
     */
    const double fab = FabSize();
    const double fabLeft = width_ - normalMargin - fab;
    const double addTop = height_ - bottomInset_ - normalMargin - fab;
    FillRoundRect(canvas, fabLeft, addTop, fabLeft + fab, addTop + fab,
                  dimens::kNormalMargin * density_, kTertiary.Resolve(dark_));
    DrawAsset(canvas, "ic_add.svg", fabLeft + (fab - iconSize) / 2.0,
              addTop + (fab - iconSize) / 2.0, iconSize, kOnTertiary.Resolve(dark_), true);

    // kt:236-252 — offered only while no current-position entry exists, and above the + as the
    // Column orders them.
    if (!hasCurrentPosition_) {
        const double myTop = addTop - normalMargin - fab;
        FillRoundRect(canvas, fabLeft, myTop, fabLeft + fab, myTop + fab,
                      dimens::kNormalMargin * density_, kTertiary.Resolve(dark_));
        DrawAsset(canvas, "ic_my_location.svg", fabLeft + (fab - iconSize) / 2.0,
                  myTop + (fab - iconSize) / 2.0, iconSize, kOnTertiary.Resolve(dark_), true);
    }
}

LocationListHit LocationListScreen::HitTest(double x, double y) const {
    LocationListHit hit;
    const double iconSize = dimens::kMaterialIconSize * density_;
    const double normalMargin = dimens::kNormalMargin * density_;

    /* The FAB column is on top of everything, so it is tested first. */
    const double fab = FabSize();
    const double fabLeft = width_ - normalMargin - fab;
    const double fabTop = height_ - bottomInset_ - normalMargin - fab;
    if (!hasCurrentPosition_) {
        const double myTop = fabTop - normalMargin - fab;
        if (x >= fabLeft && x <= fabLeft + fab && y >= myTop && y <= myTop + fab) {
            // kt:236-252 — opens the sources dialog on a fresh current-position location.
            hit.action = LocationListAction::AddCurrentPosition;
            return hit;
        }
    }
    if (x >= fabLeft && x <= fabLeft + fab && y >= fabTop && y <= fabTop + fab) {
        // kt:253-263 — the + opens SearchActivity.
        hit.action = LocationListAction::Add;
        return hit;
    }

    /* The toolbar is fixed, so its taps are in screen coordinates. */
    if (y < topInset_ + BarHeight()) {
        if (x < normalMargin + iconSize && !paneMode_) {
            // kt:182-186 — the back arrow closes the management screen. Absent in the pane.
            hit.action = LocationListAction::Close;
        } else if (x > width_ - normalMargin - iconSize) {
            // kt:205 — startSettingsActivity; screen 3 is not ported.
            hit.action = LocationListAction::Navigate;
            hit.destination = "Settings";
        }
        return hit;
    }

    /* A row — LocationHolder's click listener (kt:127) picks the location and goes back. */
    double rowY = ListTop() - scroll_;
    for (size_t i = 0; i < rows_.size(); ++i) {
        const double rowHeight = RowHeight(rows_[i]);
        if (y >= rowY && y <= rowY + rowHeight) {
            hit.action = LocationListAction::Select;
            hit.index = static_cast<int>(i);
            return hit;
        }
        rowY += rowHeight + RowSpacing();
    }
    return hit;
}

}  // namespace breezy
