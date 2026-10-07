#include "PreviewIconScreen.h"

#include <native_drawing/drawing_round_rect.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#include "../render/AnimatedIcon.h"
#include "../render/AssetImages.h"
#include "../render/Shapes.h"
#include "../render/TextPainter.h"
#include "../render/Theme.h"

namespace breezy {
namespace {

/** ?attr/actionBarSize (dimens.xml:36). */
constexpr double kToolbarHeight = 64.0;
/** textAppearanceHeadline6. */
constexpr double kToolbarTitleTextSize = 20.0;
constexpr int kColumnCount = 4;
constexpr double kLineHeight = 1.0;

/* SunDrawable.kt:38-45 and MoonDrawable.kt:40 — Color.rgb triples. */
constexpr uint32_t kSunCore = 0xFFFED675;
/** Only mHaloColors[0] is ever painted; [1] is declared and unused (kt:42-45, :71/:79). */
constexpr uint32_t kSunHalo = 0xFFF9B75D;
constexpr uint32_t kMoonCore = 0xFFABCAF7;

const double kSin45 = std::sin(M_PI / 4.0);

/** `WeatherCode.entries`, in declaration order (WeatherCode.kt:21-32). */
const WeatherKind kWeatherCodes[] = {
    WeatherKind::Clear, WeatherKind::Cloud,   WeatherKind::Cloudy, WeatherKind::Rainy,
    WeatherKind::Snow,  WeatherKind::Wind,    WeatherKind::Fog,    WeatherKind::Haze,
    WeatherKind::Sleet, WeatherKind::Hail,    WeatherKind::Thunder, WeatherKind::Thunderstorm,
};
constexpr int kWeatherCodeCount = 12;

enum class IconSet { Full, Minimal, Shortcut, Ephemeris };

struct Section {
    const char* title;
    IconSet set;
    bool daytime;
};

/** kt:69-108 — the seven sections, in order. */
const Section kSections[] = {
    {"Daytime", IconSet::Full, true},              // strings.xml `daytime`
    {"Nighttime", IconSet::Full, false},           // strings.xml `nighttime`
    {"Minimal Daytime", IconSet::Minimal, true},
    {"Minimal Nighttime", IconSet::Minimal, false},
    {"Shortcuts Daytime", IconSet::Shortcut, true},
    {"Shortcuts Nighttime", IconSet::Shortcut, false},
    {"Sun & moon", IconSet::Ephemeris, true},      // strings.xml `ephemeris`
};
constexpr int kSectionCount = 7;

/* --- the three icon dialogs -------------------------------------------------------------- */

/**
 * Container geometry, measured off the reference rather than derived: the alternate-calendar
 * picker (a tall list) and the animatable-icon dialog (one square icon) both come out 1039 px wide
 * on the 1260 px screen, i.e. 319.7 dp -> 320. Matches ArkUIApp's Dimens.DIALOG_WIDTH.
 */
constexpr double kDialogWidth = 320.0;
constexpr double kDialogCorner = 28.0;
constexpr double kDialogPadding = 24.0;
/** MaterialAlertDialog's title is headlineSmall: 24 sp on a 32 sp line. */
constexpr double kDialogTitleTextSize = 24.0;
constexpr double kDialogTitleLineHeight = 32.0;
/** @dimen/current_weather_icon_container_size. */
constexpr double kAnimatableIconContainer = 128.0;
/** dialog_adaptive_icon.xml:11-12 — a fixed 108dp view. */
constexpr double kAdaptiveIconSize = 108.0;
/** The scrim behind a dialog. */
constexpr uint32_t kDialogScrim = 0x52000000;

/** How many masks AdaptiveIconView offers — `nextInt(PATH_TEARDROP + 1)` (kt:48). */
constexpr int kAdaptiveMaskCount = 5;

/**
 * Corner radii for each mask, as a FRACTION of the side, clockwise from top-left.
 *
 * The library (com.github.breezy-weather:AdaptiveIconView:0.0.3) is a Gradle dependency and is not
 * vendored, so its path geometry cannot be read the way an app resource can. These are the standard
 * Android adaptive-icon masks; the teardrop is the one the reference happened to roll and is taken
 * from that capture. Same table as ArkUIApp's `maskRadii`, and the only thing on this screen not
 * taken from a readable source.
 */
const double kMaskRadii[kAdaptiveMaskCount][4] = {
    {0.50, 0.50, 0.50, 0.50},  // circle
    {0.42, 0.42, 0.42, 0.42},  // squircle
    {0.20, 0.20, 0.20, 0.20},  // rounded square
    {0.00, 0.00, 0.00, 0.00},  // square
    {0.50, 0.50, 0.12, 0.50}   // teardrop
};

/**
 * The Android WeatherCode enum NAME, which is what the dialog titles show.
 *
 * WeatherCode.kt:21-32 names them CLEAR, PARTLY_CLOUDY, CLOUDY, RAIN, … — this port's WeatherKind
 * calls the same three Cloud, Rainy and so on, so a straight enum-to-string would title the dialog
 * "CLOUD_DAY" where the reference reads "PARTLY_CLOUDY_DAY".
 */
std::string WeatherCodeName(WeatherKind kind, bool daytime) {
    std::string name;
    switch (kind) {
        case WeatherKind::Clear: name = "CLEAR"; break;
        case WeatherKind::Cloud: name = "PARTLY_CLOUDY"; break;
        case WeatherKind::Cloudy: name = "CLOUDY"; break;
        case WeatherKind::Rainy: name = "RAIN"; break;
        case WeatherKind::Snow: name = "SNOW"; break;
        case WeatherKind::Wind: name = "WIND"; break;
        case WeatherKind::Fog: name = "FOG"; break;
        case WeatherKind::Haze: name = "HAZE"; break;
        case WeatherKind::Sleet: name = "SLEET"; break;
        case WeatherKind::Hail: name = "HAIL"; break;
        case WeatherKind::Thunder: name = "THUNDER"; break;
        case WeatherKind::Thunderstorm: name = "THUNDERSTORM"; break;
        default: name = ""; break;
    }
    return name + (daytime ? "_DAY" : "_NIGHT");
}

/** The shortcut artwork's stem — the shortcut set drops the `weather_` prefix. */
std::string ShortcutBaseName(WeatherKind kind, bool daytime) {
    const std::string base = AnimatedIconBaseName(kind, daytime);
    const std::string prefix = "weather_";
    return base.rfind(prefix, 0) == 0 ? base.substr(prefix.size()) : base;
}

/**
 * The asset for one cell.
 *
 * Minimal: `<weather icon name>_mini_<dark|light>` with `darkText = !darkMode`
 * (DefaultResourceProvider.kt:194-213, PreviewIconActivity.kt:81-91) — a light theme wants DARK
 * glyphs. Shortcuts: `shortcuts_<name>`, which is `IconBaseName` with its `weather_` prefix
 * swapped, because `icon_provider_shortcut_filter.xml` collapses `_day`/`_night` for every code
 * but CLEAR and PARTLY_CLOUDY — exactly the codes whose base name already carries the suffix.
 */
std::string AssetFor(IconSet set, WeatherKind kind, bool daytime, bool dark) {
    const std::string base = IconBaseName(kind, daytime);
    if (base.empty()) {
        return {};
    }
    switch (set) {
        case IconSet::Full:
            return base + ".png";
        case IconSet::Minimal:
            return base + (dark ? "_mini_light.png" : "_mini_dark.png");
        case IconSet::Shortcut:
            return "shortcuts_" + base.substr(std::string("weather_").size()) + ".png";
        default:
            return {};
    }
}

/** SunDrawable.kt:69-91 — two squares, one rotated 45°, then the core disc. */
void DrawSun(OH_Drawing_Canvas* canvas, double left, double top, double side) {
    const double radius = kSin45 * side / 2.0 - 2.0;  // kt:59
    if (radius <= 0.0) {
        return;
    }
    const double cx = left + side / 2.0;
    const double cy = top + side / 2.0;
    FillRect(canvas, cx - radius, cy - radius, cx + radius, cy + radius, kSunHalo);

    OH_Drawing_CanvasSave(canvas);
    OH_Drawing_CanvasRotate(canvas, 45.0f, static_cast<float>(cx), static_cast<float>(cy));
    FillRect(canvas, cx - radius, cy - radius, cx + radius, cy + radius, kSunHalo);
    OH_Drawing_CanvasRestore(canvas);

    FillCircle(canvas, cx, cy, radius, kSunCore);
}

/**
 * MoonDrawable.kt:70-85 — a disc with a second disc taken out of it up and to the right.
 *
 * The Kotlin punches the second disc out with `PorterDuff.CLEAR` inside a saved layer, which shows
 * whatever is behind the drawable. Here the drawable is composited straight onto the screen's own
 * surface, so painting the cut-out in the surface colour gives the identical pixels without a
 * layer — the ArkTS twin uses `destination-out` for the same reason and the same result.
 */
void DrawMoon(OH_Drawing_Canvas* canvas, double left, double top, double side, uint32_t behind) {
    const double coreRadius = (kSin45 * side / 2.0 + side / 2.0) / 2.0 - 2.0;  // kt:57
    if (coreRadius <= 0.0) {
        return;
    }
    const double cx = left + side / 2.0;
    const double cy = top + side / 2.0;
    FillCircle(canvas, cx, cy, coreRadius, kMoonCore);
    FillCircle(canvas, cx + coreRadius * 0.5914, cy - coreRadius * 0.5932, coreRadius * 0.9050,
               behind);
}

}  // namespace

void PreviewIconScreen::SetGeometry(double widthPx, double heightPx, float density,
                                    double topInsetPx, double bottomInsetPx) {
    width_ = widthPx;
    height_ = heightPx;
    density_ = density;
    topInset_ = topInsetPx;
    bottomInset_ = bottomInsetPx;
}

double PreviewIconScreen::ContentTop() const {
    return topInset_ + kToolbarHeight * density_;
}

void PreviewIconScreen::Reset() {
    scroll_.Reset();
}

double PreviewIconScreen::Layout(OH_Drawing_Canvas* canvas) {
    const double margin = dimens::kNormalMargin * density_;
    const double inset = dimens::kLargeMargin * density_;
    const double cell = width_ / static_cast<double>(kColumnCount);
    const double contentTop = ContentTop();
    const uint32_t surface = kSurface.Resolve(dark_);

    TextStyleSpec titleStyle;
    // item_weather_icon_title.xml — bold content_text_size in colorTitleText.
    titleStyle.sizeVp = dimens::kContentTextSize;
    titleStyle.bold = true;
    titleStyle.color = kTitleText.Resolve(dark_);

    double y = contentTop - scroll_.Position();

    for (int s = 0; s < kSectionCount; ++s) {
        const Section& section = kSections[s];

        const double titleHeight = MeasureTextHeight(section.title, width_ - margin * 2.0,
                                                     titleStyle);
        if (canvas != nullptr && y + titleHeight + margin * 2.0 >= contentTop && y <= height_) {
            DrawText(canvas, section.title, margin, y + margin, width_ - margin * 2.0, titleStyle);
        }
        y += titleHeight + margin * 2.0;

        if (section.set == IconSet::Ephemeris) {
            // kt:106-108 — one sun cell and one moon cell.
            if (canvas != nullptr && y + cell >= contentTop && y <= height_) {
                DrawSun(canvas, inset, y + inset, cell - inset * 2.0);
                DrawMoon(canvas, cell + inset, y + inset, cell - inset * 2.0, surface);
            }
            y += cell;
        } else {
            const int rows = (kWeatherCodeCount + kColumnCount - 1) / kColumnCount;
            for (int row = 0; row < rows; ++row) {
                if (canvas != nullptr && y + cell >= contentTop && y <= height_) {
                    for (int column = 0; column < kColumnCount; ++column) {
                        const int index = row * kColumnCount + column;
                        if (index >= kWeatherCodeCount) {
                            break;
                        }
                        const std::string asset =
                            AssetFor(section.set, kWeatherCodes[index], section.daytime, dark_);
                        if (asset.empty()) {
                            continue;
                        }
                        // item_weather_icon.xml — a SQUARE cell with a large_margin inset.
                        DrawAsset(canvas, asset, cell * column + inset, y + inset,
                                  cell - inset * 2.0, 0, false);
                    }
                }
                y += cell;
            }
        }

        // kt:73/79/86/92/98/104 — a rule after every section but the last.
        if (s != kSectionCount - 1) {
            if (canvas != nullptr && y >= contentTop && y <= height_) {
                FillRect(canvas, 0.0, y, width_, y + kLineHeight * density_,
                         kOutline.Resolve(dark_));
            }
            y += kLineHeight * density_;
        }
    }

    y += margin + bottomInset_;
    return y + scroll_.Position() - contentTop;
}

void PreviewIconScreen::Draw(OH_Drawing_Canvas* canvas) {
    // Compose typography: FontFamily.Default. See SystemFontScope.
    SystemFontScope systemFont;
    if (width_ <= 0.0 || height_ <= 0.0) {
        return;
    }
    const double margin = dimens::kNormalMargin * density_;
    const double iconSize = dimens::kMaterialIconSize * density_;
    const uint32_t onSurface = kOnSurface.Resolve(dark_);

    FillRect(canvas, 0.0, 0.0, width_, height_, kSurface.Resolve(dark_));

    const double contentTop = ContentTop();
    OH_Drawing_CanvasSave(canvas);
    OH_Drawing_Rect* clip =
        OH_Drawing_RectCreate(0.0f, static_cast<float>(contentTop), static_cast<float>(width_),
                              static_cast<float>(height_));
    OH_Drawing_CanvasClipRect(canvas, clip, INTERSECT, true);
    OH_Drawing_RectDestroy(clip);
    contentHeight_ = Layout(canvas);
    OH_Drawing_CanvasRestore(canvas);

    /*
     * The toolbar is drawn LAST and over an opaque band: it is a plain MaterialToolbar pinned above
     * the RecyclerView, not a collapsing app bar, so the grid must not show through it.
     * kt:133-139 — getWidgetSurfaceColor(6f, colorPrimary, colorSurface).
     */
    FillRect(canvas, 0.0, 0.0, width_, contentTop, kElevatedSurface6.Resolve(dark_));
    DrawAsset(canvas, "ic_back.svg", margin,
              topInset_ + (kToolbarHeight * density_ - iconSize) / 2.0, iconSize, onSurface, true);
    DrawAsset(canvas, "ic_info.svg", width_ - margin - iconSize,
              topInset_ + (kToolbarHeight * density_ - iconSize) / 2.0, iconSize, onSurface, true);

    TextStyleSpec titleStyle;
    titleStyle.sizeVp = kToolbarTitleTextSize;
    titleStyle.color = onSurface;
    titleStyle.singleLine = true;
    // kt:116 — the toolbar title is the provider's name.
    const std::string title = "Breezy Weather";  // res_breezy/values/strings.xml `brand_name`
    const double titleLeft = margin + iconSize + margin;
    const double titleWidth = width_ - titleLeft - margin - iconSize - margin;
    const double titleHeight = MeasureTextHeight(title, titleWidth, titleStyle);
    DrawText(canvas, title, titleLeft,
             topInset_ + (kToolbarHeight * density_ - titleHeight) / 2.0, titleWidth, titleStyle);

    // A dialog is drawn over everything, toolbar included, exactly as a real one is.
    if (dialog_ != IconDialogKind::None) {
        DrawDialog(canvas);
    }
}

PreviewIconHit PreviewIconScreen::HitTest(double x, double y) {
    PreviewIconHit hit;
    const double margin = dimens::kNormalMargin * density_;
    const double iconSize = dimens::kMaterialIconSize * density_;

    /*
     * A dialog owns every touch while it is up. Inside the animatable one the whole container
     * replays the animation (AnimatableIconDialog.kt:44); anywhere else dismisses, which is what
     * a MaterialAlertDialog's scrim does. Restarting is a matter of putting the clock back to
     * zero, because the pose is a pure function of elapsed time.
     */
    if (dialog_ != IconDialogKind::None) {
        const double dialogWidth = kDialogWidth * density_;
        const double left = (width_ - dialogWidth) / 2.0;
        const double top = (height_ - DialogHeight()) / 2.0;
        const bool inside = x >= left && x <= left + dialogWidth && y >= top &&
                            y <= top + DialogHeight();
        if (inside && dialog_ == IconDialogKind::Animatable) {
            dialogElapsed_ = 0.0;
        } else if (!inside) {
            dialog_ = IconDialogKind::None;
        }
        return hit;
    }

    if (y >= topInset_ && y < topInset_ + kToolbarHeight * density_) {
        if (x < margin + iconSize) {
            hit.action = PreviewIconAction::Close;
        } else if (x > width_ - margin - iconSize) {
            // kt:119-132 — action_about opens the provider's system app page.
            hit.action = PreviewIconAction::AppDetails;
        }
        return hit;
    }

    // Every icon cell is a tap target; the section decides which dialog opens.
    OpenDialogAt(x, y);
    return hit;
}

double PreviewIconScreen::DialogHeight() const {
    if (dialog_ == IconDialogKind::None) {
        return 0.0;
    }
    // padding + title line + the content block, which carries its own normal_margin above and below
    // (dialog_animatable_icon.xml:15, dialog_adaptive_icon.xml:13).
    const double body = dialog_ == IconDialogKind::Adaptive
                            ? kAdaptiveIconSize + dimens::kNormalMargin * 2.0
                            : dialog_ == IconDialogKind::Animatable
                                  ? kAnimatableIconContainer + dimens::kNormalMargin * 2.0
                                  // The minimal dialog's row is three square cells across the
                                  // content width, each inset by material_icon_size.
                                  : (kDialogWidth - kDialogPadding * 2.0) / 3.0;
    return (kDialogPadding + kDialogTitleLineHeight + body) * density_;
}

bool PreviewIconScreen::OpenDialogAt(double x, double y) {
    const double margin = dimens::kNormalMargin * density_;
    const double cell = width_ / static_cast<double>(kColumnCount);
    const double contentTop = ContentTop();

    TextStyleSpec titleStyle;
    titleStyle.sizeVp = dimens::kContentTextSize;
    titleStyle.bold = true;

    // Walk the same layout Layout() produces, without drawing.
    double cursor = contentTop - scroll_.Position();
    for (int s = 0; s < kSectionCount; ++s) {
        const Section& section = kSections[s];
        cursor += MeasureTextHeight(section.title, width_ - margin * 2.0, titleStyle) +
                  margin * 2.0;

        if (section.set == IconSet::Ephemeris) {
            // kt:222 — SunIcon.onItemClicked does nothing, so this row swallows the tap.
            cursor += cell;
        } else {
            const int rows = (kWeatherCodeCount + kColumnCount - 1) / kColumnCount;
            for (int row = 0; row < rows; ++row) {
                if (y >= cursor && y < cursor + cell) {
                    const int column = static_cast<int>(x / cell);
                    const int index = row * kColumnCount + column;
                    if (column < 0 || column >= kColumnCount || index >= kWeatherCodeCount) {
                        return false;
                    }
                    dialogKind_ = kWeatherCodes[index];
                    dialogDaytime_ = section.daytime;
                    dialogElapsed_ = 0.0;
                    if (section.set == IconSet::Full) {
                        dialog_ = IconDialogKind::Animatable;
                    } else if (section.set == IconSet::Minimal) {
                        dialog_ = IconDialogKind::Minimal;
                    } else {
                        dialog_ = IconDialogKind::Adaptive;
                        /*
                         * Rolled ONCE here, as kt:48 does. There is no rand() in this tree, and it
                         * would need seeding anyway; the scroll position and the cell index are
                         * both already varying and are enough to pick a mask that differs between
                         * showings without adding a generator.
                         */
                        dialogMask_ = static_cast<int>(std::llabs(
                                          static_cast<long long>(scroll_.Position()) + index * 7 +
                                          s * 13)) %
                                      kAdaptiveMaskCount;
                    }
                    return true;
                }
                cursor += cell;
            }
        }
        if (s != kSectionCount - 1) {
            cursor += kLineHeight * density_;
        }
    }
    return false;
}

void PreviewIconScreen::DrawDialog(OH_Drawing_Canvas* canvas) {
    FillRect(canvas, 0.0, 0.0, width_, height_, kDialogScrim);

    const double dialogWidth = kDialogWidth * density_;
    const double dialogHeight = DialogHeight();
    const double left = (width_ - dialogWidth) / 2.0;
    const double top = (height_ - dialogHeight) / 2.0;
    const double padding = kDialogPadding * density_;

    FillRoundRect(canvas, left, top, left + dialogWidth, top + dialogHeight,
                  kDialogCorner * density_, kAlertDialogSurface.Resolve(dark_));

    TextStyleSpec titleStyle;
    titleStyle.sizeVp = kDialogTitleTextSize;
    titleStyle.color = kOnSurface.Resolve(dark_);
    titleStyle.singleLine = true;
    DrawTextFixed(canvas, WeatherCodeName(dialogKind_, dialogDaytime_), left + padding,
                  top + padding, dialogWidth - padding * 2.0, titleStyle,
                  kDialogTitleLineHeight * density_);

    const double bodyTop = top + padding + kDialogTitleLineHeight * density_;
    const double bodyHeight = top + dialogHeight - bodyTop;

    switch (dialog_) {
        case IconDialogKind::Animatable: {
            const double side = kAnimatableIconContainer * density_;
            DrawAnimatedIcon(canvas, dialogKind_, dialogDaytime_, left + (dialogWidth - side) / 2.0,
                             bodyTop + (bodyHeight - side) / 2.0, side, dialogElapsed_ * 1000.0);
            break;
        }
        case IconDialogKind::Minimal: {
            /*
             * Three SquareFrameLayouts of weight 1, each holding the image inset by
             * material_icon_size (dialog_minimal_icon.xml:14-70). The variants are light, grey and
             * dark in that order — MinimalIconDialog.kt:38-50.
             */
            const char* suffixes[3] = {"light", "grey", "dark"};
            const double contentWidth = dialogWidth - padding * 2.0;
            const double cellSide = contentWidth / 3.0;
            const double inset = dimens::kMaterialIconSize * density_;
            for (int i = 0; i < 3; ++i) {
                const std::string asset = AnimatedIconBaseName(dialogKind_, dialogDaytime_) +
                                          "_mini_" + suffixes[i] + ".png";
                DrawAsset(canvas, asset, left + padding + cellSide * i + inset, bodyTop + inset,
                          cellSide - inset * 2.0, 0, false);
            }
            break;
        }
        case IconDialogKind::Adaptive: {
            /*
             * AdaptiveIcon(foreground, TRANSPARENT, 0.5) — the FOREGROUND layer only, clipped to
             * the rolled mask. Nothing is drawn behind it, which is why the reference's teardrop
             * reads as whatever the foreground PNG itself carries.
             */
            const double side = kAdaptiveIconSize * density_;
            const double x = left + (dialogWidth - side) / 2.0;
            const double y = bodyTop + (bodyHeight - side) / 2.0;
            const int mask = dialogMask_ < 0 || dialogMask_ >= kAdaptiveMaskCount ? 0 : dialogMask_;

            OH_Drawing_CanvasSave(canvas);
            OH_Drawing_Rect* bounds = OH_Drawing_RectCreate(
                static_cast<float>(x), static_cast<float>(y), static_cast<float>(x + side),
                static_cast<float>(y + side));
            /*
             * OH_Drawing_RoundRectSetCorner takes one radius per corner, which is what makes the
             * teardrop's odd corner expressible — a single-radius round rect could not.
             */
            OH_Drawing_RoundRect* round = OH_Drawing_RoundRectCreate(bounds, 0.0f, 0.0f);
            const OH_Drawing_CornerPos corners[4] = {CORNER_POS_TOP_LEFT, CORNER_POS_TOP_RIGHT,
                                                     CORNER_POS_BOTTOM_RIGHT,
                                                     CORNER_POS_BOTTOM_LEFT};
            for (int c = 0; c < 4; ++c) {
                const float r = static_cast<float>(kMaskRadii[mask][c] * side);
                OH_Drawing_RoundRectSetCorner(round, corners[c], {r, r});
            }
            OH_Drawing_CanvasClipRoundRect(canvas, round, INTERSECT, true);
            OH_Drawing_RoundRectDestroy(round);
            OH_Drawing_RectDestroy(bounds);

            DrawAsset(canvas, "shortcuts_" + ShortcutBaseName(dialogKind_, dialogDaytime_) +
                                  "_foreground.png",
                      x, y, side, 0, false);
            OH_Drawing_CanvasRestore(canvas);
            break;
        }
        default:
            break;
    }
}

bool PreviewIconScreen::ScrollBy(double deltaY) {
    const double viewport = height_ - ContentTop();
    scroll_.SetRange(std::max(0.0, contentHeight_ - viewport));
    const double before = scroll_.Position();
    scroll_.Drag(-deltaY);
    return scroll_.Position() != before;
}

bool PreviewIconScreen::Advance(double seconds) {
    const bool scrolling = scroll_.Advance(seconds);
    if (dialog_ == IconDialogKind::Animatable) {
        // The icon's pose is a pure function of this clock, so advancing it IS the animation, and
        // returning true is what keeps the renderer asking for the next frame.
        dialogElapsed_ += seconds;
        return true;
    }
    return scrolling;
}

void PreviewIconScreen::StopScrolling() {
    scroll_.Stop();
}

}  // namespace breezy
