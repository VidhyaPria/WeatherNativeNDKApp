#include "AppearanceScreen.h"

#include <algorithm>

#include "../data/CalendarCatalogue.h"
#include "../render/AssetImages.h"
#include "../render/Shapes.h"
#include "../render/TextPainter.h"
#include "../render/Theme.h"

namespace breezy {
namespace {

/* LargeTopAppBar, as the root settings screen uses. Material library values, not app resources. */
constexpr double kBarRowHeight = 64.0;
constexpr double kBarExpandedHeight = 152.0;
constexpr double kHeadlineTextSize = 28.0;

constexpr double kRowHPadding = 16.0;
constexpr double kRowTitleSize = 16.0;    // typography.titleMedium
constexpr double kRowSummarySize = 14.0;  // typography.bodyMedium
/** SectionHeader: bodyMedium SemiBold in `primary`, padded normal_margin (Section.kt:31-46). */
constexpr double kSectionTextSize = 14.0;

/* The choice dialog (ListPreference.kt:241-313). */
/**
 * The container width, measured on the reference: the alternate-calendar picker (a tall list) and
 * the animatable-icon dialog (one square icon) both come out 1039 px on the 1260 px screen, i.e.
 * 319.7 dp -> 320. It used to be `width_ * 0.8` = 1008 px, 31 px narrow.
 */
constexpr double kDialogWidthVp = 320.0;
constexpr double kDialogCorner = 28.0;
constexpr double kDialogPadding = 24.0;
constexpr double kDialogTitleSize = 24.0;   // typography.headlineSmall
constexpr double kDialogOptionSize = 16.0;  // typography.bodyLarge
constexpr double kDialogButtonSize = 14.0;  // typography.labelLarge
constexpr double kDialogOptionHeight = 56.0;
/**
 * A radio row's MINIMUM height — it grows for a label that wraps. NOT the 56 dp Material3 ListItem
 * token: a single-choice row inside an AlertDialog is a radio plus a label, and its height is the
 * 48 dp minimum touch target. See the measurement note at the draw site.
 */
constexpr double kDialogOptionMinHeightVp = 48.0;
/** bodyLarge — 16 sp on a 24 sp LINE. Without it each line was 61 px against the reference's 78. */
constexpr double kDialogOptionLineHeightVp = 24.0;
/** headlineSmall — 24 sp on a 32 sp line. */
constexpr double kDialogTitleLineHeightVp = 32.0;
/** The button and the gap above it, both measured — see the container arithmetic at the draw site. */
constexpr double kDialogButtonHeightVp = 40.0;
constexpr double kDialogButtonGapVp = 32.0;
/** labelLarge — 14 sp on a 20 sp line. */
constexpr double kDialogButtonLineHeightVp = 20.0;
/** kt:316-317 — the provider's own launcher icon in the icon-packs dialog, 42 dp. */
constexpr double kProviderIconSize = 42.0;
constexpr double kRadioOuter = 20.0;
constexpr double kRadioInner = 10.0;

}  // namespace

void AppearanceScreen::SetGeometry(double widthPx, double heightPx, float density,
                                   double topInsetPx, double bottomInsetPx) {
    width_ = widthPx;
    height_ = heightPx;
    density_ = density;
    topInset_ = topInsetPx;
    bottomInset_ = bottomInsetPx;
    Layout();
}

void AppearanceScreen::SetValues(const std::string& darkMode, bool dayNightForLocations) {
    darkMode_ = darkMode;
    dayNightForLocations_ = dayNightForLocations;
    Layout();
}

double AppearanceScreen::ListTop() const {
    return topInset_ + kBarExpandedHeight * density_;
}

std::vector<AppearanceScreen::Option> AppearanceScreen::OptionsFor(Dialog dialog) const {
    if (dialog == Dialog::DarkMode) {
        /*
         * dark_mode_values / dark_modes. The first entry names the SYSTEM's current mode in
         * parentheses (kt:144-176):
         *
         *   MODE_NIGHT_NO     -> "Follow system (Light)"
         *   MODE_NIGHT_YES    -> "Follow system (Dark)"
         *   MODE_NIGHT_AUTO   -> "(Follow day/night)"      no HarmonyOS equivalent
         *   MODE_NIGHT_CUSTOM -> "(<start> - <end>)"       no HarmonyOS equivalent
         *   else              -> "Follow system (Light)"
         *
         * The two branches that HAVE an equivalent are the two that occur on a modern device, and
         * the system dark flag is exactly `uiModeManager.nightMode` for them. AUTO and CUSTOM fall
         * to the Kotlin's own `else`, which is the Light label — the same answer this gives.
         */
        return {{"system", systemDark_ ? "Follow system (Dark)" : "Follow system (Light)"},
                {"light", "Light"},
                {"dark", "Dark"}};
    }
    if (dialog == Dialog::DarkModeLocations) {
        return {{"day_night", "Follow day/night"},
                {"dark", "Always dark when app is in dark mode"}};
    }
    if (dialog == Dialog::AlternateCalendar) {
        std::vector<Option> options;
        for (const CalendarOption& option : CalendarOptions(language_, country_)) {
            options.push_back({option.id, option.name});
        }
        return options;
    }
    // English-only per CLAUDE.md, so the list is honest about having one entry.
    return {{"en", "English"}};
}

std::string AppearanceScreen::SelectedFor(Dialog dialog) const {
    if (dialog == Dialog::DarkMode) {
        return darkMode_;
    }
    if (dialog == Dialog::DarkModeLocations) {
        // kt:203-206 — while the app mode is "light" this always reads day/night, whatever is
        // stored, because the location rule is unconditional in that branch of isLightTheme.
        if (darkMode_ == "light") {
            return "day_night";
        }
        return dayNightForLocations_ ? "day_night" : "dark";
    }
    if (dialog == Dialog::AlternateCalendar) {
        return alternateCalendar_;
    }
    return "en";
}

void AppearanceScreen::Layout() {
    // Measured in the SAME face Draw paints in, or the cached heights would be Roboto's.
    SystemFontScope systemFont;
    if (width_ <= 0.0 || density_ <= 0.0f) {
        return;
    }
    if (laidOutWidth_ == width_ && laidOutDensity_ == density_ &&
        laidOutDarkMode_ == darkMode_ && laidOutDayNight_ == dayNightForLocations_ &&
        laidOutSystemDark_ == systemDark_ && !items_.empty()) {
        return;
    }
    laidOutWidth_ = width_;
    laidOutDensity_ = density_;
    laidOutDarkMode_ = darkMode_;
    laidOutDayNight_ = dayNightForLocations_;
    // The "Follow system (…)" summary names it, so a system flip has to re-lay out.
    laidOutSystemDark_ = systemDark_;

    const std::vector<Option> modes = OptionsFor(Dialog::DarkMode);
    std::string darkModeName = modes[0].name;
    for (const Option& option : modes) {
        if (option.value == darkMode_) {
            darkModeName = option.name;
        }
    }
    const std::vector<Option> locationModes = OptionsFor(Dialog::DarkModeLocations);
    const std::string locationKey = SelectedFor(Dialog::DarkModeLocations);
    const std::string locationName =
        locationKey == locationModes[0].value ? locationModes[0].name : locationModes[1].name;

    items_.clear();
    items_.push_back({true, "Language and region", "", false, false, false, true, Dialog::None});
    items_.push_back({false, "Language", "English", true, false, false, true, Dialog::Language});
    items_.push_back({false, "Units", "Temperature, precipitation, distance, speed, pressure",
                      false, false, false, true, Dialog::None});
    items_.push_back({false, "Alternate calendar",
                      CalendarSummary(alternateCalendar_, language_, country_), false, true, true,
                      true, Dialog::AlternateCalendar});
    items_.push_back({true, "Theme", "", false, false, false, true, Dialog::None});
    items_.push_back({false, "App dark mode", darkModeName, true, false, false, true,
                      Dialog::DarkMode});
    // kt:211 — disabled while the app mode is "light".
    items_.push_back({false, "Location-based dark mode", locationName, false, false, false,
                      darkMode_ != "light", Dialog::DarkModeLocations});
    // kt:238 — the row opens the "Icon packs" dialog, it does not navigate.
    items_.push_back({false, "Icon pack", "Default", false, true, false, true, Dialog::IconPack});

    const double margin = dimens::kNormalMargin * density_;
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
            // A Box padded normal_margin on every side.
            item.height = SectionHeaderHeight(item.title, textWidth, sectionStyle, density_);
            y += item.height;
            continue;
        }
        item.height = PreferenceRowHeight(item.title, item.summary, textWidth, titleStyle,
                                          summaryStyle, density_);
        y += item.height;
        /*
         * The separator follows a row even when a section HEADER comes next: the Kotlin emits
         * sectionFooterItem (a transparent divider, so invisible) and then largeSeparatorItem
         * before the next sectionHeaderItem (kt:137-141). Skipping it here closed the gap between
         * the two sections, which was visible against ArkUIApp side by side.
         */
        if (i + 1 < items_.size()) {
            y += (item.endsGroup ? dimens::kSettingsLargeSeparator
                                 : dimens::kSettingsSmallSeparator) * density_;
        }
    }
}

