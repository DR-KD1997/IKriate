add_subdirectory(layerdocker)
if(HAVE_OPENEXR AND NOT ANDROID)
    add_subdirectory(smallcolorselector)
endif()
if(NOT IKRIATE_TABLET_PACKAGE)  # IKriate: desktop-only on tablets
    add_subdirectory(specificcolorselector)
endif()
if(NOT IKRIATE_TABLET_PACKAGE)  # IKriate: desktop-only on tablets
    add_subdirectory(digitalmixer)
endif()
add_subdirectory(advancedcolorselector)
add_subdirectory(presetdocker)
add_subdirectory(historydocker)
if(NOT IKRIATE_TABLET_PACKAGE)  # IKriate: desktop-only on tablets
    add_subdirectory(channeldocker)
endif()
if(NOT IKRIATE_TABLET_PACKAGE)  # IKriate: desktop-only on tablets
    add_subdirectory(artisticcolorselector)
endif()
if(NOT IKRIATE_TABLET_PACKAGE)  # IKriate: desktop-only on tablets
    add_subdirectory(tasksetdocker)
endif()
add_subdirectory(compositiondocker)
add_subdirectory(patterndocker)
add_subdirectory(griddocker)
if(NOT IKRIATE_TABLET_PACKAGE)  # IKriate: desktop-only on tablets
    add_subdirectory(arrangedocker)
endif()
if(HAVE_OCIO AND NOT ANDROID AND NOT HAIKU)
    add_subdirectory(lut)
endif()
add_subdirectory(overview)
add_subdirectory(palettedocker)
add_subdirectory(animation)
if(NOT IKRIATE_TABLET_PACKAGE)  # IKriate: desktop-only on tablets
    add_subdirectory(presethistory)
endif()
if(NOT IKRIATE_TABLET_PACKAGE)  # IKriate: desktop-only on tablets
    add_subdirectory(svgcollectiondocker)
endif()
if(NOT IKRIATE_TABLET_PACKAGE)  # IKriate: desktop-only on tablets
    add_subdirectory(histogram)
endif()
if(NOT IKRIATE_TABLET_PACKAGE)  # IKriate: desktop-only on tablets
    add_subdirectory(gamutmask)
endif()
add_subdirectory(recorder)
if(NOT IKRIATE_TABLET_PACKAGE)  # IKriate: desktop-only on tablets
    add_subdirectory(touchdocker)
endif()
if(NOT IKRIATE_TABLET_PACKAGE)  # IKriate: desktop-only on tablets
    add_subdirectory(logdocker)
endif()
if(NOT IKRIATE_TABLET_PACKAGE)  # IKriate: desktop-only on tablets
    add_subdirectory(snapshotdocker)
endif()
add_subdirectory(storyboarddocker)
add_subdirectory(widegamutcolorselector)
add_subdirectory(brushhud)
add_subdirectory(textproperties)
