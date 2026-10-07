#include "NotificationsScreen.h"

#include <algorithm>
#include <cstdio>

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

/* The time picker dialog — every value measured off the reference; see the header. */
constexpr double kDialogWidth = 325.0;
constexpr double kDialogCorner = 28.0;
constexpr double kDialogPadding = 24.0;
/** The hour/minute readout boxes. */
constexpr double kSelectorWidth = 96.0;
constexpr double kSelectorHeight = 80.0;
constexpr double kSelectorTextSize = 45.0;
constexpr double kSelectorCorner = 8.0;
/** The AM/PM toggle, 12-hour only. */
constexpr double kPeriodWidth = 52.0;
constexpr double kPeriodTextSize = 14.0;
/** The dial. */
constexpr double kDialSize = 256.0;
/** configuration.screenHeightDp > 400 — below this the dial and its toggle are absent. */
constexpr double kMinHeightForDial = 400.0;
constexpr double kRingOuterRadius = 101.0;
constexpr double kRingInnerRadius = 69.0;
constexpr double kBallRadius = 24.0;
constexpr double kNumeralTextSize = 16.0;
constexpr double kHandWidth = 2.0;
constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;
/**
 * TimePickerPreference.kt:273 — typography.labelMedium, 12sp. A caption above the picker, NOT the
 * headline the list-choice dialogs use.
 */
constexpr double kDialogTitleSize = 12.0;
constexpr double kDialogButtonSize = 14.0;
/** strings.xml:530 — the title is this fixed string, never the row's own (kt:158-162). */
const char* kSelectTime = "Select time";
/** strings.xml:500. An app string, so it renders as written rather than uppercased. */
const char* kConfirm = "Confirm";
/** The selected value; the neighbours above and below are drawn smaller and dimmer. */
constexpr double kWheelSelectedSize = 32.0;
constexpr double kWheelNeighbourSize = 20.0;
constexpr double kWheelCellHeight = 56.0;

/** strings.xml `settings_enabled` / `settings_disabled`. */
const char* kEnabled = "Enabled";
const char* kDisabled = "Disabled";
/** strings.xml:877. */
const char* kNoBackgroundUpdates = "Unavailable, enable background updates first";

/** SettingsManager.kt:157-165 — UpdateInterval.INTERVAL_NEVER's id. */
const char* kUpdateIntervalNever = "never";

const char* kKeyTodayTime = "forecast_time_today";
const char* kKeyTomorrowTime = "forecast_time_tomorrow";

/** "HH:mm" → hour and minute. A malformed value falls back to midnight, as a parse failure would. */
void ParseTime(const std::string& value, int* hour, int* minute) {
    *hour = 0;
    *minute = 0;
    const size_t colon = value.find(':');
    if (colon == std::string::npos) {
        return;
    }
    *hour = std::atoi(value.substr(0, colon).c_str());
    *minute = std::atoi(value.substr(colon + 1).c_str());
    *hour = std::clamp(*hour, 0, 23);
    *minute = std::clamp(*minute, 0, 59);
}

/** Back to the stored form. Zero-padded, because SettingsManager writes and parses "HH:mm". */
std::string FormatTime(int hour, int minute) {
    char buffer[6];
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d", hour, minute);
    return std::string(buffer);
}

std::string TwoDigits(int value) {
    char buffer[3];
    std::snprintf(buffer, sizeof(buffer), "%02d", value);
    return std::string(buffer);
}

}  // namespace

void NotificationsScreen::SetGeometry(double widthPx, double heightPx, float density,
                                      double topInsetPx, double bottomInsetPx) {
    width_ = widthPx;
    height_ = heightPx;
    density_ = density;
    topInset_ = topInsetPx;
    bottomInset_ = bottomInsetPx;
    Layout();
}

void NotificationsScreen::SetValues(bool appUpdates, bool alert, bool precipitation, bool today,
                                    bool tomorrow, const std::string& todayTime,
                                    const std::string& tomorrowTime,
                                    const std::string& refreshRate, bool twelveHour) {
    twelveHour_ = twelveHour;
    appUpdates_ = appUpdates;
    alert_ = alert;
    precipitation_ = precipitation;
    today_ = today;
    tomorrow_ = tomorrow;
    todayTime_ = todayTime;
    tomorrowTime_ = tomorrowTime;
    refreshRate_ = refreshRate;
    Layout();
}

