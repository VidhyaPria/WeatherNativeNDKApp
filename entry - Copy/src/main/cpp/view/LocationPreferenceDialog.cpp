#include "view/LocationPreferenceDialog.h"

#include "render/Shapes.h"
#include "render/TextPainter.h"

namespace breezy {
namespace {

/* strings.xml, in the order the rows use them. */
constexpr const char* kTitle = "Edit this location";                       // :474
constexpr const char* kLocationService = "Location service";               // :685
constexpr const char* kWeatherSources = "Weather sources";                 // :701
constexpr const char* kWeatherSourcesSummary =
    "Customize weather data sources used for this location";               // :712
constexpr const char* kPerLocation = "Location preferences";               // :698
constexpr const char* kPerLocationSummary =
    "Additional customizations for this location";                          // :699
constexpr const char* kGlobal = "Global settings";                         // :696
constexpr const char* kGlobalSummary = "Blocks order, items displayed, animations";  // :802
constexpr const char* kCustomName = "Custom name";                         // :431
constexpr const char* kWeatherModels = "Weather models";                   // :766
constexpr const char* kSectionGeneral = "General";
constexpr const char* kDeleteTitle = "Delete location?";                   // :440
constexpr const char* kClose = "CLOSE";                                    // :501
constexpr const char* kDelete = "DELETE";                                  // :538
constexpr const char* kCancel = "CANCEL";
constexpr const char* kConfirm = "Confirm";                                // :500

/*
 * strings.xml:441 / :442. The curly quotes and apostrophe are part of the resource; written as
 * UTF-8 escapes because a typed non-ASCII literal in this tree has reached the device as mojibake
 * before.
 *   \xE2\x80\x99  U+2019 RIGHT SINGLE QUOTATION MARK
 *   \xE2\x80\x9C  U+201C LEFT DOUBLE QUOTATION MARK
 *   \xE2\x80\x9D  U+201D RIGHT DOUBLE QUOTATION MARK
 */
constexpr const char* kDeleteMessagePrefix =
    "You\xE2\x80\x99re about to delete \xE2\x80\x9C";
constexpr const char* kDeleteMessageSuffix =
    "\xE2\x80\x9D from your list of saved locations.";
constexpr const char* kDeleteMessageNoName =
    "You\xE2\x80\x99re about to delete this location from your list of saved locations.";

/** locationSourceList (SourceCatalogue.cpp), in the Kotlin's order. */
const char* const kLocationSourceIds[] = {"native", "ipsb", "baidu_ip"};

/* The name field's dialog, matching LocationScreen's API-key editor. */
constexpr double kDialogPadding = 24.0;
constexpr double kDialogCorner = 28.0;
constexpr double kDialogTitleSize = 24.0;
constexpr double kFieldHeight = 56.0;
constexpr double kFieldCorner = 4.0;
constexpr double kFieldBorderWidth = 1.0;
constexpr double kButtonSize = 14.0;
constexpr double kOptionSize = 16.0;

std::string NameOf(const std::string& id) {
    const SourceInfo* source = GetSource(id);
    return source != nullptr ? source->name : id;
}

}  // namespace

void LocationPreferenceDialog::Open(int index, const SourceLocation& location,
                                    const LocationSources& sources, std::string place,
                                    std::string customName, std::string weatherModels,
                                    bool canDelete) {
    open_ = true;
    mode_ = Mode::Rows;
    index_ = index;
    target_ = location;
    sources_ = sources;
    place_ = std::move(place);
    customName_ = std::move(customName);
    weatherModels_ = std::move(weatherModels);
    canDelete_ = canDelete;
    prefsChanged_ = false;
    ResetModelDraft();
    modal_.ResetScroll();
    BuildModal();
}

bool LocationPreferenceDialog::HasSourcePreferences() const {
    // kt:424 — an empty slot resolves to the forecast source.
    auto resolved = [this](const std::string& slot) {
        return slot.empty() ? sources_.forecast : slot;
    };
    return sources_.forecast == "openmeteo" || resolved(sources_.current) == "openmeteo" ||
           resolved(sources_.minutely) == "openmeteo";
}

void LocationPreferenceDialog::ResetModelDraft() {
    const std::vector<std::string> selected = SelectedWeatherModels(weatherModels_);
    draftModels_.assign(WeatherModels().size(), false);
    for (size_t i = 0; i < WeatherModels().size(); ++i) {
        for (const std::string& id : selected) {
            if (WeatherModels()[i].id == id) {
                draftModels_[i] = true;
                break;
            }
        }
    }
    modelsChanged_ = false;
}

/**
 * kt:854-879, ported as it BEHAVES.
 *
 * Turning a model on turns every other one off. The Kotlin reads
 * `OpenMeteoWeatherModel.entries.filter { id != model.model.id }`, where `id` is the SOURCE's id
 * ("openmeteo") and can never equal a model id — so the filter keeps every entry, the loop disables
 * all of them, and the tapped one is re-enabled after. Whatever was intended, the reference build
 * behaves as a single-select, and a port that allowed two at once would send a `models=a,b` request
 * the reference never sends. Both ports carry this note.
 */
void LocationPreferenceDialog::ToggleModel(size_t index, bool checked) {
    if (index >= draftModels_.size()) {
        return;
    }
    if (checked) {
        for (size_t i = 0; i < draftModels_.size(); ++i) {
            draftModels_[i] = false;
        }
        draftModels_[index] = true;
    } else {
        draftModels_[index] = false;
    }
    modelsChanged_ = true;
}

std::string LocationPreferenceDialog::DraftModelValue() const {
    std::string out;
    for (size_t i = 0; i < draftModels_.size() && i < WeatherModels().size(); ++i) {
        if (draftModels_[i]) {
            if (!out.empty()) {
                out += ',';
            }
            out += WeatherModels()[i].id;
        }
    }
    return out.empty() ? std::string(kModelBestMatch) : out;
}

bool LocationPreferenceDialog::SetEnteredText(const std::string& text) {
    if (!TextEntryOpen() || nameDraft_ == text) {
        return false;
    }
    nameDraft_ = text;
    return true;
}

/* ------------------------------------------------------------------------------ the surfaces -- */

void LocationPreferenceDialog::BuildModal() {
    switch (mode_) {
        case Mode::Rows:
            BuildRows();
            break;
        case Mode::ServicePicker:
            BuildServicePicker();
            break;
        case Mode::Prefs:
            BuildPrefs();
            break;
        case Mode::Models:
            BuildModels();
            break;
        case Mode::NameEdit:
        case Mode::DeleteConfirm:
            // Drawn directly rather than as a ModalList — one has a text field, the other a
            // paragraph, and neither is a list.
            break;
    }
}

void LocationPreferenceDialog::BuildRows() {
    modal_.SetTitle(kTitle);
    modal_.SetWarning("");
    modal_.SetNote("");
    std::vector<ModalEntry> entries;
    if (target_.isCurrentPosition) {
        ModalEntry service;
        service.id = "service";
        service.label = kLocationService;
        service.sub = NameOf(locationService_);
        service.wrapSub = true;
        entries.push_back(service);
    }
    ModalEntry sources;
    sources.id = "sources";
    sources.label = kWeatherSources;
    sources.sub = kWeatherSourcesSummary;
    sources.wrapSub = true;
    entries.push_back(sources);
    if (ShowsPreferencesRow()) {
        ModalEntry prefs;
        prefs.id = "prefs";
        prefs.label = kPerLocation;
        prefs.sub = kPerLocationSummary;
        prefs.wrapSub = true;
        entries.push_back(prefs);
    }
    ModalEntry global;
    global.id = "global";
    global.label = kGlobal;
    global.sub = kGlobalSummary;
    global.wrapSub = true;
    entries.push_back(global);
    modal_.SetEntries(std::move(entries));
    // kt:517-546 — the dismissButton (Delete) precedes the confirmButton (Close) in the row.
    if (canDelete_) {
        modal_.SetButtons({kDelete, kClose});
    } else {
        modal_.SetButtons({kClose});
    }
}

void LocationPreferenceDialog::BuildServicePicker() {
    modal_.SetTitle(kLocationService);
    std::vector<ModalEntry> entries;
    for (const char* id : kLocationSourceIds) {
        ModalEntry entry;
        entry.id = id;
        entry.label = NameOf(id);
        entry.selected = locationService_ == id;
        entries.push_back(entry);
    }
    modal_.SetEntries(std::move(entries));
    modal_.SetButtons({kCancel});
}

void LocationPreferenceDialog::BuildPrefs() {
    modal_.SetTitle(kPerLocation);
    std::vector<ModalEntry> entries;
    // kt:195-218 — a current position has no geocoded name of its own to override.
    if (!target_.isCurrentPosition) {
        ModalEntry header;
        header.label = kSectionGeneral;
        header.header = true;
        entries.push_back(header);
        ModalEntry name;
        name.id = "name";
        name.label = kCustomName;
        name.sub = draftName_.empty() ? place_ : draftName_;
        name.wrapSub = true;
        entries.push_back(name);
    }
    // kt:220-224 — one section per source with a per-location screen.
    if (HasSourcePreferences()) {
        ModalEntry header;
        header.label = "Open-Meteo";
        header.header = true;
        entries.push_back(header);
        ModalEntry models;
        models.id = "models";
        models.label = kWeatherModels;
        models.sub = WeatherModelsSummary(DraftModelValue());
        models.wrapSub = true;
        entries.push_back(models);
    }
    modal_.SetEntries(std::move(entries));
    modal_.SetButtons({kCancel, kConfirm});
}

void LocationPreferenceDialog::BuildModels() {
    modal_.SetTitle(kWeatherModels);
    std::vector<ModalEntry> entries;
    const std::vector<WeatherModelInfo>& catalogue = WeatherModels();
    for (size_t i = 0; i < catalogue.size(); ++i) {
        ModalEntry entry;
        entry.id = catalogue[i].id;
        entry.label = catalogue[i].name;
        entry.sub = catalogue[i].description;
        entry.toggle = true;
        entry.selected = i < draftModels_.size() && draftModels_[i];
        /*
         * kt:843-845 rules ABOVE every combining model; kt:880-882 rules BELOW "Best match", which
         * as "a rule above row N" is a rule above the row that follows it.
         */
        const bool seamless = entry.id.size() > 9 &&
                              entry.id.compare(entry.id.size() - 9, 9, "_seamless") == 0;
        entry.dividerAbove = seamless || (i > 0 && catalogue[i - 1].id == kModelBestMatch);
        entries.push_back(entry);
    }
    modal_.SetEntries(std::move(entries));
    modal_.SetButtons({kCancel, kConfirm});
}

void LocationPreferenceDialog::BuildNameEdit() {
    nameDraft_ = draftName_.empty() ? place_ : draftName_;
}

void LocationPreferenceDialog::BuildDeleteConfirm() {}

/* ------------------------------------------------------------------------------------ draw ---- */

void LocationPreferenceDialog::Draw(OH_Drawing_Canvas* canvas) {
    if (!open_) {
        return;
    }
    // Compose typography: every composable in this stack takes FontFamily.Default.
    SystemFontScope systemFont;

    /*
     * The rows dialog stays on screen UNDER whichever sub-dialog is showing, exactly as a second
     * AlertDialog stacks over the first on Android. It is rebuilt for the draw and the mode's own
     * list built back afterwards, so only one ModalList is needed.
     */
    if (mode_ != Mode::Rows) {
        const Mode active = mode_;
        BuildRows();
        modal_.SetGeometry(width_, height_, density_, dark_, 0.9);
        modal_.Draw(canvas);
        mode_ = active;
        BuildModal();
    }

    modal_.SetGeometry(width_, height_, density_, dark_, 0.9);
    if (mode_ == Mode::Rows || mode_ == Mode::ServicePicker || mode_ == Mode::Prefs ||
        mode_ == Mode::Models) {
        modal_.Draw(canvas);
        return;
    }

    /* ---- the two dialogs that are not lists ---- */
    FillRect(canvas, 0.0, 0.0, width_, height_, 0x52000000);

    const double padding = kDialogPadding * density_;
    const double cardWidth = width_ * 0.8;
    const double left = (width_ - cardWidth) / 2.0;
    const double right = left + cardWidth;
    const double innerWidth = cardWidth - padding * 2.0;

    TextStyleSpec titleStyle;
    titleStyle.sizeVp = kDialogTitleSize;
    titleStyle.color = kOnSurface.Resolve(dark_);

    TextStyleSpec buttonStyle;
    buttonStyle.sizeVp = kButtonSize;
    buttonStyle.color = kPrimary.Resolve(dark_);
    buttonStyle.bold = true;
    buttonStyle.singleLine = true;
    buttonStyle.alignment = TextAlignment::Center;

    const std::string title = mode_ == Mode::NameEdit ? kCustomName : kDeleteTitle;
    const double titleHeight = MeasureTextHeight(title, innerWidth, titleStyle);
    const double buttonHeight = 40.0 * density_;

    double bodyHeight = 0.0;
    std::string message;
    if (mode_ == Mode::NameEdit) {
        bodyHeight = kFieldHeight * density_;
    } else {
        message = place_.empty()
                      ? std::string(kDeleteMessageNoName)
                      : std::string(kDeleteMessagePrefix) + place_ + kDeleteMessageSuffix;
        TextStyleSpec body;
        body.sizeVp = dimens::kContentTextSize;
        body.color = kOnSurfaceVariant.Resolve(dark_);
        bodyHeight = MeasureTextHeight(message, innerWidth, body);
    }

    const double cardHeight = padding * 2.0 + titleHeight + dimens::kNormalMargin * density_ +
                              bodyHeight + dimens::kNormalMargin * density_ + buttonHeight;
    const double top = (height_ - cardHeight) / 2.0;
    FillRoundRect(canvas, left, top, right, top + cardHeight, kDialogCorner * density_,
                  kSurfaceVariant.Resolve(dark_));
    DrawText(canvas, title, left + padding, top + padding, innerWidth, titleStyle);

    const double bodyTop = top + padding + titleHeight + dimens::kNormalMargin * density_;
    if (mode_ == Mode::NameEdit) {
        // An OutlinedTextField, as EditTextPreference.kt:147-196 uses; the caret is drawn because
        // the shell owns the keystrokes and there is no real field here.
        StrokeRoundRect(canvas, left + padding, bodyTop, right - padding,
                        bodyTop + kFieldHeight * density_, kFieldCorner * density_,
                        kFieldBorderWidth * density_, kComposeOutline.Resolve(dark_));
        TextStyleSpec fieldStyle;
        fieldStyle.sizeVp = kOptionSize;
        fieldStyle.color = kOnSurface.Resolve(dark_);
        fieldStyle.singleLine = true;
        const double h = MeasureTextHeight(nameDraft_ + "|", innerWidth, fieldStyle);
        DrawText(canvas, nameDraft_ + "|", left + padding + 12.0 * density_,
                 bodyTop + (kFieldHeight * density_ - h) / 2.0, innerWidth - 24.0 * density_,
                 fieldStyle);
    } else {
        TextStyleSpec body;
        body.sizeVp = dimens::kContentTextSize;
        body.color = kOnSurfaceVariant.Resolve(dark_);
        DrawText(canvas, message, left + padding, bodyTop, innerWidth, body);
    }

    const double buttonTop = bodyTop + bodyHeight + dimens::kNormalMargin * density_;
    const double actionWidth = 110.0 * density_;
    const double confirmLeft = right - padding - actionWidth;
    const double cancelLeft = confirmLeft - actionWidth;
    const double h = MeasureTextHeight(kCancel, actionWidth, buttonStyle);
    DrawText(canvas, kCancel, cancelLeft, buttonTop + (buttonHeight - h) / 2.0, actionWidth,
             buttonStyle);
    DrawText(canvas, mode_ == Mode::NameEdit ? "OK" : kConfirm, confirmLeft,
             buttonTop + (buttonHeight - h) / 2.0, actionWidth, buttonStyle);

    plainLeft_ = left;
    plainRight_ = right;
    plainButtonTop_ = buttonTop;
    plainButtonBottom_ = buttonTop + buttonHeight;
    plainConfirmLeft_ = confirmLeft;
    plainCancelLeft_ = cancelLeft;
}

/* ------------------------------------------------------------------------------------ taps ---- */

LocationPrefsHit LocationPreferenceDialog::OnTap(double x, double y) {
    LocationPrefsHit hit;
    if (!open_) {
        return hit;
    }

    if (mode_ == Mode::NameEdit || mode_ == Mode::DeleteConfirm) {
        const bool inButtons = y >= plainButtonTop_ && y <= plainButtonBottom_;
        const double actionWidth = 110.0 * density_;
        if (inButtons && x >= plainConfirmLeft_ && x <= plainConfirmLeft_ + actionWidth) {
            if (mode_ == Mode::NameEdit) {
                /*
                 * kt:205-215 — typing the city back, or clearing the field, stores "" rather than
                 * the city, so a later geocode can still move the name. Anything else is stored.
                 */
                if (nameDraft_ != place_ && nameDraft_ != customName_) {
                    draftName_ = nameDraft_;
                    prefsChanged_ = true;
                } else if ((nameDraft_ == place_ || nameDraft_.empty()) && !customName_.empty()) {
                    draftName_.clear();
                    prefsChanged_ = true;
                }
                mode_ = Mode::Prefs;
                BuildModal();
                hit.action = LocationPrefsAction::Redraw;
                return hit;
            }
            // kt:577-581 — Confirm closes both dialogs and deletes.
            hit.action = LocationPrefsAction::Delete;
            open_ = false;
            return hit;
        }
        if (inButtons && x >= plainCancelLeft_ && x <= plainCancelLeft_ + actionWidth) {
            mode_ = mode_ == Mode::NameEdit ? Mode::Prefs : Mode::Rows;
            BuildModal();
            hit.action = LocationPrefsAction::Redraw;
            return hit;
        }
        // A tap outside the card dismisses the sub-dialog, not the whole stack.
        if (x < plainLeft_ || x > plainRight_) {
            mode_ = mode_ == Mode::NameEdit ? Mode::Prefs : Mode::Rows;
            BuildModal();
            hit.action = LocationPrefsAction::Redraw;
            return hit;
        }
        hit.action = LocationPrefsAction::Redraw;
        return hit;
    }

    const ModalList::Hit modalHit = modal_.HitTest(x, y);
    switch (mode_) {
        case Mode::Rows:
            return OnRowsTap(modalHit);
        case Mode::ServicePicker:
            if (modalHit.kind == ModalList::HitKind::Entry &&
                modalHit.index >= 0 &&
                modalHit.index < static_cast<int>(modal_.Entries().size())) {
                // kt:108-111 — writes the GLOBAL setting, then closes the whole dialog.
                hit.action = LocationPrefsAction::SetLocationService;
                hit.sourceId = modal_.Entries()[static_cast<size_t>(modalHit.index)].id;
                open_ = false;
                return hit;
            }
            if (modalHit.kind != ModalList::HitKind::None) {
                mode_ = Mode::Rows;
                BuildModal();
                hit.action = LocationPrefsAction::Redraw;
            }
            return hit;
        case Mode::Prefs:
            return OnPrefsTap(modalHit);
        case Mode::Models:
            return OnModelsTap(modalHit);
        default:
            return hit;
    }
}

LocationPrefsHit LocationPreferenceDialog::OnRowsTap(const ModalList::Hit& modalHit) {
    LocationPrefsHit hit;
    if (modalHit.kind == ModalList::HitKind::Scrim) {
        hit.action = LocationPrefsAction::Dismiss;
        open_ = false;
        return hit;
    }
    if (modalHit.kind == ModalList::HitKind::Button) {
        const bool deleteFirst = canDelete_;
        if (deleteFirst && modalHit.index == 0) {
            mode_ = Mode::DeleteConfirm;
            BuildDeleteConfirm();
            hit.action = LocationPrefsAction::Redraw;
            return hit;
        }
        hit.action = LocationPrefsAction::Dismiss;
        open_ = false;
        return hit;
    }
    if (modalHit.kind != ModalList::HitKind::Entry || modalHit.index < 0 ||
        modalHit.index >= static_cast<int>(modal_.Entries().size())) {
        return hit;
    }
    const std::string id = modal_.Entries()[static_cast<size_t>(modalHit.index)].id;
    if (id == "service") {
        mode_ = Mode::ServicePicker;
        modal_.ResetScroll();
        BuildModal();
        hit.action = LocationPrefsAction::Redraw;
    } else if (id == "sources") {
        hit.action = LocationPrefsAction::OpenSources;
    } else if (id == "prefs") {
        draftName_ = customName_;
        prefsChanged_ = false;
        ResetModelDraft();
        mode_ = Mode::Prefs;
        modal_.ResetScroll();
        BuildModal();
        hit.action = LocationPrefsAction::Redraw;
    } else if (id == "global") {
        // kt:143-144 — the settings screen opens and the dialog closes behind it.
        hit.action = LocationPrefsAction::OpenMainScreenSettings;
        open_ = false;
    }
    return hit;
}

LocationPrefsHit LocationPreferenceDialog::OnPrefsTap(const ModalList::Hit& modalHit) {
    LocationPrefsHit hit;
    if (modalHit.kind == ModalList::HitKind::Scrim) {
        mode_ = Mode::Rows;
        BuildModal();
        hit.action = LocationPrefsAction::Redraw;
        return hit;
    }
    if (modalHit.kind == ModalList::HitKind::Button) {
        if (modalHit.index == 1 && prefsChanged_) {
            hit.action = LocationPrefsAction::SavePrefs;
            hit.customName = draftName_;
            hit.weatherModels = DraftModelValue();
            // The dialog stays open on Save, as the Compose one does — only Close leaves it.
            customName_ = draftName_;
            weatherModels_ = hit.weatherModels;
            prefsChanged_ = false;
            mode_ = Mode::Rows;
            BuildModal();
            return hit;
        }
        mode_ = Mode::Rows;
        BuildModal();
        hit.action = LocationPrefsAction::Redraw;
        return hit;
    }
    if (modalHit.kind != ModalList::HitKind::Entry || modalHit.index < 0 ||
        modalHit.index >= static_cast<int>(modal_.Entries().size())) {
        return hit;
    }
    const std::string id = modal_.Entries()[static_cast<size_t>(modalHit.index)].id;
    if (id == "name") {
        BuildNameEdit();
        mode_ = Mode::NameEdit;
        hit.action = LocationPrefsAction::OpenTextEntry;
        hit.customName = nameDraft_;
    } else if (id == "models") {
        mode_ = Mode::Models;
        modal_.ResetScroll();
        BuildModal();
        hit.action = LocationPrefsAction::Redraw;
    }
    return hit;
}

LocationPrefsHit LocationPreferenceDialog::OnModelsTap(const ModalList::Hit& modalHit) {
    LocationPrefsHit hit;
    // kt:911-934 — Cancel and dismiss both restore the stored selection.
    if (modalHit.kind == ModalList::HitKind::Scrim ||
        (modalHit.kind == ModalList::HitKind::Button && modalHit.index == 0)) {
        ResetModelDraft();
        mode_ = Mode::Prefs;
        BuildModal();
        hit.action = LocationPrefsAction::Redraw;
        return hit;
    }
    if (modalHit.kind == ModalList::HitKind::Button) {
        // kt:889-897 — Confirm only saves when something actually changed.
        if (modelsChanged_) {
            prefsChanged_ = true;
        }
        mode_ = Mode::Prefs;
        BuildModal();
        hit.action = LocationPrefsAction::Redraw;
        return hit;
    }
    if (modalHit.kind == ModalList::HitKind::Entry && modalHit.index >= 0 &&
        modalHit.index < static_cast<int>(draftModels_.size())) {
        const size_t index = static_cast<size_t>(modalHit.index);
        ToggleModel(index, !draftModels_[index]);
        BuildModal();
        hit.action = LocationPrefsAction::Redraw;
    }
    return hit;
}

}  // namespace breezy
