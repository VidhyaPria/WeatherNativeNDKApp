/*
 * The only bridge between the ArkTS shell and the C++ app.
 *
 * Two things happen here. First, ArkUI hands us the OH_NativeXComponent that backs the
 * <XComponent> in Index.ets; we register surface callbacks on it and everything after that is
 * drawn by C++. Second, we expose setScene() so the ArkTS shell â€” which owns the HTTP request,
 * because the native SDK ships no HTTP stack â€” can tell the renderer what the weather is.
 */
#include <ace/xcomponent/native_interface_xcomponent.h>
#include <hilog/log.h>
#include <napi/native_api.h>
#include <native_vsync/native_vsync.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>

#include "common/Units.h"
#include "data/AirQuality.h"
#include "data/Database.h"
#include "data/FmiAlerts.h"
#include "data/OpenMeteoMapper.h"
#include "render/AssetImages.h"
#include "data/SourceCatalogue.h"
#include "render/Renderer.h"
#include "render/TextPainter.h"
#include "view/TrendData.h"

#undef LOG_TAG
#define LOG_TAG "BreezyNative"

namespace breezy {
namespace {

/**
 * One Renderer per XComponent id. ArkUI can create and destroy the surface across page
 * transitions, so lookup is by id rather than by a single global.
 */
std::unordered_map<std::string, std::unique_ptr<Renderer>>& Renderers() {
    static std::unordered_map<std::string, std::unique_ptr<Renderer>> renderers;
    return renderers;
}

std::string IdOf(OH_NativeXComponent* component) {
    char id[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    uint64_t size = OH_XCOMPONENT_ID_LEN_MAX + 1;
    if (OH_NativeXComponent_GetXComponentId(component, id, &size) != 0) {
        return {};
    }
    return std::string(id, static_cast<size_t>(size));
}

/** Defined below, next to the threadsafe functions they post to. */
void OnNavigate(const std::string& destination);
void OnOpenLink(const std::string& url);
void OnRefresh(int locationIndex);
void OnOrderChanged(const std::string& order);
void OnThemeChanged(bool lightBackground);
void CallNavigate(napi_env env, napi_value callback, void* context, void* data);
void CallRefresh(napi_env env, napi_value callback, void* context, void* data);
napi_value SetThemeHandler(napi_env env, napi_callback_info info);

Renderer* RendererFor(const std::string& id) {
    auto it = Renderers().find(id);
    return it == Renderers().end() ? nullptr : it->second.get();
}

void OnSurfaceCreatedCB(OH_NativeXComponent* component, void* window) {
    const std::string id = IdOf(component);
    if (id.empty()) {
        return;
    }
    uint64_t width = 0;
    uint64_t height = 0;
    OH_NativeXComponent_GetXComponentSize(component, window, &width, &height);

    auto& slot = Renderers()[id];
    if (slot == nullptr) {
        slot = std::make_unique<Renderer>();
    }
    slot->SetNavigationHandler(&OnNavigate);
    slot->SetLinkHandler(&OnOpenLink);
    slot->SetRefreshHandler(&OnRefresh);
    slot->SetOrderHandler(&OnOrderChanged);
    slot->SetThemeHandler(&OnThemeChanged);
    slot->OnSurfaceCreated(static_cast<OHNativeWindow*>(window), width, height);
}

void OnSurfaceChangedCB(OH_NativeXComponent* component, void* window) {
    Renderer* renderer = RendererFor(IdOf(component));
    if (renderer == nullptr) {
        return;
    }
    uint64_t width = 0;
    uint64_t height = 0;
    OH_NativeXComponent_GetXComponentSize(component, window, &width, &height);
    renderer->OnSurfaceChanged(width, height);
}

void OnSurfaceDestroyedCB(OH_NativeXComponent* component, void* /*window*/) {
    const std::string id = IdOf(component);
    if (Renderer* renderer = RendererFor(id)) {
        renderer->OnSurfaceDestroyed();
    }
    Renderers().erase(id);
}

/**
 * Drives frames while something is still moving.
 *
 * ArkUI runs its own animator; here a fling only continues if someone keeps asking for vsync.
 * The callback arrives on the vsync thread, which is why Renderer guards its scroll state â€” the
 * UI thread is writing it from DispatchTouchEvent at the same time.
 */
class FrameDriver {
public:
    void Attach(Renderer* renderer) {
        renderer_ = renderer;
        if (vsync_ == nullptr) {
            vsync_ = OH_NativeVSync_Create("breezy", 6);
        }
    }

    void Detach() {
        renderer_ = nullptr;
        running_ = false;
    }

    /**
     * Starts the loop if it is not already running.
     *
     * The claim is a compare-exchange because Start() is called from the UI thread on every touch
     * event while OnFrame may be clearing `running_` on the vsync thread at the same moment; a
     * plain read-then-write could see "running" from a loop that is in the act of stopping and
     * leave the gesture with nothing driving it.
     */
    void Start() {
        if (renderer_ == nullptr || vsync_ == nullptr) {
            return;
        }
        bool expected = false;
        if (!running_.compare_exchange_strong(expected, true)) {
            return;
        }
        lastTimestamp_ = 0;
        Request();
    }

private:
    void Request() { OH_NativeVSync_RequestFrame(vsync_, &FrameDriver::OnFrame, this); }

    static void OnFrame(long long timestamp, void* data) {
        auto* self = static_cast<FrameDriver*>(data);
        if (self == nullptr || self->renderer_ == nullptr || !self->running_) {
            return;
        }
        // Timestamps are nanoseconds. The first frame has no predecessor to measure against.
        double seconds = 1.0 / 60.0;
        if (self->lastTimestamp_ != 0 && timestamp > self->lastTimestamp_) {
            seconds = static_cast<double>(timestamp - self->lastTimestamp_) / 1.0e9;
        }
        self->lastTimestamp_ = timestamp;
        // A long stall must not teleport the fling to the end of the list.
        seconds = std::min(seconds, 0.1);

        /*
         * The next vsync is requested AFTER the frame is drawn, and that is load-bearing.
         *
         * Requesting FIRST was tried, to overlap the wait with the work: the frame costs ~19.95 ms
         * (p50) against an 8.33 ms period, so requesting afterwards lands in the third period and
         * pins the loop at one frame per three vsyncs — 42 fps, p90 25.50 ms, far too regular to be
         * jitter. Pipelining did exactly what it should on paper and measured 53.9 fps (p50
         * 16.72 ms).
         *
         * When OnVSync says there is nothing left to animate the request has already gone out, and
         * there is no cancel in the C API. `running_` is cleared instead: the pending callback
         * fires once, sees it, and returns without re-requesting. One wasted wake-up at the end of
         * a gesture, which is the same cost the old code paid on every frame of one.
         *
         * A scare worth recording, because it nearly caused a revert: after scrolling DOWN the
         * background stops animating and two frames three seconds apart are pixel-identical. That
         * is not this — it is Renderer::OnVSync's `BackgroundScrollRate() < 1.0` guard, which stops
         * stepping the scene once the page has scrolled it out of view. The old ordering does
         * exactly the same. Test animation with the page scrolled to the TOP.
         *
         * What this does NOT fix is the frame's cost: `block` is 10.7 ms of it (~6.5 visible detail
         * tiles at 1.64 ms each, re-rasterised every frame while only their y changes), so the loop
         * is still short of the 8.33 ms needed for 120. The fix for that is the trade ColumnCache
         * already makes for trend columns — cache each block as a bitmap and blit it — which costs
         * roughly 1.4 MB per block and so has to be weighed against the memory comparison this
         * project exists for.
         */
        self->Request();
        if (!self->renderer_->OnVSync(seconds)) {
            self->running_ = false;
        }
    }

    Renderer* renderer_ = nullptr;
    OH_NativeVSync* vsync_ = nullptr;
    std::atomic<bool> running_{false};
    long long lastTimestamp_ = 0;
};

FrameDriver g_frameDriver;

TouchSample::Phase PhaseOf(OH_NativeXComponent_TouchEventType type) {
    switch (type) {
        case OH_NATIVEXCOMPONENT_DOWN: return TouchSample::Phase::Down;
        case OH_NATIVEXCOMPONENT_MOVE: return TouchSample::Phase::Move;
        case OH_NATIVEXCOMPONENT_UP:   return TouchSample::Phase::Up;
        default:                       return TouchSample::Phase::Cancel;
    }
}

void DispatchTouchEventCB(OH_NativeXComponent* component, void* window) {
    Renderer* renderer = RendererFor(IdOf(component));
    if (renderer == nullptr) {
        return;
    }
    OH_NativeXComponent_TouchEvent event;
    if (OH_NativeXComponent_GetTouchEvent(component, window, &event) != 0) {
        return;
    }

    TouchSample sample;
    sample.phase = PhaseOf(event.type);
    sample.x = event.x;
    sample.y = event.y;
    // The event clock is nanoseconds; the gesture code works in milliseconds.
    sample.timeMs = static_cast<double>(event.timeStamp) / 1.0e6;
    renderer->OnTouch(sample);

    /*
     * The loop runs from touch-down and paints every frame of the gesture itself.
     *
     * It has to start here rather than from a move, because the long press that begins a drag is
     * timed in the loop and a finger held still sends no further events to time it from. What is
     * deliberately NOT here any more is a RenderFrame() call: painting the page from the touch
     * thread cost 35-66 ms per move in a trace, off the vsync beat and on top of the loop's own
     * repaint of the same state.
     */
    g_frameDriver.Attach(renderer);
    g_frameDriver.Start();
}

OH_NativeXComponent_Callback g_callback{
    .OnSurfaceCreated = OnSurfaceCreatedCB,
    .OnSurfaceChanged = OnSurfaceChangedCB,
    .OnSurfaceDestroyed = OnSurfaceDestroyedCB,
    .DispatchTouchEvent = DispatchTouchEventCB,
};

/** Pulls the OH_NativeXComponent out of the module exports and attaches our callbacks to it. */
void AttachXComponent(napi_env env, napi_value exports) {
    napi_value exportInstance = nullptr;
    if (napi_get_named_property(env, exports, OH_NATIVE_XCOMPONENT_OBJ, &exportInstance) != napi_ok) {
        OH_LOG_WARN(LOG_APP, "no XComponent on exports");
        return;
    }
    OH_NativeXComponent* component = nullptr;
    if (napi_unwrap(env, exportInstance, reinterpret_cast<void**>(&component)) != napi_ok ||
        component == nullptr) {
        // Init runs once for the plain `import ... from 'libentry.so'` too, and that pass has no
        // XComponent attached. Only the pass triggered by the node itself carries one.
        return;
    }
    OH_NativeXComponent_RegisterCallback(component, &g_callback);
}

double DoubleArg(napi_env env, napi_value value, double fallback) {
    double result = fallback;
    if (napi_get_value_double(env, value, &result) != napi_ok) {
        return fallback;
    }
    return result;
}

bool BoolArg(napi_env env, napi_value value, bool fallback) {
    bool result = fallback;
    if (napi_get_value_bool(env, value, &result) != napi_ok) {
        return fallback;
    }
    return result;
}

/** Reads a JS string of unknown length into a std::string. */
std::string StringArg(napi_env env, napi_value value) {
    size_t length = 0;
    if (napi_get_value_string_utf8(env, value, nullptr, 0, &length) != napi_ok) {
        return {};
    }
    std::string text(length, '\0');
    size_t written = 0;
    if (napi_get_value_string_utf8(env, value, text.data(), length + 1, &written) != napi_ok) {
        return {};
    }
    text.resize(written);
    return text;
}

/** DateExtensions.kt reads the device's clock preference; @ohos.i18n is the only source. */
bool g_is24Hour = true;

/**
 * setEnvironment(density, region, is24Hour): void
 *
 * The two facts the renderer cannot obtain for itself. `density` turns the ported vp dimensions
 * into surface pixels; `region` is what @ohos.i18n reported, and Breezy picks each display unit
 * from it per quantity (TemperatureUnit.getDefaultUnit kt:210-213). The rule stays in C++ â€” only
 * the region code crosses.
 */
napi_value SetEnvironment(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value args[3] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 3) {
        return nullptr;
    }
    SetDisplayDensity(static_cast<float>(DoubleArg(env, args[0], 1.0)));
    SetRegion(StringArg(env, args[1]));
    g_is24Hour = BoolArg(env, args[2], true);
    return nullptr;
}

/**
 * setInsets(xcomponentId, topPx, bottomPx): void
 *
 * The window is laid out full screen (BreezyActivity.kt:39 enableEdgeToEdge), so the surface now
 * reaches under the status and navigation bars and the renderer has to know how far in to start â€”
 * the same value Android's `doOnApplyWindowInsets` gives the AppBar and the RecyclerView.
 */
napi_value SetInsets(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value args[3] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 3) {
        return nullptr;
    }
    char id[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    size_t idLength = 0;
    napi_get_value_string_utf8(env, args[0], id, sizeof(id), &idLength);
    if (Renderer* renderer = RendererFor(std::string(id, idLength))) {
        renderer->SetInsets(DoubleArg(env, args[1], 0.0), DoubleArg(env, args[2], 0.0));
        renderer->RenderFrame();
    }
    return nullptr;
}

/**
 * setKeyboardInset(xcomponentId, heightPx): void â€” how far the IME reaches up the window.
 *
 * Screen 2b's bottom bar rides above it, as SearchActivity's does by virtue of Android resizing
 * the window for the keyboard.
 */
napi_value SetKeyboardInset(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 2) {
        return nullptr;
    }
    char id[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    size_t idLength = 0;
    napi_get_value_string_utf8(env, args[0], id, sizeof(id), &idLength);
    if (Renderer* renderer = RendererFor(std::string(id, idLength))) {
        renderer->SetKeyboardInset(DoubleArg(env, args[1], 0.0));
    }
    return nullptr;
}

/**
 * setResourceManager(resmgr): void â€” the ArkTS context.resourceManager, so native code can reach
 * rawfile. Everything ArkUIApp keeps in resources/base/media lives there in this build, because
 * rawfile is the only resource directory the NDK can open.
 */
napi_value SetResourceManagerBinding(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 1) {
        return nullptr;
    }
    SetResourceManager(OH_ResourceManager_InitNativeResourceManager(env, args[0]));
    return nullptr;
}

/**
 * The shell's navigation callback, kept as a threadsafe function because taps arrive on the UI
 * thread but the renderer may report them from either.
 */
napi_threadsafe_function g_navigate = nullptr;

void OnNavigate(const std::string& destination) {
    if (g_navigate == nullptr) {
        return;
    }
    // The callback owns the copy; CallNavigate deletes it.
    napi_call_threadsafe_function(g_navigate, new std::string(destination), napi_tsfn_nonblocking);
}

void CallNavigate(napi_env env, napi_value callback, void*, void* data) {
    std::unique_ptr<std::string> destination(static_cast<std::string*>(data));
    if (env == nullptr || callback == nullptr) {
        return;
    }
    napi_value undefined = nullptr;
    napi_get_undefined(env, &undefined);
    napi_value argument = nullptr;
    napi_create_string_utf8(env, destination->c_str(), destination->size(), &argument);
    napi_call_function(env, undefined, callback, 1, &argument, nullptr);
}

/**
 * The shell's "open this URL" callback — the About screen's AlertDialogLink OK.
 *
 * The renderer draws the confirmation itself, so this is only ever called for a link the user has
 * already agreed to; the shell just owns the browsable ability.
 */
napi_threadsafe_function g_openLink = nullptr;

void OnOpenLink(const std::string& url) {
    if (g_openLink == nullptr) {
        return;
    }
    napi_call_threadsafe_function(g_openLink, new std::string(url), napi_tsfn_nonblocking);
}

void CallOpenLink(napi_env env, napi_value callback, void*, void* data) {
    std::unique_ptr<std::string> url(static_cast<std::string*>(data));
    if (env == nullptr || callback == nullptr) {
        return;
    }
    napi_value undefined = nullptr;
    napi_get_undefined(env, &undefined);
    napi_value argument = nullptr;
    napi_create_string_utf8(env, url->c_str(), url->size(), &argument);
    napi_call_function(env, undefined, callback, 1, &argument, nullptr);
}

/** setLinkHandler(fn): void — only the shell can start a browsable ability. */
napi_value SetLinkHandler(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 1) {
        return nullptr;
    }
    napi_value name = nullptr;
    napi_create_string_utf8(env, "breezyOpenLink", NAPI_AUTO_LENGTH, &name);
    napi_create_threadsafe_function(env, args[0], nullptr, name, 0, 1, nullptr, nullptr, nullptr,
                                    CallOpenLink, &g_openLink);
    for (auto& entry : Renderers()) {
        entry.second->SetLinkHandler(&OnOpenLink);
    }
    return nullptr;
}

