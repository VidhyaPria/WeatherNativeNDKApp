#include "UnitsScreen.h"

#include <algorithm>

#include "../render/AssetImages.h"
#include "../render/Shapes.h"
#include "../render/TextPainter.h"
#include "../render/Theme.h"

namespace breezy {
namespace {

/* LargeTopAppBar, as the root settings and Appearance screens use. Material library values. */
constexpr double kBarRowHeight = 64.0;
constexpr double kBarExpandedHeight = 152.0;
constexpr double kHeadlineTextSize = 28.0;

constexpr double kRowHPadding = 16.0;
constexpr double kRowTitleSize = 16.0;    // typography.titleMedium
constexpr double kRowSummarySize = 14.0;  // typography.bodyMedium

/* The choice dialog (ListPreference.kt:241-313). */
constexpr double kDialogCorner = 28.0;
constexpr double kDialogPadding = 24.0;
constexpr double kDialogTitleSize = 24.0;   // typography.headlineSmall
constexpr double kDialogOptionSize = 16.0;  // typography.bodyLarge
constexpr double kDialogButtonSize = 14.0;  // typography.labelLarge
constexpr double kDialogOptionHeight = 56.0;
constexpr double kRadioOuter = 20.0;
constexpr double kRadioInner = 10.0;

/** kt:69, :105, :157, :192, :227 — settings_units_* in strings.xml, in this order. */
struct RowSpec {
    UnitFamily family;
    const char* title;
};

const RowSpec kRows[] = {
    {UnitFamily::Temperature, "Temperature unit"},
    {UnitFamily::Precipitation, "Precipitation unit"},
    {UnitFamily::Speed, "Speed unit"},
    {UnitFamily::Distance, "Distance unit"},
    {UnitFamily::Pressure, "Pressure unit"},
};
constexpr size_t kRowCount = sizeof(kRows) / sizeof(kRows[0]);

}  // namespace

void UnitsScreen::SetGeometry(double widthPx, double heightPx, float density, double topInsetPx,
                              double bottomInsetPx) {
    width_ = widthPx;
    height_ = heightPx;
    density_ = density;
    topInset_ = topInsetPx;
    bottomInset_ = bottomInsetPx;
    Layout();
}

void UnitsScreen::SyncValues() {
    Layout();
}

double UnitsScreen::ListTop() const {
    return topInset_ + kBarExpandedHeight * density_;
}

std::string UnitsScreen::RegionalName(UnitFamily family) const {
    const UnitDef* resolved = UnitById(family, RegionalDefaultId(family));
    const std::string name = resolved != nullptr ? resolved->longName : "";
    if (family == UnitFamily::Precipitation) {
        // kt:114-130 — "<rain>/<snow>" when the two regional defaults differ, which on a metric
        // device is "millimeters/centimeters".
        const std::string snowId = RegionalDefaultSnowfallId();
        const UnitDef* snow = UnitById(family, snowId);
        if (snow != nullptr && snowId != RegionalDefaultId(family)) {
            return "Regional preference (" + name + "/" + snow->longName + ")";
        }
    }
    return "Regional preference (" + name + ")";
}

std::vector<UnitsScreen::Option> UnitsScreen::OptionsFor(UnitFamily family) const {
    std::vector<Option> options;
    options.push_back({kUnitAuto, RegionalName(family)});
    for (const UnitDef& unit : UnitsFor(family)) {
        options.push_back({unit.id, unit.longName});
    }
    return options;
}

std::string UnitsScreen::SummaryFor(UnitFamily family) const {
    /*
     * kt:84-86 — the summary is the selected entry's name, and an id that is not in the list falls
     * back to the FIRST entry, i.e. the regional one. That fallback is what makes a stored unit the
     * Kotlin later removed degrade gracefully instead of rendering blank.
     */
    const std::string stored = UnitOverride(family);
    const std::vector<Option> options = OptionsFor(family);
    for (const Option& option : options) {
        if (option.value == stored) {
            return option.name;
        }
    }
    return options[0].name;
}

void UnitsScreen::Layout() {
    // Measured in the SAME face Draw paints in, or the cached heights would be Roboto's.
    SystemFontScope systemFont;
    if (width_ <= 0.0 || density_ <= 0.0f) {
        return;
    }
    std::string values;
    for (size_t i = 0; i < kRowCount; ++i) {
        values += UnitOverride(kRows[i].family);
        values += ',';
    }
    if (laidOutWidth_ == width_ && laidOutDensity_ == density_ && laidOutValues_ == values &&
        !items_.empty()) {
        return;
    }
    laidOutWidth_ = width_;
    laidOutDensity_ = density_;
    laidOutValues_ = values;

    items_.clear();
    for (size_t i = 0; i < kRowCount; ++i) {
        Item item;
        item.family = kRows[i].family;
        item.title = kRows[i].title;
        item.summary = SummaryFor(kRows[i].family);
        item.isFirst = i == 0;
        item.isLast = i == kRowCount - 1;
        items_.push_back(item);
    }

    const double margin = dimens::kNormalMargin * density_;
    const double textWidth = width_ - margin * 2.0 - kRowHPadding * 2.0 * density_;

    TextStyleSpec titleStyle;
    titleStyle.sizeVp = kRowTitleSize;
    titleStyle.bold = true;
    TextStyleSpec summaryStyle;
    summaryStyle.sizeVp = kRowSummarySize;

    double y = 0.0;
    for (size_t i = 0; i < items_.size(); ++i) {
        Item& item = items_[i];
        item.top = y;
        item.height = PreferenceRowHeight(item.title, item.summary, textWidth, titleStyle,
                                          summaryStyle, density_);
        y += item.height;
        // One group, so every gap is a smallSeparator (kt:104, :156, :191, :226).
        if (i + 1 < items_.size()) {
            y += dimens::kSettingsSmallSeparator * density_;
        }
    }
}

double UnitsScreen::MaxScroll() const {
    if (items_.empty()) {
        return 0.0;
    }
    const double content =
        items_.back().top + items_.back().height + dimens::kLargeMargin * density_;
    return std::max(0.0, content - (height_ - ListTop() - bottomInset_));
}

bool UnitsScreen::ScrollBy(double dy) {
    const double before = scroll_;
    scroll_ = std::max(0.0, std::min(MaxScroll(), scroll_ - dy));
    return scroll_ != before;
}

void UnitsScreen::Draw(OH_Drawing_Canvas* canvas) {
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
    // kt:61 — AboutActivityIconButton, the same action the other settings screens carry.
    DrawAsset(canvas, "ic_info.svg", width_ - margin - iconSize,
              topInset_ + (kBarRowHeight * density_ - iconSize) / 2.0, iconSize, onSurface, true);

    TextStyleSpec headline;
    headline.sizeVp = kHeadlineTextSize;
    headline.color = onSurface;
    headline.singleLine = true;
    const double headlineHeight = MeasureTextHeight("Units", width_ - margin * 2.0, headline);
    DrawText(canvas, "Units", margin,
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

        DrawPreferenceRowText(canvas, item.title, item.summary, textLeft, top, item.height,
                              textWidth, titleStyle, summaryStyle, density_);
    }
    OH_Drawing_CanvasRestore(canvas);

    DrawDialog(canvas);
}

void UnitsScreen::DrawDialog(OH_Drawing_Canvas* canvas) {
    if (!dialogOpen_) {
        dialogRight_ = dialogLeft_ = 0.0;
        return;
    }
    // A real dialog dims everything behind it.
    FillRect(canvas, 0.0, 0.0, width_, height_, 0x52000000);

    const std::vector<Option> options = OptionsFor(dialogFamily_);
    const std::string selected = UnitOverride(dialogFamily_);
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

    std::string title;
    for (size_t i = 0; i < kRowCount; ++i) {
        if (kRows[i].family == dialogFamily_) {
            title = kRows[i].title;
        }
    }
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
        const bool chosen = options[i].value == selected;
        // A Material radio: an outlined circle, filled when chosen.
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
        const double labelLeft = radioX + kRadioOuter * density_ / 2.0 +
                                 dimens::kNormalMargin * density_;
        const double labelHeight =
            MeasureTextHeight(options[i].name, right - padding - labelLeft, optionStyle);
        DrawText(canvas, options[i].name, labelLeft, centreY - labelHeight / 2.0,
                 right - padding - labelLeft, optionStyle);
    }

