/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "IkLaunchScreen.h"

#include <QtGlobal>

#ifdef Q_OS_ANDROID
#include <QApplication>
#include <QDialog>
#include <QEvent>
#include <QObject>
#include <QTimer>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QtCore/qcoreapplication_platform.h>
#else
#include <QtAndroid>
#endif

namespace
{
bool s_hidden = false;

void hideNow(int fadeMs)
{
    if (s_hidden) {
        return;
    }
    s_hidden = true;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QNativeInterface::QAndroidApplication::hideSplashScreen(fadeMs);
#else
    QtAndroid::hideSplashScreen(fadeMs);
#endif
}

/// Dialogs shown during start-up (errors, recovery) must not stay hidden
/// behind the launch screen.
class DialogWatcher : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Show && qobject_cast<QDialog *>(watched)) {
            hideNow(0);
            qApp->removeEventFilter(this);
            deleteLater();
        }
        return false;
    }
};
}

void IkLaunchScreen::install()
{
    if (!qApp) {
        return;
    }
    qApp->installEventFilter(new DialogWatcher(qApp));
    // Safety net: never keep the logo up indefinitely.
    QTimer::singleShot(30000, qApp, [] { hideNow(200); });
}

void IkLaunchScreen::hide()
{
    // Let the first frame of the new screen reach the display before fading.
    QTimer::singleShot(50, qApp, [] { hideNow(200); });
}

#else

void IkLaunchScreen::install()
{
}

void IkLaunchScreen::hide()
{
}

#endif
