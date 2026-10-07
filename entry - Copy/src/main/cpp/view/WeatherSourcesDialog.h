/*
 * The per-location weather-sources dialog — SecondarySourcesPreference (LocationPreferences.kt:351-916).
 *
 * Reached from THREE places on Android, and the port wires all three to this one object:
 *   swipe a row left        LocationItemTouchCallback.kt:96      (screen 2)
 *   the my-location FAB     ManagementFragment.kt:240-244        (not ported: no current position)
 *   picking a search result SearchActivity.kt:212-223            (screen 2b)
 *
 * It therefore lives on the RENDERER rather than on either screen: a dialog sits above whichever
 * screen opened it, and the two entry points differ only in what Save means — ADD a location the
 * list does not have yet, or UPDATE one it does (kt:459-477 picks between the two branches).
 *
 * Eight rows, one per SourceFeature, each a title plus the selected source's name, opening a
 * grouped radio picker. The catalogue behind them is data/SourceCatalogue.h, which is the whole of
 * SourceManager's metadata — CLAUDE.md scopes down the source CLIENTS, not the source list.
 *
 * What is deliberately NOT here, matching ArkUIApp's dialog and for the same reasons:
 *   the freenet disclaimer card   inside `if (BuildConfig.FLAVOR == "freenet")` (kt:454-494)
 *   the "Learn more" link dialog  only reachable from that card (kt:910-915)
 *   `cityId` / `needsGeocodeRefresh` reset on Save (kt:850, 859-860) — neither port's location
 *                                 model carries those fields
 */
#ifndef BREEZY_VIEW_WEATHER_SOURCES_DIALOG_H
#define BREEZY_VIEW_WEATHER_SOURCES_DIALOG_H

#include <native_drawing/drawing_canvas.h>

#include <string>
#include <vector>

#include "data/SourceCatalogue.h"
#include "view/ModalList.h"

namespace breezy {

enum class SourcesDialogAction {
    None,
    /** Something repainted but nothing left the dialog. */
    Redraw,
    /** A source this port has no client for — CLAUDE.md's placeholder rule. */
    Navigate,
    /** Save on a dialog opened to ADD; `encoded` is the whole location. */
    Add,
    /** Save on a dialog opened to UPDATE; `index` is the row and `sources` the eight ids. */
    Update,
    /** Cancel, or a tap on the scrim. */
    Dismiss
};

struct SourcesDialogHit {
    SourcesDialogAction action = SourcesDialogAction::None;
    std::string encoded;
    std::string sources;
    std::string destination;
    int index = -1;
};

class WeatherSourcesDialog {
public:
    /**
     * Opens the dialog for a location that is NOT saved yet — a picked search hit.
     *
     * `initial` is Location.applyDefaultPreset's answer, which is what Android hands the dialog:
     * every Location it constructs is preset, so the rows open on real sources rather than eight
     * blanks.
     */
    void OpenForAdd(const SourceLocation& location, std::string place,
                    const LocationSources& initial);

    /** Opens it for a location the list already holds; Save updates row `index` in place. */
    void OpenForUpdate(int index, const SourceLocation& location, std::string place,
                       const LocationSources& current);

    void Close() { open_ = false; }
    bool IsOpen() const { return open_; }

    /** The saved formattedIds, for the forecast row's duplicate warning (kt:544-554). */
    void SetSavedIds(std::vector<std::string> ids) { savedIds_ = std::move(ids); }

    void SetGeometry(double widthPx, double heightPx, double density, bool dark) {
        width_ = widthPx;
        height_ = heightPx;
        density_ = density;
        dark_ = dark;
    }

    void Draw(OH_Drawing_Canvas* canvas);
    SourcesDialogHit OnTap(double x, double y);
    void ScrollBy(double dy) { modal_.ScrollBy(dy); }

private:
    /** Which of the two lists is showing: the eight rows, or one row's picker. */
    enum class Mode { Rows, Picker };

    void Open(const SourceLocation& location, std::string place, const LocationSources& initial);
    void BuildModal();
    void BuildRows();
    void BuildPicker();
    SourcesDialogHit PickSource(const ModalEntry& entry);
    std::string SummaryFor(SourceFeature feature) const;
    /** `<forecastSource>-<lat>,<lon>`, Android's Location.formattedId. */
    std::string FormattedId() const;
    std::string EncodeSources() const;
    std::string EncodeLocation() const;

    bool open_ = false;
    Mode mode_ = Mode::Rows;
    /** -1 when the dialog was opened to ADD; a row index when it was opened to UPDATE. */
    int updateIndex_ = -1;
    SourceFeature openFeature_ = SourceFeature::Forecast;
    ModalList modal_;
    SourceLocation target_;
    std::string place_;
    LocationSources selection_;
    /** kt:365 — set when the chosen forecast source would duplicate an existing location. */
    bool duplicate_ = false;
    std::vector<std::string> savedIds_;
    double width_ = 0.0;
    double height_ = 0.0;
    double density_ = 1.0;
    bool dark_ = false;
};

}  // namespace breezy

#endif  // BREEZY_VIEW_WEATHER_SOURCES_DIALOG_H
