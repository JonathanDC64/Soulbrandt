# The wrapper UI: start screen, in-game menu bar and settings (docs/32).
# FFCCEOT-Recomp's UI, itself adapted from Dusklight's (CC0), drawn with
# RmlUi (MIT) and FreeType (FTL) over the game frame, through its own D3D12
# renderer on the game's device (docs/76); on Windows, for one release, also
# through RmlUi's D3D11 renderer on D3D11On12 (DES_UI_RENDER=d3d11).
#
# RmlUi, FreeType and stb are downloaded at configure time
# into the build directory (FetchContent, not vendored). Everything here is its
# own static library: des_boot only links it, so des_boot's compile flags (and
# with them the lifted chunks' build) do not change.
include(FetchContent)

# ---- FreeType (FTL) ----
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
foreach(dep ZLIB BZIP2 PNG HARFBUZZ BROTLI)
  set(FT_DISABLE_${dep} ON CACHE BOOL "" FORCE)
endforeach()
# By commit (a tag can be moved; a commit cannot), so not shallow: a shallow clone works with tag names only.
FetchContent_Declare(freetype
  GIT_REPOSITORY https://github.com/freetype/freetype.git
  GIT_TAG 0a0221a1347e2f1e07c395263540026e9a0aa7c7   # VER-2-14-3
  EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(freetype)
if(NOT TARGET Freetype::Freetype)
  add_library(Freetype::Freetype ALIAS freetype)   # what RmlUi looks for
endif()

# ---- RmlUi (MIT) ----
set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(RMLUI_FONT_ENGINE "freetype" CACHE STRING "" FORCE)
set(RMLUI_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(RmlUi
  GIT_REPOSITORY https://github.com/mikke89/RmlUi.git
  GIT_TAG ba95ffe8bfb6370efb2cdcca927eaad4710c5413   # 6.3
  EXCLUDE_FROM_ALL)
FetchContent_MakeAvailable(RmlUi)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/rmlui_fixes.cmake)   # (fixes to its sources, made at configure time)

if(WIN32)
  # RmlUi's Win32 platform helpers (the system interface) and its D3D11 renderer (the fallback) are sources in its tree.
  set(RMLUI_BACKENDS ${rmlui_SOURCE_DIR}/Backends)
  add_library(des_rmlui_backend STATIC
    ${RMLUI_BACKENDS}/RmlUi_Renderer_DX11.cpp
    ${RMLUI_BACKENDS}/RmlUi_Platform_Win32.cpp)
  target_include_directories(des_rmlui_backend PUBLIC ${RMLUI_BACKENDS})
  target_link_libraries(des_rmlui_backend PUBLIC RmlUi::RmlUi PRIVATE d3d11 d3dcompiler dxgi imm32)
endif()

# ---- stb (public domain / MIT): the images and the reports' PNGs ----
# Headers only (stb_impl.cpp has the code); a commit, as stb has no releases.
FetchContent_Declare(stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG a7d4c2175630e1ea5b6f8dd00ab96fed0b97b96d)
FetchContent_MakeAvailable(stb)

# Third-party code: no warnings, optimised (the port configures with an empty
# build type, which has no optimisation flags of its own).
foreach(t freetype rmlui_core rmlui_debugger des_rmlui_backend)
  if(TARGET ${t} AND MSVC)
    target_compile_options(${t} PRIVATE /w /O2)
  elseif(TARGET ${t})
    target_compile_options(${t} PRIVATE -w -O2)
  endif()
endforeach()

# ---- Embedded styles and fonts (cmake/EmbedFiles.cmake) ----
set(DES_UI_DIR ${CMAKE_CURRENT_SOURCE_DIR})
file(GLOB DES_UI_STYLES CONFIGURE_DEPENDS ${DES_UI_DIR}/res/rml/*.rcss)
# Each look's sheet (looks.hpp) and the menus' own images (tools/ui/make_ui_art.py): a new one is picked up as it is.
file(GLOB DES_UI_LOOKS CONFIGURE_DEPENDS ${DES_UI_DIR}/res/rml/looks/*.rcss)
file(GLOB DES_UI_IMAGES CONFIGURE_DEPENDS ${DES_UI_DIR}/res/img/*.png)
set(DES_UI_FILES
  ${DES_UI_STYLES}
  ${DES_UI_LOOKS}
  ${DES_UI_IMAGES}
  ${DES_UI_DIR}/res/fonts/Cinzel.ttf
  ${DES_UI_DIR}/res/fonts/AlegreyaSans-Regular.ttf
  ${DES_UI_DIR}/res/fonts/AlegreyaSans-Bold.ttf
  ${DES_UI_DIR}/res/fonts/OldStandard-Regular.ttf
  ${DES_UI_DIR}/res/fonts/OldStandard-Bold.ttf
  ${DES_UI_DIR}/res/fonts/OldStandard-Italic.ttf
  ${DES_UI_DIR}/res/fonts/MaterialSymbolsRounded-Regular.ttf
  ${DES_UI_DIR}/../../PRIVACY.md)  # (the privacy note: privacy.cpp; its path in the table is ../../PRIVACY.md)
set(DES_UI_ASSETS ${CMAKE_CURRENT_BINARY_DIR}/ui_assets.cpp)
add_custom_command(
  OUTPUT ${DES_UI_ASSETS}
  COMMAND ${CMAKE_COMMAND} -D BASE_DIR=${DES_UI_DIR} -D OUTPUT=${DES_UI_ASSETS} "-DFILES=${DES_UI_FILES}"
          -P ${CMAKE_CURRENT_SOURCE_DIR}/../cmake/EmbedFiles.cmake
  DEPENDS ${DES_UI_FILES} ${CMAKE_CURRENT_SOURCE_DIR}/../cmake/EmbedFiles.cmake
  COMMENT "Embedding the UI's styles and fonts"
  VERBATIM)

# ---- The UI ----
# Each platform's mouse and crash reports (docs/76): Win32 on Windows (with the crash dialog and the D3D11 renderer),
# SDL elsewhere. The setup screens' files (child_process, setup_engine, setup_folder, setup_python, setup_http,
# setup_unzip, sha256, setup_protocol, setup_page, setup_window, setup_screens, setup_options, setup_options_window,
# setup_pc, setup_rpcs3, folder_picker; docs/88, docs/89, docs/90, docs/91) build on both: child_process.cpp, setup_engine.cpp,
# setup_python.cpp, setup_http.cpp, setup_pc.cpp, setup_rpcs3.cpp and folder_picker.cpp hold their few functions for
# each platform inside.
file(GLOB DES_UI_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
if(WIN32)
  set(DES_UI_OTHER mouse_sdl crash_report_posix)
else()
  set(DES_UI_OTHER render_d3d11 mouse crash_report crash_dialog)
endif()
foreach(f ${DES_UI_OTHER})
  list(REMOVE_ITEM DES_UI_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/${f}.cpp)
endforeach()
add_library(des_ui STATIC ${DES_UI_SOURCES} ${DES_UI_ASSETS})
target_include_directories(des_ui PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${stb_SOURCE_DIR})
target_compile_definitions(des_ui PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
# Developer builds (docs/55; the option is in port/CMakeLists.txt): the settings window's Developer tab. Off when the
# UI is configured on its own without the option.
if(DES_DEV)
  target_compile_definitions(des_ui PRIVATE DES_DEV=1)
else()
  target_compile_definitions(des_ui PRIVATE DES_DEV=0)
endif()
if(MSVC)
  target_compile_options(des_ui PRIVATE /O2 /EHsc ${DES_DEBUG_FLAGS} /W3 /wd4100)   # DES_DEBUG_FLAGS: port/CMakeLists.txt
else()
  target_compile_options(des_ui PRIVATE -O2)
endif()
# PRIVATE: nothing of RmlUi's (include paths, definitions) reaches des_boot's
# compile lines, only its link line.
if(WIN32)
  target_link_libraries(des_ui PRIVATE des_rmlui_backend RmlUi::RmlUi d3d11 d3d12 d3dcompiler dxgi dxguid shell32
    ole32 user32 dbghelp uuid wevtapi winhttp setupapi)   # (dbghelp: the crash reports' minidump and stack; wevtapi:
                                                          # driver resets; winhttp: the first Python with no curl,
                                                          # setup_http.cpp; setupapi: the GPU's PCI Express link)
else()
  # vkd3d as port/CMakeLists.txt found it (D3D12 and D3DCompile). SDL3's headers: the window, the keys and the mouse
  # are the runtime's SDL3, which the runtime's build names (ps3recomp_runtime_deps.cmake) and des_boot links with it.
  # A UI built alone (CI's, no runtime beside it) takes the headers of the same pinned release, nothing built.
  if(NOT TARGET PkgConfig::DES_VKD3D_LIBS)
    message(FATAL_ERROR "The wrapper UI off Windows draws through vkd3d: configure with DES_VKD3D=ON (or DES_UI=OFF)")
  endif()
  if(PS3RECOMP_SDL3_INCLUDE_DIRS)
    target_include_directories(des_ui PRIVATE ${PS3RECOMP_SDL3_INCLUDE_DIRS})
  else()
    if(NOT EXISTS "${PS3RECOMP_DIR}/cmake/sdl3_dep.cmake")
      message(FATAL_ERROR "The wrapper UI off Windows needs SDL3's headers: a runtime build, or PS3RECOMP_DIR with "
                          "cmake/sdl3_dep.cmake (now '${PS3RECOMP_DIR}')")
    endif()
    include("${PS3RECOMP_DIR}/cmake/sdl3_dep.cmake")
    # (Headers only: SDL is configured but never built here, so it needs no window back end's headers, which SDL's
    # configure otherwise demands.)
    set(SDL_UNIX_CONSOLE_BUILD ON)
    ps3recomp_sdl3_available()
    target_link_libraries(des_ui PRIVATE $<BUILD_INTERFACE:SDL3::Headers>)
  endif()
  # (CMAKE_DL_LIBS: dlopen, for the folder dialog's libdbus, loaded only when the dialog opens; folder_picker.cpp)
  target_link_libraries(des_ui PRIVATE RmlUi::RmlUi PkgConfig::DES_VKD3D_LIBS ${CMAKE_DL_LIBS})
endif()

# The version shown on the start screen, in About, in reports and in the log: des_version.h, written on every build
# by cmake/DesVersion.cmake (port/CMakeLists.txt includes it; a build of the UI alone includes it here).
if(NOT TARGET des_version)
  include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/DesVersion.cmake)
endif()
target_include_directories(des_ui PRIVATE ${DES_VERSION_DIR})
add_dependencies(des_ui des_version)

# settings.cpp on its own, its report and privacy calls stubbed (tests/settings_check.cpp): settings.ini files and
# environments in, the environment the runtime reads out, the Renderer row's included (docs/120). With the same
# definitions as des_ui's settings.cpp (DES_RSX_VULKAN among them: port/CMakeLists.txt). Built on demand:
#   cmake --build <build> --target settings_check, then settings_check --row (or --no-row, without the row)
add_executable(settings_check EXCLUDE_FROM_ALL tests/settings_check.cpp src/settings.cpp src/mods.cpp)
target_include_directories(settings_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${DES_VERSION_DIR})
target_compile_definitions(settings_check PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
if(DES_DEV)
  target_compile_definitions(settings_check PRIVATE DES_DEV=1)
else()
  target_compile_definitions(settings_check PRIVATE DES_DEV=0)
endif()
if(MSVC)
  target_compile_options(settings_check PRIVATE /EHsc /W3 /wd4100)
endif()
if(WIN32)
  target_link_libraries(settings_check PRIVATE dxgi)   # (settings.cpp's renderer_gpu)
endif()
add_dependencies(settings_check des_version)

# Settings' rows on their own (tests/settings_catalog_check.cpp, which makes up settings_hooks.hpp): every row's setting
# in settings.cpp, sound choices and numbers, help written as the settings window expects, each disc's Language row.
# With the same definitions as des_ui's settings files (DES_RSX_VULKAN among them: port/CMakeLists.txt). The same on
# every platform; CI runs it on both (host.yml). Built on demand:
#   cmake --build <build> --target settings_catalog_check, then settings_catalog_check
add_executable(settings_catalog_check EXCLUDE_FROM_ALL tests/settings_catalog_check.cpp src/settings_catalog.cpp
  src/settings_values.cpp src/settings.cpp src/mods.cpp)
target_include_directories(settings_catalog_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${DES_VERSION_DIR})
target_compile_definitions(settings_catalog_check PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
if(DES_DEV)
  target_compile_definitions(settings_catalog_check PRIVATE DES_DEV=1)
else()
  target_compile_definitions(settings_catalog_check PRIVATE DES_DEV=0)
endif()
if(MSVC)
  target_compile_options(settings_catalog_check PRIVATE /EHsc /W3 /wd4100)
endif()
if(WIN32)
  target_link_libraries(settings_catalog_check PRIVATE dxgi)   # (settings.cpp's renderer_gpu)
endif()
add_dependencies(settings_catalog_check des_version)

# The menus' looks on their own (tests/ui_style_check.cpp): RmlUi with the embedded files, no window or renderer. Every
# look in looks.hpp has its sheet, each sheet's rules are inside its own @media (theme: ...), RmlUi reads it without a
# warning, and a theme switched on and off changes a style and changes it back. The same on every platform; CI runs it
# on both (host.yml). Built on demand:
#   cmake --build <build> --target ui_style_check, then ui_style_check
add_executable(ui_style_check EXCLUDE_FROM_ALL tests/ui_style_check.cpp ${DES_UI_ASSETS})
target_include_directories(ui_style_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(ui_style_check PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
if(DES_DEV)
  target_compile_definitions(ui_style_check PRIVATE DES_DEV=1)
else()
  target_compile_definitions(ui_style_check PRIVATE DES_DEV=0)
endif()
if(MSVC)
  target_compile_options(ui_style_check PRIVATE /EHsc /W3)
endif()
target_link_libraries(ui_style_check PRIVATE RmlUi::RmlUi)

# setup_folder.cpp and save_copy.cpp on their own (tests/setup_folder_check.cpp): where setup builds, the usual
# folder or the one chosen on setup's screens (docs/88 section 3), and an earlier folder's saves copied, in made-up
# folders under the system's temporary folder. The same on every platform; CI runs it on both (host.yml). Built on
# demand:
#   cmake --build <build> --target setup_folder_check, then setup_folder_check
add_executable(setup_folder_check EXCLUDE_FROM_ALL tests/setup_folder_check.cpp src/setup_folder.cpp
  src/save_copy.cpp src/sha256.cpp)
target_include_directories(setup_folder_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(setup_folder_check PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
if(MSVC)
  target_compile_options(setup_folder_check PRIVATE /EHsc /W3 /wd4100)
endif()

# The menus' keys on their own (tests/input_map_check.cpp): a pad's buttons as keys with the menu chord's rule, each
# key's command in the F1 menu, what is held, the prompts' buttons for each device, and Change Buttons' Gamepad rows as
# PAD_MAP's text. RmlUi's key names only (a header). The same on every platform; CI runs it on both (host.yml). Built
# on demand:
#   cmake --build <build> --target input_map_check, then input_map_check
add_executable(input_map_check EXCLUDE_FROM_ALL tests/input_map_check.cpp src/menu_pad.cpp src/menu_input.cpp
  src/prompts.cpp src/button_pages.cpp src/pad_rows.cpp)
target_include_directories(input_map_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${rmlui_SOURCE_DIR}/Include
  ${stb_SOURCE_DIR})
target_compile_definitions(input_map_check PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
if(MSVC)
  target_compile_options(input_map_check PRIVATE /EHsc /W3 /wd4100)
endif()

# mods.cpp on its own (tests/mods_check.cpp): mod folders found, read and sorted into look and gameplay mods, and
# PS3_VFS_MODS from the mods that are on (docs/143), in made-up folders under the system's temporary folder. The same
# on every platform; CI runs it on both (host.yml). Built on demand:
#   cmake --build <build> --target mods_check, then mods_check
add_executable(mods_check EXCLUDE_FROM_ALL tests/mods_check.cpp src/mods.cpp)
target_include_directories(mods_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(mods_check PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
if(MSVC)
  target_compile_options(mods_check PRIVATE /EHsc /W3)
endif()

# The button page on its own (tests/button_icons_check.cpp): the font files read, a page decoded and drawn again for
# each device, from made-up files (no game data). With --ccm and --tpf, the player's own, and --png <folder> writes the
# pages drawn, to look at (docs/138). Built on demand:
#   cmake --build <build> --target button_icons_check, then button_icons_check
add_executable(button_icons_check EXCLUDE_FROM_ALL tests/button_icons_check.cpp src/button_pages.cpp)
target_include_directories(button_icons_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${stb_SOURCE_DIR})
target_compile_definitions(button_icons_check PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX
  DES_UI_FONT="${CMAKE_CURRENT_SOURCE_DIR}/res/fonts/AlegreyaSans-Bold.ttf")
if(MSVC)
  target_compile_options(button_icons_check PRIVATE /EHsc /W3)
endif()

# text_fonts.hpp's test on its own (tests/text_fonts_check.cpp): which of the system's fonts a string needs, for its
# Japanese, Chinese or Korean characters (docs/149), from made-up strings; no font is read. Built on demand:
#   cmake --build <build> --target text_fonts_check, then text_fonts_check
add_executable(text_fonts_check EXCLUDE_FROM_ALL tests/text_fonts_check.cpp)
target_include_directories(text_fonts_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
if(MSVC)
  target_compile_options(text_fonts_check PRIVATE /EHsc /W3)
endif()

# discord_ipc.cpp on its own (tests/discord_check.cpp): the Discord status's frames and JSON, and a whole exchange with
# a stand-in for Discord on a pipe or socket of the test's own (docs/137). The same on every platform; CI runs it on
# both (host.yml). Built on demand:
#   cmake --build <build> --target discord_check, then discord_check
add_executable(discord_check EXCLUDE_FROM_ALL tests/discord_check.cpp src/discord_ipc.cpp)
target_include_directories(discord_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(discord_check PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
if(MSVC)
  target_compile_options(discord_check PRIVATE /EHsc /W3)
elseif(NOT WIN32)
  find_package(Threads REQUIRED)
  target_link_libraries(discord_check PRIVATE Threads::Threads)
endif()

# report_addresses.cpp on its own (tests/report_addresses_check.cpp): which numbers in a report's text are hidden as
# internet addresses, and which stay (home addresses, version numbers). CI runs it on both (host.yml). Built on
# demand:
#   cmake --build <build> --target report_addresses_check, then report_addresses_check
add_executable(report_addresses_check EXCLUDE_FROM_ALL tests/report_addresses_check.cpp src/report_addresses.cpp)
target_include_directories(report_addresses_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
if(MSVC)
  target_compile_options(report_addresses_check PRIVATE /EHsc /W3)
endif()

# log_runs.hpp on its own (tests/log_runs_check.cpp): the runs the logs keep (des.log and coop.log, and the four before
# each). CI runs it on both (host.yml). Built on demand:
#   cmake --build <build> --target log_runs_check, then log_runs_check
add_executable(log_runs_check EXCLUDE_FROM_ALL tests/log_runs_check.cpp)
target_include_directories(log_runs_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
if(MSVC)
  target_compile_options(log_runs_check PRIVATE /EHsc /W3)
endif()

# shader_cache.cpp on its own (tests/shader_cache_check.cpp): the menus' shaders kept compiled between starts. A stored
# one comes back only for exactly the input it was made from, and only whole: every byte of a file changed in turn,
# cut short at every length, another input, another format, the other kind of bytecode. CI runs it on both (host.yml).
# Built on demand:
#   cmake --build <build> --target shader_cache_check, then shader_cache_check
add_executable(shader_cache_check EXCLUDE_FROM_ALL tests/shader_cache_check.cpp src/shader_cache.cpp)
target_include_directories(shader_cache_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
if(MSVC)
  target_compile_options(shader_cache_check PRIVATE /EHsc /W3)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_options(shader_cache_check PRIVATE "-Wl,--build-id=sha1")  # (as des_host: the build is known by it)
endif()

# The F1 menu's rules on their own (tests/menu_logic_check.cpp): menu_model.cpp and settings_model.cpp with no RmlUi
# and no settings: where Back goes, what closing the menu does while settings wait for a restart, and what Settings
# has open and prompts. The same on every platform; CI runs it on both (host.yml). Built on demand:
#   cmake --build <build> --target menu_logic_check, then menu_logic_check
add_executable(menu_logic_check EXCLUDE_FROM_ALL tests/menu_logic_check.cpp src/menu_model.cpp src/settings_model.cpp)
target_include_directories(menu_logic_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
if(MSVC)
  target_compile_options(menu_logic_check PRIVATE /EHsc /W3)
endif()

# The first Python's fetch on its own (tests/setup_fetch_check.cpp), the data folder and the free space stubbed: curl
# and tar against the host's own download and unzip (what Wine and CrossOver get), a stopped download carried on, and
# the zip reader's refusals. It downloads python.org's embeddable zip three times. Windows only. Built on demand:
#   cmake --build <build> --target setup_fetch_check, then setup_fetch_check <a new folder>
if(WIN32)
  add_executable(setup_fetch_check EXCLUDE_FROM_ALL tests/setup_fetch_check.cpp src/setup_python.cpp
    src/setup_http.cpp src/setup_unzip.cpp src/child_process.cpp src/sha256.cpp src/stb_impl.cpp)
  target_include_directories(setup_fetch_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${stb_SOURCE_DIR})
  target_compile_definitions(setup_fetch_check PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
  if(MSVC)
    target_compile_options(setup_fetch_check PRIVATE /EHsc /W3 /wd4100)
  endif()
  target_link_libraries(setup_fetch_check PRIVATE winhttp)
endif()

# The same on a Linux with no curl (tests/setup_fetch_linux_check.cpp, issue #521): wget, the system's own python3, a
# resume with each, a wget that refuses its options, and none of them. PATH is a folder of links to the tools each
# case may use. It downloads the pinned archive (35 MB) five times. Linux only. Built on demand:
#   cmake --build <build> --target setup_fetch_linux_check, then setup_fetch_linux_check <a new folder>
if(UNIX AND NOT APPLE)
  add_executable(setup_fetch_linux_check EXCLUDE_FROM_ALL tests/setup_fetch_linux_check.cpp src/setup_python.cpp
    src/setup_http.cpp src/setup_unzip.cpp src/child_process.cpp src/sha256.cpp src/stb_impl.cpp)
  target_include_directories(setup_fetch_linux_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${stb_SOURCE_DIR})
  find_package(Threads REQUIRED)
  target_link_libraries(setup_fetch_linux_check PRIVATE Threads::Threads)
endif()
