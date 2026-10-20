/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef IKTOUCHGESTURES_H
#define IKTOUCHGESTURES_H

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QStringList>

#include "kritaui_export.h"

class QTimer;
class IkInterfaceManager;

/**
 * Multi-finger taps and double taps on the canvas, each mapped to a function
 * the user chooses under Settings › Advanced › Touch Gestures.
 *
 * Krita's own tap shortcuts are not used by the IKriate layouts (the IKriate
 * input profile leaves them out), so these are the only tap actions. Drags,
 * pinches and rotations stay with Krita's input manager.
 */
class KRITAUI_EXPORT IkTouchGestures : public QObject
{
    Q_OBJECT
public:
    enum Gesture { TwoFingerTap, ThreeFingerTap, FourFingerTap, FiveFingerTap,
                   TwoFingerDoubleTap, ThreeFingerDoubleTap, FourFingerDoubleTap, GestureCount };

    explicit IkTouchGestures(IkInterfaceManager *manager);

    static QString gestureName(Gesture gesture);
    /// Function ids and their display names, in menu order.
    static QStringList functionIds();
    static QString functionName(const QString &id);
    static QString assignedFunction(Gesture gesture);
    static void assignFunction(Gesture gesture, const QString &id);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void tapped(int fingers);
    void run(Gesture gesture);

    IkInterfaceManager *m_manager;
    QElapsedTimer m_touchTime;
    QHash<int, QPointF> m_starts;
    int m_maxPoints {0};
    bool m_valid {false};
    QElapsedTimer m_lastTap;
    int m_lastTapFingers {0};
    int m_pendingFingers {0};
    QTimer *m_pending {nullptr};
};

#endif // IKTOUCHGESTURES_H
