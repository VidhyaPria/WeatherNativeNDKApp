/*
 * Screen 2b — add a location by searching for it, drawn in C++.
 *
 * The ArkUI counterpart is pages/LocationSearch.ets, ported from SearchActivity.kt's ContentView
 * (:114-359): a Surface holding [search input | divider | progress | results] under a bottom bar
 * that carries the attribution line and the Tune FAB.
 *
 * THE TEXT FIELD IS DRAWN HERE TOO — glyphs, placeholder and caret. Only the keystrokes come from
 * the shell, which attaches @ohos.inputMethod directly rather than mounting an ArkUI TextInput:
 * putting a real TextInput on this screen would place an ArkUI text component inside the very
 * screen the two ports are being weighed against.
 *
 * What is deliberately absent, matching ArkUIApp's screen and for the same reasons:
 *   the freenet disclaimer card   inside `if (BuildConfig.FLAVOR == "freenet")` (:269)
 *   the "Learn more" link dialog  only reachable from that card (:300-310)
 *
 * NOT YET, and announced as a placeholder rather than hidden:
 *   the Tune source dialog        needs the source catalogue, which this port has only in ArkTS
 *   SecondarySourcesPreference    the per-feature picker shown after a hit is chosen; picking here
 *                                 adds the location directly, which is what ArkUIApp did before its
 *                                 own dialog existed
 */
#ifndef BREEZY_VIEW_LOCATION_SEARCH_SCREEN_H
#define BREEZY_VIEW_LOCATION_SEARCH_SCREEN_H

#include <native_drawing/drawing_canvas.h>

#include <algorithm>
#include <string>
#include <vector>

#include "data/OpenMeteoGeocoding.h"
#include "data/SourceCatalogue.h"
#include "render/TextPainter.h"
#include "render/Theme.h"
#include "view/ModalList.h"

namespace breezy {

/** What a touch on the search screen asked for, once its own chooser has had its say. */
enum class LocationSearchAction {
    None,
    /**
     * A result row was chosen. The weather-sources dialog opens over the screen next, and it is the
     * renderer's rather than this screen's — the location list opens the same one on a left swipe.
     */
    Pick,
    /** A source this port has no client for — CLAUDE.md's placeholder rule. */
    Navigate,
    /** Something repainted but nothing left the screen. */
    Redraw
};

struct LocationSearchHit {
    LocationSearchAction action = LocationSearchAction::None;
    /** Set for Pick: the hit, ready for the catalogue and for saving. */
    SourceLocation location;
    std::string place;
    std::string destination;
};

class LocationSearchScreen {
public:
    /** The live contents of the field — `var text by rememberSaveable` (:122). */
    void SetQuery(std::string query) { query_ = std::move(query); }
    const std::string& Query() const { return query_; }

    /**
     * The hits, and the term they belong to.
     *
     * `latestTextSearch` (:123) is kept apart from `query_` on purpose: the empty-state message
     * names the term that was actually searched for, not whatever the field says now.
     */
    void SetResults(std::vector<GeocodingResult> results, std::string searched, bool ok);
    const std::vector<GeocodingResult>& Results() const { return results_; }

    /** `LoadableLocationStatus.Loading` (:192-194) — the linear progress bar. */
    void SetLoading(bool loading) { loading_ = loading; }
    bool Loading() const { return loading_; }

    /**
     * `keyboardInsetPx` is how far the IME reaches up the window.
     *
     * The bottom bar sits above whichever of the navigation bar and the keyboard is taller — the
     * keyboard covers the navigation bar when it is up, so they do not add.
     */
    void SetGeometry(double widthPx, double heightPx, double density, double topInsetPx,
                     double bottomInsetPx, double keyboardInsetPx) {
        width_ = widthPx;
        height_ = heightPx;
        density_ = density;
        topInset_ = topInsetPx;
        bottomInset_ = std::max(bottomInsetPx, keyboardInsetPx);
    }

    void SetDarkMode(bool dark) { dark_ = dark; }

    /** Clears everything a fresh open must not inherit from the last one. */
    void Reset();

    /** NOT const: the dialog over this screen is laid out against the surface as it is painted. */
    void Draw(OH_Drawing_Canvas* canvas);

    /**
     * Routes a tap. NOT const: the dialogs this screen owns handle their own taps and only surface
     * what has to leave the screen.
     */
    LocationSearchHit OnTap(double x, double y);

    /** True while the source chooser covers the screen — the scroll then belongs to it. */
    bool DialogOpen() const { return sourceDialogOpen_; }

    void ScrollBy(double dy);

    /** The field's rectangle in surface pixels, which the IME wants as its cursor anchor. */
    double FieldTop() const { return topInset_; }
    double FieldHeight() const { return kInputFieldHeight * density_; }

private:
    void BuildSearchSourceEntries();
    LocationSearchHit OnDialogTap(double x, double y);

    /** SearchBarDefaults.InputFieldHeight — the Material3 search bar's fixed 56 dp field. */
    static constexpr double kInputFieldHeight = 56.0;
    /** Material3SearchBarInputField's textStyle: titleMedium at an explicit 18 sp. */
    static constexpr double kInputTextSize = 18.0;
    /** BottomAppBar's height. */
    static constexpr double kBottomBarHeight = 80.0;
    /** Material3 FAB: 56 dp with a 24 dp icon. */
    static constexpr double kFabSize = 56.0;
    /** LinearProgressIndicator's track height. */
    static constexpr double kProgressHeight = 4.0;
    /** `LazyColumn(verticalArrangement = Arrangement.spacedBy(4.dp))` (:198). */
    static constexpr double kResultSpacing = 4.0;

    /** The supporting line's style, shared by the row measure and the draw. */
    TextStyleSpec LevelsStyle() const;
    /** Row `index`'s fixed ListItem height: the 72 dp token, or 88 dp when the levels wrap. */
    double RowHeight(size_t index) const;
    double ListTop() const;
    double ListBottom() const;
    double ContentHeight() const;

    std::string query_;
    std::string searched_;
    std::vector<GeocodingResult> results_;
    /** The active location-search source; only Open-Meteo can actually answer (CLAUDE.md). */
    std::string searchSourceId_ = kDefaultLocationSearchSource;

    /**
     * openDialogLocationSearchSource (SearchActivity.kt:153) — this screen's ONLY dialog.
     *
     * The weather-sources dialog a picked hit opens is the renderer's, because the location list
     * opens the same one on a left swipe.
     */
    bool sourceDialogOpen_ = false;
    ModalList modal_;

    /** False once a search FAILED, which is what keeps the advice block off an error (:230-232). */
    bool searchOk_ = true;
    bool loading_ = false;
    double width_ = 0.0;
    double height_ = 0.0;
    double density_ = 1.0;
    double topInset_ = 0.0;
    double bottomInset_ = 0.0;
    double scroll_ = 0.0;
    bool dark_ = false;
};

}  // namespace breezy

#endif  // BREEZY_VIEW_LOCATION_SEARCH_SCREEN_H
