/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef IKONLINECOMMON_H
#define IKONLINECOMMON_H

#include <QString>
#include <functional>

class QWidget;

/// Small form in a centred popover: a title, a message, optionally a text
/// field (a null @p fieldPlaceholder means none) and an accept button. The X
/// or a tap outside cancels.
void ikAskInPopover(QWidget *window, const QString &title, const QString &text, const QString &fieldPlaceholder,
                    bool password, const QString &acceptText, std::function<void(const QString &)> onAccept,
                    std::function<void()> onCancel, const QString &initialText = QString());

#endif // IKONLINECOMMON_H