bool NotificationsScreen::BackgroundUpdatesOn() const {
    return refreshRate_ != kUpdateIntervalNever;
}

std::string NotificationsScreen::GeneralSummary(bool value) const {
    if (!BackgroundUpdatesOn()) {
        return kNoBackgroundUpdates;
    }
    return value ? kEnabled : kDisabled;
}

double NotificationsScreen::ListTop() const {
    return topInset_ + kBarExpandedHeight * density_;
}

const std::string& NotificationsScreen::EditedTime() const {
    return dialogKey_ == kKeyTomorrowTime ? tomorrowTime_ : todayTime_;
}

std::string NotificationsScreen::FormatForDisplay(const std::string& value) const {
    int hour = 0;
    int minute = 0;
    ParseTime(value, &hour, &minute);
    if (!twelveHour_) {
        return FormatTime(hour, minute);
    }
    const char* suffix = hour < 12 ? "AM" : "PM";
    int twelve = hour % 12;
    if (twelve == 0) {
        twelve = 12;
    }
    return std::to_string(twelve) + ":" + TwoDigits(minute) + " " + suffix;
}

void NotificationsScreen::Layout() {
    // Measured in the SAME face Draw paints in, or the cached heights would be Roboto's.
    SystemFontScope systemFont;
    if (width_ <= 0.0 || density_ <= 0.0f) {
        return;
    }
    const std::string values = std::string(appUpdates_ ? "1" : "0") +
                               (alert_ ? "1" : "0") + (precipitation_ ? "1" : "0") +
                               (today_ ? "1" : "0") + (tomorrow_ ? "1" : "0") + todayTime_ +
                               tomorrowTime_ + refreshRate_ + (twelveHour_ ? "1" : "0");
    if (laidOutWidth_ == width_ && laidOutDensity_ == density_ && laidOutValues_ == values &&
        !items_.empty()) {
        return;
    }
    laidOutWidth_ = width_;
    laidOutDensity_ = density_;
    laidOutValues_ = values;

    const bool updatesOn = BackgroundUpdatesOn();
    items_.clear();

    /* ---- App updates (kt:109-126) ---- */
    // strings.xml:900 `notification_channel_app_updates`.
    items_.push_back({true, "App updates", "", false, false, false, false, false, false, true, ""});
    // strings.xml:821, key `app_update_check_switch` (kt:109, default FALSE).
    items_.push_back({false, "Notifications of app updates", appUpdates_ ? kEnabled : kDisabled,
                      true, true, true, true, appUpdates_, false, true,
                      "app_update_check_switch"});

    /* ---- General (kt:130-174) ---- */
    items_.push_back({true, "General", "", false, false, false, false, false, false, true, ""});
    // kt:142-143 — the stored flag ANDed with the interval, so a row cannot read "on" while
    // nothing can deliver it.
    items_.push_back({false, "Notifications of severe weather alerts", GeneralSummary(alert_), true,
                      false, false, true, alert_ && updatesOn, false, updatesOn,
                      "alert_notification_switch"});
    items_.push_back({false, "Notifications of precipitations", GeneralSummary(precipitation_),
                      false, true, true, true, precipitation_ && updatesOn, false, updatesOn,
                      "precipitation_notification_switch"});

    /* ---- Forecast (kt:178-234) ---- */
    items_.push_back({true, "Forecast", "", false, false, false, false, false, false, true, ""});
    items_.push_back({false, "Notification for the day’s forecast", today_ ? kEnabled
                                                                                : kDisabled,
                      true, false, false, true, today_, false, true,
                      "timing_forecast_switch_today"});
    // kt:195-205 — the time row is live only while its own switch is on, and kt:134 shows the
    // LOCALISED time rather than the stored "HH:mm".
    items_.push_back({false, "Notification time for the day’s forecast",
                      FormatForDisplay(todayTime_), false, false, false, false, false, true,
                      today_, kKeyTodayTime});
    items_.push_back({false, "Notification for next-day forecast", tomorrow_ ? kEnabled : kDisabled,
                      false, false, false, true, tomorrow_, false, true,
                      "timing_forecast_switch_tomorrow"});
    items_.push_back({false, "Notification time for next-day forecast",
                      FormatForDisplay(tomorrowTime_), false, true, false, false, false, true,
                      tomorrow_, kKeyTomorrowTime});

    const double margin = dimens::kNormalMargin * density_;
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
        y += item.height;
        if (i + 1 < items_.size()) {
            y += (item.endsGroup ? dimens::kSettingsLargeSeparator
                                 : dimens::kSettingsSmallSeparator) * density_;
        }
    }
}

