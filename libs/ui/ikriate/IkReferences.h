/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef IKREFERENCES_H
#define IKREFERENCES_H

#include <QString>

#include "kritaui_export.h"

class QWidget;
class IkInterfaceManager;

/**
 * Reference images, in one of two placements (Settings › Theme › Interface):
 *
 * - On the canvas: Krita reference images. They move, zoom and rotate with
 *   the canvas and are adjusted with the reference tool ("Adjust").
 * - Floating: panels over the interface that stay where they are put while
 *   the canvas moves underneath; drag the bar to move, the corner to resize.
 */
namespace IkReferences
{
enum Placement { OnCanvas, Floating };

KRITAUI_EXPORT Placement placement();
KRITAUI_EXPORT void setPlacement(Placement placement);

/// The reference button: opens the file browser straight away when there
/// are no references yet, otherwise a small menu (add, adjust, hide, clear).
KRITAUI_EXPORT void buttonClicked(IkInterfaceManager *manager, QWidget *anchor);
/// File browser, then adds the image in the current placement.
KRITAUI_EXPORT void addImage(IkInterfaceManager *manager);
KRITAUI_EXPORT int count(IkInterfaceManager *manager);

/// Floating panels follow the canvas page: hidden on the home screen.
KRITAUI_EXPORT void setFloatingVisible(IkInterfaceManager *manager, bool visible);
/// Keeps floating panels above the layout panels.
KRITAUI_EXPORT void raiseFloating();
}

#endif // IKREFERENCES_H
