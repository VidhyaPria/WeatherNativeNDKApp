#include "render/Renderer.h"

#include <hilog/log.h>
#include <native_buffer/native_buffer.h>
#include <native_drawing/drawing_bitmap.h>
#include <native_drawing/drawing_brush.h>
#include <native_drawing/drawing_color.h>
#include <native_drawing/drawing_pen.h>
#include <native_drawing/drawing_point.h>
#include <native_drawing/drawing_rect.h>
#include <native_drawing/drawing_shader_effect.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "common/Formatter.h"
#include "common/LocalTime.h"
#include "common/Units.h"
#include "render/AssetImages.h"
#include "render/ColumnCache.h"
#include "render/FrameTrace.h"
#include "render/Shapes.h"
#include "render/TextPainter.h"
#include "render/TrendCardPainter.h"

#undef LOG_TAG
#define LOG_TAG "BreezyNative"

namespace breezy {
namespace {

/** The buffer XComponent hands us is RGBA_8888, so the bitmap is built to match. */
constexpr OH_Drawing_ColorFormat kColorFormat = COLOR_FORMAT_RGBA_8888;
constexpr OH_Drawing_AlphaFormat kAlphaFormat = ALPHA_FORMAT_OPAQUE;
constexpr uint32_t kBytesPerPixel = 4;

/**
 * What the page shows before any weather. The cards' own surface (neutral1_10 / neutral1_900),
 * matching kMainCardBackground — see Theme.h on why this is colorSurface and not the dead
 * `colorMainCardBackground` attribute these values used to come from.
 */
constexpr uint32_t kNeutralLight = 0xFFFCFCFF;
constexpr uint32_t kNeutralDark = 0xFF191C1E;

/**
 * OpenMeteoService.kt:145 â€” `weatherAttribution = "Open-Meteo (CC BY 4.0)"`. Only Open-Meteo is
 * ported (CLAUDE.md), so this is fixed until the source picker exists.
 */
constexpr const char* kForecastSourceName = "Open-Meteo (CC BY 4.0)";

/** MaterialWeatherView.mFirstCardMarginTop â€” the scroll distance the background fades over, vp. */
constexpr double kBackgroundScrollSpan = 182.0;

/** How far the page must be pulled past the top before releasing starts a refresh, vp. */
constexpr double kRefreshTriggerVp = 64.0;

/** The weather background's own frame interval â€” 30 fps, as ArkUIApp's displaySync loop runs. */
constexpr double kBackgroundFrameMs = 1000.0 / 30.0;

/** â€¦and never more than one scene repaint in this many frames, whatever the interval says. */
constexpr int kBackgroundMinFrames = 3;

/** The most elapsed time the scene may bank while it is held still, ms. */
constexpr double kBackgroundBankMs = 400.0;

/*
 * getBlocksPerRow (DisplayExtensions.kt:79-88), driven from HomeFragment.kt:259 with the
 * RecyclerView's own width. Android counts in units of 160 dp â€” `windowWidth / densityDpi`, where
 * `Context.density` is densityDpi rather than the vp scale â€” which is `widthVp / 160` here.
 *
 * It is what makes landscape a different page instead of a stretched one: the reference phone
 * (1008 x 2244 at 360 dpi) measures 2.8 units across in portrait and 5.6 in landscape, so two
 * blocks a row become five; this device (1260 x 2720 at 520 dpi) measures 2.42 and 5.23, so two
 * become four. The rule is ported, not the number.
 */
constexpr double kDpPerBlockUnit = 160.0;
/** DisplayExtensions.kt:20 â€” past two blocks, prefer fewer and roomier over squished. */
constexpr double kSquishedBlockFactor = 1.1;
/** DisplayExtensions.kt:76 â€” 0.5 + fontScale/2, with the device at the default font scale. */
constexpr double kMinBlockWidth = 1.0;

}  // namespace

Renderer::~Renderer() {
    OnSurfaceDestroyed();
}

void Renderer::OnSurfaceCreated(OHNativeWindow* window, uint64_t width, uint64_t height) {
    window_ = window;
    ApplyGeometry(width, height);
    OH_LOG_INFO(LOG_APP, "surface created %{public}llu x %{public}llu",
                static_cast<unsigned long long>(width), static_cast<unsigned long long>(height));
    RenderFrame();
}

void Renderer::OnSurfaceChanged(uint64_t width, uint64_t height) {
    ApplyGeometry(width, height);
    /*
     * Two-pane has no full-screen list, so a rotation into it has to FOLD the list screen away.
     *
     * `layout-w640dp` puts the list in the drawer pane and DrawerLayout.kt:53 unfolds that pane
     * permanently, which is why openDrawer/closeDrawer are no-ops there (ManagementFragment.kt:
     * 198-202 also drops the back arrow). The pane IS the list. Without this, rotating while the
     * list screen happened to be open left it drawn full-width with a back arrow — a state Android
     * cannot produce — instead of the list-beside-weather layout.
     *
     * OpenLocationList() already refuses to enter the screen while two-pane; this is the same rule
     * applied to a screen that was already open when the geometry changed under it.
     */
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (TwoPane() && screen_ == Screen::LocationList) {
            screen_ = Screen::Main;
            backgroundValid_ = false;
            lastFrameWasComplete_ = false;
            scratchValid_ = false;
        }
    }
    RenderFrame();
}

/**
 * Configures the surface's buffers before the first one is requested.
 *
 * All three options matter. Without SET_BUFFER_GEOMETRY the buffers come back at the producer's
 * default size rather than the XComponent's. Without CPU_READ/CPU_WRITE in the usage they are
 * allocated for the GPU only and arrive with a null virAddr â€” there is no memory to draw into
 * from this side, which is exactly what the first run hit. SET_FORMAT pins the layout the
 * OH_Drawing_Bitmap is built to match.
 */
void Renderer::ApplyGeometry(uint64_t width, uint64_t height) {
    width_ = width;
    height_ = height;
    if (window_ == nullptr || width == 0 || height == 0) {
        return;
    }

    const uint64_t usage = NATIVEBUFFER_USAGE_CPU_READ | NATIVEBUFFER_USAGE_CPU_WRITE |
                           NATIVEBUFFER_USAGE_MEM_DMA;
    int32_t status = OH_NativeWindow_NativeWindowHandleOpt(window_, SET_USAGE, usage);
    if (status != 0) {
        OH_LOG_ERROR(LOG_APP, "SET_USAGE failed: %{public}d", status);
    }
    status = OH_NativeWindow_NativeWindowHandleOpt(window_, SET_FORMAT,
                                                   NATIVEBUFFER_PIXEL_FMT_RGBA_8888);
    if (status != 0) {
        OH_LOG_ERROR(LOG_APP, "SET_FORMAT failed: %{public}d", status);
    }
    status = OH_NativeWindow_NativeWindowHandleOpt(
        window_, SET_BUFFER_GEOMETRY, static_cast<int32_t>(width), static_cast<int32_t>(height));
    if (status != 0) {
        OH_LOG_ERROR(LOG_APP, "SET_BUFFER_GEOMETRY failed: %{public}d", status);
    }
}

/**
 * The page's own height: the surface less the system bars it is now laid out under.
 *
 * Everything above and below this is drawn by the system (the clock, the navigation bar), and the
 * weather background is the only thing that reaches into it â€” exactly as an edge-to-edge Activity
 * behaves, where the AppBar and RecyclerView are inset but the window's background is not.
 */
/**
 * `layout-w640dp` AND `isLandscape` (DrawerLayout.kt:53). Both, not either.
 *
 * The width qualifier is what SELECTS the two-pane layout file; the landscape test is what unfolds
 * it. A 640 dp portrait window â€” a tablet held upright â€” gets the drawer but folded, which is the
 * same single-pane behaviour this port already has.
 */
bool Renderer::TwoPane() const {
    const float density = DisplayDensity();
    if (density <= 0.0f || width_ == 0 || height_ == 0) {
        return false;
    }
    const bool landscape = width_ > height_;
    const double widthDp = static_cast<double>(width_) / density;
    return landscape && widthDp >= 640.0;
}

double Renderer::PaneWidth() const {
    if (!TwoPane()) {
        return 0.0;
    }
    const double density = DisplayDensity();
    const double width = static_cast<double>(width_);
    // getTabletListAdaptiveWidth â€” this port targets a phone, so the 512 dp cap applies.
    const double adaptive = std::min(width, 512.0 * density);
    double drawer = width - adaptive;
    drawer = std::max(drawer, 280.0 * density);   // MIN_DRAWER_WIDTH_DP
    drawer = std::min(drawer, 320.0 * density);   // MAX_DRAWER_WIDTH_DP
    return drawer;
}

double Renderer::ContentWidth() const {
    return static_cast<double>(width_) - PaneWidth();
}

/*
 * FitTabletRecyclerView.kt:33-34 â€” the main list pads itself by `normal_margin / 2`, i.e. 8 dp,
 * horizontally, on top of whatever each item asks for.
 *
 * This port did not have it, so every card on screen 1 was 8 dp wider on each side than Android's
 * and every left-aligned thing in the header sat 8 dp further out. Measured on the reference: the
 * daily trend card spans x 52..1208, its trend list the same, and a half-size block 52..604 /
 * 656..1208 (552 square); this port had 26..1234, 26..1234 and 26..604 / 656..1234 (578 square).
 * ArkUIApp carried the identical error, which is why a port-to-port comparison never saw it.
 */
double Renderer::PageInset() const {
    return dimens::kSmallMargin * DisplayDensity();
}

double Renderer::PageContentWidth() const {
    return std::max(0.0, ContentWidth() - PageInset() * 2.0);
}

double Renderer::ViewportHeight() const {
    return std::max(0.0, static_cast<double>(height_) - topInset_ - bottomInset_);
}

void Renderer::SetInsets(double topPx, double bottomPx) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (topInset_ == topPx && bottomInset_ == bottomPx) {
            return;
        }
        topInset_ = std::max(0.0, topPx);
        bottomInset_ = std::max(0.0, bottomPx);
        backgroundValid_ = false;
    }
}

void Renderer::SetKeyboardInset(double heightPx) {
    bool searchScreen = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const double value = std::max(0.0, heightPx);
        if (keyboardInset_ == value) {
            return;
        }
        keyboardInset_ = value;
        searchScreen = (screen_ == Screen::LocationSearch);
    }
    // Nothing else lays out against it, so nothing else has to be repainted for it.
    if (searchScreen) {
        RenderFrame();
    }
}

void Renderer::SetThemeHandler(ThemeHandler handler) {
    themeChanged_ = handler;
    reportedLightBackground_ = -1;  // Force the next report through, whatever it is.
    ReportTheme();
}

/** Tells the shell the sky's lightness, but only when it has actually changed. */
void Renderer::ReportTheme() {
    if (themeChanged_ == nullptr) {
        return;
    }
    const int light = IsLightBackground(scene_.daylight, scene_.darkMode) ? 1 : 0;
    if (light == reportedLightBackground_) {
        return;
    }
    reportedLightBackground_ = light;
    themeChanged_(light != 0);
}

void Renderer::OnSurfaceDestroyed() {
    std::lock_guard<std::mutex> renderLock(renderMutex_);
    window_ = nullptr;
    width_ = 0;
    height_ = 0;
    DestroyScratch();
    ClearColumnCache();
}

/**
 * Rebuilds both cards' series for their current tabs.
 *
 * Lazy, like the ArkTS card: only the selected tab's series exists at any moment, so switching
 * tabs costs one build rather than keeping fourteen series alive for the memory comparison to
 * pick up.
 */
void Renderer::RebuildSeries() {
    // Every cached column belongs to the series being replaced.
    ++seriesGeneration_;
    if (!scene_.hasWeather) {
        return;
    }
    const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    const uint32_t titleTextColor = kTitleText.Resolve(scene_.darkMode);

    detailBlocks_ = BuildDetailBlocks(scene_.weather, scene_.darkMode, nowMs,
                                      scene_.utcOffsetSeconds, scene_.is24Hour);

    /*
     * The page's card sequence, built by walking `card_display` IN ORDER and skipping the cards the
     * data does not support — MainAdapter.kt:110-171, one `continue` per card.
     *
     * The stored list is the order: it is what the Blocks manage screen edits and what a drag on
     * this screen writes back, exactly as `SettingsManager.cardDisplayList` is on Android. There is
     * no separate in-memory order any more, so a refresh cannot undo a drag and the manage screen
     * cannot disagree with the page.
     */
    order_ = BuildCardOrder();
    // Defensive, for the same reason SetDisplayLists clamps: a stored list can name fewer tabs
    // than the selected index.
    if (dailyTab_ >= static_cast<int>(DailyTabs().size())) {
        dailyTab_ = 0;
    }
    if (hourlyTab_ >= static_cast<int>(HourlyTabs().size())) {
        hourlyTab_ = 0;
    }
    dailySeries_ = BuildDailySeries(scene_.weather, DailyTabs()[dailyTab_].id, nowMs,
                                    scene_.utcOffsetSeconds, titleTextColor);
    hourlySeries_ = BuildHourlySeries(scene_.weather, HourlyTabs()[hourlyTab_].id,
                                      scene_.utcOffsetSeconds, scene_.is24Hour, titleTextColor);

    const double columnWidth = dimens::kTrendItemWidth * DisplayDensity();
    // The trend list fills its card, which is the page column inset by the card's own 8 dp margin
    // (`daily_block_trendRecyclerView` measured at x 52..1208 against a 26..1234 page column).
    const double viewport =
        PageContentWidth() - dimens::kSmallMargin * 2 * DisplayDensity();
    dailyScroll_.SetRange(
        std::max(0.0, static_cast<double>(dailySeries_.items.size()) * columnWidth - viewport));
    hourlyScroll_.SetRange(
        std::max(0.0, static_cast<double>(hourlySeries_.items.size()) * columnWidth - viewport));
}

void Renderer::SelectLocation(int index) {
    if (index < 0 || index >= static_cast<int>(scenes_.size()) || index == currentLocation_) {
        return;
    }
    // Each location keeps its own scroll position, as ArkUIApp's per-location Scroller map does.
    if (currentLocation_ < static_cast<int>(pageScrollByLocation_.size())) {
        pageScrollByLocation_[static_cast<size_t>(currentLocation_)] = pageScroll_.Position();
    }
    // The old sky fades into the new one rather than hard-cutting (task #13's ArkUI behaviour).
    BeginBackgroundSwitch();
    currentLocation_ = index;
    scene_ = scenes_[static_cast<size_t>(index)];
    dailyTab_ = 0;
    hourlyTab_ = 0;
    dailyOverflowOpen_ = false;
    hourlyOverflowOpen_ = false;
    dailyScroll_.Stop();
    hourlyScroll_.Stop();
    RebuildSeries();
    implementor_ = CreateImplementor(scene_.kind, scene_.daylight, scene_.darkMode,
                                     ContentWidth(), static_cast<double>(height_));
    ReseedRotators();
    backgroundValid_ = false;
    enter_.Reset();
    StartTemperatureCountUp();
    pageScroll_.Drag(-pageScroll_.Position());
    if (index < static_cast<int>(pageScrollByLocation_.size())) {
        pageScroll_.Drag(pageScrollByLocation_[static_cast<size_t>(index)]);
    }
    ScrollDailyToToday();
    // The new location may have a different sky, so the status bar's icons follow it.
    ReportTheme();
}

/**
 * DailyViewHolder.kt:156-158 â€” the daily list opens scrolled to today.
 *
 * Written as an absolute position, not a relative drag: a drag adds to wherever the list already
 * was, so swapping locations or refreshing twice would push it a further column along each time.
 */
void Renderer::ScrollDailyToToday() {
    dailyScroll_.Drag(-dailyScroll_.Position());
    /*
     * ABSOLUTE, not a relative Drag.
     *
     * This runs on every SetScene for the page on screen, and a cold start alone calls it twice â€”
     * once for the cached body and once for the fresh one â€” so a relative drag put the list two
     * days past today, and any further apply pushed it further. `Stop()` then `Drag()` from zero is
     * the absolute set the Scroller has no method for.
     */
    dailyScroll_.Stop();
    dailyScroll_.Drag(-dailyScroll_.Position());
    if (scene_.weather.todayIndex > 0) {
        dailyScroll_.Drag(scene_.weather.todayIndex * dimens::kTrendItemWidth * DisplayDensity());
    }
}

void Renderer::SetScene(int index, int count, SceneState scene) {
    /*
     * True when the data belongs to a location the pager is not showing.
     *
     * It used to be an early `return`, which was right for the main screen and wrong for screen 2:
     * the list draws a row per location, so a background location's forecast is exactly what its
     * row is waiting for, and returning early left that row stale until the list was reopened.
     */
    bool background = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        /*
         * The theme is DERIVED here, not carried in with the data.
         *
         * ThemeManager.isLightTheme is a function of the settings AND the location's daylight, so a
         * scene cannot know its own theme at the moment it is parsed — and if it did, changing the
         * dark mode would need every location refetched to re-theme. `daylight` is the datum;
         * `darkMode` is computed from it, here and again in RefreshThemes when a setting changes.
         */
        scene.darkMode = !IsLightTheme(scene.daylight);
        locationCount_ = std::max(1, count);
        if (static_cast<int>(scenes_.size()) < locationCount_) {
            scenes_.resize(static_cast<size_t>(locationCount_));
            pageScrollByLocation_.resize(static_cast<size_t>(locationCount_), 0.0);
        }
        if (index >= 0 && index < static_cast<int>(scenes_.size())) {
            scenes_[static_cast<size_t>(index)] = scene;
        }
        refreshing_ = false;
        // A background location's data lands without disturbing what is on screen.
        background = (index != currentLocation_);
        if (!background) {
            // A refresh that changes the sky cross-fades it, same as a location switch.
            if (scene_.hasWeather &&
                (scene_.kind != scene.kind || scene_.daylight != scene.daylight ||
                 scene_.darkMode != scene.darkMode)) {
                BeginBackgroundSwitch();
            }
            scene_ = std::move(scene);
            dailyTab_ = 0;
            hourlyTab_ = 0;
            RebuildSeries();
            implementor_ = CreateImplementor(scene_.kind, scene_.daylight, scene_.darkMode,
                                             ContentWidth(),
                                             static_cast<double>(height_));
            ReseedRotators();
            backgroundValid_ = false;
            // A refresh is a fresh page, so the cards enter again â€” markEntered's guard is per page.
            enter_.Reset();
            StartTemperatureCountUp();
            ScrollDailyToToday();
            pageScroll_.SetRange(std::max(0.0, ContentHeight() - ViewportHeight()));
        }
    }
    if (!background) {
        // MainActivity.updateSystemBarStyle â€” a refresh that flips day to night restyles the bar.
        ReportTheme();
    }
    /*
     * A refresh that lands while screen 2 is up has to reach the rows too.
     *
     * ManagementFragment collects `validLocationList` for exactly this reason (kt:571-576): its
     * rows are a view onto the same list the pager reads, so a forecast arriving updates the row's
     * condition text and its alert colour underneath the user. Without this the list kept whatever
     * it was built with when it opened.
     */
    bool listOpen = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        listOpen = (screen_ == Screen::LocationList);
        if (screen_ != Screen::Main) {
            // Rebuilt even under the SEARCH screen, which the list is behind: a location added
            // there is refreshed immediately, and the row it lands in is the one the user sees the
            // moment the search closes. Does not lock, and does not change which screen is up.
            RebuildLocationRows();
        }
    }
    // A background location on the main screen has changed nothing that is on the display.
    if (!background || listOpen) {
        RenderFrame();
    }
}

/**
 * HomeFragment.kt:212-215 â€” the page dots: white for the current location, white at 50 % for the
 * rest. They sit over the footer at the bottom of the page, not inside the scrolling column.
 */
void Renderer::DrawIndicator(OH_Drawing_Canvas* canvas) const {
    if (locationCount_ < 2) {
        return;
    }
    const float density = DisplayDensity();
    const double dot = 8.0 * density;
    const double gap = 8.0 * density;
    const double total = locationCount_ * dot + (locationCount_ - 1) * gap;
    double x = (ContentWidth() - total) / 2.0;
    const double y = ViewportHeight() - dimens::kNormalMargin * density - dot;

    for (int i = 0; i < locationCount_; ++i) {
        FillRoundRect(canvas, x, y, x + dot, y + dot, dot / 2.0,
                      i == currentLocation_ ? 0xFFFFFFFF : 0x80FFFFFF);
        x += dot + gap;
    }
}

/**
 * The toolbar's â‹® menu â€” one item, "Open in another app" (activity_main.xml:11-16,
 * strings.xml:475), anchored under the button's trailing edge. Drawn over everything, in screen
 * coordinates, because the toolbar itself never scrolls.
 */
void Renderer::DrawToolbarMenu(OH_Drawing_Canvas* canvas) {
    if (!toolbarMenuOpen_) {
        return;
    }
    const float density = DisplayDensity();
    const double padding = dimens::kSmallMargin * density;
    const double iconSize = dimens::kMaterialIconSize * density;
    const double rowHeight = 48.0 * density;

    TextStyleSpec style;
    style.sizeVp = dimens::kContentTextSize;
    style.color = kBodyText.Resolve(scene_.darkMode);
    style.singleLine = true;
    const std::string label = "Open in another app";
    const double textWidth = MeasureTextWidth(label, style);
    const double menuWidth = padding * 3.0 + iconSize + textWidth;

    const double right = ContentWidth() - padding;
    const double left = right - menuWidth;
    const double top = dimens::kActionBarHeight * density;
    const double bottom = top + rowHeight + padding;
    FillCard(canvas, left, top, right, bottom, dimens::kCardCornerRadius / 2.0 * density,
             kMainCardBackground.Resolve(scene_.darkMode), density);

    const double rowTop = top + padding / 2.0;
    DrawAsset(canvas, "ic_open_in_new.svg", left + padding, rowTop + (rowHeight - iconSize) / 2.0,
              iconSize, style.color, true);
    const double textHeight = MeasureTextHeight(label, textWidth + 1.0, style);
    DrawText(canvas, label, left + padding + iconSize + padding,
             rowTop + (rowHeight - textHeight) / 2.0, textWidth + 1.0, style);

    menuItemLeft_ = left;
    menuItemTop_ = top;
    menuItemRight_ = right;
    menuItemBottom_ = bottom;
}

/** The pull-to-refresh spinner: a ring that grows with the pull and spins while refreshing. */
void Renderer::DrawRefreshIndicator(OH_Drawing_Canvas* canvas) const {
    if (overscroll_ <= 0.0 && !refreshing_) {
        return;
    }
    const float density = DisplayDensity();
    const double trigger = kRefreshTriggerVp * density;
    const double barHeight = dimens::kActionBarHeight * density;
    const double size = 28.0 * density;
    const double centreX = ContentWidth() / 2.0;
    // While refreshing it parks just under the toolbar; while pulling it follows the finger.
    const double centreY = refreshing_ ? barHeight + size
                                       : barHeight + std::min(overscroll_, trigger * 1.5);
    const double fraction = refreshing_ ? 1.0 : std::min(overscroll_ / trigger, 1.0);

    OH_Drawing_Pen* pen = OH_Drawing_PenCreate();
    OH_Drawing_PenSetAntiAlias(pen, true);
    OH_Drawing_PenSetColor(pen, WithAlpha(ContentColor(), fraction));
    OH_Drawing_PenSetWidth(pen, static_cast<float>(3.0 * density));
    OH_Drawing_PenSetCap(pen, LINE_ROUND_CAP);
    OH_Drawing_Rect* rect = OH_Drawing_RectCreate(
        static_cast<float>(centreX - size / 2.0), static_cast<float>(centreY - size / 2.0),
        static_cast<float>(centreX + size / 2.0), static_cast<float>(centreY + size / 2.0));
    OH_Drawing_CanvasAttachPen(canvas, pen);
    // The ring grows with the pull and, once the refresh is running, spins.
    const double startAngle = -90.0 + (refreshing_ ? spinnerAngle_ : 0.0);
    OH_Drawing_CanvasDrawArc(canvas, rect, static_cast<float>(startAngle),
                             static_cast<float>(300.0 * fraction));
    OH_Drawing_CanvasDetachPen(canvas);
    OH_Drawing_RectDestroy(rect);
    OH_Drawing_PenDestroy(pen);
}

/**
 * A chip tap. The lists keep their own scroll position across a tab change, as they do on
 * Android â€” the adapter is swapped under the same RecyclerView, not replaced.
 */
/**
 * The detail screen a trend tab navigates to — TrendTab.ets's third column, itself each adapter's
 * `onItemClicked` argument. "Sunshine" is the odd one: DailySunshineAdapter.kt:151 labels it that
 * but navigates to cloud cover (kt:123).
 */
static DetailTag DetailTagForTab(TrendTabId id) {
    switch (id) {
        case TrendTabId::Temperature:
            return DetailTag::Conditions;
        case TrendTabId::AirQuality:
            return DetailTag::AirQuality;
        case TrendTabId::Wind:
            return DetailTag::Wind;
        case TrendTabId::UvIndex:
            return DetailTag::UvIndex;
        case TrendTabId::Precipitation:
            return DetailTag::Precipitation;
        case TrendTabId::Sunshine:
            return DetailTag::CloudCover;
        case TrendTabId::FeelsLike:
            return DetailTag::FeelsLike;
        case TrendTabId::Humidity:
            return DetailTag::Humidity;
        case TrendTabId::Pressure:
            return DetailTag::Pressure;
        case TrendTabId::CloudCover:
            return DetailTag::CloudCover;
        case TrendTabId::Visibility:
            return DetailTag::Visibility;
    }
    return DetailTag::Conditions;
}

/** The detail screen a half-size block opens — DetailBlocks.ets's `detailScreen` per block. */
static DetailTag DetailTagForBlock(const std::string& blockId) {
    if (blockId == "precipitation") {
        return DetailTag::Precipitation;
    }
    if (blockId == "wind") {
        return DetailTag::Wind;
    }
    if (blockId == "air_quality") {
        return DetailTag::AirQuality;
    }
    if (blockId == "pollen") {
        return DetailTag::Pollen;
    }
    if (blockId == "humidity") {
        return DetailTag::Humidity;
    }
    if (blockId == "uv") {
        return DetailTag::UvIndex;
    }
    if (blockId == "visibility") {
        return DetailTag::Visibility;
    }
    if (blockId == "pressure") {
        return DetailTag::Pressure;
    }
    // sun and moon both land on the ephemeris screen (AstroViewHolder's item click).
    if (blockId == "sun" || blockId == "moon") {
        return DetailTag::SunMoon;
    }
    return DetailTag::Conditions;
}

