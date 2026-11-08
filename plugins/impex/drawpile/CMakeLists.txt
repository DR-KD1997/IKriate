# Drawpile's file formats (.dppr, .dpcs) and QOI, read and written by the
# Drawpile code that kritaui carries for online sessions (IkDrawpileFiles).
kis_add_library(kritadrawpileimport MODULE kis_drawpile_import.cpp)
target_link_libraries(kritadrawpileimport kritaui)
install(TARGETS kritadrawpileimport DESTINATION ${KRITA_PLUGIN_INSTALL_DIR})

kis_add_library(kritadrawpileexport MODULE kis_drawpile_export.cpp)
target_link_libraries(kritadrawpileexport kritaui kritaimpex)
install(TARGETS kritadrawpileexport DESTINATION ${KRITA_PLUGIN_INSTALL_DIR})
