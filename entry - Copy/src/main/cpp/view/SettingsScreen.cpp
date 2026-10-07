#include "SettingsScreen.h"

#include <algorithm>

#include "../render/AssetImages.h"
#include "../render/Shapes.h"
#include "../render/TextPainter.h"
#include "../render/Theme.h"

namespace breezy {
namespace {

/*
 * LargeTopAppBar's metrics. Material Components library values, NOT app resources, and flagged as
 * such: the library is not vendored in this repository, so unlike every dimen in dimens.xml these
 * could not be extracted from it.
 */
constexpr double kBarRowHeight = 64.0;       // TopAppBarSmallTokens.ContainerHeight
constexpr double kBarExpandedHeight = 152.0; // TopAppBarLargeTokens.ContainerHeight
constexpr double kHeadlineTextSize = 28.0;   // typography.headlineMedium

/** ListItemDefaults' horizontal padding and the gap after the leading icon. */
constexpr double kRowHPadding = 16.0;
constexpr double kLeadingGap = 16.0;
/** MaterialTheme.typography.titleMedium / bodyMedium. */
constexpr double kRowTitleSize = 16.0;
constexpr double kRowSummarySize = 14.0;

/** RootSettingsScreen.kt:60-148, in order. Strings are strings.xml's, verbatim. */
struct Entry {
    const char* title;
    const char* summary;
    const char* icon;
    bool isFirst;
    bool isLast;
    bool endsGroup;
};

constexpr Entry kEntries[] = {
    {"Background updates", "Refresh rate, troubleshoot", "ic_sync.svg", true, true, true},
    {"Appearance", "Language, dark mode, icon pack, units", "ic_palette.svg", true, false, false},
    {"Main screen", "Blocks order, items displayed, animations", "ic_home.svg", false, true, true},
    {"Notifications",
     "Notifications of weather alerts, precipitations or forecast for the first location",
     "ic_notifications.svg", true, false, false},
    {"External modules", "Widgets, notification widget, wallpaper, data sharing with other apps",
     "ic_widgets.svg", false, true, true},
    {"Location", "Location services settings", "ic_location.svg", true, false, false},
    {"Weather sources",
     "Default weather source for new locations, custom API keys, other source settings",
     "ic_factory.svg", false, true, true},
    {"Debug", "Crash logs", "ic_bug_report.svg", true, true, false},
};

}  // namespace

void SettingsScreen::SetGeometry(double widthPx, double heightPx, float density, double topInsetPx,
                                 double bottomInsetPx) {
    width_ = widthPx;
    height_ = heightPx;
    density_ = density;
    topInset_ = topInsetPx;
    bottomInset_ = bottomInsetPx;
    Layout();
}

double SettingsScreen::ListTop() const {
    return topInset_ + kBarExpandedHeight * density_;
}

/**
 * Measures every row once per geometry.
 *
 * A row's height is its content, not a fixed token: the summaries wrap to two lines at this width
 * (Notifications and External modules do on a phone), and the Kotlin's ListItem grows with them.
 */
void SettingsScreen::Layout() {
    // Measured in the SAME face Draw paints in, or the cached heights would be Roboto's.
    SystemFontScope systemFont;
    if (width_ <= 0.0 || density_ <= 0.0f) {
        return;
    }
    if (laidOutWidth_ == width_ && laidOutDensity_ == density_ && !rows_.empty()) {
        return;
    }
    laidOutWidth_ = width_;
    laidOutDensity_ = density_;

    const double margin = dimens::kNormalMargin * density_;
    const double iconSize = dimens::kMaterialIconSize * density_;
    // The text column: the card less its padding, the icon and the gap after it.
    const double textWidth = width_ - margin * 2.0 - kRowHPadding * 2.0 * density_ -
                             iconSize - kLeadingGap * density_;

    TextStyleSpec titleStyle;
    titleStyle.sizeVp = kRowTitleSize;
    titleStyle.bold = true;  // titleMedium is weight 500
    TextStyleSpec summaryStyle;
    summaryStyle.sizeVp = kRowSummarySize;

    rows_.clear();
    double y = 0.0;
    const int count = static_cast<int>(sizeof(kEntries) / sizeof(kEntries[0]));
    for (int i = 0; i < count; ++i) {
        const Entry& e = kEntries[i];
        Row row{e.title, e.summary, e.icon, e.isFirst, e.isLast, e.endsGroup, 0.0, 0.0};
        /*
         * The row's height comes from the Material LINE TOKENS, not from the painter's own line
         * boxes: titleMedium is 16 sp on a 24 sp line and bodyMedium 14 sp on a 20 sp line, and
         * the reference's title and summary nodes measure exactly 78 and 65 px. Measuring the
         * painter instead gave a shorter box, and with only 8 dp of padding every settings row
         * came out 183 px against the reference's 293. The line COUNT still comes from the
         * painter, since it is what decides where this font wraps.
         */
        const double summaryH = MeasureTextHeight(e.summary, textWidth, summaryStyle);
        const double oneSummaryLine = MeasureTextHeight("X", textWidth, summaryStyle);
        const int summaryLines = oneSummaryLine > 0.0
            ? std::max(1, static_cast<int>(summaryH / oneSummaryLine + 0.5))
            : 1;
        const double titleH = dimens::kTitleMediumLineHeight * density_;
        const double content = titleH + dimens::kSmallMargin * density_ +
                               static_cast<double>(summaryLines) *
                                   dimens::kBodyMediumLineHeight * density_;
        // A row with NO summary is a one-line ListItem: 56 dp, plus Preference.kt:148's 8 dp a
        // side. Derived from the source, not measured — this screen has no summary-less row.
        row.height = std::max(content + dimens::kPreferenceRowVPadding * 2.0 * density_,
                              (dimens::kListItemOneLineHeight + dimens::kSmallMargin * 2.0) *
                                  density_);
        row.top = y;
        rows_.push_back(row);

        y += row.height;
        if (i + 1 < count) {
            y += (e.endsGroup ? dimens::kSettingsLargeSeparator : dimens::kSettingsSmallSeparator) *
                 density_;
        }
    }
}

double SettingsScreen::ContentHeight() const {
    if (rows_.empty()) {
        return 0.0;
    }
    return rows_.back().top + rows_.back().height + dimens::kLargeMargin * density_;
}

double SettingsScreen::MaxScroll() const {
    const double viewport = height_ - ListTop() - bottomInset_;
    return std::max(0.0, ContentHeight() - viewport);
}

bool SettingsScreen::ScrollBy(double dy) {
    const double before = scroll_;
    scroll_ = std::max(0.0, std::min(MaxScroll(), scroll_ - dy));
    return scroll_ != before;
}

void SettingsScreen::Draw(OH_Drawing_Canvas* canvas) {
    // Compose typography: FontFamily.Default. See SystemFontScope.
    SystemFontScope systemFont;
    if (width_ <= 0.0 || height_ <= 0.0) {
        return;
    }
    Layout();

    const double margin = dimens::kNormalMargin * density_;
    const double iconSize = dimens::kMaterialIconSize * density_;
    const uint32_t onSurface = kOnSurface.Resolve(dark_);

    // Material3Scaffold's containerColor is `surface`; the CARDS carry the elevation tint.
    FillRect(canvas, 0.0, 0.0, width_, height_, kSurface.Resolve(dark_));

    /* ---- LargeTopAppBar: the icon row, then the headline below it ---- */
    const double barTop = topInset_;
    DrawAsset(canvas, "ic_back.svg", margin, barTop + (kBarRowHeight * density_ - iconSize) / 2.0,
              iconSize, onSurface, true);
    DrawAsset(canvas, "ic_info.svg", width_ - margin - iconSize,
              barTop + (kBarRowHeight * density_ - iconSize) / 2.0, iconSize, onSurface, true);

    TextStyleSpec headline;
    headline.sizeVp = kHeadlineTextSize;
    headline.color = onSurface;
    headline.singleLine = true;
    const double headlineHeight = MeasureTextHeight("Settings", width_ - margin * 2.0, headline);
    // Sits on the baseline of the expanded bar, above its bottom edge.
    DrawText(canvas, "Settings", margin,
             barTop + LargeTitleTop(headlineHeight, density_),
             width_ - margin * 2.0, headline);

    /* ---- the grouped rows ---- */
    OH_Drawing_CanvasSave(canvas);
    OH_Drawing_Rect* clip = OH_Drawing_RectCreate(
        0.0f, static_cast<float>(ListTop()), static_cast<float>(width_),
        static_cast<float>(height_));
    OH_Drawing_CanvasClipRect(canvas, clip, INTERSECT, true);
    OH_Drawing_RectDestroy(clip);

    TextStyleSpec titleStyle;
    titleStyle.sizeVp = kRowTitleSize;
    titleStyle.bold = true;
    titleStyle.color = onSurface;
    TextStyleSpec summaryStyle;
    summaryStyle.sizeVp = kRowSummarySize;
    summaryStyle.color = kOnSurfaceVariant.Resolve(dark_);

    const double outer = dimens::kSettingsCardCornerRadius * density_;
    const double inner = dimens::kSettingsCardInnerCornerRadius * density_;
    const double textLeft = margin + kRowHPadding * density_ + iconSize + kLeadingGap * density_;
    const double textWidth = width_ - margin - kRowHPadding * density_ - textLeft;

    for (const Row& row : rows_) {
        const double top = ListTop() + row.top - scroll_;
        const double bottom = top + row.height;
        if (bottom < ListTop() || top > height_) {
            continue;
        }
        // One rounded block per GROUP: full radius on the group's outer corners, 2 dp between.
        FillRoundRectCorners(canvas, margin, top, width_ - margin, bottom,
                             row.isFirst ? outer : inner, row.isFirst ? outer : inner,
                             row.isLast ? outer : inner, row.isLast ? outer : inner,
                             kSettingsCard.Resolve(dark_));

        DrawAsset(canvas, row.icon, margin + kRowHPadding * density_,
                  top + (row.height - iconSize) / 2.0, iconSize, onSurface, true);

        // The same line tokens the measure used, so the two cannot drift apart. The painter's own
        // line box is a little shorter than the token, so each line is drawn a hair high inside
        // its slot; the ROW's height, which is what the layout depends on, is exact.
        const double titleH = dimens::kTitleMediumLineHeight * density_;
        const double contentTop = top + dimens::kPreferenceRowVPadding * density_;
        DrawText(canvas, row.title, textLeft, contentTop, textWidth, titleStyle);
        DrawText(canvas, row.summary, textLeft,
                 contentTop + titleH + dimens::kSmallMargin * density_, textWidth, summaryStyle);
    }

    OH_Drawing_CanvasRestore(canvas);
}

SettingsHit SettingsScreen::HitTest(double x, double y) const {
    SettingsHit hit;
    const double margin = dimens::kNormalMargin * density_;
    const double iconSize = dimens::kMaterialIconSize * density_;

    // The bar's icons are on their own row and do not scroll.
    if (y >= topInset_ && y < topInset_ + kBarRowHeight * density_) {
        if (x < margin + iconSize) {
            hit.action = SettingsAction::Close;
        } else if (x > width_ - margin - iconSize) {
            hit.action = SettingsAction::Navigate;
            hit.destination = "About";  // strings.xml `action_about`
        }
        return hit;
    }

    for (const Row& row : rows_) {
        const double top = ListTop() + row.top - scroll_;
        if (y >= top && y < top + row.height) {
            hit.action = SettingsAction::Navigate;
            hit.destination = row.title;
            return hit;
        }
    }
    return hit;
}

}  // namespace breezy
