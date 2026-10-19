# IKriate: generated from the upstream bundles in upstream/ by
# packaging/branding/make-bundles.py.
if (IKRIATE_TABLET_PACKAGE)
    # Curated tablet set: the most distinct presets plus the tips and patterns
    # they use (make-bundles.py, TABLET_PRESETS).
    set (krita_BUNDLE_FILES README IKriate_Brushes_Tablet.bundle RGBA_Brushes.bundle)
else()
    set (krita_BUNDLE_FILES README IKriate_Brushes.bundle RGBA_Brushes.bundle)
endif()
if (HAVE_SEEXPR AND NOT IKRIATE_TABLET_PACKAGE)
    list(APPEND krita_BUNDLE_FILES SeExpr_Examples.bundle)
endif()

# IKriate: Krita 3's resource bundle (16 MB) ships disabled and is already left
# out of upstream's Android build. Leaving it out everywhere saves 16 MB of
# package, another 16 MB copied into the user's resource folder on first run,
# and about 20 s of first-run indexing. Users can still import it from
# Settings > Manage Resource Libraries.
option(IKRIATE_INSTALL_KRITA3_BUNDLE "Install Krita 3's legacy resource bundle (disabled by default in Krita)" OFF)
if (NOT ANDROID AND IKRIATE_INSTALL_KRITA3_BUNDLE)
    set (krita_BUNDLE_FILES
         ${krita_BUNDLE_FILES}
         upstream/Krita_3_Default_Resources.bundle
         )
endif()

install( FILES
    ${krita_BUNDLE_FILES}
    DESTINATION  ${KDE_INSTALL_DATADIR}/krita/bundles)
