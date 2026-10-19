# SPDX-FileCopyrightText: 2026 IKriate contributors
# SPDX-License-Identifier: BSD-2-Clause

# Bundled interface skins. Users add their own to <user data>/ikriate/skins.
install(FILES
    skins/ikriate-dark.ikskin
    skins/ikriate-light.ikskin
    skins/ikriate-charcoal.ikskin
    skins/ikriate-paper.ikskin
    skins/ikriate-midnight.ikskin
    skins/ikriate-high-contrast.ikskin
    DESTINATION ${KDE_INSTALL_DATADIR}/krita/ikriate/skins
)
