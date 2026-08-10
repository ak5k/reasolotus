include_guard(GLOBAL)

if(NOT TARGET ${PROJECT_NAME})
  message(FATAL_ERROR "Include cmake/reaper-plugin.cmake after add_library(${PROJECT_NAME} ...).")
endif()

include(FetchContent)

set(REASOLOTUS_WDL_CACHE_DIR "${FETCHCONTENT_BASE_DIR}/WDL")
set(REASOLOTUS_REAPER_SDK_CACHE_DIR "${FETCHCONTENT_BASE_DIR}/reaper-sdk")

if(EXISTS "${REASOLOTUS_WDL_CACHE_DIR}/WDL")
  set(FETCHCONTENT_SOURCE_DIR_WDL "${REASOLOTUS_WDL_CACHE_DIR}")
endif()

if(EXISTS "${REASOLOTUS_REAPER_SDK_CACHE_DIR}/sdk")
  set(FETCHCONTENT_SOURCE_DIR_REAPER_SDK_SOURCE "${REASOLOTUS_REAPER_SDK_CACHE_DIR}")
endif()

FetchContent_Declare(WDL
    GIT_REPOSITORY https://github.com/justinfrankel/WDL
    GIT_TAG        "origin/main"
    GIT_SHALLOW    TRUE
    SOURCE_DIR     "${REASOLOTUS_WDL_CACHE_DIR}"
    EXCLUDE_FROM_ALL
    )

FetchContent_Declare(reaper_sdk_source
    GIT_REPOSITORY https://github.com/justinfrankel/reaper-sdk
    GIT_TAG        "origin/main"
    GIT_SHALLOW    TRUE
    SOURCE_DIR     "${REASOLOTUS_REAPER_SDK_CACHE_DIR}"
    EXCLUDE_FROM_ALL
    )

FetchContent_MakeAvailable(WDL reaper_sdk_source)

FetchContent_GetProperties(WDL SOURCE_DIR REASOLOTUS_WDL_SOURCE_DIR)
FetchContent_GetProperties(reaper_sdk_source SOURCE_DIR REASOLOTUS_REAPER_SDK_SOURCE_DIR)

if(NOT REASOLOTUS_WDL_SOURCE_DIR OR NOT EXISTS "${REASOLOTUS_WDL_SOURCE_DIR}/WDL")
  message(FATAL_ERROR "WDL source directory was not fetched correctly.")
endif()

if(NOT REASOLOTUS_REAPER_SDK_SOURCE_DIR OR NOT EXISTS "${REASOLOTUS_REAPER_SDK_SOURCE_DIR}/sdk")
  message(FATAL_ERROR "reaper-sdk source directory was not fetched correctly.")
endif()

set(REASOLOTUS_WDL_SOURCE_DIR "${REASOLOTUS_WDL_SOURCE_DIR}" CACHE INTERNAL "")
set(REASOLOTUS_REAPER_SDK_SOURCE_DIR "${REASOLOTUS_REAPER_SDK_SOURCE_DIR}" CACHE INTERNAL "")

execute_process(
    COMMAND ${CMAKE_COMMAND} -E create_symlink
            "${REASOLOTUS_WDL_SOURCE_DIR}/WDL"
            "${REASOLOTUS_REAPER_SDK_SOURCE_DIR}/WDL"
)

if(NOT TARGET reaper-sdk)
  add_library(reaper-sdk IMPORTED INTERFACE)
  target_include_directories(reaper-sdk SYSTEM INTERFACE
    "${REASOLOTUS_REAPER_SDK_SOURCE_DIR}/sdk"
  )
endif()

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

target_link_libraries(${PROJECT_NAME} PRIVATE reaper-sdk)

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 23)

if(WIN32)
  add_compile_definitions(NOMINMAX _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
  target_compile_options(
    ${PROJECT_NAME}
      PRIVATE
      /W3 /WX)
endif()
if(UNIX AND NOT APPLE)
  target_compile_options(${PROJECT_NAME} PRIVATE -fno-unsigned-char)
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

string(TOLOWER "${ARCH_NAME}" ARCH_NAME)
set_target_properties(${PROJECT_NAME} PROPERTIES
  PREFIX ""
  OUTPUT_NAME "reaper_${PROJECT_NAME}-${ARCH_NAME}"
)

set(REAPER_USER_PLUGINS "UserPlugins")
install(TARGETS ${PROJECT_NAME}
  COMPONENT ${PROJECT_NAME}
  RUNTIME DESTINATION "${REAPER_USER_PLUGINS}"
  LIBRARY DESTINATION "${REAPER_USER_PLUGINS}"
)