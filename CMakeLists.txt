cmake_minimum_required(VERSION 3.21)
project(vimbrowser LANGUAGES C CXX)
if(APPLE)
  enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CEF_ROOT "" CACHE PATH "Source-built vimbrowser CEF distribution root")
if(NOT CEF_ROOT)
  message(FATAL_ERROR
    "CEF_ROOT must point to a distribution built from backend/chromium")
endif()
set(VIMBROWSER_DEFAULT_PROFILE_DIR "" CACHE PATH
    "Persistent profile used when --profile-dir is not supplied")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CEF_ROOT}/cmake")

find_package(CEF REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(MUSESCORE_NATIVE REQUIRED IMPORTED_TARGET
  libcurl
  cairo
  librsvg-2.0
)

if(OS_MAC)
  # std::to_chars for floating point (browser_window_internal.cc) needs the
  # macOS 13.3+ libc++ availability; CEF's default deployment target is 12.0.
  list(TRANSFORM CEF_COMPILER_FLAGS REPLACE "^-mmacosx-version-min=.*$" "-mmacosx-version-min=13.3")
  set(CMAKE_OSX_DEPLOYMENT_TARGET "13.3")
endif()

add_subdirectory(${CEF_LIBCEF_DLL_WRAPPER_PATH} libcef_dll_wrapper)

if(NOT OS_MAC)
  # On macOS libcef is never linked directly; the framework is loaded at
  # runtime by cef_library_loader from the app bundle.
  ADD_LOGICAL_TARGET(libcef_lib "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")
endif()
SET_CEF_TARGET_OUT_DIR()

set(VIMBROWSER_COMMON_SRCS
  src/a26_keyboard.cc
  src/a26_keyboard.h
  src/app.cc
  src/app.h
  src/browser_font_settings.h
  src/browser_client.cc
  src/browser_client.h
  src/browser_window_autocomplete.cc
  src/browser_window_commands.cc
  src/browser_window_context_menu.cc
  src/browser_window_sidebar.cc
  src/browser_window.cc
  src/browser_window.h
  src/browser_window_internal.cc
  src/browser_window_internal.h
  src/browser_window_ipc.cc
  src/browser_window_tabs.cc
  src/config.cc
  src/config.h
  src/ipc_server.cc
  src/ipc_server.h
  src/shortcuts.c
  src/shortcuts.h
  src/tab.h
  src/theme.h
  src/vim.cc
  src/vim.h
)

list(APPEND VIMBROWSER_COMMON_SRCS
  src/musescore_downloader.cc
  src/musescore_downloader.h
)

set(VIMBROWSER_SRCS ${VIMBROWSER_COMMON_SRCS} src/main.cc)
if(OS_MAC)
  list(APPEND VIMBROWSER_SRCS src/mac/main_mac.mm)
endif()

macro(VIMBROWSER_TARGET_COMMON target)
  SET_EXECUTABLE_TARGET_PROPERTIES(${target})
  add_dependencies(${target} libcef_dll_wrapper)
  target_include_directories(${target} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CEF_INCLUDE_PATH}"
  )
  target_precompile_headers(${target} PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/src/browser_pch.h>"
  )
  if(VIMBROWSER_DEFAULT_PROFILE_DIR)
    target_compile_definitions(${target} PRIVATE
      VIMBROWSER_DEFAULT_PROFILE_DIR="${VIMBROWSER_DEFAULT_PROFILE_DIR}"
    )
  endif()
endmacro()

