cmake_minimum_required(VERSION 4.0)
project(reasolotus VERSION 0.2.0 LANGUAGES C CXX)

set(
  FETCHCONTENT_BASE_DIR
  "${CMAKE_SOURCE_DIR}/.deps"
  CACHE PATH
  "Directory used by FetchContent for third-party sources"
  FORCE
)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET "hidden")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

add_library(${PROJECT_NAME} SHARED)
include(reaper-plugin)
add_subdirectory(src)
