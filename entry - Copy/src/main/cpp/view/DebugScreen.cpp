#include "DebugScreen.h"

#include <algorithm>

#include "../render/AssetImages.h"
#include "../render/Shapes.h"
#include "../render/TextPainter.h"
#include "../render/Theme.h"

namespace breezy {
namespace {

/* LargeTopAppBar, as every other screen-3 page uses. Material library values. */
constexpr double kBarRowHeight = 64.0;
constexpr double kBarExpandedHeight = 152.0;
constexpr double kHeadlineTextSize = 28.0;

constexpr double kRowHPadding = 16.0;
constexpr double kRowTitleSize = 16.0;    // typography.titleMedium
constexpr double kRowSummarySize = 14.0;  // typography.bodyMedium

/** strings.xml:862-863. */
const char* kRowTitle = "Share crash logs";
const char* kRowSummary = "Saves error logs to a file for sharing with the developers";

}  // namespace

void DebugScreen::SetGeometry(double widthPx, double heightPx, float density, double topInsetPx,
                              double bottomInsetPx) {
    width_ = widthPx;
    height_ = heightPx;
    density_ = density;
    topInset_ = topInsetPx;
    bottomInset_ = bottomInsetPx;
}

double DebugScreen::ListTop() const {
    return topInset_ + kBarExpandedHeight * density_;
}

void DebugScreen::Draw(OH_Drawing_Canvas* canvas) {
    // Compose typography: FontFamily.Default. See SystemFontScope.
    SystemFontScope systemFont;
    if (width_ <= 0.0 || height_ <= 0.0) {
        return;
    }
    const double margin = dimens::kNormalMargin * density_;
    const double iconSize = dimens::kMaterialIconSize * density_;
    const uint32_t onSurface = kOnSurface.Resolve(dark_);

    FillRect(canvas, 0.0, 0.0, width_, height_, kSurface.Resolve(dark_));

    /* ---- LargeTopAppBar ---- */
    DrawAsset(canvas, "ic_back.svg", margin,
              topInset_ + (kBarRowHeight * density_ - iconSize) / 2.0, iconSize, onSurface, true);
    DrawAsset(canvas, "ic_info.svg", width_ - margin - iconSize,
              topInset_ + (kBarRowHeight * density_ - iconSize) / 2.0, iconSize, onSurface, true);

    TextStyleSpec headline;
    headline.sizeVp = kHeadlineTextSize;
    headline.color = onSurface;
    headline.singleLine = true;
    const double headlineHeight = MeasureTextHeight("Debug", width_ - margin * 2.0, headline);
    DrawText(canvas, "Debug", margin,
             topInset_ + LargeTitleTop(headlineHeight, density_),
             width_ - margin * 2.0, headline);

    /* ---- the single row, a card of its own (isFirst AND isLast) ---- */
    const double outer = dimens::kSettingsCardCornerRadius * density_;
    const double textLeft = margin + kRowHPadding * density_;
    const double textWidth = width_ - margin - kRowHPadding * density_ - textLeft;

    TextStyleSpec titleStyle;
    titleStyle.sizeVp = kRowTitleSize;
    titleStyle.bold = true;
    titleStyle.color = onSurface;
    TextStyleSpec summaryStyle;
    summaryStyle.sizeVp = kRowSummarySize;
    summaryStyle.color = kOnSurfaceVariant.Resolve(dark_);

    const double rowHeight =

        PreferenceRowHeight(kRowTitle, kRowSummary, textWidth, titleStyle, summaryStyle, density_);
    const double top = ListTop();

    FillRoundRect(canvas, margin, top, width_ - margin, top + rowHeight, outer,
                  kSettingsCard.Resolve(dark_));
    DrawPreferenceRowText(canvas, kRowTitle, kRowSummary, textLeft, top, rowHeight, textWidth,
                          titleStyle, summaryStyle, density_);

    rowTop_ = top;
    rowBottom_ = top + rowHeight;
}

DebugHit DebugScreen::HitTest(double x, double y) {
    DebugHit hit;
    const double margin = dimens::kNormalMargin * density_;
    const double iconSize = dimens::kMaterialIconSize * density_;

    if (y >= topInset_ && y < topInset_ + kBarRowHeight * density_) {
        if (x < margin + iconSize) {
            hit.action = DebugAction::Close;
        } else if (x > width_ - margin - iconSize) {
            hit.action = DebugAction::Navigate;
            hit.destination = "About";
        }
        return hit;
    }

    if (y >= rowTop_ && y < rowBottom_ && rowBottom_ > rowTop_) {
        // The dump itself is not ported — see the header.
        hit.action = DebugAction::Navigate;
        hit.destination = kRowTitle;
    }
    return hit;
}

}  // namespace breezy
