/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "IkTouchGestures.h"

#include <QAction>
#include <QApplication>
#include <QLineF>
#include <QTimer>
#include <QTouchEvent>
#include <QWidget>

#include <KConfigGroup>
#include <KSharedConfig>
#include <klocalizedstring.h>

#include "IkInterfaceManager.h"
#include "KisMainWindow.h"

namespace
{
constexpr int kTapMaxMs = 300;     ///< longer than this is a hold or a drag
constexpr int kDoubleTapMs = 400;  ///< second tap must start within this
constexpr qreal kTapSlop = 28.0;   ///< fingers may move this far (px)

const char *const kKeys[IkTouchGestures::GestureCount] = {"TwoFingerTap", "ThreeFingerTap", "FourFingerTap", "FiveFingerTap",
                                                          "TwoFingerDoubleTap", "ThreeFingerDoubleTap", "FourFingerDoubleTap"};
const char *const kDefaults[IkTouchGestures::GestureCount] = {"undo", "redo", "hide_interface", "none",
                                                              "none", "flip_canvas", "none"};

KConfigGroup gestureConfig()
{
    return KConfigGroup(KSharedConfig::openConfig(), "IKriateGestures");
}

int touchCount(QTouchEvent *e)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return int(e->points().size());
#else
    return e->touchPoints().size();
#endif
}
}

IkTouchGestures::IkTouchGestures(IkInterfaceManager *manager)
    : QObject(manager)
    , m_manager(manager)
{
    m_pending = new QTimer(this);
    m_pending->setSingleShot(true);
    m_pending->setInterval(kDoubleTapMs);
    connect(m_pending, &QTimer::timeout, this, [this]() {
        const int n = m_pendingFingers;
        m_pendingFingers = 0;
        if (n >= 2 && n <= 5) run(Gesture(TwoFingerTap + n - 2));
    });
    qApp->installEventFilter(this);
}

QString IkTouchGestures::gestureName(Gesture gesture)
{
    switch (gesture) {
    case TwoFingerTap: return i18n("Two-finger tap");
    case ThreeFingerTap: return i18n("Three-finger tap");
    case FourFingerTap: return i18n("Four-finger tap");
    case FiveFingerTap: return i18n("Five-finger tap");
    case TwoFingerDoubleTap: return i18n("Two-finger double tap");
    case ThreeFingerDoubleTap: return i18n("Three-finger double tap");
    case FourFingerDoubleTap: return i18n("Four-finger double tap");
    case GestureCount: break;
    }
    return QString();
}

QStringList IkTouchGestures::functionIds()
{
    return {QStringLiteral("none"), QStringLiteral("undo"), QStringLiteral("redo"), QStringLiteral("flip_canvas"),
            QStringLiteral("fit_canvas"), QStringLiteral("reset_view"), QStringLiteral("hide_interface"),
            QStringLiteral("toggle_erase"), QStringLiteral("color_sampler"), QStringLiteral("brush_library"),
            QStringLiteral("symmetry_off"), QStringLiteral("deselect"), QStringLiteral("save")};
}

QString IkTouchGestures::functionName(const QString &id)
{
    if (id == QLatin1String("undo")) return i18n("Undo");
    if (id == QLatin1String("redo")) return i18n("Redo");
    if (id == QLatin1String("flip_canvas")) return i18n("Flip canvas horizontally");
    if (id == QLatin1String("fit_canvas")) return i18n("Fit canvas to screen");
    if (id == QLatin1String("reset_view")) return i18n("Reset zoom and rotation");
    if (id == QLatin1String("hide_interface")) return i18n("Hide or show the interface");
    if (id == QLatin1String("toggle_erase")) return i18n("Switch between draw and erase");
    if (id == QLatin1String("color_sampler")) return i18n("Colour sampler");
    if (id == QLatin1String("brush_library")) return i18n("Brush library");
    if (id == QLatin1String("symmetry_off")) return i18n("Turn symmetry off");
    if (id == QLatin1String("deselect")) return i18n("Deselect");
    if (id == QLatin1String("save")) return i18n("Save");
    return i18nc("no function for a gesture", "Nothing");
}

QString IkTouchGestures::assignedFunction(Gesture gesture)
{
    return gestureConfig().readEntry(kKeys[gesture], QString::fromLatin1(kDefaults[gesture]));
}

void IkTouchGestures::assignFunction(Gesture gesture, const QString &id)
{
    gestureConfig().writeEntry(kKeys[gesture], id);
}