bool Renderer::HandleTap(double x, double y) {
    const double barHeight = dimens::kActionBarHeight * DisplayDensity();
    const double pageY = y - barHeight + pageScroll_.Position();

    // An open toolbar menu owns the tap outright: inside the item it navigates, anywhere else it
    // just dismisses â€” the same as tapping outside a bindMenu.
    if (toolbarMenuOpen_) {
        toolbarMenuOpen_ = false;
        if (navigate_ != nullptr && x >= menuItemLeft_ && x < menuItemRight_ &&
            y >= menuItemTop_ && y < menuItemBottom_) {
            NavigateLocked("Open in another app");
        }
        return true;
    }

    // Chips first, then the overflow rows the open menu added â€” both live in the same list, with
    // the â‹® button itself marked -1.
    for (const ChipBounds& chip : dailyChips_) {
        if (x >= chip.left && x < chip.right && pageY >= chip.top && pageY < chip.bottom) {
            if (chip.tabIndex == -1) {
                dailyOverflowOpen_ = !dailyOverflowOpen_;
                return true;
            }
            dailyOverflowOpen_ = false;
            if (chip.tabIndex == dailyTab_) {
                return true;
            }
            dailyTab_ = chip.tabIndex;
            RebuildSeries();
            return true;
        }
    }
    for (const ChipBounds& chip : hourlyChips_) {
        if (x >= chip.left && x < chip.right && pageY >= chip.top && pageY < chip.bottom) {
            if (chip.tabIndex == -1) {
                hourlyOverflowOpen_ = !hourlyOverflowOpen_;
                return true;
            }
            hourlyOverflowOpen_ = false;
            if (chip.tabIndex == hourlyTab_) {
                return true;
            }
            hourlyTab_ = chip.tabIndex;
            RebuildSeries();
            return true;
        }
    }
    // A tap anywhere else dismisses an open menu, as tapping outside a bindMenu does.
    if (dailyOverflowOpen_ || hourlyOverflowOpen_) {
        dailyOverflowOpen_ = false;
        hourlyOverflowOpen_ = false;
        return true;
    }

    const double density = DisplayDensity();

    /*
     * AlertViewHolder.kt:79-81 — the whole alert card opens the alert screen; kt:147-151 — a
     * single alert ROW opens it scrolled to that alert. Which row was hit is recomputed from the
     * same geometry `DrawAlertCard` lays out: the current alerts, one fixed two-line ListItem each,
     * starting a small margin below the card's top.
     */
    if (!scene_.weather.alertList.empty() && pageY >= alertTop_ && pageY < alertBottom_) {
        const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();
        std::vector<const Alert*> current;
        for (const Alert& alert : scene_.weather.alertList) {
            if (!alert.hasStart || alert.startDate <= nowMs) {
                current.push_back(&alert);
            }
        }
        std::string alertId;
        if (!current.empty()) {
            const double rowTop = alertTop_ + dimens::kSmallMargin * density;
            const double rowHeight = dimens::kListItemTwoLineHeight * density;
            const int row = static_cast<int>((pageY - rowTop) / rowHeight);
            if (row >= 0 && row < static_cast<int>(current.size())) {
                alertId = current[static_cast<size_t>(row)]->alertId;
            }
        }
        OpenAlertListLocked(alertId);
        return true;
    }

    if (navigate_ == nullptr) {
        return false;
    }

    // The toolbar is fixed, so its taps are in screen coordinates, not page ones.
    if (y < barHeight) {
        const double iconSize = dimens::kMaterialIconSize * density;
        const double normalMargin = dimens::kNormalMargin * density;
        const double smallMargin = dimens::kSmallMargin * density;
        // MainActivity.kt:916-918 / HomeFragment.kt:193-207.
        if (x < normalMargin + iconSize) {
            // HomeFragment.kt:193-197 â†’ setManagementFragmentVisibility(true). Screen 2 is real
            // now, so this opens it rather than announcing a placeholder.
            OpenLocationList();
        } else if (x > ContentWidth() - smallMargin - iconSize) {
            // The â‹® opens its menu; the single showAsAction=never item lives inside it
            // (activity_main.xml:11-16), it is not a direct action.
            toolbarMenuOpen_ = true;
        } else if (x > ContentWidth() - smallMargin - iconSize * 2 - normalMargin) {
            OpenEditDialogLocked();
        } else {
            return false;
        }
        return true;
    }

    // A trend column opens its tab's detail screen (AbsDailyTrendAdapter's item click).
    const double columnWidth = dimens::kTrendItemWidth * density;
    // The list starts at the card's content edge: the page's own 8 dp inset plus the card's.
    const double cardLeft = PageInset() + dimens::kSmallMargin * density;
    if (pageY >= dailyListTop_ && pageY < dailyListBottom_) {
        const int index = static_cast<int>((x - cardLeft + dailyScroll_.Position()) / columnWidth);
        // Screen 4. A daily column IS the day, so the pager opens on that page directly.
        OpenDetailsLocked(DetailTagForTab(DailyTabs()[dailyTab_].id), index);
        return true;
    }
    if (pageY >= hourlyListTop_ && pageY < hourlyListBottom_) {
        const int index = static_cast<int>((x - cardLeft + hourlyScroll_.Position()) / columnWidth);
        // An hourly column belongs to a day; the pager opens on the day that hour falls in.
        int day = scene_.weather.todayIndex < 0 ? 0 : scene_.weather.todayIndex;
        if (index >= 0 && index < static_cast<int>(scene_.weather.nextHourlyForecast.size())) {
            const int64_t start =
                LocalStartOfDay(scene_.weather.nextHourlyForecast[static_cast<size_t>(index)].date,
                                scene_.utcOffsetSeconds);
            for (size_t i = 0; i < scene_.weather.dailyForecast.size(); ++i) {
                if (LocalStartOfDay(scene_.weather.dailyForecast[i].date,
                                    scene_.utcOffsetSeconds) == start) {
                    day = static_cast<int>(i);
                    break;
                }
            }
        }
        OpenDetailsLocked(DetailTagForTab(HourlyTabs()[hourlyTab_].id), day);
        return true;
    }

    /*
     * A detail block opens its own screen (e.g. HumidityViewHolder.kt:89-95).
     *
     * Hit-tested against `layout_` rather than recomputed from a row height and a half-width
     * column. The arithmetic version assumed two blocks to a row and the default card order, so it
     * opened the wrong screen after a drag-reorder and would have opened the wrong one outright in
     * landscape, where MainLayoutManager puts four across. `layout_` is the geometry that was
     * actually drawn, so it cannot disagree with what the finger is pointing at.
     */
    for (const PageItem& item : layout_) {
        if (!item.halfWidth || x < item.left || x > item.left + item.width ||
            pageY < item.top || pageY > item.top + item.height) {
            continue;
        }
        const std::string blockId = item.id.substr(std::string("block:").size());
        for (const DetailBlock& block : detailBlocks_) {
            if (block.id == blockId) {
                // Screen 4. A tile carries no day of its own, so it opens on today.
                OpenDetailsLocked(DetailTagForBlock(block.id),
                                  scene_.weather.todayIndex < 0 ? 0 : scene_.weather.todayIndex);
                return true;
            }
        }
    }
    return false;
}

/**
 * Routes a finger to the page or to a list.
 *
 * The axis is decided once, at the touch slop, and held for the rest of the gesture â€” which is
 * what ArkUI's Scroll-inside-List arbitration does for you and what keeps a mostly-vertical drag
 * from nudging the trend list sideways.
 */
/**
 * Screen 2's gesture handling, kept apart from the main screen's.
 *
 * Coordinates arrive in RAW surface pixels here â€” the list lays itself out from `topInset_` rather
 * than being drawn translated past it, so unlike OnTouch below there is nothing to subtract.
 */
void Renderer::OnLocationListTouch(const TouchSample& sample) {
    const double slop = 8.0 * DisplayDensity();  // ViewConfiguration's touch slop.
    LocationListHit hit;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        switch (sample.phase) {
            case TouchSample::Phase::Down:
                listDownX_ = sample.x;
                listDownY_ = sample.y;
                listLastY_ = sample.y;
                listDragged_ = false;
                /*
                 * A press on the drag handle IS the reorder â€” LocationHolder.kt:129-135 hands the
                 * drag to the ItemTouchHelper on ACTION_DOWN, with no long press and no slop â€” so
                 * it claims the gesture here rather than competing with the scroll for it.
                 */
                // Holds the weather scene still for the length of this gesture; see ListScrolling().
                listTouchDown_ = true;
                if (locationList_.OnHandle(sample.x, sample.y)) {
                    listGesture_ = ListGesture::Drag;
                    hit = locationList_.BeginDrag(locationList_.RowAt(sample.x, sample.y));
                    break;
                }
                listGesture_ = ListGesture::Undecided;
                return;

            case TouchSample::Phase::Move: {
                const double dx = sample.x - listDownX_;
                const double dy = sample.y - listDownY_;
                if (listGesture_ == ListGesture::Undecided) {
                    if (std::max(std::fabs(dx), std::fabs(dy)) < slop) {
                        return;  // Still inside the slop: nothing has been claimed yet.
                    }
                    /*
                     * Horizontal wins the swipe, vertical wins the scroll â€” the same arbitration
                     * ItemTouchHelper makes against the RecyclerView, and the reason ArkUIApp's
                     * PanGesture is direction-limited.
                     */
                    if (std::fabs(dx) > std::fabs(dy) &&
                        locationList_.RowAt(listDownX_, listDownY_) >= 0) {
                        listGesture_ = ListGesture::Swipe;
                        locationList_.BeginSwipe(locationList_.RowAt(listDownX_, listDownY_));
                    } else {
                        listGesture_ = ListGesture::Scroll;
                    }
                    listDragged_ = true;
                }
                switch (listGesture_) {
                    case ListGesture::Swipe:
                        locationList_.UpdateSwipe(dx);
                        break;
                    case ListGesture::Drag:
                        locationList_.UpdateDrag(dy);
                        break;
                    case ListGesture::Scroll:
                        locationList_.ScrollBy(sample.y - listLastY_);
                        break;
                    case ListGesture::Undecided:
                        break;
                }
                listLastY_ = sample.y;
                break;
            }

            case TouchSample::Phase::Up:
                if (listGesture_ == ListGesture::Swipe) {
                    hit = locationList_.EndSwipe();
                } else if (listGesture_ == ListGesture::Drag) {
                    hit = locationList_.EndDrag();
                } else if (!listDragged_) {
                    // A press that never passed the slop is a tap â€” the list has no fling, matching
                    // a RecyclerView that is only a handful of rows tall.
                    hit = locationList_.HitTest(listDownX_, listDownY_);
                }
                listGesture_ = ListGesture::Undecided;
                listTouchDown_ = false;
                break;

            case TouchSample::Phase::Cancel:
                listDragged_ = false;
                listGesture_ = ListGesture::Undecided;
                listTouchDown_ = false;
                locationList_.CancelGestures();
                break;
        }
    }

    // Acted on OUTSIDE the lock, because CloseLocationList takes it itself.
    bool paneSelection = false;
    switch (hit.action) {
        case LocationListAction::Select:
            {
                // SelectLocation does NOT lock - it is written to be called from an already-locked
                // context (HandleTap), so it gets one here rather than racing the vsync thread.
                std::lock_guard<std::mutex> lock(mutex_);
                SelectLocation(hit.index);
                /*
                 * In two-pane there is nothing to close: the list is a PANE beside the page, not a
                 * screen over it, so picking a row changes which weather the page shows and the
                 * list stays where it is. `screen_` was never anything but Main.
                 *
                 * But the ROW MARKER still has to move. `selected` is baked into each row by
                 * RebuildLocationRows, and returning here without it left the page showing one
                 * location while the list went on highlighting the previous one - Berlin's weather
                 * beside a highlighted Helsinki, measured on device. Closing the screen used to
                 * hide that: the rows were rebuilt on the way back in.
                 */
                if (locationList_.PaneMode()) {
                    // No damage call to go with it: damage drives the screen-2 overlay's partial
                    // frames, and a pane lives in the main branch, which repaints in full.
                    RebuildLocationRows();
                    paneSelection = true;
                }
            }
            if (paneSelection) {
                // Outside the lock: RenderFrame takes renderMutex_ and the draw reads state under
                // mutex_ itself.
                RenderFrame();
                return;
            }
            CloseLocationList();
            return;
        case LocationListAction::Close:
            CloseLocationList();
            return;
        case LocationListAction::Add:
            OpenLocationSearch();
            return;
        case LocationListAction::AddCurrentPosition:
            /*
             * kt:236-252 â€” the my-location FAB does NOT add. It opens the weather-sources dialog on
             * a fresh `Location(isCurrentPosition = true)` with no coordinates, and the location is
             * saved only if that dialog is. The position fix comes later, on the first refresh
             * (RefreshHelper.getLocation STEP 1), which is the shell's job.
             */
            {
                std::lock_guard<std::mutex> lock(mutex_);
                SourceLocation fresh;
                fresh.isCurrentPosition = true;
                // strings.xml `location_current`; the row shows the same until an address lands.
                sourcesDialog_.OpenForAdd(fresh, "Current location", ApplyDefaultPreset(fresh));
            }
            RenderFrame();
            return;
        case LocationListAction::Navigate:
            // kt:205 — startSettingsActivity. Screen 3's root is real now, so the gear opens it
            // rather than announcing itself; every other destination is still a placeholder.
            if (hit.destination == "Settings") {
                OpenSettings();
                return;
            }
            if (navigate_ != nullptr) {
                Navigate(hit.destination);
            }
            return;
        case LocationListAction::Delete:
            {
                // The scene list is the renderer's, so it is renumbered here rather than waiting
                // for the shell to hand every scene back.
                std::lock_guard<std::mutex> lock(mutex_);
                RemoveScene(hit.index);
                RebuildLocationRows();
                /*
                 * SnackbarHelper.showSnackbar(location_message_deleted, action_undo, ...).
                 *
                 * The shell used to raise this as a `promptAction.showDialog`, which is modal and
                 * waits indefinitely; Android's floats over the list, blocks nothing, and expires
                 * after three seconds — and letting it expire is how the delete is confirmed.
                 * Drawn here rather than rebuilt in ArkTS so this port keeps rendering its own UI.
                 */
                undoIndex_ = hit.index;
                snackbar_.Show("Location deleted", "Undo");  // strings.xml :439, :495
            }
            RenderFrame();
            if (listEdit_ != nullptr) {
                listEdit_("delete|" + std::to_string(hit.index));
            }
            /*
             * No explicit frame request: the loop is already running, started from touch-down in
             * DispatchTouchEventCB, and OnVSync now keeps it alive for as long as the bar has
             * something to animate. There is no renderer-side hook to start it — that lives in
             * napi_init's FrameDriver — so a delete arriving outside a gesture could not raise one
             * anyway, and a delete is always the tail of a swipe.
             */
            return;
        case LocationListAction::Move:
            {
                std::lock_guard<std::mutex> lock(mutex_);
                MoveScene(hit.index, hit.dropIndex);
                RebuildLocationRows();
            }
            RenderFrame();
            if (listEdit_ != nullptr) {
                listEdit_("move|" + std::to_string(hit.index) + "|" +
                          std::to_string(hit.dropIndex));
            }
            return;
        case LocationListAction::OpenSources:
            OpenSourcesForRow(hit.index);
            return;
        case LocationListAction::Refuse:
            // The row springs back and the reason is announced, exactly as the Kotlin's snackbar
            // does; `navigate_` is the shell's toast.
            RenderFrame();
            if (navigate_ != nullptr) {
                Navigate(hit.destination);
            }
            return;
        case LocationListAction::Redraw:
            RenderFrame();
            return;
        case LocationListAction::None:
            break;
    }
    if (sample.phase == TouchSample::Phase::Move &&
        (listDragged_ || listGesture_ == ListGesture::Drag)) {
        RenderFrame();
    }
}

/**
 * The weather-sources dialog owns the whole gesture while it is up, over whichever screen opened it.
 */
void Renderer::OnSourcesDialogTouch(const TouchSample& sample) {
    const double slop = 8.0 * DisplayDensity();
    SourcesDialogHit hit;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        switch (sample.phase) {
            case TouchSample::Phase::Down:
                listDownX_ = sample.x;
                listDownY_ = sample.y;
                listLastY_ = sample.y;
                listDragged_ = false;
                return;
            case TouchSample::Phase::Move:
                if (std::fabs(sample.y - listDownY_) > slop) {
                    listDragged_ = true;
                }
                if (listDragged_) {
                    sourcesDialog_.ScrollBy(sample.y - listLastY_);
                    listLastY_ = sample.y;
                }
                break;
            case TouchSample::Phase::Up:
                if (!listDragged_) {
                    hit = sourcesDialog_.OnTap(listDownX_, listDownY_);
                }
                break;
            case TouchSample::Phase::Cancel:
                listDragged_ = false;
                break;
        }
    }

    bool closed = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        closed = !sourcesDialog_.IsOpen();
    }
    // Leaving the dialog gives the search screen's keyboard back â€” but only if that is the screen
    // underneath, and only when the screen is not about to close anyway.
    if (closed && searchUi_ != nullptr && hit.action != SourcesDialogAction::Add) {
        bool searchScreen = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            searchScreen = (screen_ == Screen::LocationSearch);
        }
        if (searchScreen) {
            searchUi_(3);
        }
    }

    switch (hit.action) {
        case SourcesDialogAction::Add:
            /*
             * The screen is NOT closed here: a location the list already holds is REFUSED
             * (ManagementFragment.kt:471-476) and the search screen stays open so another hit can
             * be picked. The shell calls closeSearch() once the add really happened.
             */
            RenderFrame();
            if (pick_ != nullptr) {
                pick_(hit.encoded);
            }
            return;
        case SourcesDialogAction::Update:
            RenderFrame();
            if (listEdit_ != nullptr) {
                listEdit_("sources|" + std::to_string(hit.index) + "|" + hit.sources);
            }
            return;
        case SourcesDialogAction::Navigate:
            RenderFrame();
            if (navigate_ != nullptr) {
                Navigate(hit.destination);
            }
            return;
        case SourcesDialogAction::Dismiss:
        case SourcesDialogAction::Redraw:
            RenderFrame();
            return;
        case SourcesDialogAction::None:
            break;
    }
    if (sample.phase == TouchSample::Phase::Move && listDragged_) {
        RenderFrame();
    }
}

/**
 * Screen 2b's gesture handling â€” the same two gestures as the list, over a different hit test.
 *
 * The field itself is not a tap target: the IME is attached for as long as the screen is up, which
 * is what SearchActivity's `focusRequester.requestFocus()` (:127-133) does on entry.
 */
void Renderer::OnLocationSearchTouch(const TouchSample& sample) {
    const double slop = 8.0 * DisplayDensity();
    LocationSearchHit hit;
    // A dialog opening or closing changes whether the keyboard may stay up.
    bool dialogBefore = false;
    bool dialogAfter = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        dialogBefore = locationSearch_.DialogOpen();
        switch (sample.phase) {
            case TouchSample::Phase::Down:
                listDownX_ = sample.x;
                listDownY_ = sample.y;
                listLastY_ = sample.y;
                listDragged_ = false;
                return;
            case TouchSample::Phase::Move:
                if (std::fabs(sample.y - listDownY_) > slop) {
                    listDragged_ = true;
                }
                if (listDragged_) {
                    locationSearch_.ScrollBy(sample.y - listLastY_);
                    listLastY_ = sample.y;
                }
                break;
            case TouchSample::Phase::Up:
                if (!listDragged_) {
                    // OnTap is where the screen's own dialogs resolve; only what has to leave the
                    // screen comes back.
                    hit = locationSearch_.OnTap(listDownX_, listDownY_);
                }
                break;
            case TouchSample::Phase::Cancel:
                listDragged_ = false;
                break;
        }
        dialogAfter = locationSearch_.DialogOpen();
    }

    /*
     * The source chooser covers the bottom half of the screen, which is exactly where the keyboard
     * is â€” and where the chooser's own Cancel sits. The session is only HIDDEN, never detached:
     * re-attaching would throw the typed query away. (The weather-sources dialog does the same from
     * the Pick branch below, and gives the keyboard back in OnSourcesDialogTouch.)
     */
    if (searchUi_ != nullptr && dialogBefore != dialogAfter) {
        searchUi_(dialogAfter ? 2 : 3);
    }

    switch (hit.action) {
        case LocationSearchAction::Pick: {
            /*
             * SearchActivity.kt:212-223 â€” picking a hit opens SecondarySourcesPreference, preset by
             * Location.applyDefaultPreset, and only its `onClose(location)` finishes the activity
             * with a result. The dialog is the renderer's because screen 2 opens the same one.
             */
            {
                std::lock_guard<std::mutex> lock(mutex_);
                // ...preset by getLocationWithAppliedPreference, not the plain applyDefaultPreset:
                // a location added from SEARCH honours screen 3f's default source (kt:101-186).
                sourcesDialog_.OpenForAdd(
                    hit.location, hit.place,
                    ApplyDefaultSourcePreference(hit.location, defaultForecastSource_));
            }
            // A dialog covers the keyboard's half of the screen, and its own buttons with it.
            if (searchUi_ != nullptr) {
                searchUi_(2);
            }
            RenderFrame();
            return;
        }
        case LocationSearchAction::Navigate:
            RenderFrame();
            if (navigate_ != nullptr) {
                Navigate(hit.destination);
            }
            return;
        case LocationSearchAction::Redraw:
            RenderFrame();
            return;
        case LocationSearchAction::None:
            break;
    }
    if (sample.phase == TouchSample::Phase::Move && listDragged_) {
        RenderFrame();
    }
}

void Renderer::OnTouch(const TouchSample& raw) {
    Screen screen = Screen::Main;
    bool dialog = false;
    bool prefsOpen = false;
    {
        // Read under the lock: `screen_` is written by HandleTap and the two Close calls, and this
        // runs on the UI thread while the vsync thread is inside the renderer.
        std::lock_guard<std::mutex> lock(mutex_);
        screen = screen_;
        dialog = sourcesDialog_.IsOpen();
        prefsOpen = prefsDialog_.IsOpen();
    }

    /*
     * The snackbar's action is above everything, and it is the ONLY part of the bar that takes a
     * touch — everything else passes through to the list, exactly as Android's does. A snackbar
     * that swallowed touches would be the dialog behaviour this replaced.
     */
    if (raw.phase == TouchSample::Phase::Up) {
        bool undo = false;
        int index = -1;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (snackbar_.HitAction(raw.x, raw.y)) {
                undo = true;
                index = undoIndex_;
                snackbar_.Leave();
                undoIndex_ = -1;
            }
        }
        if (undo) {
            // CancelDeleteListener (kt:138-145) — addLocation(location, index). The shell owns the
            // list and the cached forecast, so it does the restoring and calls back with `insert`.
            if (listEdit_ != nullptr && index >= 0) {
                listEdit_("undo|" + std::to_string(index));
            }
            RenderFrame();
            return;
        }
    }

    // A dialog is above both screens, so it is tested before either of them. The sources dialog
    // stacks ABOVE the edit dialog, so it is asked first.
    if (dialog) {
        OnSourcesDialogTouch(raw);
        return;
    }
    if (prefsOpen) {
        OnPrefsDialogTouch(raw);
        return;
    }
    if (screen == Screen::MainSettings) {
        OnMainSettingsTouch(raw);
        return;
    }
    if (screen == Screen::Notifications) {
        OnNotificationsTouch(raw);
        return;
    }
    if (screen == Screen::Debug) {
        OnDebugTouch(raw);
        return;
    }
    if (screen == Screen::WorkerInfo) {
        OnWorkerInfoTouch(raw);
        return;
    }
    if (screen == Screen::Modules) {
        OnModulesTouch(raw);
        return;
    }
    if (screen == Screen::BackgroundUpdates) {
        OnBackgroundUpdatesTouch(raw);
        return;
    }
    if (screen == Screen::About) {
        OnAboutTouch(raw);
        return;
    }
    if (screen == Screen::PrivacyPolicy) {
        OnPrivacyPolicyTouch(raw);
        return;
    }
    if (screen == Screen::Dependencies) {
        OnDependenciesTouch(raw);
        return;
    }
    if (screen == Screen::AlertList) {
        OnAlertListTouch(raw);
        return;
    }
    if (screen == Screen::PreviewIcon) {
        OnPreviewIconTouch(raw);
        return;
    }
    if (screen == Screen::Location) {
        OnLocationTouch(raw);
        return;
    }
    if (screen == Screen::WeatherSources) {
        OnWeatherSourcesTouch(raw);
        return;
    }
    if (screen == Screen::DisplayManage) {
        OnDisplayManageTouch(raw);
        return;
    }
    if (screen == Screen::Details) {
        OnDetailsTouch(raw);
        return;
    }
    if (screen == Screen::Placeholder) {
        // One target on the screen, and only on release.
        if (raw.phase == TouchSample::Phase::Up && placeholder_.HitBack(raw.x, raw.y)) {
            ClosePlaceholder();
        }
        return;
    }
    if (screen == Screen::Units) {
        OnUnitsTouch(raw);
        return;
    }
    if (screen == Screen::Appearance) {
        OnAppearanceTouch(raw);
        return;
    }
    if (screen == Screen::Settings) {
        OnSettingsTouch(raw);
        return;
    }
    if (screen == Screen::LocationList) {
        OnLocationListTouch(raw);
        return;
    }
    if (screen == Screen::LocationSearch) {
        OnLocationSearchTouch(raw);
        return;
    }
    /*
     * In two-pane the list is on screen beside the page, so a touch has to be given to whichever
     * one it landed on. Left of the pane's edge belongs to the list; the rest belongs to the page,
     * which is drawn translated and therefore has to be told where its own origin is.
     */
    double pane = 0.0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pane = PaneWidth();
    }
    if (pane > 0.0 && raw.x < pane) {
        OnLocationListTouch(raw);
        return;
    }
    std::unique_lock<std::mutex> lock(mutex_);
    // The page is drawn translated past the status bar and past the pane, so the finger moves with
    // it on both axes.
    TouchSample sample = raw;
    sample.y -= topInset_;
    sample.x -= pane;
    const float density = DisplayDensity();
    /** ViewConfiguration's 8 dp touch slop. */
    const double slop = 8.0 * density;
    /*
     * Set when a tap left the main screen for screen 2, because that transition is the ONE state
     * change on this screen that nothing else will paint.
     *
     * Every other tap here â€” a tab, a menu, a page â€” is drawn by the frame loop, which is running
     * because the weather background wants frames. Opening the list stops that loop dead: OnVSync
     * returns false for screen 2 before it reaches its RenderFrame, deliberately, so the background
     * is not still animating underneath a screen that covers it. The list was therefore entered and
     * never drawn â€” the last main-screen frame simply stayed on the display while every touch went
     * to the list underneath it.
     */
    bool openedList = false;

    switch (sample.phase) {
        case TouchSample::Phase::Down:
            axis_ = Axis::Undecided;
            activeList_ = nullptr;
            downX_ = lastX_ = sample.x;
            downY_ = lastY_ = sample.y;
            lastTimeMs_ = sample.timeMs;
            velocityX_ = 0.0;
            velocityY_ = 0.0;
            touchDown_ = true;
            touchDownAtMs_ = sample.timeMs;
            // Touching down stops whatever was still gliding, as it does on Android.
            pageScroll_.Stop();
            dailyScroll_.Stop();
            hourlyScroll_.Stop();
            UpdateNowcastMarker(sample.x, sample.y);
            break;

        case TouchSample::Phase::Move: {
            const double dx = sample.x - lastX_;
            const double dy = sample.y - lastY_;
            const double dt = std::max(1.0, sample.timeMs - lastTimeMs_) / 1000.0;
            // The marker follows the finger across the buckets, as Vico's does. It does not claim
            // the gesture — a drag that becomes a scroll still scrolls, and the marker goes with it.
            UpdateNowcastMarker(sample.x, sample.y);

            // A drag in progress owns the gesture outright: the card follows the finger and the
            // drop index is recomputed from where it is now.
            if (draggingIndex_ >= 0) {
                dragDX_ += dx;
                dragDY_ += dy;
                const double barHeight = dimens::kActionBarHeight * DisplayDensity();
                const int target = DropIndexAt(sample.x, sample.y - barHeight +
                                                             pageScroll_.Position());
                if (target >= 0) {
                    dropIndex_ = target;
                }
                lastX_ = sample.x;
                lastY_ = sample.y;
                lastTimeMs_ = sample.timeMs;
                return;
            }

            if (axis_ == Axis::Undecided) {
                const double totalX = std::fabs(sample.x - downX_);
                const double totalY = std::fabs(sample.y - downY_);
                if (std::max(totalX, totalY) < slop) {
                    return;  // Still inside the slop: nothing has been claimed yet.
                }
                if (totalX > totalY) {
                    activeList_ = ListAt(downX_, downY_);
                    // A horizontal drag that is not on a trend list belongs to the Swiper â€” it
                    // pages between saved locations (MainScreen.ets:558).
                    axis_ = Axis::Horizontal;
                } else {
                    axis_ = Axis::Vertical;
                }
                // Starting to drag dismisses any open menu, as touching outside a bindMenu does.
                toolbarMenuOpen_ = false;
                dailyOverflowOpen_ = false;
                hourlyOverflowOpen_ = false;
            }

            // A blend, so one jittery sample cannot decide the fling.
            const double weight = 0.7;
            if (axis_ == Axis::Horizontal && activeList_ != nullptr) {
                activeList_->Drag(-dx);
                velocityX_ = velocityX_ * (1.0 - weight) + (-dx / dt) * weight;
            } else if (axis_ == Axis::Vertical) {
                // Dragging down at the very top over-scrolls instead, which is what arms
                // pull-to-refresh (the Refresh wrapper at MainScreen.ets:626).
                if (pageScroll_.Position() <= 0.0 && dy > 0.0) {
                    overscroll_ = std::min(overscroll_ + dy * 0.5, kRefreshTriggerVp * 2.0 *
                                                                      DisplayDensity());
                } else {
                    if (overscroll_ > 0.0) {
                        overscroll_ = std::max(0.0, overscroll_ + dy * 0.5);
                    }
                    pageScroll_.Drag(-dy);
                }
                velocityY_ = velocityY_ * (1.0 - weight) + (-dy / dt) * weight;
            }
            lastX_ = sample.x;
            lastY_ = sample.y;
            lastTimeMs_ = sample.timeMs;
            break;
        }

        case TouchSample::Phase::Up:
            touchDown_ = false;
            // The marker disappears with the finger.
            nowcastMarker_ = -1;
            if (draggingIndex_ >= 0) {
                EndDrag();
                axis_ = Axis::Undecided;
                return;
            }
            if (axis_ == Axis::Horizontal && activeList_ != nullptr) {
                activeList_->Fling(velocityX_);
            } else if (axis_ == Axis::Horizontal) {
                // A swipe past a quarter of the width, or a decisive flick, pages the Swiper.
                const double travel = sample.x - downX_;
                const double threshold = ContentWidth() / 4.0;
                if (travel < -threshold || velocityX_ > 800.0) {
                    SelectLocation(currentLocation_ + 1);
                } else if (travel > threshold || velocityX_ < -800.0) {
                    SelectLocation(currentLocation_ - 1);
                }
            } else if (axis_ == Axis::Vertical) {
                // Released past the trigger distance, the pull becomes a refresh.
                if (overscroll_ >= kRefreshTriggerVp * DisplayDensity() && refresh_ != nullptr &&
                    !refreshing_) {
                    refreshing_ = true;
                    refresh_(currentLocation_);
                }
                overscroll_ = 0.0;
                pageScroll_.Fling(velocityY_);
            } else {
                // Never left the slop, so it was a tap rather than a drag.
                HandleTap(sample.x, sample.y);
                openedList = (screen_ != Screen::Main);
            }
            axis_ = Axis::Undecided;
            activeList_ = nullptr;
            break;

        case TouchSample::Phase::Cancel:
            touchDown_ = false;
            nowcastMarker_ = -1;
            EndDrag();
            axis_ = Axis::Undecided;
            activeList_ = nullptr;
            break;
    }

    if (openedList) {
        // RenderFrame takes `mutex_` itself, so it can only be called with this one released â€”
        // the same contract CloseLocationList follows on the way back.
        lock.unlock();
        RenderFrame();
    }
}

/**
 * True while the user is touching the screen at all, or a trend list is still gliding.
 *
 * Deliberately wider than "a trend list is under the finger": the axis is not decided until the
 * finger has travelled 8 dp, so the first frames of every gesture would otherwise still be free to
 * spend 23 ms repainting the scene â€” and those are the frames that set the tone for how the drag
 * feels. A vertical page scroll is unaffected either way, because there the scroll fade genuinely
 * moves and forces the repaint through `backgroundRate_`.
 */
bool Renderer::ListScrolling() const {
    /*
     * `listTouchDown_` is the PANE's gesture, and it belongs here for the same reason the trend
     * lists do.
     *
     * This reads `touchDown_`, which is the PAGE's flag — a touch on the pane never sets it, so in
     * two-pane the scene went on repainting at its 30 fps bank underneath a list being scrolled.
     * Measured: 563 frames of a pane scroll, every one full, 46.6 fps with p90 52 ms, against a
     * portrait list that gets no frame loop at all and scrolls for free. Holding the scene still
     * for the gesture is exactly the trade already made for the trend lists, and the banked time is
     * spent the moment the finger lifts, so the particles land where the wall clock says.
     */
    return touchDown_ || listTouchDown_ || dailyScroll_.IsAnimating() ||
           hourlyScroll_.IsAnimating();
}

