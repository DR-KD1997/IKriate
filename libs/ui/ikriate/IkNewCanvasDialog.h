/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef IKNEWCANVASDIALOG_H
#define IKNEWCANVASDIALOG_H

#include <QWidget>

#include "kritaui_export.h"

class KisMainWindow;

/**
 * The "New Canvas" and "Template" sheets that replace Krita's text-heavy
 * new-document dialog: preset, size with units, resolution, background,
 * and an estimate of how many layers fit in memory.
 */
namespace IkNewCanvasDialog
{
KRITAUI_EXPORT void showNewCanvas(KisMainWindow *mainWindow);
KRITAUI_EXPORT void showTemplates(KisMainWindow *mainWindow);
/// Krita's complete dialog (colour model, depth, profile, ...).
KRITAUI_EXPORT void showAdvanced(KisMainWindow *mainWindow);
/// How many RGBA 8-bit layers of @p w x @p h fit in IKriate's memory budget.
KRITAUI_EXPORT int estimatedMaxLayers(int width, int height);
}

#endif // IKNEWCANVASDIALOG_H
