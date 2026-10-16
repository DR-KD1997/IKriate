/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef IKPATHS_H
#define IKPATHS_H

#include <QString>
#include <kritaresources_export.h>

/**
 * Locations that are specific to IKriate, so that it can be installed next to
 * a regular Krita without the two sharing configuration or user resources.
 *
 * Implemented in KoResourcePaths.cpp.
 */
namespace IkPaths
{
/// Per-user data root (resources, resource cache, skins). Linux:
/// ~/.local/share/ikriate, Windows: %APPDATA%\ikriate, Android: app sandbox.
KRITARESOURCES_EXPORT QString userDataLocation();

/// Folder that holds user-installed and generated interface skins.
KRITARESOURCES_EXPORT QString userSkinsLocation();
}

#endif // IKPATHS_H