/**
 * setAppVersion(surfaceId, versionName): void — `BuildConfig.VERSION_NAME`.
 *
 * Only the shell can read the bundle, so it pushes the string the About header renders. ArkUIApp
 * reads the same field through `bundleManager`, so both ports show the same line.
 */
napi_value SetAppVersion(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2] = {nullptr, nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 2) {
        return nullptr;
    }
    Renderer* renderer = RendererFor(StringArg(env, args[0]));
    if (renderer != nullptr) {
        renderer->SetAppVersion(StringArg(env, args[1]));
    }
    return nullptr;
}

/** The shell's refresh callback, for pull-to-refresh and for a location the user swipes to. */
napi_threadsafe_function g_refresh = nullptr;

void OnRefresh(int locationIndex) {
    if (g_refresh != nullptr) {
        napi_call_threadsafe_function(g_refresh, new int(locationIndex), napi_tsfn_nonblocking);
    }
}

void CallRefresh(napi_env env, napi_value callback, void*, void* data) {
    std::unique_ptr<int> index(static_cast<int*>(data));
    if (env == nullptr || callback == nullptr) {
        return;
    }
    napi_value undefined = nullptr;
    napi_get_undefined(env, &undefined);
    napi_value argument = nullptr;
    napi_create_int32(env, *index, &argument);
    napi_call_function(env, undefined, callback, 1, &argument, nullptr);
}