double NotificationsScreen::MaxScroll() const {
    if (items_.empty()) {
        return 0.0;
    }
    const double content =
        items_.back().top + items_.back().height + dimens::kLargeMargin * density_;
    return std::max(0.0, content - (height_ - ListTop() - bottomInset_));
}

bool NotificationsScreen::ScrollBy(double dy) {
    const double before = scroll_;
    scroll_ = std::max(0.0, std::min(MaxScroll(), scroll_ - dy));
    return scroll_ != before;
}

/*
 * The Material switch is render/Shapes.h's now — one copy, shared with the other three settings
 * screens and the weather-models dialog that was about to become a fifth.
 */
void NotificationsScreen::Draw(OH_Drawing_Canvas* canvas) {
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
    // strings.xml:819 `settings_notifications`.
    const double headlineHeight =
        MeasureTextHeight("Notifications", width_ - margin * 2.0, headline);
    DrawText(canvas, "Notifications", margin,
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

        // A disabled row greys BOTH lines — the Kotlin's `enabled = false` does this through the
        // ListItem's colours, not by hiding anything.
        TextStyleSpec titleStyle;
        titleStyle.sizeVp = kRowTitleSize;
        titleStyle.bold = true;
        titleStyle.color = item.rowEnabled ? onSurface : kOnSurfaceVariant.Resolve(dark_);
        TextStyleSpec summaryStyle;
        summaryStyle.sizeVp = kRowSummarySize;
        summaryStyle.color = kOnSurfaceVariant.Resolve(dark_);

        const double textWidth = item.isSwitch ? fullTextWidth - switchRoom : fullTextWidth;
        DrawPreferenceRowText(canvas, item.title, item.summary, textLeft, top, item.height, textWidth,
                              titleStyle, summaryStyle, density_, item.isSwitch);

        if (item.isSwitch) {
            DrawSwitch(canvas, width_ - margin - kRowHPadding * density_,
                       top + item.height / 2.0, item.checked, item.rowEnabled, density_,
                       kPrimary.Resolve(dark_), kOnSurfaceVariant.Resolve(dark_),
                       kSettingsCard.Resolve(dark_));
        }
    }
    OH_Drawing_CanvasRestore(canvas);

    DrawDialog(canvas);
}

/**
 * The Material 3 clock dial (TimePickerPreference.kt:213-214 -> TimePicker(state)).
 *
 * Laid out and painted in one pass, with every rectangle it produces stashed for HitDialog. The
 * geometry is measured, not derived — see the header for the two captures it came from.
 */
