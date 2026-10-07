#include "AboutScreen.h"

#include <algorithm>

#include "../data/AboutCatalogue.h"
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

/** The header block (kt:375-401). */
constexpr double kHeaderPadding = 24.0;
constexpr double kAppIconSize = 72.0;
constexpr double kBrandTextSize = 24.0;    // typography.headlineSmall
constexpr double kVersionTextSize = 12.0;  // typography.labelMedium
/** SectionTitle is labelMedium, padded a normal_margin all round (kt:404-411). */
constexpr double kSectionTitleSize = 12.0;

constexpr double kRowHPadding = 16.0;
constexpr double kRowTitleSize = 16.0;    // typography.titleMedium
constexpr double kRowSummarySize = 14.0;  // typography.bodyMedium
/** ListItemDefaults' gap between the leading icon and the headline. */
constexpr double kLeadingGap = 16.0;

}  // namespace

void AboutScreen::SetGeometry(double widthPx, double heightPx, float density, double topInsetPx,
                              double bottomInsetPx) {
    width_ = widthPx;
    height_ = heightPx;
    density_ = density;
    topInset_ = topInsetPx;
    bottomInset_ = bottomInsetPx;
}

double AboutScreen::ContentTop() const {
    return topInset_ + kBarExpandedHeight * density_;
}

void AboutScreen::Reset() {
    scroll_.Reset();
    pendingLink_.clear();
}

