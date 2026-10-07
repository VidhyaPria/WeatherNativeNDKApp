#include "render/AssetImages.h"

#include <hilog/log.h>
#include <multimedia/image_framework/image/image_source_native.h>
#include <multimedia/image_framework/image/pixelmap_native.h>
#include <native_drawing/drawing_brush.h>
#include <native_drawing/drawing_color_filter.h>
#include <native_drawing/drawing_filter.h>
#include <native_drawing/drawing_pixel_map.h>
#include <native_drawing/drawing_rect.h>
#include <native_drawing/drawing_sampling_options.h>
#include <rawfile/raw_file.h>

#include <map>
#include <memory>
#include <vector>

#undef LOG_TAG
#define LOG_TAG "BreezyNative"

namespace breezy {
namespace {

NativeResourceManager* g_resourceManager = nullptr;

/** A decoded asset, held for as long as it might be drawn again. */
struct DecodedAsset {
    OH_PixelmapNative* pixelmap = nullptr;
    OH_Drawing_PixelMap* drawable = nullptr;

    ~DecodedAsset() {
        if (drawable != nullptr) {
            OH_Drawing_PixelMapDissolve(drawable);
        }
        if (pixelmap != nullptr) {
            OH_PixelmapNative_Release(pixelmap);
        }
    }
};

/** Keyed by name and the integer pixel size it was decoded at. */
using AssetKey = std::pair<std::string, int>;
std::map<AssetKey, std::unique_ptr<DecodedAsset>>& Cache() {
    static std::map<AssetKey, std::unique_ptr<DecodedAsset>> cache;
    return cache;
}

/*
 * Assets that could not be read or decoded, so a broken one is attempted ONCE.
 *
 * Without this a failure is retried on every frame: `ic_sunny.svg` and `ic_weather_night.svg`
 * carried a leading XML comment the OH decoder rejects with 7600201, and the trace showed them
 * being re-read out of the HAP and re-failed ~2x per frame, each failure also writing an
 * OH_LOG_ERROR — about 1250 hilog writes a second while scrolling.
 */
std::map<AssetKey, bool>& Failures() {
    static std::map<AssetKey, bool> failures;
    return failures;
}

std::vector<uint8_t> ReadRawFile(const std::string& fileName) {
    if (g_resourceManager == nullptr) {
        return {};
    }
    RawFile* file = OH_ResourceManager_OpenRawFile(g_resourceManager, fileName.c_str());
    if (file == nullptr) {
        OH_LOG_ERROR(LOG_APP, "rawfile missing: %{public}s", fileName.c_str());
        return {};
    }
    const long size = OH_ResourceManager_GetRawFileSize(file);
    std::vector<uint8_t> bytes(size > 0 ? static_cast<size_t>(size) : 0);
    if (!bytes.empty()) {
        const int read = OH_ResourceManager_ReadRawFile(file, bytes.data(), bytes.size());
        if (read != static_cast<int>(bytes.size())) {
            bytes.clear();
        }
    }
    OH_ResourceManager_CloseRawFile(file);
    return bytes;
}

/**
 * Decodes at the exact size it will be drawn at.
 *
 * For the SVG glyphs this is the difference between a crisp icon and a blurred one: the decoder
 * rasterises the vector at the requested size, whereas decoding at some default and scaling the
 * bitmap afterwards throws the resolution away.
 */
DecodedAsset* Decode(const std::string& fileName, int sizePx) {
    const AssetKey key{fileName, sizePx};
    auto cached = Cache().find(key);
    if (cached != Cache().end()) {
        return cached->second.get();
    }
    if (Failures().count(key) != 0) {
        return nullptr;
    }

    std::vector<uint8_t> bytes = ReadRawFile(fileName);
    if (bytes.empty()) {
        Failures()[key] = true;
        return nullptr;
    }

    OH_ImageSourceNative* source = nullptr;
    if (OH_ImageSourceNative_CreateFromData(bytes.data(), bytes.size(), &source) !=
            IMAGE_SUCCESS ||
        source == nullptr) {
        OH_LOG_ERROR(LOG_APP, "cannot open image %{public}s", fileName.c_str());
        Failures()[key] = true;
        return nullptr;
    }

    OH_DecodingOptions* options = nullptr;
    OH_DecodingOptions_Create(&options);
    Image_Size desired{static_cast<uint32_t>(sizePx), static_cast<uint32_t>(sizePx)};
    OH_DecodingOptions_SetDesiredSize(options, &desired);
    OH_DecodingOptions_SetPixelFormat(options, PIXEL_FORMAT_RGBA_8888);

    auto asset = std::make_unique<DecodedAsset>();
    const Image_ErrorCode status =
        OH_ImageSourceNative_CreatePixelmap(source, options, &asset->pixelmap);
    OH_DecodingOptions_Release(options);
    OH_ImageSourceNative_Release(source);

    if (status != IMAGE_SUCCESS || asset->pixelmap == nullptr) {
        OH_LOG_ERROR(LOG_APP, "cannot decode %{public}s: %{public}d", fileName.c_str(), status);
        Failures()[key] = true;
        return nullptr;
    }
    asset->drawable = OH_Drawing_PixelMapGetFromOhPixelMapNative(asset->pixelmap);
    if (asset->drawable == nullptr) {
        OH_LOG_ERROR(LOG_APP, "cannot wrap %{public}s for drawing", fileName.c_str());
        Failures()[key] = true;
        return nullptr;
    }

    DecodedAsset* result = asset.get();
    Cache().emplace(key, std::move(asset));
    return result;
}

}  // namespace

void SetResourceManager(NativeResourceManager* manager) {
    if (g_resourceManager != nullptr && g_resourceManager != manager) {
        OH_ResourceManager_ReleaseNativeResourceManager(g_resourceManager);
    }
    g_resourceManager = manager;
}

void DrawAsset(OH_Drawing_Canvas* canvas, const std::string& fileName, double xPx, double yPx,
               double sizePx, uint32_t color, bool tint) {
    const int size = static_cast<int>(sizePx + 0.5);
    if (size <= 0) {
        return;
    }
    DecodedAsset* asset = Decode(fileName, size);
    if (asset == nullptr) {
        return;
    }

    OH_Drawing_Brush* brush = nullptr;
    OH_Drawing_Filter* filter = nullptr;
    OH_Drawing_ColorFilter* colorFilter = nullptr;
    if (tint) {
        // SRC_IN keeps the glyph's alpha and replaces its colour — what .fillColor() does.
        colorFilter = OH_Drawing_ColorFilterCreateBlendMode(color, BLEND_MODE_SRC_IN);
        filter = OH_Drawing_FilterCreate();
        OH_Drawing_FilterSetColorFilter(filter, colorFilter);
        brush = OH_Drawing_BrushCreate();
        OH_Drawing_BrushSetFilter(brush, filter);
        OH_Drawing_CanvasAttachBrush(canvas, brush);
    }

    OH_Drawing_Rect* src = OH_Drawing_RectCreate(0.0f, 0.0f, static_cast<float>(size),
                                                 static_cast<float>(size));
    OH_Drawing_Rect* dst = OH_Drawing_RectCreate(
        static_cast<float>(xPx), static_cast<float>(yPx), static_cast<float>(xPx + sizePx),
        static_cast<float>(yPx + sizePx));
    OH_Drawing_SamplingOptions* sampling =
        OH_Drawing_SamplingOptionsCreate(FILTER_MODE_LINEAR, MIPMAP_MODE_NONE);
    OH_Drawing_CanvasDrawPixelMapRect(canvas, asset->drawable, src, dst, sampling);

    OH_Drawing_SamplingOptionsDestroy(sampling);
    OH_Drawing_RectDestroy(dst);
    OH_Drawing_RectDestroy(src);

    if (tint) {
        OH_Drawing_CanvasDetachBrush(canvas);
        OH_Drawing_BrushDestroy(brush);
        OH_Drawing_FilterDestroy(filter);
        OH_Drawing_ColorFilterDestroy(colorFilter);
    }
}

void DrawAssetWithAlpha(OH_Drawing_Canvas* canvas, const std::string& fileName, double xPx,
                        double yPx, double sizePx, double alpha) {
    if (alpha >= 0.999) {
        DrawAsset(canvas, fileName, xPx, yPx, sizePx, 0, false);
        return;
    }
    if (alpha <= 0.001) {
        return;
    }
    const int size = static_cast<int>(sizePx + 0.5);
    if (size <= 0) {
        return;
    }
    DecodedAsset* asset = Decode(fileName, size);
    if (asset == nullptr) {
        return;
    }

    /*
     * A white multiply at the wanted alpha scales the source's alpha and leaves its colours alone —
     * the blend-mode equivalent of an ArkUI `.opacity()`. DST_IN against a constant is what makes
     * it act on alpha only; SRC_IN (which the tinting path uses) would replace the colours too.
     */
    const uint32_t mask =
        (static_cast<uint32_t>(alpha * 255.0 + 0.5) << 24) | 0x00FFFFFFu;
    OH_Drawing_ColorFilter* colorFilter =
        OH_Drawing_ColorFilterCreateBlendMode(mask, BLEND_MODE_DST_IN);
    OH_Drawing_Filter* filter = OH_Drawing_FilterCreate();
    OH_Drawing_FilterSetColorFilter(filter, colorFilter);
    OH_Drawing_Brush* brush = OH_Drawing_BrushCreate();
    OH_Drawing_BrushSetFilter(brush, filter);
    OH_Drawing_CanvasAttachBrush(canvas, brush);

    OH_Drawing_Rect* src = OH_Drawing_RectCreate(0.0f, 0.0f, static_cast<float>(size),
                                                 static_cast<float>(size));
    OH_Drawing_Rect* dst = OH_Drawing_RectCreate(
        static_cast<float>(xPx), static_cast<float>(yPx), static_cast<float>(xPx + sizePx),
        static_cast<float>(yPx + sizePx));
    OH_Drawing_SamplingOptions* sampling =
        OH_Drawing_SamplingOptionsCreate(FILTER_MODE_LINEAR, MIPMAP_MODE_NONE);
    OH_Drawing_CanvasDrawPixelMapRect(canvas, asset->drawable, src, dst, sampling);

    OH_Drawing_SamplingOptionsDestroy(sampling);
    OH_Drawing_RectDestroy(dst);
    OH_Drawing_RectDestroy(src);
    OH_Drawing_CanvasDetachBrush(canvas);
    OH_Drawing_BrushDestroy(brush);
    OH_Drawing_FilterDestroy(filter);
    OH_Drawing_ColorFilterDestroy(colorFilter);
}

void ClearAssetCache() {
    Cache().clear();
    Failures().clear();
}

}  // namespace breezy
