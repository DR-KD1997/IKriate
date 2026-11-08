# SPDX-FileCopyrightText: 2026 IKriate contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Drawpile's canvas engine (drawdance), protocol and networking libraries,
# vendored from Drawpile 2.3.1 (https://drawpile.net, GPL-3.0-or-later; see
# LICENSE.txt and AUTHORS here) for IKriate's online sessions.
#
# This replaces Drawpile's top-level CMakeLists.txt: it sets the variables its
# subdirectories expect and builds everything as static, position-independent
# libraries with hidden symbols, so they can be linked into kritaui without
# clashing with Krita's own copies of libmypaint and SQLite.
#
# Local changes to the vendored code are listed in IKRIATE-CHANGES.md.

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_LIST_DIR}/cmake)

set(BUILD_SHARED_LIBS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOMOC_PATH_PREFIX ON)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# What Drawpile's build options would be for a client with a built-in server.
set(CLIENT ON)
set(SERVER OFF)
set(SERVERGUI OFF)
set(BUILTINSERVER ON)
set(TOOLS OFF)
set(TESTS OFF)
set(BENCHMARKS OFF)
set(ALLOW_QTEST_TESTS OFF)
set(USE_GENERATORS OFF)
set(HAVE_TCPSOCKETS ON)
set(HAVE_UDPSOCKETS OFF)
set(IMAGE_IMPL QT)
set(FILE_IO_IMPL QT)
set(ZIP_IMPL QUAZIP)
set(PROJECT_NAME Drawpile)
set(PROJECT_VERSION 2.3.1)
set(PROJECT_HOMEPAGE_URL https://drawpile.net)
set(DRAWPILE_PROTO_DEFAULT_PORT 27750)
set(DRAWPILE_WEBADMIN_DEFAULT_PORT 27780)
set(DRAWPILE_WEBSOCKET_DEFAULT_PORT 27751)
set(dp_proto_regex "^[ \t]*version:[ \t]*\"?dp:([0-9]+)\\.([0-9]+)\\.([0-9]+)\"?$")
file(STRINGS ${CMAKE_CURRENT_LIST_DIR}/protocol.yaml dp_proto_version LIMIT_COUNT 1 REGEX ${dp_proto_regex})
if(dp_proto_version MATCHES ${dp_proto_regex})
    set(DRAWPILE_PROTO_SERVER_VERSION ${CMAKE_MATCH_1})
    set(DRAWPILE_PROTO_MAJOR_VERSION ${CMAKE_MATCH_2})
    set(DRAWPILE_PROTO_MINOR_VERSION ${CMAKE_MATCH_3})
else()
    message(FATAL_ERROR "Drawpile: invalid protocol.yaml")
endif()
message(STATUS "Drawpile protocol version: dp:${DRAWPILE_PROTO_SERVER_VERSION}.${DRAWPILE_PROTO_MAJOR_VERSION}.${DRAWPILE_PROTO_MINOR_VERSION}")

set(QT_VERSION_MAJOR ${QT_MAJOR_VERSION})
set(QT_VERSION ${Qt${QT_MAJOR_VERSION}Core_VERSION})
set(QT_PACKAGE_NAME Qt${QT_MAJOR_VERSION})
# Not wanted here even when Krita's build found them.
set(${QT_PACKAGE_NAME}LinguistTools_FOUND OFF)

find_package(${QT_PACKAGE_NAME} REQUIRED COMPONENTS Core Gui Network Xml)
if(ANDROID AND QT_MAJOR_VERSION EQUAL 5)
    find_package(Qt5 REQUIRED COMPONENTS AndroidExtras)
endif()
find_package(ZLIB REQUIRED)
if(NOT WIN32)
    find_package(Threads REQUIRED)
endif()

if(QT_VERSION VERSION_LESS "6.10.0")
    function(dp_qt_private_headers target module)
        set(var_name "Qt${QT_VERSION_MAJOR}${module}_PRIVATE_INCLUDE_DIRS")
        target_include_directories("${target}" SYSTEM PRIVATE "${${var_name}}")
    endfunction()
else()
    function(dp_qt_private_headers target module)
        find_package("Qt${QT_VERSION_MAJOR}${module}Private" REQUIRED NO_MODULE)
        target_link_libraries("${target}" PRIVATE "Qt${QT_VERSION_MAJOR}::${module}Private")
    endfunction()
endif()
function(directory_auto_source_groups)
endfunction()
include(GetIgnoreWarningsInDirectory)

# Drawpile's code is built with its own warnings policy, not Krita's.
if(MSVC)
    add_compile_options(/W1 /utf-8)
else()
    add_compile_options(-w -fno-strict-aliasing)
    add_compile_definitions(DP_NO_STRICT_ALIASING)
endif()
# Krita's strict Qt settings (no signals/slots keywords, strict iterators,
# deprecation cut-off) are not what Drawpile's code is written for.
get_directory_property(_dp_defs COMPILE_DEFINITIONS)
list(FILTER _dp_defs EXCLUDE REGEX "^-?D?QT_(NO_SIGNALS_SLOTS_KEYWORDS|NO_KEYWORDS|STRICT_ITERATORS|NO_URL_CAST_FROM_STRING|USE_QSTRINGBUILDER|USE_FAST_CONCATENATION|USE_FAST_OPERATOR_PLUS|DISABLE_DEPRECATED_BEFORE.*|NO_CAST_.*|NO_FOREACH)$")
set_directory_properties(PROPERTIES COMPILE_DEFINITIONS "${_dp_defs}")
remove_definitions(-DQT_USE_QSTRINGBUILDER -DQT_NO_SIGNALS_SLOTS_KEYWORDS -DQT_NO_URL_CAST_FROM_STRING
                   -DQT_USE_FAST_CONCATENATION -DQT_USE_FAST_OPERATOR_PLUS -DQT_STRICT_ITERATORS)

# zstd (BSD), compressed canvas messages.
file(GLOB _dp_zstd_sources
    ${CMAKE_CURRENT_LIST_DIR}/zstd/common/*.c
    ${CMAKE_CURRENT_LIST_DIR}/zstd/compress/*.c
    ${CMAKE_CURRENT_LIST_DIR}/zstd/decompress/*.c)
add_library(dp_zstd STATIC ${_dp_zstd_sources})
target_include_directories(dp_zstd SYSTEM PUBLIC ${CMAKE_CURRENT_LIST_DIR}/zstd)
target_compile_definitions(dp_zstd PRIVATE ZSTD_DISABLE_ASM ZSTD_MULTITHREAD=0 ZSTD_LEGACY_SUPPORT=0)
add_library(zstd::libzstd ALIAS dp_zstd)

# WebP, as Krita finds it.
if(NOT TARGET dpwebp::dpwebp)
    add_library(dpwebp::dpwebp INTERFACE IMPORTED)
    if(TARGET WebP::webp)
        target_link_libraries(dpwebp::dpwebp INTERFACE WebP::webp WebP::webpdemux WebP::libwebpmux)
    else()
        find_package(PkgConfig QUIET)
        if(PKG_CONFIG_FOUND)
            pkg_check_modules(DPWEBP IMPORTED_TARGET libwebp libwebpdemux libwebpmux)
        endif()
        if(TARGET PkgConfig::DPWEBP)
            target_link_libraries(dpwebp::dpwebp INTERFACE PkgConfig::DPWEBP)
        else()
            find_package(WebP REQUIRED)
            target_link_libraries(dpwebp::dpwebp INTERFACE WebP::webp WebP::webpdemux WebP::libwebpmux)
        endif()
    endif()
endif()

include_directories(${CMAKE_CURRENT_LIST_DIR}/src)
string(LENGTH "${CMAKE_CURRENT_LIST_DIR}/" project_dir_length)
add_compile_definitions("DP_PROJECT_DIR_LENGTH=${project_dir_length}" WIN32_LEAN_AND_MEAN NOMINMAX HAVE_TCPSOCKETS)

add_subdirectory(src/cmake-config)
add_subdirectory(src/drawdance)
add_subdirectory(src/libshared)
add_subdirectory(src/libserver)
add_subdirectory(src/libclient)

# Everything IKriate links against.
add_library(ikdrawpile INTERFACE)
target_link_libraries(ikdrawpile INTERFACE dpclient dpserver dpshared drawdance_client cmake-config)
target_include_directories(ikdrawpile INTERFACE ${CMAKE_CURRENT_LIST_DIR}/src)
if(UNIX AND NOT APPLE)
    # Keep the vendored libmypaint/SQLite symbols out of kritaui's export table.
    target_link_options(ikdrawpile INTERFACE
        "LINKER:--exclude-libs,libdpclient.a:libdpserver.a:libdpshared.a:libdpimpex.a:libdpengine.a:libdpmsg.a:libdpdb.a:libdpdb_qt.a:libdpcommon.a:libdp_zstd.a:libcmake-config.a")
endif()
