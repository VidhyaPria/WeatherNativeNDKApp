/*
 * The main screen's "Edit this location" dialog.
 *
 * MainActivity.kt:481-607 wraps `LocationPreference` (LocationPreferences.kt:75-350) in an
 * AlertDialogNoPadding with a Close button, a Delete button (only when more than one location is
 * saved, kt:530) and a delete-confirmation dialog behind Delete. `onEditIconClicked` (kt:897-904)
 * opens it, refusing while a refresh is in flight.
 *
 * NOT the same composable as the location list's swipe-left dialog. That one is
 * `SecondarySourcesPreference` (kt:352-916) — WeatherSourcesDialog here — which this dialog OPENS
 * as one of its four rows. Both ports had the inner dialog and neither had the outer one, so the
 * edit icon led to the placeholder: a gap that hid precisely because the thing it needed existed.
 *
 * The four rows (kt:90-145):
 *   Location service       current position only; a picker over locationSourceList, writing the
 *                          GLOBAL `location_service` setting and closing the dialog (kt:109-110)
 *   Weather sources        opens WeatherSourcesDialog (kt:113-122)
 *   Location preferences   the custom name and, for Open-Meteo, its weather models (kt:123-134)
 *   Global settings        opens Main screen settings and closes (kt:135-145)
 *
 * Like WeatherSourcesDialog this lives on the RENDERER rather than on the main screen, because a
 * dialog sits above whatever opened it and its sub-dialogs sit above IT.
 */
#ifndef BREEZY_VIEW_LOCATION_PREFERENCE_DIALOG_H
#define BREEZY_VIEW_LOCATION_PREFERENCE_DIALOG_H

#include <native_drawing/drawing_canvas.h>

#include <string>
#include <vector>

#include "data/OpenMeteoModels.h"
#include "data/SourceCatalogue.h"
#include "view/ModalList.h"

namespace breezy {

enum class LocationPrefsAction {
    None,
    /** Something repainted but nothing left the dialog. */
    Redraw,
    /** Close, the scrim, or a row that navigates away. */
    Dismiss,
    /** The Weather sources row — the caller opens WeatherSourcesDialog over this one. */
    OpenSources,
    /** Confirm on the preferences dialog; `customName` and `weatherModels` carry the new values. */
    SavePrefs,
    /** Confirm on the delete dialog. */
    Delete,
    /** Global settings — the caller opens the Main screen settings screen and dismisses. */
    OpenMainScreenSettings,
    /** The location-service picker chose `sourceId`; the caller writes the setting and dismisses. */
    SetLocationService,
    /** The custom-name field wants the shell's TEXT session. */
    OpenTextEntry
};

struct LocationPrefsHit {
    LocationPrefsAction action = LocationPrefsAction::None;
    std::string customName;
    std::string weatherModels;
    std::string sourceId;
};

class LocationPreferenceDialog {
public:
    /**
     * @param index          the row this belongs to, handed back on Save and Delete
     * @param place          `cityAndDistrict` — what the name field seeds with (kt:200-204)
     * @param canDelete      `locationListSize() > 1` (kt:530)
     */
    void Open(int index, const SourceLocation& location, const LocationSources& sources,
              std::string place, std::string customName, std::string weatherModels, bool canDelete);

    void Close() { open_ = false; }
    bool IsOpen() const { return open_; }
    int Index() const { return index_; }

    /** Whether the sub-dialog on top wants the shell's text session (the name field). */
    bool TextEntryOpen() const { return open_ && mode_ == Mode::NameEdit; }
    /** What the shell has collected for the name field. Returns true if a repaint is needed. */
    bool SetEnteredText(const std::string& text);

    /** The location-service id the picker should show as selected. */
    void SetLocationService(std::string id) { locationService_ = std::move(id); }

    void SetGeometry(double widthPx, double heightPx, double density, bool dark) {
        width_ = widthPx;
        height_ = heightPx;
        density_ = density;
        dark_ = dark;
    }

    void Draw(OH_Drawing_Canvas* canvas);
    LocationPrefsHit OnTap(double x, double y);
    void ScrollBy(double dy) { modal_.ScrollBy(dy); }

private:
    /** Which surface is on top. Each is one AlertDialog on Android. */
    enum class Mode { Rows, ServicePicker, Prefs, Models, NameEdit, DeleteConfirm };

    void BuildModal();
    void BuildRows();
    void BuildServicePicker();
    void BuildPrefs();
    void BuildModels();
    void BuildNameEdit();
    void BuildDeleteConfirm();

    LocationPrefsHit OnRowsTap(const ModalList::Hit& hit);
    LocationPrefsHit OnPrefsTap(const ModalList::Hit& hit);
    LocationPrefsHit OnModelsTap(const ModalList::Hit& hit);

    /** kt:854-879 — see the .cpp for why turning one on turns every other one off. */
    void ToggleModel(size_t index, bool checked);
    /** The draft's enabled ids, comma-joined; `best_match` when none (kt:894). */
    std::string DraftModelValue() const;
    void ResetModelDraft();

    /**
     * `sourcesWithPreferencesScreen` (SourceManager.kt:409-432) reduced to the one source in this
     * port that has a per-location screen.
     *
     * Two details matter and both were got wrong first time:
     *   - it walks ALL SEVEN source slots, not just the forecast one;
     *   - Open-Meteo's own `hasPreferencesScreen` (OpenMeteoService.kt:755-762) answers true only
     *     for FORECAST, CURRENT and MINUTELY — being the pollen or air-quality source does not
     *     earn it a section.
     * And kt:424 reads `it.first ?: location.forecastSource`, so a slot left empty is the forecast
     * source, which is exactly what "" means in both ports' models.
     */
    bool HasSourcePreferences() const;
    bool ShowsPreferencesRow() const { return !target_.isCurrentPosition || HasSourcePreferences(); }

    bool open_ = false;
    Mode mode_ = Mode::Rows;
    int index_ = -1;
    ModalList modal_;
    SourceLocation target_;
    LocationSources sources_;
    std::string place_;
    std::string customName_;
    std::string weatherModels_;
    std::string locationService_ = "native";
    bool canDelete_ = false;

    /** The preferences dialog's working copies, discarded unless Confirm is pressed. */
    std::string draftName_;
    std::vector<bool> draftModels_;
    bool prefsChanged_ = false;
    bool modelsChanged_ = false;
    /** What the shell's text session has collected so far. */
    std::string nameDraft_;

    /**
     * Hit geometry for the two surfaces that are NOT ModalLists — the name field and the delete
     * confirmation. Both are a title, a body and two trailing buttons, so the tap test needs the
     * card's edges and the two button boxes, recorded by Draw the frame before.
     */
    double plainLeft_ = 0.0;
    double plainRight_ = 0.0;
    double plainButtonTop_ = 0.0;
    double plainButtonBottom_ = 0.0;
    double plainConfirmLeft_ = 0.0;
    double plainCancelLeft_ = 0.0;

    double width_ = 0.0;
    double height_ = 0.0;
    double density_ = 1.0;
    bool dark_ = false;
};

}  // namespace breezy

#endif  // BREEZY_VIEW_LOCATION_PREFERENCE_DIALOG_H
