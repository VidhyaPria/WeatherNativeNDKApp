#include "view/LocationSearchScreen.h"

#include <native_drawing/drawing_brush.h>
#include <native_drawing/drawing_rect.h>
#include <native_drawing/drawing_round_rect.h>

#include <algorithm>
#include <cstdio>

#include "render/AssetImages.h"
#include "render/TextPainter.h"

namespace breezy {
namespace {

/** strings.xml `location_search_hint`, the placeholder Material3SearchBarInputField carries. */
constexpr const char* kPlaceholder = "Search for a location…";

void FillRect(OH_Drawing_Canvas* canvas, double left, double top, double right, double bottom,
              uint32_t color) {
    OH_Drawing_Brush* brush = OH_Drawing_BrushCreate();
    OH_Drawing_BrushSetColor(brush, color);
    OH_Drawing_Rect* rect = OH_Drawing_RectCreate(
        static_cast<float>(left), static_cast<float>(top),
        static_cast<float>(right), static_cast<float>(bottom));
    OH_Drawing_CanvasAttachBrush(canvas, brush);
    OH_Drawing_CanvasDrawRect(canvas, rect);
    OH_Drawing_CanvasDetachBrush(canvas);
    OH_Drawing_RectDestroy(rect);
    OH_Drawing_BrushDestroy(brush);
}

void FillRoundRect(OH_Drawing_Canvas* canvas, double left, double top, double right, double bottom,
                   double radius, uint32_t color) {
    OH_Drawing_Brush* brush = OH_Drawing_BrushCreate();
    OH_Drawing_BrushSetColor(brush, color);
    OH_Drawing_BrushSetAntiAlias(brush, true);
    OH_Drawing_Rect* rect = OH_Drawing_RectCreate(
        static_cast<float>(left), static_cast<float>(top),
        static_cast<float>(right), static_cast<float>(bottom));
    OH_Drawing_RoundRect* round = OH_Drawing_RoundRectCreate(
        rect, static_cast<float>(radius), static_cast<float>(radius));
    OH_Drawing_CanvasAttachBrush(canvas, brush);
    OH_Drawing_CanvasDrawRoundRect(canvas, round);
    OH_Drawing_CanvasDetachBrush(canvas);
    OH_Drawing_RoundRectDestroy(round);
    OH_Drawing_RectDestroy(rect);
    OH_Drawing_BrushDestroy(brush);
}

}  // namespace

void LocationSearchScreen::SetResults(std::vector<GeocodingResult> results, std::string searched,
                                      bool ok) {
    results_ = std::move(results);
    searched_ = std::move(searched);
    searchOk_ = ok;
    loading_ = false;
    scroll_ = 0.0;
}

void LocationSearchScreen::Reset() {
    query_.clear();
    searched_.clear();
    results_.clear();
    searchOk_ = true;
    loading_ = false;
    scroll_ = 0.0;
    sourceDialogOpen_ = false;
    // The chosen search source is NOT reset: it is a preference on Android (SettingsManager), not
    // activity state, so it outlives one visit to the screen.
}

/** The supporting line's style, shared by the measure and the draw so the two cannot drift. */
TextStyleSpec LocationSearchScreen::LevelsStyle() const {
    TextStyleSpec style;
    style.sizeVp = dimens::kContentTextSize;
    // Android's `supportingContent` WRAPS — "United States, Wisconsin, Bayfield County, Town of
    // Oulu" is two lines on the reference. This port used to force a single line.
    style.singleLine = false;
    return style;
}

/**
 * Row `index`'s height.
 *
 * A Material3 ListItem's height is a FIXED TOKEN, not the sum of its content — the same thing the
 * alert card was wrong about. Measured on the reference, the result rows sit on a perfect
 * 72 dp / 88 dp grid with 4 dp between them:
 *
 *     row 1 top 315.5  two-line   234 px   title at 361   (measured 361)
 *     row 2 top 562.5  three-line 286 px   title at 601.5 (measured 601)
 *     row 3 top 861.5  two-line   234 px   title at 907   (measured 907)
 *     ... and every row after it, to 2389
 *
 * A supporting text that wraps takes the three-line token. This port drew every row at
 * title + body + 16 dp = 179 px against 247, so the whole list was ~27 % short. ArkUIApp had the
 * same error and now reaches the same grid.
 */
double LocationSearchScreen::RowHeight(size_t index) const {
    if (index >= results_.size()) {
        return dimens::kListItemTwoLineHeight * density_;
    }
    const double textWidth = width_ - dimens::kNormalMargin * density_ * 2.0;
    const double levels =
        MeasureTextHeight(results_[index].levels, textWidth, LevelsStyle());
    const bool wrapped = levels > dimens::kBodyMediumLineHeight * density_ * 1.5;
    return (wrapped ? dimens::kListItemThreeLineHeight : dimens::kListItemTwoLineHeight) * density_;
}

double LocationSearchScreen::ListTop() const {
    // The field, its divider, and the 4 dp the progress bar occupies whether or not it is showing —
    // the Kotlin lets the list jump by that much, but a bar that appears under the finger and moves
    // the row being tapped is worse here, where there is no ripple to explain it.
    return topInset_ + FieldHeight() + density_ + kProgressHeight * density_;
}

double LocationSearchScreen::ListBottom() const {
    return height_ - bottomInset_ - kBottomBarHeight * density_;
}

double LocationSearchScreen::ContentHeight() const {
    if (results_.empty()) {
        return 0.0;
    }
    const double spacing = kResultSpacing * density_;
    double total = static_cast<double>(results_.size() - 1) * spacing;
    for (size_t i = 0; i < results_.size(); ++i) {
        total += RowHeight(i);
    }
    return total;
}

void LocationSearchScreen::ScrollBy(double dy) {
    if (sourceDialogOpen_) {
        modal_.ScrollBy(dy);
        return;
    }
    const double viewport = ListBottom() - ListTop();
    const double maxScroll = std::max(0.0, ContentHeight() - viewport);
    scroll_ = std::max(0.0, std::min(maxScroll, scroll_ - dy));
}

void LocationSearchScreen::Draw(OH_Drawing_Canvas* canvas) {
    /*
     * `Surface(color = SearchBarDefaults.colors().containerColor)` (:163-168) — the whole screen,
     * field and rows alike, sits on ONE tinted surface rather than the plain background. It is the
     * 2 dp elevated surface, the same derivation ArkUIApp's searchSurface() makes.
     */
    const uint32_t surface = ElevatedSurface(2.0, kPrimary.Resolve(dark_), kSurface.Resolve(dark_));
    const uint32_t onSurface = kOnSurface.Resolve(dark_);
    const uint32_t onSurfaceVariant = kOnSurfaceVariant.Resolve(dark_);
    FillRect(canvas, 0.0, 0.0, width_, height_, surface);

    const double iconSize = dimens::kMaterialIconSize * density_;
    const double normalMargin = dimens::kNormalMargin * density_;
    const double fieldHeight = FieldHeight();
    const double fieldCentre = topInset_ + fieldHeight / 2.0;

    /*
     * ---- the search input (:173-189) ----
     *
     * `leadingIcon = Icon(Icons.Default.Search)` (:188), inside the decoration box that offsets it
     * 4 dp (Widgets.kt:167-171).
     *
     * MEASURED on the reference rather than assembled from the Compose constants, because the 4 dp
     * offset applies to a slot whose own width and padding are Material3 internals this tree does
     * not contain. Pixel-scanning the reference against ArkUIApp over the same row band gave the
     * magnifier 4 dp and the hint text 8 dp too far right in both ports: the reference's icon box
     * starts at 16 dp and its text 12 dp after the box ends. ArkUIApp carries the same two numbers.
     */
    const double iconTextGap = 12.0 * density_;
    DrawAsset(canvas, "ic_search.svg", normalMargin, fieldCentre - iconSize / 2.0,
              iconSize, onSurface, true);

    const double textLeft = normalMargin + iconSize + iconTextGap;
    TextStyleSpec fieldStyle;
    fieldStyle.sizeVp = kInputTextSize;
    fieldStyle.singleLine = true;
    // The placeholder takes onSurfaceVariant; typed text takes onBackground, which is onSurface's
    // value in this palette.
    fieldStyle.color = query_.empty() ? onSurfaceVariant : onSurface;
    const std::string shown = query_.empty() ? std::string(kPlaceholder) : query_;
    const double fieldTextWidth = width_ - textLeft - normalMargin;
    const double fieldTextHeight = MeasureTextHeight(shown, fieldTextWidth, fieldStyle);
    DrawText(canvas, shown, textLeft, fieldCentre - fieldTextHeight / 2.0, fieldTextWidth,
             fieldStyle);

    /*
     * The caret.
     *
     * A TextInput draws its own and blinks it; here it is a static 2 dp rule after the last glyph,
     * because a blink means a repaint twice a second on a screen that is otherwise completely
     * static — and this screen has no frame loop at all (Renderer::OnVSync returns immediately for
     * it). A steady caret is what an unfocused-looking field needs least.
     */
    if (!query_.empty()) {
        const double caretX =
            std::min(textLeft + MeasureTextWidth(query_, fieldStyle) + 2.0 * density_,
                     width_ - normalMargin);
        const double caretHeight = kInputTextSize * density_ * 1.2;
        FillRect(canvas, caretX, fieldCentre - caretHeight / 2.0, caretX + 2.0 * density_,
                 fieldCentre + caretHeight / 2.0, kPrimary.Resolve(dark_));
    }

    /* `HorizontalDivider(color = SearchBarDefaults.colors().dividerColor)` (:190) */
    const double dividerTop = topInset_ + fieldHeight;
    FillRect(canvas, 0.0, dividerTop, width_, dividerTop + density_, kOutline.Resolve(dark_));

    /* ---- progress (:192-194) ---- */
    if (loading_) {
        const double top = dividerTop + density_;
        const double bottom = top + kProgressHeight * density_;
        // An indeterminate bar would need the frame loop this screen deliberately does not run
        // (Renderer::OnVSync returns immediately for it), so the track is filled solid rather than
        // swept — the bar's only job here is to say that a request is out.
        FillRect(canvas, 0.0, top, width_, bottom, kPrimary.Resolve(dark_));
    }

    /* ---- results, or the empty state (:195-250) ---- */
    if (!results_.empty()) {
        OH_Drawing_CanvasSave(canvas);
        OH_Drawing_Rect* clip = OH_Drawing_RectCreate(
            0.0f, static_cast<float>(ListTop()),
            static_cast<float>(width_), static_cast<float>(ListBottom()));
        OH_Drawing_CanvasClipRect(canvas, clip, OH_Drawing_CanvasClipOp::INTERSECT, true);
        OH_Drawing_RectDestroy(clip);

        TextStyleSpec titleStyle;
        titleStyle.sizeVp = dimens::kTitleTextSize;
        titleStyle.color = onSurface;
        titleStyle.singleLine = true;

        TextStyleSpec levelStyle = LevelsStyle();
        levelStyle.color = onSurfaceVariant;

        const double textWidth = width_ - normalMargin * 2.0;
        // The two lines are centred as a BLOCK inside the fixed row, which is what puts the
        // reference's headline 45.5 px down a two-line row and 39 px down a three-line one.
        const double titleH = dimens::kBodyLargeLineHeight * density_;
        double y = ListTop() - scroll_;
        for (size_t i = 0; i < results_.size(); ++i) {
            const double rowHeight = RowHeight(i);
            if (y + rowHeight >= ListTop() && y <= ListBottom()) {
                const double levelsH =
                    MeasureTextHeight(results_[i].levels, textWidth, levelStyle);
                const double top = y + (rowHeight - titleH - levelsH) / 2.0;
                DrawText(canvas, results_[i].place, normalMargin, top, textWidth, titleStyle);
                DrawText(canvas, results_[i].levels, normalMargin, top + titleH, textWidth,
                         levelStyle);
            }
            y += rowHeight + kResultSpacing * density_;
        }
        OH_Drawing_CanvasRestore(canvas);
    } else if (!searched_.empty() && searchOk_ && !loading_) {
        /*
         * The advice block appears only after a SUCCESSFUL search that found nothing (:230-232).
         * After a failure the toast has already said so and this stays empty.
         */
        TextStyleSpec headline;
        headline.sizeVp = dimens::kTitleTextSize;
        headline.color = onSurface;

        TextStyleSpec advice;
        advice.sizeVp = dimens::kContentTextSize;
        advice.color = onSurfaceVariant;

        const double textWidth = width_ - normalMargin * 2.0;
        const double top = ListTop() + normalMargin;
        // strings.xml `location_search_no_results` — "%1$s can’t find %2$s", naming the ACTIVE
        // source, which is why the message changes with the Tune dialog's pick.
        const SourceInfo* source = LocationSearchSourceOrDefault(searchSourceId_);
        const std::string message =
            (source != nullptr ? source->name : std::string()) + " can\xE2\x80\x99t find " +
            searched_;
        const double height = DrawText(canvas, message, normalMargin, top, textWidth, headline);
        DrawText(canvas,
                 "Make sure your search is spelled correctly and not too specific. Alternatively, "
                 "you can try another location search source which may produce different results.",
                 normalMargin, top + height + dimens::kSmallMargin * density_, textWidth, advice);
    }

    /* ---- the bottom app bar (:129-160) ---- */
    const double barTop = ListBottom();
    const double barCentre = barTop + kBottomBarHeight * density_ / 2.0;
    const double fab = kFabSize * density_;
    const double fabLeft = width_ - normalMargin - fab;

    TextStyleSpec attribution;
    // `MaterialTheme.typography.bodySmall` (:145)
    attribution.sizeVp = dimens::kSubtitleTextSize;
    attribution.color = onSurfaceVariant;
    /*
     * 16 dp from the Box (:136) PLUS Material3's own `BottomAppBarHorizontalPadding` of 4 dp on
     * the actions row — the reference measures this text node at x=65, i.e. 20 dp, where both
     * ports had 52. The FAB is unaffected: it is the bar's `floatingActionButton` slot rather
     * than an action, and both ports already place it at 1026..1208 like the reference.
     */
    const double attributionLeft = normalMargin + 4.0 * density_;
    const double attributionWidth = fabLeft - attributionLeft - normalMargin;
    // strings.xml `location_results_by` — "Location results by %s", the ACTIVE source's own
    // attribution (:141-144).
    const SourceInfo* active = LocationSearchSourceOrDefault(searchSourceId_);
    const std::string credit =
        std::string("Location results by ") + (active != nullptr ? active->searchAttribution : "");
    const double creditHeight = MeasureTextHeight(credit, attributionWidth, attribution);
    DrawText(canvas, credit, attributionLeft, barCentre - creditHeight / 2.0, attributionWidth,
             attribution);

    /*
     * The Tune FAB (:151-159). Unlike the location list's gold FAB this one takes
     * FloatingActionButtonDefaults.containerColor, the Material default, which is primaryContainer.
     */
    FillRoundRect(canvas, fabLeft, barCentre - fab / 2.0, fabLeft + fab, barCentre + fab / 2.0,
                  normalMargin, kPrimaryContainer.Resolve(dark_));
    DrawAsset(canvas, "ic_tune.svg", fabLeft + (fab - iconSize) / 2.0,
              barCentre - iconSize / 2.0, iconSize, kOnPrimaryContainer.Resolve(dark_), true);

    /* ---- the source chooser, when it is up ---- */
    if (sourceDialogOpen_) {
        // 80 % of the window, as the Compose AlertDialogNoPadding is.
        modal_.SetGeometry(width_, height_, density_, dark_, 0.8);
        modal_.Draw(canvas);
    }
}

/* ------------------------------------------------------------------- the source chooser ------- */

void LocationSearchScreen::BuildSearchSourceEntries() {
    // strings.xml `location_search_source` (SearchActivity.kt:258-263).
    modal_.SetTitle("Location search source");
    modal_.SetNote("");
    modal_.SetWarning("");
    std::vector<ModalEntry> entries;
    for (const SourceInfo* source : LocationSearchSources()) {
        ModalEntry entry;
        entry.id = source->id;
        entry.label = source->DisplayName();
        entry.selected = source->id == searchSourceId_;
        entries.push_back(std::move(entry));
    }
    modal_.SetEntries(std::move(entries));
    // The confirm button is a Cancel — it only closes (:339-349).
    modal_.SetButtons({"CANCEL"});
}

LocationSearchHit LocationSearchScreen::OnDialogTap(double x, double y) {
    LocationSearchHit hit;
    hit.action = LocationSearchAction::Redraw;
    const ModalList::Hit tap = modal_.HitTest(x, y);

    if (tap.kind == ModalList::HitKind::None) {
        hit.action = LocationSearchAction::None;
        return hit;
    }
    // The scrim is `onDismissRequest` (:257), and the one button is a Cancel; both just close.
    if (tap.kind != ModalList::HitKind::Entry) {
        sourceDialogOpen_ = false;
        return hit;
    }

    const ModalEntry& entry = modal_.Entries()[static_cast<size_t>(tap.index)];
    sourceDialogOpen_ = false;
    const SourceInfo* source = GetSource(entry.id);
    if (source == nullptr || !source->implemented) {
        /*
         * Android would switch the source and re-run the query. This port has a client for
         * Open-Meteo only, so a different pick is answered with the placeholder and the active
         * source is left alone — switching to a source that cannot answer would leave the screen
         * unable to search at all.
         */
        hit.action = LocationSearchAction::Navigate;
        hit.destination = (source != nullptr ? source->name : entry.id) + " location search";
        return hit;
    }
    searchSourceId_ = entry.id;
    return hit;
}

LocationSearchHit LocationSearchScreen::OnTap(double x, double y) {
    if (sourceDialogOpen_) {
        return OnDialogTap(x, y);
    }

    LocationSearchHit hit;
    const double normalMargin = dimens::kNormalMargin * density_;
    const double fab = kFabSize * density_;
    const double fabLeft = width_ - normalMargin - fab;
    const double barCentre = ListBottom() + kBottomBarHeight * density_ / 2.0;

    if (x >= fabLeft && x <= fabLeft + fab && y >= barCentre - fab / 2.0 &&
        y <= barCentre + fab / 2.0) {
        // openDialogLocationSearchSource (:153).
        sourceDialogOpen_ = true;
        modal_.ResetScroll();
        BuildSearchSourceEntries();
        hit.action = LocationSearchAction::Redraw;
        return hit;
    }

    if (y < ListTop() || y > ListBottom()) {
        return hit;
    }
    double rowY = ListTop() - scroll_;
    for (size_t i = 0; i < results_.size(); ++i) {
        const double rowHeight = RowHeight(i);
        if (y >= rowY && y <= rowY + rowHeight) {
            /*
             * SearchActivity.kt:212-223 — picking a hit opens SecondarySourcesPreference, and only
             * its `onClose(location)` finishes the activity with a result. The dialog belongs to
             * the renderer, so the hit is simply reported; everything it needs travels with it.
             */
            const GeocodingResult& result = results_[i];
            hit.action = LocationSearchAction::Pick;
            hit.location.countryCode = result.countryCode;
            hit.location.region = result.region;
            hit.location.admin2 = result.admin2;
            hit.location.latitude = result.latitude;
            hit.location.longitude = result.longitude;
            hit.location.isCurrentPosition = false;
            hit.place = result.place;
            return hit;
        }
        rowY += rowHeight + kResultSpacing * density_;
    }
    return hit;
}

}  // namespace breezy