void AboutScreen::Draw(OH_Drawing_Canvas* canvas) {
    // Compose typography: FontFamily.Default. See SystemFontScope.
    SystemFontScope systemFont;
    if (width_ <= 0.0 || height_ <= 0.0) {
        return;
    }
    const double margin = dimens::kNormalMargin * density_;
    const double small = dimens::kSmallMargin * density_;
    const double iconSize = dimens::kMaterialIconSize * density_;
    const uint32_t onSurface = kOnSurface.Resolve(dark_);
    const uint32_t onSurfaceVariant = kOnSurfaceVariant.Resolve(dark_);
    const uint32_t card = kSettingsCard.Resolve(dark_);
    const double outer = dimens::kSettingsCardCornerRadius * density_;
    const double inner = dimens::kSettingsCardInnerCornerRadius * density_;

    FillRect(canvas, 0.0, 0.0, width_, height_, kSurface.Resolve(dark_));

    /* ---- LargeTopAppBar ---- */
    DrawAsset(canvas, "ic_back.svg", margin,
              topInset_ + (kBarRowHeight * density_ - iconSize) / 2.0, iconSize, onSurface, true);

    TextStyleSpec headline;
    headline.sizeVp = kHeadlineTextSize;
    headline.color = onSurface;
    headline.singleLine = true;
    const double headlineHeight = MeasureTextHeight("About", width_ - margin * 2.0, headline);
    DrawText(canvas, "About", margin,
             topInset_ + LargeTitleTop(headlineHeight, density_),
             width_ - margin * 2.0, headline);

    /* ---- the scrolling content ---- */
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
    double y = contentTop - scroll_.Position();

    TextStyleSpec titleStyle;
    titleStyle.sizeVp = kRowTitleSize;
    titleStyle.bold = true;
    titleStyle.color = onSurface;
    TextStyleSpec summaryStyle;
    summaryStyle.sizeVp = kRowSummarySize;
    summaryStyle.color = onSurfaceVariant;
    TextStyleSpec sectionStyle;
    sectionStyle.sizeVp = kSectionTitleSize;
    sectionStyle.color = onSurfaceVariant;
    sectionStyle.singleLine = true;

    /* Header (kt:375-401) — the icon, the brand name, the version, centred. */
    {
        const double blockTop = y + kHeaderPadding * density_;
        const double icon = kAppIconSize * density_;
        // `ic_launcher_round` (kt:381) — the PORT's launcher icon, the same image ArkUIApp draws
        // from `startIcon`. Not tinted: it carries its own colours.
        DrawAsset(canvas, "app_icon.png", (width_ - icon) / 2.0, blockTop, icon, 0, false);

        TextStyleSpec brand;
        brand.sizeVp = kBrandTextSize;
        brand.color = onSurface;
        brand.singleLine = true;
        brand.alignment = TextAlignment::Center;
        TextStyleSpec version;
        version.sizeVp = kVersionTextSize;
        version.color = onSurfaceVariant;
        version.singleLine = true;
        version.alignment = TextAlignment::Center;

        double textY = blockTop + icon + small;
        textY += DrawText(canvas, "Breezy Weather", 0.0, textY, width_, brand);
        textY += DrawText(canvas, version_, 0.0, textY, width_, version);
        y = textY + kHeaderPadding * density_;
    }

    /*
     * One row of a grouped card. `isFirst`/`isLast` shape the corners exactly as ArkUIApp's
     * PreferenceRow does — 28 vp on the group's outer edges, 2 vp on the inner ones.
     */
    const auto drawRow = [&](const std::string& icon, const std::string& title,
                             const std::string& summary, bool isFirst, bool isLast,
                             AboutAction action, const std::string& destination) {
        const bool hasIcon = !icon.empty();
        const double rowTextLeft = hasIcon ? textLeft + iconSize + kLeadingGap * density_ : textLeft;
        const double rowTextWidth = left + cardWidth - kRowHPadding * density_ - rowTextLeft;
        const double rowHeight =
            PreferenceRowHeight(title, summary, rowTextWidth, titleStyle, summaryStyle, density_);

        // Off-screen rows still advance `y` and record a band, so the scroll extent stays right.
        if (y + rowHeight >= contentTop && y <= height_) {
            FillRoundRectCorners(canvas, left, y, left + cardWidth, y + rowHeight,
                                 isFirst ? outer : inner, isFirst ? outer : inner,
                                 isLast ? outer : inner, isLast ? outer : inner, card);
            if (hasIcon) {
                DrawAsset(canvas, icon, textLeft, y + (rowHeight - iconSize) / 2.0, iconSize,
                          onSurface, true);
            }
            DrawPreferenceRowText(canvas, title, summary, rowTextLeft, y, rowHeight, rowTextWidth,
                                  titleStyle, summaryStyle, density_);
        }
        if (action != AboutAction::None) {
            bands_.push_back({y, y + rowHeight, action, destination});
        }
        y += rowHeight;
    };

    const auto drawSectionTitle = [&](const char* title) {
        y += margin;
        y += DrawText(canvas, title, left + margin, y, cardWidth - margin * 2.0, sectionStyle);
        y += margin;
    };

    /* The update card is a group of one (kt:184-279). */
    drawRow("ic_sync.svg", "Check for app updates", "", true, true, AboutAction::OpenLink,
            kReleasesLink);
    y += dimens::kSettingsLargeSeparator * density_;

    drawSectionTitle("Contact");  // strings.xml `about_contact`
    {
        const std::vector<AboutLinkItem>& items = AboutContactLinks();
        for (size_t i = 0; i < items.size(); ++i) {
            const bool hasUrl = !items[i].url.empty();
            drawRow(items[i].icon, items[i].title, "", i == 0, i + 1 == items.size(),
                    hasUrl ? AboutAction::OpenLink : AboutAction::Navigate,
                    hasUrl ? items[i].url : items[i].screen);
            if (i + 1 < items.size()) {
                y += dimens::kSettingsSmallSeparator * density_;
            }
        }
    }
    y += dimens::kSettingsLargeSeparator * density_;

    drawSectionTitle("About app");  // strings.xml `about_app`
    {
        const std::vector<AboutLinkItem>& items = AboutAppLinks();
        for (size_t i = 0; i < items.size(); ++i) {
            const bool hasUrl = !items[i].url.empty();
            drawRow(items[i].icon, items[i].title, "", i == 0, i + 1 == items.size(),
                    hasUrl ? AboutAction::OpenLink : AboutAction::Navigate,
                    hasUrl ? items[i].url : items[i].screen);
            if (i + 1 < items.size()) {
                y += dimens::kSettingsSmallSeparator * density_;
            }
        }
    }
    y += dimens::kSettingsLargeSeparator * density_;

    /* ContributorView (kt:474-509) — the name, and the contribution line when there is one. */
    const auto drawPeople = [&](const std::vector<ContributorItem>& items) {
        for (size_t i = 0; i < items.size(); ++i) {
            // kt:326-330 — a contributor with no link is not a link; the row does nothing.
            const bool tappable = !items[i].link.empty();
            drawRow("", items[i].name, items[i].contribution, i == 0, i + 1 == items.size(),
                    tappable ? AboutAction::OpenLink : AboutAction::None, items[i].link);
            if (i + 1 < items.size()) {
                y += dimens::kSettingsSmallSeparator * density_;
            }
        }
    };

    drawSectionTitle("Contributors");  // strings.xml `about_contributors`
    drawPeople(AppContributors());
    y += dimens::kSettingsLargeSeparator * density_;

    drawSectionTitle("Translators");  // strings.xml `about_translators`
    drawPeople(AppTranslators());

    // bottomInsetItem + getCardListItemMarginDp (kt:360-362).
    y += margin + bottomInset_;
    contentHeight_ = y + scroll_.Position() - contentTop;
    OH_Drawing_CanvasRestore(canvas);

    /* ---- AlertDialogLink, over everything ---- */
    cancelBand_ = Rect{};
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
        const double pad = kHeaderPadding * density_;
        const double innerWidth = dialogWidth - pad * 2.0;
        // strings.xml `about_open_link_message` — "Open %s?".
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
        cancelBand_ = {cancelLeft - small, actionsY - small, cancelLeft + cancelWidth + small,
                       actionsY + actionHeight + small};
        okBand_ = {okLeft - small, actionsY - small, okLeft + okWidth + small,
                   actionsY + actionHeight + small};
    }
}

