cmake_minimum_required(VERSION 3.20)

# VERSION is the single source of truth for the release version and follows
# SemVer: MAJOR.MINOR.PATCH, for example `0.3.0`.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" SPARK_TUI_VERSION_LINE
     LIMIT_COUNT 1)
string(STRIP "${SPARK_TUI_VERSION_LINE}" SPARK_TUI_VERSION_LINE)
if(NOT SPARK_TUI_VERSION_LINE MATCHES "^([0-9]+\.[0-9]+\.[0-9]+)$")
  message(FATAL_ERROR "VERSION must be a SemVer, e.g. 0.3.0")
endif()
set(SPARK_TUI_PROJECT_VERSION "${SPARK_TUI_VERSION_LINE}"
    CACHE STRING "spark-client version" FORCE)
set(SPARK_TUI_PACKAGE_VERSION "${SPARK_TUI_VERSION_LINE}"
    CACHE STRING "spark-client package version" FORCE)
project(spark-client VERSION ${SPARK_TUI_PROJECT_VERSION} LANGUAGES CXX)

# Build-tree binaries are copied into the standalone Linux tarball, so their
# rpath must not contain an absolute build directory. With this option CMake
# rewrites it to $ORIGIN, which is valid both in the build tree and in the
# extracted tarball.
set(CMAKE_BUILD_RPATH_USE_ORIGIN ON)

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
option(SPARK_TUI_BUILD_GTK         "Build the GTK4/libadwaita frontend" OFF)
option(SPARK_TUI_USE_SYSTEM_FTXUI  "Require a system-installed FTXUI (no fetch)" OFF)
option(SPARK_TUI_USE_SYSTEM_JSON   "Use system nlohmann_json"      OFF)
option(SPARK_TUI_USE_SYSTEM_HTTPLIB "Use system cpp-httplib"       OFF)

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED COMPONENTS SSL Crypto)

if(MSVC)
  set(SPARK_TUI_WARNING_FLAGS /W4 /permissive- /utf-8)
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
set(SPARK_CORE_SOURCES
  src/util.cpp
  src/worker.cpp
  src/i18n.cpp
  src/config.cpp
  src/options.cpp
  src/platform/session.cpp
  src/service_source.cpp
  src/api/client.cpp
  src/api/models.cpp
  src/forum/client.cpp
  src/forum/models.cpp
  src/forum/render.cpp
  src/ui/render_detail.cpp
)
if(UNIX AND NOT APPLE)
  add_library(spark_core SHARED ${SPARK_CORE_SOURCES})
  set_target_properties(spark_core PROPERTIES
    OUTPUT_NAME sparkapp-core
    VERSION ${PROJECT_VERSION}
    SOVERSION 0)
else()
  add_library(spark_core STATIC ${SPARK_CORE_SOURCES})
endif()
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
  src/ui/app.cpp
)
target_link_libraries(spark-tui PRIVATE
  spark_core ftxui::screen ftxui::dom ftxui::component)
