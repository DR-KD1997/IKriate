install(FILES
    clipstudiopaintcompatible.profile
    kritadefault.profile
    ikriatetablet.profile
    painttoolsaicompatible.profile
    photoshopcompatible.profile
    DESTINATION  ${KDE_INSTALL_DATADIR}/krita/input
)
if (WIN32)
   install(FILES
        tabletpro.profile
        DESTINATION  ${KDE_INSTALL_DATADIR}/krita/input
    )
endif()

