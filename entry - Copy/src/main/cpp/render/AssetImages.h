/*
 * Icons and weather images on the native canvas.
 *
 * ArkUI resolves $r('app.media.ic_list') and tints it with .fillColor(). Native code has neither:
 * it can only reach rawfile, and only through a NativeResourceManager the ArkTS side hands over.
 * So every asset ArkUIApp keeps in resources/base/media lives in resources/rawfile here, is
 * decoded by the image framework, and is tinted by a blend-mode colour filter — which is what
 * .fillColor() is underneath.
 *
 * Decoded images are cached by name and pixel size. The vector icons are decoded straight to the
 * size they are drawn at, so they stay sharp rather than being scaled after the fact, and the
 * cache means a resize is the only thing that ever re-decodes.
 */
#ifndef BREEZY_RENDER_ASSETIMAGES_H
#define BREEZY_RENDER_ASSETIMAGES_H

#include <native_drawing/drawing_canvas.h>
#include <rawfile/raw_file_manager.h>

#include <cstdint>
#include <string>

namespace breezy {

/** Takes ownership; pass the manager built from the ArkTS context.resourceManager. */
void SetResourceManager(NativeResourceManager* manager);

/**
 * Draws `fileName` from rawfile into a `sizePx` square at (xPx, yPx), tinted with `color`.
 *
 * Tinting replaces every colour while keeping the alpha, so it suits the single-colour Material
 * glyphs (ic_list, ic_time, …) exactly as ?attr/colorSurfaceInverse does on Android. Pass
 * `tint = false` for the weather images, which carry their own colours.
 */
void DrawAsset(OH_Drawing_Canvas* canvas, const std::string& fileName, double xPx, double yPx,
               double sizePx, uint32_t color, bool tint);

/**
 * The same draw at a per-call opacity, for the animated weather icon's fading layers.
 *
 * `alpha` is 0..1 and multiplies the asset's own alpha, which is what an ArkUI `.opacity()` on the
 * Image does. Kept separate from DrawAsset rather than added as a defaulted parameter so the
 * dozens of existing call sites keep their exact meaning.
 */
void DrawAssetWithAlpha(OH_Drawing_Canvas* canvas, const std::string& fileName, double xPx,
                        double yPx, double sizePx, double alpha);

/** Drops every cached decode — call when the surface goes away. */
void ClearAssetCache();

}  // namespace breezy

#endif  // BREEZY_RENDER_ASSETIMAGES_H
