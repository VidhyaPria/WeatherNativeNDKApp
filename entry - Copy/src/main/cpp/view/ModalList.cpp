#include "view/ModalList.h"

#include <native_drawing/drawing_brush.h>
#include <native_drawing/drawing_pen.h>
#include <native_drawing/drawing_point.h>
#include <native_drawing/drawing_rect.h>
#include <native_drawing/drawing_round_rect.h>

#include <algorithm>

#include "render/Shapes.h"
#include "render/TextPainter.h"

namespace breezy {
namespace {

/** `.alpha(if (enabled) 1f else 0.5f)` — Preference.kt:159 and ListPreference.kt:398. */
constexpr double kDisabledAlpha = 0.5;

/*
 * FillRect / FillRoundRect used to be re-implemented here, byte-identical to render/Shapes.h's.
 * They are the header's now, because DrawSwitch (also the header's) needs FillCircle as well and
 * two definitions of the same name in one translation unit is an ambiguity, not a convenience.
 */

/** The Material radio control: a ring, filled with a dot when selected. */
void DrawRadio(OH_Drawing_Canvas* canvas, double centreX, double centreY, double size,
               bool selected, uint32_t checkedColor, uint32_t borderColor) {
    OH_Drawing_Pen* pen = OH_Drawing_PenCreate();
    OH_Drawing_PenSetAntiAlias(pen, true);
    OH_Drawing_PenSetWidth(pen, static_cast<float>(size * 0.083));  // 2 dp on a 24 dp control
    OH_Drawing_PenSetColor(pen, selected ? checkedColor : borderColor);
    OH_Drawing_CanvasAttachPen(canvas, pen);
    OH_Drawing_Point* centre = OH_Drawing_PointCreate(static_cast<float>(centreX),
                                                      static_cast<float>(centreY));
    OH_Drawing_CanvasDrawCircle(canvas, centre, static_cast<float>(size * 0.4));
    OH_Drawing_CanvasDetachPen(canvas);
    OH_Drawing_PointDestroy(centre);
    OH_Drawing_PenDestroy(pen);

    if (selected) {
        OH_Drawing_Brush* brush = OH_Drawing_BrushCreate();
        OH_Drawing_BrushSetAntiAlias(brush, true);
        OH_Drawing_BrushSetColor(brush, checkedColor);
        OH_Drawing_CanvasAttachBrush(canvas, brush);
        OH_Drawing_Point* dot = OH_Drawing_PointCreate(static_cast<float>(centreX),
                                                       static_cast<float>(centreY));
        OH_Drawing_CanvasDrawCircle(canvas, dot, static_cast<float>(size * 0.21));
        OH_Drawing_CanvasDetachBrush(canvas);
        OH_Drawing_PointDestroy(dot);
        OH_Drawing_BrushDestroy(brush);
    }
}

}  // namespace

double ModalList::TitleHeight() const {
    return title_.empty() ? 0.0 : (kHeadlineSmall * 1.35 + dimens::kNormalMargin) * density_;
}

double ModalList::ButtonsHeight() const {
    if (buttons_.empty()) {
        return 0.0;
    }
    return (dimens::kContentTextSize * 1.45 + dimens::kSmallMargin * 3.0) * density_;
}

double ModalList::NoteHeight() const {
    if (note_.empty()) {
        return 0.0;
    }
    TextStyleSpec style;
    style.sizeVp = dimens::kContentTextSize;
    const double inner = PanelWidth() - kPanelPadding * density_ * 2.0 -
                         dimens::kNormalMargin * density_ * 2.0;
    return MeasureTextHeight(note_, inner, style) +
           (dimens::kNormalMargin * 2.0 + dimens::kSmallMargin) * density_;
}

double ModalList::WarningHeight() const {
    if (warning_.empty()) {
        return 0.0;
    }
    TextStyleSpec style;
    style.sizeVp = dimens::kSubtitleTextSize;
    const double inner = PanelWidth() - kPanelPadding * density_ * 2.0 -
                         dimens::kNormalMargin * density_ * 2.0;
    return MeasureTextHeight(warning_, inner, style) + dimens::kSmallMargin * density_;
}

double ModalList::EntryHeight(size_t index) const {
    const ModalEntry& entry = entries_[index];
    const double divider = entry.dividerAbove ? kDividerHeight * density_ : 0.0;
    if (entry.header) {
        // large_margin above unless it is the first line, small_margin below.
        const double above = index == 0 ? dimens::kSmallMargin : dimens::kLargeMargin;
        return (kTitleSmall * 1.45 + above + dimens::kSmallMargin) * density_ + divider;
    }
    /*
     * A toggle row is a BARE ListItem — the non-card SwitchPreferenceView (SwitchPreference.kt:
     * 155-230) passes no outer padding — so it is the 56/72 dp token alone, where a PreferenceView
     * row adds Preference.kt:148-166's 8 dp a side on top.
     */
    if (entry.toggle) {
        return (entry.sub.empty() ? kListItemOneLine : kListItemTwoLine) * density_ + divider;
    }
    if (!entry.sub.empty()) {
        // PreferenceView (Preference.kt:148-166): a two-line M3 ListItem inside 8 dp of vertical
        // padding. The ListItem's height is fixed by the spec, not by what the text measures.
        const double token = (kListItemTwoLine + dimens::kSmallMargin * 2.0) * density_;
        if (!entry.wrapSub) {
            return token + divider;
        }
        // A wrapping summary makes the ListItem three-line or taller, so the token becomes a FLOOR
        // rather than the height. Measured in the same face and width the draw uses.
        TextStyleSpec sub;
        sub.sizeVp = dimens::kContentTextSize;
        const double inner = PanelWidth() - kPanelPadding * density_ * 2.0 -
                             dimens::kNormalMargin * density_ * 2.0;
        const double content = (dimens::kTitleTextSize * 1.45 + dimens::kSmallMargin) * density_ +
                               MeasureTextHeight(entry.sub, inner, sub) +
                               dimens::kSmallMargin * 2.0 * density_;
        return std::max(token, content) + divider;
    }
    return kRadioRowHeight * density_ + divider;
}

double ModalList::BodyHeight() const {
    double total = 0.0;
    for (size_t i = 0; i < entries_.size(); ++i) {
        total += EntryHeight(i);
    }
    return total;
}

double ModalList::PanelHeight() const {
    const double wanted = kPanelPadding * density_ * 2.0 + TitleHeight() + NoteHeight() +
                          WarningHeight() + BodyHeight() + ButtonsHeight();
    return std::min(wanted, height_ * kMaxPanelHeightFraction);
}

double ModalList::BodyTop() const {
    return PanelTop() + kPanelPadding * density_ + TitleHeight() + NoteHeight() + WarningHeight();
}

double ModalList::BodyViewport() const {
    return std::max(0.0, PanelTop() + PanelHeight() - kPanelPadding * density_ -
                             ButtonsHeight() - BodyTop());
}

void ModalList::ScrollBy(double dy) {
    const double maxScroll = std::max(0.0, BodyHeight() - BodyViewport());
    scroll_ = std::max(0.0, std::min(maxScroll, scroll_ - dy));
}

void ModalList::Draw(OH_Drawing_Canvas* canvas) const {
    const uint32_t onSurface = kOnSurface.Resolve(dark_);
    const uint32_t onSurfaceVariant = kOnSurfaceVariant.Resolve(dark_);
    const uint32_t primary = kPrimary.Resolve(dark_);
    // The dialog sits on the same 2 dp elevated surface the search screen does.
    const uint32_t surface = ElevatedSurface(2.0, primary, kSurface.Resolve(dark_));

    FillRect(canvas, 0.0, 0.0, width_, height_, WithAlpha(0xFF000000u, kScrim));

    const double panelLeft = PanelLeft();
    const double panelRight = panelLeft + PanelWidth();
    const double panelTop = PanelTop();
    const double panelBottom = panelTop + PanelHeight();
    FillRoundRect(canvas, panelLeft, panelTop, panelRight, panelBottom,
                  dimens::kCardCornerRadius * density_, surface);

    const double padding = kPanelPadding * density_;
    const double contentLeft = panelLeft + padding;
    const double contentWidth = PanelWidth() - padding * 2.0;

    double y = panelTop + padding;
    if (!title_.empty()) {
        TextStyleSpec style;
        style.sizeVp = kHeadlineSmall;
        style.color = onSurface;
        style.singleLine = true;
        DrawText(canvas, title_, contentLeft, y, contentWidth, style);
        y += TitleHeight();
    }

    if (!note_.empty()) {
        // A filled secondary-container card, as the Kotlin's own note is.
        TextStyleSpec style;
        style.sizeVp = dimens::kContentTextSize;
        style.color = onSurfaceVariant;
        const double inner = contentWidth - dimens::kNormalMargin * density_ * 2.0;
        const double textHeight = MeasureTextHeight(note_, inner, style);
        const double boxHeight = textHeight + dimens::kNormalMargin * 2.0 * density_;
        FillRoundRect(canvas, contentLeft, y, contentLeft + contentWidth, y + boxHeight,
                      dimens::kCardCornerRadius * density_, kSurfaceVariant.Resolve(dark_));
        DrawText(canvas, note_, contentLeft + dimens::kNormalMargin * density_,
                 y + dimens::kNormalMargin * density_, inner, style);
        y += NoteHeight();
    }

    if (!warning_.empty()) {
        TextStyleSpec style;
        style.sizeVp = dimens::kSubtitleTextSize;
        style.color = kAlertText.Resolve(dark_);
        DrawText(canvas, warning_, contentLeft + dimens::kNormalMargin * density_, y,
                 contentWidth - dimens::kNormalMargin * density_ * 2.0, style);
        y += WarningHeight();
    }

    /* ---- the body, clipped to its viewport ---- */
    const double bodyTop = BodyTop();
    const double bodyBottom = bodyTop + BodyViewport();
    OH_Drawing_CanvasSave(canvas);
    OH_Drawing_Rect* clip = OH_Drawing_RectCreate(
        static_cast<float>(panelLeft), static_cast<float>(bodyTop),
        static_cast<float>(panelRight), static_cast<float>(bodyBottom));
    OH_Drawing_CanvasClipRect(canvas, clip, OH_Drawing_CanvasClipOp::INTERSECT, true);
    OH_Drawing_RectDestroy(clip);

    double rowY = bodyTop - scroll_;
    for (size_t i = 0; i < entries_.size(); ++i) {
        const ModalEntry& entry = entries_[i];
        const double rowHeight = EntryHeight(i);
        if (rowY + rowHeight >= bodyTop && rowY <= bodyBottom) {
            const double alpha = entry.enabled ? 1.0 : kDisabledAlpha;
            /*
             * The rule is drawn first and the row below it, so `rowY` advances past it before
             * anything else measures from it. DividerDefaults.color is Material's outlineVariant.
             */
            double contentY = rowY;
            if (entry.dividerAbove) {
                FillRect(canvas, contentLeft, contentY, contentLeft + contentWidth,
                         contentY + kDividerHeight * density_, kOutlineVariant.Resolve(dark_));
                contentY += kDividerHeight * density_;
            }
            const double innerHeight = rowHeight - (contentY - rowY);
            if (entry.toggle) {
                const double left = contentLeft + dimens::kNormalMargin * density_;
                const double switchRoom = (kSwitchWidth + dimens::kSmallMargin) * density_;
                const double innerWidth =
                    contentWidth - dimens::kNormalMargin * density_ * 2.0 - switchRoom;
                TextStyleSpec title;
                title.sizeVp = dimens::kTitleTextSize;
                title.color = WithAlpha(onSurface, alpha);
                title.medium = true;
                title.singleLine = true;
                if (entry.sub.empty()) {
                    const double h = MeasureTextHeight(entry.label, innerWidth, title);
                    DrawText(canvas, entry.label, left, contentY + (innerHeight - h) / 2.0,
                             innerWidth, title);
                } else {
                    TextStyleSpec sub;
                    sub.sizeVp = dimens::kContentTextSize;
                    sub.color = WithAlpha(onSurfaceVariant, alpha);
                    const double subHeight = MeasureTextHeight(entry.sub, innerWidth, sub);
                    const double titleHeight = dimens::kTitleTextSize * 1.45 * density_;
                    const double block =
                        titleHeight + dimens::kSmallMargin * density_ + subHeight;
                    const double top = contentY + (innerHeight - block) / 2.0;
                    DrawText(canvas, entry.label, left, top, innerWidth, title);
                    DrawText(canvas, entry.sub, left,
                             top + titleHeight + dimens::kSmallMargin * density_, innerWidth, sub);
                }
                DrawSwitch(canvas, contentLeft + contentWidth - dimens::kNormalMargin * density_,
                           contentY + innerHeight / 2.0, entry.selected, entry.enabled, density_,
                           WithAlpha(primary, alpha), WithAlpha(onSurfaceVariant, alpha),
                           kSurfaceVariant.Resolve(dark_));
            } else if (entry.header) {
                TextStyleSpec style;
                style.sizeVp = kTitleSmall;
                style.color = onSurface;
                style.bold = true;
                style.alignment = TextAlignment::Center;
                const double above =
                    (i == 0 ? dimens::kSmallMargin : dimens::kLargeMargin) * density_;
                DrawText(canvas, entry.label, contentLeft, contentY + above, contentWidth, style);
            } else if (!entry.sub.empty()) {
                TextStyleSpec title;
                title.sizeVp = dimens::kTitleTextSize;
                title.color = WithAlpha(onSurface, alpha);
                title.bold = true;
                title.singleLine = true;
                TextStyleSpec sub;
                sub.sizeVp = dimens::kContentTextSize;
                sub.color = WithAlpha(onSurfaceVariant, alpha);
                sub.singleLine = !entry.wrapSub;
                const double left = contentLeft + dimens::kNormalMargin * density_;
                const double innerWidth = contentWidth - dimens::kNormalMargin * density_ * 2.0;
                // A ListItem centres its content in its fixed height rather than sitting it at the
                // top, so the two lines are centred in the row rather than pinned under its edge.
                const double contentHeight =
                    (dimens::kTitleTextSize * 1.45 + dimens::kSmallMargin) * density_ +
                    (entry.wrapSub ? MeasureTextHeight(entry.sub, innerWidth, sub)
                                   : dimens::kContentTextSize * 1.45 * density_);
                const double top = contentY + (innerHeight - contentHeight) / 2.0;
                DrawText(canvas, entry.label, left, top, innerWidth, title);
                DrawText(canvas, entry.sub, left,
                         top + (dimens::kTitleTextSize * 1.45 + dimens::kSmallMargin) * density_,
                         innerWidth, sub);
            } else {
                const double control = dimens::kMaterialIconSize * density_;
                const double left = contentLeft + dimens::kSmallMargin * density_;
                DrawRadio(canvas, left + control / 2.0, contentY + innerHeight / 2.0, control,
                          entry.selected, WithAlpha(primary, alpha),
                          WithAlpha(onSurfaceVariant, alpha));
                TextStyleSpec style;
                style.sizeVp = dimens::kTitleTextSize;
                style.color = WithAlpha(onSurface, alpha);
                style.singleLine = true;
                const double textLeft = left + control + dimens::kSmallMargin * density_;
                const double textWidth = contentLeft + contentWidth - textLeft -
                                         dimens::kSmallMargin * density_;
                const double textHeight = MeasureTextHeight(entry.label, textWidth, style);
                DrawText(canvas, entry.label, textLeft, contentY + (innerHeight - textHeight) / 2.0,
                         textWidth, style);
            }
        }
        rowY += rowHeight;
    }
    OH_Drawing_CanvasRestore(canvas);

    /* ---- the trailing text buttons ---- */
    if (!buttons_.empty()) {
        TextStyleSpec style;
        style.sizeVp = dimens::kContentTextSize;
        style.bold = true;
        style.singleLine = true;
        double right = panelRight - padding;
        const double buttonTop = panelBottom - padding - ButtonsHeight() +
                                 dimens::kSmallMargin * density_;
        for (size_t i = buttons_.size(); i > 0; --i) {
            const size_t index = i - 1;
            style.color = WithAlpha(primary,
                                    static_cast<int>(index) == disabledButton_ ? kDisabledAlpha
                                                                               : 1.0);
            const double textWidth = MeasureTextWidth(buttons_[index], style);
            DrawText(canvas, buttons_[index], right - textWidth,
                     buttonTop + dimens::kSmallMargin * density_, textWidth + density_, style);
            right -= textWidth + dimens::kNormalMargin * density_ * 2.0;
        }
    }
}

ModalList::Hit ModalList::HitTest(double x, double y) const {
    Hit hit;
    const double panelLeft = PanelLeft();
    const double panelRight = panelLeft + PanelWidth();
    const double panelTop = PanelTop();
    const double panelBottom = panelTop + PanelHeight();
    if (x < panelLeft || x > panelRight || y < panelTop || y > panelBottom) {
        // Tapping outside is `onDismissRequest`.
        hit.kind = HitKind::Scrim;
        return hit;
    }

    const double padding = kPanelPadding * density_;
    if (!buttons_.empty() && y >= panelBottom - padding - ButtonsHeight()) {
        TextStyleSpec style;
        style.sizeVp = dimens::kContentTextSize;
        style.bold = true;
        style.singleLine = true;
        double right = panelRight - padding;
        for (size_t i = buttons_.size(); i > 0; --i) {
            const size_t index = i - 1;
            const double textWidth = MeasureTextWidth(buttons_[index], style);
            // The touch target is the label plus the gap either side of it.
            const double slack = dimens::kSmallMargin * density_;
            if (x >= right - textWidth - slack && x <= right + slack) {
                if (static_cast<int>(index) != disabledButton_) {
                    hit.kind = HitKind::Button;
                    hit.index = static_cast<int>(index);
                }
                return hit;
            }
            right -= textWidth + dimens::kNormalMargin * density_ * 2.0;
        }
        return hit;
    }

    const double bodyTop = BodyTop();
    const double bodyBottom = bodyTop + BodyViewport();
    if (y < bodyTop || y > bodyBottom) {
        return hit;
    }
    double rowY = bodyTop - scroll_;
    for (size_t i = 0; i < entries_.size(); ++i) {
        const double rowHeight = EntryHeight(i);
        if (y >= rowY && y <= rowY + rowHeight) {
            if (!entries_[i].header && entries_[i].enabled) {
                hit.kind = HitKind::Entry;
                hit.index = static_cast<int>(i);
            }
            return hit;
        }
        rowY += rowHeight;
    }
    return hit;
}

}  // namespace breezy
