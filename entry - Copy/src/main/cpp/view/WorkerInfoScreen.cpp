#include "WorkerInfoScreen.h"

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

/** The block is bodyMedium — 14 sp on a 20 sp line. */
constexpr double kBodyTextSize = 14.0;
constexpr double kBodyLineHeight = 20.0;

/** strings.xml settings_background_updates_worker_info_title. */
const char* kTitle = "Worker info";
/**
 * The three section names are `translatable="false"` in strings.xml, so they are these literals in
 * every locale.
 */
const char* kSections[3] = {"Enqueued", "Finished", "Running"};
/** kt:175-181 — an empty state list renders as a single "-". */
const char* kEmptySection = "-";

}  // namespace

void WorkerInfoScreen::SetGeometry(double widthPx, double heightPx, float density,
                                   double topInsetPx, double bottomInsetPx) {
    width_ = widthPx;
    height_ = heightPx;
    density_ = density;
    topInset_ = topInsetPx;
    bottomInset_ = bottomInsetPx;
}

double WorkerInfoScreen::ListTop() const {
    return topInset_ + kBarExpandedHeight * density_;
}

void WorkerInfoScreen::Draw(OH_Drawing_Canvas* canvas) {
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
    /*
     * NO ⓘ here. `AboutActivityIconButton` is used by TEN settings screens (AppBar.kt:32 and its
     * nine callers) but NOT by WorkerInfoActivity, whose FitStatusBarTopAppBar (kt:79-85) takes
     * only a title and onBackPressed with no `actions`. An earlier version of this file drew one
     * by copying the Debug screen's bar.
     */
    DrawAsset(canvas, "ic_back.svg", margin,
              topInset_ + (kBarRowHeight * density_ - iconSize) / 2.0, iconSize, onSurface, true);

    TextStyleSpec headline;
    headline.sizeVp = kHeadlineTextSize;
    headline.color = onSurface;
    headline.singleLine = true;
    const double headlineHeight = MeasureTextHeight(kTitle, width_ - margin * 2.0, headline);
    DrawText(canvas, kTitle, margin, topInset_ + LargeTitleTop(headlineHeight, density_),
             width_ - margin * 2.0, headline);

    /* ---- three sections, each a header over one card ---- */
    TextStyleSpec sectionStyle;
    sectionStyle.sizeVp = kBodyTextSize;
    sectionStyle.bold = true;
    sectionStyle.color = kPrimary.Resolve(dark_);

    TextStyleSpec bodyStyle;
    bodyStyle.sizeVp = kBodyTextSize;
    bodyStyle.color = onSurface;
    /*
     * `FontFamily.Monospace` (kt:136). TextPainter has no family selector, so this run uses the app
     * face at the same size — the block's only content in this port is a single "-", which is
     * identical in any family, so nothing measurable rides on it. Recorded rather than silently
     * dropped: a build that ever gained real worker output would need the monospace face here.
     */
    const double outer = dimens::kSettingsCardCornerRadius * density_;
    const double cardWidth = width_ - margin * 2.0;
    const double textWidth = cardWidth - margin * 2.0;

    double y = ListTop();
    for (int i = 0; i < 3; ++i) {
        // The shared section header — 16 + 20n + 16 dp around a bodyMedium line.
        const double headerHeight = SectionHeaderHeight(kSections[i], textWidth, sectionStyle,
                                                        density_);
        DrawTextFixed(canvas, kSections[i], margin, y + margin, textWidth, sectionStyle,
                      kBodyLineHeight * density_);
        y += headerHeight;

        const double bodyHeight = kBodyLineHeight * density_;
        const double cardHeight = bodyHeight + margin * 2.0;
        FillRoundRect(canvas, margin, y, width_ - margin, y + cardHeight, outer,
                      kSettingsCard.Resolve(dark_));
        DrawTextFixed(canvas, kEmptySection, margin + margin, y + margin, textWidth, bodyStyle,
                      bodyHeight);
        y += cardHeight;
    }
}

WorkerInfoHit WorkerInfoScreen::HitTest(double x, double y) {
    WorkerInfoHit hit;
    const double margin = dimens::kNormalMargin * density_;
    const double iconSize = dimens::kMaterialIconSize * density_;

    if (y >= topInset_ && y < topInset_ + kBarRowHeight * density_ && x < margin + iconSize) {
        hit.action = WorkerInfoAction::Close;
    }
    // Nothing else on this screen is tappable — the Kotlin's rows are plain text blocks.
    return hit;
}

}  // namespace breezy
