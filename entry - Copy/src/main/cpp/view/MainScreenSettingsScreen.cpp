#include "MainScreenSettingsScreen.h"

#include "../data/DisplayCatalogue.h"

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
constexpr double kSectionTextSize = 14.0;

/* The choice dialog (ListPreference.kt:241-313). */
constexpr double kDialogCorner = 28.0;
constexpr double kDialogPadding = 24.0;
constexpr double kDialogTitleSize = 24.0;
constexpr double kDialogOptionSize = 16.0;
constexpr double kDialogButtonSize = 14.0;
constexpr double kDialogOptionHeight = 56.0;
constexpr double kRadioOuter = 20.0;
constexpr double kRadioInner = 10.0;

/** strings.xml `settings_enabled` / `settings_disabled`. */
const char* kEnabled = "Enabled";
const char* kDisabled = "Disabled";

}  // namespace

void MainScreenSettingsScreen::SetGeometry(double widthPx, double heightPx, float density,
                                           double topInsetPx, double bottomInsetPx) {
    width_ = widthPx;
    height_ = heightPx;
    density_ = density;
    topInset_ = topInsetPx;
    bottomInset_ = bottomInsetPx;
    Layout();
}

void MainScreenSettingsScreen::SetValues(bool background, bool trendLines,
                                         const std::string& animationMode, bool gravity,
                                         bool fadeIn, bool elements) {
    background_ = background;
    trendLines_ = trendLines;
    animationMode_ = animationMode;
    gravity_ = gravity;
    fadeIn_ = fadeIn;
    elements_ = elements;
    Layout();
}

void MainScreenSettingsScreen::SetDisplayLists(const std::string& cards, const std::string& daily,
                                               const std::string& hourly) {
    cardDisplay_ = cards;
    dailyDisplay_ = daily;
    hourlyDisplay_ = hourly;
    Layout();
}

double MainScreenSettingsScreen::ListTop() const {
    return topInset_ + kBarExpandedHeight * density_;
}

std::vector<MainScreenSettingsScreen::Option> MainScreenSettingsScreen::AnimationOptions() const {
    // background_animation_values / background_animation (arrays.xml).
    return {{"system", "Follow system"}, {"enabled", kEnabled}, {"disabled", kDisabled}};
}

std::string MainScreenSettingsScreen::AnimationName() const {
    for (const Option& option : AnimationOptions()) {
        if (option.value == animationMode_) {
            return option.name;
        }
    }
    return AnimationOptions()[0].name;
}

