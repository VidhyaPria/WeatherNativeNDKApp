#include "DisplayManageScreen.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "../render/AssetImages.h"
#include "../render/Shapes.h"
#include "../render/TextPainter.h"
#include "../render/Theme.h"

namespace breezy {
namespace {

/** `?attr/actionBarSize` — the SMALL toolbar these activities use. */
constexpr double kToolbarHeight = 56.0;
/** Material 3 Toolbar title, `titleLarge`. A library token; the library is not vendored here. */
constexpr double kToolbarTitleSize = 22.0;
/** item_card_display.xml:7 — the row is a fixed 56 dp, not content-sized. */
constexpr double kRowHeight = 56.0;
/** `@style/material_image_button`: a 40 dp box with 8 dp padding, so a 24 dp glyph. */
constexpr double kIconButton = 40.0;
/** `@style/title_text` — TitleMedium at `title_text_size`. */
constexpr double kRowTitleSize = 16.0;
/** Widget.Material3.Chip.Assist, with item_tag.xml's bold `content_text_size` label. */
constexpr double kChipHeight = 32.0;
constexpr double kChipCorner = 8.0;
constexpr double kChipHPadding = 16.0;
constexpr double kChipTextSize = 14.0;
/** ViewConfiguration's touch slop, as every other screen in this port uses. */
constexpr double kTouchSlop = 8.0;
/** How far a row must travel before the swipe deletes it — ItemTouchHelper's default is a third. */
constexpr double kSwipeCommit = 0.33;

}  // namespace

void DisplayManageScreen::SetGeometry(double widthPx, double heightPx, float density,
                                      double topInsetPx, double bottomInsetPx) {
    width_ = widthPx;
    height_ = heightPx;
    density_ = density;
    topInset_ = topInsetPx;
    bottomInset_ = bottomInsetPx;
}

void DisplayManageScreen::Open(DisplayList list, const std::string& value) {
    list_ = list;
    opened_ = value;
    items_ = ParseDisplayList(list, value);
    chips_ = DisplayListRemainder(list, items_);
    scroll_ = 0.0;
    dragIndex_ = -1;
    dragFrom_ = -1;
    dragDY_ = 0.0;
    swipeIndex_ = -1;
    swipeDX_ = 0.0;
}

double DisplayManageScreen::ListTop() const {
    return topInset_ + kToolbarHeight * density_;
}

double DisplayManageScreen::RowTop(int index) const {
    return ListTop() + static_cast<double>(index) * kRowHeight * density_ - scroll_;
}

double DisplayManageScreen::MaxScroll() const {
    const double content = static_cast<double>(items_.size()) * kRowHeight * density_;
    // activity_card_display_manage.xml:38 — the list is padded 128 dp so the bar never covers a row.
    const double viewport = height_ - ListTop() - bottomInset_ - 128.0 * density_;
    return std::max(0.0, content - viewport);
}

bool DisplayManageScreen::ScrollBy(double dy) {
    const double before = scroll_;
    scroll_ = std::max(0.0, std::min(MaxScroll(), scroll_ - dy));
    return scroll_ != before;
}

int DisplayManageScreen::RowAt(double y) const {
    if (y < ListTop()) {
        return -1;
    }
    const int index = static_cast<int>((y - ListTop() + scroll_) / (kRowHeight * density_));
    return index >= 0 && index < static_cast<int>(items_.size()) ? index : -1;
}

void DisplayManageScreen::RemoveAt(int index) {
    if (index < 0 || index >= static_cast<int>(items_.size())) {
        return;
    }
    // `mTagAdapter.insertItem` APPENDS, so the chip row is in removal order, not catalogue order.
    chips_.push_back(items_[static_cast<size_t>(index)]);
    items_.erase(items_.begin() + index);
}

void DisplayManageScreen::RestoreChip(int index) {
    if (index < 0 || index >= static_cast<int>(chips_.size())) {
        return;
    }
    items_.push_back(chips_[static_cast<size_t>(index)]);  // kt:162 — appended to the end.
    chips_.erase(chips_.begin() + index);
}

/** ItemTouchHelper.onMove — `add(to, removeAt(from))`, a MOVE rather than a swap. */
void DisplayManageScreen::MoveItem(int from, int to) {
    const int count = static_cast<int>(items_.size());
    if (from < 0 || from >= count || to < 0 || to >= count || from == to) {
        return;
    }
    const DisplayItem moved = items_[static_cast<size_t>(from)];
    items_.erase(items_.begin() + from);
    items_.insert(items_.begin() + to, moved);
}

void DisplayManageScreen::Draw(OH_Drawing_Canvas* canvas) {
    if (width_ <= 0.0 || height_ <= 0.0) {
        return;
    }
    const double margin = dimens::kNormalMargin * density_;
    const double iconSize = dimens::kMaterialIconSize * density_;
    const uint32_t onSurface = kOnSurface.Resolve(dark_);

    FillRect(canvas, 0.0, 0.0, width_, height_, kSurface.Resolve(dark_));

    /* ---- the small toolbar ---- */
    FillRect(canvas, 0.0, 0.0, width_, ListTop(), kElevatedSurface6.Resolve(dark_));
    DrawAsset(canvas, "ic_back.svg", margin,
              topInset_ + (kToolbarHeight * density_ - iconSize) / 2.0, iconSize, onSurface, true);
    TextStyleSpec titleStyle;
    titleStyle.sizeVp = kToolbarTitleSize;
    titleStyle.color = onSurface;
    titleStyle.singleLine = true;
    const std::string title = DisplayListTitle(list_);
    const double titleLeft = margin + iconSize + margin;
    const double titleHeight = MeasureTextHeight(title, width_ - titleLeft - margin, titleStyle);
    DrawText(canvas, title, titleLeft,
             topInset_ + (kToolbarHeight * density_ - titleHeight) / 2.0,
             width_ - titleLeft - margin, titleStyle);

    /* ---- the rows ---- */
    OH_Drawing_CanvasSave(canvas);
    OH_Drawing_Rect* clip = OH_Drawing_RectCreate(0.0f, static_cast<float>(ListTop()),
                                                  static_cast<float>(width_),
                                                  static_cast<float>(height_));
    OH_Drawing_CanvasClipRect(canvas, clip, INTERSECT, true);
    OH_Drawing_RectDestroy(clip);

    for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
        if (i == dragIndex_) {
            continue;  // Drawn last so it rides above its neighbours.
        }
        const double top = RowTop(i);
        if (top + kRowHeight * density_ < ListTop() || top > height_) {
            continue;
        }
        DrawRow(canvas, i, top, i == swipeIndex_ ? swipeDX_ : 0.0, false);
    }
    if (dragIndex_ >= 0 && dragIndex_ < static_cast<int>(items_.size())) {
        DrawRow(canvas, dragIndex_, RowTop(dragFrom_) + dragDY_, 0.0, true);
    }
    OH_Drawing_CanvasRestore(canvas);

