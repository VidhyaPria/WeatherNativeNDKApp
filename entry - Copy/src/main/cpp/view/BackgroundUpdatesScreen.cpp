#include "BackgroundUpdatesScreen.h"

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

constexpr double kDialogPadding = 24.0;
constexpr double kDialogCorner = 28.0;
constexpr double kDialogTitleSize = 24.0;   // typography.headlineSmall
constexpr double kDialogOptionHeight = 56.0;

/** strings.xml `settings_background_updates` and the two section names. */
const char* kTitle = "Background updates";
const char* kSectionGeneral = "General";
const char* kSectionTroubleshoot = "Troubleshoot";

const char* kRefreshTitle = "Refresh rate";
const char* kSkipTitle = "Skip when battery is low";
const char* kBatteryTitle = "Disable battery optimization";
const char* kBatterySummary =
    "This is the first thing to try when you don\xE2\x80\x99t receive notifications or widgets are "
    "not refreshing.";
const char* kDontKillTitle = "Manufacturer specific instructions";
const char* kDontKillSummary =
    "Some manufacturers have additional app restrictions that kill background services. The "
    "Don\xE2\x80\x99t kill my app! website has info on how to fix it.";
const char* kWorkerTitle = "Worker info";

/** kt:140 — the Continue button is dead for ten seconds. */
constexpr int kNeverCountdownSeconds = 10;

/* The three warning paragraphs, verbatim from strings.xml. %d is 5 (kt:163). */
const char* kNeverWarning1 =
    "By turning off background updates, you will no longer receive notifications of alerts or "
    "precipitation. Widgets won\xE2\x80\x99t be refreshed until you open the app.";
const char* kNeverWarning2 =
    "The background update process is lightweight: it only refreshes your first location at your "
    "select rate, and secondary locations only if you need them, once a day. It\xE2\x80\x99s "
    "battery-efficient and should not exceed 5 MB of data consumption a month.";
const char* kNeverWarning3 =
    "Consider using a less frequent refresh rate rather than turning off background updates, "
    "otherwise tap \xE2\x80\x9C""Continue\xE2\x80\x9D if you still want to proceed.";

}  // namespace

const std::vector<std::string>& UpdateIntervalIds() {
    static const std::vector<std::string> ids = {"never", "0:30", "1:00", "1:30", "2:00",
                                                 "3:00",  "6:00", "12:00", "24:00"};
    return ids;
}

/*
 * NOT `automatic_refresh_rates` verbatim: kt:107-115 replaces each with
 * `UpdateInterval.interval.formatTime(smallestUnit = MINUTES, unitWidth = LONG)` and falls back to
 * the array item only when there is no interval, which is "Never" alone. That is why the
 * reference's summary reads "1 hour, 30 minutes" and not the array's "1 hr and 30 min".
 */
const std::vector<std::string>& UpdateIntervalNames() {
    static const std::vector<std::string> names = {
        "Never",   "30 minutes", "1 hour",   "1 hour, 30 minutes", "2 hours",
        "3 hours", "6 hours",    "12 hours", "24 hours"};
    return names;
}

std::string UpdateIntervalName(const std::string& id) {
    const std::vector<std::string>& ids = UpdateIntervalIds();
    for (size_t i = 0; i < ids.size(); ++i) {
        if (ids[i] == id) {
            return UpdateIntervalNames()[i];
        }
    }
    return UpdateIntervalNames()[0];
}

void BackgroundUpdatesScreen::SetGeometry(double widthPx, double heightPx, float density,
                                          double topInsetPx, double bottomInsetPx) {
    width_ = widthPx;
    height_ = heightPx;
    density_ = density;
    topInset_ = topInsetPx;
    bottomInset_ = bottomInsetPx;
}

void BackgroundUpdatesScreen::SetValues(const std::string& refreshRate, bool skipWhenBatteryLow) {
    refreshRate_ = refreshRate;
    skipWhenBatteryLow_ = skipWhenBatteryLow;
}

