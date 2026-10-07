#include "view/WeatherSourcesDialog.h"

#include <algorithm>
#include <cstdio>

namespace breezy {
namespace {

/**
 * The dialog's rows, in the Kotlin's order (LocationPreferences.kt:514-840).
 *
 * Reverse geocoding is appended conditionally — kt:800, the address-lookup row exists only for a
 * current position — which is why it is not in this array.
 */
constexpr SourceFeature kFeatureRows[] = {
    SourceFeature::Forecast, SourceFeature::Current,  SourceFeature::AirQuality,
    SourceFeature::Pollen,   SourceFeature::Minutely, SourceFeature::Alert,
    SourceFeature::Normals};

constexpr size_t kFeatureRowCount = sizeof(kFeatureRows) / sizeof(kFeatureRows[0]);

std::string Lowercased(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    }
    return text;
}

}  // namespace

void WeatherSourcesDialog::Open(const SourceLocation& location, std::string place,
                                const LocationSources& initial) {
    target_ = location;
    place_ = std::move(place);
    selection_ = initial;
    mode_ = Mode::Rows;
    open_ = true;
    modal_.ResetScroll();
    duplicate_ = false;
    BuildModal();
}

void WeatherSourcesDialog::OpenForAdd(const SourceLocation& location, std::string place,
                                      const LocationSources& initial) {
    updateIndex_ = -1;
    Open(location, std::move(place), initial);
    // Opening on a location the list already holds is itself a duplicate, which is what stops a
    // second copy being added without changing anything.
    duplicate_ =
        std::find(savedIds_.begin(), savedIds_.end(), FormattedId()) != savedIds_.end();
    BuildModal();
}

void WeatherSourcesDialog::OpenForUpdate(int index, const SourceLocation& location,
                                         std::string place, const LocationSources& current) {
    updateIndex_ = index;
    Open(location, std::move(place), current);
    // The row's OWN id is in the saved list, so an unchanged forecast source must not read as a
    // duplicate — only a change to one another row already uses can (kt:544-554).
}

std::string WeatherSourcesDialog::SummaryFor(SourceFeature feature) const {
    const std::string& selected = selection_.For(feature);
    if (selected.empty()) {
        // kt:589-595 — the current-weather row says "Forecast" where the others say "None".
        return feature == SourceFeature::Current ? "Forecast" : "None";
    }
    const SourceInfo* source = GetSource(selected);
    // strings.xml `settings_weather_source_unavailable` = "%s (unavailable)".
    return source != nullptr ? source->name : selected + " (unavailable)";
}

std::string WeatherSourcesDialog::FormattedId() const {
    /*
     * Location.formattedId is CURRENT_POSITION for the current-position entry (Location.kt), not
     * the coordinate form — its coordinates are 0,0 until the first fix, and they change every time
     * the user moves. A fixed id is also what makes "there can only ever be one" true, and what the
     * duplicate check keys off.
     */
    if (target_.isCurrentPosition) {
        return "current-position";
    }
    char coordinates[64] = {};
    std::snprintf(coordinates, sizeof(coordinates), "%.4f,%.4f", target_.latitude,
                  target_.longitude);
    // The forecast source and the rounded coordinates, so two locations that differ only by
    // forecast source are two entries, as they are on Android.
    return selection_.forecast + "-" + coordinates;
}

std::string WeatherSourcesDialog::EncodeSources() const {
    return selection_.forecast + "," + selection_.current + "," + selection_.airQuality + "," +
           selection_.pollen + "," + selection_.minutely + "," + selection_.alert + "," +
           selection_.normals + "," + selection_.reverseGeocoding;
}

std::string WeatherSourcesDialog::EncodeLocation() const {
    char coordinates[64] = {};
    std::snprintf(coordinates, sizeof(coordinates), "%.6f|%.6f", target_.latitude,
                  target_.longitude);
    /*
     * `admin2` and `countryCode` travel with the rest.
     *
     * They used to be dropped here, on the reasoning that the shell reads neither and the sources
     * they would have decided are already chosen. That holds only until the row is swiped again:
     * `locationDetails` hands this location straight back to the catalogue, and a saved row that
     * arrived through the search then presented an empty country and an empty admin2. The Atmo
     * regional predicates test admin2 NAMES, so for a French location they could only
     * under-recommend, and every countryCode predicate saw "" for a searched location.
     *
     * A '|' cannot occur in a place name out of the geocoder, so a split on it is unambiguous.
     */
    return FormattedId() + "|" + place_ + "|" + target_.region + "|" + target_.admin2 + "|" +
           target_.countryCode + "|" + coordinates + "|" + EncodeSources() + "|" +
           (target_.isCurrentPosition ? "1" : "0");
}

