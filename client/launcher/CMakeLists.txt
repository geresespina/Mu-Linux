# MEAMU Launcher — standalone Win32 auto-updater for the desktop client.
# Built on its own (not part of the client's CMake tree) so it stays a tiny,
# dependency-free exe: statically linked CRT, WinHTTP + vendored miniz only.
cmake_minimum_required(VERSION 3.20)
project(MEAMULauncher C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(MEAMU-Launcher WIN32
  src/main.cpp
  src/miniz.c
)

target_compile_definitions(MEAMU-Launcher PRIVATE
  UNICODE _UNICODE
  WIN32_LEAN_AND_MEAN NOMINMAX
  _CRT_SECURE_NO_WARNINGS
)

# The launcher must run on a bare Windows install, before anything else is
# set up: link the CRT statically so no VC++ redistributable is needed.
set_property(TARGET MEAMU-Launcher PROPERTY
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

target_link_libraries(MEAMU-Launcher PRIVATE winhttp comctl32)

if(MSVC)
  target_compile_options(MEAMU-Launcher PRIVATE /W4)
endif()

# Stable output location for the CI artifact upload, every configuration.
set_target_properties(MEAMU-Launcher PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/dist"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/dist"
)