double BackgroundUpdatesScreen::ListTop() const {
    return topInset_ + kBarExpandedHeight * density_;
}

void BackgroundUpdatesScreen::Layout() {
    // Measured in the SAME face Draw paints in, or the cached heights would be Roboto's.
    SystemFontScope systemFont;
    const double margin = dimens::kNormalMargin * density_;
    const double textLeft = margin + kRowHPadding * density_;
    const double fullTextWidth = width_ - margin - kRowHPadding * density_ - textLeft;
    const double switchRoom = (kSwitchWidth + kRowHPadding) * density_;

    items_.clear();

    Item general;
    general.header = true;
    general.title = kSectionGeneral;
    items_.push_back(general);

    Item rate;
    rate.title = kRefreshTitle;
    rate.summary = UpdateIntervalName(refreshRate_);
    rate.isFirst = true;
    rate.key = "refresh_rate";
    items_.push_back(rate);

    Item skip;
    skip.title = kSkipTitle;
    skip.summary = skipWhenBatteryLow_ ? "Enabled" : "Disabled";
    skip.isLast = true;
    skip.endsGroup = true;
    skip.isSwitch = true;
    skip.checked = skipWhenBatteryLow_;
    // kt:224 — dead while nothing is scheduled to skip.
    skip.rowEnabled = refreshRate_ != "never";
    skip.key = "ignore_updates_when_battery_low";
    items_.push_back(skip);

    Item troubleshoot;
    troubleshoot.header = true;
    troubleshoot.title = kSectionTroubleshoot;
    items_.push_back(troubleshoot);

    Item battery;
    battery.title = kBatteryTitle;
    battery.summary = kBatterySummary;
    battery.isFirst = true;
    battery.destination = kBatteryTitle;
    items_.push_back(battery);

    Item dontKill;
    dontKill.title = kDontKillTitle;
    dontKill.summary = kDontKillSummary;
    dontKill.destination = kDontKillTitle;
    items_.push_back(dontKill);

    Item worker;
    worker.title = kWorkerTitle;
    /*
     * kt:280-288 — the summary is present only when a background run has happened, and this port
     * never schedules one, so it is always absent. Android shows the same empty row on a device
     * that has never run the job.
     */
    worker.isLast = true;
    worker.endsGroup = true;
    worker.destination = kWorkerTitle;
    items_.push_back(worker);

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
            item.height = SectionHeaderHeight(item.title, fullTextWidth, sectionStyle, density_);
            y += item.height;
            continue;
        }
        const double textWidth = item.isSwitch ? fullTextWidth - switchRoom : fullTextWidth;
        const int summaryLines =
            item.summary.empty() ? 0 : MeasureLineCount(item.summary, textWidth, summaryStyle);
        const double content =
            dimens::kTitleMediumLineHeight * density_ +
            (summaryLines > 0 ? dimens::kSmallMargin * density_ +
                                    static_cast<double>(summaryLines) *
                                        dimens::kBodyMediumLineHeight * density_
                              : 0.0);
        item.height = std::max(content + dimens::kPreferenceRowVPadding * 2.0 * density_,
                               (dimens::kListItemOneLineHeight + dimens::kSmallMargin * 2.0) *
                                   density_);
        y += item.height;
        y += item.endsGroup ? dimens::kSettingsLargeSeparator * density_
                            : dimens::kSettingsSmallSeparator * density_;
    }
}

double BackgroundUpdatesScreen::MaxScroll() const {
    if (items_.empty()) {
        return 0.0;
    }
    const Item& last = items_.back();
    const double content = last.top + last.height + dimens::kLargeMargin * density_;
    const double viewport = height_ - ListTop() - bottomInset_;
    return std::max(0.0, content - viewport);
}