/** setRefreshHandler(fn): void â€” the shell owns the HTTP, so a pull has to ask it. */
napi_value SetRefreshHandler(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 1) {
        return nullptr;
    }
    napi_value name = nullptr;
    napi_create_string_utf8(env, "breezyRefresh", NAPI_AUTO_LENGTH, &name);
    napi_create_threadsafe_function(env, args[0], nullptr, name, 0, 1, nullptr, nullptr, nullptr,
                                    CallRefresh, &g_refresh);
    for (auto& entry : Renderers()) {
        entry.second->SetRefreshHandler(&OnRefresh);
    }
    return nullptr;
}

/**
 * The status bar's foreground follows the sky, so the renderer has to tell the shell when the sky
 * changes â€” a location swipe or a refresh that flips day to night.
 *
 * MainActivity.updateSystemBarStyle (kt:216-228) does the same thing from the Android side; only
 * the shell can reach `setWindowSystemBarProperties`, so it crosses back as a 0/1.
 */
napi_threadsafe_function g_theme = nullptr;

void OnThemeChanged(bool lightBackground) {
    if (g_theme != nullptr) {
        napi_call_threadsafe_function(g_theme, new int(lightBackground ? 1 : 0),
                                      napi_tsfn_nonblocking);
    }
}

napi_value SetThemeHandler(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 1) {
        return nullptr;
    }
    napi_value name = nullptr;
    napi_create_string_utf8(env, "breezyTheme", NAPI_AUTO_LENGTH, &name);
    napi_create_threadsafe_function(env, args[0], nullptr, name, 0, 1, nullptr, nullptr, nullptr,
                                    CallRefresh, &g_theme);
    for (auto& entry : Renderers()) {
        entry.second->SetThemeHandler(&OnThemeChanged);
    }
    return nullptr;
}

/** The card order goes back to the shell, which owns the file it is persisted in. */
napi_threadsafe_function g_orderChanged = nullptr;

void OnOrderChanged(const std::string& order) {
    if (g_orderChanged != nullptr) {
        napi_call_threadsafe_function(g_orderChanged, new std::string(order),
                                      napi_tsfn_nonblocking);
    }
}

/** setOrderHandler(fn) / restoreCardOrder(id, csv). */
napi_value SetOrderHandler(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 1) {
        return nullptr;
    }
    napi_value name = nullptr;
    napi_create_string_utf8(env, "breezyOrder", NAPI_AUTO_LENGTH, &name);
    napi_create_threadsafe_function(env, args[0], nullptr, name, 0, 1, nullptr, nullptr, nullptr,
                                    CallNavigate, &g_orderChanged);
    for (auto& entry : Renderers()) {
        entry.second->SetOrderHandler(&OnOrderChanged);
    }
    return nullptr;
}

napi_value RestoreCardOrder(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 2) {
        return nullptr;
    }
    char id[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    size_t idLength = 0;
    napi_get_value_string_utf8(env, args[0], id, sizeof(id), &idLength);
    if (Renderer* renderer = RendererFor(std::string(id, idLength))) {
        renderer->RestoreOrder(StringArg(env, args[1]));
    }
    return nullptr;
}

/**
 * endRefresh(xcomponentId): void â€” clears the pull-to-refresh spinner after a FAILED refresh.
 * A successful one clears it through applyForecast; without this path a network error would
 * leave the spinner parked under the toolbar forever.
 */
napi_value EndRefreshBinding(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 1) {
        return nullptr;
    }
    char id[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    size_t idLength = 0;
    napi_get_value_string_utf8(env, args[0], id, sizeof(id), &idLength);
    if (Renderer* renderer = RendererFor(std::string(id, idLength))) {
        renderer->EndRefresh();
        renderer->RenderFrame();
    }
    return nullptr;
}

/** alertFeedLinks(rss): string[] â€” the CAP document URLs the shell must fetch next. */
napi_value AlertFeedLinks(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    napi_value result = nullptr;
    napi_create_array(env, &result);
    if (argc < 1) {
        return result;
    }
    const std::vector<std::string> links = ParseAlertFeed(StringArg(env, args[0]));
    for (size_t i = 0; i < links.size(); ++i) {
        napi_value item = nullptr;
        napi_create_string_utf8(env, links[i].c_str(), links[i].size(), &item);
        napi_set_element(env, result, static_cast<uint32_t>(i), item);
    }
    return result;
}

/**
 * applyAlerts(xcomponentId, locationIndex, region, capDocuments): void
 *
 * `capDocuments` is the CAP XML bodies joined by a form feed â€” one string rather than an array,
 * because these are ~650 KB each and crossing them as separate JS strings doubles the peak.
 */
napi_value ApplyAlerts(napi_env env, napi_callback_info info) {
    size_t argc = 4;
    napi_value args[4] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 4) {
        return nullptr;
    }
    char id[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    size_t idLength = 0;
    napi_get_value_string_utf8(env, args[0], id, sizeof(id), &idLength);
    Renderer* renderer = RendererFor(std::string(id, idLength));
    if (renderer == nullptr) {
        return nullptr;
    }

    const int locationIndex = static_cast<int>(DoubleArg(env, args[1], 0.0));
    const std::string region = StringArg(env, args[2]);
    const std::string documents = StringArg(env, args[3]);
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

    std::vector<Alert> alerts;
    size_t start = 0;
    while (start <= documents.size()) {
        const size_t end = documents.find('\f', start);
        const std::string one =
            documents.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!one.empty()) {
            for (Alert& alert : ParseCapAlert(one, region, nowMs)) {
                alerts.push_back(std::move(alert));
            }
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }

    OH_LOG_INFO(LOG_APP, "alerts for location %{public}d: %{public}zu", locationIndex,
                alerts.size());
    renderer->SetAlerts(locationIndex, std::move(alerts));
    return nullptr;
}

/** setNavigationHandler(fn): void â€” every tap destination on this screen is a placeholder. */
napi_value SetNavigationHandler(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 1) {
        return nullptr;
    }
    napi_value name = nullptr;
    napi_create_string_utf8(env, "breezyNavigate", NAPI_AUTO_LENGTH, &name);
    napi_create_threadsafe_function(env, args[0], nullptr, name, 0, 1, nullptr, nullptr, nullptr,
                                    CallNavigate, &g_navigate);
    for (auto& entry : Renderers()) {
        entry.second->SetNavigationHandler(&OnNavigate);
    }
    return nullptr;
}