void NotificationsScreen::DrawDialog(OH_Drawing_Canvas* canvas) {
    if (!dialogOpen_) {
        dialogRight_ = dialogLeft_ = 0.0;
        return;
    }
    FillRect(canvas, 0.0, 0.0, width_, height_, 0x52000000);

    /*
     * kt:173 / kt:213 — `configuration.screenHeightDp > 400`.
     *
     * In a SHORT viewport Material's dialog is the text input ALONE: no dial, and no keyboard/clock
     * toggle to get back to one. It is a height test rather than an orientation one, but on this
     * device it comes to the same thing — 837 dp tall in portrait, 388 in landscape — and both
     * ports drew the dial there, in a card taller than the window it sat in.
     */
    const bool hasDial = height_ <= 0.0 || height_ / density_ > kMinHeightForDial;
    const bool showInput = textEntry_ || !hasDial;

    const double padding = kDialogPadding * density_;
    const double cardWidth = kDialogWidth * density_;
    const double left = (width_ - cardWidth) / 2.0;
    const double right = left + cardWidth;
    const double innerWidth = cardWidth - padding * 2.0;

    TextStyleSpec titleStyle;
    titleStyle.sizeVp = kDialogTitleSize;
    titleStyle.color = kOnSurfaceVariant.Resolve(dark_);
    titleStyle.singleLine = true;

    const double titleHeight = MeasureTextHeight(kSelectTime, innerWidth, titleStyle);
    const double selectorHeight = kSelectorHeight * density_;
    const double dialSize = kDialSize * density_;
    const double buttonHeight = 48.0 * density_;
    // kt:271 — 20 dp between the title and the picker.
    const double titleGap = 20.0 * density_;
    // In TimeInput mode the dial is replaced by the two typed fields, so the card is that much
    // shorter — kt:213-217 swaps TimePicker for TimeInput rather than stacking them.
    const double bodyHeight = showInput ? 0.0 : dialSize + dimens::kLargeMargin * density_;
    const double cardHeight = padding * 2.0 + titleHeight + titleGap + selectorHeight +
                              bodyHeight + dimens::kNormalMargin * density_ + buttonHeight;
    const double top = (height_ - cardHeight) / 2.0;

    FillRoundRect(canvas, left, top, right, top + cardHeight, kDialogCorner * density_,
                  kSettingsCard.Resolve(dark_));
    // strings.xml:530 — the fixed title, never the row's own (kt:158-162).
    DrawText(canvas, kSelectTime, left + padding, top + padding, innerWidth, titleStyle);

    /* ---- the hour : minute readout, with AM/PM beside it on a 12-hour device ---- */
    const double selectorTop = top + padding + titleHeight + titleGap;
    const double selectorWidth = kSelectorWidth * density_;
    const double periodWidth = kPeriodWidth * density_;
    const double colonWidth = 24.0 * density_;
    const double periodGap = dimens::kNormalMargin * density_;
    const double clusterWidth = selectorWidth * 2.0 + colonWidth +
                                (twelveHour_ ? periodWidth + periodGap : 0.0);
    const double clusterLeft = left + (cardWidth - clusterWidth) / 2.0;

    const int displayHour =
        twelveHour_ ? (pendingHour_ % 12 == 0 ? 12 : pendingHour_ % 12) : pendingHour_;
    const bool pm = pendingHour_ >= 12;

    TextStyleSpec selectorStyle;
    selectorStyle.sizeVp = kSelectorTextSize;
    selectorStyle.singleLine = true;
    selectorStyle.alignment = TextAlignment::Center;

    // The active field is the one the dial is editing — primaryContainer against surfaceVariant.
    const double hourLeft = clusterLeft;
    const double hourRight = hourLeft + selectorWidth;
    FillRoundRect(canvas, hourLeft, selectorTop, hourRight, selectorTop + selectorHeight,
                  kSelectorCorner * density_,
                  editingMinute_ ? kSurfaceVariant.Resolve(dark_)
                                 : kPrimaryContainer.Resolve(dark_));
    selectorStyle.color = editingMinute_ ? kOnSurface.Resolve(dark_)
                                         : kOnPrimaryContainer.Resolve(dark_);
    {
        const double h = MeasureTextHeight(TwoDigits(displayHour), selectorWidth, selectorStyle);
        DrawText(canvas, TwoDigits(displayHour), hourLeft,
                 selectorTop + (selectorHeight - h) / 2.0, selectorWidth, selectorStyle);
    }

    TextStyleSpec colonStyle = selectorStyle;
    colonStyle.color = kOnSurface.Resolve(dark_);
    {
        const double h = MeasureTextHeight(":", colonWidth, colonStyle);
        DrawText(canvas, ":", hourRight, selectorTop + (selectorHeight - h) / 2.0, colonWidth,
                 colonStyle);
    }

    const double minuteLeft = hourRight + colonWidth;
    const double minuteRight = minuteLeft + selectorWidth;
    FillRoundRect(canvas, minuteLeft, selectorTop, minuteRight, selectorTop + selectorHeight,
                  kSelectorCorner * density_,
                  editingMinute_ ? kPrimaryContainer.Resolve(dark_)
                                 : kSurfaceVariant.Resolve(dark_));
    selectorStyle.color = editingMinute_ ? kOnPrimaryContainer.Resolve(dark_)
                                         : kOnSurface.Resolve(dark_);
    {
        const double h = MeasureTextHeight(TwoDigits(pendingMinute_), selectorWidth, selectorStyle);
        DrawText(canvas, TwoDigits(pendingMinute_), minuteLeft,
                 selectorTop + (selectorHeight - h) / 2.0, selectorWidth, selectorStyle);
    }

    dialogPeriodLeft_ = 0.0;
    dialogPeriodRight_ = 0.0;
    if (twelveHour_) {
        const double periodLeft = minuteRight + periodGap;
        const double periodRight = periodLeft + periodWidth;
        const double middle = selectorTop + selectorHeight / 2.0;
        // An outlined container; only the chosen half is filled (tertiaryContainer).
        StrokeRoundRect(canvas, periodLeft, selectorTop, periodRight,
                        selectorTop + selectorHeight, kSelectorCorner * density_,
                        1.0 * density_, kComposeOutline.Resolve(dark_));
        if (!pm) {
            FillRoundRectCorners(canvas, periodLeft, selectorTop, periodRight, middle,
                                 kSelectorCorner * density_, kSelectorCorner * density_, 0.0, 0.0,
                                 kTertiaryContainer.Resolve(dark_));
        } else {
            FillRoundRectCorners(canvas, periodLeft, middle, periodRight,
                                 selectorTop + selectorHeight, 0.0, 0.0,
                                 kSelectorCorner * density_, kSelectorCorner * density_,
                                 kTertiaryContainer.Resolve(dark_));
        }
        TextStyleSpec periodStyle;
        periodStyle.sizeVp = kPeriodTextSize;
        periodStyle.singleLine = true;
        periodStyle.alignment = TextAlignment::Center;
        const double half = selectorHeight / 2.0;
        periodStyle.color = !pm ? kOnTertiaryContainer.Resolve(dark_)
                                : kOnSurfaceVariant.Resolve(dark_);
        double h = MeasureTextHeight("AM", periodWidth, periodStyle);
        DrawText(canvas, "AM", periodLeft, selectorTop + (half - h) / 2.0, periodWidth,
                 periodStyle);
        periodStyle.color = pm ? kOnTertiaryContainer.Resolve(dark_)
                               : kOnSurfaceVariant.Resolve(dark_);
        h = MeasureTextHeight("PM", periodWidth, periodStyle);
        DrawText(canvas, "PM", periodLeft, middle + (half - h) / 2.0, periodWidth, periodStyle);

        dialogPeriodLeft_ = periodLeft;
        dialogPeriodRight_ = periodRight;
        dialogPeriodMiddle_ = middle;
    }

    /* ---- the dial, unless the keyboard button swapped it for the typed fields ---- */
    const double dialTop = selectorTop + selectorHeight + dimens::kLargeMargin * density_;
    const double centreX = left + cardWidth / 2.0;
    const double centreY = dialTop + dialSize / 2.0;
    if (showInput) {
        // The focused field carries an underline, which is what marks it in a TimeInput.
        const double underlineY = selectorTop + selectorHeight + 2.0 * density_;
        const double fieldLeft = editingMinute_ ? minuteLeft : hourLeft;
        FillRect(canvas, fieldLeft, underlineY, fieldLeft + selectorWidth,
                 underlineY + 2.0 * density_, kPrimary.Resolve(dark_));
        dialogDialRadius_ = 0.0;
    } else {
    FillCircle(canvas, centreX, centreY, dialSize / 2.0, kSurfaceVariant.Resolve(dark_));

    // Where the hand points, and how far out the ball sits. The inner ring only exists at 24-hour.
    const double angle = editingMinute_ ? pendingMinute_ * 6.0 : (pendingHour_ % 12) * 30.0;
    const bool innerRing = !editingMinute_ && !twelveHour_ && pendingHour_ >= 12;
    const double handRadius = (innerRing ? kRingInnerRadius : kRingOuterRadius) * density_;
    const double ballX = centreX + handRadius * std::sin(angle * kDeg);
    const double ballY = centreY - handRadius * std::cos(angle * kDeg);
    StrokeLine(canvas, centreX, centreY, ballX, ballY, kHandWidth * density_,
               kPrimary.Resolve(dark_));
    FillCircle(canvas, ballX, ballY, kBallRadius * density_, kPrimary.Resolve(dark_));

    TextStyleSpec numeralStyle;
    numeralStyle.sizeVp = kNumeralTextSize;
    numeralStyle.singleLine = true;
    numeralStyle.alignment = TextAlignment::Center;
    const double cell = kBallRadius * 2.0 * density_;

    for (int i = 0; i < 12; ++i) {
        // The minute face is labelled every five minutes; the hour faces differ by mode.
        int value;
        bool selected;
        if (editingMinute_) {
            value = i * 5;
            selected = pendingMinute_ == value;
        } else if (twelveHour_) {
            value = i == 0 ? 12 : i;
            selected = (pendingHour_ % 12 == 0 ? 12 : pendingHour_ % 12) == value;
        } else {
            value = i;
            selected = pendingHour_ == value;
        }
        const double r = kRingOuterRadius * density_;
        const double cx = centreX + r * std::sin(i * 30.0 * kDeg);
        const double cy = centreY - r * std::cos(i * 30.0 * kDeg);
        numeralStyle.color = selected ? kOnPrimary.Resolve(dark_) : kOnSurface.Resolve(dark_);
        const std::string label = (editingMinute_ || !twelveHour_) ? TwoDigits(value)
                                                                   : std::to_string(value);
        const double h = MeasureTextHeight(label, cell, numeralStyle);
        DrawText(canvas, label, cx - cell / 2.0, cy - h / 2.0, cell, numeralStyle);
    }

    // 24-hour only: the inner ring, 12..23 (ref_screen3d_timepicker_24h.png).
    if (!twelveHour_ && !editingMinute_) {
        for (int i = 0; i < 12; ++i) {
            const int value = 12 + i;
            const double r = kRingInnerRadius * density_;
            const double cx = centreX + r * std::sin(i * 30.0 * kDeg);
            const double cy = centreY - r * std::cos(i * 30.0 * kDeg);
            numeralStyle.color = pendingHour_ == value ? kOnPrimary.Resolve(dark_)
                                                       : kOnSurface.Resolve(dark_);
            const double h = MeasureTextHeight(TwoDigits(value), cell, numeralStyle);
            DrawText(canvas, TwoDigits(value), cx - cell / 2.0, cy - h / 2.0, cell, numeralStyle);
        }
    }
    }  // !showInput

    /*
     * Actions. The keyboard toggle is bottom-LEFT (kt:184-208).
     *
     * `ic_keyboard.svg` is Icons.Outlined.Keyboard — a Material Components ICON, not an app
     * resource, the same category as the LargeTopAppBar metrics this port already flags, because
     * the library is not vendored in breezy-weather-6.2.1/. It carries no XML comment: the OH
     * image decoder rejects an SVG whose first bytes are a comment rather than the element.
     */
    const double buttonTop = top + cardHeight - padding - buttonHeight;
    const double iconSize = dimens::kMaterialIconSize * density_;
    /*
     * kt:172-210 gates the WHOLE toggle slot on the same height test, so a short viewport has no
     * button at all rather than one offering a dial that would not fit. `dialogKeyboardRight_` is
     * what HitDialog tests, so the tap target goes with it.
     */
    if (hasDial) {
        // In TimeInput mode the button offers the way BACK to the dial, so it shows the clock face
        // instead — Icons.Outlined.Schedule (kt:195-199).
        DrawAsset(canvas, textEntry_ ? "ic_schedule.svg" : "ic_keyboard.svg", left + padding,
                  buttonTop + (buttonHeight - iconSize) / 2.0, iconSize,
                  kOnSurfaceVariant.Resolve(dark_), true);
        dialogKeyboardRight_ = left + padding + iconSize;
    } else {
        dialogKeyboardRight_ = 0.0;
    }

    TextStyleSpec buttonStyle;
    buttonStyle.sizeVp = kDialogButtonSize;
    buttonStyle.color = kPrimary.Resolve(dark_);
    buttonStyle.bold = true;
    buttonStyle.singleLine = true;
    buttonStyle.alignment = TextAlignment::Center;
    const double actionWidth = 110.0 * density_;
    const double confirmLeft = right - padding - actionWidth;
    const double cancelLeft = confirmLeft - actionWidth;
    {
        const double h = MeasureTextHeight("CANCEL", actionWidth, buttonStyle);
        DrawText(canvas, "CANCEL", cancelLeft, buttonTop + (buttonHeight - h) / 2.0, actionWidth,
                 buttonStyle);
        DrawText(canvas, kConfirm, confirmLeft, buttonTop + (buttonHeight - h) / 2.0, actionWidth,
                 buttonStyle);
    }

    dialogLeft_ = left;
    dialogRight_ = right;
    dialogHourLeft_ = hourLeft;
    dialogHourRight_ = hourRight;
    dialogMinuteLeft_ = minuteLeft;
    dialogMinuteRight_ = minuteRight;
    dialogSelectorTop_ = selectorTop;
    dialogSelectorBottom_ = selectorTop + selectorHeight;
    dialogDialCentreX_ = centreX;
    dialogDialCentreY_ = centreY;
    dialogDialRadius_ = dialSize / 2.0;
    dialogButtonTop_ = buttonTop;
    dialogButtonBottom_ = buttonTop + buttonHeight;
    dialogCancelLeft_ = cancelLeft;
    dialogConfirmLeft_ = confirmLeft;
}