    DrawChips(canvas);
}

void DisplayManageScreen::DrawRow(OH_Drawing_Canvas* canvas, int index, double top, double dx,
                                  bool raised) {
    const DisplayItem& item = items_[static_cast<size_t>(index)];
    const double margin = dimens::kNormalMargin * density_;
    const double small = dimens::kSmallMargin * density_;
    const double iconSize = dimens::kMaterialIconSize * density_;
    const double button = kIconButton * density_;
    const double rowHeight = kRowHeight * density_;

    // The errorContainer bed a swiped row reveals — item_card_display.xml:10-15, both edges.
    if (dx != 0.0) {
        FillRect(canvas, 0.0, top, width_, top + rowHeight, kErrorContainer.Resolve(dark_));
        const double iconY = top + (rowHeight - iconSize) / 2.0;
        DrawAsset(canvas, "ic_delete.svg", margin, iconY, iconSize,
                  kOnErrorContainer.Resolve(dark_), true);
        DrawAsset(canvas, "ic_delete.svg", width_ - margin - iconSize, iconY, iconSize,
                  kOnErrorContainer.Resolve(dark_), true);
    }

    const double left = dx;
    // `?attr/colorSurface` behind the row itself, so the bed only shows where it has moved off.
    FillRect(canvas, left, top, left + width_, top + rowHeight,
             raised ? kSettingsCard.Resolve(dark_) : kSurface.Resolve(dark_));

    const uint32_t primary = kPrimary.Resolve(dark_);
    const double buttonY = top + (rowHeight - button) / 2.0;
    DrawAsset(canvas, "ic_drag.svg", left + small + (button - iconSize) / 2.0,
              buttonY + (button - iconSize) / 2.0, iconSize, primary, true);

    double rightEdge = left + width_ - small - button;
    DrawAsset(canvas, "ic_close.svg", rightEdge + (button - iconSize) / 2.0,
              buttonY + (button - iconSize) / 2.0, iconSize, primary, true);
    if (!item.configures.empty()) {
        rightEdge -= button;
        DrawAsset(canvas, "ic_settings.svg", rightEdge + (button - iconSize) / 2.0,
                  buttonY + (button - iconSize) / 2.0, iconSize, primary, true);
    }

    TextStyleSpec titleStyle;
    titleStyle.sizeVp = kRowTitleSize;
    titleStyle.bold = true;
    titleStyle.color = kOnSurface.Resolve(dark_);
    titleStyle.singleLine = true;
    const double textLeft = left + small + button + small;
    const double textWidth = std::max(0.0, rightEdge - textLeft);
    const double textHeight = MeasureTextHeight(item.name, textWidth, titleStyle);
    DrawText(canvas, item.name, textLeft, top + (rowHeight - textHeight) / 2.0, textWidth,
             titleStyle);

    // ListDecoration — a hairline in colorOutline under every row.
    FillRect(canvas, 0.0, top + rowHeight - 1.0, width_, top + rowHeight,
             kOutline.Resolve(dark_));
}