/** registerFont(buffer): boolean â€” the .ttf the shell read out of rawfile. */
napi_value RegisterFont(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    void* data = nullptr;
    size_t length = 0;
    bool registered = false;
    if (argc >= 1 && napi_get_arraybuffer_info(env, args[0], &data, &length) == napi_ok) {
        registered = RegisterAppFont(static_cast<const uint8_t*>(data), length);
    }
    napi_value result = nullptr;
    napi_get_boolean(env, registered, &result);
    return result;
}

/**
 * applyForecast(xcomponentId, city, body, latitude, longitude, darkMode): void
 *
 * `body` is the Open-Meteo forecast response verbatim. The shell reads nothing out of it â€” parsing,
 * the WMO code-to-kind rule, the half-day aggregation and the sun-times calculation all happen in
 * C++, so the two ports can only disagree about pixels, never about what the response said.
 */
napi_value ApplyForecast(napi_env env, napi_callback_info info) {
    size_t argc = 9;
    napi_value args[9] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 9) {
        return nullptr;
    }

    char id[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    size_t idLength = 0;
    napi_get_value_string_utf8(env, args[0], id, sizeof(id), &idLength);

    Renderer* renderer = RendererFor(std::string(id, idLength));
    if (renderer == nullptr) {
        OH_LOG_WARN(LOG_APP, "applyForecast before surface for %{public}s", id);
        return nullptr;
    }

    const int locationIndex = static_cast<int>(DoubleArg(env, args[1], 0.0));
    const int locationCount = static_cast<int>(DoubleArg(env, args[2], 1.0));
    const std::string city = StringArg(env, args[3]);
    const std::string body = StringArg(env, args[4]);
    const std::string airQualityBody = StringArg(env, args[5]);
    const double latitude = DoubleArg(env, args[6], 0.0);
    const double longitude = DoubleArg(env, args[7], 0.0);

    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

    ForecastResult forecast = MapForecast(body, latitude, longitude, nowMs);
    if (!forecast.ok) {
        OH_LOG_ERROR(LOG_APP, "forecast body did not parse (%{public}zu bytes)", body.size());
        return nullptr;
    }
    double peakNowcast = 0.0;
    for (const Minutely& bucket : forecast.weather.minutelyForecast) {
        peakNowcast = std::max(peakNowcast, bucket.precipitationIntensity);
    }
    OH_LOG_INFO(LOG_APP,
                "forecast: kind=%{public}d daylight=%{public}d offset=%{public}d "
                "hourly=%{public}zu daily=%{public}zu today=%{public}d "
                "minutely=%{public}zu peak=%{public}.2f alerts=%{public}zu",
                static_cast<int>(forecast.weather.current.weatherKind),
                forecast.daylight ? 1 : 0, forecast.utcOffsetSeconds,
                forecast.weather.hourlyForecast.size(), forecast.weather.dailyForecast.size(),
                forecast.weather.todayIndex, forecast.weather.minutelyForecast.size(),
                peakNowcast, forecast.weather.alertList.size());

    SceneState scene;
    scene.city = city;
    scene.kind = forecast.weather.current.weatherKind;
    scene.daylight = forecast.daylight;
    /*
     * BreezyWeather.kt updateDayNightMode: the DEFAULT setting (DarkMode.SYSTEM â†’
     * MODE_NIGHT_FOLLOW_SYSTEM) falls into the `else` branch and becomes
     * UiModeManager.MODE_NIGHT_AUTO on Android 12+ â€” which is TWILIGHT-driven, not system-driven.
     * So the stock app turns dark at night however the system is set, which is why the Android
     * reference shows dark cards on a phone whose `cmd uimode night` reports "no".
     *
     * MODE_NIGHT_AUTO REPLACES the system value rather than combining with it, so a daytime
     * location stays light even on a dark-themed device â€” which is also what ArkUIApp's
     * MainScreen.isDark() does. The system colorMode still crosses from the shell for the pre-S
     * path, where the same setting really does follow the system.
     */
    (void)BoolArg(env, args[8], false);
    // `darkMode` is DERIVED by the renderer from `daylight` and the settings (SetScene) — it is not
    // a property of the parsed data, and computing it here would freeze it at fetch time.
    scene.utcOffsetSeconds = forecast.utcOffsetSeconds;
    scene.hasWeather = true;
    scene.is24Hour = g_is24Hour;
    scene.latitude = latitude;
    scene.longitude = longitude;
    // A failed air-quality leg leaves the AQ tabs and the two tiles empty and costs nothing else.
    ApplyAirQualitySeries(forecast.weather, ParseAirQualitySeries(airQualityBody),
                          forecast.utcOffsetSeconds);
    ApplyCurrentAirQuality(airQualityBody, forecast.weather.current, nowMs);
    scene.weather = std::move(forecast.weather);
    renderer->SetScene(locationIndex, locationCount, std::move(scene));
    // The weather background animates continuously, so the frame loop starts with the data.
    g_frameDriver.Attach(renderer);
    g_frameDriver.Start();
    return nullptr;
}

/* ------------------------------------------------------- screen 2b: add a location ------------ */

/**
 * The three callbacks the search screen needs, and why each one exists.
 *
 * `g_searchUi`  the screen opened, closed, or was covered by one of its own dialogs. Only ArkTS can
 *               attach @ohos.inputMethod, so the shell raises, hides and dismisses the keyboard on
 *               this signal. Nothing else about the field is the shell's: its glyphs, placeholder
 *               and caret are drawn natively.
 * `g_pick`      a result row was chosen, as `id|place|region|admin2|countryCode|latitude|longitude`. C++ parsed the
 *               geocoding body, so C++ hands back the fields; the shell still never reads a
 *               response.
 *
 * There is deliberately no "run the search" callback: the shell is the one holding the typed string
 * â€” it has to, to keep the IME's own text state â€” so it runs the request off its own Search key
 * rather than asking C++ for a query it just sent there.
 */
napi_threadsafe_function g_searchUi = nullptr;
napi_threadsafe_function g_pick = nullptr;
/** The time picker's own edit session — separate from the search field's (screen 3d). */
napi_threadsafe_function g_timeEntry = nullptr;

void OnSearchUi(int state) {
    if (g_searchUi != nullptr) {
        napi_call_threadsafe_function(g_searchUi, new int(state), napi_tsfn_nonblocking);
    }
}

void OnTimeEntry(int state) {
    if (g_timeEntry != nullptr) {
        napi_call_threadsafe_function(g_timeEntry, new int(state), napi_tsfn_nonblocking);
    }
}

void OnPick(const std::string& encoded) {
    if (g_pick != nullptr) {
        napi_call_threadsafe_function(g_pick, new std::string(encoded), napi_tsfn_nonblocking);
    }
}

/** setSearchUiHandler(fn) / setPickHandler(fn). */
napi_value SetSearchUiHandler(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 1) {
        return nullptr;
    }
    napi_value name = nullptr;
    napi_create_string_utf8(env, "breezySearchUi", NAPI_AUTO_LENGTH, &name);
    // CallRefresh is the int-argument marshaller; the boolean crosses as 0/1, as the theme does.
    napi_create_threadsafe_function(env, args[0], nullptr, name, 0, 1, nullptr, nullptr, nullptr,
                                    CallRefresh, &g_searchUi);
    for (auto& entry : Renderers()) {
        entry.second->SetSearchUiHandler(&OnSearchUi);
    }
    return nullptr;
}

/** setTimeEntryHandler(fn) — screen 3d's TimeInput. */
napi_value SetTimeEntryHandler(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 1) {
        return nullptr;
    }
    napi_value name = nullptr;
    napi_create_string_utf8(env, "breezyTimeEntry", NAPI_AUTO_LENGTH, &name);
    napi_create_threadsafe_function(env, args[0], nullptr, name, 0, 1, nullptr, nullptr, nullptr,
                                    CallRefresh, &g_timeEntry);
    for (auto& entry : Renderers()) {
        entry.second->SetTimeEntryHandler(&OnTimeEntry);
    }
    return nullptr;
}

/** setTimeEntryText(id, text) — what the shell has collected for the focused field. */
napi_value SetTimeEntryText(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 2) {
        return nullptr;
    }
    char id[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    size_t idLength = 0;
    napi_get_value_string_utf8(env, args[0], id, sizeof(id), &idLength);
    if (Renderer* renderer = RendererFor(std::string(id, idLength))) {
        renderer->SetTimeEntryText(StringArg(env, args[1]));
    }
    return nullptr;
}

