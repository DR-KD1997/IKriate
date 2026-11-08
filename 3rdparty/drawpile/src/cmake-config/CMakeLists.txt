# IKriate: Drawpile's build configuration header, without git or translations.
add_library(cmake-config STATIC)
target_sources(cmake-config PRIVATE
	${CMAKE_CURRENT_BINARY_DIR}/cmake-config/cmake-config/config.cpp
	${CMAKE_CURRENT_BINARY_DIR}/cmake-config/cmake-config/config.h
)
target_include_directories(cmake-config PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/cmake-config)
set(DRAWPILE_LOCALES "\"en\"")
set(DRAWPILE_FILE_GROUP_CANVAS_REPLACING "\"ora\", \"psd\", \"dppr\", \"dpcs\", \"dprec\", \"dptxt\"")
set(DRAWPILE_FILE_GROUP_RECORDING "*.dprec *.dptxt")
set(DRAWPILE_FILE_GROUP_LAYERED_IMAGE "*.dppr *.dpcs *.ora *.psd")
set(DRAWPILE_FILE_GROUP_FLAT_IMAGE "*.png *.jpg *.jpeg *.webp *.qoi")
set(DRAWPILE_FILE_GROUP_IMAGE "${DRAWPILE_FILE_GROUP_LAYERED_IMAGE} ${DRAWPILE_FILE_GROUP_FLAT_IMAGE}")
set(GIT_VERSION "${PROJECT_VERSION}")
configure_file(config.h.in cmake-config/cmake-config/config.h)
configure_file(config.cpp.in cmake-config/cmake-config/config.cpp)
