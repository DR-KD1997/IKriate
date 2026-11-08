/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef IKDRAWPILEFILES_H
#define IKDRAWPILEFILES_H

#include <QString>

#include <kis_types.h>

#include "kritaui_export.h"

/**
 * Drawpile's file formats, read and written with Drawpile's own code (which
 * is part of kritaui for online sessions; the drawpile import/export plugin
 * calls these):
 *
 *  - .dppr  Drawpile project (layers, history-free snapshot, SQLite)
 *  - .dpcs  Drawpile canvas state (layers, SQLite)
 *  - .qoi   "Quite OK Image" (flat RGBA)
 *
 * Layers, groups (pass-through or isolated), opacity, blend modes,
 * visibility and clipping ("inherit alpha" in Krita) carry over. Pixels are
 * 8 bit RGBA, Drawpile's colour model; other colour models are converted.
 */
namespace IkDrawpileFiles
{
/// Starts Drawpile's canvas engine (once; any thread).
KRITAUI_EXPORT void ensureEngine();

/// Reads @p path (any format Drawpile reads: the above, ORA, PSD, PNG, JPEG,
/// WebP). Null on failure, with the reason in @p error.
KRITAUI_EXPORT KisImageSP load(const QString &path, QString *error);

/// Writes @p image to @p path in the format its suffix names (dppr, dpcs,
/// qoi, or any other Drawpile writes).
KRITAUI_EXPORT bool save(KisImageSP image, const QString &path, QString *error);
}

#endif // IKDRAWPILEFILES_H