bool BackgroundUpdatesScreen::ScrollBy(double dy) {
    const double next = std::max(0.0, std::min(MaxScroll(), scroll_ - dy));
    if (next == scroll_) {
        return false;
    }
    scroll_ = next;
    return true;
}

void BackgroundUpdatesScreen::CloseDialog() {
    listOpen_ = false;
    neverOpen_ = false;
}

bool BackgroundUpdatesScreen::Tick(int64_t nowMs) {
    if (!neverOpen_) {
        return false;
    }
    const int elapsed = static_cast<int>((nowMs - neverOpenedAtMs_) / 1000);
    const int left = std::max(0, kNeverCountdownSeconds - elapsed);
    if (left == countdown_) {
        return false;
    }
    countdown_ = left;
    return true;
}
/*
 * The Material switch is render/Shapes.h's now — one copy, shared with the other three settings
 * screens and the weather-models dialog that was about to become a fifth.
 */
void BackgroundUpdatesScreen::Draw(OH_Drawing_Canvas* canvas) {
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
    const double headlineHeight = MeasureTextHeight(kTitle, width_ - margin * 2.0, headline);
    DrawText(canvas, kTitle, margin, topInset_ + LargeTitleTop(headlineHeight, density_),
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

        // A disabled row is drawn at half alpha, not hidden (Preference.kt:159).
        const double alpha = item.rowEnabled ? 1.0 : 0.5;
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

        const double textWidth = item.isSwitch ? fullTextWidth - switchRoom : fullTextWidth;
        const double contentTop = top + dimens::kPreferenceRowVPadding * density_;
        const double titleH =
            DrawTextFixed(canvas, item.title, textLeft, contentTop, textWidth, titleStyle,
                          dimens::kTitleMediumLineHeight * density_);
        if (!item.summary.empty()) {
            DrawTextFixed(canvas, item.summary, textLeft,
                          contentTop + titleH + dimens::kSmallMargin * density_, textWidth,
                          summaryStyle, dimens::kBodyMediumLineHeight * density_);
        }
        if (item.isSwitch) {
            DrawSwitch(canvas, width_ - margin - kRowHPadding * density_, top + item.height / 2.0,
                       item.checked, item.rowEnabled, density_, kPrimary.Resolve(dark_),
                       kOnSurfaceVariant.Resolve(dark_), kSettingsCard.Resolve(dark_));
        }
    }
    OH_Drawing_CanvasRestore(canvas);

    if (listOpen_) {
        DrawListDialog(canvas);
    }
    if (neverOpen_) {
        DrawNeverDialog(canvas);
    }
}