bool IkTouchGestures::eventFilter(QObject *watched, QEvent *event)
{
    const QEvent::Type t = event->type();
    if (t != QEvent::TouchBegin && t != QEvent::TouchUpdate && t != QEvent::TouchEnd && t != QEvent::TouchCancel) {
        return false;
    }
    // Only taps on the canvas of this window.
    QWidget *w = qobject_cast<QWidget *>(watched);
    if (!w || w->window() != static_cast<QWidget *>(m_manager->mainWindow())
        || !(w->inherits("KisOpenGLCanvas2") || w->inherits("KisQPainterCanvas") || w->inherits("KisCanvasWidgetBase"))) {
        return false;
    }
    QTouchEvent *te = static_cast<QTouchEvent *>(event);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const auto points = te->points();
#else
    const auto points = te->touchPoints();
#endif
    if (t == QEvent::TouchBegin) {
        m_touchTime.start();
        m_starts.clear();
        m_maxPoints = 0;
        m_valid = true;
    }
    for (const auto &p : points) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        const QPointF pos = p.position();
#else
        const QPointF pos = p.pos();
#endif
        if (!m_starts.contains(p.id())) m_starts.insert(p.id(), pos);
        else if (QLineF(m_starts.value(p.id()), pos).length() > kTapSlop) m_valid = false;
    }
    m_maxPoints = qMax(m_maxPoints, touchCount(te));
    if (m_touchTime.isValid() && m_touchTime.elapsed() > kTapMaxMs) m_valid = false;

    if (t == QEvent::TouchEnd) {
        if (m_valid && m_maxPoints >= 2 && m_maxPoints <= 5) tapped(m_maxPoints);
        m_starts.clear();
    } else if (t == QEvent::TouchCancel) {
        m_valid = false;
        m_starts.clear();
    }
    return false;
}

void IkTouchGestures::tapped(int fingers)
{
    // The classic docker interface keeps Krita's own tap shortcuts.
    if (m_manager->mode() == IkInterfaceManager::Classic) return;
    const bool hasDouble = fingers <= 4 && assignedFunction(Gesture(TwoFingerDoubleTap + fingers - 2)) != QLatin1String("none");
    if (hasDouble && m_lastTapFingers == fingers && m_lastTap.isValid() && m_lastTap.elapsed() < kDoubleTapMs) {
        m_pending->stop();
        m_pendingFingers = 0;
        m_lastTapFingers = 0;
        run(Gesture(TwoFingerDoubleTap + fingers - 2));
        return;
    }
    m_lastTap.start();
    m_lastTapFingers = fingers;
    if (hasDouble) {
        // Wait to see whether a second tap follows.
        m_pendingFingers = fingers;
        m_pending->start();
    } else {
        run(Gesture(TwoFingerTap + fingers - 2));
    }
}

void IkTouchGestures::run(Gesture gesture)
{
    const QString f = assignedFunction(gesture);
    IkInterfaceManager *m = m_manager;
    if (f == QLatin1String("undo")) m->triggerAction(QStringLiteral("edit_undo"));
    else if (f == QLatin1String("redo")) m->triggerAction(QStringLiteral("edit_redo"));
    else if (f == QLatin1String("flip_canvas")) m->triggerAction(QStringLiteral("mirror_canvas"));
    else if (f == QLatin1String("fit_canvas")) m->triggerAction(QStringLiteral("zoom_to_fit"));
    else if (f == QLatin1String("reset_view")) m->triggerAction(QStringLiteral("reset_display"));
    else if (f == QLatin1String("hide_interface")) m->triggerAction(QStringLiteral("view_show_canvas_only"));
    else if (f == QLatin1String("toggle_erase"))
        m->setPaintMode(m->paintMode() == IkInterfaceManager::Erase ? IkInterfaceManager::Draw : IkInterfaceManager::Erase);
    else if (f == QLatin1String("color_sampler")) m->activateTool(QStringLiteral("KritaSelected/KisToolColorSampler"));
    else if (f == QLatin1String("brush_library")) m->showBrushLibrary();
    else if (f == QLatin1String("symmetry_off")) m->setSymmetry(IkInterfaceManager::SymmetryOff);
    else if (f == QLatin1String("deselect")) m->triggerAction(QStringLiteral("deselect"));
    else if (f == QLatin1String("save")) m->triggerAction(QStringLiteral("file_save"));
}
