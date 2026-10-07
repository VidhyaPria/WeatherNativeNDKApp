/*
 * Screen 3d — Notifications. Port of NotificationsSettingsScreen.kt (:55-239).
 *
 *   General   Notifications of severe weather alerts | Notifications of precipitations
 *   Forecast  Notification for the day's forecast | its time |
 *             Notification for next-day forecast | its time
 *
 * Real here: the four switches and the two times, stored under SettingsManager's own keys through
 * the shell round trip, with its per-key defaults (alert TRUE, precipitation FALSE, both forecast
 * switches FALSE, times "07:00"/"21:00" — SettingsManager.kt:148,155,381,397 and :92-93).
 *
 * NOT ported: the notification jobs themselves. CLAUDE.md skips background refresh entirely, and
 * TodayForecastNotificationJob and the alert/precipitation pushes are exactly that — the same
 * treatment the weather sources get, where the screen is ported and the client behind it is not.
 *
 * The permission banner (kt:81-107) is absent: it is gated on Android 13's POST_NOTIFICATIONS not
 * being granted, and HarmonyOS has no equivalent runtime grant to be missing.
 *
 * The App-updates section (kt:109-126) IS present. The source alone argues otherwise — it needs
 * BuildConfig.GITHUB_ORG/REPO, which come from a properties file absent from the tree
 * (build.gradle.kts:193-209) — but the reference APK was built with them and does render it, which
 * a screenshot of the device settled.
 *
 * The two General rows are disabled, and re-summarised, when the update interval is "never"
 * (kt:135-145). Background updates is not ported, nothing writes `refresh_rate`, and its default is
 * "1:30" — so the rows are live. The conditional is ported anyway, so porting that screen later
 * needs no change here.
 *
 * This is the first screen in this port with a TIME PICKER. Android calls Material 3's
 * `TimePicker(state)` — a CLOCK DIAL — which is library code and is NOT in breezy-weather-6.2.1/,
 * so every number in the dialog below was measured off screenshots of the reference APK rather
 * than read from source or recalled from androidx:
 *
 *   ref_screen3d_timepicker.png      12-hour, 07:00 selected
 *   ref_screen3d_timepicker_24h.png  the same at 24-hour
 *
 * What the captures settled, and what guessing would have got wrong:
 *
 *   - 12-hour: ONE ring of 12 numerals, 12 at the top, plus an AM/PM toggle beside the readout.
 *   - 24-hour: TWO concentric rings — OUTER 0..11, INNER 12..23, both from the top — and NO
 *     AM/PM toggle.
 */

#ifndef BREEZY_VIEW_NOTIFICATIONSSCREEN_H
#define BREEZY_VIEW_NOTIFICATIONSSCREEN_H

#include <native_drawing/drawing_canvas.h>

#include <string>
#include <vector>

namespace breezy {

enum class NotificationsAction {
    None,
    /** The back arrow. */
    Close,
    /** A row that is not ported; `destination` carries its name. */
    Navigate,
    /** A setting changed: `key` is the preferences key, `value` its new value as a string. */
    Changed
};

struct NotificationsHit {
    NotificationsAction action = NotificationsAction::None;
    std::string destination;
    std::string key;
    std::string value;
};

class NotificationsScreen {
public:
    void SetGeometry(double widthPx, double heightPx, float density, double topInsetPx,
                     double bottomInsetPx);
    void SetDarkMode(bool dark) { dark_ = dark; }
    /** The live values, so the rows can show what is stored. */
    void SetValues(bool appUpdates, bool alert, bool precipitation, bool today, bool tomorrow,
                   const std::string& todayTime, const std::string& tomorrowTime,
                   const std::string& refreshRate, bool twelveHour);

    void Draw(OH_Drawing_Canvas* canvas);
    NotificationsHit HitTest(double x, double y);
    bool ScrollBy(double dy);
    void ResetScroll() { scroll_ = 0.0; }
    bool DialogOpen() const { return dialogOpen_; }
    void CloseDialog() { dialogOpen_ = false; }
    /** A drag inside the open dial moves the hand. True when the frame needs repainting. */
    bool DragDial(double x, double y);

