# IKriate: the default presets now live in IKriate_Brushes.bundle (see
# packaging/branding/make-bundles.py). Only the tags are installed here: the
# brush categories shown by the brush panel, and the favourites tag.
file(GLOB IKRIATE_CATEGORY_TAGS ${CMAKE_CURRENT_SOURCE_DIR}/ikriate-*.tag)
install( FILES
    "Favorites.tag"
    ${IKRIATE_CATEGORY_TAGS}
DESTINATION  ${KDE_INSTALL_DATADIR}/krita/paintoppresets)
