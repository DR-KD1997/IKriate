# SPDX-License-Identifier: MIT
# IKriate: dependencies are found by ../../CMakeLists.txt; no generators,
# tests or Rust bindings.

include(DrawdanceHelpers)

add_subdirectory(bundled)
add_subdirectory(libcommon)
add_subdirectory(libdb)
add_subdirectory(libmsg)
add_subdirectory(libengine)

add_library(drawdance_server INTERFACE)
target_link_libraries(drawdance_server INTERFACE
    dpengine dpmsg dpdb_qt dpdb dpcommon cmake-config)

add_subdirectory(libimpex)
add_library(drawdance_client INTERFACE)
target_link_libraries(drawdance_client INTERFACE
    dpimpex dpengine dpmsg dpdb dpcommon cmake-config)