/**
 * Everything that can change a pixel OUTSIDE the two trend lists, folded into one number.
 *
 * Deliberately over-inclusive. A value left out of here is a value that could change without the
 * frame noticing, and the symptom would be a stale strip of screen â€” so anything animated, anything
 * a gesture touches, and anything the layout depends on is in it. The two lists' own scroll
 * positions are the only things NOT here; they are what a partial frame is allowed to redraw.
 */
uint64_t Renderer::PageSignature() const {
    const auto mix = [](uint64_t hash, uint64_t value) {
        return hash * 1099511628211ULL ^ (value + 0x9e3779b97f4a7c15ULL);
    };
    const auto quantise = [](double value) {
        return static_cast<uint64_t>(static_cast<int64_t>(std::llround(value * 16.0)));
    };
    uint64_t hash = 1469598103934665603ULL;
    hash = mix(hash, quantise(pageScroll_.Position()));
    hash = mix(hash, quantise(overscroll_));
    hash = mix(hash, backgroundPaintedTick_);
    hash = mix(hash, quantise(backgroundRate_));
    hash = mix(hash, seriesGeneration_);
    hash = mix(hash, static_cast<uint64_t>(dailyTab_) * 64ULL + static_cast<uint64_t>(hourlyTab_));
    hash = mix(hash, static_cast<uint64_t>(currentLocation_) * 8ULL +
                         (toolbarMenuOpen_ ? 1 : 0) + (dailyOverflowOpen_ ? 2 : 0) +
                         (hourlyOverflowOpen_ ? 4 : 0));
    hash = mix(hash, static_cast<uint64_t>(draggingIndex_ + 1) * 1024ULL +
                         static_cast<uint64_t>(dropIndex_ + 1));
    hash = mix(hash, quantise(dragDX_) ^ (quantise(dragDY_) << 20));
    hash = mix(hash, refreshing_ ? quantise(spinnerAngle_) + 1ULL : 0ULL);
    hash = mix(hash, quantise(DisplayedTemperature()));
    hash = mix(hash, quantise(switchProgress_));
    hash = mix(hash, quantise(topInset_) ^ (quantise(bottomInset_) << 20));
    hash = mix(hash, quantise(contentHeight_) ^ (static_cast<uint64_t>(layout_.size()) << 40));
    hash = mix(hash, static_cast<uint64_t>(ContentWidth()) * 65536ULL + static_cast<uint64_t>(height_));
    return hash;
}

/**
 * The band a list-only frame redraws, in surface pixels.
 *
 * Only granted when a trend list is genuinely the one thing moving: nothing else animating, the
 * previous frame complete and at the same page state, and the band's geometry already measured by
 * that frame. The band is the whole trend CARD, not just its list, so the card's rounded edges,
 * shadow and side margins are all inside it.
 */
void Renderer::RecordFrameBand() {
    frameBands_.push_back(FrameBand{frameIndex_, frameDirtyTop_, frameDirtyBottom_});
    if (frameBands_.size() > kFrameBandHistory) {
        frameBands_.erase(frameBands_.begin(),
                          frameBands_.begin() +
                              static_cast<long>(frameBands_.size() - kFrameBandHistory));
    }
}

bool Renderer::StaleBandFor(uint64_t bufferFrameIndex, double* top, double* bottom) const {
    if (frameBands_.empty()) {
        return false;
    }
    // Every frame from the one after this buffer was written must be in the history, or there is
    // no way to know what it missed.
    if (frameBands_.front().frame > bufferFrameIndex + 1) {
        return false;
    }
    double lo = 0.0;
    double hi = 0.0;
    bool any = false;
    for (const FrameBand& band : frameBands_) {
        if (band.frame <= bufferFrameIndex || band.bottom <= band.top) {
            continue;
        }
        if (!any) {
            lo = band.top;
            hi = band.bottom;
            any = true;
            continue;
        }
        lo = std::min(lo, band.top);
        hi = std::max(hi, band.bottom);
    }
    if (!any) {
        // The buffer already holds this frame's content; nothing to copy.
        *top = 0.0;
        *bottom = 0.0;
        return true;
    }
    *top = lo;
    *bottom = hi;
    return true;
}

bool Renderer::PartialBand(double* top, double* bottom) const {
    if (!lastFrameWasComplete_ || !ListScrolling() || enter_.Running() || draggingIndex_ >= 0 ||
        refreshing_ || switchProgress_ < 1.0 || overscroll_ != 0.0 ||
        tempAnimMs_ < tempAnimDurationMs_ || PageSignature() != lastPageSignature_) {
        return false;
    }
    const bool dailyMoving = dailyScroll_.IsAnimating() ||
                             (touchDown_ && activeList_ == &dailyScroll_);
    const bool hourlyMoving = hourlyScroll_.IsAnimating() ||
                              (touchDown_ && activeList_ == &hourlyScroll_);
    if (!dailyMoving && !hourlyMoving) {
        return false;
    }
    // The bands were recorded in the scrolling column's coordinates by the last frame.
    const double barHeight = dimens::kActionBarHeight * DisplayDensity();
    const double toSurface = topInset_ + barHeight - pageScroll_.Position();
    const double margin = dimens::kSmallMargin * DisplayDensity() * 2.0;
    double lo = 1.0e9;
    double hi = -1.0e9;
    if (dailyMoving) {
        lo = std::min(lo, dailyListTop_ + toSurface - margin);
        hi = std::max(hi, dailyListBottom_ + toSurface + margin);
    }
    if (hourlyMoving) {
        lo = std::min(lo, hourlyListTop_ + toSurface - margin);
        hi = std::max(hi, hourlyListBottom_ + toSurface + margin);
    }
    if (hi <= lo) {
        return false;
    }
    *top = std::max(0.0, std::floor(lo));
    *bottom = std::min(static_cast<double>(height_), std::ceil(hi));
    return *bottom > *top;
}

int Renderer::BlocksPerRow() const {
    const float density = DisplayDensity();
    if (width_ == 0 || density <= 0.0f) {
        return 2;
    }
    const double units = ContentWidth() / density / kDpPerBlockUnit;
    const auto clamp5 = [](double value) {
        return std::max(1, std::min(5, static_cast<int>(std::floor(value))));
    };
    const int potential = clamp5(units / kMinBlockWidth);
    if (potential > 2) {
        return clamp5(units / (kMinBlockWidth * kSquishedBlockFactor));
    }
    return potential;
}

Scroller* Renderer::ListAt(double /*x*/, double y) {
    // The lists span the full width, so only the row matters. The bands were recorded in the
    // scrolling column's own coordinates, so the touch has to be moved into them.
    const double barHeight = dimens::kActionBarHeight * DisplayDensity();
    const double pageY = y - barHeight + pageScroll_.Position();
    if (pageY >= dailyListTop_ && pageY < dailyListBottom_) {
        return &dailyScroll_;
    }
    if (pageY >= hourlyListTop_ && pageY < hourlyListBottom_) {
        return &hourlyScroll_;
    }
    return nullptr;
}

bool Renderer::OnVSync(double seconds) {
    bool wantsMore = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        /*
         * Screen 2 covers the weather: nothing behind it should be advancing or repainting.
         *
         * This returns BEFORE the RenderFrame at the end, so the list gets no frame from the loop at
         * all â€” by design, it is a static surface. Every state change on it therefore has to paint
         * itself: the tap that opens it (OnTouch), a scroll (OnLocationListTouch), a forecast
         * landing under it (SetScene), and the tap that leaves it (CloseLocationList).
         */
        if (screen_ != Screen::Main) {
            /*
             * TWO exceptions: a swiped row springing home, and the delete snackbar.
             *
             * The list is otherwise static, but ItemTouchHelper animates the row back over
             * DefaultItemAnimator's 250 ms and this port used to snap it. The loop runs for exactly
             * as long as that animation, and the frames it produces are banded to the one row. The
             * snackbar is the second thing on this screen with a clock: it slides in, waits three
             * seconds and leaves, and nothing else would drive it.
             */
            const bool settling = screen_ == Screen::LocationList && locationList_.Settling();
            const bool bar = snackbar_.Visible();
            /*
             * THREE exceptions now: screen 4's two flings are the third.
             *
             * Screen 4 is as static as the list between gestures, but its day pager and its content
             * both carry a fling, and a fling is a clock. Without a frame source the row moved only
             * while the finger was down and stopped dead on lift — which is exactly what it did.
             */
            // Screen 4's two flings AND its condition icon, which is a clock of its own.
            const bool gliding = screen_ == Screen::Details && details_.Advance(seconds);
            /*
             * FOUR now: the icon preview's animatable dialog is a clock too.
             *
             * That screen is otherwise as static as the list, but AnimatableIconView loops its
             * animator set for as long as the dialog is up, and the pose this port draws is a pure
             * function of elapsed time — so without a frame source the icon would sit at step 0.
             * `Advance` also carries the screen's own scroll fling, which had no frame source
             * either and so stopped dead on lift exactly as screen 4's used to.
             */
            const bool preview = screen_ == Screen::PreviewIcon && previewIcon_.Advance(seconds);
            if (!settling && !bar && !gliding && !preview) {
                return false;
            }
            wantsMore = gliding || preview;
            if (settling) {
                wantsMore = locationList_.AdvanceSettle(seconds) || wantsMore;
            }
            if (bar) {
                wantsMore = snackbar_.Advance(seconds) || wantsMore;
            }
        } else {
        wantsMore |= pageScroll_.Advance(seconds);
        wantsMore |= dailyScroll_.Advance(seconds);
        wantsMore |= hourlyScroll_.Advance(seconds);
        wantsMore |= enter_.Advance(seconds);

        /*
         * The PANE's swiped row springs home from here.
         *
         * The settle is otherwise advanced by the screen-2 branch above, which two-pane never takes
         * - the list is a pane inside the MAIN screen, so `screen_` is Main throughout. Without
         * this the swipe ran its action and the row then stayed parked at its swipe offset for
         * ever: measured on device as a row left showing its swipe background and gear after the
         * sources dialog had been opened and dismissed.
         */
        if (TwoPane() && locationList_.Settling()) {
            wantsMore |= locationList_.AdvanceSettle(seconds);
        }
        // And the snackbar, which in two-pane is raised over the main screen rather than over a
        // list screen. Same reasoning: nothing else has a clock for it.
        if (snackbar_.Visible()) {
            wantsMore = snackbar_.Advance(seconds) || wantsMore;
        }

        // The spinner turns while a refresh is in flight.
        if (refreshing_) {
            spinnerAngle_ = std::fmod(spinnerAngle_ + seconds * 360.0, 360.0);
            wantsMore = true;
        }
        // The header count-up (NumberAnimTextView's linear ramp).
        if (tempAnimMs_ < tempAnimDurationMs_) {
            tempAnimMs_ += seconds * 1000.0;
            wantsMore = true;
        }
        // The 300 ms background cross-fade; the outgoing scene keeps animating through it.
        if (switchProgress_ < 1.0) {
            switchProgress_ = std::min(1.0, switchProgress_ + seconds * 1000.0 / 300.0);
            if (prevImplementor_ != nullptr) {
                prevImplementor_->UpdateData(ContentWidth(), static_cast<double>(height_),
                                             seconds * 1000.0, sceneRotation2D_,
                                             sceneRotation3D_);
            }
            if (switchProgress_ >= 1.0) {
                prevImplementor_.reset();
                prevValid_ = false;
            }
            ++backgroundTick_;
            wantsMore = true;
        }

        /*
         * The long press fires from here rather than from a move, because a finger held perfectly
         * still sends no move events â€” which is exactly how a drag starts. 500 ms is
         * ViewConfiguration's long-press timeout, the same one ItemTouchHelper uses.
         */
        if (touchDown_ && draggingIndex_ < 0 && axis_ == Axis::Undecided) {
            heldMs_ += seconds * 1000.0;
            if (heldMs_ >= 500.0) {
                const double barHeight = dimens::kActionBarHeight * DisplayDensity();
                StartDrag(downX_, downY_ - barHeight + pageScroll_.Position());
            }
            wantsMore = true;
        } else if (!touchDown_) {
            heldMs_ = 0.0;
        }
        if (draggingIndex_ >= 0) {
            wantsMore = true;
        }
        /*
         * A finger on the screen keeps the loop running for the whole gesture.
         *
         * DispatchTouchEvent used to call RenderFrame itself, on the UI thread, for every MOVE.
         * A traced fling showed those calls taking 35-66 ms each: the touch thread was painting
         * the entire page, unaligned to vsync, while the frame loop painted it again â€” two full
         * repaints per frame contending on one mutex, with touch samples backing up behind them.
         * That is what made the trend lists feel worse than ArkUI's, which never paints off the
         * vsync beat. Touch now only updates scroll state; this flag makes sure the loop is there
         * to draw it, exactly once per frame.
         */
        if (touchDown_) {
            wantsMore = true;
        }
        if (implementor_ != nullptr && BackgroundScrollRate() < 1.0) {
            /*
             * The particles step at their own rate, not the display's.
             *
             * ArkUIApp drives the same implementors from a displaySync loop at 30 fps, and
             * MaterialWeatherView feeds IntervalComputer whatever interval its own invalidate loop
             * produced rather than one step per vsync. Stepping every vsync means a full CPU
             * repaint of the scene at 120 Hz, and the scene is by far the most expensive thing on
             * the page â€” so the interval is banked and spent 30 times a second, which is also what
             * makes the cached background worth having.
             */
            backgroundAccumMs_ = std::min(backgroundAccumMs_ + seconds * 1000.0, kBackgroundBankMs);
            /*
             * The frame counter is not redundant with the interval, and leaving it out was tried
             * and measured: a repaint costs ~23 ms, so the vsync AFTER a repaint always arrives
             * 33.3 ms later, which is exactly the 30 fps interval â€” the scene re-armed itself on
             * every single frame and the cache never hit (9 of 349 frames). Requiring three
             * intervening frames breaks that feedback and caps the scene at one repaint in four.
             *
             * `ListScrolling()` holds the scene still for the length of a trend-list gesture. A
             * scene repaint is 23 ms of CPU rasterising against a ~12 ms frame, so one landing
             * mid-fling is the whole of the remaining judder â€” three short frames then a long one,
             * four times a second. Android never has to make this trade because MaterialPainterView
             * is a hardware-accelerated View; here the elapsed time is BANKED (capped, so the
             * resume is a step and not a jump) and spent the moment the list comes to rest, which
             * puts the particles exactly where the wall clock says they should be.
             */
            if (backgroundAccumMs_ >= kBackgroundFrameMs && !ListScrolling() &&
                backgroundFramesSince_ >= kBackgroundMinFrames) {
                /*
                 * kt:251-253 — the rotators are advanced by the real elapsed time whatever the
                 * animation setting says, then read back as the angles this advance draws at.
                 *
                 * They ride the same banked interval as the particles rather than every vsync, so
                 * the lean is held still for the length of a trend-list gesture exactly as the
                 * scene is. Android has no equivalent pause because its painter view is
                 * hardware-accelerated; the reason for the trade is at the top of this block.
                 */
                rotator2D_.UpdateRotation(gravitySensor_.Rotation2D(), backgroundAccumMs_);
                rotator3D_.UpdateRotation(gravitySensor_.Rotation3D(), backgroundAccumMs_);
                sceneRotation2D_ = rotator2D_.Rotation();
                sceneRotation3D_ = rotator3D_.Rotation();

                // kt:255-262 — animations off: advance the shapes once, then hold them. The
                // rotators above are untouched by this, which is what lets the scene still lean.
                double dataInterval = backgroundAccumMs_;
                if (backgroundAnimationMode_ == "disabled") {
                    if (sceneHasDrawn_) {
                        dataInterval = 0.0;
                    } else {
                        sceneHasDrawn_ = true;
                    }
                }
                implementor_->UpdateData(ContentWidth(), static_cast<double>(height_),
                                         dataInterval, sceneRotation2D_, sceneRotation3D_);
                backgroundAccumMs_ = 0.0;
                backgroundFramesSince_ = 0;
                ++backgroundTick_;
            } else {
                ++backgroundFramesSince_;
            }
            // MaterialPainterView.kt:286-296 stops the loop once the cards cover the background,
            // which is what keeps a scrolled-down page off the animation budget entirely.
            wantsMore = true;
        }
        }
    }
    RenderFrame();
    return wantsMore;
}

bool Renderer::WantsFrames() const {
    std::lock_guard<std::mutex> lock(mutex_);
    /*
     * Screen 2 and 2b are static. Without this the weather background's own frame loop kept driving
     * full repaints of the list â€” MaterialWeatherView stops when it is not visible, and it is not
     * visible while the management screen covers it.
     */
    if (screen_ != Screen::Main) {
        // Except while a swiped row is springing home, or the delete snackbar is on screen — the
        // two animations screen 2 has.
        if (snackbar_.Visible()) {
            return true;
        }
        return screen_ == Screen::LocationList && locationList_.Settling();
    }
    if (snackbar_.Visible()) {
        return true;
    }
    // The pane carries that same spring-back into the main screen, where the background may
    // already have stopped asking for frames.
    if (TwoPane() && locationList_.Settling()) {
        return true;
    }
    /*
     * screen 3c's `main_screen_background` (kt:127-138). No scene at all means no frames — the
     * same reason screens 2 and 2b above ask for none.
     */
    if (!mainScreenBackground_) {
        return false;
    }
    /*
     * screen 3c's `background_animation_mode` (kt:165-186). "system" is the default and means
     * animate; there is no HarmonyOS reduced-motion flag for it to consult.
     *
     * Animations off does NOT mean no frames when the gravity sensor is on: upstream keeps
     * reposting regardless (kt:286-296) and only freezes the SHAPES, so the scene still leans with
     * the phone. The rotators also have to keep running for a moment after the sensor is switched
     * off, to carry the lean back down to level instead of snapping.
     */
    if (backgroundAnimationMode_ == "disabled" && !gravitySensor_.IsEnabled() &&
        sceneRotation2D_ == 0.0 && sceneRotation3D_ == 0.0) {
        return false;
    }
    return implementor_ != nullptr && BackgroundScrollRate() < 1.0;
}

uint32_t Renderer::ContentColor() const {
    return IsLightBackground(scene_.daylight, scene_.darkMode) ? kSurfaceInverseLight
                                                               : kSurfaceInverseDark;
}

/*
 * fragment_home.xml:23-35 plus res/menu/activity_main.xml â€” a transparent bar of ?attr/actionBarSize,
 * the ic_list navigation icon at NORMAL_MARGIN, the title after another NORMAL_MARGIN, and the edit
 * and overflow icons at the trailing edge. Both menu items stay hidden until a location has loaded
 * (HomeFragment.kt:204-205, 279-284), which here means until there is weather to show.
 */
void Renderer::DrawToolbar(OH_Drawing_Canvas* canvas) const {
    if (scene_.city.empty()) {
        return;
    }
    const float density = DisplayDensity();
    const double barHeight = dimens::kActionBarHeight * density;
    const double iconSize = dimens::kMaterialIconSize * density;
    const double normalMargin = dimens::kNormalMargin * density;
    const double smallMargin = dimens::kSmallMargin * density;
    const uint32_t color = ContentColor();
    const double iconTop = (barHeight - iconSize) / 2.0;

    DrawAsset(canvas, "ic_list.svg", normalMargin, iconTop, iconSize, color, true);

    double trailing = ContentWidth() - smallMargin;
    if (scene_.hasWeather) {
        trailing -= iconSize;
        DrawAsset(canvas, "ic_more_vert.svg", trailing, iconTop, iconSize, color, true);
        trailing -= normalMargin + iconSize;
        DrawAsset(canvas, "ic_edit.svg", trailing, iconTop, iconSize, color, true);
        trailing -= normalMargin;
    }

    /*
     * The Toolbar's own `titleMarginStart` (4 dp) on top of the navigation button's 56 dp.
     *
     * MEASURED on the reference, not guessed: the navigation ImageButton occupies [0,182] at
     * 3.25 px/dp and the title TextView starts at 195. Neither fragment_home.xml nor the project's
     * styles set a title margin, so it is the Material toolbar default. Both ports placed the
     * title flush at 56 dp; ArkUIApp carries the same constant.
     */
    const double left = normalMargin + iconSize + normalMargin + 4.0 * density;
    TextStyleSpec style;
    style.sizeVp = dimens::kToolbarTitleTextSize;
    style.color = color;
    style.singleLine = true;

    const double titleWidth = trailing - left;
    /*
     * getPlace prints cityAndDistrict (Location.kt:36-37); the district rides the details line.
     *
     * HomeFragment.kt:413-416 prefixes the title with U+2299 for a current position on a PHONE
     * ("on the left on mobile because it might not be visible on small displays otherwise"; a
     * tablet gets it as a suffix instead, and this port is phone-only). The list row already did
     * this; the toolbar did not, so the NDK read "Helsinki" where Android and ArkUIApp read
     * "\xE2\x8A\x99 Helsinki". Written as escapes for the reason given at the list-row site.
     */
    std::string title = PlaceFor(currentLocation_, scene_.city);
    if (IsCurrentPositionAt(currentLocation_)) {
        title = "\xE2\x8A\x99 " + title;
    }
    const double textHeight = MeasureTextHeight(title, titleWidth, style);
    // The bar centres its title vertically.
    DrawText(canvas, title, left, (barHeight - textHeight) / 2.0, titleWidth, style);
}

/*
 * container_main_header.xml, top to bottom:
 *   refresh time  14sp, 8dp bottom margin                          (:21-37)
 *   condition     textAppearanceTitleLarge bold, 8dp top margin    (:58-70)
 *   temperature   140sp value with a 50sp unit beside it,
 *                 value -10dp top margin, unit 5dp down            (:77-106)
 *   feels like    titleLarge bold, -10dp top, 8dp bottom           (:109-123)
 *   range         titleMedium bold                                 (:125-137)
 * Everything is centred, inside 8dp side padding.
 */
double Renderer::DrawHeader(OH_Drawing_Canvas* canvas) const {
    if (!scene_.hasWeather || !scene_.weather.hasCurrent) {
        return 0.0;
    }
    const float density = DisplayDensity();
    const double padding = dimens::kSmallMargin * density;
    // The header is an item of the main list, so it sits inside that list's own 8 dp padding
    // (FitTabletRecyclerView.kt:33-34) and then applies its own 8 dp on top â€” the reference's
    // refresh-time row measures x 52..1208, i.e. 16 dp in, not 8.
    const double left = PageInset() + padding;
    const double contentWidth = PageContentWidth() - padding * 2;
    const uint32_t color = ContentColor();
    const Current& current = scene_.weather.current;

    // The header is the first child of the scrolling column, so it starts at its top.
    double y = 0.0;

    const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();

    // Refresh time. Unlike everything below it this row is LEFT aligned â€” container_main_header
    // gives it a full-width Row, whose default gravity is start, while the texts under it are
    // centred in the column (:21-37 against :58-137).
    TextStyleSpec caption;
    caption.sizeVp = dimens::kContentTextSize;
    caption.color = color;
    caption.singleLine = true;
    const std::string refreshText = FormatRelativeTime(scene_.weather.refreshTime, nowMs);
    if (!refreshText.empty()) {
        const double iconSize = dimens::kContentTextSize * density;
        const double gap = 6.0 * density;  // :30 â€” the drawable padding before the text
        const double textHeight = MeasureTextHeight(refreshText, contentWidth, caption);
        // The row centres the icon against the text.
        DrawAsset(canvas, "ic_time.svg", left, y + (textHeight - iconSize) / 2.0, iconSize,
                  color, true);
        DrawText(canvas, refreshText, left + iconSize + gap, y,
                 contentWidth - iconSize - gap, caption);
        y += textHeight + dimens::kSmallMargin * density;
    }

    // Weather condition.
    TextStyleSpec titleLarge;
    titleLarge.sizeVp = dimens::kTitleLargeTextSize;
    titleLarge.color = color;
    titleLarge.bold = true;
    titleLarge.alignment = TextAlignment::Center;
    if (!current.weatherText.empty()) {
        y += dimens::kSmallMargin * density;
        y += DrawText(canvas, current.weatherText, left, y, contentWidth, titleLarge);
    }

    // Big temperature with its unit beside it. The two are laid out as a row and centred as a
    // group, so each is measured and placed by hand rather than centred independently.
    if (current.temperature.temperature.has_value()) {
        TextStyleSpec big;
        big.sizeVp = dimens::kMainTitleTextSize;
        big.color = color;
        // container_main_header.xml:82-83 â€” the VALUE carries textStyle="bold" and
        // textFontWeight="700" just as the unit beside it does; the port only set it on the unit.
        big.bold = true;

        TextStyleSpec unit;
        unit.sizeVp = dimens::kTemperatureUnitTextSize;
        unit.color = color;
        unit.bold = true;

        // The number the count-up currently shows, not the final value straight away.
        const std::string value =
            std::to_string(static_cast<long long>(DisplayedTemperature()));
        const std::string unitLabel = TemperatureUnitLabel();
        const double valueWidth = MeasureTextWidth(value, big);
        const double unitWidth = MeasureTextWidth(unitLabel, unit);
        const double startX =
            PageInset() + (PageContentWidth() - valueWidth - unitWidth) / 2.0;

        y -= 10.0 * density;
        const double valueHeight = DrawText(canvas, value, startX, y, valueWidth + 1.0, big);
        DrawText(canvas, unitLabel, startX + valueWidth, y + 5.0 * density, unitWidth + 1.0, unit);
        y += valueHeight;
    }

    const std::string feelsLike = FeelsLikeText(current.temperature);
    if (!feelsLike.empty()) {
        y -= 10.0 * density;
        y += DrawText(canvas, feelsLike, left, y, contentWidth, titleLarge);
        y += dimens::kSmallMargin * density;
    }

    const std::string range =
        TemperatureRangeSummary(scene_.weather, nowMs, scene_.utcOffsetSeconds);
    if (!range.empty()) {
        TextStyleSpec titleMedium;
        titleMedium.sizeVp = dimens::kTitleMediumTextSize;
        titleMedium.color = color;
        titleMedium.bold = true;
        titleMedium.alignment = TextAlignment::Center;
        titleMedium.singleLine = true;
        y += DrawText(canvas, range, left, y, contentWidth, titleMedium);
    }

    // container_main_header.xml:12 â€” 16 dp of bottom padding before the first card.
    return y + dimens::kNormalMargin * density;
}

std::vector<std::string> Renderer::EffectiveOrder() const {
    if (draggingIndex_ < 0 || dropIndex_ < 0 || dropIndex_ == draggingIndex_) {
        return order_;
    }
    std::vector<std::string> shifted = order_;
    const std::string lifted = shifted[static_cast<size_t>(draggingIndex_)];
    shifted.erase(shifted.begin() + draggingIndex_);
    shifted.insert(shifted.begin() + std::min<size_t>(dropIndex_, shifted.size()), lifted);
    return shifted;
}

/**
 * MainLayoutManager's span lookup: a full-width card takes a whole row, half-width blocks fill a
 * row `BlocksPerRow()` at a time, and a full-width card after a part-filled row starts a new one.
 */
void Renderer::LayOutItems(double topPx) {
    const float density = DisplayDensity();
    const double margin = dimens::kSmallMargin * density;
    const double pageLeft = PageInset();
    const double pageWidth = PageContentWidth();
    const int columns = BlocksPerRow();
    const double columnWidth = pageWidth / static_cast<double>(columns);
    const double blockSide = columnWidth - margin * 2.0;

    layout_.clear();
    double y = topPx;
    int filled = 0;  // Blocks already placed in the row being filled.
    double rowHeight = 0.0;

    for (const std::string& id : EffectiveOrder()) {
        const bool half = id.rfind("block:", 0) == 0;
        if (half) {
            PageItem item;
            item.id = id;
            item.halfWidth = true;
            item.left = pageLeft + filled * columnWidth + margin;
            item.top = y + margin;
            item.width = blockSide;
            item.height = blockSide;
            layout_.push_back(item);
            rowHeight = blockSide + margin * 2.0;
            if (++filled >= columns) {
                y += rowHeight;
                filled = 0;
            }
            continue;
        }
        if (filled > 0) {
            y += rowHeight;
            filled = 0;
        }
        PageItem item;
        item.id = id;
        item.left = pageLeft;
        item.top = y;
        item.width = pageWidth;
        item.height = DrawItem(nullptr, id, pageLeft, y, pageWidth);
        layout_.push_back(item);
        y += item.height;
    }
}

int Renderer::DropIndexAt(double x, double pageY) const {
    for (size_t i = 0; i < layout_.size(); ++i) {
        const PageItem& item = layout_[i];
        if (pageY < item.top || pageY >= item.top + item.height) {
            continue;
        }
        if (!item.halfWidth) {
            return static_cast<int>(i);
        }
        // A half-width slot has two drop points: before it, or after it.
        return static_cast<int>(i) + (x > item.left + item.width / 2.0 ? 1 : 0);
    }
    return -1;
}

void Renderer::StartDrag(double x, double pageY) {
    const int index = DropIndexAt(x, pageY);
    if (index < 0 || index >= static_cast<int>(order_.size())) {
        return;
    }
    draggingIndex_ = index;
    dropIndex_ = index;
    dragDX_ = 0.0;
    dragDY_ = 0.0;
    // The drag owns the gesture from here; nothing scrolls while a card is lifted.
    axis_ = Axis::Horizontal;
    activeList_ = nullptr;
}

/**
 * Takes the saved order. It is kept rather than applied: at restore time the id list does not
 * exist yet â€” it is built from the first refresh â€” so the merge in RebuildSeries is what actually
 * puts the cards back where the user left them.
 */