/** The refresh-rate picker — ListPreference's plain radio list, one row per UpdateInterval. */
void BackgroundUpdatesScreen::DrawListDialog(OH_Drawing_Canvas* canvas) {
    FillRect(canvas, 0.0, 0.0, width_, height_, 0x52000000);

    const double padding = kDialogPadding * density_;
    const double rowHeight = kDialogOptionHeight * density_;
    const std::vector<std::string>& ids = UpdateIntervalIds();
    const std::vector<std::string>& names = UpdateIntervalNames();

    const double cardWidth = width_ * 0.8;
    const double left = (width_ - cardWidth) / 2.0;
    const double right = left + cardWidth;
    const double innerWidth = cardWidth - padding * 2.0;

    TextStyleSpec titleStyle;
    titleStyle.sizeVp = kDialogTitleSize;
    titleStyle.color = kOnSurface.Resolve(dark_);
    titleStyle.singleLine = true;
    const double titleHeight = MeasureTextHeight(kRefreshTitle, innerWidth, titleStyle);

    const double cardHeight =
        padding * 2.0 + titleHeight + dimens::kNormalMargin * density_ +
        rowHeight * static_cast<double>(names.size());
    const double top = (height_ - cardHeight) / 2.0;

    FillRoundRect(canvas, left, top, right, top + cardHeight, kDialogCorner * density_,
                  kSettingsCard.Resolve(dark_));
    DrawText(canvas, kRefreshTitle, left + padding, top + padding, innerWidth, titleStyle);

    const double firstRow = top + padding + titleHeight + dimens::kNormalMargin * density_;
    TextStyleSpec optionStyle;
    optionStyle.sizeVp = kRowTitleSize;
    optionStyle.singleLine = true;
    const double dot = dimens::kMaterialIconSize * density_;
    for (size_t i = 0; i < names.size(); ++i) {
        const double rowTop = firstRow + rowHeight * static_cast<double>(i);
        const bool on = ids[i] == refreshRate_;
        optionStyle.color = on ? kPrimary.Resolve(dark_) : kOnSurface.Resolve(dark_);
        FillCircle(canvas, left + padding + dot / 2.0, rowTop + rowHeight / 2.0,
                   on ? dot / 2.0 : dot / 2.0 - 3.0 * density_,
                   on ? kPrimary.Resolve(dark_) : kOnSurfaceVariant.Resolve(dark_));
        const double labelHeight = MeasureTextHeight(names[i], innerWidth, optionStyle);
        DrawText(canvas, names[i], left + padding + dot + dimens::kNormalMargin * density_,
                 rowTop + (rowHeight - labelHeight) / 2.0, innerWidth, optionStyle);
    }

    optionTop_ = firstRow;
    optionHeight_ = rowHeight;
    dialogLeft_ = left;
    dialogRight_ = right;
}

/** The AlertDialog at kt:147-214 — three bodyMedium paragraphs, Cancel, and a timed Continue. */
void BackgroundUpdatesScreen::DrawNeverDialog(OH_Drawing_Canvas* canvas) {
    FillRect(canvas, 0.0, 0.0, width_, height_, 0x52000000);

    const double padding = kDialogPadding * density_;
    const double margin = dimens::kNormalMargin * density_;
    const double cardWidth = width_ * 0.8;
    const double left = (width_ - cardWidth) / 2.0;
    const double right = left + cardWidth;
    const double innerWidth = cardWidth - padding * 2.0;
    const double bodyLine = dimens::kBodyMediumLineHeight * density_;

    TextStyleSpec body;
    body.sizeVp = kRowSummarySize;
    body.color = kOnSurfaceVariant.Resolve(dark_);

    const char* const paragraphs[] = {kNeverWarning1, kNeverWarning2, kNeverWarning3};
    double bodyHeight = 0.0;
    for (const char* paragraph : paragraphs) {
        bodyHeight +=
            bodyLine * static_cast<double>(MeasureLineCount(paragraph, innerWidth, body));
    }
    bodyHeight += margin * 2.0;  // the two spacers between the three paragraphs

    TextStyleSpec button;
    button.sizeVp = kRowSummarySize;
    button.singleLine = true;
    const double buttonHeight = dimens::kListItemOneLineHeight * density_ * 0.7;
    const double cardHeight = padding * 2.0 + bodyHeight + margin + buttonHeight;
    const double top = (height_ - cardHeight) / 2.0;

    FillRoundRect(canvas, left, top, right, top + cardHeight, kDialogCorner * density_,
                  kSettingsCard.Resolve(dark_));

    double y = top + padding;
    for (const char* paragraph : paragraphs) {
        y += DrawTextFixed(canvas, paragraph, left + padding, y, innerWidth, body, bodyLine);
        y += margin;
    }
    y -= margin;

    const double buttonsTop = y + margin;
    const std::string continueLabel =
        countdown_ > 0 ? std::string("Continue (") + std::to_string(countdown_) + ")" : "Continue";
    button.color = kPrimary.Resolve(dark_);
    const double cancelWidth = MeasureTextWidth("Cancel", button) + margin * 2.0;
    const double continueWidth = MeasureTextWidth(continueLabel, button) + margin * 2.0;
    const double continueLeft = right - padding - continueWidth;
    const double cancelLeft = continueLeft - cancelWidth;

    DrawText(canvas, "Cancel", cancelLeft + margin, buttonsTop + margin / 2.0, cancelWidth, button);
    // kt:185 — dead until the counter reaches zero, and drawn as dead.
    button.color = countdown_ > 0 ? kOnSurfaceVariant.Resolve(dark_) : kPrimary.Resolve(dark_);
    DrawText(canvas, continueLabel, continueLeft + margin, buttonsTop + margin / 2.0, continueWidth,
             button);

    cancelLeft_ = cancelLeft;
    continueLeft_ = continueLeft;
    buttonWidth_ = continueWidth;
    buttonsTop_ = buttonsTop;
    buttonsBottom_ = buttonsTop + buttonHeight;
}