# Standalone Linux tarballs ship libsparkapp-core.so next to the binaries.
set_target_properties(spark-tui PROPERTIES
  BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
target_compile_definitions(spark-tui PRIVATE
  SPARK_TUI_VERSION="${PROJECT_VERSION}")
target_compile_options(spark-tui PRIVATE ${SPARK_TUI_WARNING_FLAGS})

# ---------------------------------------------------------------------------
# Dispatcher (ships with sparkapp-core, picks spark-gui or spark-tui)
# ---------------------------------------------------------------------------
if(UNIX AND NOT APPLE)
  add_executable(spark-client src/entry/main.cpp)
  target_link_libraries(spark-client PRIVATE spark_core)
  set_target_properties(spark-client PROPERTIES
    BUILD_RPATH "$ORIGIN" INSTALL_RPATH "$ORIGIN")
  target_compile_definitions(spark-client PRIVATE
    SPARK_TUI_VERSION="${PROJECT_VERSION}")
  target_compile_options(spark-client PRIVATE ${SPARK_TUI_WARNING_FLAGS})
endif()

# ---------------------------------------------------------------------------
# GTK / libadwaita frontend (code-only framework; packaging comes later)
# ---------------------------------------------------------------------------
if(SPARK_TUI_BUILD_GTK)
  if(WIN32)
    message(FATAL_ERROR "SPARK_TUI_BUILD_GTK is currently supported on Linux only")
  endif()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
  pkg_check_modules(ADWAITA REQUIRED IMPORTED_TARGET libadwaita-1)

  add_executable(spark-gui
    src/gtk/main.cpp
    src/gtk/application.cpp
    src/gtk/widgets.cpp
    src/gtk/detail.cpp
    src/gtk/pages/home_page.cpp
    src/gtk/pages/list_page.cpp
    src/gtk/pages/forum_page.cpp
    src/gtk/pages/me_page.cpp
  )
  target_link_libraries(spark-gui PRIVATE
    spark_core
    PkgConfig::GTK4
    PkgConfig::ADWAITA
  )
  target_compile_definitions(spark-gui PRIVATE
    SPARK_TUI_VERSION="${PROJECT_VERSION}")
  target_compile_options(spark-gui PRIVATE ${SPARK_TUI_WARNING_FLAGS})

  install(TARGETS spark-gui
          RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT gui)
  install(FILES packaging/spark-gui.desktop
          DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
          COMPONENT gui)
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(TARGETS spark_core
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT core
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT core
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT core)
if(UNIX AND NOT APPLE)
  install(TARGETS spark-client
          RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT core)
endif()
install(TARGETS spark-tui
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT tui)
install(FILES packaging/spark-tui.desktop
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
        COMPONENT tui)
# Shared by the TUI and GTK desktop entries, so it ships with sparkapp-core
# instead of being duplicated (and conflicting) in both frontend packages.
install(FILES packaging/spark-tui.svg
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
        COMPONENT core)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(SPARK_TUI_BUILD_TESTS)
  enable_testing()
  add_executable(spark-tui-tests tests/test_core.cpp)
  target_link_libraries(spark-tui-tests PRIVATE spark_core)
  target_compile_options(spark-tui-tests PRIVATE ${SPARK_TUI_WARNING_FLAGS})
  add_test(NAME spark-tui-tests COMMAND spark-tui-tests)
endif()

# ---------------------------------------------------------------------------
# CPack (DEB)
# ---------------------------------------------------------------------------
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
set(CPACK_PACKAGE_NAME            "spark-client")
set(CPACK_PACKAGE_VENDOR          "Spark ORG")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Spark ORG client (terminal and GTK frontends)")
set(CPACK_PACKAGE_VERSION         "${SPARK_TUI_PACKAGE_VERSION}")
set(CPACK_PACKAGE_CONTACT         "Jianyin Li <jianyin@sparkorg.dpdns.org>")
set(CPACK_RESOURCE_FILE_LICENSE   "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_DEBIAN_PACKAGE_SECTION  "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
option(SPARK_TUI_DEB_SHLIBDEPS
       "Let dpkg-shlibdeps compute the runtime dependencies" ON)

set(CPACK_DEB_COMPONENT_INSTALL ON)
set(_spark_tui_components core tui)
if(SPARK_TUI_BUILD_GTK)
  list(APPEND _spark_tui_components gui)
endif()
set(CPACK_COMPONENTS_ALL ${_spark_tui_components})
set(CPACK_DEBIAN_CORE_PACKAGE_NAME "sparkapp-core")
set(CPACK_DEBIAN_TUI_PACKAGE_NAME  "sparkapp-tui")
set(CPACK_DEBIAN_GUI_PACKAGE_NAME  "sparkapp-gui")
# All binary packages come from the spark-client source package; reprepro
# rejects a binary whose Source does not match the .dsc.
set(CPACK_DEBIAN_CORE_PACKAGE_SOURCE "spark-client")
set(CPACK_DEBIAN_TUI_PACKAGE_SOURCE  "spark-client")
set(CPACK_DEBIAN_GUI_PACKAGE_SOURCE  "spark-client")

if(SPARK_TUI_DEB_SHLIBDEPS)
  set(CPACK_DEBIAN_CORE_PACKAGE_SHLIBDEPS ON)
  set(CPACK_DEBIAN_TUI_PACKAGE_SHLIBDEPS OFF)
  set(CPACK_DEBIAN_GUI_PACKAGE_SHLIBDEPS OFF)
  if(SPARK_TUI_BUILD_GTK)
    set(CPACK_DEBIAN_GUI_PACKAGE_DEPENDS
        "sparkapp-core (= ${CPACK_PACKAGE_VERSION}), libgtk-4-1, libadwaita-1-0, libc6, libstdc++6")
  endif()
  set(_spark_tui_depends "sparkapp-core (= ${CPACK_PACKAGE_VERSION}), libc6, libstdc++6")
  if(SPARK_TUI_FTXUI_FROM_SYSTEM)
    string(APPEND _spark_tui_depends
      ", libftxui-screen5.0.0, libftxui-dom5.0.0, libftxui-component5.0.0")
  endif()
  set(CPACK_DEBIAN_TUI_PACKAGE_DEPENDS "${_spark_tui_depends}")
  set(CPACK_DEBIAN_CORE_PACKAGE_DEPENDS "")
else()
  set(CPACK_DEBIAN_CORE_PACKAGE_SHLIBDEPS OFF)
  set(CPACK_DEBIAN_TUI_PACKAGE_SHLIBDEPS OFF)
  set(CPACK_DEBIAN_GUI_PACKAGE_SHLIBDEPS OFF)
  if(SPARK_TUI_BUILD_GTK)
    set(CPACK_DEBIAN_GUI_PACKAGE_DEPENDS
        "sparkapp-core (= ${CPACK_PACKAGE_VERSION}), libgtk-4-1, libadwaita-1-0, libc6, libstdc++6")
  endif()
  set(CPACK_DEBIAN_CORE_PACKAGE_DEPENDS
      "libc6, libstdc++6, libssl3t64 | libssl3")
  set(CPACK_DEBIAN_TUI_PACKAGE_DEPENDS
      "sparkapp-core (= ${CPACK_PACKAGE_VERSION}), libc6, libstdc++6")
endif()
set(CPACK_DEBIAN_FILE_NAME        DEB-DEFAULT)
set(CPACK_GENERATOR               "DEB")
include(CPack)
endif()