double AppearanceScreen::MaxScroll() const {
    if (items_.empty()) {
        return 0.0;
    }
    const double content =
        items_.back().top + items_.back().height + dimens::kLargeMargin * density_;
    return std::max(0.0, content - (height_ - ListTop() - bottomInset_));
}

bool AppearanceScreen::ScrollBy(double dy) {
    const double before = scroll_;
    scroll_ = std::max(0.0, std::min(MaxScroll(), scroll_ - dy));
    return scroll_ != before;
}

void AppearanceScreen::Draw(OH_Drawing_Canvas* canvas) {
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
    const double headlineHeight = MeasureTextHeight("Appearance", width_ - margin * 2.0, headline);
    DrawText(canvas, "Appearance", margin,
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
    const double textWidth = width_ - margin - kRowHPadding * density_ - textLeft;

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
            DrawTextFixed(canvas, item.title, margin + margin, top + margin, textWidth,
                          sectionStyle, dimens::kBodyMediumLineHeight * density_);
            continue;
        }

        /*
         * A disabled row is drawn at half alpha, not hidden (Preference.kt:159). The alpha is
         * folded into each colour rather than applied as a layer, which would cost an offscreen.
         */
        const double alpha = item.enabled ? 1.0 : 0.5;
        FillRoundRectCorners(canvas, margin, top, width_ - margin, top + item.height,
                             item.isFirst ? outer : inner, item.isFirst ? outer : inner,
                             item.isLast ? outer : inner, item.isLast ? outer : inner,
                             kSettingsCard.Resolve(dark_));

        TextStyleSpec titleStyle;
        titleStyle.sizeVp = kRowTitleSize;
        titleStyle.bold = true;
        titleStyle.color = WithAlpha(onSurface, alpha);
        TextStyleSpec summaryStyle;
        summaryStyle.sizeVp = kRowSummarySize;
        summaryStyle.color = WithAlpha(kOnSurfaceVariant.Resolve(dark_), alpha);

        DrawPreferenceRowText(canvas, item.title, item.summary, textLeft, top, item.height, textWidth, titleStyle,
                              summaryStyle, density_);
    }
    OH_Drawing_CanvasRestore(canvas);

    DrawDialog(canvas);
}

