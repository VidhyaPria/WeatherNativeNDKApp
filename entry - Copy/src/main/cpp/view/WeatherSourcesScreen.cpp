#include "WeatherSourcesScreen.h"

#include <algorithm>
#include <cstddef>

#include "../data/SourceCatalogue.h"
#include "../data/SourcePreferences.h"
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

/* The dialogs, identical to screen 3e's. */
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
/** The supporting text under a failing field, bodySmall. */
constexpr double kErrorTextSize = 12.0;
/** Compose's disabled-button alpha, and ListPreference.kt:395-399's disabled option. */
constexpr double kDisabledOpacity = 0.38;
constexpr double kDisabledOptionOpacity = 0.5;

/** strings.xml:701 `settings_weather_sources`, :704, :879 and the "auto" key it stands for. */
const char* const kScreenTitle = "Weather sources";
const char* const kDefaultSourceTitle = "Default source for new locations";
const char* const kAutomatic = "Automatic";
const char* const kAuto = "auto";
/** SettingsManager.defaultForecastSource's preferences key. ArkUIApp stores the same one. */
const char* const kKeyDefaultSource = "default_weather_source";
/** strings.xml:493 `action_done` — EditTextPreference's positive button (kt:198-212). */
const char* const kDone = "Done";
/** `settings_weather_source_not_configured` — "%s (not configured)". */
const char* const kNotConfiguredSuffix = " (not configured)";

}  // namespace

void WeatherSourcesScreen::SetGeometry(double widthPx, double heightPx, float density,
                                       double topInsetPx, double bottomInsetPx) {
    width_ = widthPx;
    height_ = heightPx;
    density_ = density;
    topInset_ = topInsetPx;
    bottomInset_ = bottomInsetPx;
    Layout();
}

void WeatherSourcesScreen::SetValues(const std::string& defaultForecastSource,
                                     const std::string& prefs) {
    defaultForecastSource_ = defaultForecastSource;
    prefsBlob_ = prefs;
    prefs_.clear();
    size_t start = 0;
    while (start < prefs.size()) {
        size_t end = prefs.find('\n', start);
        if (end == std::string::npos) {
            end = prefs.size();
        }
        const std::string line = prefs.substr(start, end - start);
        const size_t tab = line.find('\t');
        // A value may itself be empty only transiently; the shell sends set keys only.
        if (tab != std::string::npos) {
            prefs_.emplace_back(line.substr(0, tab), line.substr(tab + 1));
        }
        start = end + 1;
    }
    Layout();
}

void WeatherSourcesScreen::CloseDialog() {
    if (editing_ && textEntry_ != nullptr) {
        textEntry_(0);
    }
    picker_ = false;
    editing_ = false;
    openItem_ = -2;
}

double WeatherSourcesScreen::ListTop() const {
    return topInset_ + kBarExpandedHeight * density_;
}

std::string WeatherSourcesScreen::StoredValue(const std::string& storageKey) const {
    for (const std::pair<std::string, std::string>& entry : prefs_) {
        if (entry.first == storageKey) {
            return entry.second;
        }
    }
    return std::string();
}

/** A List row shows the chosen option's name; the stored value is the option's key. */
std::string WeatherSourcesScreen::ListSummary(int section, int pref) const {
    const SourcePrefSection& s = SourcePreferences()[static_cast<size_t>(section)];
    const SourcePrefSpec& spec = s.prefs[static_cast<size_t>(pref)];
    const std::string stored = StoredValue(SourcePrefKey(s.sourceId, spec.key));
    const std::string wanted = stored.empty() ? spec.fallback : stored;
    for (const SourcePrefOption& option : spec.options) {
        if (option.value == wanted) {
            return option.name;
        }
    }
    return spec.options.empty() ? std::string() : spec.options.front().name;
}

/**
 * A Text row shows what is stored. Empty falls back to the source's own default URL for an instance
 * row (ClimWebService.kt:376-380) and to "Default (unchanged)" for an API key (AtmoService.kt:158).
 */