/** setLocationSettings(id, locationService, baiduApiKey) — screen 3e's two stored values. */
napi_value SetLocationSettings(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value args[3] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 3) {
        return nullptr;
    }
    char id[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    size_t idLength = 0;
    napi_get_value_string_utf8(env, args[0], id, sizeof(id), &idLength);
    if (Renderer* renderer = RendererFor(std::string(id, idLength))) {
        renderer->SetLocationSettings(StringArg(env, args[1]), StringArg(env, args[2]));
    }
    return nullptr;
}

/**
 * setSourceSettings(id, defaultForecastSource, prefs) — screen 3f's stored values.
 *
 * `prefs` is one `<storageKey>\t<value>` per line, and only for keys that HAVE a value: an absent
 * key is a row on its default, which is what clearing one back to the bundled default means
 * (ClimWebService.kt:366-368 removes the key rather than storing "").
 */
napi_value SetSourceSettings(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value args[3] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 3) {
        return nullptr;
    }
    char id[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    size_t idLength = 0;
    napi_get_value_string_utf8(env, args[0], id, sizeof(id), &idLength);
    if (Renderer* renderer = RendererFor(std::string(id, idLength))) {
        renderer->SetSourceSettings(StringArg(env, args[1]), StringArg(env, args[2]));
    }
    return nullptr;
}

/** setDisplayLists(id, cards, daily, hourly) — screens 3h/3i/3j's three ordered lists. */
napi_value SetDisplayLists(napi_env env, napi_callback_info info) {
    size_t argc = 4;
    napi_value args[4] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 4) {
        return nullptr;
    }
    char id[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    size_t idLength = 0;
    napi_get_value_string_utf8(env, args[0], id, sizeof(id), &idLength);
    if (Renderer* renderer = RendererFor(std::string(id, idLength))) {
        renderer->SetDisplayLists(StringArg(env, args[1]), StringArg(env, args[2]),
                                  StringArg(env, args[3]));
    }
    return nullptr;
}

/** setLocationPermissions(id, coarse, precise) — the live grant state only ArkTS can read. */
napi_value SetLocationPermissions(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value args[3] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 3) {
        return nullptr;
    }
    char id[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    size_t idLength = 0;
    napi_get_value_string_utf8(env, args[0], id, sizeof(id), &idLength);
    if (Renderer* renderer = RendererFor(std::string(id, idLength))) {
        renderer->SetLocationPermissions(BoolArg(env, args[1], false),
                                         BoolArg(env, args[2], false));
    }
    return nullptr;
}

/** setPermissionHandler(fn) — where a request goes; the shell prompts and pushes the answer back. */
napi_threadsafe_function g_permission = nullptr;

void OnPermission(const std::string& permission) {
    if (g_permission != nullptr) {
        napi_call_threadsafe_function(g_permission, new std::string(permission),
                                      napi_tsfn_nonblocking);
    }
}

napi_value SetPermissionHandler(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 1) {
        return nullptr;
    }
    napi_value name = nullptr;
    napi_create_string_utf8(env, "breezyPermission", NAPI_AUTO_LENGTH, &name);
    // CallNavigate is the std::string marshaller the navigation handler already uses.
    napi_create_threadsafe_function(env, args[0], nullptr, name, 0, 1, nullptr, nullptr, nullptr,
                                    CallNavigate, &g_permission);
    for (auto& entry : Renderers()) {
        entry.second->SetPermissionHandler(&OnPermission);
    }
    return nullptr;
}

napi_value SetPickHandler(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 1) {
        return nullptr;
    }
    napi_value name = nullptr;
    napi_create_string_utf8(env, "breezyPick", NAPI_AUTO_LENGTH, &name);
    napi_create_threadsafe_function(env, args[0], nullptr, name, 0, 1, nullptr, nullptr, nullptr,
                                    CallNavigate, &g_pick);
    for (auto& entry : Renderers()) {
        entry.second->SetPickHandler(&OnPick);
    }
    return nullptr;
}

/**
 * The list-edit callback: `delete|<i>`, `move|<from>|<to>`, `sources|<i>|<ids>`.
 *
 * One callback rather than three, because all three are the same thing to the shell â€” a write to
 * the location list, then a persist. The renderer owns only what is drawn.
 */
napi_threadsafe_function g_listEdit = nullptr;

void OnListEdit(const std::string& command) {
    if (g_listEdit != nullptr) {
        napi_call_threadsafe_function(g_listEdit, new std::string(command), napi_tsfn_nonblocking);
    }
}

napi_value SetListEditHandler(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 1) {
        return nullptr;
    }
    napi_value name = nullptr;
    napi_create_string_utf8(env, "breezyListEdit", NAPI_AUTO_LENGTH, &name);
    napi_create_threadsafe_function(env, args[0], nullptr, name, 0, 1, nullptr, nullptr, nullptr,
                                    CallNavigate, &g_listEdit);
    for (auto& entry : Renderers()) {
        entry.second->SetListEditHandler(&OnListEdit);
    }
    return nullptr;
}

/**
 * The settings-edit callback: `<key>|<value>`, where key is "dark_mode" or "day_night".
 *
 * Screen 3a's choices. C++ owns no files, so a chosen value is handed to the shell to store; the
 * shell then pushes the values back through setDarkModeSettings, which is what actually re-themes.
 */
napi_threadsafe_function g_settingsEdit = nullptr;

void OnSettingsEdit(const std::string& command) {
    if (g_settingsEdit != nullptr) {
        napi_call_threadsafe_function(g_settingsEdit, new std::string(command),
                                      napi_tsfn_nonblocking);
    }
}

napi_value SetSettingsEditHandler(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 1) {
        return nullptr;
    }
    napi_value name = nullptr;
    napi_create_string_utf8(env, "breezySettingsEdit", NAPI_AUTO_LENGTH, &name);
    napi_create_threadsafe_function(env, args[0], nullptr, name, 0, 1, nullptr, nullptr, nullptr,
                                    CallNavigate, &g_settingsEdit);
    for (auto& entry : Renderers()) {
        entry.second->SetSettingsEditHandler(&OnSettingsEdit);
    }
    return nullptr;
}

/**
 * setLocationDetails(xcomponentId, details): void â€” one line per location,
 * `formattedId \t countryCode \t region \t admin2 \t latitude \t longitude \t sources`.
 *
 * The scenes carry what a WEATHER page draws; the weather-sources dialog needs where the location
 * is and which sources it is on, and only the shell has those.
 */
napi_value SetLocationDetails(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 2) {
        return nullptr;
    }
    char id[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    size_t idLength = 0;
    napi_get_value_string_utf8(env, args[0], id, sizeof(id), &idLength);
    if (Renderer* renderer = RendererFor(std::string(id, idLength))) {
        renderer->SetLocationDetails(StringArg(env, args[1]));
    }
    return nullptr;
}

/**
 * listChanged(xcomponentId, command): void â€” a list change the SHELL originated.
 *
 * Only ever `insert|<index>`, which is the Undo of a delete: everything else starts in the renderer
 * and is applied there.
 */
napi_value ListChanged(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 2) {
        return nullptr;
    }
    char id[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    size_t idLength = 0;
    napi_get_value_string_utf8(env, args[0], id, sizeof(id), &idLength);
    if (Renderer* renderer = RendererFor(std::string(id, idLength))) {
        renderer->OnListChanged(StringArg(env, args[1]));
    }
    return nullptr;
}

/** setSearchQuery(xcomponentId, text): void â€” the field's contents, from the IME listener. */
napi_value SetSearchQuery(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 2) {
        return nullptr;
    }
    char id[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    size_t idLength = 0;
    napi_get_value_string_utf8(env, args[0], id, sizeof(id), &idLength);
    if (Renderer* renderer = RendererFor(std::string(id, idLength))) {
        renderer->SetSearchQuery(StringArg(env, args[1]));
    }
    return nullptr;
}

/**
 * applySearchResults(xcomponentId, body, searched): void
 *
 * `body` is the geocoding response verbatim â€” the shell reads nothing out of it, so the two ports
 * cannot disagree about which hits the API returned or how their admin levels read.
 */
