#include "LocationScreen.h"

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

/* The dialogs, identical to screen 3c's choice dialog. */
constexpr double kDialogCorner = 28.0;
constexpr double kDialogPadding = 24.0;
constexpr double kDialogTitleSize = 24.0;
constexpr double kDialogOptionSize = 16.0;
constexpr double kDialogButtonSize = 14.0;
constexpr double kDialogOptionHeight = 56.0;
constexpr double kRadioOuter = 20.0;
constexpr double kRadioInner = 10.0;
constexpr double kFieldHeight = 56.0;
/** OutlinedTextFieldDefaults — 1 dp border, 4 dp corner. */
constexpr double kFieldBorderWidth = 1.0;
constexpr double kFieldCorner = 4.0;

/** strings.xml:713 / :686. */
const char* kDefaultUnchanged = "Default (unchanged)";
const char* kAlreadyGranted = "Permission already granted";
/*
 * strings.xml:493 `action_done` — the EditTextPreference dialog's positive button
 * (EditTextPreference.kt:198-212). NOT `action_confirm` ("Confirm"), which is a different string
 * that other dialogs use; read off the reference device, ref_screen3f_instance_dialog.png.
 */
const char* kDone = "Done";

const char* kKeyLocationService = "location_service";
const char* kKeyBaiduApiKey = "source_baidu_ip_apikey";
const char* kPermCoarse = "ohos.permission.APPROXIMATELY_LOCATION";
const char* kPermPrecise = "ohos.permission.LOCATION";

}  // namespace

void LocationScreen::SetGeometry(double widthPx, double heightPx, float density, double topInsetPx,
                                 double bottomInsetPx) {
    width_ = widthPx;
    height_ = heightPx;
    density_ = density;
    topInset_ = topInsetPx;
    bottomInset_ = bottomInsetPx;
    Layout();
}

void LocationScreen::SetValues(const std::string& locationService, const std::string& baiduApiKey) {
    locationService_ = locationService;
    baiduApiKey_ = baiduApiKey;
    Layout();
}

void LocationScreen::SetPermissions(bool coarseGranted, bool preciseGranted) {
    coarseGranted_ = coarseGranted;
    preciseGranted_ = preciseGranted;
    Layout();
}

void LocationScreen::CloseDialog() {
    if (editing_ && textEntry_ != nullptr) {
        textEntry_(0);
    }
    picker_ = false;
    editing_ = false;
}

double LocationScreen::ListTop() const {
    return topInset_ + kBarExpandedHeight * density_;
}

/** SourceCatalogue's locationSourceList (:210-214), in the Kotlin's order. */
std::vector<LocationScreen::Option> LocationScreen::LocationOptions() const {
    return {{"native", "Android"}, {"ipsb", "IP.SB"}, {"baidu_ip", "Baidu IP location (China)"}};
}

std::string LocationScreen::LocationServiceName() const {
    for (const Option& option : LocationOptions()) {
        if (option.value == locationService_) {
            return option.name;
        }
    }
    return LocationOptions()[0].name;
}

std::string LocationScreen::ApiKeySummary() const {
    return baiduApiKey_.empty() ? kDefaultUnchanged : baiduApiKey_;
}

