/*
 * Screen 3f — Weather sources. Port of WeatherSourcesSettingsScreen.kt (:71-242), and the NDK twin
 * of ArkUIApp's pages/WeatherSourcesSettings.ets.
 *
 *   General      Default source for new locations
 *   <source>     that source's own preferences, one section each, 40 of them
 *
 * References captured before either port was written: ref_screen3f_sources_top.png,
 * ref_screen3f_sources_bottom.png and ref_screen3f_instance_dialog.png.
 *
 * The Kotlin is a loop, not a layout: it asks every ConfigurableSource for `getPreferences(context)`
 * and renders whatever comes back (kt:196-233). This port has no source clients to ask, so the lists
 * live in data/SourcePreferences and the loop reads that instead. Section headers come from
 * SourceCatalogue, so they match the pickers on screens 2 and 3e exactly.
 *
 * WHAT IS REAL HERE: all of it. Every value is stored by the shell — C++ owns no files — every
 * summary reflects what is stored, and the default source actually decides which sources a location
 * added on screen 2b starts on (SearchActivityRepository.kt:101-140).
 *
 * The freenet disclaimer (kt:95-146) is absent for the same reason it is on screen 3e: the reference
 * build is not that flavour, and its capture starts straight at General.
 */

#ifndef BREEZY_VIEW_WEATHERSOURCESSCREEN_H
#define BREEZY_VIEW_WEATHERSOURCESSCREEN_H

#include <native_drawing/drawing_canvas.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace breezy {

enum class WeatherSourcesAction {
    None,
    Close,
    /** A destination that is not ported — only the About button, here. */
    Navigate,
    /** A value changed: `key` is the preferences key, `value` its new value ("" clears it). */
    Changed
};

struct WeatherSourcesHit {
    WeatherSourcesAction action = WeatherSourcesAction::None;
    std::string destination;
    std::string key;
    std::string value;
};

class WeatherSourcesScreen {
public:
    void SetGeometry(double widthPx, double heightPx, float density, double topInsetPx,
                     double bottomInsetPx);
    void SetDarkMode(bool dark) { dark_ = dark; }
    /**
     * The stored values, from the shell that owns the preferences file.
     *
     * `prefs` is `<storageKey>\t<value>\n` per SET preference — only the ones that have a value, as
     * the ArkTS port's map holds. An absent key is a row on its default.
     */
    void SetValues(const std::string& defaultForecastSource, const std::string& prefs);

    void Draw(OH_Drawing_Canvas* canvas);
    WeatherSourcesHit HitTest(double x, double y);
    bool ScrollBy(double dy);
    void ResetScroll() { scroll_ = 0.0; }
    bool DialogOpen() const { return picker_ || editing_; }
    void CloseDialog();
    /** True while a text field is open, so the shell's TEXT session should be attached. */
    bool TextEntryOpen() const { return editing_; }
    /** What the shell has collected for the open field. Returns true if a repaint is needed. */
    bool SetEnteredText(const std::string& text);

    /** Asks the shell to open (2 = TEXT) or close (0) an edit session. */
    using TextEntryHandler = void (*)(int state);
    void SetTextEntryHandler(TextEntryHandler handler) { textEntry_ = handler; }
    /**
     * Pre-fills the shell's edit buffer with the value already stored, so the first keystroke
     * appends instead of replacing. `attach()` clears the buffer, which is right for the time
     * fields and wrong for a stored API key.
     */
    using SeedHandler = std::function<void(const std::string& text)>;
    void SetSeedHandler(SeedHandler handler) { seed_ = handler; }

private:
    struct Item {
        bool header = false;
        std::string title;
        /** Rows only — recomputed on every layout from the stored values. */
        std::string summary;
        /** -1 on the General header and on the default-source row. */
        int section = -1;
        int pref = -1;
        /** The one row in General; its options are sources, not a stored value array. */
        bool defaultSource = false;
        bool isFirst = false;
        bool isLast = false;
        /** A large separator sits above every source section's header. */
        bool gapAbove = false;
        double top = 0.0;
        double height = 0.0;
    };

    struct Option {
        std::string value;
        std::string name;
        /** `enableArray` (ListPreference.kt:280-294) — listed, dimmed, and absorbs its tap. */
        bool enabled = true;
    };

    void Layout();
    double ListTop() const;
    double MaxScroll() const;
    /** '' when the key has never been set. */
    std::string StoredValue(const std::string& storageKey) const;
    std::string ListSummary(int section, int pref) const;
    std::string TextSummary(int section, int pref) const;
    /** kt:159-172 — "Automatic" then every source reporting FORECAST for a BLANK location. */
    std::vector<Option> DefaultSourceOptions() const;
    std::string DefaultSourceSummary() const;
    /** The options the open picker shows. */
    std::vector<Option> PickerOptions() const;
    std::string PickerTitle() const;
    std::string PickerSelected() const;
    /** EditTextPreference.URL_REGEX, and kt:166-168's empty-is-fine-with-a-placeholder rule. */
    bool DraftInvalid() const;

    void DrawPicker(OH_Drawing_Canvas* canvas);
    void DrawEditor(OH_Drawing_Canvas* canvas);
    WeatherSourcesHit HitPicker(double x, double y);
    WeatherSourcesHit HitEditor(double x, double y);

    std::vector<Item> items_;
    double laidOutWidth_ = 0.0;
    float laidOutDensity_ = 0.0f;
    std::string laidOutValues_;

    std::string defaultForecastSource_ = "auto";
    /** The set preferences, `storageKey` → value, in the order the shell sent them. */
    std::vector<std::pair<std::string, std::string>> prefs_;
    std::string prefsBlob_;

    double width_ = 0.0;
    double height_ = 0.0;
    float density_ = 1.0f;
    double topInset_ = 0.0;
    double bottomInset_ = 0.0;
    double scroll_ = 0.0;
    bool dark_ = false;

    /** Which row's dialog is open. -2 = none, -1 = the default-source row, else an item index. */
    int openItem_ = -2;
    bool picker_ = false;
    bool editing_ = false;
    /** The draft the field is showing; committed only on Done. */
    std::string draft_;
    TextEntryHandler textEntry_ = nullptr;
    SeedHandler seed_ = nullptr;

    double dialogLeft_ = 0.0;
    double dialogRight_ = 0.0;
    double dialogOptionTop_ = 0.0;
    double dialogOptionHeight_ = 0.0;
    double dialogButtonTop_ = 0.0;
    double dialogButtonBottom_ = 0.0;
    double dialogConfirmLeft_ = 0.0;
};

}  // namespace breezy

#endif  // BREEZY_VIEW_WEATHERSOURCESSCREEN_H