napi_value ApplySearchResults(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value args[3] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 3) {
        return nullptr;
    }
    char id[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    size_t idLength = 0;
    napi_get_value_string_utf8(env, args[0], id, sizeof(id), &idLength);
    if (Renderer* renderer = RendererFor(std::string(id, idLength))) {
        renderer->SetSearchResults(StringArg(env, args[1]), StringArg(env, args[2]));
    }
    return nullptr;
}

/**
 * closeSearch(xcomponentId): void â€” the add went through, so screen 2b finishes onto the list.
 *
 * The shell calls it rather than the tap doing it directly, because a duplicate is REFUSED and the
 * screen has to stay open for another pick (ManagementFragment.kt:471-476) â€” and only the shell
 * holds the saved list to test that against.
 */
napi_value CloseSearch(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 1) {
        return nullptr;
    }
    char id[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    size_t idLength = 0;
    napi_get_value_string_utf8(env, args[0], id, sizeof(id), &idLength);
    if (Renderer* renderer = RendererFor(std::string(id, idLength))) {
        renderer->CloseSearchScreen();
    }
    return nullptr;
}

/**
 * setSavedLocationIds(xcomponentId, ids): void â€” the saved formattedIds, newline separated.
 *
 * What the weather-sources dialog's duplicate warning is tested against (LocationPreferences.kt:
 * 544-554). The shell owns the list, so it pushes; asking back across NAPI could only answer
 * asynchronously, and this decides a pixel.
 */
napi_value SetListLoading(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 2) {
        return nullptr;
    }
    char id[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    size_t idLength = 0;
    napi_get_value_string_utf8(env, args[0], id, sizeof(id), &idLength);
    if (Renderer* renderer = RendererFor(std::string(id, idLength))) {
        renderer->SetListLoading(BoolArg(env, args[1], false));
    }
    return nullptr;
}

/**
 * setSystemDarkMode(xcomponentId, dark): void — the ABILITY's colour mode, on its own channel.
 *
 * The system mode already crosses bundled with each forecast, because that is what picks the
 * weather_background_* set. Screen 3 needs it WITHOUT data: SettingsActivity takes
 * BreezyWeatherTheme's default `isSystemInDarkTheme()` (:107-111) rather than the location's
 * daylight, so the settings screen must be able to theme itself before any forecast has landed.
 */
napi_value SetSystemDarkMode(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 2) {
        return nullptr;
    }
    char id[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    size_t idLength = 0;
    napi_get_value_string_utf8(env, args[0], id, sizeof(id), &idLength);
    if (Renderer* renderer = RendererFor(std::string(id, idLength))) {
        renderer->SetSystemDarkMode(BoolArg(env, args[1], false));
    }
    return nullptr;
}

/**
 * setDarkModeSettings(xcomponentId, darkMode, dayNightForLocations): void
 *
 * The two Appearance settings. The shell owns the preferences file, as it owns every other file in
 * this port; C++ applies ThemeManager.isLightTheme with them.
 */
napi_value SetDarkModeSettings(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value args[3] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 3) {
        return nullptr;
    }
    char id[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    size_t idLength = 0;
    napi_get_value_string_utf8(env, args[0], id, sizeof(id), &idLength);
    if (Renderer* renderer = RendererFor(std::string(id, idLength))) {
        renderer->SetDarkModeSettings(StringArg(env, args[1]), BoolArg(env, args[2], false));
    }
    return nullptr;
}

/**
 * setAlternateCalendar(xcomponentId, value): void
 *
 * SettingsManager.alternateCalendar — "none", "" for the regional default, or a Unicode calendar
 * key. Separate from setDarkModeSettings because it is a separate preference with its own default,
 * and bundling it would have made that call's argument list mean two unrelated things.
 */
napi_value SetAlternateCalendar(napi_env env, napi_callback_info info) {
    size_t argc = 4;
    napi_value args[4] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 4) {
        return nullptr;
    }
    char id[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    size_t idLength = 0;
    napi_get_value_string_utf8(env, args[0], id, sizeof(id), &idLength);
    if (Renderer* renderer = RendererFor(std::string(id, idLength))) {
        renderer->SetAlternateCalendar(StringArg(env, args[1]), StringArg(env, args[2]),
                                       StringArg(env, args[3]));
    }
    return nullptr;
}

/* ------------------------------------------------------------------ the relational store ----- */

/**
 * dbOpen(filesDir, bundleName): boolean
 *
 * The shell owns `context.filesDir` and the bundle name, so it opens the store; everything after
 * this is C++'s. See data/Database.h on why this port stopped writing flat files.
 */
napi_value DbOpen(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    napi_value out = nullptr;
    const bool ok = argc >= 2 && DatabaseOpen(StringArg(env, args[0]), StringArg(env, args[1]));
    napi_get_boolean(env, ok, &out);
    return out;
}

/** dbReadLocations(): string — the rows, one per line, payload verbatim. */
napi_value DbReadLocations(napi_env env, napi_callback_info info) {
    (void)info;
    std::string joined;
    for (const StoredLocation& row : DatabaseReadLocations()) {
        if (!joined.empty()) {
            joined.push_back('\n');
        }
        joined += row.payload;
    }
    napi_value out = nullptr;
    napi_create_string_utf8(env, joined.c_str(), NAPI_AUTO_LENGTH, &out);
    return out;
}

/**
 * dbWriteLocations(formattedIds, payloads): void
 *
 * Both are newline-joined and index-aligned. The list is written WHOLE, in one transaction, which
 * is how the shell has always handed it over.
 */
napi_value DbWriteLocations(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 2) {
        return nullptr;
    }
    const std::string ids = StringArg(env, args[0]);
    const std::string payloads = StringArg(env, args[1]);

    std::vector<StoredLocation> rows;
    size_t idPos = 0;
    size_t payloadPos = 0;
    int index = 0;
    while (idPos <= ids.size() && !ids.empty()) {
        const size_t idEnd = ids.find('\n', idPos);
        const size_t payloadEnd = payloads.find('\n', payloadPos);
        StoredLocation row;
        row.formattedId = ids.substr(idPos, idEnd == std::string::npos ? std::string::npos
                                                                       : idEnd - idPos);
        row.payload = payloadPos <= payloads.size()
                          ? payloads.substr(payloadPos, payloadEnd == std::string::npos
                                                            ? std::string::npos
                                                            : payloadEnd - payloadPos)
                          : "";
        row.position = index++;
        if (!row.formattedId.empty()) {
            rows.push_back(std::move(row));
        }
        if (idEnd == std::string::npos) {
            break;
        }
        idPos = idEnd + 1;
        payloadPos = payloadEnd == std::string::npos ? payloads.size() + 1 : payloadEnd + 1;
    }
    DatabaseWriteLocations(rows);
    return nullptr;
}

/** dbReadWeather(formattedId, kind): string */
napi_value DbReadWeather(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    const std::string value =
        argc >= 2 ? DatabaseReadWeather(StringArg(env, args[0]), StringArg(env, args[1])) : "";
    napi_value out = nullptr;
    napi_create_string_utf8(env, value.c_str(), NAPI_AUTO_LENGTH, &out);
    return out;
}

/** dbWriteWeather(formattedId, kind, payload, updatedAtMs): void */
napi_value DbWriteWeather(napi_env env, napi_callback_info info) {
    size_t argc = 4;
    napi_value args[4] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 4) {
        return nullptr;
    }
    double updatedAt = 0.0;
    napi_get_value_double(env, args[3], &updatedAt);
    DatabaseWriteWeather(StringArg(env, args[0]), StringArg(env, args[1]), StringArg(env, args[2]),
                         static_cast<int64_t>(updatedAt));
    return nullptr;
}

/** dbWeatherUpdatedAt(formattedId, kind): number — 0 when there is no row. */
napi_value DbWeatherUpdatedAt(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    const int64_t value =
        argc >= 2 ? DatabaseWeatherUpdatedAt(StringArg(env, args[0]), StringArg(env, args[1])) : 0;
    napi_value out = nullptr;
    napi_create_double(env, static_cast<double>(value), &out);
    return out;
}

/** dbDeleteWeather(formattedId): void */
napi_value DbDeleteWeather(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc >= 1) {
        DatabaseDeleteWeather(StringArg(env, args[0]));
    }
    return nullptr;
}

