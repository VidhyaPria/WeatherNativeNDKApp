#include "ModulesScreen.h"

#include <algorithm>

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
constexpr double kSectionTextSize = 14.0;

/* The choice dialog, same geometry as every other list preference — see AppearanceScreen. */
constexpr double kDialogWidthVp = 320.0;
constexpr double kDialogCorner = 28.0;
constexpr double kDialogPadding = 24.0;
constexpr double kDialogTitleTextSize = 24.0;
constexpr double kDialogTitleLineHeightVp = 32.0;
constexpr double kDialogOptionSize = 16.0;
constexpr double kDialogOptionMinHeightVp = 48.0;
constexpr double kDialogOptionLineHeightVp = 24.0;
constexpr double kDialogButtonSize = 14.0;
constexpr double kDialogButtonHeightVp = 40.0;
constexpr double kDialogButtonGapVp = 32.0;
constexpr double kDialogButtonLineHeightVp = 20.0;
constexpr double kRadioOuter = 20.0;
constexpr double kRadioInner = 10.0;

/** The switch track, matching the other settings screens'. */

/** SettingsManager's key names. ArkUIApp's ModulesSettings.ets carries the same list. */
const char* kKeyMonochrome = "widget_monochrome_icons";
const char* kKeyNotificationWidget = "notification_widget_switch";
const char* kKeyNotificationPersistent = "notification_widget_persistent_switch";
const char* kKeyNotificationTempIcon = "notification_widget_temp_icon_switch";
const char* kKeyNotificationFeelsLike = "notification_widget_feels_like";
const char* kKeyWeekIconMode = "week_icon_mode";
const char* kKeyNotificationStyle = "notification_widget_style";

/**
 * The broadcast sources the Data sharing section lists, in sourceManager order — the two the
 * reference renders. Each is a MultiListPreference over the INSTALLED packages that declare the
 * source's intent action; none can be on HarmonyOS, so both always read "Disabled".
 */
const char* kBroadcastNames[2] = {"Breezy Weather Location Update Notifier", "Gadgetbridge"};

const char* kOn = "Enabled";    // strings.xml settings_enabled
const char* kOff = "Disabled";  // strings.xml settings_disabled

}  // namespace

void ModulesScreen::SetGeometry(double widthPx, double heightPx, float density, double topInsetPx,
                                double bottomInsetPx) {
    width_ = widthPx;
    height_ = heightPx;
    density_ = density;
    topInset_ = topInsetPx;
    bottomInset_ = bottomInsetPx;
}

void ModulesScreen::SetValues(const std::string& weekIconMode, bool monochromeIcons,
                              bool notificationWidget, bool notificationPersistent,
                              const std::string& notificationStyle, bool notificationTempIcon,
                              bool notificationFeelsLike) {
    weekIconMode_ = weekIconMode;
    monochromeIcons_ = monochromeIcons;
    notificationWidget_ = notificationWidget;
    notificationPersistent_ = notificationPersistent;
    notificationStyle_ = notificationStyle;
    notificationTempIcon_ = notificationTempIcon;
    notificationFeelsLike_ = notificationFeelsLike;
}

double ModulesScreen::ListTop() const {
    return topInset_ + kBarExpandedHeight * density_;
}

std::vector<ModulesScreen::Option> ModulesScreen::OptionsFor(Dialog dialog) const {
    if (dialog == Dialog::WeekIconMode) {
        // arrays.xml week_icon_modes / week_icon_mode_values.
        return {{"auto", "Day / Night"}, {"day", "Always day"}, {"night", "Always night"}};
    }
    // arrays.xml notification_styles / notification_style_values.
    return {{"native", "Native"},
            {"cities", "Cities"},
            {"daily", "Daily forecast"},
            {"hourly", "Hourly forecast"}};
}

std::string ModulesScreen::SelectedFor(Dialog dialog) const {
    return dialog == Dialog::WeekIconMode ? weekIconMode_ : notificationStyle_;
}