void MainScreenSettingsScreen::Layout() {
    // Measured in the SAME face Draw paints in, or the cached heights would be Roboto's.
    SystemFontScope systemFont;
    if (width_ <= 0.0 || density_ <= 0.0f) {
        return;
    }
    std::string values = std::string(background_ ? "1" : "0") + (trendLines_ ? "1" : "0") +
                         animationMode_ + (gravity_ ? "1" : "0") + (fadeIn_ ? "1" : "0") +
                         (elements_ ? "1" : "0") + "\x1f" + cardDisplay_ + "\x1f" + dailyDisplay_ +
                         "\x1f" + hourlyDisplay_;
    if (laidOutWidth_ == width_ && laidOutDensity_ == density_ && laidOutValues_ == values &&
        !items_.empty()) {
        return;
    }
    laidOutWidth_ = width_;
    laidOutDensity_ = density_;
    laidOutValues_ = values;

    const char* on = kEnabled;
    const char* off = kDisabled;

    items_.clear();
    /* ---- Displayed data (kt:81-122) ---- */
    /*
     * getSummary (CardDisplay.kt:78-82) — the STORED list's names, joined by ", ". These used to be
     * constants, because the manage screens were not ported and the lists could not differ from
     * their defaults; they are ported now, so the summaries read what is stored.
     */
    items_.push_back({true, "Displayed data", "", false, false, false, false, false, false, ""});
    items_.push_back({false, "Blocks",
                      DisplayListSummary(ParseDisplayList(DisplayList::Blocks, cardDisplay_)), true,
                      false, false, false, false, false, ""});
    items_.push_back({false, "Daily trends",
                      DisplayListSummary(ParseDisplayList(DisplayList::Daily, dailyDisplay_)),
                      false, false, false, false, false, false, ""});
    items_.push_back({false, "Hourly trends",
                      DisplayListSummary(ParseDisplayList(DisplayList::Hourly, hourlyDisplay_)),
                      false, true, true, false, false, false, ""});

    /* ---- Options (kt:126-160) ---- */
    items_.push_back({true, "Options", "", false, false, false, false, false, false, ""});
    items_.push_back({false, "Weather-based background", background_ ? on : off, true, false, false,
                      true, background_, false, "main_screen_background"});
    items_.push_back({false, "Threshold lines on charts", trendLines_ ? on : off, false, true, true,
                      true, trendLines_, false, "trend_horizontal_line_switch"});

    /* ---- Animations (kt:164-254) ---- */
    items_.push_back({true, "Animations", "", false, false, false, false, false, false, ""});
    items_.push_back({false, "Background animation", AnimationName(), true, false, false, false,
                      false, true, "background_animation_mode"});
    items_.push_back({false, "Gravity sensor", gravity_ ? on : off, false, false, false, true,
                      gravity_, false, "gravity_sensor_switch"});
    /*
     * kt:208-228 — the last two switches are DISABLED when the system has animations off, and then
     * read "Unavailable, enable animations in Android settings first". HarmonyOS exposes no
     * equivalent of `context.isMotionReduced`, so they are always live here. Flagged rather than
     * faked, exactly as in ArkUIApp.
     */
    items_.push_back({false, "Blocks fade in", fadeIn_ ? on : off, false, false, false, true,
                      fadeIn_, false, "list_animation_switch"});
    items_.push_back({false, "Other element animations", elements_ ? on : off, false, true, false,
                      true, elements_, false, "item_animation_switch"});

    const double margin = dimens::kNormalMargin * density_;
    // The switch takes room out of the text column, or a long summary would run under it.
    const double switchRoom = (kSwitchWidth + kRowHPadding) * density_;
    const double textWidth = width_ - margin * 2.0 - kRowHPadding * 2.0 * density_;

    TextStyleSpec titleStyle;
    titleStyle.sizeVp = kRowTitleSize;
    titleStyle.bold = true;
    TextStyleSpec summaryStyle;
    summaryStyle.sizeVp = kRowSummarySize;
    TextStyleSpec sectionStyle;
    sectionStyle.sizeVp = kSectionTextSize;
    sectionStyle.bold = true;

    double y = 0.0;
    for (size_t i = 0; i < items_.size(); ++i) {
        Item& item = items_[i];
        item.top = y;
        if (item.header) {
            item.height = SectionHeaderHeight(item.title, textWidth, sectionStyle, density_);
            y += item.height;
            continue;
        }
        const double available = item.isSwitch ? textWidth - switchRoom : textWidth;
        item.height = PreferenceRowHeight(item.title, item.summary, available, titleStyle,
                                          summaryStyle, density_, item.isSwitch);
        // A switch row must still be tall enough for the switch itself.
        y += item.height;
        /*
         * The separator follows a row even when a section HEADER comes next: the Kotlin emits
         * sectionFooterItem (a transparent divider, so invisible) and then largeSeparatorItem
         * before the next sectionHeaderItem (kt:122-126).
         */
        if (i + 1 < items_.size()) {
            y += (item.endsGroup ? dimens::kSettingsLargeSeparator
                                 : dimens::kSettingsSmallSeparator) * density_;
        }
    }
}

double MainScreenSettingsScreen::MaxScroll() const {
    if (items_.empty()) {
        return 0.0;
    }
    const double content =
        items_.back().top + items_.back().height + dimens::kLargeMargin * density_;
    return std::max(0.0, content - (height_ - ListTop() - bottomInset_));
}

bool MainScreenSettingsScreen::ScrollBy(double dy) {
    const double before = scroll_;
    scroll_ = std::max(0.0, std::min(MaxScroll(), scroll_ - dy));
    return scroll_ != before;
}
/*
 * The Material switch is render/Shapes.h's now — one copy, shared with the other three settings
 * screens and the weather-models dialog that was about to become a fifth.
 */
