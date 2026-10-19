if(NOT IKRIATE_TABLET_PACKAGE)  # IKriate: desktop-only on tablets
    add_subdirectory( imagesplit )
endif()
if(NOT IKRIATE_TABLET_PACKAGE)  # IKriate: desktop-only on tablets
    add_subdirectory( clonesarray )
endif()
add_subdirectory( colorrange )
add_subdirectory( colorspaceconversion )
if(NOT IKRIATE_TABLET_PACKAGE)  # IKriate: desktop-only on tablets
    add_subdirectory( metadataeditor )
endif()
add_subdirectory( modify_selection )
add_subdirectory( offsetimage )
add_subdirectory( rotateimage )
if(NOT IKRIATE_TABLET_PACKAGE)  # IKriate: desktop-only on tablets
    add_subdirectory( separate_channels )
endif()
add_subdirectory( shearimage )
add_subdirectory( layergroupswitcher )
add_subdirectory( resourcemanager )
add_subdirectory( layersplit )
if(NOT IKRIATE_TABLET_PACKAGE)  # IKriate: desktop-only on tablets
    add_subdirectory( waveletdecompose )
endif()
add_subdirectory( pykrita )
if(NOT IKRIATE_TABLET_PACKAGE)  # IKriate: desktop-only on tablets
    add_subdirectory( qmic )
endif()
add_subdirectory( buginfo )
add_subdirectory( samplescreencolor )
if(NOT IKRIATE_TABLET_PACKAGE)  # IKriate: desktop-only on tablets
    add_subdirectory( dbexplorer )
endif()

if (ANDROID)
    add_subdirectory( spensettings )
endif()
