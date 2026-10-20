/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef IKWINDOWFRAMES_H
#define IKWINDOWFRAMES_H

#include <QObject>

#include "kritaui_export.h"

class QWidget;

/**
 * Gives every secondary window (dialogs, menus, pop-ups, tool windows) rounded
 * corners and an outline in the accent colours, of the same thickness
 * everywhere, in place of the style's plain rectangular frame.
 *
 * Works on windows IKriate does not own (Krita's dialogs, the brush editor,
 * combo box lists): an outline overlay is added on top of the window's
 * children and the window is masked to the rounded shape.
 */
class KRITAUI_EXPORT IkWindowFrames : public QObject
{
    Q_OBJECT
public:
    explicit IkWindowFrames(QObject *parent = nullptr);

    /// Outline thickness in device-independent pixels.
    static int thickness();
    /// Corner radius for @p window.
    static int radiusFor(const QWidget *window);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    static bool wantsFrame(const QWidget *window);
    void decorate(QWidget *window);
    void updateShape(QWidget *window);
};

#endif // IKWINDOWFRAMES_H
