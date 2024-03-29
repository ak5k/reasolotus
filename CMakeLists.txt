cmake_minimum_required(VERSION 3.15)
project(reasolotus VERSION 0.1.8 LANGUAGES C CXX)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET "hidden")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# path for external 3rd party library dependencies 
include(FetchContent)
set(PROJECT_LIB_DIR ${PROJECT_SOURCE_DIR}/lib)

FetchContent_Declare(GSL
    GIT_REPOSITORY "https://github.com/microsoft/GSL"
    GIT_TAG        "origin/main"
    SOURCE_DIR     "${PROJECT_LIB_DIR}/GSL"
)

FetchContent_Declare(WDL
    GIT_REPOSITORY https://github.com/justinfrankel/WDL
    GIT_TAG        "origin/main" # or specify a tag or branch here
    SOURCE_DIR     "${PROJECT_LIB_DIR}/WDL"
    )

FetchContent_Declare(reaper-sdk
    GIT_REPOSITORY https://github.com/justinfrankel/reaper-sdk
    GIT_TAG        "origin/main" # or specify a tag or branch here
    SOURCE_DIR     "${PROJECT_LIB_DIR}/reaper-sdk"
    )

FetchContent_MakeAvailable(GSL WDL reaper-sdk)

# symlink WDL to reaper-sdk
execute_process(
    COMMAND ${CMAKE_COMMAND} -E create_symlink
            # source
            "${PROJECT_LIB_DIR}/WDL/WDL"
            # target
            "${PROJECT_LIB_DIR}/reaper-sdk/WDL"
)

if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
    CACHE STRING "")
endif()

if(DEFINED ENV{VCPKG_DEFAULT_TRIPLET} AND NOT DEFINED VCPKG_TARGET_TRIPLET)
  set(VCPKG_TARGET_TRIPLET "$ENV{VCPKG_DEFAULT_TRIPLET}" CACHE STRING "")
endif()

FILE(GLOB header_paths 
    ${PROJECT_LIB_DIR}/*/include
    ${PROJECT_LIB_DIR}/*/sdk
    )

add_library(reaper-sdk IMPORTED INTERFACE)
target_include_directories(reaper-sdk INTERFACE ${header_paths})
target_link_libraries(reaper-sdk INTERFACE GSL)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

find_package(WDL REQUIRED)

if(NOT WIN32)
  find_package(SWELL REQUIRED)
endif()

target_link_libraries(reaper-sdk INTERFACE WDL::WDL)

if(SWELL_FOUND)
  target_link_libraries(reaper-sdk INTERFACE SWELL::swell)

  # Transpile the Win32 resource file
  # find_package(PHP REQUIRED)
  # add_custom_command(
  #   OUTPUT  resource.rc_mac_dlg resource.rc_mac_menu
  #   COMMAND ${CMAKE_COMMAND} -E copy
  #     ${CMAKE_CURRENT_SOURCE_DIR}/resource.rc ${CMAKE_CURRENT_BINARY_DIR}
  #   COMMAND ${PHP_EXECUTABLE} ${SWELL_RESGEN} --quiet resource.rc
  #   DEPENDS ${SWELL_RESGEN}
  #   MAIN_DEPENDENCY resource.rc
  # )
endif()

# if(WIN32)
#   add_link_options(
#     # Use relative path to the PDB file to avoid exposing the full path
#     /PDBALTPATH:%_PDB%
#   )
# endif()

add_library(${PROJECT_NAME} SHARED)
add_subdirectory(src)
target_link_libraries(${PROJECT_NAME} PRIVATE reaper-sdk)

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 23)

if(WIN32)
  add_compile_definitions(NOMINMAX _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
  target_compile_options(
    ${PROJECT_NAME} 
      PRIVATE 
      /W3 /WX)
else()
  target_compile_options(
    ${PROJECT_NAME}
      PRIVATE
      -Wall -Wextra -Werror -Wpedantic 
  )
endif()

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  if(WIN32)
    set(USER_CONFIG_DIR "$ENV{APPDATA}")
  elseif(APPLE)
    set(USER_CONFIG_DIR "~/Library/Application Support")
  else()
    set(USER_CONFIG_DIR "~/.config")
  endif()

  set(CMAKE_INSTALL_PREFIX "${USER_CONFIG_DIR}/REAPER" CACHE PATH
    "REAPER resource path where to install ReaPack" FORCE)
endif()

if(CMAKE_OSX_ARCHITECTURES)
  list(JOIN CMAKE_OSX_ARCHITECTURES "-" ARCH_NAME)
elseif(MSVC_CXX_ARCHITECTURE_ID)
  set(ARCH_NAME ${MSVC_CXX_ARCHITECTURE_ID})
else()
  set(ARCH_NAME ${CMAKE_SYSTEM_PROCESSOR})
endif()

STRING(TOLOWER "${ARCH_NAME}" ARCH_NAME)
set_target_properties(${PROJECT_NAME} PROPERTIES
  PREFIX "" # disable the "lib" prefix
  OUTPUT_NAME "reaper_${PROJECT_NAME}-${ARCH_NAME}"
)

set(REAPER_USER_PLUGINS "UserPlugins")
install(TARGETS ${PROJECT_NAME}
  COMPONENT ${PROJECT_NAME}
  RUNTIME DESTINATION "${REAPER_USER_PLUGINS}" # Windows .dll
  LIBRARY DESTINATION "${REAPER_USER_PLUGINS}" # Linux .so/macOS .dylib
)