if(OS_MAC)
  set(VIMBROWSER_APP "${CEF_TARGET_OUT_DIR}/vimbrowser.app")
  set(VIMBROWSER_ICON "${CMAKE_CURRENT_SOURCE_DIR}/src/mac/vimbrowser.icns")
  set(VIMBROWSER_CEF_FRAMEWORK_BINARY
      "${CEF_BINARY_DIR_RELEASE}/Chromium Embedded Framework.framework/Chromium Embedded Framework")
  execute_process(
    COMMAND /usr/bin/nm -gUj "${VIMBROWSER_CEF_FRAMEWORK_BINARY}"
    RESULT_VARIABLE VIMBROWSER_CEF_NM_RESULT
    OUTPUT_VARIABLE VIMBROWSER_CEF_EXPORTED_SYMBOLS
    ERROR_QUIET
  )
  if(NOT VIMBROWSER_CEF_NM_RESULT EQUAL 0 OR
     NOT VIMBROWSER_CEF_EXPORTED_SYMBOLS MATCHES
         "_cef_browser_host_vimbrowser_send_browser_command_key_event")
    message(FATAL_ERROR
      "CEF_ROOT must point to a source-built vimbrowser CEF distribution; stock CEF fallbacks have been removed")
  endif()
  set_source_files_properties(${VIMBROWSER_ICON} PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources"
  )

  # Referenced from Info.plist.in.
  set(EXECUTABLE_NAME "vimbrowser")
  set(PRODUCT_NAME "vimbrowser")

  add_executable(vimbrowser MACOSX_BUNDLE ${VIMBROWSER_SRCS} ${VIMBROWSER_ICON})
  VIMBROWSER_TARGET_COMMON(vimbrowser)
  # CEF is loaded from the bundle instead of participating in the normal macOS
  # link line. Still make its binary a link dependency so an incremental
  # backend rebuild relinks/finalizes the app and reruns the framework copy and
  # signing steps below.
  set_property(TARGET vimbrowser APPEND PROPERTY LINK_DEPENDS
    "${VIMBROWSER_CEF_FRAMEWORK_BINARY}"
  )
  target_link_libraries(vimbrowser PRIVATE
    libcef_dll_wrapper
    PkgConfig::MUSESCORE_NATIVE
    ${CEF_STANDARD_LIBS}
  )
  set_target_properties(vimbrowser PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/src/mac/Info.plist.in"
  )

  # CEF's COPY_MAC_FRAMEWORK macro recreates framework symlinks with `ln -sf`.
  # On a repeat build, BSD ln follows the existing directory symlink and
  # creates self-referential entries inside Versions/A. Remove only the copied
  # framework first so bundle assembly is idempotent.
  set(VIMBROWSER_CEF_FRAMEWORK
      "${VIMBROWSER_APP}/Contents/Frameworks/Chromium Embedded Framework.framework")
  add_custom_command(
    TARGET vimbrowser
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${VIMBROWSER_CEF_FRAMEWORK}"
    VERBATIM
  )
  COPY_MAC_FRAMEWORK("vimbrowser" "${CEF_BINARY_DIR}" "${VIMBROWSER_APP}")

  # Helper app bundles: subprocesses (renderer/GPU/plugin/alerts) run through
  # these instead of the main executable. The renderer needs the same
  # CefRenderProcessHandler as the browser process, so helpers link the full
  # shell sources with their own entry point.
  foreach(_suffix_list ${CEF_HELPER_APP_SUFFIXES})
    string(REPLACE ":" ";" _suffix_list ${_suffix_list})
    list(GET _suffix_list 0 _name_suffix)
    list(GET _suffix_list 1 _target_suffix)
    list(GET _suffix_list 2 _plist_suffix)

    set(_helper_target "vimbrowser_helper${_target_suffix}")
    set(_helper_output_name "vimbrowser Helper${_name_suffix}")
    set(_helper_output_dir "${CEF_TARGET_OUT_DIR}/.helpers")

    # Older builds emitted helper bundles beside the main app, so Spotlight
    # presented GPU/Renderer/etc. as applications the user could launch.
    # Remove those legacy outputs and keep build-only helper bundles under a
    # hidden staging directory. Installed helpers still live in the standard
    # Contents/Frameworks location inside vimbrowser.app.
    file(REMOVE_RECURSE "${CEF_TARGET_OUT_DIR}/${_helper_output_name}.app")

    # Substitute plist variables manually: MACOSX_BUNDLE_INFO_PLIST's own
    # configure step uses global variables and would reuse one helper's values
    # for all of them.
    set(_helper_info_plist "${CMAKE_CURRENT_BINARY_DIR}/helper-Info${_target_suffix}.plist")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/mac/helper-Info.plist.in" _plist_contents)
    string(REPLACE "\${EXECUTABLE_NAME}" "${_helper_output_name}" _plist_contents ${_plist_contents})
    string(REPLACE "\${PRODUCT_NAME}" "${_helper_output_name}" _plist_contents ${_plist_contents})
    string(REPLACE "\${BUNDLE_ID_SUFFIX}" "${_plist_suffix}" _plist_contents ${_plist_contents})
    file(WRITE ${_helper_info_plist} ${_plist_contents})

    add_executable(${_helper_target} MACOSX_BUNDLE
      ${VIMBROWSER_COMMON_SRCS}
      src/mac/process_helper_mac.cc
    )
    VIMBROWSER_TARGET_COMMON(${_helper_target})
    target_link_libraries(${_helper_target} PRIVATE
      libcef_dll_wrapper
      PkgConfig::MUSESCORE_NATIVE
      ${CEF_STANDARD_LIBS}
    )
    set_target_properties(${_helper_target} PROPERTIES
      MACOSX_BUNDLE_INFO_PLIST ${_helper_info_plist}
      OUTPUT_NAME ${_helper_output_name}
      RUNTIME_OUTPUT_DIRECTORY ${_helper_output_dir}
    )

    add_dependencies(vimbrowser "${_helper_target}")

    add_custom_command(
      TARGET vimbrowser
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              "${_helper_output_dir}/${_helper_output_name}.app"
              "${VIMBROWSER_APP}/Contents/Frameworks/${_helper_output_name}.app"
      VERBATIM
    )
  endforeach()

  # CMake/linker ad-hoc signs the main executable before the framework and
  # helper bundles are copied. Sign nested code inside-out, keeping CEF's large
  # framework ad-hoc signed for dyld while the outer app retains the stable
  # developer identity used by Keychain.
  set(VIMBROWSER_CODESIGN_IDENTITY "-" CACHE STRING
      "macOS code-signing identity; '-' selects an ad-hoc signature")
  add_custom_command(
    TARGET vimbrowser
    POST_BUILD
    COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/scripts/codesign-mac-app.sh"
            "${VIMBROWSER_APP}" "${VIMBROWSER_CODESIGN_IDENTITY}"
    VERBATIM
  )
else()
  add_executable(vimbrowser ${VIMBROWSER_SRCS})
  VIMBROWSER_TARGET_COMMON(vimbrowser)
  target_link_libraries(vimbrowser PRIVATE
    libcef_lib
    libcef_dll_wrapper
    PkgConfig::MUSESCORE_NATIVE
    ${CEF_STANDARD_LIBS}
  )

  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(vimbrowser PRIVATE Xi)
  endif()

  COPY_FILES(vimbrowser "${CEF_BINARY_FILES}" "${CEF_BINARY_DIR}" "${CEF_TARGET_OUT_DIR}")
  COPY_FILES(vimbrowser "${CEF_RESOURCE_FILES}" "${CEF_RESOURCE_DIR}" "${CEF_TARGET_OUT_DIR}")
endif()

add_executable(vimbrowser-ipc
  src/ipc_client.c
)

target_compile_definitions(vimbrowser-ipc PRIVATE
  VIMBROWSER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

install(TARGETS vimbrowser RUNTIME DESTINATION bin BUNDLE DESTINATION .)
install(TARGETS vimbrowser-ipc RUNTIME DESTINATION bin)
