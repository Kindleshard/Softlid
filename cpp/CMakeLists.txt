cmake_minimum_required(VERSION 3.20)
project(BlinkBuddy LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# The C runtime linked into the exe: one file that runs anywhere, nothing to install.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# Small code rather than the last bit of speed: most of the time BlinkBuddy just waits.
set(CMAKE_CXX_FLAGS_RELEASE "/O1 /Ob2 /DNDEBUG")

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
# The control gallery (--ui-gallery) is for working on the interface: in debug builds only.
if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    list(FILTER SOURCES EXCLUDE REGEX "/src/tools/")
endif()
add_executable(BlinkBuddy WIN32 ${SOURCES} res/app.rc)
target_compile_definitions(BlinkBuddy PRIVATE $<$<CONFIG:Debug>:BB_GALLERY>)
# The translations are built into the exe compressed (a third of their size): LangPack packs them and writes lang.rc,
# which app.rc includes, and a change to one of them must make both again.
file(GLOB LANGUAGE_FILES CONFIGURE_DEPENDS res/lang/*.txt)
add_executable(LangPack tools/LangPack.cpp)
set(PACKED ${CMAKE_BINARY_DIR}/lang)
add_custom_command(OUTPUT ${PACKED}/lang.rc
    COMMAND LangPack ${CMAKE_SOURCE_DIR}/res/lang ${PACKED}
    DEPENDS LangPack ${LANGUAGE_FILES})
add_custom_target(PackedLanguages DEPENDS ${PACKED}/lang.rc)
add_dependencies(BlinkBuddy PackedLanguages)
set_source_files_properties(res/app.rc PROPERTIES OBJECT_DEPENDS ${PACKED}/lang.rc)

target_include_directories(BlinkBuddy PRIVATE src res ${PACKED})
target_compile_definitions(BlinkBuddy PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(BlinkBuddy PRIVATE /W4 /permissive- /utf-8 /EHsc
    $<$<CONFIG:Release>:/GL /Gy /Gw>)
target_link_options(BlinkBuddy PRIVATE /MANIFEST:NO   # the manifest is in app.rc
    $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>)
target_link_libraries(BlinkBuddy PRIVATE user32 gdi32 shell32 ole32 comctl32 advapi32 shlwapi uxtheme cabinet delayimp)
# Loaded the first time they are used, not at start: the tray app idles without drawing (Direct2D, DirectWrite),
# the GPU engine (Direct3D, the shader compiler), downloads, the backlight, the magnifier filter (off by default),
# sounds, the file dialog or the unpacking of a translation (English needs none) - and most sessions never use some.
target_link_options(BlinkBuddy PRIVATE
    /DELAYLOAD:d2d1.dll /DELAYLOAD:dwrite.dll /DELAYLOAD:d3d11.dll /DELAYLOAD:dxgi.dll /DELAYLOAD:d3dcompiler_47.dll
    /DELAYLOAD:urlmon.dll /DELAYLOAD:dxva2.dll /DELAYLOAD:magnification.dll /DELAYLOAD:winmm.dll /DELAYLOAD:comdlg32.dll
    /DELAYLOAD:uiautomationcore.dll /DELAYLOAD:cabinet.dll)

# The tests: the core logic (and the few platform pieces it reads through), run by build.ps1 after each build.
file(GLOB TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp src/core/*.cpp)
add_executable(BlinkBuddyTests ${TEST_SOURCES}
    src/platform/Text.cpp src/platform/Files.cpp src/platform/NightLight.cpp src/platform/Keys.cpp
    src/platform/Language.cpp src/platform/ResourceUse.cpp src/features/update/Updates.cpp tests/Languages.rc)
target_include_directories(BlinkBuddyTests PRIVATE src res tests ${PACKED})
target_compile_definitions(BlinkBuddyTests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
    BB_LANG_DIR="${CMAKE_SOURCE_DIR}/res/lang")
target_compile_options(BlinkBuddyTests PRIVATE /W4 /permissive- /utf-8 /EHsc)
target_link_libraries(BlinkBuddyTests PRIVATE user32 shell32 ole32 advapi32 cabinet)
add_dependencies(BlinkBuddyTests PackedLanguages)
set_source_files_properties(tests/Languages.rc PROPERTIES OBJECT_DEPENDS ${PACKED}/lang.rc)