std::string WeatherSourcesScreen::TextSummary(int section, int pref) const {
    const SourcePrefSection& s = SourcePreferences()[static_cast<size_t>(section)];
    const SourcePrefSpec& spec = s.prefs[static_cast<size_t>(pref)];
    const std::string stored = StoredValue(SourcePrefKey(s.sourceId, spec.key));
    if (!stored.empty()) {
        return stored;
    }
    return spec.placeholder.empty() ? std::string(kSourceDefaultUnchanged) : spec.placeholder;
}

std::vector<WeatherSourcesScreen::Option> WeatherSourcesScreen::DefaultSourceOptions() const {
    std::vector<Option> options;
    options.push_back({kAuto, kAutomatic, true});
    /*
     * kt:165 calls `it.getName(context)`, not `it.name` — SourceExtensions.getName appends
     * `settings_weather_source_not_configured` to an unconfigured source, so the entry is RENAMED
     * as well as dimmed. Read off the Android dialog, which lists "Infoplaza (not configured)",
     * "PirateWeather (not configured)" and "Met Office (United Kingdom) (not configured)".
     */
    const SourceLocation blank;  // `Location()` is what the Kotlin passes.
    for (const SourceInfo* source : GetSupportedFeatureSources(SourceFeature::Forecast, blank, "")) {
        const std::string label =
            source->configured ? source->name : source->name + kNotConfiguredSuffix;
        options.push_back({source->id, label, source->configured});
    }
    return options;
}

/** kt:154-158 — a stored id that is not on the list reads as "Automatic". */
std::string WeatherSourcesScreen::DefaultSourceSummary() const {
    for (const Option& option : DefaultSourceOptions()) {
        if (option.value == defaultForecastSource_) {
            return option.name;
        }
    }
    return kAutomatic;
}

void WeatherSourcesScreen::Layout() {
    // Measured in the SAME face Draw paints in, or the cached heights would be Roboto's.
    SystemFontScope systemFont;
    if (width_ <= 0.0 || density_ <= 0.0f) {
        return;
    }
    const std::string values = defaultForecastSource_ + "\x1f" + prefsBlob_;
    if (laidOutWidth_ == width_ && laidOutDensity_ == density_ && laidOutValues_ == values &&
        !items_.empty()) {
        return;
    }
    laidOutWidth_ = width_;
    laidOutDensity_ = density_;
    laidOutValues_ = values;

    items_.clear();

    Item general;
    general.header = true;
    general.title = "General";
    items_.push_back(general);

    Item defaultRow;
    defaultRow.title = kDefaultSourceTitle;
    defaultRow.summary = DefaultSourceSummary();
    defaultRow.defaultSource = true;
    defaultRow.isFirst = true;
    defaultRow.isLast = true;
    items_.push_back(defaultRow);

    const std::vector<SourcePrefSection>& table = SourcePreferences();
    for (size_t si = 0; si < table.size(); ++si) {
        const SourcePrefSection& section = table[si];
        const SourceInfo* source = GetSource(section.sourceId);
        // A source in the preferences table but not the catalogue would be a porting slip, not a
        // state the app can reach; skip it rather than draw a section with no name.
        if (source == nullptr) {
            continue;
        }
        Item header;
        header.header = true;
        header.title = source->name;
        header.gapAbove = true;  // largeSeparatorItem() before every source section (kt:192).
        items_.push_back(header);

        for (size_t pi = 0; pi < section.prefs.size(); ++pi) {
            const SourcePrefSpec& spec = section.prefs[pi];
            Item row;
            row.title = spec.title;
            row.section = static_cast<int>(si);
            row.pref = static_cast<int>(pi);
            row.summary = spec.kind == SourcePrefKind::List
                              ? ListSummary(row.section, row.pref)
                              : TextSummary(row.section, row.pref);
            row.isFirst = pi == 0;
            row.isLast = pi + 1 == section.prefs.size();
            items_.push_back(row);
        }
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
        if (item.gapAbove) {
            y += dimens::kSettingsLargeSeparator * density_;
        }
        item.top = y;
        if (item.header) {
            item.height = SectionHeaderHeight(item.title, textWidth, sectionStyle, density_);
            y += item.height;
            continue;
        }
        item.height = PreferenceRowHeight(item.title, item.summary, textWidth, titleStyle,
                                          summaryStyle, density_);
        y += item.height;
        // smallSeparatorItem() between rows of one section (kt:230-232). The gap BEFORE the next
        // header is the large one, and that is added by `gapAbove` above.
        if (i + 1 < items_.size() && !items_[i + 1].header) {
            y += dimens::kSettingsSmallSeparator * density_;
        }
    }
}

