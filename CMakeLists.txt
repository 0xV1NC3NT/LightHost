cmake_minimum_required(VERSION 3.22)

project(LightHost VERSION 2.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ASIO support (-DLIGHTHOST_ENABLE_ASIO=ON). Uses ThirdParty/ASIOSDK if present,
# otherwise fetches the Steinberg ASIO SDK 2.3.4 (dual-licensed, used here under GPLv3).
option(LIGHTHOST_ENABLE_ASIO "Enable ASIO device support" OFF)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.2
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

set(LIGHTHOST_ASIO_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/ASIOSDK/common")
if(LIGHTHOST_ENABLE_ASIO AND NOT EXISTS "${LIGHTHOST_ASIO_INCLUDE_DIR}/asio.h")
    FetchContent_Declare(
        asiosdk
        GIT_REPOSITORY https://github.com/audiosdk/asio.git
        GIT_TAG 496a0765b8bb9c26f764f22f9a9712a937177db2
        GIT_SHALLOW FALSE
    )
    FetchContent_GetProperties(asiosdk)
    if(NOT asiosdk_POPULATED)
        FetchContent_Populate(asiosdk)
    endif()
    set(LIGHTHOST_ASIO_INCLUDE_DIR "${asiosdk_SOURCE_DIR}/common")
endif()

juce_add_gui_app(LightHost
    VERSION "2.0.0"
    PRODUCT_NAME "Light Host"
    COMPANY_NAME "Vincent"
    BUNDLE_ID "com.vincent.lighthost"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon.png"
    MICROPHONE_PERMISSION_ENABLED TRUE
    NEEDS_CURL FALSE
)

juce_generate_juce_header(LightHost)

juce_add_binary_data(LightHostAssets SOURCES
    Resources/icon.png
    Resources/menu_icon.png
    Resources/menu_icon_white.png
)

target_sources(LightHost PRIVATE
    Source/HostStartup.cpp
    Source/IconMenu.cpp
    Source/IconMenu.h
    Source/PluginWindow.cpp
    Source/PluginWindow.h
    Source/ControlPanelWindow.cpp
    Source/ControlPanelWindow.h
    Source/PluginChainListBox.cpp
    Source/PluginChainListBox.h
)

target_compile_definitions(LightHost PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_ASIO=$<BOOL:${LIGHTHOST_ENABLE_ASIO}>
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=1
    JUCE_ALSA=1
    JUCE_PLUGINHOST_VST=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
)

if(LIGHTHOST_ENABLE_ASIO)
    target_include_directories(LightHost PRIVATE "${LIGHTHOST_ASIO_INCLUDE_DIR}")
endif()

# Vectorize for AVX2. Trades support for pre-2013-ish CPUs for better SIMD
# codegen in JUCE's own buffer/gain routines; has no effect on hosted plugins,
# which are separate binaries compiled by their own vendors.
if(MSVC)
    target_compile_options(LightHost PRIVATE $<$<CONFIG:Release>:/arch:AVX2>)
else()
    target_compile_options(LightHost PRIVATE $<$<CONFIG:Release>:-mavx2>)
endif()

target_link_libraries(LightHost PRIVATE
    LightHostAssets
    juce::juce_audio_utils
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
