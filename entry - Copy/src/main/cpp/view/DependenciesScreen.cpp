#include "DependenciesScreen.h"

#include <algorithm>
#include <string>
#include <vector>

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

/** AboutLibraries' row padding. */
constexpr double kRowPadding = 16.0;
constexpr double kBadgeHPadding = 10.0;
constexpr double kBadgeVPadding = 4.0;
constexpr double kBadgeRadius = 12.0;
/** The gap between the name and its right-aligned version. */
constexpr double kVersionGap = 8.0;

}  // namespace

void DependenciesScreen::SetGeometry(double widthPx, double heightPx, float density,
                                     double topInsetPx, double bottomInsetPx) {
    width_ = widthPx;
    height_ = heightPx;
    density_ = density;
    topInset_ = topInsetPx;
    bottomInset_ = bottomInsetPx;
}

double DependenciesScreen::ContentTop() const {
    return topInset_ + kBarExpandedHeight * density_;
}

void DependenciesScreen::Reset() {
    scroll_.Reset();
}

void DependenciesScreen::Draw(OH_Drawing_Canvas* canvas) {
    if (width_ <= 0.0 || height_ <= 0.0) {
        return;
    }
    const double margin = dimens::kNormalMargin * density_;
    const double small = dimens::kSmallMargin * density_;
    const double iconSize = dimens::kMaterialIconSize * density_;
    const uint32_t onSurface = kOnSurface.Resolve(dark_);

    FillRect(canvas, 0.0, 0.0, width_, height_, kSurface.Resolve(dark_));

    DrawAsset(canvas, "ic_back.svg", margin,
              topInset_ + (kBarRowHeight * density_ - iconSize) / 2.0, iconSize, onSurface, true);

    TextStyleSpec headline;
    headline.sizeVp = kHeadlineTextSize;
    headline.color = onSurface;
    headline.singleLine = true;
    const std::string title = "Dependencies";  // strings.xml `action_dependencies`
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

    const double pad = kRowPadding * density_;
    const double textLeft = pad;
    const double textWidth = width_ - pad * 2.0;
    double y = contentTop - scroll_.Position();

    TextStyleSpec nameStyle;
    nameStyle.sizeVp = dimens::kTitleTextSize;
    nameStyle.color = onSurface;
    nameStyle.singleLine = true;
    TextStyleSpec versionStyle;
    versionStyle.sizeVp = dimens::kContentTextSize;
    versionStyle.color = onSurface;
    versionStyle.singleLine = true;
    TextStyleSpec authorStyle;
    authorStyle.sizeVp = dimens::kContentTextSize;
    authorStyle.color = kOnSurfaceVariant.Resolve(dark_);
    TextStyleSpec badgeStyle;
    badgeStyle.sizeVp = dimens::kSubtitleTextSize;
    badgeStyle.color = kOnPrimaryContainer.Resolve(dark_);
    badgeStyle.singleLine = true;

    for (const DependencyItem& item : AppDependencies()) {
        const double versionWidth =
            item.version.empty() ? 0.0 : MeasureTextWidth(item.version, versionStyle);
        const double nameWidth =
            textWidth - (versionWidth > 0.0 ? versionWidth + kVersionGap * density_ : 0.0);
        const double nameH = MeasureTextHeight(item.name, nameWidth, nameStyle);
        const double authorH =
            item.author.empty() ? 0.0 : MeasureTextHeight(item.author, textWidth, authorStyle);
        const double badgeTextH =
            item.license.empty() ? 0.0 : MeasureTextHeight(item.license, textWidth, badgeStyle);
        const double badgeH = item.license.empty() ? 0.0 : badgeTextH + kBadgeVPadding * 2.0 * density_;

        double rowHeight = pad * 2.0 + nameH;
        if (authorH > 0.0) {
            rowHeight += 2.0 * density_ + authorH;
        }
        if (badgeH > 0.0) {
            rowHeight += small + badgeH;
        }

        if (y + rowHeight >= contentTop && y <= height_) {
            double lineY = y + pad;
            DrawText(canvas, item.name, textLeft, lineY, nameWidth, nameStyle);
            if (versionWidth > 0.0) {
                /*
                 * Both are centred on the same line — the version is the smaller of the two, so
                 * baselines set from the top would put it visibly high.
                 */
                const double versionH = MeasureTextHeight(item.version, versionWidth + 1.0,
                                                          versionStyle);
                DrawText(canvas, item.version, textLeft + textWidth - versionWidth,
                         lineY + (nameH - versionH) / 2.0, versionWidth + 1.0, versionStyle);
            }
            lineY += nameH;
            if (authorH > 0.0) {
                lineY += 2.0 * density_;
                DrawText(canvas, item.author, textLeft, lineY, textWidth, authorStyle);
                lineY += authorH;
            }
            if (badgeH > 0.0) {
                lineY += small;
                const double badgeWidth =
                    MeasureTextWidth(item.license, badgeStyle) + kBadgeHPadding * 2.0 * density_;
                FillRoundRect(canvas, textLeft, lineY, textLeft + badgeWidth, lineY + badgeH,
                              kBadgeRadius * density_, kPrimaryContainer.Resolve(dark_));
                DrawText(canvas, item.license, textLeft + kBadgeHPadding * density_,
                         lineY + kBadgeVPadding * density_, badgeWidth, badgeStyle);
            }
        }
        y += rowHeight;
    }

    y += margin + bottomInset_;
    contentHeight_ = y + scroll_.Position() - contentTop;
    OH_Drawing_CanvasRestore(canvas);
}

DependenciesHit DependenciesScreen::HitTest(double x, double y) {
    DependenciesHit hit;
    const double margin = dimens::kNormalMargin * density_;
    const double iconSize = dimens::kMaterialIconSize * density_;
    if (y >= topInset_ && y < topInset_ + kBarRowHeight * density_ && x < margin + iconSize) {
        hit.action = DependenciesAction::Close;
    }
    return hit;
}

bool DependenciesScreen::ScrollBy(double deltaY) {
    const double viewport = height_ - ContentTop();
    scroll_.SetRange(std::max(0.0, contentHeight_ - viewport));
    const double before = scroll_.Position();
    scroll_.Drag(-deltaY);
    return scroll_.Position() != before;
}

bool DependenciesScreen::Advance(double seconds) {
    return scroll_.Advance(seconds);
}

void DependenciesScreen::StopScrolling() {
    scroll_.Stop();
}

}  // namespace breezy
