# Breezy Weather â€” NativeApp (C++/NDK port).
#
# Everything except the HTTP request is C++: the model, the Open-Meteo mapping, the astronomy,
# the theme, and all rendering through XComponent + libnative_drawing. The ArkTS shell is one
# XComponent and one fetch, because the native SDK ships no HTTP stack (no libcurl, no netstack)
# and vendoring one would add about a megabyte to the HAP â€” which would distort the very memory
# comparison this project exists to make.
cmake_minimum_required(VERSION 3.5.0)
project(NativeApp)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(NATIVERENDER_ROOT_PATH ${CMAKE_CURRENT_SOURCE_DIR})

if(DEFINED PACKAGE_FIND_FILE)
    include(${PACKAGE_FIND_FILE})
endif()

include_directories(${NATIVERENDER_ROOT_PATH}
                    ${NATIVERENDER_ROOT_PATH}/include
                    ${NATIVERENDER_ROOT_PATH}/third_party)

add_library(entry SHARED
    napi_init.cpp
    common/Formatter.cpp
    common/LocalTime.cpp
    common/Units.cpp
    data/AboutCatalogue.cpp
    data/AirQuality.cpp
    data/Astronomy.cpp
    data/FmiAlerts.cpp
    data/OpenMeteoCodes.cpp
    data/OpenMeteoGeocoding.cpp
    data/OpenMeteoMapper.cpp
    data/OpenMeteoModels.cpp
    data/Pollutant.cpp
    data/SourceCatalogue.cpp
    data/SourcePreferences.cpp
    data/CalendarCatalogue.cpp
    data/Database.cpp
    data/DisplayCatalogue.cpp
    input/Scroller.cpp
    json/Json.cpp
    render/AnimatedIcon.cpp
    render/AssetImages.cpp
    render/ColumnCache.cpp
    render/DetailBlockPainter.cpp
    render/EnterAnimation.cpp
    render/GravitySensor.cpp
    render/PageCards.cpp
    render/Renderer.cpp
    render/TextPainter.cpp
    render/Theme.cpp
    render/TrendCardPainter.cpp
    render/WeatherImplementors.cpp
    view/LocationListScreen.cpp
    view/ModulesScreen.cpp
    view/WorkerInfoScreen.cpp
    view/LocationSearchScreen.cpp
    view/AppearanceScreen.cpp
    view/UnitsScreen.cpp
    view/MainScreenSettingsScreen.cpp
    view/NotificationsScreen.cpp
    view/AboutScreen.cpp
    view/PrivacyPolicyScreen.cpp
    view/DependenciesScreen.cpp
    view/AlertListScreen.cpp
    view/PreviewIconScreen.cpp
    view/DebugScreen.cpp
    view/BackgroundUpdatesScreen.cpp
    view/LocationScreen.cpp
    view/WeatherSourcesScreen.cpp
    view/DisplayManageScreen.cpp
    view/DetailsScreen.cpp
    view/LocationPreferenceDialog.cpp
    view/PlaceholderScreen.cpp
    view/ModalList.cpp
    view/SettingsScreen.cpp
    view/Snackbar.cpp
    view/WeatherSourcesDialog.cpp
        view/DetailBlocks.cpp
    view/TrendData.cpp
)

target_link_libraries(entry PUBLIC
    libace_napi.z.so          # NAPI
    libace_ndk.z.so           # XComponent
    libnative_window.so       # the surface XComponent hands us
    libnative_buffer.so       # buffer usage/format flags for CPU-drawable buffers
    libnative_drawing.so      # OH_Drawing_* â€” the Canvas equivalent
    librawfile.z.so           # the only resource directory native code can open
    # PNG/SVG decode, standing in for $r('app.media.â€¦'). The OH_ImageSourceNative/OH_PixelmapNative
    # API lives in these, NOT in the similarly named lib*_ndk.z.so, which carry the older
    # image_source_mdk.h entry points.
    libimage_source.so
    libpixelmap.so
    # The relational store — the C surface of the very engine ArkUIApp reaches through
    # @ohos.data.relationalStore, so both ports sit on ONE database rather than one database and a
    # pile of flat files. See data/Database.h on why that matters for the memory comparison.
    libnative_rdb_ndk.z.so
    libnative_vsync.so       # drives the fling; ArkUI's animator has no NDK equivalent
    libohsensor.so           # TYPE_GRAVITY for the weather background's parallax
    libhilog_ndk.z.so
    libhitrace_ndk.z.so      # the frame's phases, so a slow frame can be attributed
)