/**
 * The "Icon packs" dialog — AppearanceSettingsScreen.kt:246-344.
 *
 * `ResourcesProviderFactory.getProviderList` returns the default provider plus every installed icon
 * pack. CLAUDE.md scopes external packs out, so the list is exactly what Android shows on a device
 * with none installed: one row, the default provider, its 42 dp launcher icon, and the magnifier
 * that opens the preview (kt:322-338). The "Get more" button is gated on
 * `BuildConfig.ICON_PACKS_LINK` (kt:254) and is absent here for the same reason the packs are.
 */
void AppearanceScreen::DrawIconPackDialog(OH_Drawing_Canvas* canvas) {
    FillRect(canvas, 0.0, 0.0, width_, height_, 0x52000000);

    const double padding = kDialogPadding * density_;
    const double margin = dimens::kNormalMargin * density_;
    const double iconSize = dimens::kMaterialIconSize * density_;
    const double providerIcon = kProviderIconSize * density_;
    const double rowHeight = dimens::kListItemOneLineHeight * density_;

    TextStyleSpec titleStyle;
    titleStyle.sizeVp = kDialogTitleSize;
    titleStyle.color = kOnSurface.Resolve(dark_);
    titleStyle.singleLine = true;

    // 320 dp, measured on the reference — it is a THEME value, not 80 % of whatever the screen is.
    // See Dimens.DIALOG_WIDTH in ArkUIApp, which carries the same number and the measurement.
    const double cardWidth = kDialogWidthVp * density_;
    const double left = (width_ - cardWidth) / 2.0;
    const double right = left + cardWidth;
    const double innerWidth = cardWidth - padding * 2.0;

    const std::string title = "Icon packs";  // strings.xml `settings_icon_packs_title`
    const double titleHeight = MeasureTextHeight(title, innerWidth, titleStyle);
    const double buttonHeight = kDialogOptionHeight * density_ * 0.7;
    const double cardHeight = padding * 2.0 + titleHeight + margin + rowHeight + buttonHeight;
    const double top = (height_ - cardHeight) / 2.0;

    FillRoundRect(canvas, left, top, right, top + cardHeight, kDialogCorner * density_,
                  kSettingsCard.Resolve(dark_));
    DrawText(canvas, title, left + padding, top + padding, innerWidth, titleStyle);

    const double rowTop = top + padding + titleHeight + margin;
    DrawAsset(canvas, "app_icon.png", left + margin, rowTop + (rowHeight - providerIcon) / 2.0,
              providerIcon, 0, false);

    TextStyleSpec nameStyle;
    nameStyle.sizeVp = kRowTitleSize;
    nameStyle.color = kOnSurface.Resolve(dark_);
    nameStyle.singleLine = true;
    const double nameLeft = left + margin + providerIcon + margin;
    const double nameWidth = right - margin - iconSize - margin - nameLeft;
    const std::string name = "Breezy Weather";  // res_breezy/values/strings.xml `brand_name`
    const double nameHeight = MeasureTextHeight(name, nameWidth, nameStyle);
    DrawText(canvas, name, nameLeft, rowTop + (rowHeight - nameHeight) / 2.0, nameWidth, nameStyle);

    const double searchLeft = right - margin - iconSize;
    DrawAsset(canvas, "ic_search.svg", searchLeft, rowTop + (rowHeight - iconSize) / 2.0, iconSize,
              kOnSurface.Resolve(dark_), true);

    TextStyleSpec buttonStyle;
    buttonStyle.sizeVp = kDialogButtonSize;
    buttonStyle.color = kPrimary.Resolve(dark_);
    buttonStyle.bold = true;
    buttonStyle.alignment = TextAlignment::Right;
    const double closeTop = rowTop + rowHeight;
    // strings.xml `action_close`, upper-cased by the Material button style as CANCEL is.
    DrawText(canvas, "CLOSE", left + padding, closeTop, innerWidth, buttonStyle);

    dialogLeft_ = left;
    dialogRight_ = right;
    packRowTop_ = rowTop;
    packRowBottom_ = rowTop + rowHeight;
    packSearchLeft_ = searchLeft;
    dialogOptionTop_ = 0.0;
    dialogOptionHeight_ = 0.0;
    dialogOptionHeights_.clear();
    dialogCancelTop_ = closeTop;
    dialogCancelBottom_ = closeTop + buttonHeight;
}