void Renderer::RestoreOrder(const std::string& order) {
    std::lock_guard<std::mutex> lock(mutex_);
    restoredOrder_.clear();
    size_t start = 0;
    while (start <= order.size()) {
        const size_t comma = order.find(',', start);
        const std::string id = order.substr(start, comma == std::string::npos
                                                       ? std::string::npos
                                                       : comma - start);
        if (!id.empty()) {
            restoredOrder_.push_back(id);
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    order_ = restoredOrder_;
}

/**
 * The page's card sequence: `card_display` in order, minus the cards this weather cannot fill.
 *
 * The per-card tests are MainAdapter.kt:110-171's `continue`s. Cards whose data is absent are left
 * OUT of the sequence but stay in the stored list, which is why PersistCardOrder merges rather than
 * overwrites.
 */
/** The stored id for a tab — DailyTrendDisplay.kt / HourlyTrendDisplay.kt's enum ids. */
static const char* TrendTabStoredId(TrendTabId id) {
    switch (id) {
        case TrendTabId::Temperature:
            return "temperature";
        case TrendTabId::AirQuality:
            return "air_quality";
        case TrendTabId::Wind:
            return "wind";
        case TrendTabId::UvIndex:
            return "uv_index";
        case TrendTabId::Precipitation:
            return "precipitation";
        case TrendTabId::Sunshine:
            return "sunshine";
        case TrendTabId::FeelsLike:
            return "feels_like";
        case TrendTabId::Humidity:
            return "humidity";
        // The three the hourly list adds (HourlyTrendDisplay.kt:36-38). Missing them here left
        // Pressure, Cloud cover and Visibility unmatched, so the hourly card built 7 tabs from a
        // stored list of 10 — the same class of bug as the hardcoded DisplayList::Daily above,
        // one layer further in.
        case TrendTabId::Pressure:
            return "pressure";
        case TrendTabId::CloudCover:
            return "cloud_cover";
        case TrendTabId::Visibility:
            return "visibility";
    }
    return "";
}

/**
 * The tabs a trend card actually shows: the STORED list's order, restricted to the tabs this port
 * draws. `DailyTrendAdapter`/`HourlyTrendAdapter` are built from the same two settings, so the two
 * manage screens decide both which tabs exist and in what order.
 *
 * An id this port has no series for is simply skipped — the NDK's hourly table is shorter than
 * ArkUIApp's, and that predates this change.
 */
std::vector<TrendTab> Renderer::TabsFor(DisplayList list, const std::vector<TrendTab>& all,
                                        const std::string& stored) const {
    std::vector<TrendTab> chosen;
    /*
     * `list` matters and used to be hardcoded to Daily, which was a real bug: the hourly card's
     * stored ids were parsed against the DAILY catalogue, so `humidity`, `pressure`, `cloud_cover`
     * and `visibility` matched nothing and were dropped. The card showed 6 tabs instead of 10 —
     * which is exactly what the overflow menu was reporting.
     */
    for (const DisplayItem& item : ParseDisplayList(list, stored)) {
        for (const TrendTab& tab : all) {
            if (item.id == TrendTabStoredId(tab.id)) {
                chosen.push_back(tab);
                break;
            }
        }
    }
    // A stored list naming nothing this port draws would leave the card with no tabs at all.
    return chosen.empty() ? all : chosen;
}

std::vector<TrendTab> Renderer::DailyTabs() const {
    return TabsFor(DisplayList::Daily, kDailyTabs, dailyTrendDisplay_);
}

std::vector<TrendTab> Renderer::HourlyTabs() const {
    return TabsFor(DisplayList::Hourly, kHourlyTabs, hourlyTrendDisplay_);
}

std::vector<std::string> Renderer::BuildCardOrder() const {
    std::vector<std::string> sequence;
    for (const DisplayItem& item : ParseDisplayList(DisplayList::Blocks, cardDisplay_)) {
        if (item.id == "nowcast") {
            if (scene_.weather.minutelyForecast.size() >= 3) {
                sequence.push_back("nowcast");
            }
            continue;
        }
        if (item.id == "daily_forecast") {
            if (!scene_.weather.dailyForecast.empty()) {
                sequence.push_back("daily");
            }
            continue;
        }
        if (item.id == "hourly_forecast") {
            if (!scene_.weather.hourlyForecast.empty()) {
                sequence.push_back("hourly");
            }
            continue;
        }
        // Every other entry is a half-size block, and BuildDetailBlocks has already applied that
        // card's own validity test — a block it did not build is a card Android would `continue` on.
        for (const DetailBlock& block : detailBlocks_) {
            if (block.id == item.id) {
                sequence.push_back("block:" + item.id);
                break;
            }
        }
    }
    return sequence;
}

/**
 * MainAdapter.onItemMove (kt:296-311) — a drop on the WEATHER screen writes the new order back to
 * `card_display`, the same setting the Blocks manage screen edits.
 *
 * The stored list also holds cards this weather cannot fill (and `clock`, which no port draws yet),
 * and those keep their positions: the merge walks the stored list and replaces only the entries
 * that are on the page, in the page's new order.
 */
void Renderer::PersistCardOrder() {
    std::vector<std::string> onPage;
    for (const std::string& id : order_) {
        if (id == "nowcast") {
            onPage.push_back("nowcast");
        } else if (id == "daily") {
            onPage.push_back("daily_forecast");
        } else if (id == "hourly") {
            onPage.push_back("hourly_forecast");
        } else if (id.rfind("block:", 0) == 0) {
            onPage.push_back(id.substr(6));
        }
    }
    const std::vector<DisplayItem> stored = ParseDisplayList(DisplayList::Blocks, cardDisplay_);
    std::vector<DisplayItem> merged;
    size_t next = 0;
    for (const DisplayItem& item : stored) {
        const bool onScreen = std::find(onPage.begin(), onPage.end(), item.id) != onPage.end();
        if (!onScreen) {
            merged.push_back(item);
            continue;
        }
        if (next < onPage.size()) {
            for (const DisplayItem& candidate : stored) {
                if (candidate.id == onPage[next]) {
                    merged.push_back(candidate);
                    break;
                }
            }
            ++next;
        }
    }
    const std::string value = DisplayListToValue(merged);
    if (value != cardDisplay_ && settingsEdit_ != nullptr) {
        cardDisplay_ = value;
        settingsEdit_(std::string("card_display|") + value);
    }
}

void Renderer::EndDrag() {
    if (draggingIndex_ >= 0 && dropIndex_ >= 0 && dropIndex_ != draggingIndex_) {
        order_ = EffectiveOrder();
        // kt:311 — the drop persists. It goes to `card_display` now, not to a private order file,
        // so the Blocks manage screen and this screen can no longer disagree.
        PersistCardOrder();
    }
    draggingIndex_ = -1;
    dropIndex_ = -1;
    dragDX_ = 0.0;
    dragDY_ = 0.0;
}

/** The colours both trend cards resolve from the theme and the current weather. */
TrendCardColors Renderer::CardColors() const {
    const bool darkMode = scene_.darkMode;
    // DailyTemperatureAdapter.kt:142-166 â€” the chart takes the weather theme's accent.
    const uint32_t accent = WeatherThemeAccent(scene_.kind, scene_.daylight);
    TrendCardColors colors;
    colors.cardBackground = kMainCardBackground.Resolve(darkMode);
    // container_main_*_trend_card.xml:21,31 â€” the title icon and title use ?attr/colorSurfaceInverse.
    colors.onSurfaceInverse = darkMode ? kSurfaceInverseDark : kSurfaceInverseLight;
    colors.accentText = kTitleText.Resolve(darkMode);
    colors.bodyText = kBodyText.Resolve(darkMode);
    colors.captionText = kCaptionText.Resolve(darkMode);
    colors.highLine = accent;
    colors.lowLine = WithAlpha(accent, 0.5);
    colors.subLine = kOutline.Resolve(darkMode);
    // setShadowColors (PolylineAndHistogramView.kt:404-417): the high colour at 15 % in light
    // themes, the low colour at 30 % in dark ones.
    colors.shadowTop = darkMode ? WithAlpha(colors.lowLine, trend::kShadowAlphaDark)
                                : WithAlpha(accent, trend::kShadowAlphaLight);
    colors.highText = kTitleText.Resolve(darkMode);
    colors.lowText = kBodyText.Resolve(darkMode);
    colors.histogramText = kPrecipitationProbability.Resolve(darkMode);
    colors.histogramAlpha = darkMode ? 0.5 : 0.2;
    colors.chipContainer = kSurfaceVariant.Resolve(darkMode);
    colors.chipSelectedContainer = kPrimary.Resolve(darkMode);
    colors.chipLabel = kOnSurfaceVariant.Resolve(darkMode);
    colors.chipSelectedLabel = kOnPrimary.Resolve(darkMode);
    // screen 3c's `trend_horizontal_line_switch` (kt:139-157).
    colors.keyLinesEnabled = trendHorizontalLines_;
    return colors;
}

/*
 * The scrolling column: the header, then the cards, in MainAdapter's order.
 *
 * Returns the total height, which is what the page scroller's range is derived from â€” measuring by
 * drawing keeps the two from ever disagreeing.
 */
double Renderer::DrawContent(OH_Drawing_Canvas* canvas) {
    double y = 0.0;
    pendingOverflow_ = false;
    {
        BREEZY_TRACE("breezy:Header");
        y = DrawHeader(canvas);
    }
    if (!scene_.hasWeather) {
        return y;
    }
    const TrendCardColors colors = CardColors();
    const float density = DisplayDensity();
    const double margin = dimens::kSmallMargin * density;

    PageCardColors pageColors;
    pageColors.cardBackground = colors.cardBackground;
    pageColors.onSurfaceInverse = colors.onSurfaceInverse;
    pageColors.bodyText = colors.bodyText;
    pageColors.line = colors.subLine;
    pageColors.label = kTextGrey.Resolve(scene_.darkMode);
    // getThemeColors(WEATHER_KIND_RAINY, â€¦)[0] â€” the nowcast columns are rain-themed whatever
    // the current weather is (PrecipitationNowcastViewHolder.kt:193-202).
    pageColors.column = WeatherThemeAccent(WeatherKind::Rainy, scene_.daylight);
    pageColors.alertBackground = kAlertBackground.Resolve(scene_.darkMode);
    // AlertViewHolder tints the headline and icon with onSurface, the dates with onSurfaceVariant.
    pageColors.alertText = kOnSurface.Resolve(scene_.darkMode);
    pageColors.alertDates = kOnSurfaceVariant.Resolve(scene_.darkMode);
    pageColors.markerBackground = kPrimary.Resolve(scene_.darkMode);
    pageColors.markerText = kOnPrimary.Resolve(scene_.darkMode);

    /*
     * MainAdapter.kt:107-109 â€” the alert card sits directly under the header, ahead of every
     * reorderable card. It is the ONLY card drawn here: it is not in `card_display` and so cannot
     * be reordered or switched off.
     *
     * The nowcast used to be drawn here too, and then AGAIN by `LayOutItems` â€” `BuildCardOrder`
     * puts "nowcast" in `order_` and `DrawCard` renders it, because the main screen obeys
     * `card_display` now. So the page carried TWO identical Precipitation nowcast cards whenever
     * there was precipitation to draw; `DrawNowcastCard` returns 0 on a dry forecast, which is why
     * this only ever appeared on a wet day and was written off as a stale-band artefact once.
     */
    const auto alertNowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::system_clock::now().time_since_epoch())
                                .count();
    {
        BREEZY_TRACE("breezy:AlertNowcast");
        alertTop_ = y;
        y += DrawAlertCard(canvas, PageInset(), y, PageContentWidth(), scene_.weather.alertList,
                           pageColors, scene_.utcOffsetSeconds, scene_.is24Hour, alertNowMs);
        alertBottom_ = y;
    }

    // Everything below the alert card is reorderable, so it is laid out from `order_` rather than
    // written out in sequence â€” MainAdapter.isDraggable (kt:274-285) draws exactly this line.
    {
        BREEZY_TRACE("breezy:LayOutItems");
        LayOutItems(y);
    }

    /*
     * Every card plays the staggered entrance. On Android they all extend AbstractMainViewHolder,
     * so they all get checkEnterScreen's cascade â€” not just the half-width blocks.
     */
    const auto beginEnter = [&](const std::string& id, double top) {
        OH_Drawing_CanvasSave(canvas);
        const double alpha = enter_.Apply(canvas, id, ContentWidth() / 2.0, top,
                                          density);
        if (alpha < 1.0) {
            // A layer is the only way to fade a whole card at once; ArkUI's .opacity() is the
            // same thing underneath.
            OH_Drawing_Brush* brush = OH_Drawing_BrushCreate();
            OH_Drawing_BrushSetAlpha(brush, static_cast<uint8_t>(alpha * 255.0));
            OH_Drawing_CanvasSaveLayer(canvas, nullptr, brush);
            OH_Drawing_BrushDestroy(brush);
            return 2;
        }
        return 1;
    };
    const auto endEnter = [&](int saves) {
        for (int i = 0; i < saves; ++i) {
            OH_Drawing_CanvasRestore(canvas);
        }
    };

    const std::string lifted =
        draggingIndex_ >= 0 ? order_[static_cast<size_t>(draggingIndex_)] : std::string{};

    /*
     * Only what is on screen is painted.
     *
     * The trend painter already culls columns outside its own card, but the CARD was painted
     * whatever the page scroll â€” so flinging the daily list at the top of the page also repainted
     * the hourly card's visible-in-card columns, entirely below the fold. The trace counted 25
     * column charts a frame where the daily card needs about seven.
     *
     * It is also what makes the entrance faithful. `EnterAnimation::Progress` claims a card's
     * cascade slot the first time it is asked for, and with every card painted from the first
     * frame every card claimed at once. AbstractMainViewHolder.checkEnterScreen (kt:61-79) runs
     * when a card actually reaches the screen, which is what ArkUIApp drives from
     * onVisibleAreaChange â€” so a card below the fold should enter when it is scrolled to.
     */
    const double barHeight = dimens::kActionBarHeight * density;
    const double viewTop = pageScroll_.Position() - overscroll_;
    const double viewBottom = viewTop + (ViewportHeight() - barHeight);

    for (const PageItem& item : layout_) {
        y = std::max(y, item.top + item.height);
        if (item.id == lifted) {
            continue;  // Drawn last, on top of everything, at the finger.
        }
        if (item.top >= viewBottom || item.top + item.height <= viewTop) {
            // Off screen. The touch bands still have to follow the layout, because they are read
            // in content coordinates and the card may be scrolled back into reach at any time.
            NoteTrendBands(item);
            continue;
        }
        BREEZY_TRACE(item.id == "daily"    ? "breezy:daily"
                     : item.id == "hourly" ? "breezy:hourly"
                                           : "breezy:block");
        const int saves = beginEnter(item.id, item.top);
        DrawItem(canvas, item.id, item.left, item.top, item.width);
        endEnter(saves);
    }

    // The dragged card rides above the page. Android lifts it with a bigger elevation; here the
    // translation alone reads as the lift, since every card already carries the same shadow.
    if (!lifted.empty()) {
        for (const PageItem& item : layout_) {
            if (item.id != lifted) {
                continue;
            }
            OH_Drawing_CanvasSave(canvas);
            OH_Drawing_CanvasTranslate(canvas, static_cast<float>(dragDX_),
                                       static_cast<float>(dragDY_));
            DrawItem(canvas, item.id, item.left, item.top, item.width);
            OH_Drawing_CanvasRestore(canvas);
            break;
        }
    }

    // MainAdapter.kt:173 appends the footer unconditionally, so it is the last row on every page.
    // It sits on the weather background, not on a card, so it takes the header's text colour.
    y += DrawFooter(canvas, PageInset(), y, PageContentWidth(), kForecastSourceName,
                    ContentColor());

    /*
     * Last of all, so it is above every card: the open tab overflow menu. Its rows are pushed back
     * into the same chip vector the card filled, which is what the tap handler reads â€” so the menu
     * has to be drawn in the frame that lays the chips out, not skipped when the card is culled.
     */
    if (pendingOverflow_) {
        DrawTrendOverflowMenu(canvas, pendingOverflowSpec_, pendingOverflowColors_,
                              pendingOverflowAnchor_, pendingOverflowWidth_,
                              pendingOverflowDaily_ ? &dailyChips_ : &hourlyChips_);
    }
    return y;
}

/**
 * Keeps a trend card's touch band in step with the layout without painting the card.
 *
 * The bands are in the scrolling column's own coordinates and are read by every touch, so they
 * cannot simply go stale while a card is culled â€” a fling can bring the card back under the finger
 * in the same gesture. The geometry is already in `layout_`, so nothing has to be measured.
 */
void Renderer::NoteTrendBands(const PageItem& item) {
    if (item.id != "daily" && item.id != "hourly") {
        return;
    }
    const float density = DisplayDensity();
    const double margin = dimens::kSmallMargin * density;
    const bool daily = item.id == "daily";
    const double listHeight =
        (daily ? dimens::kDailyTrendItemHeight : dimens::kHourlyTrendItemHeight) * density;
    const double listBottom = item.top + item.height - margin;
    if (daily) {
        dailyListBottom_ = listBottom;
        dailyListTop_ = listBottom - listHeight;
    } else {
        hourlyListBottom_ = listBottom;
        hourlyListTop_ = listBottom - listHeight;
    }
}

/**
 * Draws one reorderable item. A null canvas measures without drawing, which is how LayOutItems
 * learns a full-width card's height before anything is painted.
 */
double Renderer::DrawItem(OH_Drawing_Canvas* canvas, const std::string& id, double left,
                          double top, double width) {
    const float density = DisplayDensity();
    const double margin = dimens::kSmallMargin * density;
    const TrendCardColors colors = CardColors();

    if (id == "nowcast") {
        PageCardColors pageColors;
        pageColors.cardBackground = colors.cardBackground;
        pageColors.onSurfaceInverse = colors.onSurfaceInverse;
        pageColors.bodyText = colors.bodyText;
        pageColors.line = colors.subLine;
        pageColors.label = kTextGrey.Resolve(scene_.darkMode);
        pageColors.column = WeatherThemeAccent(WeatherKind::Rainy, scene_.daylight);
        pageColors.alertBackground = kAlertBackground.Resolve(scene_.darkMode);
        pageColors.alertText = kOnSurface.Resolve(scene_.darkMode);
        pageColors.alertDates = kOnSurfaceVariant.Resolve(scene_.darkMode);
        // The nowcast marker's pill (PrecipitationNowcastViewHolder.kt:165-167).
        pageColors.markerBackground = kPrimary.Resolve(scene_.darkMode);
        pageColors.markerText = kOnPrimary.Resolve(scene_.darkMode);
        return DrawNowcastCard(canvas, left, top, width, scene_.weather.minutelyForecast, pageColors,
                               scene_.utcOffsetSeconds, scene_.is24Hour, nowcastMarker_,
                               &nowcastChart_);
    }

    if (id == "daily" || id == "hourly") {
        const bool daily = id == "daily";
        const TrendSeries& series = daily ? dailySeries_ : hourlySeries_;
        if (series.items.empty()) {
            return 0.0;
        }
        TrendCardSpec spec;
        spec.title = daily ? "Daily forecast" : "Hourly forecast";  // strings.xml:7-8
        spec.titleIcon = daily ? "ic_calendar.svg" : "ic_schedule.svg";
        for (const TrendTab& tab : daily ? DailyTabs() : HourlyTabs()) {
            spec.tabs.push_back(tab.label);
        }
        spec.selectedTab = daily ? dailyTab_ : hourlyTab_;
        spec.listHeightVp =
            daily ? dimens::kDailyTrendItemHeight : dimens::kHourlyTrendItemHeight;
        spec.isDaily = daily;
        spec.overflowOpen = daily ? dailyOverflowOpen_ : hourlyOverflowOpen_;

        std::vector<ChipBounds>& chips = daily ? dailyChips_ : hourlyChips_;
        if (canvas != nullptr) {
            chips.clear();
        }
        // The measure pass has no viewport to speak of, so it lays the card out in full.
        const double barHeight = dimens::kActionBarHeight * density;
        const double visibleTop =
            canvas != nullptr ? pageScroll_.Position() - overscroll_ : -1.0e9;
        const double visibleBottom =
            canvas != nullptr ? visibleTop + (ViewportHeight() - barHeight) : 1.0e9;
        /*
         * The two cards cache their columns separately, and each has to be invalidated by anything
         * that changes what a column looks like: a refresh, a tab, a location, the theme. All of
         * those already run through RebuildSeries, so `seriesGeneration_` covers them; the selected
         * tab is folded in as well because a card can change tab without the other rebuilding.
         */
        const uint64_t generation =
            seriesGeneration_ * 64ULL + static_cast<uint64_t>(daily ? dailyTab_ : hourlyTab_);
        const double cardHeight =
            DrawTrendCard(canvas, left, top, width, spec, series, colors,
                          (daily ? dailyScroll_ : hourlyScroll_).Position(),
                          canvas != nullptr ? &chips : nullptr, visibleTop, visibleBottom,
                          generation, /*deferOverflow=*/true);
        /*
         * The menu is held back to the end of the page rather than drawn here; see
         * `pendingOverflow_`. The anchor is the ⋮ chip, which the card records with tabIndex -1.
         */
        if (canvas != nullptr && spec.overflowOpen) {
            for (const ChipBounds& chip : chips) {
                if (chip.tabIndex == -1) {
                    pendingOverflow_ = true;
                    pendingOverflowDaily_ = daily;
                    pendingOverflowSpec_ = spec;
                    pendingOverflowColors_ = colors;
                    pendingOverflowAnchor_ = chip;
                    pendingOverflowWidth_ = width;
                    break;
                }
            }
        }
        if (canvas != nullptr) {
            double& bottom = daily ? dailyListBottom_ : hourlyListTop_;
            (void)bottom;
            const double listBottom = top + cardHeight - margin;
            if (daily) {
                dailyListBottom_ = listBottom;
                dailyListTop_ = listBottom - spec.listHeightVp * density;
            } else {
                hourlyListBottom_ = listBottom;
                hourlyListTop_ = listBottom - spec.listHeightVp * density;
            }
        }
        return cardHeight;
    }

    // A half-width detail block: "block:<id>".
    for (const DetailBlock& block : detailBlocks_) {
        if ("block:" + block.id != id) {
            continue;
        }
        DetailBlockColors blockColors;
        blockColors.cardBackground = kMainCardBackground.Resolve(scene_.darkMode);
        blockColors.content = scene_.darkMode ? kSurfaceInverseDark : kSurfaceInverseLight;
        blockColors.secondary = kOnSurfaceVariant.Resolve(scene_.darkMode);
        blockColors.tertiary = kTertiary.Resolve(scene_.darkMode);
        blockColors.onTertiary = kOnTertiary.Resolve(scene_.darkMode);
        blockColors.onTertiaryContainer = kOnTertiaryContainer.Resolve(scene_.darkMode);
        blockColors.darkMode = scene_.darkMode;
        if (canvas != nullptr) {
            DrawDetailBlock(canvas, block, left, top, width, blockColors, scene_.utcOffsetSeconds,
                            scene_.is24Hour);
        }
        return width;  // Blocks are square.
    }
    return 0.0;
}

double Renderer::ContentHeight() const {
    return contentHeight_;
}

/**
 * The delete Undo, at the surface's own origin.
 *
 * Its dark mode follows the LIST's, not the page's: the bar is raised from screen 2 and its colours
 * come from the same theme that screen is drawn in, which is `scene_.darkMode` either way.
 */
void Renderer::DrawSnackbar(OH_Drawing_Canvas* canvas) {
    if (!snackbar_.Visible()) {
        return;
    }
    snackbar_.SetGeometry(static_cast<double>(width_), static_cast<double>(height_),
                          DisplayDensity(), bottomInset_, scene_.darkMode);
    snackbar_.Draw(canvas);
}

/**
 * The sky alone — the gradient, with no particles on it.
 *
 * Split out of `PaintScene` because it is the expensive half AND the half that does not move: it
 * depends only on the weather kind, the daylight flag, the theme and the geometry, none of which
 * change between animation ticks. `RenderFrame` paints it into `sky_` once and memcpys it back for
 * every tick, which is what took the scene repaint off the judder budget.
 */
void Renderer::PaintSky(OH_Drawing_Canvas* canvas, WeatherKind kind, bool daylight,
                        bool darkMode) const {
    const BackgroundGradient gradient = WeatherBackgroundGradient(kind, daylight, darkMode);

    OH_Drawing_Brush* brush = OH_Drawing_BrushCreate();
    OH_Drawing_ShaderEffect* shader = nullptr;
    OH_Drawing_Point* start = nullptr;
    OH_Drawing_Point* end = nullptr;

    if (gradient.valid) {
        // The drawables are angle="270", i.e. startColor at the top, endColor at the bottom.
        start = OH_Drawing_PointCreate(0.0f, 0.0f);
        end = OH_Drawing_PointCreate(0.0f, static_cast<float>(height_));
        const uint32_t colors[] = {gradient.top, gradient.middle, gradient.bottom};
        const float stops[] = {0.0f, 0.5f, 1.0f};
        shader = OH_Drawing_ShaderEffectCreateLinearGradient(start, end, colors, stops, 3, CLAMP);
        OH_Drawing_BrushSetShaderEffect(brush, shader);
    } else {
        // weather_background_default is transparent, so the page shows through as the plain app
        // background. Paint it so the surface is never black while a location has no weather.
        OH_Drawing_BrushSetColor(brush, darkMode ? kNeutralDark : kNeutralLight);
    }

    OH_Drawing_Rect* rect =
        OH_Drawing_RectCreate(0.0f, 0.0f, static_cast<float>(ContentWidth()), static_cast<float>(height_));
    OH_Drawing_CanvasAttachBrush(canvas, brush);
    OH_Drawing_CanvasDrawRect(canvas, rect);
    OH_Drawing_CanvasDetachBrush(canvas);

    OH_Drawing_RectDestroy(rect);
    if (shader != nullptr) {
        OH_Drawing_ShaderEffectDestroy(shader);
        OH_Drawing_PointDestroy(start);
        OH_Drawing_PointDestroy(end);
    }
    OH_Drawing_BrushDestroy(brush);
}

/** The particles, which ride on top of the sky and fade out as the cards cover them. */
void Renderer::PaintParticles(OH_Drawing_Canvas* canvas, WeatherImplementor* implementor) const {
    if (implementor != nullptr) {
        implementor->Draw(canvas, ContentWidth(), static_cast<double>(height_),
                          BackgroundScrollRate(), sceneRotation2D_, sceneRotation3D_);
    }
}

void Renderer::PaintScene(OH_Drawing_Canvas* canvas, WeatherKind kind, bool daylight,
                          bool darkMode, WeatherImplementor* implementor) const {
    PaintSky(canvas, kind, daylight, darkMode);
    PaintParticles(canvas, implementor);
}

void Renderer::DrawBackground(OH_Drawing_Canvas* canvas) const {
    if (prevValid_ && switchProgress_ < 1.0) {
        // WeatherBackground.ets paintScene: the outgoing scene stays put while the incoming one
        // fades up over it â€” 300 ms, so a location swipe never hard-cuts the sky.
        PaintScene(canvas, prevKind_, prevDaylight_, prevDarkMode_, prevImplementor_.get());

        OH_Drawing_Brush* layer = OH_Drawing_BrushCreate();
        OH_Drawing_BrushSetAlpha(layer, static_cast<uint8_t>(switchProgress_ * 255.0));
        OH_Drawing_CanvasSaveLayer(canvas, nullptr, layer);
        OH_Drawing_BrushDestroy(layer);
        PaintScene(canvas, scene_.kind, scene_.daylight, scene_.darkMode, implementor_.get());
        OH_Drawing_CanvasRestore(canvas);
        return;
    }
    PaintScene(canvas, scene_.kind, scene_.daylight, scene_.darkMode, implementor_.get());
}

void Renderer::BeginBackgroundSwitch() {
    backgroundValid_ = false;
    prevKind_ = scene_.kind;
    prevDaylight_ = scene_.daylight;
    prevDarkMode_ = scene_.darkMode;
    // The outgoing implementor keeps animating through the fade rather than freezing.
    prevImplementor_ = std::move(implementor_);
    prevValid_ = true;
    switchProgress_ = 0.0;
}

/**
 * HeaderViewHolder.kt:98-104 â€” the count runs from wherever the previous number left off
 * (kt:156-162, initially 0), over |to - from| Ã— 20 ms clamped to 1000..2000. Values are rounded
 * display-unit degrees, because that is what the Kotlin animates.
 */
void Renderer::StartTemperatureCountUp() {
    if (!scene_.hasWeather || !scene_.weather.hasCurrent ||
        !scene_.weather.current.temperature.temperature.has_value()) {
        tempAnimDurationMs_ = 0.0;
        return;
    }
    const double from = DisplayedTemperature();
    const double to = static_cast<double>(
        std::llround(TemperatureValue(*scene_.weather.current.temperature.temperature)));
    tempFromDisp_ = from;
    tempToDisp_ = to;
    tempAnimMs_ = 0.0;
    /*
     * screen 3c's `item_animation_switch` (kt:230-249). HeaderViewHolder only starts the
     * NumberAnimTextView count when itemAnimationEnabled is set; otherwise the view is given the
     * final value outright. A zero duration is exactly that — DisplayedTemperature returns the
     * target immediately.
     */
    tempAnimDurationMs_ = (from == to || !elementAnimations_)
                              ? 0.0
                              : std::clamp(std::fabs(to - from) * 20.0, 1000.0, 2000.0);
}

double Renderer::DisplayedTemperature() const {
    if (tempAnimDurationMs_ <= 0.0) {
        return tempToDisp_;
    }
    const double progress = std::min(1.0, tempAnimMs_ / tempAnimDurationMs_);
    return std::round(tempFromDisp_ + (tempToDisp_ - tempFromDisp_) * progress);
}

void Renderer::EndRefresh() {
    std::lock_guard<std::mutex> lock(mutex_);
    refreshing_ = false;
}

void Renderer::SetAlerts(int locationIndex, std::vector<Alert> alerts) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (locationIndex < 0 || locationIndex >= static_cast<int>(scenes_.size())) {
            return;
        }
        scenes_[static_cast<size_t>(locationIndex)].weather.alertList = alerts;
        if (locationIndex != currentLocation_) {
            return;  // A background location; nothing on screen changes.
        }
        scene_.weather.alertList = std::move(alerts);
    }
    RenderFrame();
}

/**
 * MaterialWeatherView.mFirstCardMarginTop (kt:95) â€” the background fades out over the scroll
 * distance that brings the first card up to the top, and is skipped entirely past that.
 */
