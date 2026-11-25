# The wrapper UI (port/ui) built on its own, for CI (.github/workflows/ci.yml). des_boot needs the lift,
# which CI never has; the UI library does not. Same standards as port/CMakeLists.txt.
cmake_minimum_required(VERSION 3.20)
project(des_ui_ci LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Off Windows the UI draws through D3D12 on vkd3d in the runtime's SDL3 window (docs/76): vkd3d found here the way
# port/CMakeLists.txt finds it for des_boot, and SDL3's headers from the pinned ps3recomp's cmake/sdl3_dep.cmake
# (PS3RECOMP_DIR, which ci.yml checks out first).
if(NOT WIN32)
  set(PS3RECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../ps3recomp" CACHE PATH "the pinned ps3recomp")
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(DES_VKD3D_LIBS REQUIRED IMPORTED_TARGET libvkd3d libvkd3d-utils)
endif()

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../../../port/ui" des_ui)
