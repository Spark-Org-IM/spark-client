cmake_minimum_required(VERSION 3.20)

# CI stamps these from the release tag. The project version must stay numeric;
# the package version may use Debian's tilde suffix (`0.2.0~beta.1`).
set(SPARK_TUI_PROJECT_VERSION "0.2.0" CACHE STRING "spark-tui version")
set(SPARK_TUI_PACKAGE_VERSION "${SPARK_TUI_PROJECT_VERSION}"
    CACHE STRING "spark-tui package version")
project(spark-tui VERSION ${SPARK_TUI_PROJECT_VERSION} LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Language / build defaults
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include(GNUInstallDirs)
include(FetchContent)

option(SPARK_TUI_BUILD_TESTS       "Build the unit tests"          ON)
option(SPARK_TUI_USE_SYSTEM_FTXUI  "Require a system-installed FTXUI (no fetch)" OFF)
option(SPARK_TUI_USE_SYSTEM_JSON   "Use system nlohmann_json"      OFF)
option(SPARK_TUI_USE_SYSTEM_HTTPLIB "Use system cpp-httplib"       OFF)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED COMPONENTS SSL Crypto)

if(MSVC)
  set(SPARK_TUI_WARNING_FLAGS /W4 /permissive-)
else()
  set(SPARK_TUI_WARNING_FLAGS -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# FTXUI (TUI foundation): prefer a system package, otherwise fetch a pinned
# release. Debian/Ubuntu ship libftxui-dev (5.0.0 in trixie), which is the
# preferred choice for a .deb.
# ---------------------------------------------------------------------------
set(SPARK_TUI_FTXUI_FROM_SYSTEM OFF)
if(SPARK_TUI_USE_SYSTEM_FTXUI)
  find_package(ftxui REQUIRED)
  set(SPARK_TUI_FTXUI_FROM_SYSTEM ON)
else()
  find_package(ftxui QUIET)
  if(ftxui_FOUND)
    message(STATUS "Using system FTXUI")
    set(SPARK_TUI_FTXUI_FROM_SYSTEM ON)
  else()
    message(STATUS "System FTXUI not found; fetching v6.1.9")
    set(FTXUI_BUILD_DOCS       OFF CACHE BOOL "" FORCE)
    set(FTXUI_BUILD_EXAMPLES   OFF CACHE BOOL "" FORCE)
    set(FTXUI_BUILD_TESTS      OFF CACHE BOOL "" FORCE)
    set(FTXUI_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
    set(FTXUI_ENABLE_INSTALL   OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(ftxui
      GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
      GIT_TAG        v6.1.9
      GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(ftxui)
  endif()
endif()

# For a non-standard / extracted prefix where headers are not in the
# compiler's default search path.
if(SPARK_TUI_FTXUI_FROM_SYSTEM AND SPARK_TUI_FTXUI_INCLUDE_DIR)
  target_include_directories(ftxui::screen INTERFACE
    "${SPARK_TUI_FTXUI_INCLUDE_DIR}")
endif()

# ---------------------------------------------------------------------------
# nlohmann/json (header-only, vendored by default)
# ---------------------------------------------------------------------------
if(SPARK_TUI_USE_SYSTEM_JSON)
  find_package(nlohmann_json REQUIRED)
  add_library(spark_json INTERFACE)
  target_link_libraries(spark_json INTERFACE nlohmann_json::nlohmann_json)
else()
  add_library(spark_json INTERFACE)
  target_include_directories(spark_json INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
endif()

# ---------------------------------------------------------------------------
# toml++ (header-only, vendored)
# ---------------------------------------------------------------------------
add_library(spark_toml INTERFACE)
target_include_directories(spark_toml INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

# ---------------------------------------------------------------------------
# cpp-httplib (header-only, vendored by default) + OpenSSL for HTTPS
# ---------------------------------------------------------------------------
if(SPARK_TUI_USE_SYSTEM_HTTPLIB)
  find_package(httplib REQUIRED)
  add_library(spark_httplib INTERFACE)
  target_link_libraries(spark_httplib INTERFACE
    httplib::httplib OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
  target_compile_definitions(spark_httplib INTERFACE CPPHTTPLIB_OPENSSL_SUPPORT)
else()
  add_library(spark_httplib INTERFACE)
  target_include_directories(spark_httplib INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
  target_compile_definitions(spark_httplib INTERFACE CPPHTTPLIB_OPENSSL_SUPPORT)
  target_link_libraries(spark_httplib INTERFACE
    OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
endif()

# ---------------------------------------------------------------------------
# Core library (config, HTTP, API models) — no FTXUI dependency
# ---------------------------------------------------------------------------
add_library(spark_core STATIC
  src/util.cpp
  src/i18n.cpp
  src/config.cpp
  src/service_source.cpp
  src/api/client.cpp
  src/api/models.cpp
  src/forum/client.cpp
  src/forum/models.cpp
  src/forum/render.cpp
  src/ui/render_detail.cpp
)
target_include_directories(spark_core PUBLIC src)
target_link_libraries(spark_core PUBLIC spark_json spark_httplib PRIVATE spark_toml)
target_compile_definitions(spark_core PRIVATE
  SPARK_TUI_VERSION="${PROJECT_VERSION}")
target_compile_options(spark_core PRIVATE ${SPARK_TUI_WARNING_FLAGS})
if(WIN32)
  # OpenUrl uses ShellExecuteA.
  target_link_libraries(spark_core PUBLIC shell32)
endif()

# ---------------------------------------------------------------------------
# Executable
# ---------------------------------------------------------------------------
add_executable(spark-tui
  src/main.cpp
  src/options.cpp
  src/ui/worker.cpp
  src/ui/app.cpp
)
target_link_libraries(spark-tui PRIVATE
  spark_core ftxui::screen ftxui::dom ftxui::component)
target_compile_definitions(spark-tui PRIVATE
  SPARK_TUI_VERSION="${PROJECT_VERSION}")
target_compile_options(spark-tui PRIVATE ${SPARK_TUI_WARNING_FLAGS})

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(TARGETS spark-tui RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES packaging/spark-tui.desktop
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
install(FILES packaging/spark-tui.svg
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps")
install(DIRECTORY docs/api-v2
        DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/spark-tui")
install(FILES README.md LICENSE
        DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/spark-tui")
install(FILES docs/config.example.toml
        DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/spark-tui")

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(SPARK_TUI_BUILD_TESTS)
  enable_testing()
  add_executable(spark-tui-tests tests/test_core.cpp)
  target_link_libraries(spark-tui-tests PRIVATE spark_core)
  add_test(NAME spark-tui-tests COMMAND spark-tui-tests)
endif()

# ---------------------------------------------------------------------------
# CPack (DEB)
# ---------------------------------------------------------------------------
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
set(CPACK_PACKAGE_NAME            "spark-tui")
set(CPACK_PACKAGE_VENDOR          "Spark ORG")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Terminal client for Spark ORG (news / contact / platform / forum)")
set(CPACK_PACKAGE_VERSION         "${SPARK_TUI_PACKAGE_VERSION}")
set(CPACK_PACKAGE_CONTACT         "Jianyin Li <jianyin@sparkorg.dpdns.org>")
set(CPACK_RESOURCE_FILE_LICENSE   "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_DEBIAN_PACKAGE_SECTION  "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
option(SPARK_TUI_DEB_SHLIBDEPS
       "Let dpkg-shlibdeps compute the runtime dependencies" ON)
if(SPARK_TUI_DEB_SHLIBDEPS)
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
  # Let dpkg-shlibdeps compute all runtime dependencies (OpenSSL, FTXUI, ...).
  set(CPACK_DEBIAN_PACKAGE_DEPENDS "")
else()
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
  set(_spark_deb_depends "libc6, libstdc++6, libssl3t64 | libssl3")
  if(SPARK_TUI_FTXUI_FROM_SYSTEM)
    string(APPEND _spark_deb_depends
      ", libftxui-screen5.0.0, libftxui-dom5.0.0, libftxui-component5.0.0")
  endif()
  set(CPACK_DEBIAN_PACKAGE_DEPENDS "${_spark_deb_depends}")
endif()
set(CPACK_DEBIAN_FILE_NAME        DEB-DEFAULT)
set(CPACK_GENERATOR               "DEB")
include(CPack)
endif()