/**
 * A touch inside the dial picks the nearest value: one step per 6 degrees for minutes, per 30 for
 * hours, measured clockwise from 12 o'clock. At 24-hour the RADIUS chooses the ring — inside the
 * midpoint between them is 12..23, outside is 0..11.
 *
 * Returns true when the touch landed on the dial, so a DRAG can keep moving the hand.
 */
bool NotificationsScreen::HitDial(double x, double y) {
    const double dx = x - dialogDialCentreX_;
    const double dy = y - dialogDialCentreY_;
    const double radius = std::sqrt(dx * dx + dy * dy);
    if (dialogDialRadius_ <= 0.0 || radius > dialogDialRadius_) {
        return false;
    }
    double degrees = std::atan2(dx, -dy) / kDeg;
    if (degrees < 0.0) {
        degrees += 360.0;
    }
    if (editingMinute_) {
        pendingMinute_ = static_cast<int>(std::llround(degrees / 6.0)) % 60;
        return true;
    }
    const int step = static_cast<int>(std::llround(degrees / 30.0)) % 12;
    if (twelveHour_) {
        // 1..12 on the face; the period toggle supplies the other half of the day.
        const int base = step == 0 ? 0 : step;
        pendingHour_ = (pendingHour_ >= 12) ? base + 12 : base;
    } else {
        const bool inner = radius < (kRingOuterRadius + kRingInnerRadius) / 2.0 * density_;
        pendingHour_ = inner ? step + 12 : step;
    }
    return true;
}