double WeatherSourcesScreen::MaxScroll() const {
    if (items_.empty()) {
        return 0.0;
    }
    const double content =
        items_.back().top + items_.back().height + dimens::kLargeMargin * density_;
    return std::max(0.0, content - (height_ - ListTop() - bottomInset_));
}

bool WeatherSourcesScreen::ScrollBy(double dy) {
    const double before = scroll_;
    scroll_ = std::max(0.0, std::min(MaxScroll(), scroll_ - dy));
    return scroll_ != before;
}

bool WeatherSourcesScreen::SetEnteredText(const std::string& text) {
    if (!editing_) {
        return false;
    }
    if (draft_ == text) {
        return false;
    }
    draft_ = text;
    return true;
}

std::vector<WeatherSourcesScreen::Option> WeatherSourcesScreen::PickerOptions() const {
    if (openItem_ == -1) {
        return DefaultSourceOptions();
    }
    std::vector<Option> options;
    if (openItem_ < 0 || openItem_ >= static_cast<int>(items_.size())) {
        return options;
    }
    const Item& item = items_[static_cast<size_t>(openItem_)];
    if (item.section < 0) {
        return options;
    }
    const SourcePrefSection& section = SourcePreferences()[static_cast<size_t>(item.section)];
    for (const SourcePrefOption& option :
         section.prefs[static_cast<size_t>(item.pref)].options) {
        options.push_back({option.value, option.name, true});
    }
    return options;
}

std::string WeatherSourcesScreen::PickerTitle() const {
    if (openItem_ == -1) {
        return kDefaultSourceTitle;
    }
    if (openItem_ < 0 || openItem_ >= static_cast<int>(items_.size())) {
        return std::string();
    }
    return items_[static_cast<size_t>(openItem_)].title;
}

/** The key the radio starts on — the stored one, or the preference's own default. */
std::string WeatherSourcesScreen::PickerSelected() const {
    if (openItem_ == -1) {
        for (const Option& option : DefaultSourceOptions()) {
            if (option.value == defaultForecastSource_) {
                return option.value;
            }
        }
        return kAuto;
    }
    if (openItem_ < 0 || openItem_ >= static_cast<int>(items_.size())) {
        return std::string();
    }
    const Item& item = items_[static_cast<size_t>(openItem_)];
    if (item.section < 0) {
        return std::string();
    }
    const SourcePrefSection& section = SourcePreferences()[static_cast<size_t>(item.section)];
    const SourcePrefSpec& spec = section.prefs[static_cast<size_t>(item.pref)];
    const std::string stored = StoredValue(SourcePrefKey(section.sourceId, spec.key));
    return stored.empty() ? spec.fallback : stored;
}

/**
 * EditTextPreference.URL_REGEX (kt:200): an https URL ending in a slash. kt:166-168 — an empty value
 * is only an error when there is no placeholder to fall back on.
 */
