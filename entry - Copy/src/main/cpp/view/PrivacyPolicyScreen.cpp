#include "PrivacyPolicyScreen.h"

#include <algorithm>

#include "../data/AboutCatalogue.h"
#include "../render/AssetImages.h"
#include "../render/Shapes.h"
#include "../render/TextPainter.h"
#include "../render/Theme.h"

namespace breezy {
namespace {

/* LargeTopAppBar, as every other screen-3 page uses. */
constexpr double kBarRowHeight = 64.0;
constexpr double kBarExpandedHeight = 152.0;
constexpr double kHeadlineTextSize = 28.0;

constexpr double kRowHPadding = 16.0;
constexpr double kRowTitleSize = 16.0;    // typography.titleMedium
constexpr double kRowSummarySize = 14.0;  // typography.bodyMedium
constexpr double kDialogPadding = 24.0;

}  // namespace

void PrivacyPolicyScreen::SetGeometry(double widthPx, double heightPx, float density,
                                      double topInsetPx, double bottomInsetPx) {
    width_ = widthPx;
    height_ = heightPx;
    density_ = density;
    topInset_ = topInsetPx;
    bottomInset_ = bottomInsetPx;
}

double PrivacyPolicyScreen::ContentTop() const {
    return topInset_ + kBarExpandedHeight * density_;
}

void PrivacyPolicyScreen::Reset() {
    scroll_.Reset();
    pendingLink_.clear();
}

void PrivacyPolicyScreen::Draw(OH_Drawing_Canvas* canvas) {
    // Compose typography: FontFamily.Default. See SystemFontScope.
    SystemFontScope systemFont;
    if (width_ <= 0.0 || height_ <= 0.0) {
        return;
    }
    const double margin = dimens::kNormalMargin * density_;
    const double small = dimens::kSmallMargin * density_;
    const double iconSize = dimens::kMaterialIconSize * density_;
    const uint32_t onSurface = kOnSurface.Resolve(dark_);
    const uint32_t card = kSettingsCard.Resolve(dark_);
    const double outer = dimens::kSettingsCardCornerRadius * density_;
    const double inner = dimens::kSettingsCardInnerCornerRadius * density_;

    FillRect(canvas, 0.0, 0.0, width_, height_, kSurface.Resolve(dark_));

    DrawAsset(canvas, "ic_back.svg", margin,
              topInset_ + (kBarRowHeight * density_ - iconSize) / 2.0, iconSize, onSurface, true);

    TextStyleSpec headline;
    headline.sizeVp = kHeadlineTextSize;
    headline.color = onSurface;
    headline.singleLine = true;
    const std::string title = "Privacy policy";  // strings.xml `about_privacy_policy`
    const double headlineHeight = MeasureTextHeight(title, width_ - margin * 2.0, headline);
    DrawText(canvas, title, margin,
             topInset_ + LargeTitleTop(headlineHeight, density_),
             width_ - margin * 2.0, headline);

    const double contentTop = ContentTop();
    OH_Drawing_CanvasSave(canvas);
    OH_Drawing_Rect* clip =
        OH_Drawing_RectCreate(0.0f, static_cast<float>(contentTop), static_cast<float>(width_),
                              static_cast<float>(height_));
    OH_Drawing_CanvasClipRect(canvas, clip, INTERSECT, true);
    OH_Drawing_RectDestroy(clip);

    bands_.clear();
    const double left = margin;
    const double cardWidth = width_ - margin * 2.0;
    const double textLeft = left + kRowHPadding * density_;
    const double textWidth = cardWidth - kRowHPadding * 2.0 * density_;
    double y = contentTop - scroll_.Position();

    TextStyleSpec titleStyle;
    titleStyle.sizeVp = kRowTitleSize;
    titleStyle.bold = true;
    titleStyle.color = onSurface;
    TextStyleSpec summaryStyle;
    summaryStyle.sizeVp = kRowSummarySize;
    summaryStyle.color = kOnSurfaceVariant.Resolve(dark_);

    const std::vector<PrivacyPolicyRow>& rows = PrivacyPolicyRows();
    for (size_t i = 0; i < rows.size(); ++i) {
        const PrivacyPolicyRow& row = rows[i];
        const double rowHeight =
            PreferenceRowHeight(row.name, row.url, textWidth, titleStyle, summaryStyle, density_);
        const bool isFirst = i == 0;
        const bool isLast = i + 1 == rows.size();

        if (y + rowHeight >= contentTop && y <= height_) {
            FillRoundRectCorners(canvas, left, y, left + cardWidth, y + rowHeight,
                                 isFirst ? outer : inner, isFirst ? outer : inner,
                                 isLast ? outer : inner, isLast ? outer : inner, card);
            DrawPreferenceRowText(canvas, row.name, row.url, textLeft, y, rowHeight, textWidth,
                                  titleStyle, summaryStyle, density_);
        }
        bands_.push_back({y, y + rowHeight, row.url});
        y += rowHeight;
        if (!isLast) {
            y += dimens::kSettingsSmallSeparator * density_;
        }
    }

    y += margin + bottomInset_;
    contentHeight_ = y + scroll_.Position() - contentTop;
    OH_Drawing_CanvasRestore(canvas);

    /* The open confirmation, over everything — the same bubble the About screen draws. */
    okBand_ = Rect{};
    if (!pendingLink_.empty()) {
        FillRect(canvas, 0.0, 0.0, width_, height_, 0x80000000);

        TextStyleSpec message;
        message.sizeVp = kRowTitleSize;
        message.color = onSurface;
        TextStyleSpec action;
        action.sizeVp = kRowTitleSize;
        action.color = kPrimary.Resolve(dark_);
        action.singleLine = true;

        const double dialogWidth = width_ * 0.8;
        const double dialogLeft = (width_ - dialogWidth) / 2.0;
        const double pad = kDialogPadding * density_;
        const double innerWidth = dialogWidth - pad * 2.0;
        const std::string text = "Open " + pendingLink_ + "?";
        const double textHeight = MeasureTextHeight(text, innerWidth, message);
        const double actionHeight = MeasureTextHeight("OK", innerWidth, action);
        const double dialogHeight = pad * 2.0 + textHeight + margin + actionHeight;
        const double dialogTop = (height_ - dialogHeight) / 2.0;

        FillRoundRect(canvas, dialogLeft, dialogTop, dialogLeft + dialogWidth,
                      dialogTop + dialogHeight, outer, card);
        DrawText(canvas, text, dialogLeft + pad, dialogTop + pad, innerWidth, message);

        const double actionsY = dialogTop + pad + textHeight + margin;
        const double okWidth = MeasureTextWidth("OK", action);
        const double cancelWidth = MeasureTextWidth("Cancel", action);
        const double okLeft = dialogLeft + dialogWidth - pad - okWidth;
        const double cancelLeft = okLeft - margin - cancelWidth;
        DrawText(canvas, "Cancel", cancelLeft, actionsY, cancelWidth + 1.0, action);
        DrawText(canvas, "OK", okLeft, actionsY, okWidth + 1.0, action);
        okBand_ = {okLeft - small, actionsY - small, okLeft + okWidth + small,
                   actionsY + actionHeight + small};
    }
}

PrivacyHit PrivacyPolicyScreen::HitTest(double x, double y) {
    PrivacyHit hit;
    const double margin = dimens::kNormalMargin * density_;
    const double iconSize = dimens::kMaterialIconSize * density_;

    if (!pendingLink_.empty()) {
        const bool inOk = x >= okBand_.left && x < okBand_.right && y >= okBand_.top &&
                          y < okBand_.bottom;
        if (inOk) {
            hit.action = PrivacyAction::OpenLink;
            hit.destination = pendingLink_;
        }
        pendingLink_.clear();
        return hit;
    }

    if (y >= topInset_ && y < topInset_ + kBarRowHeight * density_) {
        if (x < margin + iconSize) {
            hit.action = PrivacyAction::Close;
        }
        return hit;
    }
    if (y < ContentTop()) {
        return hit;
    }
    for (const Band& band : bands_) {
        if (y >= band.top && y < band.bottom) {
            // The row RAISES the confirmation; only its OK opens anything.
            pendingLink_ = band.url;
            return hit;
        }
    }
    return hit;
}

bool PrivacyPolicyScreen::ScrollBy(double deltaY) {
    const double viewport = height_ - ContentTop();
    scroll_.SetRange(std::max(0.0, contentHeight_ - viewport));
    const double before = scroll_.Position();
    scroll_.Drag(-deltaY);
    return scroll_.Position() != before;
}

bool PrivacyPolicyScreen::Advance(double seconds) {
    return scroll_.Advance(seconds);
}

void PrivacyPolicyScreen::StopScrolling() {
    scroll_.Stop();
}

}  // namespace breezy