/**
 * Digits typed into the focused field.
 *
 * The shell holds the keystrokes and hands over the whole buffer after every edit, so this reads
 * the LAST two digits rather than tracking a caret — which also means a backspace lands correctly.
 * Out-of-range values are ignored rather than clamped: typing "9" toward "19" would otherwise stick
 * at 9 for a frame and then jump.
 */
bool NotificationsScreen::SetEnteredText(const std::string& text) {
    if (!dialogOpen_ || !textEntry_) {
        return false;
    }
    std::string digits;
    for (const char c : text) {
        if (c >= '0' && c <= '9') {
            digits.push_back(c);
        }
    }
    if (digits.size() > 2) {
        digits = digits.substr(digits.size() - 2);
    }
    if (digits.empty()) {
        return false;
    }
    const int value = std::atoi(digits.c_str());
    if (editingMinute_) {
        if (value > 59) {
            return false;
        }
        pendingMinute_ = value;
    } else {
        // A 12-hour field accepts 1..12 and keeps the AM/PM half the toggle is showing.
        if (twelveHour_) {
            if (value < 1 || value > 12) {
                return false;
            }
            const int base = value == 12 ? 0 : value;
            pendingHour_ = (pendingHour_ >= 12) ? base + 12 : base;
        } else {
            if (value > 23) {
                return false;
            }
            pendingHour_ = value;
        }
    }
    return true;
}