    /*
     * android.R.string.cancel — the only button; the radio is the commit.
     *
     * UPPERCASE because that is what the reference device renders: the string resource is "Cancel"
     * and the Material button style upper-cases it. Confirmed with `uiautomator dump` in all five
     * of this screen's dialogs on the Android build.
     */
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

UnitsHit UnitsScreen::HitDialog(double x, double y) {
    UnitsHit hit;
    const std::vector<Option> options = OptionsFor(dialogFamily_);
    if (y >= dialogOptionTop_ && y < dialogCancelTop_ && dialogOptionHeight_ > 0.0) {
        const int index = static_cast<int>((y - dialogOptionTop_) / dialogOptionHeight_);
        if (index >= 0 && index < static_cast<int>(options.size()) && x >= dialogLeft_ &&
            x <= dialogRight_) {
            hit.action = UnitsAction::Changed;
            hit.key = UnitKeyName(dialogFamily_);
            hit.value = options[static_cast<size_t>(index)].value;
            dialogOpen_ = false;
            return hit;
        }
    }
    // Cancel, or anywhere outside the card: dismiss.
    dialogOpen_ = false;
    return hit;
}

UnitsHit UnitsScreen::HitTest(double x, double y) {
    if (dialogOpen_) {
        return HitDialog(x, y);
    }

    UnitsHit hit;
    const double margin = dimens::kNormalMargin * density_;
    const double iconSize = dimens::kMaterialIconSize * density_;

    if (y >= topInset_ && y < topInset_ + kBarRowHeight * density_) {
        if (x < margin + iconSize) {
            hit.action = UnitsAction::Close;
        } else if (x > width_ - margin - iconSize) {
            hit.action = UnitsAction::Navigate;
            hit.destination = "About";
        }
        return hit;
    }

    for (const Item& item : items_) {
        const double top = ListTop() + item.top - scroll_;
        if (y >= top && y < top + item.height) {
            dialogOpen_ = true;
            dialogFamily_ = item.family;
            return hit;
        }
    }
    return hit;
}

}  // namespace breezy