void DisplayManageScreen::DrawChips(OH_Drawing_Canvas* canvas) {
    chipLeft_.clear();
    chipRight_.clear();
    if (chips_.empty()) {
        chipTop_ = 0.0;
        chipBottom_ = 0.0;
        return;
    }
    const double margin = dimens::kNormalMargin * density_;
    const double barHeight = kChipHeight * density_ + margin * 2.0 + bottomInset_;
    const double barTop = height_ - barHeight;
    FillRect(canvas, 0.0, barTop, width_, height_, kBackground.Resolve(dark_));

    TextStyleSpec chipStyle;
    chipStyle.sizeVp = kChipTextSize;
    chipStyle.bold = true;  // item_tag.xml:8
    chipStyle.color = kOnSurface.Resolve(dark_);
    chipStyle.singleLine = true;
    chipStyle.alignment = TextAlignment::Center;

    chipTop_ = barTop + margin;
    chipBottom_ = chipTop_ + kChipHeight * density_;
    double x = margin;
    for (const DisplayItem& chip : chips_) {
        const double textWidth = MeasureTextWidth(chip.name, chipStyle);
        const double chipWidth = textWidth + kChipHPadding * 2.0 * density_;
        StrokeRoundRect(canvas, x, chipTop_, x + chipWidth, chipBottom_, kChipCorner * density_,
                        1.0 * density_, kComposeOutline.Resolve(dark_));
        const double textHeight = MeasureTextHeight(chip.name, chipWidth, chipStyle);
        DrawText(canvas, chip.name, x, chipTop_ + (kChipHeight * density_ - textHeight) / 2.0,
                 chipWidth, chipStyle);
        chipLeft_.push_back(x);
        chipRight_.push_back(x + chipWidth);
        x += chipWidth + margin;
    }
}

