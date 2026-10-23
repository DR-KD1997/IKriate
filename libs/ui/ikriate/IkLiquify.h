/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef IKLIQUIFY_H
#define IKLIQUIFY_H

#include "kritaui_export.h"

class IkInterfaceManager;

/**
 * The Liquify bar near the bottom of the canvas while the transform tool is
 * in liquify mode: cancel, more settings, strength, hide the interface,
 * apply; and the modes Push, Pinch, Bloat, Swirl clockwise, Swirl
 * counter-clockwise and Restore.
 *
 * It drives the liquify controls of Krita's transform tool options.
 */
namespace IkLiquify
{
KRITAUI_EXPORT void show(IkInterfaceManager *manager);
KRITAUI_EXPORT void hide();
KRITAUI_EXPORT bool isShown();
/// Keeps the bar above the layout panels and at the bottom of the canvas.
KRITAUI_EXPORT void raiseAndPlace();
}

#endif // IKLIQUIFY_H
