/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef IKLAUNCHSCREEN_H
#define IKLAUNCHSCREEN_H

#include "kritaui_export.h"

/**
 * The Android launch screen (logo on the app background) is shown by the
 * system before any native code runs, and kept on screen ("sticky") while Qt
 * and IKriate start, so the window never goes black. It is removed when the
 * first IKriate screen is ready, or earlier if a dialog needs the user.
 *
 * On other platforms these calls do nothing.
 */
namespace IkLaunchScreen
{
/// Call right after the QApplication exists.
KRITAUI_EXPORT void install();
/// Call once the first screen has been painted.
KRITAUI_EXPORT void hide();
}

#endif
