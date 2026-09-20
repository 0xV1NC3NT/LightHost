cmake_minimum_required(VERSION 3.22)

project(LightHost VERSION 1.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Enable ASIO support by dropping the Steinberg ASIO SDK into ThirdParty/ASIOSDK
# (the SDK cannot be redistributed) and setting -DLIGHTHOST_ENABLE_ASIO=ON.
option(LIGHTHOST_ENABLE_ASIO "Enable ASIO device support (requires the ASIO SDK)" OFF)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.2
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_gui_app(LightHost
    VERSION "1.3.0"
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
    target_include_directories(LightHost PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/ThirdParty/ASIOSDK/common")
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
