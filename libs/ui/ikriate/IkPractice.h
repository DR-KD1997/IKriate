/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef IKPRACTICE_H
#define IKPRACTICE_H

#include "kritaui_export.h"

class QWidget;
class IkInterfaceManager;

/**
 * Guided practice, opened from the lightbulb on the home screen.
 *
 *  - Timed drawing: the user's reference images, one after another, each for
 *    a fixed time (gesture and figure drawing).
 *  - Memory drawing: an image is shown briefly, then hidden; draw it from
 *    memory and reveal it to compare.
 *  - Value study: the reference in greyscale, to practise light and shadow.
 *  - Warm-up: timed prompts (lines, ellipses, hatching) without references.
 *
 * Each session opens a new canvas and a floating, movable reference panel
 * with the countdown and pause / skip / stop controls.
 */
namespace IkPractice
{
/// Shows the activity menu anchored to @p anchor.
KRITAUI_EXPORT void showMenu(IkInterfaceManager *manager, QWidget *anchor);
/// Keeps the session panel above the canvas layouts.
KRITAUI_EXPORT void raiseActive();
/// Ends a running session (e.g. when returning to the home screen).
KRITAUI_EXPORT void stopActive();
}

#endif // IKPRACTICE_H