void LocationScreen::Layout() {
    // Measured in the SAME face Draw paints in, or the cached heights would be Roboto's.
    SystemFontScope systemFont;
    if (width_ <= 0.0 || density_ <= 0.0f) {
        return;
    }
    const std::string values = locationService_ + "|" + baiduApiKey_ +
                               (coarseGranted_ ? "1" : "0") + (preciseGranted_ ? "1" : "0");
    if (laidOutWidth_ == width_ && laidOutDensity_ == density_ && laidOutValues_ == values &&
        !items_.empty()) {
        return;
    }
    laidOutWidth_ = width_;
    laidOutDensity_ = density_;
    laidOutValues_ = values;

    items_.clear();

    /* ---- General (kt:162-183) ---- */
    items_.push_back({true, "General", "", false, false, false, true, Item::Tap::None});
    items_.push_back({false, "Location service", LocationServiceName(), true, true, true, true,
                      Item::Tap::Picker});

    /* ---- Native API (kt:185-283) ---- */
    items_.push_back({true, "Native API", "", false, false, false, true, Item::Tap::None});
    items_.push_back(
        {false, "Location access",
         coarseGranted_ ? "Allowed. You can add your current location as a location."
                        : "Please allow if you want to be able to add your current location as a "
                          "location.",
         true, false, false, true, Item::Tap::RequestCoarse});
    // kt:227 — dead until coarse is granted. The summary stays on the OFF line because this port
    // never requests the background permission; see the header.
    items_.push_back(
        {false, "Location access in background",
         "Please enable “Allow all the time” to have background refreshes for your "
         "current location.",
         false, false, false, coarseGranted_, Item::Tap::Placeholder});
    items_.push_back(
        {false, "Device’s GPS usage",
         preciseGranted_ ? "Enabled. Device’s network provider will be used in priority and "
                           "most of the time, but GPS can also be used as a fallback."
                         : "Enable “Use precise location” if you have trouble getting a "
                           "location.",
         false, true, true, coarseGranted_, Item::Tap::RequestPrecise});

    /* ---- the configurable source's own section (kt:286-327) ---- */
    items_.push_back(
        {true, "Baidu IP location (China)", "", false, false, false, true, Item::Tap::None});
    items_.push_back(
        {false, "API key", ApiKeySummary(), true, true, false, true, Item::Tap::EditKey});

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
        item.height = PreferenceRowHeight(item.title, item.summary, textWidth, titleStyle,
                                          summaryStyle, density_);
        y += item.height;
        if (i + 1 < items_.size()) {
            y += (item.endsGroup ? dimens::kSettingsLargeSeparator
                                 : dimens::kSettingsSmallSeparator) * density_;
        }
    }
}

double LocationScreen::MaxScroll() const {
    if (items_.empty()) {
        return 0.0;
    }
    const double content =
        items_.back().top + items_.back().height + dimens::kLargeMargin * density_;
    return std::max(0.0, content - (height_ - ListTop() - bottomInset_));
}

bool LocationScreen::ScrollBy(double dy) {
    const double before = scroll_;
    scroll_ = std::max(0.0, std::min(MaxScroll(), scroll_ - dy));
    return scroll_ != before;
}

bool LocationScreen::SetEnteredText(const std::string& text) {
    if (!editing_) {
        return false;
    }
    if (draft_ == text) {
        return false;
    }
    draft_ = text;
    return true;
}

void LocationScreen::Draw(OH_Drawing_Canvas* canvas) {
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

    DrawAsset(canvas, "ic_back.svg", margin,
              topInset_ + (kBarRowHeight * density_ - iconSize) / 2.0, iconSize, onSurface, true);
    DrawAsset(canvas, "ic_info.svg", width_ - margin - iconSize,
              topInset_ + (kBarRowHeight * density_ - iconSize) / 2.0, iconSize, onSurface, true);

    TextStyleSpec headline;
    headline.sizeVp = kHeadlineTextSize;
    headline.color = onSurface;
    headline.singleLine = true;
    const double headlineHeight = MeasureTextHeight("Location", width_ - margin * 2.0, headline);
    DrawText(canvas, "Location", margin,
             topInset_ + LargeTitleTop(headlineHeight, density_),
             width_ - margin * 2.0, headline);

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

        FillRoundRectCorners(canvas, margin, top, width_ - margin, top + item.height,
                             item.isFirst ? outer : inner, item.isFirst ? outer : inner,
                             item.isLast ? outer : inner, item.isLast ? outer : inner,
                             kSettingsCard.Resolve(dark_));

        TextStyleSpec titleStyle;
        titleStyle.sizeVp = kRowTitleSize;
        titleStyle.bold = true;
        titleStyle.color = item.rowEnabled ? onSurface : kOnSurfaceVariant.Resolve(dark_);
        TextStyleSpec summaryStyle;
        summaryStyle.sizeVp = kRowSummarySize;
        summaryStyle.color = kOnSurfaceVariant.Resolve(dark_);

        DrawPreferenceRowText(canvas, item.title, item.summary, textLeft, top, item.height, textWidth, titleStyle,
                              summaryStyle, density_);
    }
    OH_Drawing_CanvasRestore(canvas);

    DrawPicker(canvas);
    DrawEditor(canvas);
}

