cmake_minimum_required(VERSION 3.20)
project(PocketSoundMixer VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(PSM_BUILD_APP "Build the desktop app (GLFW + OpenGL + Dear ImGui)" ON)
option(PSM_BUILD_TESTS "Build the core tests" ON)

include(FetchContent)
set(FETCHCONTENT_QUIET ON)

FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(nlohmann_json)

# ---- Core: audio engine, EQ, mixer, presets. No UI dependencies. ----
add_library(psm_core STATIC
    src/core/AppCapture.cpp
    src/core/AudioEngine.cpp
    src/core/Channel.cpp
    src/core/DeviceVolume.cpp
    src/core/GraphicEq.cpp
    src/core/Mixer.cpp
    src/core/Paths.cpp
    src/core/Presets.cpp
    src/core/MiniaudioImpl.c)
target_include_directories(psm_core PUBLIC src/core third_party/miniaudio)
target_link_libraries(psm_core PUBLIC nlohmann_json::nlohmann_json)
# Strip miniaudio features we never use: smaller binary, faster compile.
target_compile_definitions(psm_core PUBLIC
    MA_NO_DECODING MA_NO_ENCODING MA_NO_RESOURCE_MANAGER MA_NO_GENERATION MA_NO_ENGINE MA_NO_NODE_GRAPH)
if(WIN32)
    # WASAPI per-app capture: COM, MMCSS thread priority, ActivateAudioInterfaceAsync.
    target_link_libraries(psm_core PUBLIC ole32 avrt mmdevapi shell32 runtimeobject)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(psm_core PUBLIC ${CMAKE_DL_LIBS} pthread m)
elseif(APPLE)
    target_link_libraries(psm_core PUBLIC "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox")
endif()

if(MSVC)
    target_compile_options(psm_core PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4 /permissive->)
    target_compile_definitions(psm_core PUBLIC _CRT_SECURE_NO_WARNINGS NOMINMAX)
else()
    target_compile_options(psm_core PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra>)
endif()

# ---- Desktop app ----
if(PSM_BUILD_APP)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE) # X11 only; runs under XWayland too
    FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.4
        GIT_SHALLOW TRUE)
    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.91.5
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(glfw imgui)
    find_package(OpenGL REQUIRED)

    add_library(imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
    target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
    target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
    target_link_libraries(imgui PUBLIC glfw OpenGL::GL)

    add_executable(PocketSoundMixer
        src/app/main.cpp
        src/app/Lang.cpp
        src/app/MixerUI.cpp
        src/app/OpenUrl.cpp
        src/app/Session.cpp
        src/app/Theme.cpp)
    target_link_libraries(PocketSoundMixer PRIVATE psm_core imgui)
    if(MSVC)
        target_compile_options(PocketSoundMixer PRIVATE /utf-8) # Turkish UI strings are UTF-8 literals
    endif()
    if(WIN32)
        # GUI app without a console window, but keep the standard main() entry point.
        set_target_properties(PocketSoundMixer PROPERTIES WIN32_EXECUTABLE TRUE)
        if(MSVC)
            target_link_options(PocketSoundMixer PRIVATE /ENTRY:mainCRTStartup)
        endif()
    endif()
endif()

# ---- Tests ----
if(PSM_BUILD_TESTS)
    enable_testing()
    add_executable(psm_core_tests tests/core_tests.cpp src/app/Session.cpp src/app/Lang.cpp)
    target_include_directories(psm_core_tests PRIVATE src/app)
    target_link_libraries(psm_core_tests PRIVATE psm_core)
    if(MSVC)
        target_compile_options(psm_core_tests PRIVATE /utf-8)
    endif()
    add_test(NAME core COMMAND psm_core_tests)
endif()
