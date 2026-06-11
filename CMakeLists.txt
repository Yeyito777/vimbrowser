cmake_minimum_required(VERSION 3.21)
project(vimbrowser LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CEF_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cef" CACHE PATH "CEF binary/source distribution root")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CEF_ROOT}/cmake")

find_package(CEF REQUIRED)
add_subdirectory(${CEF_LIBCEF_DLL_WRAPPER_PATH} libcef_dll_wrapper)

ADD_LOGICAL_TARGET(libcef_lib "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")
SET_CEF_TARGET_OUT_DIR()

add_executable(vimbrowser
  src/app.cc
  src/app.h
  src/browser_client.cc
  src/browser_client.h
  src/browser_window_autocomplete.cc
  src/browser_window_commands.cc
  src/browser_window_context_menu.cc
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
  src/main.cc
  src/shortcuts.c
  src/shortcuts.h
  src/tab.h
  src/theme.h
  src/vim.cc
  src/vim.h
)

add_executable(vimbrowser-ipc
  src/ipc_client.c
)

SET_EXECUTABLE_TARGET_PROPERTIES(vimbrowser)

target_include_directories(vimbrowser PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${CEF_INCLUDE_PATH}"
)

target_precompile_headers(vimbrowser PRIVATE
  "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/src/browser_pch.h>"
)

target_link_libraries(vimbrowser PRIVATE
  libcef_lib
  libcef_dll_wrapper
  ${CEF_STANDARD_LIBS}
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_libraries(vimbrowser PRIVATE Xi)
endif()

target_compile_definitions(vimbrowser-ipc PRIVATE
  VIMBROWSER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

add_dependencies(vimbrowser libcef_dll_wrapper)

COPY_FILES(vimbrowser "${CEF_BINARY_FILES}" "${CEF_BINARY_DIR}" "${CEF_TARGET_OUT_DIR}")
COPY_FILES(vimbrowser "${CEF_RESOURCE_FILES}" "${CEF_RESOURCE_DIR}" "${CEF_TARGET_OUT_DIR}")

install(TARGETS vimbrowser RUNTIME DESTINATION bin)
install(TARGETS vimbrowser-ipc RUNTIME DESTINATION bin)