BackgroundHit BackgroundUpdatesScreen::HitTest(double x, double y) {
    BackgroundHit hit;
    const double margin = dimens::kNormalMargin * density_;
    const double iconSize = dimens::kMaterialIconSize * density_;

    /* ---- the Never sheet swallows everything while it is up ---- */
    if (neverOpen_) {
        if (y >= buttonsTop_ && y < buttonsBottom_) {
            if (x >= continueLeft_ && x < continueLeft_ + buttonWidth_) {
                if (countdown_ > 0) {
                    return hit;  // kt:185 — the button is dead, and a tap does nothing.
                }
                neverOpen_ = false;
                hit.action = BackgroundAction::Changed;
                hit.key = "refresh_rate";
                hit.value = "never";
                return hit;
            }
            if (x >= cancelLeft_ && x < continueLeft_) {
                neverOpen_ = false;
                return hit;
            }
        }
        neverOpen_ = false;  // a tap on the scrim dismisses (kt:148)
        return hit;
    }

    /* ---- the rate picker ---- */
    if (listOpen_) {
        const std::vector<std::string>& ids = UpdateIntervalIds();
        if (x >= dialogLeft_ && x < dialogRight_ && y >= optionTop_ && optionHeight_ > 0.0) {
            const int index = static_cast<int>((y - optionTop_) / optionHeight_);
            if (index >= 0 && index < static_cast<int>(ids.size())) {
                listOpen_ = false;
                // kt:128-136 — "Never" asks first; everything else commits on the tap.
                if (ids[static_cast<size_t>(index)] == "never") {
                    neverOpen_ = true;
                    countdown_ = kNeverCountdownSeconds;
                    neverOpenedAtMs_ = 0;
                    return hit;
                }
                hit.action = BackgroundAction::Changed;
                hit.key = "refresh_rate";
                hit.value = ids[static_cast<size_t>(index)];
                return hit;
            }
        }
        listOpen_ = false;
        return hit;
    }

    /* ---- the app bar ---- */
    if (y >= topInset_ && y < topInset_ + kBarRowHeight * density_) {
        if (x < margin + iconSize) {
            hit.action = BackgroundAction::Close;
        } else if (x > width_ - margin - iconSize) {
            hit.action = BackgroundAction::Navigate;
            hit.destination = "About";
        }
        return hit;
    }

    /* ---- the rows ---- */
    for (const Item& item : items_) {
        if (item.header) {
            continue;
        }
        const double top = ListTop() + item.top - scroll_;
        if (y < top || y >= top + item.height) {
            continue;
        }
        if (!item.rowEnabled) {
            return hit;  // greyed rows take the tap and do nothing.
        }
        if (item.isSwitch) {
            hit.action = BackgroundAction::Changed;
            hit.key = item.key;
            hit.value = item.checked ? "false" : "true";
            return hit;
        }
        if (item.key == "refresh_rate") {
            listOpen_ = true;
            return hit;
        }
        hit.action = BackgroundAction::Navigate;
        hit.destination = item.destination;
        return hit;
    }
    return hit;
}

}  // namespace breezy