std::string ModulesScreen::KeyFor(Dialog dialog) const {
    return dialog == Dialog::WeekIconMode ? kKeyWeekIconMode : kKeyNotificationStyle;
}

void ModulesScreen::Layout() {
    // Measured in the SAME face Draw paints in, or the cached heights would be Roboto's.
    SystemFontScope systemFont;
    if (width_ <= 0.0 || density_ <= 0.0f) {
        return;
    }
    const std::string weekIconName = [this]() {
        for (const Option& option : OptionsFor(Dialog::WeekIconMode)) {
            if (option.value == weekIconMode_) {
                return option.name;
            }
        }
        return std::string("Day / Night");
    }();
    const std::string styleName = [this]() {
        for (const Option& option : OptionsFor(Dialog::NotificationStyle)) {
            if (option.value == notificationStyle_) {
                return option.name;
            }
        }
        return std::string("Daily forecast");
    }();

    // kt:283-296 — the rows under the master switch are disabled while it is off.
    const bool sub = notificationWidget_;

    items_.clear();
    items_.push_back({true, "General", "", false, false, false, true, false, false, Dialog::None,
                      "", ""});
    items_.push_back({false, "Live wallpaper", "Set a weather animation as wallpaper", true, false,
                      false, true, false, false, Dialog::None, "", "Live wallpaper"});
    items_.push_back({false, "Icon to display in weekly views", weekIconName, false, false, false,
                      true, false, false, Dialog::WeekIconMode, "", ""});
    items_.push_back({false, "Monochrome icons", monochromeIcons_ ? kOn : kOff, false, true, true,
                      true, true, monochromeIcons_, Dialog::None, kKeyMonochrome, ""});

    /*
     * "Widgets in use" (kt:189-242) is ABSENT: the section only exists when AppWidgetManager
     * reports a placed widget, and this port places none. The reference omits it too on a device
     * with no widgets.
     */

    items_.push_back({true, "Notification widget", "", false, false, false, true, false, false,
                      Dialog::None, "", ""});
    items_.push_back({false, "Notification widget", notificationWidget_ ? kOn : kOff, true, false,
                      false, true, true, notificationWidget_, Dialog::None, kKeyNotificationWidget,
                      ""});
    items_.push_back({false, "Persistent", notificationPersistent_ ? kOn : kOff, false, false,
                      false, sub, true, notificationPersistent_, Dialog::None,
                      kKeyNotificationPersistent, ""});
    items_.push_back({false, "Style", styleName, false, false, false, sub, false, false,
                      Dialog::NotificationStyle, "", ""});
    items_.push_back({false, "Temperature as status bar icon", notificationTempIcon_ ? kOn : kOff,
                      false, false, false, sub, true, notificationTempIcon_, Dialog::None,
                      kKeyNotificationTempIcon, ""});
    items_.push_back({false, "Use feels like temperature", notificationFeelsLike_ ? kOn : kOff,
                      false, true, true, sub, true, notificationFeelsLike_, Dialog::None,
                      kKeyNotificationFeelsLike, ""});

    items_.push_back({true, "Data sharing", "", false, false, false, true, false, false,
                      Dialog::None, "", ""});
    for (int i = 0; i < 2; ++i) {
        // strings.xml settings_modules_broadcast_send_data_title = "Send %s data"
        const std::string title = std::string("Send ") + kBroadcastNames[i] + " data";
        items_.push_back({false, title, kOff, i == 0, i == 1, false, true, false, false,
                          Dialog::None, "", title});
    }

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
            item.height = SectionHeaderHeight(item.title, textWidth, sectionStyle, density_);
            y += item.height;
            continue;
        }
        // A switch row's text is narrower, because the track sits at its trailing edge.
        const double rowTextWidth =
            item.showSwitch ? textWidth - kSwitchWidth * density_ - margin : textWidth;
        item.height = PreferenceRowHeight(item.title, item.summary, rowTextWidth, titleStyle,
                                          summaryStyle, density_);
        y += item.height;
        y += item.endsGroup ? dimens::kSettingsLargeSeparator * density_
                            : dimens::kSettingsSmallSeparator * density_;
    }
}