    /**
     * The keyboard button's TimeInput (TimePickerPreference.kt:213-217).
     *
     * Only ArkTS can reach @ohos.inputMethod, so the screen ASKS for an edit session and is told
     * what was typed — the same split the search field uses (shell/TextEntry.ets). `1` opens a
     * session for the focused field, `0` closes it.
     */
    using TimeEntryHandler = void (*)(int state);
    void SetTimeEntryHandler(TimeEntryHandler handler) { timeEntry_ = handler; }
    /** Digits typed into the focused field. Returns true when the frame needs repainting. */
    bool SetEnteredText(const std::string& text);
    bool TextEntryOpen() const { return textEntry_; }

private:
    struct Item {
        bool header = false;
        std::string title;
        std::string summary;
        bool isFirst = false;
        bool isLast = false;
        bool endsGroup = false;
        bool isSwitch = false;
        bool checked = false;
        /** Opens the time picker; `key` says which of the two times it edits. */
        bool opensPicker = false;
        /**
         * kt:144-145, :199 — a row the Kotlin passes `enabled = false`. It still draws, greyed,
         * and does not respond, which is what SwitchPreferenceView does with a disabled switch.
         */
        bool rowEnabled = true;
        std::string key;
        double top = 0.0;
        double height = 0.0;
    };

    void Layout();
    /** kt:143-145 — background updates off makes both General rows dead. */
    bool BackgroundUpdatesOn() const;
    /** kt:135-141 — the OFF summary becomes the "enable background updates first" line. */
    std::string GeneralSummary(bool value) const;
    double ListTop() const;
    double MaxScroll() const;
    void DrawDialog(OH_Drawing_Canvas* canvas);
    NotificationsHit HitDialog(double x, double y);
    /** Moves the hand if the touch landed on the dial; true when it did, so a drag can continue. */
    bool HitDial(double x, double y);
    /** The "HH:mm" currently being edited, by key. */
    const std::string& EditedTime() const;
    /**
     * How a stored time is DISPLAYED — Date.getFormattedTime (DateExtensions.kt:111-121) through
     * TimePickerPreference.kt:134. A 12-hour device gets "h:mm a", a 24-hour one "HH:mm". The
     * stored value is always "HH:mm" either way (kt:222-233), so this never round-trips.
     */
    std::string FormatForDisplay(const std::string& value) const;

    std::vector<Item> items_;
    double laidOutWidth_ = 0.0;
    float laidOutDensity_ = 0.0f;
    std::string laidOutValues_;

    bool appUpdates_ = false;
    bool alert_ = true;
    bool precipitation_ = false;
    bool today_ = false;
    bool tomorrow_ = false;
    std::string todayTime_ = "07:00";
    std::string tomorrowTime_ = "21:00";
    std::string refreshRate_ = "1:30";
    /** `Context.is12Hour` (DateExtensions.kt:42-43), pushed by the shell. */
    bool twelveHour_ = false;

    double width_ = 0.0;
    double height_ = 0.0;
    float density_ = 1.0f;
    double topInset_ = 0.0;
    double bottomInset_ = 0.0;
    double scroll_ = 0.0;
    bool dark_ = false;

    bool dialogOpen_ = false;
    /** Which time the open picker edits — one of the two forecast-time keys. */
    std::string dialogKey_;
    int pendingHour_ = 0;
    int pendingMinute_ = 0;
    /** Which field the dial edits. The hour box is the selected one on open (kt:151-155). */
    bool editingMinute_ = false;
    /** The keyboard button's mode: the dial is replaced by two typed fields. */
    bool textEntry_ = false;
    TimeEntryHandler timeEntry_ = nullptr;
    /** Where the keyboard button landed, so a tap can be matched. */
    double dialogKeyboardRight_ = 0.0;

    /*
     * Where the dialog's parts landed last frame, so a touch can be matched without laying it out
     * again — the same approach the trend chips use.
     */
    double dialogLeft_ = 0.0;
    double dialogRight_ = 0.0;
    double dialogHourLeft_ = 0.0;
    double dialogHourRight_ = 0.0;
    double dialogMinuteLeft_ = 0.0;
    double dialogMinuteRight_ = 0.0;
    double dialogSelectorTop_ = 0.0;
    double dialogSelectorBottom_ = 0.0;
    double dialogPeriodLeft_ = 0.0;
    double dialogPeriodRight_ = 0.0;
    double dialogPeriodMiddle_ = 0.0;
    double dialogDialCentreX_ = 0.0;
    double dialogDialCentreY_ = 0.0;
    double dialogDialRadius_ = 0.0;
    double dialogButtonTop_ = 0.0;
    double dialogButtonBottom_ = 0.0;
    double dialogCancelLeft_ = 0.0;
    double dialogConfirmLeft_ = 0.0;
};

}  // namespace breezy

#endif  // BREEZY_VIEW_NOTIFICATIONSSCREEN_H