bool NotificationsScreen::DragDial(double x, double y) {
    if (!dialogOpen_) {
        return false;
    }
    const int hour = pendingHour_;
    const int minute = pendingMinute_;
    HitDial(x, y);
    return pendingHour_ != hour || pendingMinute_ != minute;
}

NotificationsHit NotificationsScreen::HitDialog(double x, double y) {
    NotificationsHit hit;

    // The readout boxes choose which field the dial — or the keyboard — edits.
    if (y >= dialogSelectorTop_ && y <= dialogSelectorBottom_) {
        if (x >= dialogHourLeft_ && x <= dialogHourRight_) {
            editingMinute_ = false;
            // Moving focus starts a fresh edit session, so the digits land in the new field.
            if (textEntry_ && timeEntry_ != nullptr) {
                timeEntry_(1);
            }
            return hit;
        }
        if (x >= dialogMinuteLeft_ && x <= dialogMinuteRight_) {
            editingMinute_ = true;
            if (textEntry_ && timeEntry_ != nullptr) {
                timeEntry_(1);
            }
            return hit;
        }
        if (dialogPeriodRight_ > dialogPeriodLeft_ && x >= dialogPeriodLeft_ &&
            x <= dialogPeriodRight_) {
            // Moving AM/PM keeps the hand where it is and shifts by twelve hours.
            const bool wantPm = y >= dialogPeriodMiddle_;
            if (wantPm != (pendingHour_ >= 12)) {
                pendingHour_ += wantPm ? 12 : -12;
            }
            return hit;
        }
    }

    if (HitDial(x, y)) {
        return hit;
    }

    if (y >= dialogButtonTop_ && y <= dialogButtonBottom_) {
        const double actionWidth = 110.0 * density_;
        if (x >= dialogConfirmLeft_ && x <= dialogConfirmLeft_ + actionWidth) {
            hit.action = NotificationsAction::Changed;
            hit.key = dialogKey_;
            hit.value = FormatTime(pendingHour_, pendingMinute_);
            dialogOpen_ = false;
            if (textEntry_ && timeEntry_ != nullptr) {
                timeEntry_(0);
            }
            textEntry_ = false;
            return hit;
        }
        if (x >= dialogCancelLeft_ && x <= dialogCancelLeft_ + actionWidth) {
            dialogOpen_ = false;
            if (textEntry_ && timeEntry_ != nullptr) {
                timeEntry_(0);
            }
            textEntry_ = false;
            return hit;
        }
        // The keyboard button swaps the dial for the typed fields, and back (kt:193-199).
        if (x >= dialogLeft_ && x <= dialogKeyboardRight_) {
            textEntry_ = !textEntry_;
            if (timeEntry_ != nullptr) {
                timeEntry_(textEntry_ ? 1 : 0);
            }
            return hit;
        }
    }

    // Anywhere else — the scrim — dismisses without storing, as a Material dialog does.
    if (x < dialogLeft_ || x > dialogRight_) {
        dialogOpen_ = false;
    }
    return hit;
}