void WeatherSourcesDialog::BuildRows() {
    // strings.xml `settings_weather_sources`.
    modal_.SetTitle("Weather sources");
    // kt:495-513 — a current position with no fix yet cannot be matched to national sources.
    modal_.SetNote(target_.isCurrentPosition && target_.latitude == 0.0 &&
                           target_.longitude == 0.0
                       ? "More weather sources may become available for your location after the "
                         "first refresh."
                       : "");
    // kt:558-565 — the duplicate warning sits directly under the forecast row; a flat list has
    // nowhere to put it but above.
    modal_.SetWarning(duplicate_
                          ? "Location list already contains this location with this forecast source."
                          : "");

    std::vector<ModalEntry> entries;
    for (SourceFeature feature : kFeatureRows) {
        ModalEntry entry;
        entry.label = SourceFeatureName(feature);
        entry.sub = SummaryFor(feature);
        entries.push_back(std::move(entry));
    }
    if (target_.isCurrentPosition) {
        // kt:800 — the address-lookup row exists only for a current position.
        ModalEntry entry;
        entry.label = SourceFeatureName(SourceFeature::ReverseGeocoding);
        entry.sub = SummaryFor(SourceFeature::ReverseGeocoding);
        entries.push_back(std::move(entry));
    }
    modal_.SetEntries(std::move(entries));
    // kt:843-907 — Cancel then Save, trailing; Save is disabled while the choice would duplicate.
    modal_.SetButtons({"CANCEL", "Save"}, duplicate_ ? 1 : -1);
}

void WeatherSourcesDialog::BuildPicker() {
    const SourceFeature feature = openFeature_;
    // The picker's title is the ROW's title, at headlineSmall (ListPreference.kt:244-250).
    modal_.SetTitle(SourceFeatureName(feature));
    modal_.SetNote("");
    modal_.SetWarning("");

    const std::string& selected = selection_.For(feature);
    const std::vector<SourceGroup> groups = GetCompatibleSources(feature, target_, selected);

    std::vector<ModalEntry> entries;
    /*
     * The ungrouped bucket first — the Kotlin's `put(null, ...)` (kt:569-597), for which
     * ListPreferenceWithGroupsView emits no heading. It holds up to two pseudo-entries: the
     * SELECTED source when it is no longer compatible, and "None" (or "Forecast" on the
     * current-weather row). The forecast row gets neither: a location must have a forecast source.
     */
    bool compatible = false;
    for (const SourceGroup& group : groups) {
        for (const SourceInfo* source : group.sources) {
            if (source->id == selected) {
                compatible = true;
            }
        }
    }
    if (!selected.empty() && !compatible) {
        ModalEntry entry;
        entry.id = selected;
        entry.label = selected + " (unavailable)";
        entry.enabled = false;
        entry.selected = true;
        entries.push_back(std::move(entry));
    }
    if (feature != SourceFeature::Forecast) {
        ModalEntry entry;
        entry.label = feature == SourceFeature::Current ? "Forecast" : "None";
        entry.selected = selected.empty();
        entries.push_back(std::move(entry));
    }

    for (const SourceGroup& group : groups) {
        if (group.sources.empty()) {
            continue;
        }
        ModalEntry heading;
        heading.label = group.title;
        heading.header = true;
        entries.push_back(std::move(heading));
        for (const SourceInfo* source : group.sources) {
            ModalEntry entry;
            entry.id = source->id;
            // `SourceExtensions.getName` RENAMES an unconfigured source rather than only dimming
            // it: "%s (not configured)".
            entry.label = source->DisplayName();
            // kt:341-344 — an unconfigured ConfigurableSource, or one that does not support this
            // location, is listed but not selectable.
            entry.enabled = source->configured && source->Supports(target_, feature);
            entry.selected = source->id == selected;
            entries.push_back(std::move(entry));
        }
    }
    modal_.SetEntries(std::move(entries));
    // The picker's only button is a Cancel; picking a row applies and closes (kt:301-311).
    modal_.SetButtons({"CANCEL"});
}