AboutHit AboutScreen::HitTest(double x, double y) {
    AboutHit hit;
    const double margin = dimens::kNormalMargin * density_;
    const double iconSize = dimens::kMaterialIconSize * density_;

    /*
     * The dialog owns the whole surface while it is up: OK opens the link, and anything else
     * dismisses — which is what tapping outside an AlertDialog does.
     */
    if (!pendingLink_.empty()) {
        const bool inOk = x >= okBand_.left && x < okBand_.right && y >= okBand_.top &&
                          y < okBand_.bottom;
        if (inOk) {
            hit.action = AboutAction::OpenLink;
            hit.destination = pendingLink_;
        }
        pendingLink_.clear();
        return hit;
    }

    if (y >= topInset_ && y < topInset_ + kBarRowHeight * density_) {
        if (x < margin + iconSize) {
            hit.action = AboutAction::Close;
        }
        return hit;
    }
    if (y < ContentTop()) {
        return hit;
    }
    for (const Band& band : bands_) {
        if (y >= band.top && y < band.bottom) {
            // A link row RAISES the confirmation rather than opening anything; only its OK does.
            if (band.action == AboutAction::OpenLink) {
                pendingLink_ = band.destination;
                return hit;
            }
            hit.action = band.action;
            hit.destination = band.destination;
            return hit;
        }
    }
    return hit;
}

bool AboutScreen::ScrollBy(double deltaY) {
    const double viewport = height_ - ContentTop();
    scroll_.SetRange(std::max(0.0, contentHeight_ - viewport));
    const double before = scroll_.Position();
    scroll_.Drag(-deltaY);
    return scroll_.Position() != before;
}

void AboutScreen::Fling(double velocity) {
    const double viewport = height_ - ContentTop();
    scroll_.SetRange(std::max(0.0, contentHeight_ - viewport));
    scroll_.Fling(velocity);
}

bool AboutScreen::Advance(double seconds) {
    return scroll_.Advance(seconds);
}

void AboutScreen::StopScrolling() {
    scroll_.Stop();
}

}  // namespace breezy