NotificationsHit NotificationsScreen::HitTest(double x, double y) {
    if (dialogOpen_) {
        return HitDialog(x, y);
    }

    NotificationsHit hit;
    const double margin = dimens::kNormalMargin * density_;
    const double iconSize = dimens::kMaterialIconSize * density_;

    if (y >= topInset_ && y < topInset_ + kBarRowHeight * density_) {
        if (x < margin + iconSize) {
            hit.action = NotificationsAction::Close;
        } else if (x > width_ - margin - iconSize) {
            hit.action = NotificationsAction::Navigate;
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
            // A disabled row swallows the tap rather than acting on it.
            if (!item.rowEnabled) {
                return hit;
            }
            if (item.opensPicker) {
                dialogOpen_ = true;
                dialogKey_ = item.key;
                ParseTime(EditedTime(), &pendingHour_, &pendingMinute_);
                return hit;
            }
            if (item.isSwitch) {
                // kt:116-120 — the whole row flips it, not just the switch.
                hit.action = NotificationsAction::Changed;
                hit.key = item.key;
                hit.value = item.checked ? "false" : "true";
                return hit;
            }
            hit.action = NotificationsAction::Navigate;
            hit.destination = item.title;
            return hit;
        }
    }
    return hit;
}

}  // namespace breezy
