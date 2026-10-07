/*
 * The two rounded-rectangle fills every painter needs.
 *
 * Header-only and inline: they are called per card and per histogram bar, and a translation unit
 * boundary here would cost more than the bodies are worth.
 */
#ifndef BREEZY_RENDER_SHAPES_H
#define BREEZY_RENDER_SHAPES_H

#include <native_drawing/drawing_brush.h>
#include <native_drawing/drawing_canvas.h>
#include <native_drawing/drawing_rect.h>
#include <native_drawing/drawing_round_rect.h>
#include <native_drawing/drawing_shadow_layer.h>
#include <native_drawing/drawing_path.h>
#include <native_drawing/drawing_pen.h>
#include <native_drawing/drawing_point.h>

#include <algorithm>
#include <cstdint>

namespace breezy {

/** An OUTLINED rounded rect — the time picker's AM/PM container has a visible border. */
inline void StrokeRoundRect(OH_Drawing_Canvas* canvas, double left, double top, double right,
                            double bottom, double radius, double strokeWidth, uint32_t color) {
    OH_Drawing_Pen* pen = OH_Drawing_PenCreate();
    OH_Drawing_PenSetAntiAlias(pen, true);
    OH_Drawing_PenSetColor(pen, color);
    OH_Drawing_PenSetWidth(pen, static_cast<float>(strokeWidth));
    OH_Drawing_CanvasAttachPen(canvas, pen);
    OH_Drawing_Rect* rect = OH_Drawing_RectCreate(
        static_cast<float>(left), static_cast<float>(top), static_cast<float>(right),
        static_cast<float>(bottom));
    OH_Drawing_RoundRect* round =
        OH_Drawing_RoundRectCreate(rect, static_cast<float>(radius), static_cast<float>(radius));
    OH_Drawing_CanvasDrawRoundRect(canvas, round);
    OH_Drawing_RoundRectDestroy(round);
    OH_Drawing_RectDestroy(rect);
    OH_Drawing_CanvasDetachPen(canvas);
    OH_Drawing_PenDestroy(pen);
}

/** A straight stroked segment — the clock dial's hand. */
inline void StrokeLine(OH_Drawing_Canvas* canvas, double x1, double y1, double x2, double y2,
                       double strokeWidth, uint32_t color) {
    OH_Drawing_Pen* pen = OH_Drawing_PenCreate();
    OH_Drawing_PenSetAntiAlias(pen, true);
    OH_Drawing_PenSetColor(pen, color);
    OH_Drawing_PenSetWidth(pen, static_cast<float>(strokeWidth));
    OH_Drawing_CanvasAttachPen(canvas, pen);
    OH_Drawing_Path* path = OH_Drawing_PathCreate();
    OH_Drawing_PathMoveTo(path, static_cast<float>(x1), static_cast<float>(y1));
    OH_Drawing_PathLineTo(path, static_cast<float>(x2), static_cast<float>(y2));
    OH_Drawing_CanvasDrawPath(canvas, path);
    OH_Drawing_PathDestroy(path);
    OH_Drawing_CanvasDetachPen(canvas);
    OH_Drawing_PenDestroy(pen);
}

/**
 * An upward-pointing filled triangle inscribed in a `size` square — MaterialShapes.Triangle, the
 * leading content of each listed alert row (AlertViewHolder.kt:181-189).
 */
inline void DrawTriangle(OH_Drawing_Canvas* canvas, double left, double top, double size,
                         uint32_t color) {
    OH_Drawing_Brush* brush = OH_Drawing_BrushCreate();
    OH_Drawing_BrushSetColor(brush, color);
    OH_Drawing_BrushSetAntiAlias(brush, true);

    OH_Drawing_Path* path = OH_Drawing_PathCreate();
    OH_Drawing_PathMoveTo(path, static_cast<float>(left + size / 2.0), static_cast<float>(top));
    OH_Drawing_PathLineTo(path, static_cast<float>(left + size),
                          static_cast<float>(top + size));
    OH_Drawing_PathLineTo(path, static_cast<float>(left), static_cast<float>(top + size));
    OH_Drawing_PathClose(path);

    OH_Drawing_CanvasAttachBrush(canvas, brush);
    OH_Drawing_CanvasDrawPath(canvas, path);
    OH_Drawing_CanvasDetachBrush(canvas);

    OH_Drawing_PathDestroy(path);
    OH_Drawing_BrushDestroy(brush);
}

/** A plain filled circle. */
inline void FillCircle(OH_Drawing_Canvas* canvas, double centerX, double centerY, double radius,
                       uint32_t color) {
    if (radius <= 0.0) {
        return;
    }
    OH_Drawing_Brush* brush = OH_Drawing_BrushCreate();
    OH_Drawing_BrushSetColor(brush, color);
    OH_Drawing_BrushSetAntiAlias(brush, true);

    OH_Drawing_Point* center = OH_Drawing_PointCreate(static_cast<float>(centerX),
                                                      static_cast<float>(centerY));
    OH_Drawing_CanvasAttachBrush(canvas, brush);
    OH_Drawing_CanvasDrawCircle(canvas, center, static_cast<float>(radius));
    OH_Drawing_CanvasDetachBrush(canvas);

    OH_Drawing_PointDestroy(center);
    OH_Drawing_BrushDestroy(brush);
}

/** A plain rounded-rectangle fill. */
inline void FillRoundRect(OH_Drawing_Canvas* canvas, double left, double top, double right,
                          double bottom, double radius, uint32_t color) {
    if (right <= left || bottom <= top) {
        return;
    }
    OH_Drawing_Brush* brush = OH_Drawing_BrushCreate();
    OH_Drawing_BrushSetColor(brush, color);
    OH_Drawing_BrushSetAntiAlias(brush, true);

    OH_Drawing_Rect* rect = OH_Drawing_RectCreate(
        static_cast<float>(left), static_cast<float>(top), static_cast<float>(right),
        static_cast<float>(bottom));
    const float r = static_cast<float>(
        std::max(0.0, std::min(radius, std::min(right - left, bottom - top) / 2.0)));
    OH_Drawing_RoundRect* round = OH_Drawing_RoundRectCreate(rect, r, r);

    OH_Drawing_CanvasAttachBrush(canvas, brush);
    OH_Drawing_CanvasDrawRoundRect(canvas, round);
    OH_Drawing_CanvasDetachBrush(canvas);

    OH_Drawing_RoundRectDestroy(round);
    OH_Drawing_RectDestroy(rect);
    OH_Drawing_BrushDestroy(brush);
}

/** A plain rectangle fill. */
inline void FillRect(OH_Drawing_Canvas* canvas, double left, double top, double right,
                     double bottom, uint32_t color) {
    if (right <= left || bottom <= top) {
        return;
    }
    OH_Drawing_Brush* brush = OH_Drawing_BrushCreate();
    OH_Drawing_BrushSetColor(brush, color);
    OH_Drawing_Rect* rect = OH_Drawing_RectCreate(
        static_cast<float>(left), static_cast<float>(top), static_cast<float>(right),
        static_cast<float>(bottom));
    OH_Drawing_CanvasAttachBrush(canvas, brush);
    OH_Drawing_CanvasDrawRect(canvas, rect);
    OH_Drawing_CanvasDetachBrush(canvas);
    OH_Drawing_RectDestroy(rect);
    OH_Drawing_BrushDestroy(brush);
}

/**
 * A rounded rectangle with a radius PER CORNER.
 *
 * The settings list needs it: a group of rows is one block whose outer corners carry 28 dp while
 * every corner between two rows carries 2 dp (Material3Widgets.kt:204-225). Built from a path
 * because OH_Drawing_RoundRectCreate takes a single radius for all four.
 */
inline void FillRoundRectCorners(OH_Drawing_Canvas* canvas, double left, double top, double right,
                                 double bottom, double topLeft, double topRight, double bottomRight,
                                 double bottomLeft, uint32_t color) {
    if (right <= left || bottom <= top) {
        return;
    }
    const double limit = std::min(right - left, bottom - top) / 2.0;
    const float tl = static_cast<float>(std::max(0.0, std::min(topLeft, limit)));
    const float tr = static_cast<float>(std::max(0.0, std::min(topRight, limit)));
    const float br = static_cast<float>(std::max(0.0, std::min(bottomRight, limit)));
    const float bl = static_cast<float>(std::max(0.0, std::min(bottomLeft, limit)));

    const float l = static_cast<float>(left);
    const float t = static_cast<float>(top);
    const float r = static_cast<float>(right);
    const float b = static_cast<float>(bottom);

    OH_Drawing_Path* path = OH_Drawing_PathCreate();
    OH_Drawing_PathMoveTo(path, l + tl, t);
    OH_Drawing_PathLineTo(path, r - tr, t);
    OH_Drawing_PathQuadTo(path, r, t, r, t + tr);
    OH_Drawing_PathLineTo(path, r, b - br);
    OH_Drawing_PathQuadTo(path, r, b, r - br, b);
    OH_Drawing_PathLineTo(path, l + bl, b);
    OH_Drawing_PathQuadTo(path, l, b, l, b - bl);
    OH_Drawing_PathLineTo(path, l, t + tl);
    OH_Drawing_PathQuadTo(path, l, t, l + tl, t);
    OH_Drawing_PathClose(path);

    OH_Drawing_Brush* brush = OH_Drawing_BrushCreate();
    OH_Drawing_BrushSetColor(brush, color);
    OH_Drawing_BrushSetAntiAlias(brush, true);
    OH_Drawing_CanvasAttachBrush(canvas, brush);
    OH_Drawing_CanvasDrawPath(canvas, path);
    OH_Drawing_CanvasDetachBrush(canvas);
    OH_Drawing_BrushDestroy(brush);
    OH_Drawing_PathDestroy(path);
}

/* The Material 3 Switch — 52 x 32 dp, a 24 dp thumb when on and a 16 dp one when off. */
constexpr double kSwitchWidth = 52.0;
constexpr double kSwitchHeight = 32.0;
constexpr double kSwitchThumbOn = 24.0;
constexpr double kSwitchThumbOff = 16.0;

/**
 * `Switch` as Material draws it, positioned by its RIGHT edge and vertical centre.
 *
 * ONE copy. Four screens (Notifications, Background updates, Main screen, Modules) each carried a
 * byte-identical private `DrawSwitch` plus its own four constants — the same duplication that let
 * the settings section header drift in five places before it was hoisted. A fifth copy was about
 * to be written for the weather-models dialog, which is what prompted this.
 *
 * `surface` is the colour the thumb and the off-state fill take — the settings card on a settings
 * screen, the dialog panel inside a dialog — so the caller passes it rather than this reaching for
 * a token that is only right on one of the two.
 */
inline void DrawSwitch(OH_Drawing_Canvas* canvas, double right, double centreY, bool on,
                       bool enabled, double density, uint32_t accentColor, uint32_t offColor,
                       uint32_t surfaceColor) {
    const double w = kSwitchWidth * density;
    const double h = kSwitchHeight * density;
    const double left = right - w;
    const double top = centreY - h / 2.0;
    // A disabled switch keeps its shape and loses its colour, which is what Material does.
    const uint32_t accent = enabled ? accentColor : offColor;
    const uint32_t track = on ? accent : offColor;
    FillRoundRect(canvas, left, top, right, top + h, h / 2.0, track);
    if (!on) {
        FillRoundRect(canvas, left + 2.0 * density, top + 2.0 * density, right - 2.0 * density,
                      top + h - 2.0 * density, (h - 4.0 * density) / 2.0, surfaceColor);
    }
    const double thumb = (on ? kSwitchThumbOn : kSwitchThumbOff) * density;
    const double inset = (h - thumb) / 2.0;
    const double thumbX = on ? right - inset - thumb / 2.0 : left + inset + thumb / 2.0;
    FillCircle(canvas, thumbX, centreY, thumb / 2.0, on ? surfaceColor : offColor);
}

/**
 * The same fill with the Material card elevation under it.
 *
 * AbstractMainCardViewHolder.kt:47-51 sets cardElevation to 2 dp; ArkUIApp renders that as
 * `.shadow({ radius: 4, color: '#20000000', offsetY: 2 })`, and a shadow layer is the direct
 * equivalent — the blur is drawn from the same geometry rather than as a second rectangle.
 */
inline void FillCard(OH_Drawing_Canvas* canvas, double left, double top, double right,
                     double bottom, double radius, uint32_t color, float density) {
    if (right <= left || bottom <= top) {
        return;
    }
    OH_Drawing_Brush* brush = OH_Drawing_BrushCreate();
    OH_Drawing_BrushSetColor(brush, color);
    OH_Drawing_BrushSetAntiAlias(brush, true);

    constexpr float kElevation = 2.0f;
    OH_Drawing_ShadowLayer* shadow = OH_Drawing_ShadowLayerCreate(
        kElevation * 2.0f * density, 0.0f, kElevation * density, 0x20000000);
    OH_Drawing_BrushSetShadowLayer(brush, shadow);

    OH_Drawing_Rect* rect = OH_Drawing_RectCreate(
        static_cast<float>(left), static_cast<float>(top), static_cast<float>(right),
        static_cast<float>(bottom));
    const float r = static_cast<float>(
        std::max(0.0, std::min(radius, std::min(right - left, bottom - top) / 2.0)));
    OH_Drawing_RoundRect* round = OH_Drawing_RoundRectCreate(rect, r, r);

    OH_Drawing_CanvasAttachBrush(canvas, brush);
    OH_Drawing_CanvasDrawRoundRect(canvas, round);
    OH_Drawing_CanvasDetachBrush(canvas);

    OH_Drawing_RoundRectDestroy(round);
    OH_Drawing_RectDestroy(rect);
    OH_Drawing_ShadowLayerDestroy(shadow);
    OH_Drawing_BrushDestroy(brush);
}

}  // namespace breezy

#endif  // BREEZY_RENDER_SHAPES_H