int DisplayManageScreen::ChipAt(double x, double y) const {
    if (y < chipTop_ || y > chipBottom_) {
        return -1;
    }
    for (size_t i = 0; i < chipLeft_.size(); ++i) {
        if (x >= chipLeft_[i] && x <= chipRight_[i]) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

DisplayManageHit DisplayManageScreen::OnTouch(int phase, double x, double y) {
    DisplayManageHit hit;
    const double small = dimens::kSmallMargin * density_;
    const double button = kIconButton * density_;
    const double margin = dimens::kNormalMargin * density_;
    const double iconSize = dimens::kMaterialIconSize * density_;
    const double rowHeight = kRowHeight * density_;

    if (phase == 0) {  // Down
        downX_ = x;
        downY_ = y;
        lastY_ = y;
        scrolled_ = false;
        // The handle is the drag, exactly as CardDisplayAdapter.kt:52-57 has it.
        onHandle_ = x <= small + button && RowAt(y) >= 0;
        return hit;
    }

    if (phase == 1) {  // Move
        const double dx = x - downX_;
        const double dy = y - downY_;
        if (dragIndex_ >= 0) {
            dragDY_ = dy;
            int target = dragFrom_ + static_cast<int>(std::lround(dragDY_ / rowHeight));
            target = std::max(0, std::min(static_cast<int>(items_.size()) - 1, target));
            if (target != dragIndex_) {
                MoveItem(dragIndex_, target);
                dragIndex_ = target;
            }
            lastY_ = y;
            return hit;
        }
        if (swipeIndex_ >= 0) {
            swipeDX_ = dx;
            lastY_ = y;
            return hit;
        }
        if (onHandle_ && std::fabs(dy) > kTouchSlop * density_) {
            dragFrom_ = RowAt(downY_);
            dragIndex_ = dragFrom_;
            dragDY_ = dy;
            lastY_ = y;
            return hit;
        }
        if (!onHandle_ && std::fabs(dx) > kTouchSlop * density_ && std::fabs(dx) > std::fabs(dy)) {
            swipeIndex_ = RowAt(downY_);
            swipeDX_ = dx;
            lastY_ = y;
            return hit;
        }
        if (std::fabs(dy) > kTouchSlop * density_) {
            scrolled_ = true;
            ScrollBy(y - lastY_);
        }
        lastY_ = y;
        return hit;
    }

    // Up or Cancel.
    const bool cancelled = phase == 3;
    if (dragIndex_ >= 0) {
        const bool moved = dragIndex_ != dragFrom_;
        dragIndex_ = -1;
        dragFrom_ = -1;
        dragDY_ = 0.0;
        onHandle_ = false;
        if (moved && !cancelled) {
            hit.action = DisplayManageAction::Changed;
            hit.key = DisplayListKey(list_);
            hit.value = Value();
        }
        return hit;
    }
    if (swipeIndex_ >= 0) {
        const int index = swipeIndex_;
        const double travelled = std::fabs(swipeDX_);
        swipeIndex_ = -1;
        swipeDX_ = 0.0;
        if (!cancelled && travelled > width_ * kSwipeCommit) {
            RemoveAt(index);
            hit.action = DisplayManageAction::Changed;
            hit.key = DisplayListKey(list_);
            hit.value = Value();
        }
        return hit;
    }
    onHandle_ = false;
    if (cancelled || scrolled_) {
        scrolled_ = false;
        return hit;
    }

    if (y >= topInset_ && y < ListTop() && x < margin + iconSize + margin) {
        hit.action = DisplayManageAction::Close;
        return hit;
    }

    const int chip = ChipAt(x, y);
    if (chip >= 0) {
        RestoreChip(chip);
        hit.action = DisplayManageAction::Changed;
        hit.key = DisplayListKey(list_);
        hit.value = Value();
        return hit;
    }

    const int row = RowAt(y);
    if (row < 0) {
        return hit;
    }
    const DisplayItem& item = items_[static_cast<size_t>(row)];
    const double closeLeft = width_ - small - button;
    if (x >= closeLeft) {
        RemoveAt(row);
        hit.action = DisplayManageAction::Changed;
        hit.key = DisplayListKey(list_);
        hit.value = Value();
        return hit;
    }
    if (!item.configures.empty() && x >= closeLeft - button && x < closeLeft) {
        hit.action = DisplayManageAction::OpenList;
        hit.destination = item.configures;
        return hit;
    }
    return hit;
}

}  // namespace breezy