double Renderer::BackgroundScrollRate() const {
    const double span = kBackgroundScrollSpan * DisplayDensity();
    if (span <= 0.0) {
        return 0.0;
    }
    return std::clamp(pageScroll_.Position() / span, 0.0, 1.0);
}

/**
 * Builds the rows from the scenes already held.
 *
 * Nothing is fetched and nothing crosses the NAPI boundary: every value a row draws is already in
 * `scenes_`, which is exactly what ManagementFragment does â€” it reads `viewModel.validLocationList`,
 * the same list HomeFragment's pager is showing, rather than loading its own copy.
 *
 * DOES NOT LOCK, and does not paint. It is called from HandleTap, which runs inside OnTouch's
 * `mutex_` â€” `std::mutex` is not recursive, so taking it again deadlocked the UI thread and the
 * system killed the app with an appfreeze the first time this ran on device. The frame comes from
 * HandleTap returning true, as it does for every other toolbar action. `SelectLocation` next to it
 * carries the same contract for the same reason.
 */
void Renderer::RebuildLocationRows() {
    const int64_t nowMs = static_cast<int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    /*
     * The SAVED LIST is what has rows, not the scenes.
     *
     * This used to walk `scenes_`, which meant a location with no forecast yet had no row at all â€”
     * invisible in the list until something fetched for it. Android has no such coupling: adding a
     * location writes it and the row appears immediately, showing its administration levels until a
     * refresh gives it a condition. The details line carries the city and the region for exactly
     * that case, and the scene supplies the weather when there is one.
     */
    const size_t count = std::max(scenes_.size(), locationDetails_.size());
    std::vector<LocationRow> rows;
    rows.reserve(count);
    bool anyCurrentPosition = false;
    for (size_t i = 0; i < count; ++i) {
        static const SceneState kNoScene;
        const SceneState& scene = i < scenes_.size() ? scenes_[i] : kNoScene;
        std::string detailCity;
        std::string detailRegion;
        bool detailCurrent = false;
        DetailsNameFor(static_cast<int>(i), &detailCity, &detailRegion, &detailCurrent);
        LocationRow row;
        row.currentPosition = detailCurrent;
        anyCurrentPosition = anyCurrentPosition || detailCurrent;
        // `detailCity` is already joined with its district; the scene carries the city alone.
        row.title = !scene.city.empty() ? PlaceFor(static_cast<int>(i), scene.city) : detailCity;
        if (detailCurrent) {
            /*
             * Location.kt:39-41 â€” a current position with no address yet reads "Current location",
             * and kt:41 prefixes the resolved name with "âŠ™ " exactly as the toolbar title does.
             * Both ports show the same string here.
             */
            /*
             * WRITTEN AS ESCAPES, not as the literal character.
             *
             * This file is stored double-encoded: its UTF-8 was decoded as Latin-1 and re-encoded
             * at some point, so U+2299 does not occur in it at all and the prefix reached the
             * device as "âŠ™ Helsinki". `\xE2\x8A\x99` IS U+2299 in UTF-8 and is pure ASCII in the
             * source, so no further encoding round-trip can corrupt it.
             */
            row.title = row.title.empty() ? std::string("\xE2\x8A\x99 Current location")
                                          : ("\xE2\x8A\x99 " + row.title);
        }
        row.kind = scene.kind;
        row.daylight = scene.daylight;
        row.hasWeather = scene.hasWeather;
        row.selected = (static_cast<int>(i) == currentLocation_);
        row.alertCount = static_cast<int>(scene.weather.alertList.size());
        /*
         * LocationModel.getWeatherText (kt:54-88), in its own order:
         *   never refreshed            â†’ the administration levels
         *   alert source, stale > 24 h â†’ "Last updated x"
         *   alert source, alerts       â†’ "âš  Active alert"
         *   stale past the interval    â†’ "Last updated x"
         *   otherwise                  â†’ the current condition
         *
         * `updateInterval.validity` is 1.5 h on the default setting, which is the threshold the
         * fourth branch uses. Having a real `refreshTime` on the scene is what makes the two
         * "Last updated" branches possible; a row used to jump straight from the alert test to the
         * condition and so never admitted that its data was hours old.
         *
         * The never-refreshed branch is complete now: the region comes off the details line rather
         * than the scene, which is the widening the note here used to ask for.
         */
        const int64_t refreshMs = scene.weather.refreshTime;
        const int64_t ageMs = refreshMs > 0 ? (nowMs - refreshMs) : 0;
        constexpr int64_t kDayMs = 24LL * 60LL * 60LL * 1000LL;
        /** SettingsManager.updateInterval.validity â€” the default refresh interval is 1.5 h. */
        constexpr int64_t kValidityMs = 90LL * 60LL * 1000LL;
        // Only a location with an alert SOURCE takes the first two branches; here that is a
        // location the alert feed actually answered for.
        const bool hasAlertSource = row.alertCount > 0;
        if (refreshMs <= 0) {
            row.body = detailRegion;
        } else if (hasAlertSource && ageMs > kDayMs) {
            row.body = "Last updated " + FormatRelativeTime(refreshMs, nowMs);
        } else if (hasAlertSource) {
            row.body = "\xE2\x9A\xA0 Active alert";  // strings.xml location_has_active_alerts
        } else if (ageMs > kValidityMs) {
            row.body = "Last updated " + FormatRelativeTime(refreshMs, nowMs);
        } else if (scene.hasWeather && scene.weather.current.weatherText.length() > 0) {
            row.body = scene.weather.current.weatherText;
        }
        rows.push_back(std::move(row));
    }
    locationList_.SetRows(std::move(rows));
    // kt:236 â€” the my-location FAB is offered only while no current-position entry exists.
    locationList_.SetHasCurrentPosition(anyCurrentPosition);
}

/** The rows, then the screen. Same contract as RebuildLocationRows: no lock, no paint. */
void Renderer::OpenLocationList() {
    RebuildLocationRows();
    /*
     * In two-pane the list is already on screen, so the toolbar's list button has nothing to open.
     *
     * Android's button folds the drawer away instead (setManagementFragmentVisibility ->
     * DrawerLayout.isUnfold), which is an animated width change this port does not have. Leaving the
     * pane up is the closer of the two available behaviours: pushing the full-screen list over a
     * page that is already showing the same list would be plainly wrong.
     */
    if (TwoPane()) {
        return;
    }
    if (screen_ != Screen::LocationList) {
        // A fresh open starts at the top; a rebuild under an open list keeps its place.
        locationList_.ResetScroll();
    }
    screen_ = Screen::LocationList;
}

/**
 * Screen 2b, over the list â€” SearchActivity is started from ManagementFragment's + FAB and finishes
 * back onto it, so the list is what is underneath and what returns.
 *
 * Locks itself and paints, unlike OpenLocationList: it is called from OnLocationSearchTouch's
 * post-lock section and from OnBackPressed, neither of which holds `mutex_`.
 */
void Renderer::OpenLocationSearch() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // A fresh search every time, as a newly started activity is (`var text by rememberSaveable`
        // is scoped to the activity, not the app).
        locationSearch_.Reset();
        screen_ = Screen::LocationSearch;
    }
    // The keyboard only exists on the ArkTS side; the shell attaches the IME when this fires.
    if (searchUi_ != nullptr) {
        searchUi_(1);
    }
    RenderFrame();
}

void Renderer::CloseLocationSearch() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        screen_ = Screen::LocationList;
    }
    if (searchUi_ != nullptr) {
        searchUi_(0);
    }
    RenderFrame();
}

void Renderer::SetSavedLocationIds(const std::string& ids) {
    std::vector<std::string> list;
    size_t start = 0;
    while (start <= ids.size()) {
        const size_t end = ids.find('\n', start);
        std::string one = ids.substr(start, end == std::string::npos ? std::string::npos
                                                                     : end - start);
        if (!one.empty()) {
            list.push_back(std::move(one));
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    sourcesDialog_.SetSavedIds(std::move(list));
}

void Renderer::SetListLoading(bool loading) {
    bool onList = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        locationList_.SetListLoading(loading);
        onList = (screen_ == Screen::LocationList);
    }
    // Screen 2 has no frame loop, so the indicator appearing and vanishing has to paint itself.
    if (onList) {
        RenderFrame();
    }
}

void Renderer::SetLocationDetails(const std::string& details) {
    std::vector<std::string> list;
    size_t start = 0;
    while (start <= details.size()) {
        const size_t end = details.find('\n', start);
        std::string one = details.substr(start, end == std::string::npos ? std::string::npos
                                                                         : end - start);
        if (!one.empty()) {
            list.push_back(std::move(one));
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    /*
     * The rows come from this list, so a change to it IS a change to the list screen.
     *
     * Storing the lines and stopping was enough only while rows were derived from the scenes and a
     * new location always arrived with a forecast. Now that a location appears the moment it is
     * saved, the details are what tells the screen it exists â€” and screen 2 runs no frame loop, so
     * nothing else would ever repaint it.
     */
    bool onList = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        locationDetails_ = std::move(list);
        onList = (screen_ == Screen::LocationList);
        /*
         * A location with no weather still needs a SCENE SLOT, or it cannot be selected.
         *
         * `SelectLocation` refuses an index past `scenes_`, so a row that exists only in the details
         * â€” a freshly added location, or the current position before its first fix â€” was drawn and
         * then silently ignored when tapped. Sizing the scenes from the same list that sizes the
         * rows keeps the two in step; the empty scene is what a location being refreshed shows.
         */
        const int count = static_cast<int>(locationDetails_.size());
        if (count > 0) {
            locationCount_ = std::max(locationCount_, count);
            if (static_cast<int>(scenes_.size()) < count) {
                scenes_.resize(static_cast<size_t>(count));
                pageScrollByLocation_.resize(static_cast<size_t>(count), 0.0);
            }
        }
        // RebuildLocationRows does NOT lock; it is written to run inside an already-held one.
        RebuildLocationRows();
    }
    // RenderFrame takes mutex_ itself, so it can only be called with this one released.
    if (onList) {
        RenderFrame();
    }
}

/**
 * The saved list's own view of a row: what it is CALLED and which area it is in.
 *
 * A location that has never been refreshed has no scene, so neither its title nor its body can come
 * from one â€” which is why adding a location used to leave a row that simply did not appear.
 */
bool Renderer::DetailsNameFor(int index, std::string* city, std::string* region,
                              bool* currentPosition) const {
    if (index < 0 || index >= static_cast<int>(locationDetails_.size())) {
        return false;
    }
    const std::string& line = locationDetails_[static_cast<size_t>(index)];
    std::vector<std::string> fields;
    size_t start = 0;
    while (true) {
        const size_t end = line.find('\t', start);
        fields.push_back(line.substr(start, end == std::string::npos ? std::string::npos
                                                                     : end - start));
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    *region = fields.size() > 2 ? fields[2] : std::string();
    // Field 9 is the district, appended after the current-position flag; a line written before it
    // simply has none, which is what a searched location has anyway.
    // getPlace prefers Location.customName (field 10) over the geocoded name — Location.kt:33-35.
    *city = fields.size() > 10 && !fields[10].empty()
                ? fields[10]
                : CityAndDistrict(fields.size() > 7 ? fields[7] : std::string(),
                                  fields.size() > 9 ? fields[9] : std::string());
    *currentPosition = fields.size() > 8 && fields[8] == "1";
    return true;
}

/**
 * One tab-separated field of row `index`'s details line, or empty.
 *
 * ONE copy of the walk. Four callers wanted a single field each and three of them had written the
 * loop out again with a different constant in it, which is how a field appended at the end stays
 * cheap to add: only this function knows the line is tabs.
 */
std::string Renderer::DetailField(int index, int field) const {
    if (index < 0 || index >= static_cast<int>(locationDetails_.size()) || field < 0) {
        return {};
    }
    const std::string& line = locationDetails_[static_cast<size_t>(index)];
    size_t start = 0;
    for (int i = 0; i < field; ++i) {
        start = line.find('\t', start);
        if (start == std::string::npos) {
            return {};
        }
        ++start;
    }
    const size_t end = line.find('\t', start);
    return line.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

/**
 * The district of row `index`, or empty.
 *
 * The toolbar titles from the SCENE, which carries only the city — the district rides the details
 * line, so the two are joined here rather than at the fetch that built the scene.
 */
std::string Renderer::DistrictFor(int index) const { return DetailField(index, 9); }

/** Location.customName — field 10, '' when the user has not renamed this location. */
std::string Renderer::CustomNameFor(int index) const { return DetailField(index, 10); }

/** The Open-Meteo model selection — field 11, '' for the source's own default. */
std::string Renderer::WeatherModelsFor(int index) const { return DetailField(index, 11); }

/**
 * getPlace (Location.kt:29-43) for row `index`.
 *
 * The custom name wins over the geocoded one (kt:33-35), which is the whole point of the field —
 * so every title in this port goes through here rather than joining city and district directly.
 */
std::string Renderer::PlaceFor(int index, const std::string& city) const {
    const std::string custom = CustomNameFor(index);
    if (!custom.empty()) {
        return custom;
    }
    return CityAndDistrict(city, DistrictFor(index));
}

/**
 * Whether row `index` is the current position.
 *
 * Field 8 of the details line, the same field `DetailsNameFor` reads for the list row. The toolbar
 * needs it on its own because it titles from the SCENE and never asks for the row's name.
 */
bool Renderer::IsCurrentPositionAt(int index) const { return DetailField(index, 8) == "1"; }

/**
 * Splits one details line â€”
 * `formattedId \t countryCode \t region \t admin2 \t latitude \t longitude \t sources \t city`.
 */
bool Renderer::DetailsFor(int index, SourceLocation* location, LocationSources* sources) const {
    if (index < 0 || index >= static_cast<int>(locationDetails_.size())) {
        return false;
    }
    std::vector<std::string> fields;
    const std::string& line = locationDetails_[static_cast<size_t>(index)];
    size_t start = 0;
    while (true) {
        const size_t end = line.find('\t', start);
        fields.push_back(line.substr(start, end == std::string::npos ? std::string::npos
                                                                     : end - start));
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    if (fields.size() < 6) {
        return false;
    }
    location->countryCode = fields[1];
    location->region = fields[2];
    location->admin2 = fields[3];
    location->latitude = std::strtod(fields[4].c_str(), nullptr);
    location->longitude = std::strtod(fields[5].c_str(), nullptr);
    // The catalogue's own predicates read this â€” kt:243 builds the entry with it set, and
    // applyDefaultPreset behaves differently for a location whose coordinates are not yet known.
    location->isCurrentPosition = fields.size() > 8 && fields[8] == "1";

    // The eight ids, comma separated, in SourceFeature order â€” the same string the shell stores.
    const std::string csv = fields.size() > 6 ? fields[6] : std::string();
    std::vector<std::string> ids;
    start = 0;
    while (true) {
        const size_t end = csv.find(',', start);
        ids.push_back(csv.substr(start, end == std::string::npos ? std::string::npos
                                                                 : end - start));
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    ids.resize(8);
    sources->forecast = ids[0];
    sources->current = ids[1];
    sources->airQuality = ids[2];
    sources->pollen = ids[3];
    sources->minutely = ids[4];
    sources->alert = ids[5];
    sources->normals = ids[6];
    sources->reverseGeocoding = ids[7];
    return true;
}

void Renderer::RemoveScene(int index) {
    if (index < 0 || index >= static_cast<int>(scenes_.size())) {
        return;
    }
    scenes_.erase(scenes_.begin() + index);
    if (index < static_cast<int>(pageScrollByLocation_.size())) {
        pageScrollByLocation_.erase(pageScrollByLocation_.begin() + index);
    }
    locationCount_ = std::max(1, static_cast<int>(scenes_.size()));
    // The pager cannot stay on a page that no longer exists, and a page BEFORE the removed one
    // keeps its number.
    if (currentLocation_ > index) {
        --currentLocation_;
    }
    currentLocation_ = std::max(0, std::min(currentLocation_, static_cast<int>(scenes_.size()) - 1));
    if (!scenes_.empty()) {
        scene_ = scenes_[static_cast<size_t>(currentLocation_)];
    }
}

void Renderer::MoveScene(int from, int to) {
    const int count = static_cast<int>(scenes_.size());
    if (from < 0 || to < 0 || from >= count || to >= count || from == to) {
        return;
    }
    // `valid.add(to, valid.removeAt(from))` â€” a MOVE, not a swap.
    SceneState moved = scenes_[static_cast<size_t>(from)];
    scenes_.erase(scenes_.begin() + from);
    scenes_.insert(scenes_.begin() + to, std::move(moved));
    if (from < static_cast<int>(pageScrollByLocation_.size()) &&
        to < static_cast<int>(pageScrollByLocation_.size())) {
        const double scroll = pageScrollByLocation_[static_cast<size_t>(from)];
        pageScrollByLocation_.erase(pageScrollByLocation_.begin() + from);
        pageScrollByLocation_.insert(pageScrollByLocation_.begin() + to, scroll);
    }
    // The pager follows the location it was on, wherever it has gone.
    if (currentLocation_ == from) {
        currentLocation_ = to;
    } else if (from < currentLocation_ && to >= currentLocation_) {
        --currentLocation_;
    } else if (from > currentLocation_ && to <= currentLocation_) {
        ++currentLocation_;
    }
}

void Renderer::InsertScene(int index) {
    const int at = std::max(0, std::min(index, static_cast<int>(scenes_.size())));
    scenes_.insert(scenes_.begin() + at, SceneState());
    pageScrollByLocation_.insert(
        pageScrollByLocation_.begin() +
            std::min(static_cast<size_t>(at), pageScrollByLocation_.size()), 0.0);
    locationCount_ = static_cast<int>(scenes_.size());
    if (currentLocation_ >= at) {
        ++currentLocation_;
    }
}

void Renderer::OnListChanged(const std::string& command) {
    const size_t bar = command.find('|');
    if (bar == std::string::npos || command.substr(0, bar) != "insert") {
        return;
    }
    const int index = std::atoi(command.c_str() + bar + 1);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        InsertScene(index);
        if (screen_ != Screen::Main) {
            RebuildLocationRows();
        }
    }
    RenderFrame();
}

/**
 * ItemTouchHelper.START (LocationItemTouchCallback.kt:91-97) â€” the per-location weather-sources
 * dialog. The row has already sprung back, which is the port's equivalent of the unconditional
 * `notifyItemChanged` at kt:92.
 */
void Renderer::OpenSourcesForRow(int index) {
    bool opened = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        SourceLocation location;
        LocationSources sources;
        if (DetailsFor(index, &location, &sources)) {
            const std::string place = index < static_cast<int>(scenes_.size())
                                          ? scenes_[static_cast<size_t>(index)].city
                                          : std::string();
            sourcesDialog_.OpenForUpdate(index, location, place, sources);
            opened = true;
        }
    }
    if (!opened && navigate_ != nullptr) {
        // Nothing to configure without the shell's details; say so rather than doing nothing.
        Navigate("Weather sources");
    }
    RenderFrame();
}

void Renderer::SetSearchQuery(const std::string& query) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (screen_ != Screen::LocationSearch) {
            return;
        }
        locationSearch_.SetQuery(query);
    }
    RenderFrame();
}

void Renderer::SetSearchResults(const std::string& body, const std::string& searched) {
    GeocodingResponse response = ParseGeocoding(body);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (screen_ != Screen::LocationSearch) {
            return;
        }
        locationSearch_.SetResults(std::move(response.results), searched, response.ok);
    }
    RenderFrame();
}

void Renderer::SetSearchLoading(bool loading) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (screen_ != Screen::LocationSearch) {
            return;
        }
        locationSearch_.SetLoading(loading);
    }
    RenderFrame();
}

void Renderer::CloseSearchScreen() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (screen_ != Screen::LocationSearch) {
            return;
        }
    }
    CloseLocationSearch();
}

bool Renderer::OnBackPressed() {
    Screen screen = Screen::Main;
    bool dialog = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        screen = screen_;
        dialog = sourcesDialog_.IsOpen();
        // A dialog takes the gesture before the screen under it does â€” `onDismissRequest`.
        if (dialog) {
            sourcesDialog_.Close();
        }
    }
    if (dialog) {
        // Dismissing over the search screen gives its keyboard back.
        if (screen == Screen::LocationSearch && searchUi_ != nullptr) {
            searchUi_(3);
        }
        RenderFrame();
        return true;
    }
    /*
     * The edit dialog is below the sources dialog, so it takes Back only once that one is gone —
     * and it takes it a level at a time, as a stack of AlertDialogs does. Closing it takes the
     * keyboard away with it, in case Back arrived while the name field was open.
     */
    {
        bool prefsOpen = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            prefsOpen = prefsDialog_.IsOpen();
            if (prefsOpen) {
                prefsDialog_.Close();
            }
        }
        if (prefsOpen) {
            if (timeEntry_ != nullptr) {
                timeEntry_(0);
            }
            RenderFrame();
            return true;
        }
    }
    if (screen == Screen::MainSettings) {
        // Screen 3c is opened from the settings ROOT, so back returns there.
        CloseMainSettings();
        return true;
    }
    if (screen == Screen::Notifications) {
        // Screen 3d likewise.
        CloseNotifications();
        return true;
    }
    if (screen == Screen::Modules) {
        // An open choice dialog takes Back first, as a real dialog does.
        if (modules_.DialogOpen()) {
            modules_.CloseDialog();
            RenderFrame();
            return true;
        }
        CloseModules();
        return true;
    }
    if (screen == Screen::WorkerInfo) {
        CloseWorkerInfo();
        return true;
    }
    if (screen == Screen::Debug) {
        CloseDebug();
        return true;
    }
    if (screen == Screen::BackgroundUpdates) {
        // A dialog takes Back before the screen does, as every other screen-3 page does.
        if (backgroundUpdates_.DialogOpen()) {
            backgroundUpdates_.CloseDialog();
            RenderFrame();
            return true;
        }
        CloseBackgroundUpdates();
        return true;
    }
    if (screen == Screen::About) {
        CloseAbout();
        return true;
    }
    if (screen == Screen::PrivacyPolicy) {
        ClosePrivacyPolicy();
        return true;
    }
    if (screen == Screen::Dependencies) {
        CloseDependencies();
        return true;
    }
    if (screen == Screen::AlertList) {
        CloseAlertList();
        return true;
    }
    if (screen == Screen::PreviewIcon) {
        // An open icon dialog takes Back first, as a real dialog does.
        if (previewIcon_.DialogOpen()) {
            previewIcon_.CloseDialog();
            return true;
        }
        ClosePreviewIcon();
        return true;
    }
    if (screen == Screen::Location) {
        CloseLocation();
        return true;
    }
    if (screen == Screen::WeatherSources) {
        CloseWeatherSources();
        return true;
    }
    if (screen == Screen::DisplayManage) {
        CloseDisplayManage();
        return true;
    }
    if (screen == Screen::Details) {
        // BackHandler(fabMenuExpanded) (kt:282) — an open FAB menu eats Back first.
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (details_.MenuOpen()) {
                details_.CloseMenu();
            } else {
                screen_ = Screen::Main;
            }
        }
        RenderFrame();
        return true;
    }
    if (screen == Screen::Placeholder) {
        ClosePlaceholder();
        return true;
    }
    if (screen == Screen::Units) {
        // Back from Units lands on Appearance, the screen that opened it — not the settings root.
        CloseUnits();
        return true;
    }
    if (screen == Screen::Appearance) {
        CloseAppearance();
        return true;
    }
    if (screen == Screen::Settings) {
        // Finishing the settings Activity returns to the list it was opened from.
        CloseSettings();
        return true;
    }
    if (screen == Screen::LocationSearch) {
        CloseLocationSearch();
        return true;
    }
    if (screen == Screen::LocationList) {
        CloseLocationList();
        return true;
    }
    return false;
}

/**
 * The gear on screen 2 — kt:205's startSettingsActivity.
 *
 * An Activity over the list, so the list keeps its scroll and its rows; this only changes which
 * screen owns the surface. The theme is the SYSTEM one, not the location's: see SettingsScreen.
 */
/**
 * ThemeManager.isLightTheme (ThemeManager.kt:68-84), verbatim:
 *
 *   LIGHT  -> daylight
 *   DARK   -> dayNightModeForLocations && daylight
 *   SYSTEM -> systemDark ? (dayNightModeForLocations && daylight) : daylight
 *
 * The Kotlin's own comment (kt:61-67) reads it out: with dark mode "always light" a location is
 * light by day and dark by night; with "always dark" the location rule only applies when day/night
 * for locations is on; "follow system" applies whichever of the two the system is in.
 *
 * Under the shipped defaults — dark mode `system`, day/night for locations FALSE on anything modern
 * (SettingsManager.kt:185-190) — and a light system, this reduces to plain `daylight`, which is the
 * flat `!daylight` this port used before the settings existed. That is why it looked right.
 *
 * Caller must hold `mutex_`.
 */
bool Renderer::IsLightTheme(bool daylight) const {
    if (darkModeSetting_ == DarkModeSetting::Light) {
        return daylight;
    }
    if (darkModeSetting_ == DarkModeSetting::Dark) {
        return dayNightForLocations_ && daylight;
    }
    return systemDarkMode_ ? (dayNightForLocations_ && daylight) : daylight;
}

/**
 * The app's EFFECTIVE dark mode — what `isSystemInDarkTheme()` answers inside the app once
 * setDefaultNightMode has run, and so what the settings screens theme from.
 *
 * Caller must hold `mutex_`.
 */
bool Renderer::AppIsDark() const {
    if (darkModeSetting_ == DarkModeSetting::Dark) {
        return true;
    }
    if (darkModeSetting_ == DarkModeSetting::Light) {
        return false;
    }
    return systemDarkMode_;
}

/** Re-derives every scene's theme after a setting changed. Caller must hold `mutex_`. */
void Renderer::RefreshThemes() {
    for (SceneState& scene : scenes_) {
        scene.darkMode = !IsLightTheme(scene.daylight);
    }
    scene_.darkMode = !IsLightTheme(scene_.daylight);
    // The sky is painted from the theme, so a change invalidates the cached background.
    backgroundValid_ = false;
    lastFrameWasComplete_ = false;
    scratchValid_ = false;
}

void Renderer::SetSystemDarkMode(bool dark) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (systemDarkMode_ == dark) {
            return;
        }
        systemDarkMode_ = dark;
        RefreshThemes();
    }
    RenderFrame();
}

/**
 * The two Appearance settings, from the shell that owns the preferences file.
 *
 * `darkMode` is DarkMode.kt's id — "system", "light" or "dark".
 */
void Renderer::SetAlternateCalendar(const std::string& value, const std::string& language,
                                    const std::string& country) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        appearance_.SetAlternateCalendar(value);
        appearance_.SetLocale(language, country);
    }
    if (screen_ == Screen::Appearance) {
        RenderFrame();
    }
}

void Renderer::SetDarkModeSettings(const std::string& darkMode, bool dayNightForLocations) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        DarkModeSetting parsed = DarkModeSetting::System;
        if (darkMode == "light") {
            parsed = DarkModeSetting::Light;
        } else if (darkMode == "dark") {
            parsed = DarkModeSetting::Dark;
        }
        if (parsed == darkModeSetting_ && dayNightForLocations == dayNightForLocations_) {
            return;
        }
        darkModeSetting_ = parsed;
        dayNightForLocations_ = dayNightForLocations;
        RefreshThemes();
    }
    RenderFrame();
}

void Renderer::SetUnitSettings(const std::string& temperature, const std::string& precipitation,
                               const std::string& speed, const std::string& distance,
                               const std::string& pressure) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (UnitOverride(UnitFamily::Temperature) == temperature &&
            UnitOverride(UnitFamily::Precipitation) == precipitation &&
            UnitOverride(UnitFamily::Speed) == speed &&
            UnitOverride(UnitFamily::Distance) == distance &&
            UnitOverride(UnitFamily::Pressure) == pressure) {
            return;
        }
        SetUnitOverride(UnitFamily::Temperature, temperature);
        SetUnitOverride(UnitFamily::Precipitation, precipitation);
        SetUnitOverride(UnitFamily::Speed, speed);
        SetUnitOverride(UnitFamily::Distance, distance);
        SetUnitOverride(UnitFamily::Pressure, pressure);
        /*
         * A unit change rewrites EVERY formatted string on a page — the header, both trend cards'
         * labels and all twelve detail blocks — so all three caches have to go, exactly as a theme
         * change drops them. `RebuildSeries` bumps `seriesGeneration_`, which is what the trend
         * cards' per-column cache keys off; the rest is the same invalidation RefreshThemes does.
         */
        RebuildSeries();
        backgroundValid_ = false;
        lastFrameWasComplete_ = false;
        scratchValid_ = false;
        /*
         * The big header number needs its own line, because it is not drawn from the model — it is
         * an animated count-up whose TARGET was computed in the old unit by
         * StartTemperatureCountUp, and nothing re-runs that on a settings change. Without this the
         * header read "20 °F" for Helsinki: the label, the feels-like, the day/night range and
         * every trend label had switched to Fahrenheit around a Celsius number.
         *
         * ArkUIApp had the identical bug in NumberAnimText, from the identical cause — a target
         * snapshotted once and never reconciled.
         *
         * Snapped rather than animated: the reading has not changed, only the unit it is written
         * in, so counting 20 up to 68 would be inventing a change that did not happen.
         */
        if (scene_.hasWeather && scene_.weather.hasCurrent &&
            scene_.weather.current.temperature.temperature.has_value()) {
            tempToDisp_ = static_cast<double>(
                std::llround(TemperatureValue(*scene_.weather.current.temperature.temperature)));
            tempFromDisp_ = tempToDisp_;
            tempAnimMs_ = 0.0;
            tempAnimDurationMs_ = 0.0;
        }
    }
    RenderFrame();
}

void Renderer::OpenSettings() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        settings_.ResetScroll();
        screen_ = Screen::Settings;
    }
    RenderFrame();
}

void Renderer::CloseSettings() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Back to the list it was opened from, exactly as finishing the Activity does.
        screen_ = Screen::LocationList;
    }
    RenderFrame();
}