void MainScreenSettingsScreen::Draw(OH_Drawing_Canvas* canvas) {
    // Compose typography: FontFamily.Default. See SystemFontScope.
    SystemFontScope systemFont;
    if (width_ <= 0.0 || height_ <= 0.0) {
        return;
    }
    Layout();

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
    const double headlineHeight = MeasureTextHeight("Main screen", width_ - margin * 2.0, headline);
    DrawText(canvas, "Main screen", margin,
             topInset_ + LargeTitleTop(headlineHeight, density_),
             width_ - margin * 2.0, headline);

    /* ---- the rows ---- */
    OH_Drawing_CanvasSave(canvas);
    OH_Drawing_Rect* clip = OH_Drawing_RectCreate(
        0.0f, static_cast<float>(ListTop()), static_cast<float>(width_),
        static_cast<float>(height_));
    OH_Drawing_CanvasClipRect(canvas, clip, INTERSECT, true);
    OH_Drawing_RectDestroy(clip);

    const double outer = dimens::kSettingsCardCornerRadius * density_;
    const double inner = dimens::kSettingsCardInnerCornerRadius * density_;
    const double textLeft = margin + kRowHPadding * density_;
    const double fullTextWidth = width_ - margin - kRowHPadding * density_ - textLeft;
    const double switchRoom = (kSwitchWidth + kRowHPadding) * density_;

    for (const Item& item : items_) {
        const double top = ListTop() + item.top - scroll_;
        if (top + item.height < ListTop() || top > height_) {
            continue;
        }
        if (item.header) {
            TextStyleSpec sectionStyle;
            sectionStyle.sizeVp = kSectionTextSize;
            sectionStyle.bold = true;
            sectionStyle.color = kPrimary.Resolve(dark_);
            DrawTextFixed(canvas, item.title, margin + margin, top + margin, fullTextWidth,
                          sectionStyle, dimens::kBodyMediumLineHeight * density_);
            continue;
        }

        FillRoundRectCorners(canvas, margin, top, width_ - margin, top + item.height,
                             item.isFirst ? outer : inner, item.isFirst ? outer : inner,
                             item.isLast ? outer : inner, item.isLast ? outer : inner,
                             kSettingsCard.Resolve(dark_));

        TextStyleSpec titleStyle;
        titleStyle.sizeVp = kRowTitleSize;
        titleStyle.bold = true;
        titleStyle.color = onSurface;
        TextStyleSpec summaryStyle;
        summaryStyle.sizeVp = kRowSummarySize;
        summaryStyle.color = kOnSurfaceVariant.Resolve(dark_);

        const double textWidth = item.isSwitch ? fullTextWidth - switchRoom : fullTextWidth;
        DrawPreferenceRowText(canvas, item.title, item.summary, textLeft, top, item.height, textWidth,
                              titleStyle, summaryStyle, density_, item.isSwitch);

        if (item.isSwitch) {
            // Every row on this screen is enabled, so the shared helper's `enabled` is fixed true.
            DrawSwitch(canvas, width_ - margin - kRowHPadding * density_,
                       top + item.height / 2.0, item.checked, true, density_,
                       kPrimary.Resolve(dark_), kOnSurfaceVariant.Resolve(dark_),
                       kSettingsCard.Resolve(dark_));
        }
    }
    OH_Drawing_CanvasRestore(canvas);

    DrawDialog(canvas);
}

