#include "PlaceholderScreen.h"

#include <algorithm>

#include "../render/Shapes.h"
#include "../render/TextPainter.h"
#include "../render/Theme.h"

namespace breezy {
namespace {

/** Matches ArkUIApp's Placeholder page: content_text_size / large_title_text_size, 8 dp apart. */
constexpr double kNoteTextSize = 14.0;
constexpr double kNameTextSize = 20.0;
/** ArkUI's default Button — a filled capsule, 40 dp tall. */
constexpr double kButtonHeight = 40.0;
constexpr double kButtonPadding = 24.0;
constexpr double kButtonTextSize = 16.0;

const char* const kNote = "Not ported yet";
const char* const kBack = "Back";

}  // namespace

void PlaceholderScreen::SetGeometry(double widthPx, double heightPx, float density) {
    width_ = widthPx;
    height_ = heightPx;
    density_ = density;
}

void PlaceholderScreen::Open(const std::string& destination, const std::string& detail) {
    destination_ = destination;
    detail_ = detail;
}

void PlaceholderScreen::Draw(OH_Drawing_Canvas* canvas) {
    if (width_ <= 0.0 || height_ <= 0.0) {
        return;
    }
    const double gap = dimens::kSmallMargin * density_;
    const double padding = dimens::kLargeMargin * density_;
    const double contentWidth = width_ - padding * 2.0;

    // The LIGHT palette on purpose — ArkUIApp's page pins `.light` too.
    FillRect(canvas, 0.0, 0.0, width_, height_, kBackground.Resolve(false));

    TextStyleSpec noteStyle;
    noteStyle.sizeVp = kNoteTextSize;
    noteStyle.color = kBodyText.Resolve(false);
    noteStyle.alignment = TextAlignment::Center;

    TextStyleSpec nameStyle;
    nameStyle.sizeVp = kNameTextSize;
    nameStyle.bold = true;
    nameStyle.color = kOnSurface.Resolve(false);
    nameStyle.alignment = TextAlignment::Center;

    const double noteHeight = MeasureTextHeight(kNote, contentWidth, noteStyle);
    const double nameHeight = MeasureTextHeight(destination_, contentWidth, nameStyle);
    const double detailHeight =
        detail_.empty() ? 0.0 : MeasureTextHeight(detail_, contentWidth, noteStyle) + gap;
    const double buttonHeight = kButtonHeight * density_;
    const double total = noteHeight + gap + nameHeight + detailHeight +
                         dimens::kLargeMargin * density_ + buttonHeight;

    double y = (height_ - total) / 2.0;
    DrawText(canvas, kNote, padding, y, contentWidth, noteStyle);
    y += noteHeight + gap;
    DrawText(canvas, destination_, padding, y, contentWidth, nameStyle);
    y += nameHeight;
    if (!detail_.empty()) {
        y += gap;
        DrawText(canvas, detail_, padding, y, contentWidth, noteStyle);
        y += detailHeight - gap;
    }
    y += dimens::kLargeMargin * density_;

    TextStyleSpec buttonStyle;
    buttonStyle.sizeVp = kButtonTextSize;
    buttonStyle.color = kOnPrimary.Resolve(false);
    buttonStyle.singleLine = true;
    buttonStyle.alignment = TextAlignment::Center;
    const double labelWidth = MeasureTextWidth(kBack, buttonStyle);
    const double buttonWidth = labelWidth + kButtonPadding * 2.0 * density_;
    buttonLeft_ = (width_ - buttonWidth) / 2.0;
    buttonRight_ = buttonLeft_ + buttonWidth;
    buttonTop_ = y;
    buttonBottom_ = y + buttonHeight;
    // A capsule, which is what an ArkUI Button is by default.
    FillRoundRect(canvas, buttonLeft_, buttonTop_, buttonRight_, buttonBottom_, buttonHeight / 2.0,
                  kPrimary.Resolve(false));
    const double labelHeight = MeasureTextHeight(kBack, buttonWidth, buttonStyle);
    DrawText(canvas, kBack, buttonLeft_, buttonTop_ + (buttonHeight - labelHeight) / 2.0,
             buttonWidth, buttonStyle);
}

bool PlaceholderScreen::HitBack(double x, double y) const {
    return x >= buttonLeft_ && x <= buttonRight_ && y >= buttonTop_ && y <= buttonBottom_;
}

}  // namespace breezy