/**
 * Screen 3 owns the whole gesture while it is up: a vertical drag scrolls, a tap acts.
 *
 * The same shape as OnLocationListTouch, and for the same reason — the screen has no frame loop, so
 * every state change paints itself.
 */
void Renderer::OnSettingsTouch(const TouchSample& raw) {
    SettingsHit hit;
    bool needsFrame = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        switch (raw.phase) {
            case TouchSample::Phase::Down:
                settingsDownY_ = raw.y;
                settingsLastY_ = raw.y;
                settingsDragged_ = false;
                return;

            case TouchSample::Phase::Move: {
                // ViewConfiguration's touch slop, as the other two screens use.
                if (!settingsDragged_ &&
                    std::fabs(raw.y - settingsDownY_) > 8.0 * DisplayDensity()) {
                    settingsDragged_ = true;
                }
                if (settingsDragged_) {
                    needsFrame = settings_.ScrollBy(raw.y - settingsLastY_);
                }
                settingsLastY_ = raw.y;
                break;
            }

            case TouchSample::Phase::Up:
                if (!settingsDragged_) {
                    hit = settings_.HitTest(raw.x, raw.y);
                }
                settingsDragged_ = false;
                break;

            case TouchSample::Phase::Cancel:
                settingsDragged_ = false;
                break;
        }
    }

    if (hit.action == SettingsAction::Close) {
        CloseSettings();
        return;
    }
    if (hit.action == SettingsAction::Navigate) {
        // Appearance and Main screen are real now; every other destination is a placeholder.
        if (hit.destination == "Appearance") {
            OpenAppearance();
            return;
        }
        if (hit.destination == "Main screen") {
            OpenMainSettings();
            return;
        }
        if (hit.destination == "Notifications") {
            OpenNotifications();
            return;
        }
        if (hit.destination == "Debug") {
            OpenDebug();
            return;
        }
        if (hit.destination == "External modules") {
            OpenModules();
            return;
        }
        if (hit.destination == "Background updates") {
            OpenBackgroundUpdates();
            return;
        }
        if (hit.destination == "Location") {
            OpenLocation();
            return;
        }
        if (hit.destination == "Weather sources") {
            OpenWeatherSources();
            return;
        }
        // The ⓘ action in the app bar — AboutActivityIconButton (AppBar.kt:32-44).
        if (hit.destination == "About") {
            OpenAbout();
            return;
        }
        if (navigate_ != nullptr) {
            Navigate(hit.destination);
        }
        return;
    }
    if (needsFrame) {
        RenderFrame();
    }
}

void Renderer::OpenAppearance() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        appearance_.ResetScroll();
        appearance_.CloseDialog();
        screen_ = Screen::Appearance;
    }
    RenderFrame();
}

void Renderer::CloseAppearance() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        screen_ = Screen::Settings;
    }
    RenderFrame();
}

/** Screen 3a's gestures: a vertical drag scrolls, a tap acts, and a dialog takes everything. */
void Renderer::OnAppearanceTouch(const TouchSample& raw) {
    AppearanceHit hit;
    bool needsFrame = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        switch (raw.phase) {
            case TouchSample::Phase::Down:
                appearanceDownY_ = raw.y;
                appearanceLastY_ = raw.y;
                appearanceDragged_ = false;
                return;

            case TouchSample::Phase::Move:
                // A dialog does not scroll behind itself.
                if (!appearance_.DialogOpen()) {
                    if (!appearanceDragged_ &&
                        std::fabs(raw.y - appearanceDownY_) > 8.0 * DisplayDensity()) {
                        appearanceDragged_ = true;
                    }
                    if (appearanceDragged_) {
                        needsFrame = appearance_.ScrollBy(raw.y - appearanceLastY_);
                    }
                }
                appearanceLastY_ = raw.y;
                break;

            case TouchSample::Phase::Up:
                if (!appearanceDragged_) {
                    hit = appearance_.HitTest(raw.x, raw.y);
                    // Opening or dismissing the dialog is a state change with no action attached.
                    needsFrame = true;
                }
                appearanceDragged_ = false;
                break;

            case TouchSample::Phase::Cancel:
                appearanceDragged_ = false;
                break;
        }
    }

    if (hit.action == AppearanceAction::Close) {
        CloseAppearance();
        return;
    }
    if (hit.action == AppearanceAction::Preview) {
        OpenPreviewIcon();
        return;
    }
    if (hit.action == AppearanceAction::Navigate) {
        // Units is real now; every other destination on this screen is still a placeholder.
        if (hit.destination == "Units") {
            OpenUnits();
            return;
        }
        if (navigate_ != nullptr) {
            Navigate(hit.destination);
        }
        return;
    }
    if (hit.action == AppearanceAction::Changed) {
        // The shell stores it and hands the values back through SetDarkModeSettings, which is what
        // re-themes every scene — this side does not apply the change itself.
        if (settingsEdit_ != nullptr) {
            settingsEdit_(hit.key + "|" + hit.value);
        }
        RenderFrame();
        return;
    }
    if (needsFrame) {
        RenderFrame();
    }
}

void Renderer::SetNotificationSettings(bool appUpdates, bool alert, bool precipitation, bool today,
                                       bool tomorrow, const std::string& todayTime,
                                       const std::string& tomorrowTime,
                                       const std::string& refreshRate, bool twelveHour) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        notifications_.SetValues(appUpdates, alert, precipitation, today, tomorrow, todayTime,
                                 tomorrowTime, refreshRate, twelveHour);
        // Screen 3c shows the same rate; the shell pushes it once, for both.
        backgroundUpdatesRate_ = refreshRate;
        backgroundUpdates_.SetValues(backgroundUpdatesRate_, backgroundUpdatesSkipBatteryLow_);
    }
    RenderFrame();
}

void Renderer::SetBackgroundUpdatesValues(const std::string& refreshRate,
                                          bool skipWhenBatteryLow) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        backgroundUpdatesRate_ = refreshRate;
        backgroundUpdatesSkipBatteryLow_ = skipWhenBatteryLow;
        backgroundUpdates_.SetValues(refreshRate, skipWhenBatteryLow);
    }
    RenderFrame();
}

void Renderer::SetTimeEntryText(const std::string& text) {
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // One channel, routed by whichever screen has a field open — or by the edit DIALOG, which
        // floats over the main screen and so cannot be identified by `screen_`.
        if (prefsDialog_.TextEntryOpen()) {
            changed = prefsDialog_.SetEnteredText(text);
        } else if (screen_ == Screen::Location) {
            changed = location_.SetEnteredText(text);
        } else if (screen_ == Screen::WeatherSources) {
            changed = weatherSources_.SetEnteredText(text);
        } else {
            changed = notifications_.SetEnteredText(text);
        }
    }
    if (changed) {
        RenderFrame();
    }
}

void Renderer::ReseedRotators() {
    sceneHasDrawn_ = false;
    rotator2D_ = DelayRotateController(gravitySensor_.Rotation2D());
    rotator3D_ = DelayRotateController(gravitySensor_.Rotation3D());
    sceneRotation2D_ = rotator2D_.Rotation();
    sceneRotation3D_ = rotator3D_.Rotation();
    /*
     * `Context.isLandscape`, which the device-orientation mapping branches on. The background is
     * full-bleed, so the surface being wider than it is tall is the same predicate. This is the
     * point where a rotation has definitely settled, because the implementor is rebuilt with the
     * new size.
     */
    gravitySensor_.SetLandscape(ContentWidth() > static_cast<double>(height_));
}

void Renderer::SetMainScreenSettings(bool background, bool trendLines,
                                     const std::string& animationMode, bool gravity, bool fadeIn,
                                     bool elements) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        mainSettings_.SetValues(background, trendLines, animationMode, gravity, fadeIn, elements);
        /*
         * The three the WEATHER page obeys are kept here as well. A change to any of them
         * invalidates the cached scene — turning the background off has to repaint the page
         * without it, and turning the animation or the sensor back on has to restart the loop that
         * `WantsFrames` had stopped.
         */
        /*
         * The trend lines are baked into the cached column bitmaps, so turning them on or off has
         * to invalidate every cached column — `seriesGeneration_` is what ColumnCache keys on.
         */
        if (trendHorizontalLines_ != trendLines) {
            trendHorizontalLines_ = trendLines;
            ++seriesGeneration_;
            backgroundValid_ = false;
            lastFrameWasComplete_ = false;
            scratchValid_ = false;
        }
        /*
         * The card entrance and the header count-up. Both are read where they are used rather than
         * re-applied to anything already on screen: Android rebinds its holders on the next bind,
         * and this port's equivalent is the next page build. `elementAnimations_` is picked up by
         * the next StartTemperatureCountUp.
         */
        cardsFadeIn_ = fadeIn;
        elementAnimations_ = elements;
        enter_.SetEnabled(fadeIn);
        // Screen 4's condition icon branches on the same flag (DetailsConditions.kt:732).
        details_.SetElementAnimations(elements);

        if (mainScreenBackground_ != background || backgroundAnimationMode_ != animationMode ||
            gravitySensor_.IsEnabled() != gravity) {
            mainScreenBackground_ = background;
            // A change of animation mode re-arms the single "advance once, then hold" step, so
            // turning animations off and on again does not leave the shapes frozen.
            if (backgroundAnimationMode_ != animationMode) {
                sceneHasDrawn_ = false;
            }
            backgroundAnimationMode_ = animationMode;
            /*
             * ContextExtensions.kt:80-85 — the setting gates the SensorManager itself, so off
             * means no subscription and both angles pinned at 0. The rotators are left alone to
             * chase back down to level from wherever they were.
             */
            gravitySensor_.SetEnabled(gravity);
            backgroundValid_ = false;
            lastFrameWasComplete_ = false;
            scratchValid_ = false;
        }
    }
    RenderFrame();
}

void Renderer::SetLocationSettings(const std::string& locationService,
                                   const std::string& baiduApiKey) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        location_.SetValues(locationService, baiduApiKey);
        // The edit dialog's first row shows and writes the same global setting without the
        // Location screen ever being opened, so the renderer keeps its own copy.
        locationService_ = locationService;
        prefsDialog_.SetLocationService(locationService);
    }
    RenderFrame();
}

void Renderer::SetLocationPermissions(bool coarseGranted, bool preciseGranted) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        location_.SetPermissions(coarseGranted, preciseGranted);
    }
    RenderFrame();
}

void Renderer::OpenLocation() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        location_.ResetScroll();
        location_.CloseDialog();
        /*
         * kt:210-213's "already granted" bar, which is the screen's own and not the list's.
         *
         * Bound here rather than at construction because the renderer's snackbar outlives every
         * screen — it floats over whatever is showing, as Android's does over the content view.
         * No action button: the Kotlin passes none.
         */
        location_.SetSnackbarHandler([this](const std::string& message) {
            snackbar_.Show(message, "");
        });
        screen_ = Screen::Location;
    }
    RenderFrame();
}

void Renderer::CloseLocation() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        location_.CloseDialog();
        screen_ = Screen::Settings;
    }
    RenderFrame();
}

/** Screen 3e's gestures, the same shape as 3c's and 3d's. */
void Renderer::OnLocationTouch(const TouchSample& raw) {
    LocationSettingsHit hit;
    bool needsFrame = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        switch (raw.phase) {
            case TouchSample::Phase::Down:
                locationDownY_ = raw.y;
                locationLastY_ = raw.y;
                locationDragged_ = false;
                return;

            case TouchSample::Phase::Move:
                if (!location_.DialogOpen()) {
                    if (!locationDragged_ &&
                        std::fabs(raw.y - locationDownY_) > 8.0 * DisplayDensity()) {
                        locationDragged_ = true;
                    }
                    if (locationDragged_) {
                        needsFrame = location_.ScrollBy(raw.y - locationLastY_);
                    }
                }
                locationLastY_ = raw.y;
                break;

            case TouchSample::Phase::Up:
                if (!locationDragged_) {
                    hit = location_.HitTest(raw.x, raw.y);
                    needsFrame = true;
                }
                locationDragged_ = false;
                break;

            case TouchSample::Phase::Cancel:
                locationDragged_ = false;
                break;
        }
    }

    if (hit.action == LocationSettingsAction::Close) {
        CloseLocation();
        return;
    }
    if (hit.action == LocationSettingsAction::Navigate) {
        if (navigate_ != nullptr) {
            Navigate(hit.destination);
        }
        return;
    }
    if (hit.action == LocationSettingsAction::RequestPermission) {
        if (permission_ != nullptr) {
            permission_(hit.key);
        }
        RenderFrame();
        return;
    }
    if (hit.action == LocationSettingsAction::Changed) {
        // Stored by the shell and pushed straight back, as every other settings screen works.
        if (settingsEdit_ != nullptr) {
            settingsEdit_(hit.key + "|" + hit.value);
        }
        RenderFrame();
        return;
    }
    if (needsFrame) {
        RenderFrame();
    }
}

void Renderer::SetSourceSettings(const std::string& defaultForecastSource,
                                 const std::string& prefs) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        defaultForecastSource_ = defaultForecastSource;
        weatherSources_.SetValues(defaultForecastSource, prefs);
    }
    RenderFrame();
}

void Renderer::OpenWeatherSources() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        weatherSources_.ResetScroll();
        weatherSources_.CloseDialog();
        screen_ = Screen::WeatherSources;
    }
    RenderFrame();
}

void Renderer::CloseWeatherSources() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        weatherSources_.CloseDialog();
        screen_ = Screen::Settings;
    }
    RenderFrame();
}

/** Screen 3f's gestures, the same shape as 3e's — it is the same list, ninety rows longer. */
void Renderer::OnWeatherSourcesTouch(const TouchSample& raw) {
    WeatherSourcesHit hit;
    bool needsFrame = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        switch (raw.phase) {
            case TouchSample::Phase::Down:
                weatherSourcesDownY_ = raw.y;
                weatherSourcesLastY_ = raw.y;
                weatherSourcesDragged_ = false;
                return;

            case TouchSample::Phase::Move:
                if (!weatherSources_.DialogOpen()) {
                    if (!weatherSourcesDragged_ &&
                        std::fabs(raw.y - weatherSourcesDownY_) > 8.0 * DisplayDensity()) {
                        weatherSourcesDragged_ = true;
                    }
                    if (weatherSourcesDragged_) {
                        needsFrame = weatherSources_.ScrollBy(raw.y - weatherSourcesLastY_);
                    }
                }
                weatherSourcesLastY_ = raw.y;
                break;

            case TouchSample::Phase::Up:
                if (!weatherSourcesDragged_) {
                    hit = weatherSources_.HitTest(raw.x, raw.y);
                    needsFrame = true;
                }
                weatherSourcesDragged_ = false;
                break;

            case TouchSample::Phase::Cancel:
                weatherSourcesDragged_ = false;
                break;
        }
    }

    if (hit.action == WeatherSourcesAction::Close) {
        CloseWeatherSources();
        return;
    }
    if (hit.action == WeatherSourcesAction::Navigate) {
        if (navigate_ != nullptr) {
            Navigate(hit.destination);
        }
        return;
    }
    if (hit.action == WeatherSourcesAction::Changed) {
        // Stored by the shell and pushed straight back, as every other settings screen works.
        if (settingsEdit_ != nullptr) {
            settingsEdit_(hit.key + "|" + hit.value);
        }
        RenderFrame();
        return;
    }
    if (needsFrame) {
        RenderFrame();
    }
}

/**
 * Screen 4. Called with the lock already held, from HandleTap.
 *
 * The screen takes a COPY of the current scene's weather rather than a reference: it outlives the
 * tap, a refresh can land under it, and every other pushed screen in this port owns its data the
 * same way.
 */
void Renderer::OpenDetailsLocked(DetailTag tag, int dayIndex) {
    details_.SetGeometry(static_cast<double>(width_), static_cast<double>(height_),
                         DisplayDensity(), topInset_, bottomInset_);
    /*
     * DetailsScreen.kt:121 — `ThemeManager.isLightTheme(context, detailsUiState.location)`, then
     * `BreezyWeatherTheme(!isLightTheme)` on :129. Screen 4 follows the LOCATION's day/night, not
     * the system: a night location paints the whole details window near-black on Android.
     *
     * This was AppIsDark(), which is the SETTINGS rule (SettingsActivity takes BreezyWeatherTheme's
     * default). Under the shipped "follow system" on a light phone that pinned screen 4 light, so a
     * night location opened a white details page over a correctly dark main screen.
     */
    details_.SetDarkMode(!IsLightTheme(scene_.daylight));
    const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    /*
     * The per-feature source columns are not carried on the C++ side — the shell owns them — so
     * both gates fall to the DATA, which is the half of ArkUIApp's union that this port can
     * answer. Documented there too; the two ports produce the same list for an Open-Meteo
     * location, which is the only source with a client.
     */
    details_.Open(scene_.weather, scene_.utcOffsetSeconds, tag, dayIndex, nowMs, false, false,
                  scene_.latitude, scene_.longitude);
    screen_ = Screen::Details;
}

void Renderer::CloseDetails() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (screen_ != Screen::Details) {
            return;
        }
        screen_ = Screen::Main;
    }
    RenderFrame();
}

void Renderer::OnDetailsTouch(const TouchSample& raw) {
    DetailsHit hit;
    bool needsFrame = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        switch (raw.phase) {
            case TouchSample::Phase::Down:
                detailsDownX_ = raw.x;
                detailsDownY_ = raw.y;
                detailsLastX_ = raw.x;
                detailsLastY_ = raw.y;
                detailsLastTimeMs_ = raw.timeMs;
                detailsVelocityX_ = 0.0;
                detailsVelocityY_ = 0.0;
                detailsDragged_ = false;
                /*
                 * The day pager scrolls sideways; everything below it scrolls down the page.
                 * Measured from the row the screen actually drew rather than guessed from the bar
                 * height plus a constant — the row's height depends on the text metrics.
                 */
                detailsDragTabs_ = raw.y < details_.TabRowBottom();
                // Touching down stops whatever was still gliding, as it does on Android.
                details_.StopScrolling();
                return;

            case TouchSample::Phase::Move: {
                const double dx = raw.x - detailsDownX_;
                const double dy = raw.y - detailsDownY_;
                if (!detailsDragged_ &&
                    std::sqrt(dx * dx + dy * dy) > 8.0 * DisplayDensity()) {
                    detailsDragged_ = true;
                }
                /*
                 * A touch that lands on the CHART drives the marker, not a scroll. Tried first so
                 * a slow drag along the plot reads hour by hour, which is what the Compose marker
                 * does; MoveMarker declines anything outside the chart's band.
                 */
                if (!detailsDragTabs_ && details_.MoveMarker(raw.x, raw.y)) {
                    needsFrame = true;
                    detailsLastX_ = raw.x;
                    detailsLastY_ = raw.y;
                    detailsLastTimeMs_ = raw.timeMs;
                    break;
                }
                if (detailsDragged_) {
                    // The same 0.7 blend the main screen uses, so one jittery sample cannot decide
                    // the fling.
                    const double dt = std::max(0.001, (raw.timeMs - detailsLastTimeMs_) / 1000.0);
                    const double weight = 0.7;
                    if (detailsDragTabs_) {
                        const double stepX = raw.x - detailsLastX_;
                        needsFrame = details_.ScrollTabsBy(stepX);
                        detailsVelocityX_ =
                            detailsVelocityX_ * (1.0 - weight) + (-stepX / dt) * weight;
                    } else {
                        const double stepY = raw.y - detailsLastY_;
                        needsFrame = details_.ScrollBy(stepY);
                        detailsVelocityY_ =
                            detailsVelocityY_ * (1.0 - weight) + (-stepY / dt) * weight;
                    }
                }
                detailsLastX_ = raw.x;
                detailsLastY_ = raw.y;
                detailsLastTimeMs_ = raw.timeMs;
                break;
            }

            case TouchSample::Phase::Up:
                // Lifting hides the marker, which is `onHidden`.
                needsFrame = details_.ClearMarker() || needsFrame;
                if (!detailsDragged_) {
                    hit = details_.HitTest(raw.x, raw.y);
                    needsFrame = true;
                } else {
                    // Lifting hands the gesture to the flywheel; OnVSync advances it from here.
                    if (detailsDragTabs_) {
                        details_.FlingTabs(detailsVelocityX_);
                    } else {
                        details_.FlingContent(detailsVelocityY_);
                    }
                    needsFrame = true;
                }
                detailsDragged_ = false;
                break;

            case TouchSample::Phase::Cancel:
                detailsDragged_ = false;
                details_.StopScrolling();
                break;
        }
    }

    if (hit.action == DetailsAction::Close) {
        CloseDetails();
        return;
    }
    if (needsFrame) {
        RenderFrame();
    }
}

/**
 * Which nowcast bucket a touch is over, or -1 — the marker's whole hit test.
 *
 * `nowcastChart_` is in PAGE coordinates, because that is what the card drew in; the sample is in
 * screen ones, so the toolbar and the page scroll come off first. Called on Down and on every Move,
 * and it deliberately does NOT claim the gesture: a drag that turns into a scroll still scrolls.
 */
void Renderer::UpdateNowcastMarker(double x, double y) {
    if (!nowcastChart_.valid || nowcastChart_.buckets <= 0) {
        nowcastMarker_ = -1;
        return;
    }
    const double barHeight = dimens::kActionBarHeight * DisplayDensity();
    const double pageY = y - barHeight + pageScroll_.Position();
    if (x < nowcastChart_.left || x > nowcastChart_.right || pageY < nowcastChart_.top ||
        pageY > nowcastChart_.bottom) {
        nowcastMarker_ = -1;
        return;
    }
    const double slot =
        (nowcastChart_.right - nowcastChart_.left) / static_cast<double>(nowcastChart_.buckets);
    int index = static_cast<int>((x - nowcastChart_.left) / slot);
    index = std::max(0, std::min(index, nowcastChart_.buckets - 1));
    nowcastMarker_ = index;
}

/**
 * MainActivity.kt:897-904 — the toolbar's edit icon.
 *
 * Refused while a refresh is in flight, with `message_please_wait_refresh` (strings.xml:553), which
 * is the same refusal screen 2's swipe already makes. Called with the lock held.
 */
void Renderer::OpenEditDialogLocked() {
    if (refreshing_) {
        // strings.xml:553, no action button — the same refusal screen 2's swipe makes.
        snackbar_.Show("Please wait for refresh to complete", "");
        return;
    }
    SourceLocation location;
    LocationSources sources;
    if (!DetailsFor(currentLocation_, &location, &sources)) {
        // kt:488 — `location?.let { ... }`; with nothing selected the dialog does not open.
        return;
    }
    prefsDialog_.SetLocationService(locationService_);
    prefsDialog_.Open(currentLocation_, location, sources,
                      CityAndDistrict(DetailField(currentLocation_, 7),
                                      DistrictFor(currentLocation_)),
                      CustomNameFor(currentLocation_), WeatherModelsFor(currentLocation_),
                      // kt:530 — no Delete button while this is the only saved location.
                      locationDetails_.size() > 1);
}

/** The edit dialog owns the whole gesture while it is up, over the main screen. */
void Renderer::OnPrefsDialogTouch(const TouchSample& sample) {
    const double slop = 8.0 * DisplayDensity();
    LocationPrefsHit hit;
    int index = -1;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        index = prefsDialog_.Index();
        switch (sample.phase) {
            case TouchSample::Phase::Down:
                listDownX_ = sample.x;
                listDownY_ = sample.y;
                listLastY_ = sample.y;
                listDragged_ = false;
                return;
            case TouchSample::Phase::Move:
                if (std::fabs(sample.y - listDownY_) > slop) {
                    listDragged_ = true;
                }
                if (listDragged_) {
                    prefsDialog_.ScrollBy(sample.y - listLastY_);
                    listLastY_ = sample.y;
                }
                break;
            case TouchSample::Phase::Up:
                if (!listDragged_) {
                    hit = prefsDialog_.OnTap(listDownX_, listDownY_);
                }
                break;
            case TouchSample::Phase::Cancel:
                listDragged_ = false;
                break;
        }
    }

    switch (hit.action) {
        case LocationPrefsAction::OpenSources: {
            // kt:147-174 — the same SecondarySourcesPreference the location list opens, on the
            // location this dialog is editing.
            std::lock_guard<std::mutex> lock(mutex_);
            SourceLocation location;
            LocationSources sources;
            // The saved ids are already on the dialog — SetSavedLocationIds pushes them there
            // whenever the list changes, so there is nothing to re-supply here.
            if (DetailsFor(index, &location, &sources)) {
                sourcesDialog_.OpenForUpdate(index, location,
                                             CityAndDistrict(DetailField(index, 7),
                                                             DistrictFor(index)),
                                             sources);
            }
            break;
        }
        case LocationPrefsAction::SavePrefs:
            if (listEdit_ != nullptr) {
                listEdit_("prefs|" + std::to_string(index) + "|" + hit.customName + "|" +
                          hit.weatherModels);
            }
            break;
        case LocationPrefsAction::Delete:
            // kt:577-581 — deleteLocation(it). The shell owns the list and its caches.
            if (listEdit_ != nullptr) {
                listEdit_("delete|" + std::to_string(index));
            }
            break;
        case LocationPrefsAction::OpenMainScreenSettings:
            // kt:143-144 — startMainScreenSettingsActivity, and the dialog closes behind it.
            OpenMainSettings();
            return;
        case LocationPrefsAction::SetLocationService:
            // kt:108-110 — the GLOBAL `location_service` setting, then the dialog closes.
            {
                std::lock_guard<std::mutex> lock(mutex_);
                locationService_ = hit.sourceId;
                location_.SetValues(hit.sourceId, std::string());
            }
            if (settingsEdit_ != nullptr) {
                settingsEdit_("location_service|" + hit.sourceId);
            }
            break;
        case LocationPrefsAction::OpenTextEntry:
            // Seed BEFORE opening the session, as the API-key field does — the reserved key the
            // settings channel already carries for exactly this.
            if (settingsEdit_ != nullptr) {
                settingsEdit_("__ime_seed__|" + hit.customName);
            }
            if (timeEntry_ != nullptr) {
                timeEntry_(2);  // 2 = a TEXT session.
            }
            break;
        case LocationPrefsAction::Dismiss:
        case LocationPrefsAction::Redraw:
        case LocationPrefsAction::None:
            break;
    }

    /*
     * Leaving the name field takes the keyboard away again. Asked after the switch because several
     * of the branches above close the field as a side effect of leaving the dialog.
     */
    bool wantsText = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        wantsText = prefsDialog_.TextEntryOpen();
    }
    if (!wantsText && hit.action != LocationPrefsAction::OpenTextEntry && timeEntry_ != nullptr &&
        sample.phase == TouchSample::Phase::Up) {
        timeEntry_(0);  // 0 = no session.
    }

    if (hit.action != LocationPrefsAction::None ||
        (sample.phase == TouchSample::Phase::Move && listDragged_)) {
        RenderFrame();
    }
}

void Renderer::NavigateLocked(const std::string& destination, const std::string& detail) {
    /*
     * "About" is a REAL screen, and every settings screen's ⓘ asks for it — AboutActivityIconButton
     * (AppBar.kt:32) has nine callers besides the root. Only the settings root used to open it; the
     * other nine fell through to the placeholder. Intercepted here rather than in ten touch
     * handlers, and inline rather than by calling OpenAbout(), which would re-take the lock.
     */
    if (destination == "About") {
        aboutReturn_ = screen_;
        about_.Reset();
        screen_ = Screen::About;
        return;
    }
    if (screen_ != Screen::Placeholder) {
        placeholderReturn_ = screen_;
    }
    placeholder_.Open(destination, detail);
    screen_ = Screen::Placeholder;
    // The shell still hears about it — it may log, and it owns nothing else here.
    if (navigate_ != nullptr) {
        navigate_(destination);
    }
}

void Renderer::Navigate(const std::string& destination, const std::string& detail) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        NavigateLocked(destination, detail);
    }
    RenderFrame();
}

void Renderer::ClosePlaceholder() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        screen_ = placeholderReturn_;
    }
    RenderFrame();
}

void Renderer::SetDisplayLists(const std::string& cards, const std::string& daily,
                               const std::string& hourly) {
    bool refresh = false;
    DisplayList open = DisplayList::Blocks;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const bool cardsChanged = cardDisplay_ != cards;
        cardDisplay_ = cards;
        dailyTrendDisplay_ = daily;
        hourlyTrendDisplay_ = hourly;
        // The weather page is laid out from this list, so a change to it has to rebuild the page's
        // card sequence — otherwise the manage screen's edit is only visible after a refresh.
        if (cardsChanged && scene_.hasWeather) {
            order_ = BuildCardOrder();
            backgroundValid_ = false;
            lastFrameWasComplete_ = false;
        }
        // A tab list can SHRINK — removing tabs is what the manage screens are for — so the
        // selected index has to come back into range before anything indexes with it.
        if (dailyTab_ >= static_cast<int>(DailyTabs().size())) {
            dailyTab_ = 0;
        }
        if (hourlyTab_ >= static_cast<int>(HourlyTabs().size())) {
            hourlyTab_ = 0;
        }
        // A push that lands while the screen is up is the echo of its own write; re-seed it so the
        // stored value and the drawn one cannot diverge, exactly as the other settings screens do.
        refresh = screen_ == Screen::DisplayManage;
        if (refresh) {
            open = displayManage_.List();
        }
    }
    if (refresh) {
        std::lock_guard<std::mutex> lock(mutex_);
        const double scroll = 0.0;
        (void)scroll;
        displayManage_.Open(open, open == DisplayList::Blocks ? cardDisplay_
                                  : open == DisplayList::Daily ? dailyTrendDisplay_
                                                               : hourlyTrendDisplay_);
    }
    RenderFrame();
}

/**
 * kt:186-199 — the write happens on the way out, and only when the list changed. Called from
 * CloseDisplayManage and before a gear opens a second list over the first.
 */
void Renderer::FlushDisplayManage() {
    std::string command;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (screen_ != Screen::DisplayManage || !displayManage_.Dirty()) {
            return;
        }
        command = DisplayListKey(displayManage_.List()) + "|" + displayManage_.Value();
        displayManage_.MarkClean();
    }
    if (settingsEdit_ != nullptr) {
        settingsEdit_(command);
    }
}