void AppearanceScreen::DrawDialog(OH_Drawing_Canvas* canvas) {
    if (dialog_ == Dialog::None) {
        dialogRight_ = dialogLeft_ = 0.0;
        return;
    }
    if (dialog_ == Dialog::IconPack) {
        DrawIconPackDialog(canvas);
        return;
    }
    // A real dialog dims everything behind it.
    FillRect(canvas, 0.0, 0.0, width_, height_, 0x52000000);

    const std::vector<Option> options = OptionsFor(dialog_);
    const std::string selected = SelectedFor(dialog_);
    const double padding = kDialogPadding * density_;

    TextStyleSpec titleStyle;
    titleStyle.sizeVp = kDialogTitleSize;
    titleStyle.color = kOnSurface.Resolve(dark_);
    titleStyle.singleLine = true;

    // 320 dp, measured on the reference — it is a THEME value, not 80 % of whatever the screen is.
    // See Dimens.DIALOG_WIDTH in ArkUIApp, which carries the same number and the measurement.
    const double cardWidth = kDialogWidthVp * density_;
    const double left = (width_ - cardWidth) / 2.0;
    const double right = left + cardWidth;
    const double innerWidth = cardWidth - padding * 2.0;

    const std::string title =
        dialog_ == Dialog::DarkMode          ? "App dark mode"
        : dialog_ == Dialog::DarkModeLocations ? "Location-based dark mode"
        : dialog_ == Dialog::AlternateCalendar
            ? "Alternate calendar"  // strings.xml settings_appearance_calendar_title
            : "Language";
    const double titleHeight = kDialogTitleLineHeightVp * density_;

    /*
     * Each row is `max(48 dp, its own label)`, NOT a fixed height.
     *
     * Measured on the reference's alternate-calendar dialog, the one list with both one-line and
     * two-line labels: Chinese -> Dangi -> Hebrew -> Indian all sit on a 156 px pitch (48 dp),
     * while "Islamic Calendar (Saudi Arabia, sighting)" occupies 1542..1698 — also 156 px, but
     * that is its TEXT, two 78 px lines with no padding left over. A fixed height cannot express
     * both: at the old 56 dp every row was 26 px too tall.
     */
    TextStyleSpec optionStyle;
    optionStyle.sizeVp = kDialogOptionSize;
    optionStyle.color = kOnSurface.Resolve(dark_);
    const double radioX = left + padding + kRadioOuter * density_ / 2.0;
    const double labelLeft = radioX + kRadioOuter * density_ / 2.0 + dimens::kNormalMargin *
                                                                        density_;
    const double labelWidth = right - padding - labelLeft;
    const double optionLine = kDialogOptionLineHeightVp * density_;

    std::vector<double> heights;
    heights.reserve(options.size());
    double optionsHeight = 0.0;
    for (const Option& option : options) {
        const int lines = MeasureLineCount(option.name, labelWidth, optionStyle);
        const double height = std::max(kDialogOptionMinHeightVp * density_,
                                       optionLine * static_cast<double>(lines < 1 ? 1 : lines));
        heights.push_back(height);
        optionsHeight += height;
    }

    /*
     * The container, term by term, all measured on the reference (whose total is 2418 px):
     *   24 padding + 32 title + 16 margin + rows + 32 gap + 40 button + 24 padding
     */
    const double buttonHeight = kDialogButtonHeightVp * density_;
    const double buttonGap = kDialogButtonGapVp * density_;
    const double cardHeight = padding * 2.0 + titleHeight + dimens::kNormalMargin * density_ +
                              optionsHeight + buttonGap + buttonHeight;
    /*
     * Centred in the window BELOW the status bar, not in the display: the reference's container
     * spans 216..2633, whose centre is 1424 where the display's is 1360. Half the top inset is
     * exactly that. ArkUIApp's ListPreferenceDialog carries the same offset.
     */
    const double top = (height_ - cardHeight) / 2.0 + topInset_ / 2.0;

    FillRoundRect(canvas, left, top, right, top + cardHeight, kDialogCorner * density_,
                  kDialogSurface.Resolve(dark_));

    DrawTextFixed(canvas, title, left + padding, top + padding, innerWidth, titleStyle,
                  titleHeight);

    const double optionsTop = top + padding + titleHeight + dimens::kNormalMargin * density_;
    double rowTop = optionsTop;
    for (size_t i = 0; i < options.size(); ++i) {
        const double rowHeight = heights[i];
        const double centreY = rowTop + rowHeight / 2.0;
        const bool chosen = options[i].value == selected;
        // A Material radio: an outlined circle, filled when chosen.
        FillCircle(canvas, radioX, centreY, kRadioOuter * density_ / 2.0,
                   chosen ? kPrimary.Resolve(dark_) : kOnSurfaceVariant.Resolve(dark_));
        FillCircle(canvas, radioX, centreY, kRadioOuter * density_ / 2.0 - 2.0 * density_,
                   kDialogSurface.Resolve(dark_));
        if (chosen) {
            FillCircle(canvas, radioX, centreY, kRadioInner * density_ / 2.0,
                       kPrimary.Resolve(dark_));
        }

        const int lines = MeasureLineCount(options[i].name, labelWidth, optionStyle);
        const double labelHeight = optionLine * static_cast<double>(lines < 1 ? 1 : lines);
        DrawTextFixed(canvas, options[i].name, labelLeft, centreY - labelHeight / 2.0, labelWidth,
                      optionStyle, optionLine);
        rowTop += rowHeight;
    }
    dialogOptionHeights_ = heights;

    /*
     * android.R.string.cancel — the only button; the radio is the commit.
     *
     * UPPERCASE because that is what the reference device renders: the string resource is "Cancel"
     * and the Material button style upper-cases it. Confirmed with `uiautomator dump` on the
     * Android build, in this dialog and in all five of the Units screen's.
     */
    TextStyleSpec buttonStyle;
    buttonStyle.sizeVp = kDialogButtonSize;
    buttonStyle.color = kPrimary.Resolve(dark_);
    buttonStyle.bold = true;
    buttonStyle.alignment = TextAlignment::Right;
    // The button sits in its own 40 dp row under a 32 dp gap; the label is centred in it.
    const double cancelTop = optionsTop + optionsHeight + buttonGap;
    const double cancelLine = kDialogButtonLineHeightVp * density_;
    DrawTextFixed(canvas, "CANCEL", left + padding,
                  cancelTop + (buttonHeight - cancelLine) / 2.0, innerWidth, buttonStyle,
                  cancelLine);

    dialogLeft_ = left;
    dialogRight_ = right;
    dialogOptionTop_ = optionsTop;
    dialogOptionHeight_ = 0.0;
    dialogCancelTop_ = cancelTop;
    dialogCancelBottom_ = cancelTop + buttonHeight;
}

