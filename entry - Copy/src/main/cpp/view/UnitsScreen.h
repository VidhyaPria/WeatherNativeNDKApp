/*
 * Screen 3b — Units. Port of UnitSettingsScreen.kt (:44-264).
 *
 * FIVE list preferences in ONE card group, no section headers: Temperature, Precipitation, Speed,
 * Distance, Pressure. `isFirst = true` on temperature (kt:90) and `isLast = true` on pressure
 * (kt:248), with `smallSeparatorItem()` between each pair (kt:104, :156, :191, :226).
 *
 * Every list is `arrayOf("auto") + <Unit>.entries` — the regional preference first, then the units
 * themselves. The row summary is the name of whatever is selected, and "auto" reads
 * "Regional preference (<what it resolves to>)" (kt:72-81).
 *
 * All five rows are REAL. The write is a round trip, as Appearance's is: C++ owns no files in this
 * port, so a choice goes to the shell as `<key>|<value>`, the shell writes it to the same
 * preferences bag ArkUIApp uses and pushes the values straight back. C++ never applies the change
 * itself, so the stored value and the drawn one cannot diverge.
 *
 * The single-choice dialog is drawn here rather than in its own file, for the same reason
 * AppearanceScreen draws its own: it is the same radio list for all five rows and nothing else
 * opens it.
 */

#ifndef BREEZY_VIEW_UNITSSCREEN_H
#define BREEZY_VIEW_UNITSSCREEN_H

#include <native_drawing/drawing_canvas.h>

#include <string>
#include <vector>

#include "../common/Units.h"

namespace breezy {

enum class UnitsAction {
    None,
    /** The back arrow. */
    Close,
    /** A row that is not ported; `destination` carries its name. Only the About action, here. */
    Navigate,
    /** A unit was chosen: `key` is e.g. "pressure_unit", `value` a unit id or "auto". */
    Changed
};

struct UnitsHit {
    UnitsAction action = UnitsAction::None;
    std::string destination;
    std::string key;
    std::string value;
};

class UnitsScreen {
public:
    void SetGeometry(double widthPx, double heightPx, float density, double topInsetPx,
                     double bottomInsetPx);
    void SetDarkMode(bool dark) { dark_ = dark; }
    /** Re-reads the five overrides out of common/Units and re-lays out the summaries. */
    void SyncValues();

    void Draw(OH_Drawing_Canvas* canvas);
    UnitsHit HitTest(double x, double y);
    bool ScrollBy(double dy);
    void ResetScroll() { scroll_ = 0.0; }
    /** True while the choice dialog is up — it takes every touch. */
    bool DialogOpen() const { return dialogOpen_; }
    void CloseDialog() { dialogOpen_ = false; }

private:
    struct Item {
        UnitFamily family = UnitFamily::Temperature;
        std::string title;
        std::string summary;
        bool isFirst = false;
        bool isLast = false;
        double top = 0.0;
        double height = 0.0;
    };

    struct Option {
        std::string value;
        std::string name;
    };

    void Layout();
    std::string RegionalName(UnitFamily family) const;
    std::vector<Option> OptionsFor(UnitFamily family) const;
    std::string SummaryFor(UnitFamily family) const;
    double ListTop() const;
    double MaxScroll() const;
    void DrawDialog(OH_Drawing_Canvas* canvas);
    UnitsHit HitDialog(double x, double y);

    std::vector<Item> items_;
    double laidOutWidth_ = 0.0;
    float laidOutDensity_ = 0.0f;
    /** Re-laid out when a value changes, because a summary's height can change with it. */
    std::string laidOutValues_;

    double width_ = 0.0;
    double height_ = 0.0;
    float density_ = 1.0f;
    double topInset_ = 0.0;
    double bottomInset_ = 0.0;
    double scroll_ = 0.0;
    bool dark_ = false;

    bool dialogOpen_ = false;
    UnitFamily dialogFamily_ = UnitFamily::Temperature;
    /** The dialog's option rows, in surface coordinates, filled by DrawDialog for the hit test. */
    double dialogLeft_ = 0.0;
    double dialogRight_ = 0.0;
    double dialogOptionTop_ = 0.0;
    double dialogOptionHeight_ = 0.0;
    double dialogCancelTop_ = 0.0;
    double dialogCancelBottom_ = 0.0;
};

}  // namespace breezy

#endif  // BREEZY_VIEW_UNITSSCREEN_H