void Renderer::OpenDisplayManage(DisplayList list) {
    // A gear opens a second list over this one; the first one's edits are written first, as
    // finishing one Activity before starting another would.
    FlushDisplayManage();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::string& value = list == DisplayList::Blocks ? cardDisplay_
                                   : list == DisplayList::Daily ? dailyTrendDisplay_
                                                                : hourlyTrendDisplay_;
        // A gear opens a second manage screen OVER the first, as startActivity does.
        if (screen_ == Screen::DisplayManage) {
            displayManageStack_.push_back(displayManage_.List());
        } else {
            displayManageStack_.clear();
        }
        displayManage_.Open(list, value);
        screen_ = Screen::DisplayManage;
    }
    RenderFrame();
}

void Renderer::CloseDisplayManage() {
    FlushDisplayManage();
    DisplayList back = DisplayList::Blocks;
    bool nested = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        nested = !displayManageStack_.empty();
        if (nested) {
            back = displayManageStack_.back();
            displayManageStack_.pop_back();
        } else {
            screen_ = Screen::MainSettings;
        }
    }
    if (nested) {
        // Back out of a gear-opened list into the Blocks screen that opened it.
        {
            std::lock_guard<std::mutex> lock(mutex_);
            displayManage_.Open(back, back == DisplayList::Blocks ? cardDisplay_
                                      : back == DisplayList::Daily ? dailyTrendDisplay_
                                                                   : hourlyTrendDisplay_);
        }
    }
    RenderFrame();
}

/**
 * Screens 3h/3i/3j own the whole gesture: the screen itself decides between a reorder, a swipe and
 * a scroll, because all three start the same way and only the handle tells them apart.
 */
void Renderer::OnDisplayManageTouch(const TouchSample& raw) {
    DisplayManageHit hit;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        int phase = 0;
        switch (raw.phase) {
            case TouchSample::Phase::Down:
                phase = 0;
                break;
            case TouchSample::Phase::Move:
                phase = 1;
                break;
            case TouchSample::Phase::Up:
                phase = 2;
                break;
            case TouchSample::Phase::Cancel:
                phase = 3;
                break;
        }
        hit = displayManage_.OnTouch(phase, raw.x, raw.y);
    }

    if (hit.action == DisplayManageAction::Close) {
        CloseDisplayManage();
        return;
    }
    if (hit.action == DisplayManageAction::OpenList) {
        OpenDisplayManage(DisplayListFromId(hit.destination));
        return;
    }
    if (hit.action == DisplayManageAction::Changed) {
        /*
         * NOT written here. CardDisplayManageActivity.kt:186-199 writes in `onStop`, and only if the
         * list actually changed — precisely so a drag does not write once per frame. ArkUIApp does
         * the same from `onPageHide`; this screen's equivalent is FlushDisplayManage, called when it
         * closes or when a gear opens another list over it.
         */
        RenderFrame();
        return;
    }
    RenderFrame();
}

void Renderer::OpenDebug() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        screen_ = Screen::Debug;
    }
    RenderFrame();
}

void Renderer::CloseDebug() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        screen_ = Screen::Settings;
    }
    RenderFrame();
}

/** Screen 3g has one row and no scroll, so a tap is all there is to handle. */
void Renderer::OnDebugTouch(const TouchSample& raw) {
    if (raw.phase != TouchSample::Phase::Up) {
        return;
    }
    DebugHit hit;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        hit = debug_.HitTest(raw.x, raw.y);
    }
    if (hit.action == DebugAction::Close) {
        CloseDebug();
        return;
    }
    if (hit.action == DebugAction::Navigate && navigate_ != nullptr) {
        Navigate(hit.destination);
    }
}

/**
 * Worker info returns to Background updates, not to the settings root — it is opened from that
 * screen's Troubleshoot section, so that is what Back has to reveal.
 */
void Renderer::OpenModules() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        modules_.ResetScroll();
        screen_ = Screen::Modules;
    }
    RenderFrame();
}

void Renderer::CloseModules() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        screen_ = Screen::Settings;
    }
    RenderFrame();
}

void Renderer::SetModuleSettings(const std::string& weekIconMode, bool monochromeIcons,
                                 bool notificationWidget, bool notificationPersistent,
                                 const std::string& notificationStyle, bool notificationTempIcon,
                                 bool notificationFeelsLike) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        modules_.SetValues(weekIconMode, monochromeIcons, notificationWidget,
                           notificationPersistent, notificationStyle, notificationTempIcon,
                           notificationFeelsLike);
    }
    if (screen_ == Screen::Modules) {
        RenderFrame();
    }
}

/** Screen 3e scrolls, so a drag has to be told apart from a tap the way screen 3b's is. */
void Renderer::OnModulesTouch(const TouchSample& raw) {
    bool needsFrame = false;
    if (raw.phase == TouchSample::Phase::Down) {
        modulesDownY_ = raw.y;
        modulesLastY_ = raw.y;
        modulesDragged_ = false;
        return;
    }
    if (raw.phase == TouchSample::Phase::Move) {
        if (!modulesDragged_ &&
            std::fabs(raw.y - modulesDownY_) > 8.0 * DisplayDensity()) {
            modulesDragged_ = true;
        }
        if (modulesDragged_) {
            std::lock_guard<std::mutex> lock(mutex_);
            needsFrame = modules_.ScrollBy(raw.y - modulesLastY_);
        }
        modulesLastY_ = raw.y;
        if (needsFrame) {
            RenderFrame();
        }
        return;
    }
    if (raw.phase != TouchSample::Phase::Up || modulesDragged_) {
        return;
    }

    ModulesHit hit;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        hit = modules_.HitTest(raw.x, raw.y);
    }
    if (hit.action == ModulesAction::Close) {
        CloseModules();
        return;
    }
    if (hit.action == ModulesAction::Navigate && navigate_ != nullptr) {
        Navigate(hit.destination);
        return;
    }
    if (hit.action == ModulesAction::Changed && settingsEdit_ != nullptr) {
        // The shell stores it and pushes the values back through SetModuleSettings.
        settingsEdit_(hit.key + "|" + hit.value);
    }
    RenderFrame();
}

void Renderer::OpenWorkerInfo() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        screen_ = Screen::WorkerInfo;
    }
    RenderFrame();
}

void Renderer::CloseWorkerInfo() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        screen_ = Screen::BackgroundUpdates;
    }
    RenderFrame();
}

/** Three text blocks and no scroll, so a tap on the app bar is all there is to handle. */
void Renderer::OnWorkerInfoTouch(const TouchSample& raw) {
    if (raw.phase != TouchSample::Phase::Up) {
        return;
    }
    WorkerInfoHit hit;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        hit = workerInfo_.HitTest(raw.x, raw.y);
    }
    if (hit.action == WorkerInfoAction::Close) {
        CloseWorkerInfo();
        return;
    }
    if (hit.action == WorkerInfoAction::Navigate && navigate_ != nullptr) {
        Navigate(hit.destination);
    }
}

void Renderer::OpenBackgroundUpdates() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        screen_ = Screen::BackgroundUpdates;
        backgroundUpdates_.ResetScroll();
        backgroundUpdates_.CloseDialog();
    }
    RenderFrame();
}

void Renderer::CloseBackgroundUpdates() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        screen_ = Screen::Settings;
    }
    RenderFrame();
}

/**
 * Screen 3c's touches. The two settings it edits belong to the SHELL's preferences bag, so a change
 * goes out through the same `setting_` channel every other settings screen uses; the shell writes
 * it and pushes the new value back with `SetBackgroundUpdatesValues`.
 */
void Renderer::OnBackgroundUpdatesTouch(const TouchSample& raw) {
    if (raw.phase != TouchSample::Phase::Up) {
        return;
    }
    BackgroundHit hit;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        hit = backgroundUpdates_.HitTest(raw.x, raw.y);
    }
    if (hit.action == BackgroundAction::Close) {
        CloseBackgroundUpdates();
        return;
    }
    if (hit.action == BackgroundAction::Navigate) {
        // Worker info is a real screen now; the other two Troubleshoot rows lead to an Android
        // intent and a web page, neither of which this port owns, so they keep the placeholder.
        if (hit.destination == "Worker info") {
            OpenWorkerInfo();
            return;
        }
        if (navigate_ != nullptr) {
            Navigate(hit.destination);
        }
        return;
    }
    if (hit.action == BackgroundAction::Changed && settingsEdit_ != nullptr) {
        // The shell stores it and pushes the values back through SetBackgroundUpdatesValues —
        // this side does not apply the change itself, as every other settings screen works.
        settingsEdit_(hit.key + "|" + hit.value);
    }
    RenderFrame();
}

void Renderer::OpenAbout() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        aboutReturn_ = screen_;
        about_.Reset();
        screen_ = Screen::About;
    }
    RenderFrame();
}

/**
 * Back goes to whatever OPENED About, not always to the settings root.
 *
 * AboutActivity is an activity: `finish()` returns to whichever screen started it, and nine of the
 * ten that can are settings SUB-screens. Returning to Settings unconditionally would have skipped a
 * level for all nine.
 */
void Renderer::CloseAbout() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        screen_ = aboutReturn_;
    }
    RenderFrame();
}

/** The About screen's gestures — a scrolling list of rows, the same shape as screen 3c's. */
void Renderer::OnAboutTouch(const TouchSample& raw) {
    AboutHit hit;
    bool needsFrame = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        switch (raw.phase) {
            case TouchSample::Phase::Down:
                about_.StopScrolling();
                aboutDownY_ = raw.y;
                aboutLastY_ = raw.y;
                aboutDragged_ = false;
                return;

            case TouchSample::Phase::Move:
                if (!aboutDragged_ && std::fabs(raw.y - aboutDownY_) > 8.0 * DisplayDensity()) {
                    aboutDragged_ = true;
                }
                if (aboutDragged_) {
                    needsFrame = about_.ScrollBy(raw.y - aboutLastY_);
                }
                aboutLastY_ = raw.y;
                break;

            case TouchSample::Phase::Up:
                if (!aboutDragged_) {
                    hit = about_.HitTest(raw.x, raw.y);
                    needsFrame = true;
                }
                aboutDragged_ = false;
                break;

            case TouchSample::Phase::Cancel:
                aboutDragged_ = false;
                break;
        }
    }

    if (hit.action == AboutAction::Close) {
        CloseAbout();
        return;
    }
    if (hit.action == AboutAction::Navigate) {
        if (hit.destination == "Privacy policy") {
            OpenPrivacyPolicy();
            return;
        }
        if (hit.destination == "Dependencies") {
            OpenDependencies();
            return;
        }
        if (navigate_ != nullptr) {
            Navigate(hit.destination);
        }
        return;
    }
    if (hit.action == AboutAction::OpenLink && openLink_ != nullptr) {
        openLink_(hit.destination);
        return;
    }
    if (needsFrame) {
        RenderFrame();
    }
}

void Renderer::OpenPrivacyPolicy() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        privacy_.Reset();
        screen_ = Screen::PrivacyPolicy;
    }
    RenderFrame();
}

void Renderer::ClosePrivacyPolicy() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        screen_ = Screen::About;
    }
    RenderFrame();
}

/** The same gesture shape the About screen has: a vertical drag scrolls, a tap acts. */
void Renderer::OnPrivacyPolicyTouch(const TouchSample& raw) {
    PrivacyHit hit;
    bool needsFrame = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        switch (raw.phase) {
            case TouchSample::Phase::Down:
                privacy_.StopScrolling();
                privacyDownY_ = raw.y;
                privacyLastY_ = raw.y;
                privacyDragged_ = false;
                return;

            case TouchSample::Phase::Move:
                if (!privacyDragged_ &&
                    std::fabs(raw.y - privacyDownY_) > 8.0 * DisplayDensity()) {
                    privacyDragged_ = true;
                }
                if (privacyDragged_) {
                    needsFrame = privacy_.ScrollBy(raw.y - privacyLastY_);
                }
                privacyLastY_ = raw.y;
                break;

            case TouchSample::Phase::Up:
                if (!privacyDragged_) {
                    hit = privacy_.HitTest(raw.x, raw.y);
                    needsFrame = true;
                }
                privacyDragged_ = false;
                break;

            case TouchSample::Phase::Cancel:
                privacyDragged_ = false;
                break;
        }
    }

    if (hit.action == PrivacyAction::Close) {
        ClosePrivacyPolicy();
        return;
    }
    if (hit.action == PrivacyAction::OpenLink && openLink_ != nullptr) {
        openLink_(hit.destination);
        return;
    }
    if (needsFrame) {
        RenderFrame();
    }
}

void Renderer::OpenDependencies() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        dependencies_.Reset();
        screen_ = Screen::Dependencies;
    }
    RenderFrame();
}

void Renderer::CloseDependencies() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        screen_ = Screen::About;
    }
    RenderFrame();
}

/** The same gesture shape again — a vertical drag scrolls, a tap acts. */
void Renderer::OnDependenciesTouch(const TouchSample& raw) {
    DependenciesHit hit;
    bool needsFrame = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        switch (raw.phase) {
            case TouchSample::Phase::Down:
                dependencies_.StopScrolling();
                dependenciesDownY_ = raw.y;
                dependenciesLastY_ = raw.y;
                dependenciesDragged_ = false;
                return;

            case TouchSample::Phase::Move:
                if (!dependenciesDragged_ &&
                    std::fabs(raw.y - dependenciesDownY_) > 8.0 * DisplayDensity()) {
                    dependenciesDragged_ = true;
                }
                if (dependenciesDragged_) {
                    needsFrame = dependencies_.ScrollBy(raw.y - dependenciesLastY_);
                }
                dependenciesLastY_ = raw.y;
                break;

            case TouchSample::Phase::Up:
                if (!dependenciesDragged_) {
                    hit = dependencies_.HitTest(raw.x, raw.y);
                    needsFrame = true;
                }
                dependenciesDragged_ = false;
                break;

            case TouchSample::Phase::Cancel:
                dependenciesDragged_ = false;
                break;
        }
    }

    if (hit.action == DependenciesAction::Close) {
        CloseDependencies();
        return;
    }
    if (needsFrame) {
        RenderFrame();
    }
}

/** Caller holds `mutex_` — the alert card's hit test already does. */
void Renderer::OpenAlertListLocked(const std::string& alertId) {
    alertList_.SetAlerts(scene_.weather.alertList, scene_.utcOffsetSeconds, scene_.is24Hour);
    alertList_.SetInitialAlert(alertId);
    alertList_.Reset();
    screen_ = Screen::AlertList;
}

void Renderer::CloseAlertList() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        screen_ = Screen::Main;
    }
    RenderFrame();
}

void Renderer::OnAlertListTouch(const TouchSample& raw) {
    AlertListHit hit;
    bool needsFrame = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        switch (raw.phase) {
            case TouchSample::Phase::Down:
                alertList_.StopScrolling();
                alertListDownY_ = raw.y;
                alertListLastY_ = raw.y;
                alertListDragged_ = false;
                return;

            case TouchSample::Phase::Move:
                if (!alertListDragged_ &&
                    std::fabs(raw.y - alertListDownY_) > 8.0 * DisplayDensity()) {
                    alertListDragged_ = true;
                }
                if (alertListDragged_) {
                    needsFrame = alertList_.ScrollBy(raw.y - alertListLastY_);
                }
                alertListLastY_ = raw.y;
                break;

            case TouchSample::Phase::Up:
                if (!alertListDragged_) {
                    hit = alertList_.HitTest(raw.x, raw.y);
                    needsFrame = true;
                }
                alertListDragged_ = false;
                break;

            case TouchSample::Phase::Cancel:
                alertListDragged_ = false;
                break;
        }
    }

    if (hit.action == AlertListAction::Close) {
        CloseAlertList();
        return;
    }
    if (needsFrame) {
        RenderFrame();
    }
}

void Renderer::OpenPreviewIcon() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        previewIcon_.Reset();
        screen_ = Screen::PreviewIcon;
    }
    RenderFrame();
}

void Renderer::ClosePreviewIcon() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        screen_ = Screen::Appearance;
    }
    RenderFrame();
}

void Renderer::OnPreviewIconTouch(const TouchSample& raw) {
    PreviewIconHit hit;
    bool needsFrame = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        switch (raw.phase) {
            case TouchSample::Phase::Down:
                previewIcon_.StopScrolling();
                previewIconDownY_ = raw.y;
                previewIconLastY_ = raw.y;
                previewIconDragged_ = false;
                return;

            case TouchSample::Phase::Move:
                if (!previewIconDragged_ &&
                    std::fabs(raw.y - previewIconDownY_) > 8.0 * DisplayDensity()) {
                    previewIconDragged_ = true;
                }
                if (previewIconDragged_) {
                    needsFrame = previewIcon_.ScrollBy(raw.y - previewIconLastY_);
                }
                previewIconLastY_ = raw.y;
                break;

            case TouchSample::Phase::Up:
                if (!previewIconDragged_) {
                    hit = previewIcon_.HitTest(raw.x, raw.y);
                    needsFrame = true;
                }
                previewIconDragged_ = false;
                break;

            case TouchSample::Phase::Cancel:
                previewIconDragged_ = false;
                break;
        }
    }

    if (hit.action == PreviewIconAction::Close) {
        ClosePreviewIcon();
        return;
    }
    if (hit.action == PreviewIconAction::AppDetails && openLink_ != nullptr) {
        /*
         * kt:121-129 — `action_about` opens the provider's system app-details page. The shell owns
         * every ability start, so it goes down the same channel the About screen's links use; the
         * scheme tells it this is not a URL to hand to a browser.
         */
        openLink_("app-details:");
        return;
    }
    if (needsFrame) {
        RenderFrame();
    }
}

void Renderer::OpenNotifications() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        notifications_.ResetScroll();
        notifications_.CloseDialog();
        screen_ = Screen::Notifications;
    }
    RenderFrame();
}

void Renderer::CloseNotifications() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        screen_ = Screen::Settings;
    }
    RenderFrame();
}

/**
 * Screen 3d's gestures, the same shape as 3c's — with one difference: the time picker STAYS OPEN
 * while its wheels are being tapped, so a hit that changes nothing still has to repaint.
 */
void Renderer::OnNotificationsTouch(const TouchSample& raw) {
    NotificationsHit hit;
    bool needsFrame = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        switch (raw.phase) {
            case TouchSample::Phase::Down:
                notificationsDownY_ = raw.y;
                notificationsLastY_ = raw.y;
                notificationsDragged_ = false;
                return;

            case TouchSample::Phase::Move:
                if (notifications_.DialogOpen()) {
                    // The clock dial's hand follows the finger, which is what makes it a dial
                    // rather than a set of tap targets.
                    needsFrame = notifications_.DragDial(raw.x, raw.y);
                } else {
                    if (!notificationsDragged_ &&
                        std::fabs(raw.y - notificationsDownY_) > 8.0 * DisplayDensity()) {
                        notificationsDragged_ = true;
                    }
                    if (notificationsDragged_) {
                        needsFrame = notifications_.ScrollBy(raw.y - notificationsLastY_);
                    }
                }
                notificationsLastY_ = raw.y;
                break;

            case TouchSample::Phase::Up:
                if (!notificationsDragged_) {
                    hit = notifications_.HitTest(raw.x, raw.y);
                    needsFrame = true;
                }
                notificationsDragged_ = false;
                break;

            case TouchSample::Phase::Cancel:
                notificationsDragged_ = false;
                break;
        }
    }

    if (hit.action == NotificationsAction::Close) {
        CloseNotifications();
        return;
    }
    if (hit.action == NotificationsAction::Navigate) {
        if (navigate_ != nullptr) {
            Navigate(hit.destination);
        }
        return;
    }
    if (hit.action == NotificationsAction::Changed) {
        // The shell stores it and pushes the values back through SetNotificationSettings — this
        // side does not apply the change itself, exactly as every other settings screen works.
        if (settingsEdit_ != nullptr) {
            settingsEdit_(hit.key + "|" + hit.value);
        }
        RenderFrame();
        return;
    }
    if (needsFrame) {
        RenderFrame();
    }
}

void Renderer::OpenMainSettings() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        mainSettings_.ResetScroll();
        mainSettings_.CloseDialog();
        screen_ = Screen::MainSettings;
    }
    RenderFrame();
}

void Renderer::CloseMainSettings() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        screen_ = Screen::Settings;
    }
    RenderFrame();
}

/** Screen 3c's gestures, the same shape as 3a's and 3b's. */
void Renderer::OnMainSettingsTouch(const TouchSample& raw) {
    MainSettingsHit hit;
    bool needsFrame = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        switch (raw.phase) {
            case TouchSample::Phase::Down:
                mainSettingsDownY_ = raw.y;
                mainSettingsLastY_ = raw.y;
                mainSettingsDragged_ = false;
                return;

            case TouchSample::Phase::Move:
                if (!mainSettings_.DialogOpen()) {
                    if (!mainSettingsDragged_ &&
                        std::fabs(raw.y - mainSettingsDownY_) > 8.0 * DisplayDensity()) {
                        mainSettingsDragged_ = true;
                    }
                    if (mainSettingsDragged_) {
                        needsFrame = mainSettings_.ScrollBy(raw.y - mainSettingsLastY_);
                    }
                }
                mainSettingsLastY_ = raw.y;
                break;

            case TouchSample::Phase::Up:
                if (!mainSettingsDragged_) {
                    hit = mainSettings_.HitTest(raw.x, raw.y);
                    needsFrame = true;
                }
                mainSettingsDragged_ = false;
                break;

            case TouchSample::Phase::Cancel:
                mainSettingsDragged_ = false;
                break;
        }
    }

    if (hit.action == MainSettingsAction::Close) {
        CloseMainSettings();
        return;
    }
    if (hit.action == MainSettingsAction::Navigate) {
        // The three Displayed-data rows are real screens now (kt:90-120's start*ManageActivity).
        if (hit.destination == "Blocks") {
            OpenDisplayManage(DisplayList::Blocks);
            return;
        }
        if (hit.destination == "Daily trends") {
            OpenDisplayManage(DisplayList::Daily);
            return;
        }
        if (hit.destination == "Hourly trends") {
            OpenDisplayManage(DisplayList::Hourly);
            return;
        }
        if (navigate_ != nullptr) {
            Navigate(hit.destination);
        }
        return;
    }
    if (hit.action == MainSettingsAction::Changed) {
        // The shell stores it and pushes the values back through SetMainScreenSettings — this side
        // does not apply the change itself, exactly as screens 3a and 3b work.
        if (settingsEdit_ != nullptr) {
            settingsEdit_(hit.key + "|" + hit.value);
        }
        RenderFrame();
        return;
    }
    if (needsFrame) {
        RenderFrame();
    }
}

void Renderer::OpenUnits() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        units_.ResetScroll();
        units_.CloseDialog();
        screen_ = Screen::Units;
    }
    RenderFrame();
}

void Renderer::CloseUnits() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        screen_ = Screen::Appearance;
    }
    RenderFrame();
}

/** Screen 3b's gestures, identical in shape to 3a's: drag scrolls, tap acts, dialog takes all. */
void Renderer::OnUnitsTouch(const TouchSample& raw) {
    UnitsHit hit;
    bool needsFrame = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        switch (raw.phase) {
            case TouchSample::Phase::Down:
                unitsDownY_ = raw.y;
                unitsLastY_ = raw.y;
                unitsDragged_ = false;
                return;

            case TouchSample::Phase::Move:
                // A dialog does not scroll behind itself.
                if (!units_.DialogOpen()) {
                    if (!unitsDragged_ &&
                        std::fabs(raw.y - unitsDownY_) > 8.0 * DisplayDensity()) {
                        unitsDragged_ = true;
                    }
                    if (unitsDragged_) {
                        needsFrame = units_.ScrollBy(raw.y - unitsLastY_);
                    }
                }
                unitsLastY_ = raw.y;
                break;

            case TouchSample::Phase::Up:
                if (!unitsDragged_) {
                    hit = units_.HitTest(raw.x, raw.y);
                    // Opening or dismissing the dialog is a state change with no action attached.
                    needsFrame = true;
                }
                unitsDragged_ = false;
                break;

            case TouchSample::Phase::Cancel:
                unitsDragged_ = false;
                break;
        }
    }

    if (hit.action == UnitsAction::Close) {
        CloseUnits();
        return;
    }
    if (hit.action == UnitsAction::Navigate) {
        if (navigate_ != nullptr) {
            Navigate(hit.destination);
        }
        return;
    }
    if (hit.action == UnitsAction::Changed) {
        // The shell stores it and hands every value back through SetUnitSettings, which is what
        // re-formats the cards — this side does not apply the change itself.
        if (settingsEdit_ != nullptr) {
            settingsEdit_(hit.key + "|" + hit.value);
        }
        RenderFrame();
        return;
    }
    if (needsFrame) {
        RenderFrame();
    }
}

void Renderer::CloseLocationList() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        screen_ = Screen::Main;
        // The scene was held still while the list was up; the background has to be repainted.
        backgroundValid_ = false;
        lastFrameWasComplete_ = false;
        scratchValid_ = false;
    }
    RenderFrame();
}