void MainScreenSettingsScreen::DrawDialog(OH_Drawing_Canvas* canvas) {
    if (!dialogOpen_) {
        dialogRight_ = dialogLeft_ = 0.0;
        return;
    }
    FillRect(canvas, 0.0, 0.0, width_, height_, 0x52000000);

    const std::vector<Option> options = AnimationOptions();
    const double padding = kDialogPadding * density_;
    const double optionHeight = kDialogOptionHeight * density_;

    TextStyleSpec titleStyle;
    titleStyle.sizeVp = kDialogTitleSize;
    titleStyle.color = kOnSurface.Resolve(dark_);
    titleStyle.singleLine = true;

    const double cardWidth = width_ * 0.8;
    const double left = (width_ - cardWidth) / 2.0;
    const double right = left + cardWidth;
    const double innerWidth = cardWidth - padding * 2.0;

    const std::string title = "Background animation";
    const double titleHeight = MeasureTextHeight(title, innerWidth, titleStyle);
    const double buttonHeight = kDialogOptionHeight * density_ * 0.7;
    const double cardHeight = padding * 2.0 + titleHeight + dimens::kNormalMargin * density_ +
                              optionHeight * static_cast<double>(options.size()) + buttonHeight;
    const double top = (height_ - cardHeight) / 2.0;

    FillRoundRect(canvas, left, top, right, top + cardHeight, kDialogCorner * density_,
                  kSettingsCard.Resolve(dark_));
    DrawText(canvas, title, left + padding, top + padding, innerWidth, titleStyle);

    const double optionsTop = top + padding + titleHeight + dimens::kNormalMargin * density_;
    for (size_t i = 0; i < options.size(); ++i) {
        const double rowTop = optionsTop + optionHeight * static_cast<double>(i);
        const double centreY = rowTop + optionHeight / 2.0;
        const double radioX = left + padding + kRadioOuter * density_ / 2.0;
        const bool chosen = options[i].value == animationMode_;
        FillCircle(canvas, radioX, centreY, kRadioOuter * density_ / 2.0,
                   chosen ? kPrimary.Resolve(dark_) : kOnSurfaceVariant.Resolve(dark_));
        FillCircle(canvas, radioX, centreY, kRadioOuter * density_ / 2.0 - 2.0 * density_,
                   kSettingsCard.Resolve(dark_));
        if (chosen) {
            FillCircle(canvas, radioX, centreY, kRadioInner * density_ / 2.0,
                       kPrimary.Resolve(dark_));
        }

        TextStyleSpec optionStyle;
        optionStyle.sizeVp = kDialogOptionSize;
        optionStyle.color = kOnSurface.Resolve(dark_);
        const double labelLeft =
            radioX + kRadioOuter * density_ / 2.0 + dimens::kNormalMargin * density_;
        const double labelHeight =
            MeasureTextHeight(options[i].name, right - padding - labelLeft, optionStyle);
        DrawText(canvas, options[i].name, labelLeft, centreY - labelHeight / 2.0,
                 right - padding - labelLeft, optionStyle);
    }

    // UPPERCASE, as the reference device renders android.R.string.cancel — see UnitsScreen.
    TextStyleSpec buttonStyle;
    buttonStyle.sizeVp = kDialogButtonSize;
    buttonStyle.color = kPrimary.Resolve(dark_);
    buttonStyle.bold = true;
    buttonStyle.alignment = TextAlignment::Right;
    const double cancelTop = optionsTop + optionHeight * static_cast<double>(options.size());
    DrawText(canvas, "CANCEL", left + padding, cancelTop, innerWidth, buttonStyle);

    dialogLeft_ = left;
    dialogRight_ = right;
    dialogOptionTop_ = optionsTop;
    dialogOptionHeight_ = optionHeight;
    dialogCancelTop_ = cancelTop;
    dialogCancelBottom_ = cancelTop + buttonHeight;
}

MainSettingsHit MainScreenSettingsScreen::HitDialog(double x, double y) {
    MainSettingsHit hit;
    const std::vector<Option> options = AnimationOptions();
    if (y >= dialogOptionTop_ && y < dialogCancelTop_ && dialogOptionHeight_ > 0.0) {
        const int index = static_cast<int>((y - dialogOptionTop_) / dialogOptionHeight_);
        if (index >= 0 && index < static_cast<int>(options.size()) && x >= dialogLeft_ &&
            x <= dialogRight_) {
            hit.action = MainSettingsAction::Changed;
            hit.key = "background_animation_mode";
            hit.value = options[static_cast<size_t>(index)].value;
            dialogOpen_ = false;
            return hit;
        }
    }
    dialogOpen_ = false;
    return hit;
}

MainSettingsHit MainScreenSettingsScreen::HitTest(double x, double y) {
    if (dialogOpen_) {
        return HitDialog(x, y);
    }

    MainSettingsHit hit;
    const double margin = dimens::kNormalMargin * density_;
    const double iconSize = dimens::kMaterialIconSize * density_;

    if (y >= topInset_ && y < topInset_ + kBarRowHeight * density_) {
        if (x < margin + iconSize) {
            hit.action = MainSettingsAction::Close;
        } else if (x > width_ - margin - iconSize) {
            hit.action = MainSettingsAction::Navigate;
            hit.destination = "About";
        }
        return hit;
    }

    for (const Item& item : items_) {
        if (item.header) {
            continue;
        }
        const double top = ListTop() + item.top - scroll_;
        if (y >= top && y < top + item.height) {
            if (item.opensDialog) {
                dialogOpen_ = true;
                return hit;
            }
            if (item.isSwitch) {
                // kt:116-120 — the whole row flips it, not just the switch.
                hit.action = MainSettingsAction::Changed;
                hit.key = item.key;
                hit.value = item.checked ? "false" : "true";
                return hit;
            }
            hit.action = MainSettingsAction::Navigate;
            hit.destination = item.title;
            return hit;
        }
    }
    return hit;
}

}  // namespace breezy