double ModulesScreen::ContentHeight() const {
    if (items_.empty()) {
        return 0.0;
    }
    const Item& last = items_[items_.size() - 1];
    return last.top + last.height + dimens::kLargeMargin * density_ + bottomInset_;
}

bool ModulesScreen::ScrollBy(double dy) {
    const double maxScroll = std::max(0.0, ContentHeight() - (height_ - ListTop()));
    const double before = scroll_;
    scroll_ = std::max(0.0, std::min(maxScroll, scroll_ - dy));
    return scroll_ != before;
}

void ModulesScreen::Draw(OH_Drawing_Canvas* canvas) {
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
    // strings.xml settings_modules
    const double headlineHeight =
        MeasureTextHeight("External modules", width_ - margin * 2.0, headline);
    DrawText(canvas, "External modules", margin, topInset_ + LargeTitleTop(headlineHeight, density_),
             width_ - margin * 2.0, headline);

    /* ---- the rows ---- */
    OH_Drawing_CanvasSave(canvas);
    OH_Drawing_Rect* clip =
        OH_Drawing_RectCreate(0.0f, static_cast<float>(ListTop()), static_cast<float>(width_),
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

        // A disabled row is drawn at half alpha, not hidden (Preference.kt:159).
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

        const double rowTextWidth =
            item.showSwitch ? textWidth - kSwitchWidth * density_ - margin : textWidth;
        DrawPreferenceRowText(canvas, item.title, item.summary, textLeft, top, item.height, rowTextWidth, titleStyle,
                              summaryStyle, density_);

        if (item.showSwitch) {
            /*
             * This screen drew its switch at 36 x 20 dp with a 16 dp thumb — ArkUI's DEFAULT Toggle
             * size, not Material's. It is the same defect the ArkUI port's Toggle carried until it
             * was corrected to 52 x 32; the NDK's other three switch screens were already right,
             * and this one was missed because it hand-rolled the control instead of calling
             * DrawSwitch. Now it calls the shared one, so there is nowhere left for the two to
             * disagree.
             */
            DrawSwitch(canvas, width_ - margin - kRowHPadding * density_,
                       top + item.height / 2.0, item.checked, item.enabled, density_,
                       WithAlpha(kPrimary.Resolve(dark_), alpha),
                       WithAlpha(kOnSurfaceVariant.Resolve(dark_), alpha),
                       WithAlpha(kSettingsCard.Resolve(dark_), alpha));
        }
    }
    OH_Drawing_CanvasRestore(canvas);

    DrawDialog(canvas);
}