AppearanceHit AppearanceScreen::HitIconPackDialog(double x, double y) {
    AppearanceHit hit;
    const double iconSize = dimens::kMaterialIconSize * density_;
    if (y >= packRowTop_ && y < packRowBottom_ && x >= dialogLeft_ && x <= dialogRight_) {
        if (x >= packSearchLeft_ && x < packSearchLeft_ + iconSize) {
            hit.action = AppearanceAction::Preview;
        }
        // The row itself selects the pack; with one provider that is already selected, so it just
        // closes — the same outcome as choosing the current value in the radio dialogs.
    }
    dialog_ = Dialog::None;
    return hit;
}

AppearanceHit AppearanceScreen::HitDialog(double x, double y) {
    if (dialog_ == Dialog::IconPack) {
        return HitIconPackDialog(x, y);
    }

    AppearanceHit hit;
    const std::vector<Option> options = OptionsFor(dialog_);
    if (y >= dialogOptionTop_ && !dialogOptionHeights_.empty()) {
        /*
         * Walk the rows rather than dividing by a uniform height — a wrapped label makes its row
         * taller, so a division would drift by a row for everything below the first wrap. The
         * heights are the ones the last Draw computed, which is the geometry actually on screen.
         */
        int index = -1;
        double rowTop = dialogOptionTop_;
        for (size_t i = 0; i < dialogOptionHeights_.size(); ++i) {
            if (y >= rowTop && y < rowTop + dialogOptionHeights_[i]) {
                index = static_cast<int>(i);
                break;
            }
            rowTop += dialogOptionHeights_[i];
        }
        if (index >= 0 && index < static_cast<int>(options.size()) && x >= dialogLeft_ &&
            x <= dialogRight_) {
            hit.action = AppearanceAction::Changed;
            if (dialog_ == Dialog::DarkMode) {
                hit.key = "dark_mode";
            } else if (dialog_ == Dialog::AlternateCalendar) {
                hit.key = "alternate_calendar";
            } else {
                hit.key = "day_night";
            }
            hit.value = options[static_cast<size_t>(index)].value;
            // Language has nothing to store; choosing simply closes.
            if (dialog_ == Dialog::Language) {
                hit.action = AppearanceAction::None;
            }
            // The chosen value has to land before the next Layout(), or the row would redraw with
            // the OLD summary until the shell's round trip came back.
            if (dialog_ == Dialog::AlternateCalendar) {
                alternateCalendar_ = hit.value;
            }
            dialog_ = Dialog::None;
            return hit;
        }
    }
    // Cancel, or anywhere outside the card: dismiss.
    dialog_ = Dialog::None;
    return hit;
}

AppearanceHit AppearanceScreen::HitTest(double x, double y) {
    if (dialog_ != Dialog::None) {
        return HitDialog(x, y);
    }

    AppearanceHit hit;
    const double margin = dimens::kNormalMargin * density_;
    const double iconSize = dimens::kMaterialIconSize * density_;

    if (y >= topInset_ && y < topInset_ + kBarRowHeight * density_) {
        if (x < margin + iconSize) {
            hit.action = AppearanceAction::Close;
        } else if (x > width_ - margin - iconSize) {
            hit.action = AppearanceAction::Navigate;
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
            if (!item.enabled) {
                return hit;  // A disabled row swallows the tap, as the Kotlin's does.
            }
            if (item.opens != Dialog::None) {
                dialog_ = item.opens;
                return hit;
            }
            hit.action = AppearanceAction::Navigate;
            hit.destination = item.title;
            return hit;
        }
    }
    return hit;
}

}  // namespace breezy