bool WeatherSourcesScreen::DraftInvalid() const {
    if (openItem_ < 0 || openItem_ >= static_cast<int>(items_.size())) {
        return false;
    }
    const Item& item = items_[static_cast<size_t>(openItem_)];
    if (item.section < 0) {
        return false;
    }
    const SourcePrefSpec& spec =
        SourcePreferences()[static_cast<size_t>(item.section)].prefs[static_cast<size_t>(item.pref)];
    if (!spec.url) {
        return false;
    }
    if (draft_.empty()) {
        return spec.placeholder.empty();
    }
    const std::string https = "https://";
    return draft_.rfind(https, 0) != 0 || draft_.back() != '/';
}

void WeatherSourcesScreen::Draw(OH_Drawing_Canvas* canvas) {
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
    const double headlineHeight = MeasureTextHeight(kScreenTitle, width_ - margin * 2.0, headline);
    DrawText(canvas, kScreenTitle, margin,
             topInset_ + LargeTitleTop(headlineHeight, density_),
             width_ - margin * 2.0, headline);

    OH_Drawing_CanvasSave(canvas);
    OH_Drawing_Rect* clip = OH_Drawing_RectCreate(0.0f, static_cast<float>(ListTop()),
                                                  static_cast<float>(width_),
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
        titleStyle.color = onSurface;
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

void WeatherSourcesScreen::DrawPicker(OH_Drawing_Canvas* canvas) {
    if (!picker_) {
        return;
    }
    FillRect(canvas, 0.0, 0.0, width_, height_, 0x52000000);

    const std::vector<Option> options = PickerOptions();
    const std::string title = PickerTitle();
    const std::string selected = PickerSelected();
    const double padding = kDialogPadding * density_;
    const double optionHeight = kDialogOptionHeight * density_;

    const double cardWidth = width_ * 0.8;
    const double left = (width_ - cardWidth) / 2.0;
    const double right = left + cardWidth;
    const double innerWidth = cardWidth - padding * 2.0;

    TextStyleSpec titleStyle;
    titleStyle.sizeVp = kDialogTitleSize;
    titleStyle.color = kOnSurface.Resolve(dark_);

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
        const double alpha = options[i].enabled ? 1.0 : kDisabledOptionOpacity;
        FillCircle(canvas, radioX, centreY, kRadioOuter * density_ / 2.0,
                   WithAlpha(chosen ? kPrimary.Resolve(dark_) : kOnSurfaceVariant.Resolve(dark_),
                             alpha));
        FillCircle(canvas, radioX, centreY, kRadioOuter * density_ / 2.0 - 2.0 * density_,
                   kSettingsCard.Resolve(dark_));
        if (chosen) {
            FillCircle(canvas, radioX, centreY, kRadioInner * density_ / 2.0,
                       WithAlpha(kPrimary.Resolve(dark_), alpha));
        }
        TextStyleSpec optionStyle;
        optionStyle.sizeVp = kDialogOptionSize;
        optionStyle.color = WithAlpha(kOnSurface.Resolve(dark_), alpha);
        optionStyle.singleLine = true;
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

/** EditTextPreference's dialog: a title, one outlined field, its error line and the two actions. */
void WeatherSourcesScreen::DrawEditor(OH_Drawing_Canvas* canvas) {
    if (!editing_ || openItem_ < 0 || openItem_ >= static_cast<int>(items_.size())) {
        return;
    }
    const Item& item = items_[static_cast<size_t>(openItem_)];
    if (item.section < 0) {
        return;
    }
    const SourcePrefSpec& spec =
        SourcePreferences()[static_cast<size_t>(item.section)].prefs[static_cast<size_t>(item.pref)];
    const bool invalid = DraftInvalid();

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

    TextStyleSpec errorStyle;
    errorStyle.sizeVp = kErrorTextSize;
    errorStyle.color = kError.Resolve(dark_);
    // kt:169-184 — the supporting line is only present while the value actually fails.
    const double errorHeight =
        invalid ? MeasureTextHeight(kSourceInstanceInvalid, innerWidth, errorStyle) +
                      dimens::kSmallMargin * density_
                : 0.0;

    const double titleHeight = MeasureTextHeight(item.title, innerWidth, titleStyle);
    const double cardHeight = padding * 2.0 + titleHeight + dimens::kNormalMargin * density_ +
                              fieldHeight + errorHeight + dimens::kNormalMargin * density_ +
                              buttonHeight;
    const double top = (height_ - cardHeight) / 2.0;

    FillRoundRect(canvas, left, top, right, top + cardHeight, kDialogCorner * density_,
                  kSettingsCard.Resolve(dark_));
    DrawText(canvas, item.title, left + padding, top + padding, innerWidth, titleStyle);

    /*
     * The field: an OUTLINED box with the draft and a caret, since the shell holds the keystrokes.
     * While the draft is empty it shows the row's placeholder in grey, as Compose's does.
     */
    const double fieldTop = top + padding + titleHeight + dimens::kNormalMargin * density_;
    StrokeRoundRect(canvas, left + padding, fieldTop, right - padding, fieldTop + fieldHeight,
                    kFieldCorner * density_, kFieldBorderWidth * density_,
                    invalid ? kError.Resolve(dark_) : kComposeOutline.Resolve(dark_));
    TextStyleSpec fieldStyle;
    fieldStyle.sizeVp = kDialogOptionSize;
    fieldStyle.color = draft_.empty() && !spec.placeholder.empty()
                           ? kOnSurfaceVariant.Resolve(dark_)
                           : kOnSurface.Resolve(dark_);
    fieldStyle.singleLine = true;
    const std::string shown = draft_.empty() && !spec.placeholder.empty()
                                  ? spec.placeholder
                                  : draft_ + "|";
    const double fieldTextHeight = MeasureTextHeight(shown, innerWidth, fieldStyle);
    DrawText(canvas, shown, left + padding + 12.0 * density_,
             fieldTop + (fieldHeight - fieldTextHeight) / 2.0, innerWidth - 24.0 * density_,
             fieldStyle);

    if (invalid) {
        DrawText(canvas, kSourceInstanceInvalid,
                 left + padding + dimens::kNormalMargin * density_,
                 fieldTop + fieldHeight + dimens::kSmallMargin * density_,
                 innerWidth - dimens::kNormalMargin * density_, errorStyle);
    }

    TextStyleSpec buttonStyle;
    buttonStyle.sizeVp = kDialogButtonSize;
    buttonStyle.color = kPrimary.Resolve(dark_);
    buttonStyle.bold = true;
    buttonStyle.singleLine = true;
    buttonStyle.alignment = TextAlignment::Center;
    TextStyleSpec doneStyle = buttonStyle;
    doneStyle.color = WithAlpha(kPrimary.Resolve(dark_), invalid ? kDisabledOpacity : 1.0);

    const double buttonTop =
        fieldTop + fieldHeight + errorHeight + dimens::kNormalMargin * density_;
    const double actionWidth = 110.0 * density_;
    const double confirmLeft = right - padding - actionWidth;
    const double cancelLeft = confirmLeft - actionWidth;
    const double h = MeasureTextHeight("CANCEL", actionWidth, buttonStyle);
    DrawText(canvas, "CANCEL", cancelLeft, buttonTop + (buttonHeight - h) / 2.0, actionWidth,
             buttonStyle);
    DrawText(canvas, kDone, confirmLeft, buttonTop + (buttonHeight - h) / 2.0, actionWidth,
             doneStyle);

    dialogLeft_ = left;
    dialogRight_ = right;
    dialogButtonTop_ = buttonTop;
    dialogButtonBottom_ = buttonTop + buttonHeight;
    dialogConfirmLeft_ = confirmLeft;
}

WeatherSourcesHit WeatherSourcesScreen::HitPicker(double x, double y) {
    WeatherSourcesHit hit;
    const std::vector<Option> options = PickerOptions();
    if (y >= dialogOptionTop_ && y < dialogButtonTop_ && dialogOptionHeight_ > 0.0 &&
        x >= dialogLeft_ && x <= dialogRight_) {
        const int index = static_cast<int>((y - dialogOptionTop_) / dialogOptionHeight_);
        if (index >= 0 && index < static_cast<int>(options.size())) {
            const Option& option = options[static_cast<size_t>(index)];
            // kt:287 — a disabled row absorbs the tap rather than closing the dialog.
            if (!option.enabled) {
                return hit;
            }
            hit.action = WeatherSourcesAction::Changed;
            if (openItem_ == -1) {
                hit.key = kKeyDefaultSource;
            } else {
                const Item& item = items_[static_cast<size_t>(openItem_)];
                const SourcePrefSection& section =
                    SourcePreferences()[static_cast<size_t>(item.section)];
                hit.key = SourcePrefKey(section.sourceId,
                                        section.prefs[static_cast<size_t>(item.pref)].key);
            }
            hit.value = option.value;
        }
    }
    picker_ = false;
    openItem_ = -2;
    return hit;
}

WeatherSourcesHit WeatherSourcesScreen::HitEditor(double x, double y) {
    WeatherSourcesHit hit;
    if (y >= dialogButtonTop_ && y <= dialogButtonBottom_ && x >= dialogLeft_ &&
        x <= dialogRight_) {
        const double actionWidth = 110.0 * density_;
        const bool done = x >= dialogConfirmLeft_ && x <= dialogConfirmLeft_ + actionWidth;
        // kt:200 — a failing value cannot be committed at all, and the button stays live-looking
        // nowhere: it is drawn at 38% and swallows its tap.
        if (done && DraftInvalid()) {
            return hit;
        }
        if (done && openItem_ >= 0 && openItem_ < static_cast<int>(items_.size())) {
            const Item& item = items_[static_cast<size_t>(openItem_)];
            const SourcePrefSection& section =
                SourcePreferences()[static_cast<size_t>(item.section)];
            hit.action = WeatherSourcesAction::Changed;
            hit.key =
                SourcePrefKey(section.sourceId, section.prefs[static_cast<size_t>(item.pref)].key);
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

WeatherSourcesHit WeatherSourcesScreen::HitTest(double x, double y) {
    if (picker_) {
        return HitPicker(x, y);
    }
    if (editing_) {
        return HitEditor(x, y);
    }

    WeatherSourcesHit hit;
    const double margin = dimens::kNormalMargin * density_;
    const double iconSize = dimens::kMaterialIconSize * density_;

    if (y >= topInset_ && y < topInset_ + kBarRowHeight * density_) {
        if (x < margin + iconSize) {
            hit.action = WeatherSourcesAction::Close;
        } else if (x > width_ - margin - iconSize) {
            hit.action = WeatherSourcesAction::Navigate;
            hit.destination = "About";
        }
        return hit;
    }

    for (size_t i = 0; i < items_.size(); ++i) {
        const Item& item = items_[i];
        if (item.header) {
            continue;
        }
        const double top = ListTop() + item.top - scroll_;
        if (y < top || y >= top + item.height) {
            continue;
        }
        if (item.defaultSource) {
            openItem_ = -1;
            picker_ = true;
            return hit;
        }
        const SourcePrefSpec& spec =
            SourcePreferences()[static_cast<size_t>(item.section)]
                .prefs[static_cast<size_t>(item.pref)];
        openItem_ = static_cast<int>(i);
        if (spec.kind == SourcePrefKind::List) {
            picker_ = true;
            return hit;
        }
        editing_ = true;
        draft_ = StoredValue(
            SourcePrefKey(SourcePreferences()[static_cast<size_t>(item.section)].sourceId,
                          spec.key));
        // Seed BEFORE opening the session: the shell applies it once the session is attached.
        if (seed_ != nullptr) {
            seed_(draft_);
        }
        if (textEntry_ != nullptr) {
            textEntry_(2);  // 2 = a TEXT session; 1 is the time picker's numeric one.
        }
        return hit;
    }
    return hit;
}

}  // namespace breezy