void ModulesScreen::DrawDialog(OH_Drawing_Canvas* canvas) {
    if (dialog_ == Dialog::None) {
        return;
    }
    FillRect(canvas, 0.0, 0.0, width_, height_, 0x52000000);

    const std::vector<Option> options = OptionsFor(dialog_);
    const std::string selected = SelectedFor(dialog_);
    const double padding = kDialogPadding * density_;

    TextStyleSpec titleStyle;
    titleStyle.sizeVp = kDialogTitleTextSize;
    titleStyle.color = kOnSurface.Resolve(dark_);
    titleStyle.singleLine = true;

    const double cardWidth = kDialogWidthVp * density_;
    const double left = (width_ - cardWidth) / 2.0;
    const double right = left + cardWidth;
    const double innerWidth = cardWidth - padding * 2.0;
    const double titleHeight = kDialogTitleLineHeightVp * density_;
    const std::string title = dialog_ == Dialog::WeekIconMode
                                  ? "Icon to display in weekly views"
                                  : "Style";

    TextStyleSpec optionStyle;
    optionStyle.sizeVp = kDialogOptionSize;
    optionStyle.color = kOnSurface.Resolve(dark_);
    const double radioX = left + padding + kRadioOuter * density_ / 2.0;
    const double labelLeft =
        radioX + kRadioOuter * density_ / 2.0 + dimens::kNormalMargin * density_;
    const double labelWidth = right - padding - labelLeft;
    const double optionLine = kDialogOptionLineHeightVp * density_;

    std::vector<double> heights;
    double optionsHeight = 0.0;
    for (const Option& option : options) {
        const int lines = MeasureLineCount(option.name, labelWidth, optionStyle);
        const double h = std::max(kDialogOptionMinHeightVp * density_,
                                  optionLine * static_cast<double>(lines < 1 ? 1 : lines));
        heights.push_back(h);
        optionsHeight += h;
    }

    const double buttonHeight = kDialogButtonHeightVp * density_;
    const double buttonGap = kDialogButtonGapVp * density_;
    const double cardHeight = padding * 2.0 + titleHeight + dimens::kNormalMargin * density_ +
                              optionsHeight + buttonGap + buttonHeight;
    // Centred in the window BELOW the status bar, not in the display — see AppearanceScreen.
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

    TextStyleSpec buttonStyle;
    buttonStyle.sizeVp = kDialogButtonSize;
    buttonStyle.color = kPrimary.Resolve(dark_);
    buttonStyle.bold = true;
    buttonStyle.alignment = TextAlignment::Right;
    const double cancelLine = kDialogButtonLineHeightVp * density_;
    const double cancelTop = optionsTop + optionsHeight + buttonGap;
    DrawTextFixed(canvas, "CANCEL", left + padding, cancelTop + (buttonHeight - cancelLine) / 2.0,
                  innerWidth, buttonStyle, cancelLine);

    dialogLeft_ = left;
    dialogRight_ = right;
    dialogOptionTop_ = optionsTop;
    dialogOptionHeights_ = heights;
}

ModulesHit ModulesScreen::HitDialog(double x, double y) {
    ModulesHit hit;
    const std::vector<Option> options = OptionsFor(dialog_);
    if (y >= dialogOptionTop_ && !dialogOptionHeights_.empty()) {
        // Walk the rows: a wrapped label makes its row taller, so a division would drift.
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
            hit.action = ModulesAction::Changed;
            hit.key = KeyFor(dialog_);
            hit.value = options[static_cast<size_t>(index)].value;
            // Land it before the next Layout, or the row would redraw with the old summary.
            if (dialog_ == Dialog::WeekIconMode) {
                weekIconMode_ = hit.value;
            } else {
                notificationStyle_ = hit.value;
            }
            dialog_ = Dialog::None;
            return hit;
        }
    }
    dialog_ = Dialog::None;
    return hit;
}

ModulesHit ModulesScreen::HitTest(double x, double y) {
    if (dialog_ != Dialog::None) {
        return HitDialog(x, y);
    }

    ModulesHit hit;
    const double margin = dimens::kNormalMargin * density_;
    const double iconSize = dimens::kMaterialIconSize * density_;

    if (y >= topInset_ && y < topInset_ + kBarRowHeight * density_) {
        if (x < margin + iconSize) {
            hit.action = ModulesAction::Close;
        } else if (x > width_ - margin - iconSize) {
            hit.action = ModulesAction::Navigate;
            hit.destination = "About";
        }
        return hit;
    }

    for (const Item& item : items_) {
        if (item.header) {
            continue;
        }
        const double top = ListTop() + item.top - scroll_;
        if (y < top || y >= top + item.height) {
            continue;
        }
        if (!item.enabled) {
            // A disabled row absorbs the tap rather than acting on it.
            return hit;
        }
        if (item.showSwitch) {
            hit.action = ModulesAction::Changed;
            hit.key = item.key;
            hit.value = item.checked ? "false" : "true";
            return hit;
        }
        if (item.opens != Dialog::None) {
            dialog_ = item.opens;
            return hit;
        }
        if (!item.destination.empty()) {
            hit.action = ModulesAction::Navigate;
            hit.destination = item.destination;
        }
        return hit;
    }
    return hit;
}

}  // namespace breezy