/** dbReadSetting(key): string */
napi_value DbReadSetting(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    const std::string value = argc >= 1 ? DatabaseReadSetting(StringArg(env, args[0])) : "";
    napi_value out = nullptr;
    napi_create_string_utf8(env, value.c_str(), NAPI_AUTO_LENGTH, &out);
    return out;
}

/** dbWriteSetting(key, value): void */
napi_value DbWriteSetting(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc >= 2) {
        DatabaseWriteSetting(StringArg(env, args[0]), StringArg(env, args[1]));
    }
    return nullptr;
}

/**
 * setModuleSettings(xcomponentId, weekIconMode, monochromeIcons, notificationWidget,
 *                   notificationPersistent, notificationStyle, notificationTempIcon,
 *                   notificationFeelsLike): void
 *
 * Screen 3e's seven values. Pushed together, as screen 3c's six are: they belong to one screen and
 * a change to any of them redraws the same rows.
 */
napi_value SetModuleSettings(napi_env env, napi_callback_info info) {
    size_t argc = 8;
    napi_value args[8] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 8) {
        return nullptr;
    }
    char id[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    size_t idLength = 0;
    napi_get_value_string_utf8(env, args[0], id, sizeof(id), &idLength);
    if (Renderer* renderer = RendererFor(std::string(id, idLength))) {
        renderer->SetModuleSettings(StringArg(env, args[1]), BoolArg(env, args[2], false),
                                    BoolArg(env, args[3], false), BoolArg(env, args[4], true),
                                    StringArg(env, args[5]), BoolArg(env, args[6], false),
                                    BoolArg(env, args[7], false));
    }
    return nullptr;
}

/**
 * Screen 3b's five unit overrides, each a unit id or "auto".
 *
 * One call with all five rather than five calls: they are read together by every formatter, and a
 * partial push would render a page in a mix of old and new units for a frame.
 */
napi_value SetUnitSettings(napi_env env, napi_callback_info info) {
    size_t argc = 6;
    napi_value args[6] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 6) {
        return nullptr;
    }
    char id[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    size_t idLength = 0;
    napi_get_value_string_utf8(env, args[0], id, sizeof(id), &idLength);
    if (Renderer* renderer = RendererFor(std::string(id, idLength))) {
        renderer->SetUnitSettings(StringArg(env, args[1]), StringArg(env, args[2]),
                                  StringArg(env, args[3]), StringArg(env, args[4]),
                                  StringArg(env, args[5]));
    }
    return nullptr;
}

/** Screen 3c's six, all in one call for the reason setUnitSettings takes five. */
napi_value SetMainScreenSettings(napi_env env, napi_callback_info info) {
    size_t argc = 7;
    napi_value args[7] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 7) {
        return nullptr;
    }
    char id[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    size_t idLength = 0;
    napi_get_value_string_utf8(env, args[0], id, sizeof(id), &idLength);
    if (Renderer* renderer = RendererFor(std::string(id, idLength))) {
        renderer->SetMainScreenSettings(BoolArg(env, args[1], true), BoolArg(env, args[2], true),
                                        StringArg(env, args[3]), BoolArg(env, args[4], true),
                                        BoolArg(env, args[5], true), BoolArg(env, args[6], true));
    }
    return nullptr;
}

/**
 * Screen 3d's six plus the update interval it reads, in one call for the reason the two above take
 * theirs together. The defaults here are NOT uniform — they mirror SettingsManager's per-key ones,
 * so a missing argument falls back to what Android would have read.
 */
napi_value SetNotificationSettings(napi_env env, napi_callback_info info) {
    size_t argc = 10;
    napi_value args[10] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 10) {
        return nullptr;
    }
    char id[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    size_t idLength = 0;
    napi_get_value_string_utf8(env, args[0], id, sizeof(id), &idLength);
    if (Renderer* renderer = RendererFor(std::string(id, idLength))) {
        renderer->SetNotificationSettings(
            BoolArg(env, args[1], false),   // app_update_check_switch (kt:109)
            BoolArg(env, args[2], true),    // alert_notification_switch (kt:148)
            BoolArg(env, args[3], false),   // precipitation_notification_switch (kt:155)
            BoolArg(env, args[4], false),   // timing_forecast_switch_today (kt:381)
            BoolArg(env, args[5], false),   // timing_forecast_switch_tomorrow (kt:397)
            StringArg(env, args[6]), StringArg(env, args[7]), StringArg(env, args[8]),
            BoolArg(env, args[9], false));  // Context.is12Hour (DateExtensions.kt:42-43)
    }
    return nullptr;
}

/** setBackgroundUpdatesSettings(id, refreshRate, skipWhenBatteryLow) — screen 3c's two values. */
napi_value SetBackgroundUpdatesSettings(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value args[3] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 3) {
        return nullptr;
    }
    char id[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    size_t idLength = 0;
    napi_get_value_string_utf8(env, args[0], id, sizeof(id), &idLength);
    if (Renderer* renderer = RendererFor(std::string(id, idLength))) {
        // SettingsManager's default for ignoreUpdatesWhenBatteryLow is TRUE.
        renderer->SetBackgroundUpdatesValues(StringArg(env, args[1]),
                                             BoolArg(env, args[2], true));
    }
    return nullptr;
}

napi_value SetSavedLocationIds(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 2) {
        return nullptr;
    }
    char id[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    size_t idLength = 0;
    napi_get_value_string_utf8(env, args[0], id, sizeof(id), &idLength);
    if (Renderer* renderer = RendererFor(std::string(id, idLength))) {
        renderer->SetSavedLocationIds(StringArg(env, args[1]));
    }
    return nullptr;
}

/**
 * defaultSources(countryCode, region, admin2, latitude, longitude, isCurrentPosition): string
 *
 * Location.applyDefaultPreset, as the eight ids comma separated. The shell calls it for the seeded
 * locations, which are stand-ins for locations the user added and which Android would have preset
 * the same way; a location added through the search screen gets its preset from the dialog instead.
 */
napi_value DefaultSources(napi_env env, napi_callback_info info) {
    size_t argc = 6;
    napi_value args[6] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    std::string encoded;
    if (argc >= 6) {
        SourceLocation location;
        location.countryCode = StringArg(env, args[0]);
        location.region = StringArg(env, args[1]);
        location.admin2 = StringArg(env, args[2]);
        location.latitude = DoubleArg(env, args[3], 0.0);
        location.longitude = DoubleArg(env, args[4], 0.0);
        location.isCurrentPosition = BoolArg(env, args[5], false);
        const LocationSources sources = ApplyDefaultPreset(location);
        encoded = sources.forecast + "," + sources.current + "," + sources.airQuality + "," +
                  sources.pollen + "," + sources.minutely + "," + sources.alert + "," +
                  sources.normals + "," + sources.reverseGeocoding;
    }
    napi_value result = nullptr;
    napi_create_string_utf8(env, encoded.c_str(), encoded.size(), &result);
    return result;
}

/** setSearchLoading(xcomponentId, loading): void â€” the linear progress bar. */
napi_value SetSearchLoading(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 2) {
        return nullptr;
    }
    char id[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
    size_t idLength = 0;
    napi_get_value_string_utf8(env, args[0], id, sizeof(id), &idLength);
    if (Renderer* renderer = RendererFor(std::string(id, idLength))) {
        renderer->SetSearchLoading(BoolArg(env, args[1], false));
    }
    return nullptr;
}

/**
 * onBackPressed(xcomponentId): boolean â€” true when an overlay screen consumed the gesture.
 *
 * ManagementFragment and SearchActivity both close on back. Without this the gesture left the app,
 * which is the one navigation Android never does from either.
 */
napi_value OnBackPressedBinding(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1] = {nullptr};
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    bool consumed = false;
    if (argc >= 1) {
        char id[OH_XCOMPONENT_ID_LEN_MAX + 1] = {};
        size_t idLength = 0;
        napi_get_value_string_utf8(env, args[0], id, sizeof(id), &idLength);
        if (Renderer* renderer = RendererFor(std::string(id, idLength))) {
            consumed = renderer->OnBackPressed();
        }
    }
    napi_value result = nullptr;
    napi_get_boolean(env, consumed, &result);
    return result;
}

}  // namespace
}  // namespace breezy

