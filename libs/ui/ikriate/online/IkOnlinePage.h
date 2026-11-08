/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef IKONLINEPAGE_H
#define IKONLINEPAGE_H

#include <QPointer>
#include <QWidget>

#include "kritaui_export.h"

class KisDocument;
class KisMainWindow;
class QRect;

/**
 * The Online tab of the home screen: join a session by link or code, the
 * sessions joined or hosted lately, Drawpile's public session list, hosting,
 * and the name shown to others. While in a session it offers to go back to
 * the shared canvas or leave.
 */
class KRITAUI_EXPORT IkOnlinePage : public QWidget
{
    Q_OBJECT
public:
    IkOnlinePage(KisMainWindow *mainWindow, QWidget *parent);
    ~IkOnlinePage() override;
    /// The tab was opened: refreshes the public list (at most once a minute).
    void activate();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    struct Private;
    Private *const d;
};

/**
 * What IKriate shows over the canvas of a session: a pill with the live
 * state, the people (tap for the list, invite link and moderation) and the
 * chat; the other participants' cursors are drawn on the canvas.
 */
class KRITAUI_EXPORT IkSessionOverlay : public QWidget
{
    Q_OBJECT
public:
    explicit IkSessionOverlay(KisMainWindow *mainWindow);
    ~IkSessionOverlay() override;
    /// Places the pill in @p canvasArea (main window coordinates).
    void relayout(const QRect &canvasArea, bool canvasShown);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    struct Private;
    Private *const d;
};

namespace IkHostDialog
{
/// Host a session: @p source is the artwork to share (the current one when
/// opened from the canvas); null lets the user pick one from the gallery or
/// a blank canvas.
KRITAUI_EXPORT void show(KisMainWindow *mainWindow, KisDocument *source);
}

#endif // IKONLINEPAGE_H
