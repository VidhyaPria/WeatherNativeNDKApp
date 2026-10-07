#include "AlertListScreen.h"

#include <algorithm>

#include "../common/Formatter.h"
#include "../render/AssetImages.h"
#include "../render/Shapes.h"
#include "../render/TextPainter.h"
#include "../render/Theme.h"

namespace breezy {
namespace {

/* LargeTopAppBar — FitStatusBarTopAppBar is one (FitSystemBarComposeWrappers.kt:65-88). */
constexpr double kBarRowHeight = 64.0;
constexpr double kBarExpandedHeight = 152.0;
constexpr double kHeadlineTextSize = 28.0;

/** typography.labelMedium — the date range under the headline. */
constexpr double kLabelMediumTextSize = 12.0;
constexpr double kDividerHeight = 1.0;

}  // namespace

void AlertListScreen::SetGeometry(double widthPx, double heightPx, float density,
                                  double topInsetPx, double bottomInsetPx) {
    width_ = widthPx;
    height_ = heightPx;
    density_ = density;
    topInset_ = topInsetPx;
    bottomInset_ = bottomInsetPx;
}

void AlertListScreen::SetAlerts(std::vector<Alert> alerts, int utcOffsetSeconds, bool is24Hour) {
    alerts_ = std::move(alerts);
    utcOffsetSeconds_ = utcOffsetSeconds;
    is24Hour_ = is24Hour;
}

void AlertListScreen::SetInitialAlert(const std::string& alertId) {
    pendingAlertId_ = alertId;
}

double AlertListScreen::ContentTop() const {
    return topInset_ + kBarExpandedHeight * density_;
}

void AlertListScreen::Reset() {
    scroll_.Reset();
}

std::string AlertListScreen::FormattedDates(const Alert& alert) const {
    if (!alert.hasStart) {
        return {};
    }
    const std::string startDay = MediumDayAndMonth(alert.startDate, utcOffsetSeconds_);
    std::string dates =
        startDay + kLocaleSeparator + ClockTime(alert.startDate, utcOffsetSeconds_, is24Hour_);
    if (alert.hasEnd) {
        const std::string endDay = MediumDayAndMonth(alert.endDate, utcOffsetSeconds_);
        dates += " — ";
        if (endDay != startDay) {
            dates += endDay + kLocaleSeparator;
        }
        dates += ClockTime(alert.endDate, utcOffsetSeconds_, is24Hour_);
    }
    return dates;
}

double AlertListScreen::Layout(OH_Drawing_Canvas* canvas, size_t stopAt) {
    const double margin = dimens::kNormalMargin * density_;
    const double small = dimens::kSmallMargin * density_;
    const double iconSize = dimens::kMaterialIconSize * density_;
    const double radius = dimens::kSettingsCardCornerRadius * density_;
    const uint32_t onSurface = kOnSurface.Resolve(dark_);
    const uint32_t onSurfaceVariant = kOnSurfaceVariant.Resolve(dark_);
    const uint32_t card = kSettingsCard.Resolve(dark_);

    const double cardLeft = margin;
    const double cardWidth = width_ - margin * 2.0;
    // The headline column starts after the triangle and a 16 vp gap (kt:146).
    const double headLeft = cardLeft + margin + iconSize + margin;
    const double headWidth = cardWidth - margin * 3.0 - iconSize;
    const double bodyLeft = cardLeft + margin;
    const double bodyWidth = cardWidth - margin * 2.0;

    TextStyleSpec headlineStyle;
    headlineStyle.sizeVp = dimens::kTitleTextSize;
    headlineStyle.bold = true;  // kt:154 — FontWeight.Bold, not titleMedium's Medium.
    headlineStyle.color = onSurface;
    TextStyleSpec datesStyle;
    datesStyle.sizeVp = kLabelMediumTextSize;
    datesStyle.color = onSurfaceVariant;
    TextStyleSpec bodyStyle;
    bodyStyle.sizeVp = dimens::kContentTextSize;  // typography.bodyMedium
    bodyStyle.color = onSurfaceVariant;

    const double contentTop = ContentTop();
    double y = contentTop - scroll_.Position();

    for (size_t i = 0; i < stopAt; ++i) {
        const Alert& alert = alerts_[i];
        const std::string headline = alert.headline.empty() ? "Alert" : alert.headline;
        const std::string dates = FormattedDates(alert);
        const std::string sourceLine = alert.source.empty() ? std::string() :
                                       // strings.xml `alert_source` = "Source: %s"
                                       std::string("Source: ") + alert.source;

        const double headlineHeight = MeasureTextHeight(headline, headWidth, headlineStyle);
        const double datesHeight =
            dates.empty() ? 0.0 : MeasureTextHeight(dates, headWidth, datesStyle);
        // The row is as tall as the taller of the triangle and the two text lines.
        const double rowHeight = std::max(iconSize, headlineHeight + datesHeight);

        const double descHeight = alert.description.empty()
                                      ? 0.0
                                      : MeasureTextHeight(alert.description, bodyWidth, bodyStyle);
        const double instrHeight = alert.instruction.empty()
                                       ? 0.0
                                       : MeasureTextHeight(alert.instruction, bodyWidth, bodyStyle);
        const double sourceHeight =
            sourceLine.empty() ? 0.0 : MeasureTextHeight(sourceLine, bodyWidth, bodyStyle);

        double cardHeight = margin * 2.0 + rowHeight;
        if (descHeight > 0.0) {
            cardHeight += small + descHeight;
        }
        if (instrHeight > 0.0) {
            if (descHeight > 0.0) {
                cardHeight += small + kDividerHeight * density_;
            }
            cardHeight += small + instrHeight;
        }
        if (sourceHeight > 0.0) {
            if (descHeight > 0.0 || instrHeight > 0.0) {
                cardHeight += small + kDividerHeight * density_;
            }
            cardHeight += small + sourceHeight;
        }

        if (canvas != nullptr && y + cardHeight >= contentTop && y <= height_) {
            FillRoundRect(canvas, cardLeft, y, cardLeft + cardWidth, y + cardHeight, radius, card);

            // kt:138-145 — MaterialShapes.Triangle filled with the alert's severity colour.
            DrawTriangle(canvas, cardLeft + margin, y + margin + (rowHeight - iconSize) / 2.0,
                         iconSize, alert.color != 0 ? alert.color : onSurface);

            double textY = y + margin + (rowHeight - headlineHeight - datesHeight) / 2.0;
            DrawText(canvas, headline, headLeft, textY, headWidth, headlineStyle);
            if (datesHeight > 0.0) {
                DrawText(canvas, dates, headLeft, textY + headlineHeight, headWidth, datesStyle);
            }

            double bodyY = y + margin + rowHeight;
            if (descHeight > 0.0) {
                bodyY += small;
                DrawText(canvas, alert.description, bodyLeft, bodyY, bodyWidth, bodyStyle);
                bodyY += descHeight;
            }
            if (instrHeight > 0.0) {
                if (descHeight > 0.0) {
                    bodyY += small;
                    FillRect(canvas, bodyLeft, bodyY, bodyLeft + bodyWidth,
                             bodyY + kDividerHeight * density_, kOutlineVariant.Resolve(dark_));
                    bodyY += kDividerHeight * density_;
                }
                bodyY += small;
                DrawText(canvas, alert.instruction, bodyLeft, bodyY, bodyWidth, bodyStyle);
                bodyY += instrHeight;
            }
            if (sourceHeight > 0.0) {
                if (descHeight > 0.0 || instrHeight > 0.0) {
                    bodyY += small;
                    FillRect(canvas, bodyLeft, bodyY, bodyLeft + bodyWidth,
                             bodyY + kDividerHeight * density_, kOutlineVariant.Resolve(dark_));
                    bodyY += kDividerHeight * density_;
                }
                bodyY += small;
                DrawText(canvas, sourceLine, bodyLeft, bodyY, bodyWidth, bodyStyle);
            }
        }

        y += cardHeight;
        if (i + 1 != alerts_.size()) {
            y += margin;  // kt:217-219 — 16 vp between cards, none after the last.
        }
    }

    if (stopAt < alerts_.size()) {
        return y + scroll_.Position() - contentTop;
    }
    // bottomInsetItem(extraHeight = getCardListItemMarginDp) — kt:222-224.
    y += margin + small + bottomInset_;
    return y + scroll_.Position() - contentTop;
}

void AlertListScreen::Draw(OH_Drawing_Canvas* canvas) {
    if (width_ <= 0.0 || height_ <= 0.0) {
        return;
    }
    const double margin = dimens::kNormalMargin * density_;
    const double iconSize = dimens::kMaterialIconSize * density_;
    const uint32_t onSurface = kOnSurface.Resolve(dark_);

    FillRect(canvas, 0.0, 0.0, width_, height_, kSurface.Resolve(dark_));

    DrawAsset(canvas, "ic_back.svg", margin,
              topInset_ + (kBarRowHeight * density_ - iconSize) / 2.0, iconSize, onSurface, true);

    TextStyleSpec headline;
    headline.sizeVp = kHeadlineTextSize;
    headline.color = onSurface;
    headline.singleLine = true;
    const std::string title = "Alerts";  // strings.xml `alerts`
    const double headlineHeight = MeasureTextHeight(title, width_ - margin * 2.0, headline);
    DrawText(canvas, title, margin,
             topInset_ + LargeTitleTop(headlineHeight, density_),
             width_ - margin * 2.0, headline);

    /*
     * The requested alert has to be scrolled to before anything is painted, and its offset is only
     * known once the cards above it have been measured — so a dry Layout runs first.
     */
    if (!pendingAlertId_.empty()) {
        const std::string wanted = pendingAlertId_;
        pendingAlertId_.clear();
        size_t index = alerts_.size();
        for (size_t i = 0; i < alerts_.size(); ++i) {
            if (alerts_[i].alertId == wanted) {
                index = i;
                break;
            }
        }
        // An id that is not in the list leaves the scroll at the top, as `scrollToItem(0)` does.
        if (index < alerts_.size() && index > 0) {
            scroll_.Reset();
            scroll_.SetRange(
                std::max(0.0, Layout(nullptr, alerts_.size()) - (height_ - ContentTop())));
            // The cards above the target are exactly how far down the list opens.
            scroll_.Drag(-Layout(nullptr, index));
        }
    }

    const double contentTop = ContentTop();
    OH_Drawing_CanvasSave(canvas);
    OH_Drawing_Rect* clip =
        OH_Drawing_RectCreate(0.0f, static_cast<float>(contentTop), static_cast<float>(width_),
                              static_cast<float>(height_));
    OH_Drawing_CanvasClipRect(canvas, clip, INTERSECT, true);
    OH_Drawing_RectDestroy(clip);

    contentHeight_ = Layout(canvas, alerts_.size());

    OH_Drawing_CanvasRestore(canvas);
}

AlertListHit AlertListScreen::HitTest(double x, double y) {
    AlertListHit hit;
    const double margin = dimens::kNormalMargin * density_;
    const double iconSize = dimens::kMaterialIconSize * density_;
    if (y >= topInset_ && y < topInset_ + kBarRowHeight * density_ && x < margin + iconSize) {
        hit.action = AlertListAction::Close;
    }
    return hit;
}

bool AlertListScreen::ScrollBy(double deltaY) {
    const double viewport = height_ - ContentTop();
    scroll_.SetRange(std::max(0.0, contentHeight_ - viewport));
    const double before = scroll_.Position();
    scroll_.Drag(-deltaY);
    return scroll_.Position() != before;
}

bool AlertListScreen::Advance(double seconds) {
    return scroll_.Advance(seconds);
}

void AlertListScreen::StopScrolling() {
    scroll_.Stop();
}

}  // namespace breezy