EXTERN_C_START
static napi_value Init(napi_env env, napi_value exports) {
    napi_property_descriptor desc[] = {
        {"setEnvironment", nullptr, breezy::SetEnvironment, nullptr, nullptr, nullptr,
         napi_default, nullptr},
        {"setResourceManager", nullptr, breezy::SetResourceManagerBinding, nullptr, nullptr,
         nullptr, napi_default, nullptr},
        {"setInsets", nullptr, breezy::SetInsets, nullptr, nullptr, nullptr, napi_default,
         nullptr},
        {"setKeyboardInset", nullptr, breezy::SetKeyboardInset, nullptr, nullptr, nullptr,
         napi_default, nullptr},
        {"setThemeHandler", nullptr, breezy::SetThemeHandler, nullptr, nullptr, nullptr,
         napi_default, nullptr},
        {"setNavigationHandler", nullptr, breezy::SetNavigationHandler, nullptr, nullptr, nullptr,
         napi_default, nullptr},
        {"setRefreshHandler", nullptr, breezy::SetRefreshHandler, nullptr, nullptr, nullptr,
         napi_default, nullptr},
        {"setLinkHandler", nullptr, breezy::SetLinkHandler, nullptr, nullptr, nullptr,
         napi_default, nullptr},
        {"setAppVersion", nullptr, breezy::SetAppVersion, nullptr, nullptr, nullptr,
         napi_default, nullptr},
        {"setOrderHandler", nullptr, breezy::SetOrderHandler, nullptr, nullptr, nullptr,
         napi_default, nullptr},
        {"restoreCardOrder", nullptr, breezy::RestoreCardOrder, nullptr, nullptr, nullptr,
         napi_default, nullptr},
        {"endRefresh", nullptr, breezy::EndRefreshBinding, nullptr, nullptr, nullptr,
         napi_default, nullptr},
        {"alertFeedLinks", nullptr, breezy::AlertFeedLinks, nullptr, nullptr, nullptr,
         napi_default, nullptr},
        {"applyAlerts", nullptr, breezy::ApplyAlerts, nullptr, nullptr, nullptr, napi_default,
         nullptr},
        {"registerFont", nullptr, breezy::RegisterFont, nullptr, nullptr, nullptr, napi_default,
         nullptr},
        {"applyForecast", nullptr, breezy::ApplyForecast, nullptr, nullptr, nullptr, napi_default,
         nullptr},
        {"setSearchUiHandler", nullptr, breezy::SetSearchUiHandler, nullptr, nullptr, nullptr,
         napi_default, nullptr},
        {"setPickHandler", nullptr, breezy::SetPickHandler, nullptr, nullptr, nullptr,
         napi_default, nullptr},
        {"setSearchQuery", nullptr, breezy::SetSearchQuery, nullptr, nullptr, nullptr,
         napi_default, nullptr},
        {"applySearchResults", nullptr, breezy::ApplySearchResults, nullptr, nullptr, nullptr,
         napi_default, nullptr},
        {"setSearchLoading", nullptr, breezy::SetSearchLoading, nullptr, nullptr, nullptr,
         napi_default, nullptr},
        {"closeSearch", nullptr, breezy::CloseSearch, nullptr, nullptr, nullptr, napi_default,
         nullptr},
        {"setSavedLocationIds", nullptr, breezy::SetSavedLocationIds, nullptr, nullptr, nullptr,
         napi_default, nullptr},
        {"setListLoading", nullptr, breezy::SetListLoading, nullptr, nullptr, nullptr,
         napi_default, nullptr},
        {"setSystemDarkMode", nullptr, breezy::SetSystemDarkMode, nullptr, nullptr, nullptr,
         napi_default, nullptr},
        {"setDarkModeSettings", nullptr, breezy::SetDarkModeSettings, nullptr, nullptr, nullptr,
         napi_default, nullptr},
        {"setAlternateCalendar", nullptr, breezy::SetAlternateCalendar, nullptr, nullptr, nullptr,
         napi_default, nullptr},
        {"setModuleSettings", nullptr, breezy::SetModuleSettings, nullptr, nullptr, nullptr,
         napi_default, nullptr},
        {"dbOpen", nullptr, breezy::DbOpen, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"dbReadLocations", nullptr, breezy::DbReadLocations, nullptr, nullptr, nullptr,
         napi_default, nullptr},
        {"dbWriteLocations", nullptr, breezy::DbWriteLocations, nullptr, nullptr, nullptr,
         napi_default, nullptr},
        {"dbReadWeather", nullptr, breezy::DbReadWeather, nullptr, nullptr, nullptr, napi_default,
         nullptr},
        {"dbWriteWeather", nullptr, breezy::DbWriteWeather, nullptr, nullptr, nullptr,
         napi_default, nullptr},
        {"dbWeatherUpdatedAt", nullptr, breezy::DbWeatherUpdatedAt, nullptr, nullptr, nullptr,
         napi_default, nullptr},
        {"dbDeleteWeather", nullptr, breezy::DbDeleteWeather, nullptr, nullptr, nullptr,
         napi_default, nullptr},
        {"dbReadSetting", nullptr, breezy::DbReadSetting, nullptr, nullptr, nullptr, napi_default,
         nullptr},
        {"dbWriteSetting", nullptr, breezy::DbWriteSetting, nullptr, nullptr, nullptr,
         napi_default, nullptr},
        {"setUnitSettings", nullptr, breezy::SetUnitSettings, nullptr, nullptr, nullptr,
         napi_default, nullptr},
        {"setMainScreenSettings", nullptr, breezy::SetMainScreenSettings, nullptr, nullptr, nullptr,
         napi_default, nullptr},
        {"setNotificationSettings", nullptr, breezy::SetNotificationSettings, nullptr, nullptr,
         nullptr, napi_default, nullptr},
        {"setBackgroundUpdatesSettings", nullptr, breezy::SetBackgroundUpdatesSettings, nullptr,
         nullptr, nullptr, napi_default, nullptr},
        {"setTimeEntryHandler", nullptr, breezy::SetTimeEntryHandler, nullptr, nullptr, nullptr,
         napi_default, nullptr},
        {"setTimeEntryText", nullptr, breezy::SetTimeEntryText, nullptr, nullptr, nullptr,
         napi_default, nullptr},
        {"setLocationSettings", nullptr, breezy::SetLocationSettings, nullptr, nullptr, nullptr,
         napi_default, nullptr},
        {"setLocationPermissions", nullptr, breezy::SetLocationPermissions, nullptr, nullptr,
         nullptr, napi_default, nullptr},
        {"setSourceSettings", nullptr, breezy::SetSourceSettings, nullptr, nullptr, nullptr,
         napi_default, nullptr},
        {"setDisplayLists", nullptr, breezy::SetDisplayLists, nullptr, nullptr, nullptr,
         napi_default, nullptr},
        {"setPermissionHandler", nullptr, breezy::SetPermissionHandler, nullptr, nullptr, nullptr,
         napi_default, nullptr},
        {"setSettingsEditHandler", nullptr, breezy::SetSettingsEditHandler, nullptr, nullptr,
         nullptr, napi_default, nullptr},
        {"defaultSources", nullptr, breezy::DefaultSources, nullptr, nullptr, nullptr,
         napi_default, nullptr},
        {"setListEditHandler", nullptr, breezy::SetListEditHandler, nullptr, nullptr, nullptr,
         napi_default, nullptr},
        {"setLocationDetails", nullptr, breezy::SetLocationDetails, nullptr, nullptr, nullptr,
         napi_default, nullptr},
        {"listChanged", nullptr, breezy::ListChanged, nullptr, nullptr, nullptr, napi_default,
         nullptr},
        {"onBackPressed", nullptr, breezy::OnBackPressedBinding, nullptr, nullptr, nullptr,
         napi_default, nullptr},
    };
    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);
    breezy::AttachXComponent(env, exports);
    return exports;
}
EXTERN_C_END

static napi_module entryModule = {
    .nm_version = 1,
    .nm_flags = 0,
    .nm_filename = nullptr,
    .nm_register_func = Init,
    .nm_modname = "entry",
    .nm_priv = ((void*)0),
    .reserved = {0},
};

extern "C" __attribute__((constructor)) void RegisterEntryModule(void) {
    napi_module_register(&entryModule);
}