void WeatherSourcesDialog::BuildModal() {
    if (mode_ == Mode::Rows) {
        BuildRows();
    } else {
        BuildPicker();
    }
}

void WeatherSourcesDialog::Draw(OH_Drawing_Canvas* canvas) {
    if (!open_) {
        return;
    }
    modal_.SetGeometry(width_, height_, density_, dark_, 0.9);
    modal_.Draw(canvas);
}

/**
 * A picked entry, in the feature picker.
 *
 * CLAUDE.md's source scope: "Selecting a source other than Open-Meteo shows the same 'not yet
 * ported' placeholder used for unported screens — never hidden, never a crash." So an unimplemented
 * pick is answered with the placeholder and the selection is left alone. Clearing a feature
 * ("None") is always allowed.
 */
SourcesDialogHit WeatherSourcesDialog::PickSource(const ModalEntry& entry) {
    SourcesDialogHit hit;
    hit.action = SourcesDialogAction::Redraw;
    const SourceFeature feature = openFeature_;

    if (!entry.id.empty()) {
        const SourceInfo* source = GetSource(entry.id);
        if (source == nullptr || !source->implemented) {
            mode_ = Mode::Rows;
            BuildModal();
            hit.action = SourcesDialogAction::Navigate;
            hit.destination = (source != nullptr ? source->name : entry.id) + " " +
                              Lowercased(SourceFeatureName(feature));
            return hit;
        }
    }

    if (feature == SourceFeature::Forecast) {
        // kt:544-554 — only a CHANGE of forecast source can create a duplicate. The row's own id is
        // in the saved list, so an unchanged pick must not trip it.
        const std::string previous = selection_.forecast;
        selection_.Set(feature, entry.id);
        duplicate_ = entry.id != previous &&
                     std::find(savedIds_.begin(), savedIds_.end(), FormattedId()) !=
                         savedIds_.end();
    } else {
        selection_.Set(feature, entry.id);
    }
    mode_ = Mode::Rows;
    BuildModal();
    return hit;
}

SourcesDialogHit WeatherSourcesDialog::OnTap(double x, double y) {
    SourcesDialogHit hit;
    hit.action = SourcesDialogAction::Redraw;
    const ModalList::Hit tap = modal_.HitTest(x, y);

    if (tap.kind == ModalList::HitKind::Scrim) {
        // `onDismissRequest`: the picker returns to the rows, the rows close the dialog.
        if (mode_ == Mode::Picker) {
            mode_ = Mode::Rows;
            BuildModal();
            return hit;
        }
        open_ = false;
        hit.action = SourcesDialogAction::Dismiss;
        return hit;
    }
    if (tap.kind == ModalList::HitKind::None) {
        hit.action = SourcesDialogAction::None;
        return hit;
    }

    if (mode_ == Mode::Picker) {
        if (tap.kind == ModalList::HitKind::Button) {
            mode_ = Mode::Rows;
            BuildModal();
            return hit;
        }
        return PickSource(modal_.Entries()[static_cast<size_t>(tap.index)]);
    }

    if (tap.kind == ModalList::HitKind::Button) {
        if (tap.index == 0) {
            // Cancel — nothing is saved and, at the entry point that was ADDING a location, nothing
            // is added (kt:439-441).
            open_ = false;
            hit.action = SourcesDialogAction::Dismiss;
            return hit;
        }
        if (duplicate_) {
            return hit;  // kt:845 disables Save while the choice would duplicate.
        }
        open_ = false;
        if (updateIndex_ >= 0) {
            hit.action = SourcesDialogAction::Update;
            hit.index = updateIndex_;
            hit.sources = EncodeSources();
        } else {
            hit.action = SourcesDialogAction::Add;
            hit.encoded = EncodeLocation();
        }
        return hit;
    }

    // A row opens its picker.
    const size_t index = static_cast<size_t>(tap.index);
    openFeature_ =
        index < kFeatureRowCount ? kFeatureRows[index] : SourceFeature::ReverseGeocoding;
    mode_ = Mode::Picker;
    modal_.ResetScroll();
    BuildModal();
    return hit;
}

}  // namespace breezy