void LocationScreen::DrawPicker(OH_Drawing_Canvas* canvas) {
    if (!picker_) {
        return;
    }
    FillRect(canvas, 0.0, 0.0, width_, height_, 0x52000000);

    const std::vector<Option> options = LocationOptions();
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

    const double titleHeight = MeasureTextHeight("Location service", innerWidth, titleStyle);
    const double buttonHeight = kDialogOptionHeight * density_ * 0.7;
    const double cardHeight = padding * 2.0 + titleHeight + dimens::kNormalMargin * density_ +
                              optionHeight * static_cast<double>(options.size()) + buttonHeight;
    const double top = (height_ - cardHeight) / 2.0;

    FillRoundRect(canvas, left, top, right, top + cardHeight, kDialogCorner * density_,
                  kSettingsCard.Resolve(dark_));
    DrawText(canvas, "Location service", left + padding, top + padding, innerWidth, titleStyle);

    const double optionsTop = top + padding + titleHeight + dimens::kNormalMargin * density_;
    for (size_t i = 0; i < options.size(); ++i) {
        const double rowTop = optionsTop + optionHeight * static_cast<double>(i);
        const double centreY = rowTop + optionHeight / 2.0;
        const double radioX = left + padding + kRadioOuter * density_ / 2.0;
        const bool chosen = options[i].value == locationService_;
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
    dialogButtonTop_ = cancelTop;
    dialogButtonBottom_ = cancelTop + buttonHeight;
}

/** EditTextPreference's dialog: a title, one field and the two actions. */
void LocationScreen::DrawEditor(OH_Drawing_Canvas* canvas) {
    if (!editing_) {
        return;
    }
    FillRect(canvas, 0.0, 0.0, width_, height_, 0x52000000);

    const double padding = kDialogPadding * density_;
    TextStyleSpec titleStyle;
    titleStyle.sizeVp = kDialogTitleSize;
    titleStyle.color = kOnSurface.Resolve(dark_);
    titleStyle.singleLine = true;

    const double cardWidth = width_ * 0.8;
    const double left = (width_ - cardWidth) / 2.0;
    const double right = left + cardWidth;
    const double innerWidth = cardWidth - padding * 2.0;
    const double fieldHeight = kFieldHeight * density_;
    const double buttonHeight = kDialogOptionHeight * density_ * 0.7;

    const double titleHeight = MeasureTextHeight("API key", innerWidth, titleStyle);
    const double cardHeight = padding * 2.0 + titleHeight + dimens::kNormalMargin * density_ +
                              fieldHeight + dimens::kNormalMargin * density_ + buttonHeight;
    const double top = (height_ - cardHeight) / 2.0;

    FillRoundRect(canvas, left, top, right, top + cardHeight, kDialogCorner * density_,
                  kSettingsCard.Resolve(dark_));
    DrawText(canvas, "API key", left + padding, top + padding, innerWidth, titleStyle);

    /*
     * The field: an OUTLINED box with the draft and a caret, since the shell holds the keystrokes.
     *
     * Outlined, not filled — Android's is an `OutlinedTextField` (EditTextPreference.kt:147-196),
     * confirmed on the reference device. This used to paint a surfaceVariant fill.
     */
    const double fieldTop = top + padding + titleHeight + dimens::kNormalMargin * density_;
    StrokeRoundRect(canvas, left + padding, fieldTop, right - padding, fieldTop + fieldHeight,
                    kFieldCorner * density_, kFieldBorderWidth * density_,
                    kComposeOutline.Resolve(dark_));
    TextStyleSpec fieldStyle;
    fieldStyle.sizeVp = kDialogOptionSize;
    fieldStyle.color = kOnSurface.Resolve(dark_);
    fieldStyle.singleLine = true;
    const double fieldTextHeight = MeasureTextHeight(draft_ + "|", innerWidth, fieldStyle);
    DrawText(canvas, draft_ + "|", left + padding + 12.0 * density_,
             fieldTop + (fieldHeight - fieldTextHeight) / 2.0, innerWidth - 24.0 * density_,
             fieldStyle);

    TextStyleSpec buttonStyle;
    buttonStyle.sizeVp = kDialogButtonSize;
    buttonStyle.color = kPrimary.Resolve(dark_);
    buttonStyle.bold = true;
    buttonStyle.singleLine = true;
    buttonStyle.alignment = TextAlignment::Center;
    const double buttonTop = fieldTop + fieldHeight + dimens::kNormalMargin * density_;
    const double actionWidth = 110.0 * density_;
    const double confirmLeft = right - padding - actionWidth;
    const double cancelLeft = confirmLeft - actionWidth;
    const double h = MeasureTextHeight("CANCEL", actionWidth, buttonStyle);
    DrawText(canvas, "CANCEL", cancelLeft, buttonTop + (buttonHeight - h) / 2.0, actionWidth,
             buttonStyle);
    DrawText(canvas, kDone, confirmLeft, buttonTop + (buttonHeight - h) / 2.0, actionWidth,
             buttonStyle);

    dialogLeft_ = left;
    dialogRight_ = right;
    dialogButtonTop_ = buttonTop;
    dialogButtonBottom_ = buttonTop + buttonHeight;
    dialogConfirmLeft_ = confirmLeft;
}

LocationSettingsHit LocationScreen::HitPicker(double x, double y) {
    LocationSettingsHit hit;
    const std::vector<Option> options = LocationOptions();
    if (y >= dialogOptionTop_ && y < dialogButtonTop_ && dialogOptionHeight_ > 0.0 &&
        x >= dialogLeft_ && x <= dialogRight_) {
        const int index = static_cast<int>((y - dialogOptionTop_) / dialogOptionHeight_);
        if (index >= 0 && index < static_cast<int>(options.size())) {
            hit.action = LocationSettingsAction::Changed;
            hit.key = kKeyLocationService;
            hit.value = options[static_cast<size_t>(index)].value;
        }
    }
    picker_ = false;
    return hit;
}

LocationSettingsHit LocationScreen::HitEditor(double x, double y) {
    LocationSettingsHit hit;
    if (y >= dialogButtonTop_ && y <= dialogButtonBottom_ && x >= dialogLeft_ &&
        x <= dialogRight_) {
        const double actionWidth = 110.0 * density_;
        if (x >= dialogConfirmLeft_ && x <= dialogConfirmLeft_ + actionWidth) {
            hit.action = LocationSettingsAction::Changed;
            hit.key = kKeyBaiduApiKey;
            hit.value = draft_;
        }
        CloseDialog();
        return hit;
    }
    // The scrim dismisses without storing; a tap on the card itself is swallowed.
    if (x < dialogLeft_ || x > dialogRight_) {
        CloseDialog();
    }
    return hit;
}

LocationSettingsHit LocationScreen::HitTest(double x, double y) {
    if (picker_) {
        return HitPicker(x, y);
    }
    if (editing_) {
        return HitEditor(x, y);
    }

    LocationSettingsHit hit;
    const double margin = dimens::kNormalMargin * density_;
    const double iconSize = dimens::kMaterialIconSize * density_;

    if (y >= topInset_ && y < topInset_ + kBarRowHeight * density_) {
        if (x < margin + iconSize) {
            hit.action = LocationSettingsAction::Close;
        } else if (x > width_ - margin - iconSize) {
            hit.action = LocationSettingsAction::Navigate;
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
        if (!item.rowEnabled) {
            return hit;  // A disabled row swallows the tap.
        }
        switch (item.tap) {
            case Item::Tap::Picker:
                picker_ = true;
                return hit;
            case Item::Tap::EditKey:
                editing_ = true;
                draft_ = baiduApiKey_;
                // Seed BEFORE opening the session; see SetSeedHandler.
                if (seed_ != nullptr) {
                    seed_(draft_);
                }
                if (textEntry_ != nullptr) {
                    textEntry_(2);  // 2 = a TEXT session; 1 is the time picker's numeric one.
                }
                return hit;
            case Item::Tap::RequestCoarse:
            case Item::Tap::RequestPrecise: {
                const bool granted =
                    item.tap == Item::Tap::RequestCoarse ? coarseGranted_ : preciseGranted_;
                if (granted) {
                    // kt:210-213 — already granted just says so.
                    if (snackbar_ != nullptr) {
                        snackbar_(kAlreadyGranted);
                    }
                    return hit;
                }
                hit.action = LocationSettingsAction::RequestPermission;
                hit.key = item.tap == Item::Tap::RequestCoarse ? kPermCoarse : kPermPrecise;
                return hit;
            }
            case Item::Tap::Placeholder:
                hit.action = LocationSettingsAction::Navigate;
                hit.destination = item.title;
                return hit;
            default:
                return hit;
        }
    }
    return hit;
}

}  // namespace breezy