void Renderer::RenderFrame() {
    std::lock_guard<std::mutex> renderLock(renderMutex_);
    if (window_ == nullptr || width_ == 0 || height_ == 0) {
        OH_LOG_WARN(LOG_APP, "RenderFrame with no surface (%{public}llu x %{public}llu)",
                    static_cast<unsigned long long>(width_),
                    static_cast<unsigned long long>(height_));
        return;
    }

    // 1. Take a buffer out of the surface and lock it for CPU access.
    //
    // NativeWindowRequestBuffer is the GPU-producer entry point: it hands back a buffer whose
    // virAddr is null, because nothing has mapped it into this process. LockBuffer is the
    // CPU-drawing counterpart â€” it maps the memory, which is the whole premise of this renderer.
    // The empty Region marks the entire buffer dirty.
    OHNativeWindowBuffer* buffer = nullptr;
    Region region{nullptr, 0};
    const int32_t status = OH_NativeWindow_LockBuffer(window_, region, &buffer);
    if (status != 0 || buffer == nullptr) {
        OH_LOG_ERROR(LOG_APP, "LockBuffer failed: %{public}d", status);
        return;
    }

    BufferHandle* handle = OH_NativeWindow_GetBufferHandleFromNative(buffer);
    if (handle == nullptr || handle->virAddr == nullptr) {
        OH_LOG_ERROR(LOG_APP, "locked buffer is unmapped: handle=%{public}d",
                     handle != nullptr ? 1 : 0);
        OH_NativeWindow_UnlockAndFlushBuffer(window_);
        return;
    }

    /*
     * 2. Draw into a scratch bitmap, kept for the life of the surface.
     *
     * It used to be created, built, and destroyed on every frame â€” on this device a 1260 x 2720 x 4
     * = 13.7 MB allocation per frame, inside an 8.3 ms budget at 120 Hz. It is now allocated once
     * per geometry and only cleared per frame.
     *
     * Drawing into the locked buffer directly, via OH_Drawing_BitmapCreateFromPixels, looks like
     * the obvious way to also drop the copy in step 3 â€” and it was tried and MEASURED: the frame
     * went from 37 ms to about 600 ms. `handle->virAddr` is uncached DMA memory, and every blend,
     * every antialiased edge and every glyph is a read-modify-write on it. A streaming memcpy of
     * the finished frame is the one access pattern that memory is good at, which is why the copy
     * stays.
     */
    if (scratch_ == nullptr || scratchWidth_ != handle->width ||
        scratchHeight_ != handle->height) {
        DestroyScratch();
        OH_Drawing_BitmapFormat format{kColorFormat, kAlphaFormat};
        scratch_ = OH_Drawing_BitmapCreate();
        OH_Drawing_BitmapBuild(scratch_, static_cast<uint32_t>(handle->width),
                               static_cast<uint32_t>(handle->height), &format);
        scratchCanvas_ = OH_Drawing_CanvasCreate();
        OH_Drawing_CanvasBind(scratchCanvas_, scratch_);
        background_ = OH_Drawing_BitmapCreate();
        OH_Drawing_BitmapBuild(background_, static_cast<uint32_t>(handle->width),
                               static_cast<uint32_t>(handle->height), &format);
        backgroundCanvas_ = OH_Drawing_CanvasCreate();
        OH_Drawing_CanvasBind(backgroundCanvas_, background_);
        // The cached sky, same size as the background it is blitted into.
        sky_ = OH_Drawing_BitmapCreate();
        OH_Drawing_BitmapBuild(sky_, static_cast<uint32_t>(handle->width),
                               static_cast<uint32_t>(handle->height), &format);
        skyCanvas_ = OH_Drawing_CanvasCreate();
        OH_Drawing_CanvasBind(skyCanvas_, sky_);
        skyValid_ = false;
        backgroundValid_ = false;
        scratchWidth_ = handle->width;
        scratchHeight_ = handle->height;
        // New buffers behind new geometry: nothing that was written before can be trusted.
        bufferFrame_.clear();
        lastFrameWasComplete_ = false;
        scratchValid_ = false;
    }

    OH_Drawing_Bitmap* bitmap = scratch_;
    OH_Drawing_Canvas* canvas = scratchCanvas_;

    /*
     * Screen 2 takes the whole surface while it is open, and repaints only the rows that moved.
     *
     * This branch used to force a full surface every frame, on the reasoning that a flat list with
     * a handful of rows gains nothing from the partial-frame machinery. Measuring said otherwise:
     * `breezy:CopyFlush` â€” the 1260 x 2720 x 4 = 13.7 MB scratch-to-DMA copy at the end of this
     * function â€” was 3.3 ms of a 7.0 ms frame at 120 Hz, for a swipe in which ONE row moves. The
     * clip and the banded copy were already here and already proven on the main screen; the only
     * thing missing was a band, which LocationListScreen now accumulates from its own mutators.
     *
     * The search screen keeps drawing in full: it is a text field whose content is not tracked, and
     * it is not on the path this project measures.
     */
    bool overlayScreen = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        overlayScreen = (screen_ != Screen::Main);
    }
    if (overlayScreen) {
        std::lock_guard<std::mutex> lock(mutex_);
        ++frameIndex_;

        // Ahead of the band, because a geometry or theme change is itself a reason there is not one.
        if (screen_ == Screen::MainSettings) {
            mainSettings_.SetGeometry(static_cast<double>(width_), static_cast<double>(height_),
                                      DisplayDensity(), topInset_, bottomInset_);
            mainSettings_.SetDarkMode(AppIsDark());
            mainSettings_.SetDisplayLists(cardDisplay_, dailyTrendDisplay_, hourlyTrendDisplay_);
        }
        if (screen_ == Screen::Notifications) {
            notifications_.SetGeometry(static_cast<double>(width_), static_cast<double>(height_),
                                       DisplayDensity(), topInset_, bottomInset_);
            notifications_.SetDarkMode(AppIsDark());
        }
        if (screen_ == Screen::Debug) {
            debug_.SetGeometry(static_cast<double>(width_), static_cast<double>(height_),
                               DisplayDensity(), topInset_, bottomInset_);
            debug_.SetDarkMode(AppIsDark());
        }
        if (screen_ == Screen::WorkerInfo) {
            workerInfo_.SetGeometry(static_cast<double>(width_), static_cast<double>(height_),
                                    DisplayDensity(), topInset_, bottomInset_);
            workerInfo_.SetDarkMode(AppIsDark());
        }
        if (screen_ == Screen::Modules) {
            modules_.SetGeometry(static_cast<double>(width_), static_cast<double>(height_),
                                 DisplayDensity(), topInset_, bottomInset_);
            modules_.SetDarkMode(AppIsDark());
        }
        if (screen_ == Screen::BackgroundUpdates) {
            backgroundUpdates_.SetGeometry(static_cast<double>(width_),
                                           static_cast<double>(height_), DisplayDensity(),
                                           topInset_, bottomInset_);
            backgroundUpdates_.SetDarkMode(AppIsDark());
        }
        if (screen_ == Screen::About) {
            about_.SetGeometry(static_cast<double>(width_), static_cast<double>(height_),
                               DisplayDensity(), topInset_, bottomInset_);
            about_.SetDarkMode(AppIsDark());
        }
        if (screen_ == Screen::PrivacyPolicy) {
            privacy_.SetGeometry(static_cast<double>(width_), static_cast<double>(height_),
                                 DisplayDensity(), topInset_, bottomInset_);
            privacy_.SetDarkMode(AppIsDark());
        }
        if (screen_ == Screen::Dependencies) {
            dependencies_.SetGeometry(static_cast<double>(width_), static_cast<double>(height_),
                                      DisplayDensity(), topInset_, bottomInset_);
            dependencies_.SetDarkMode(AppIsDark());
        }
        if (screen_ == Screen::AlertList) {
            alertList_.SetGeometry(static_cast<double>(width_), static_cast<double>(height_),
                                   DisplayDensity(), topInset_, bottomInset_);
            alertList_.SetDarkMode(AppIsDark());
        }
        if (screen_ == Screen::PreviewIcon) {
            previewIcon_.SetGeometry(static_cast<double>(width_), static_cast<double>(height_),
                                     DisplayDensity(), topInset_, bottomInset_);
            previewIcon_.SetDarkMode(AppIsDark());
        }
        if (screen_ == Screen::Location) {
            location_.SetGeometry(static_cast<double>(width_), static_cast<double>(height_),
                                  DisplayDensity(), topInset_, bottomInset_);
            location_.SetDarkMode(AppIsDark());
        }
        if (screen_ == Screen::WeatherSources) {
            weatherSources_.SetGeometry(static_cast<double>(width_), static_cast<double>(height_),
                                        DisplayDensity(), topInset_, bottomInset_);
            weatherSources_.SetDarkMode(AppIsDark());
        }
        if (screen_ == Screen::DisplayManage) {
            displayManage_.SetGeometry(static_cast<double>(width_), static_cast<double>(height_),
                                       DisplayDensity(), topInset_, bottomInset_);
            displayManage_.SetDarkMode(AppIsDark());
        }
        if (screen_ == Screen::Details) {
            details_.SetGeometry(static_cast<double>(width_), static_cast<double>(height_),
                                 DisplayDensity(), topInset_, bottomInset_);
            // The location's day/night, as in OpenDetailsLocked — see the note there.
            details_.SetDarkMode(!IsLightTheme(scene_.daylight));
            details_.SetKeyLinesEnabled(trendHorizontalLines_);
        }
        if (screen_ == Screen::Placeholder) {
            placeholder_.SetGeometry(static_cast<double>(width_), static_cast<double>(height_),
                                     DisplayDensity());
        }
        if (screen_ == Screen::Units) {
            units_.SetGeometry(static_cast<double>(width_), static_cast<double>(height_),
                               DisplayDensity(), topInset_, bottomInset_);
            units_.SetDarkMode(AppIsDark());
            units_.SyncValues();
        }
        if (screen_ == Screen::Appearance) {
            appearance_.SetGeometry(static_cast<double>(width_), static_cast<double>(height_),
                                    DisplayDensity(), topInset_, bottomInset_);
            appearance_.SetDarkMode(AppIsDark());
            appearance_.SetSystemDark(systemDarkMode_);
            appearance_.SetValues(
                darkModeSetting_ == DarkModeSetting::Light ? "light"
                    : darkModeSetting_ == DarkModeSetting::Dark ? "dark" : "system",
                dayNightForLocations_);
        } else if (screen_ == Screen::Settings) {
            settings_.SetGeometry(static_cast<double>(width_), static_cast<double>(height_),
                                  DisplayDensity(), topInset_, bottomInset_);
            /*
             * The SYSTEM colour mode, not `scene_.darkMode`.
             *
             * SettingsActivity takes BreezyWeatherTheme's default `isSystemInDarkTheme()`
             * (:107-111) where MainActivity passes the location's daylight. `systemDarkMode_` is
             * what the shell publishes from the ability configuration.
             */
            settings_.SetDarkMode(AppIsDark());
        } else if (screen_ == Screen::LocationList) {
            locationList_.SetGeometry(static_cast<double>(width_), static_cast<double>(height_),
                                      DisplayDensity(), topInset_, bottomInset_);
            locationList_.SetDarkMode(scene_.darkMode);
            locationList_.SetRefreshing(refreshing_);
            // A screen of its own, not a pane — so it gets its back arrow back. Matters after a
            // rotation, which is exactly when one becomes the other.
            locationList_.SetPaneMode(false);
        } else {
            locationSearch_.SetGeometry(static_cast<double>(width_), static_cast<double>(height_),
                                        DisplayDensity(), topInset_, bottomInset_, keyboardInset_);
            locationSearch_.SetDarkMode(scene_.darkMode);
        }

        double bandTop = 0.0;
        double bandBottom = static_cast<double>(height_);
        bool partial = false;
        /*
         * ...and not while the snackbar is up. Its band is the bottom of the surface, which the
         * list's damage tracking knows nothing about: DirtyBand covers the rows that moved, so a
         * partial frame clipped to them drops the bar entirely. Measured as the bar never appearing
         * at all after a delete.
         */
        if (screen_ == Screen::LocationList && !sourcesDialog_.IsOpen() && !dialogWasOpen_ &&
            !snackbar_.Visible() && scratchValid_ && scratchScreen_ == screen_) {
            partial = locationList_.DirtyBand(&bandTop, &bandBottom);
        }
        /*
         * Screen 4's day pager, for the same reason and by the same measurement.
         *
         * A sideways drag or fling repaints one row out of a full-height screen, and the copy at
         * the end of this function does not care: traced at `breezy:CopyFlush` 3.42 ms average
         * against an 8.33 ms budget, with the frame's p90 at 7.6 ms and its worst at 14.1 — which
         * is precisely the roughness the pager had. The content scroll deliberately does NOT take
         * this path: it moves everything below the row, so its band is most of the surface and the
         * 0.85 rule below would reject it anyway.
         */
        if (screen_ == Screen::Details && scratchValid_ && scratchScreen_ == screen_ &&
            !snackbar_.Visible() && !details_.MenuOpen() &&
            (details_.TabsAnimating() || (detailsDragged_ && detailsDragTabs_))) {
            partial = details_.TabBand(&bandTop, &bandBottom);
        }
        /*
         * A scroll moves every row, so its band is the whole list â€” near enough the whole surface.
         * Clipping to it buys nothing and the clip test itself costs, so past this point the plain
         * full frame is the cheaper of the two. Measured: forcing every screen-2 frame down the
         * partial path took frames over the 8.33 ms budget from 9% to 22%.
         */
        if (partial && (bandBottom - bandTop) > 0.85 * static_cast<double>(height_)) {
            partial = false;
            locationList_.ClearDamage();
        }
        {
            // Named so a trace says outright how many of screen 2's frames took the cheap path.
            BREEZY_TRACE(partial ? "breezy:OverlayPartialFrame" : "breezy:OverlayFullFrame");
        }
        if (!partial) {
            bandTop = 0.0;
            bandBottom = static_cast<double>(height_);
            partialRunStart_ = frameIndex_;
            runDirtyTop_ = bandTop;
            runDirtyBottom_ = bandBottom;
        } else {
            runDirtyTop_ = std::min(runDirtyTop_, bandTop);
            runDirtyBottom_ = std::max(runDirtyBottom_, bandBottom);
        }
        frameDirtyTop_ = bandTop;
        frameDirtyBottom_ = bandBottom;

        OH_Drawing_CanvasSave(canvas);
        if (partial) {
            OH_Drawing_Rect* band = OH_Drawing_RectCreate(
                0.0f, static_cast<float>(bandTop), static_cast<float>(width_),
                static_cast<float>(bandBottom));
            OH_Drawing_CanvasClipRect(canvas, band, INTERSECT, false);
            OH_Drawing_RectDestroy(band);
        }

        if (screen_ == Screen::MainSettings) {
            mainSettings_.Draw(canvas);
        } else if (screen_ == Screen::Notifications) {
            notifications_.Draw(canvas);
        } else if (screen_ == Screen::Debug) {
            debug_.Draw(canvas);
        } else if (screen_ == Screen::WorkerInfo) {
            workerInfo_.Draw(canvas);
        } else if (screen_ == Screen::Modules) {
            modules_.Draw(canvas);
        } else if (screen_ == Screen::BackgroundUpdates) {
            backgroundUpdates_.Draw(canvas);
        } else if (screen_ == Screen::About) {
            about_.Draw(canvas);
        } else if (screen_ == Screen::PrivacyPolicy) {
            privacy_.Draw(canvas);
        } else if (screen_ == Screen::Dependencies) {
            dependencies_.Draw(canvas);
        } else if (screen_ == Screen::AlertList) {
            alertList_.Draw(canvas);
        } else if (screen_ == Screen::PreviewIcon) {
            previewIcon_.Draw(canvas);
        } else if (screen_ == Screen::Location) {
            location_.Draw(canvas);
        } else if (screen_ == Screen::WeatherSources) {
            weatherSources_.Draw(canvas);
        } else if (screen_ == Screen::DisplayManage) {
            displayManage_.Draw(canvas);
        } else if (screen_ == Screen::Details) {
            details_.Draw(canvas);
        } else if (screen_ == Screen::Placeholder) {
            placeholder_.Draw(canvas);
        } else if (screen_ == Screen::Units) {
            units_.Draw(canvas);
        } else if (screen_ == Screen::Appearance) {
            appearance_.Draw(canvas);
        } else if (screen_ == Screen::Settings) {
            settings_.Draw(canvas);
        } else if (screen_ == Screen::LocationList) {
            locationList_.Draw(canvas);
            locationList_.ClearDamage();
        } else {
            locationSearch_.Draw(canvas);
        }
        // The dialog is above whichever screen opened it.
        sourcesDialog_.SetGeometry(static_cast<double>(width_), static_cast<double>(height_),
                                   DisplayDensity(), scene_.darkMode);
        sourcesDialog_.Draw(canvas);

        // Above the list and below nothing: the snackbar's container is the activity's content
        // view, so it floats over whatever is showing. Drawn last for that reason.
        DrawSnackbar(canvas);

        OH_Drawing_CanvasRestore(canvas);

        dialogWasOpen_ = sourcesDialog_.IsOpen();
        lastFrameWasComplete_ = true;
        scratchValid_ = true;
        scratchScreen_ = screen_;
    }

    if (!overlayScreen) {
        std::lock_guard<std::mutex> lock(mutex_);
        {
            /*
             * Repaint the scene only when it has actually moved. The fade is quantised because it
             * is an alpha on soft particles, and without it every frame of a vertical scroll would
             * be a full repaint.
             *
             * The step was 1/64, and that was still too fine to help. `BackgroundScrollRate()` is
             * `min(1, scrollY / firstCardMarginTop)`, so it moves CONTINUOUSLY through the first
             * card-margin of a scroll and crosses 1/64 (0.016) on essentially every frame — which
             * is exactly the case the quantisation exists to prevent. A trace of a screen-1 scroll
             * measured `breezy:Background` at p90 27.98 ms and max 49.78 ms, with **23.1 % of
             * paints over even the 60 Hz budget**; that is the port's scroll judder.
             *
             * 1/16 is a 6 % alpha step on soft, low-contrast particles against a gradient — below
             * what is visible on this display, and it cuts the repaints during that stretch by
             * four. The particle algorithm, its parameters and the fade curve are untouched, which
             * CLAUDE.md requires: this changes only how often the same scene is re-rendered.
             */
            BREEZY_TRACE("breezy:Background");
            const double rate = BackgroundScrollRate();
            /*
             * The two SETTLED states are always painted exactly, however coarse the step is.
             * Quantising alone would let the scene come to rest up to 1/16 away from the true
             * fade — invisible while moving, but this is where it stops and stays, and the top of
             * the page is the state the user looks at longest.
             */
            const bool atRest = (rate <= 0.0 || rate >= 1.0) && rate != backgroundRate_;
            if (!backgroundValid_ || backgroundPaintedTick_ != backgroundTick_ || atRest ||
                std::fabs(rate - backgroundRate_) > 1.0 / 16.0) {
                /*
                 * The sky is CACHED and the particles are not.
                 *
                 * A scene repaint used to regenerate a three-stop linear gradient over the whole
                 * 1260 x 2720 surface — 3.4 M pixels of per-pixel interpolation — and then draw the
                 * particles on top. But the gradient depends only on the kind, the daylight flag,
                 * the theme and the geometry; NONE of those change between animation ticks, and the
                 * ticks are what drive the repaint (~30/s). So the expensive half was being redone
                 * ~30 times a second to produce exactly the same pixels.
                 *
                 * `sky_` holds that gradient, already translated to the pane, and a tick now costs
                 * one memcpy of it plus the particles — the same operation the background-to-scratch
                 * blit below already does at ~3.3 ms.
                 *
                 * OUTPUT-IDENTICAL: same gradient, same particle algorithm, same parameters, same
                 * fade. Only how often the gradient is GENERATED changes, which is what CLAUDE.md's
                 * 1:1 requirement allows — and what the two options flagged last session (render
                 * smaller and upscale, or move the scene off-thread) would not have been.
                 *
                 * The cross-fade between two skies keeps the old path: it is two scenes through a
                 * saveLayer for 300 ms, and caching one of them would buy nothing.
                 */
                const bool crossFading = prevValid_ && switchProgress_ < 1.0;
                bool blitted = false;
                if (mainScreenBackground_ && !crossFading && sky_ != nullptr) {
                    if (!skyValid_ || skyKind_ != scene_.kind || skyDaylight_ != scene_.daylight ||
                        skyDarkMode_ != scene_.darkMode || skyPaneWidth_ != PaneWidth()) {
                        OH_Drawing_CanvasClear(skyCanvas_,
                                               OH_Drawing_ColorSetArgb(0xFF, 0x00, 0x00, 0x00));
                        OH_Drawing_CanvasSave(skyCanvas_);
                        OH_Drawing_CanvasTranslate(skyCanvas_, static_cast<float>(PaneWidth()),
                                                   0.0f);
                        PaintSky(skyCanvas_, scene_.kind, scene_.daylight, scene_.darkMode);
                        OH_Drawing_CanvasRestore(skyCanvas_);
                        skyKind_ = scene_.kind;
                        skyDaylight_ = scene_.daylight;
                        skyDarkMode_ = scene_.darkMode;
                        skyPaneWidth_ = PaneWidth();
                        skyValid_ = true;
                    }
                    auto* dst = static_cast<uint8_t*>(OH_Drawing_BitmapGetPixels(background_));
                    const auto* src = static_cast<const uint8_t*>(OH_Drawing_BitmapGetPixels(sky_));
                    if (dst != nullptr && src != nullptr) {
                        std::memcpy(dst, src,
                                    static_cast<size_t>(scratchWidth_) *
                                        static_cast<size_t>(scratchHeight_) * kBytesPerPixel);
                        blitted = true;
                    }
                }
                if (!blitted) {
                    OH_Drawing_CanvasClear(backgroundCanvas_,
                                           OH_Drawing_ColorSetArgb(0xFF, 0x00, 0x00, 0x00));
                }
                /*
                 * The scene belongs to the WEATHER PAGE, which in two-pane starts at the pane's
                 * right edge. Painting it at the origin instead would leave the sky showing through
                 * the pane's own margins and put the whole gradient a pane-width out of place.
                 */
                OH_Drawing_CanvasSave(backgroundCanvas_);
                OH_Drawing_CanvasTranslate(backgroundCanvas_, static_cast<float>(PaneWidth()), 0.0f);
                /*
                 * screen 3c's `main_screen_background` (MainScreenSettingsScreen.kt:127-138). Off
                 * means MaterialWeatherView is not drawn at all: no sky gradient, no particles,
                 * just the flat themed surface the cards sit on — which is what the Kotlin's
                 * `mainScreenBackgroundEnabled` produces, and what ArkUIApp's `sceneEnabled` does.
                 */
                if (mainScreenBackground_) {
                    // With the sky already blitted in, only the particles are left to draw.
                    if (blitted) {
                        PaintParticles(backgroundCanvas_, implementor_.get());
                    } else {
                        DrawBackground(backgroundCanvas_);
                    }
                } else {
                    FillRect(backgroundCanvas_, 0.0, 0.0, ContentWidth(),
                             static_cast<double>(height_), kSurface.Resolve(scene_.darkMode));
                }
                OH_Drawing_CanvasRestore(backgroundCanvas_);
                backgroundRate_ = rate;
                backgroundPaintedTick_ = backgroundTick_;
                backgroundValid_ = true;
            }
        }
        /*
         * Is this frame allowed to redraw only a band?
         *
         * Asked AFTER the scene, because a scene repaint is itself a reason it may not be â€” and the
         * page signature carries `backgroundPaintedTick_` for exactly that.
         */
        ++frameIndex_;
        double bandTop = 0.0;
        double bandBottom = static_cast<double>(height_);
        // PartialBand only tracks the page and the scene, so it cannot see the sources dialog that
        // a fold leaves open over this branch; a band from it would clip the dialog out of the
        // copy. Short-circuited, so bandTop/bandBottom keep their full-surface defaults.
        const bool partial = !sourcesDialog_.IsOpen() && !prefsDialog_.IsOpen() &&
                             PartialBand(&bandTop, &bandBottom);
        {
            // Named so a trace says outright how many frames took the cheap path.
            BREEZY_TRACE(partial ? "breezy:PartialFrame" : "breezy:FullFrame");
        }
        if (!partial) {
            bandTop = 0.0;
            bandBottom = static_cast<double>(height_);
            partialRunStart_ = frameIndex_;
            runDirtyTop_ = bandTop;
            runDirtyBottom_ = bandBottom;
        } else {
            runDirtyTop_ = std::min(runDirtyTop_, bandTop);
            runDirtyBottom_ = std::max(runDirtyBottom_, bandBottom);
        }
        frameDirtyTop_ = bandTop;
        frameDirtyBottom_ = bandBottom;

        {
            // Replaces both the clear and the scene: the frame starts as a copy of the background.
            // Only the band being redrawn needs restoring; the rest of the scratch is already right.
            BREEZY_TRACE("breezy:BlitBackground");
            auto* dst = static_cast<uint8_t*>(OH_Drawing_BitmapGetPixels(scratch_));
            const auto* src = static_cast<const uint8_t*>(OH_Drawing_BitmapGetPixels(background_));
            if (dst != nullptr && src != nullptr) {
                const size_t stride = static_cast<size_t>(scratchWidth_) * kBytesPerPixel;
                const int32_t first = std::max(0, static_cast<int32_t>(bandTop));
                const int32_t last = std::min(scratchHeight_, static_cast<int32_t>(bandBottom));
                if (last > first) {
                    std::memcpy(dst + static_cast<size_t>(first) * stride,
                                src + static_cast<size_t>(first) * stride,
                                static_cast<size_t>(last - first) * stride);
                }
            }
        }

        // A partial frame draws the same things, clipped â€” nothing outside the band is touched.
        OH_Drawing_CanvasSave(canvas);
        if (partial) {
            OH_Drawing_Rect* band = OH_Drawing_RectCreate(
                0.0f, static_cast<float>(bandTop), static_cast<float>(width_),
                static_cast<float>(bandBottom));
            OH_Drawing_CanvasClipRect(canvas, band, INTERSECT, false);
            OH_Drawing_RectDestroy(band);
        }

        /*
         * Everything except the weather background is laid out INSIDE the system bars.
         *
         * The background above has already filled the whole surface, which is the point of going
         * edge-to-edge â€” the sky runs behind the clock and the signal icons, as it does on Android.
         * From here the origin moves to the top of the status bar's shadow, so every coordinate
         * below (the toolbar at 0..actionBarSize, the scrolling column under it, the page dots
         * measured up from the bottom) is unchanged and simply lands inside the safe area.
         */
        OH_Drawing_CanvasSave(canvas);
        // In two-pane the page starts at the pane's right edge. Every coordinate below is measured
        // from there, which is why ContentWidth() and not width_ is what the page lays out against.
        OH_Drawing_CanvasTranslate(canvas, static_cast<float>(PaneWidth()),
                                   static_cast<float>(topInset_));

        // fragment_home.xml â€” the toolbar sits outside the Scroll, so the scrolling column starts
        // below it and is clipped to what is left of the page.
        const double barHeight = dimens::kActionBarHeight * DisplayDensity();
        OH_Drawing_CanvasSave(canvas);
        OH_Drawing_Rect* viewport = OH_Drawing_RectCreate(
            0.0f, static_cast<float>(barHeight), static_cast<float>(ContentWidth()),
            static_cast<float>(ViewportHeight()));
        OH_Drawing_CanvasClipRect(canvas, viewport, INTERSECT, true);
        OH_Drawing_CanvasTranslate(
            canvas, 0.0f,
            static_cast<float>(barHeight - pageScroll_.Position() + overscroll_));
        contentHeight_ = DrawContent(canvas);
        OH_Drawing_CanvasRestore(canvas);
        OH_Drawing_RectDestroy(viewport);

        // Measured by drawing: the range can never disagree with what was just painted.
        pageScroll_.SetRange(std::max(0.0, contentHeight_ - (ViewportHeight() - barHeight)));

        BREEZY_TRACE("breezy:Toolbar");
        DrawToolbar(canvas);
        DrawToolbarMenu(canvas);
        DrawRefreshIndicator(canvas);
        DrawIndicator(canvas);
        OH_Drawing_CanvasRestore(canvas);
        OH_Drawing_CanvasRestore(canvas);

        /*
         * The list pane, beside the page rather than over it (layout-w640dp).
         *
         * Drawn AFTER the page and at the surface origin, so it is never inside the page's
         * translate. It gets its own geometry — the pane's width, not the surface's — and no back
         * arrow, because ManagementFragment.kt:198-202 passes a null onBackPressed while the drawer
         * is visible.
         */
        if (TwoPane()) {
            // Named because it is per-frame cost the single-pane arrangement never pays: the page
            // animates continuously, every main-screen frame is a full one, and the pane is redrawn
            // inside each of them even though it is static between selections.
            BREEZY_TRACE("breezy:Pane");
            const double pane = PaneWidth();
            OH_Drawing_CanvasSave(canvas);
            OH_Drawing_Rect* paneRect = OH_Drawing_RectCreate(
                0.0f, 0.0f, static_cast<float>(pane), static_cast<float>(height_));
            OH_Drawing_CanvasClipRect(canvas, paneRect, INTERSECT, true);
            OH_Drawing_RectDestroy(paneRect);
            locationList_.SetGeometry(pane, static_cast<double>(height_), DisplayDensity(),
                                      topInset_, bottomInset_);
            locationList_.SetDarkMode(scene_.darkMode);
            locationList_.SetRefreshing(refreshing_);
            locationList_.SetPaneMode(true);
            locationList_.Draw(canvas);
            locationList_.ClearDamage();
            OH_Drawing_CanvasRestore(canvas);
        }

        /*
         * The sources dialog outlives the fold above.
         *
         * It is normally drawn by the overlay branch, over the list screen. Folding that screen into
         * the pane on rotation would otherwise leave an open dialog undrawn while OnTouch still
         * routed every touch to it — invisible, and swallowing input. Android keeps the dialog
         * across a configuration change, so it is drawn here too.
         */
        /*
         * The edit dialog is the main screen's own, so unlike the sources dialog it is only ever
         * drawn here — and it is drawn BEFORE that one, because its "Weather sources" row is what
         * opens the sources dialog and a nested AlertDialog stacks above its parent.
         */
        if (prefsDialog_.IsOpen()) {
            prefsDialog_.SetGeometry(static_cast<double>(width_), static_cast<double>(height_),
                                     DisplayDensity(), scene_.darkMode);
            prefsDialog_.Draw(canvas);
        }

        if (sourcesDialog_.IsOpen()) {
            sourcesDialog_.SetGeometry(static_cast<double>(width_), static_cast<double>(height_),
                                       DisplayDensity(), scene_.darkMode);
            sourcesDialog_.Draw(canvas);
        }

        // A delete made in the PANE raises its snackbar over the main screen, so it is drawn here
        // too — outside the pane's clip, spanning the window as its container does.
        DrawSnackbar(canvas);

        // Captured after the draw: `contentHeight_` and the list bands are outputs of it.
        lastPageSignature_ = PageSignature();
        lastFrameWasComplete_ = true;
        scratchValid_ = true;
        scratchScreen_ = Screen::Main;
    }

    /*
     * 3. Copy the rows this buffer is actually stale in, row by row â€” the buffer's stride is not
     * always width * 4 (this device reports 5120 for a 1260-wide surface, packed rows being 5040).
     *
     * Buffers rotate, so "stale" is per buffer: everything drawn since this one was last written.
     * `partialRunStart_` is the frame of the last full repaint, and `runDirty*` the union of every
     * band since â€” so a buffer written on or after that frame needs only those rows, and one older
     * (or never seen) is copied whole. The scratch is a complete frame either way, so the worst a
     * mistake here could cost is a redundant copy, never a stale pixel.
     */
    RecordFrameBand();

    BREEZY_TRACE("breezy:CopyFlush");
    void* pixels = OH_Drawing_BitmapGetPixels(bitmap);
    if (pixels != nullptr) {
        const uint32_t srcStride = static_cast<uint32_t>(handle->width) * kBytesPerPixel;
        const uint32_t dstStride = static_cast<uint32_t>(handle->stride);
        const uint32_t rowBytes = std::min(srcStride, dstStride);
        auto* src = static_cast<const uint8_t*>(pixels);
        auto* dst = static_cast<uint8_t*>(handle->virAddr);

        int32_t first = 0;
        int32_t last = handle->height;
        const auto seen = bufferFrame_.find(handle->virAddr);
        if (seen != bufferFrame_.end() && seen->second >= partialRunStart_) {
            double staleTop = 0.0;
            double staleBottom = 0.0;
            if (StaleBandFor(seen->second, &staleTop, &staleBottom)) {
                first = std::max(0, static_cast<int32_t>(staleTop));
                last = std::min(handle->height, static_cast<int32_t>(staleBottom) + 1);
                if (staleBottom <= staleTop) {
                    last = first;  // Nothing changed since this buffer was written.
                }
            } else {
                // Older than the band history: fall back to the union since the last full frame.
                first = std::max(0, static_cast<int32_t>(runDirtyTop_));
                last = std::min(handle->height, static_cast<int32_t>(runDirtyBottom_) + 1);
            }
        }
        if (first == 0 && last == handle->height && srcStride == dstStride) {
            std::memcpy(dst, src, static_cast<size_t>(dstStride) * handle->height);
        } else {
            for (int32_t y = first; y < last; ++y) {
                std::memcpy(dst + static_cast<size_t>(y) * dstStride,
                            src + static_cast<size_t>(y) * srcStride, rowBytes);
            }
        }
        // The map is tiny â€” a surface has two or three buffers for the life of the window.
        bufferFrame_[handle->virAddr] = frameIndex_;
    }

    // 4. Unlock and hand the buffer back to the compositor.
    OH_NativeWindow_UnlockAndFlushBuffer(window_);
}

void Renderer::DestroyScratch() {
    if (scratchCanvas_ != nullptr) {
        OH_Drawing_CanvasDestroy(scratchCanvas_);
        scratchCanvas_ = nullptr;
    }
    if (scratch_ != nullptr) {
        OH_Drawing_BitmapDestroy(scratch_);
        scratch_ = nullptr;
    }
    if (backgroundCanvas_ != nullptr) {
        OH_Drawing_CanvasDestroy(backgroundCanvas_);
        backgroundCanvas_ = nullptr;
    }
    if (background_ != nullptr) {
        OH_Drawing_BitmapDestroy(background_);
        background_ = nullptr;
    }
    if (skyCanvas_ != nullptr) {
        OH_Drawing_CanvasDestroy(skyCanvas_);
        skyCanvas_ = nullptr;
    }
    if (sky_ != nullptr) {
        OH_Drawing_BitmapDestroy(sky_);
        sky_ = nullptr;
    }
    skyValid_ = false;
    backgroundValid_ = false;
    scratchWidth_ = 0;
    scratchHeight_ = 0;
}

}  // namespace breezy
