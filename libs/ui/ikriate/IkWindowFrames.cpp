/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "IkWindowFrames.h"

#include <QApplication>
#include <QChildEvent>
#include <QDialog>
#include <QEvent>
#include <QMainWindow>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QRegion>
#include <QTimer>
#include <QWidget>

#include "IkStyle.h"

namespace
{
const char kOverlayName[] = "ikWindowFrame";

/// Draws the outline; sits above the window's other children and lets all
/// input through.
class IkFrameOverlay : public QWidget
{
public:
    explicit IkFrameOverlay(QWidget *window)
        : QWidget(window)
    {
        setObjectName(QLatin1String(kOverlayName));
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
        setFocusPolicy(Qt::NoFocus);
        setGeometry(window->rect());
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const qreal t = IkWindowFrames::thickness();
        const qreal r = IkWindowFrames::radiusFor(window());
        const QRectF outer = QRectF(rect());
        const QRectF line = outer.adjusted(t / 2, t / 2, -t / 2, -t / 2);

        // Where the platform cannot clip the window to its mask, fill the
        // corners outside the curve with the window colour instead.
        QPainterPath corners;
        corners.addRect(outer);
        QPainterPath inside;
        inside.addRoundedRect(outer, r, r);
        corners = corners.subtracted(inside);
        p.fillPath(corners, palette().color(QPalette::Window).darker(115));

        QPen pen(IkStyle::accentGradient(outer, palette()), t);
        pen.setJoinStyle(Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(line, qMax<qreal>(0, r - t / 2), qMax<qreal>(0, r - t / 2));
    }
};

IkFrameOverlay *overlayOf(QWidget *window)
{
    for (QObject *c : window->children()) {
        if (c->objectName() == QLatin1String(kOverlayName)) return static_cast<IkFrameOverlay *>(c);
    }
    return nullptr;
}
}

IkWindowFrames::IkWindowFrames(QObject *parent)
    : QObject(parent)
{
    qApp->installEventFilter(this);
}

int IkWindowFrames::thickness()
{
    return qMax(2, IkStyle::scaled(2));
}

int IkWindowFrames::radiusFor(const QWidget *window)
{
    const bool small = qobject_cast<const QMenu *>(window) || (window->windowFlags() & Qt::Popup) == Qt::Popup;
    const int r = IkStyle::scaled(small ? 10 : 14);
    // Never more than a quarter of the shorter side (thin lists, small pop-ups).
    return qMin(r, qMin(window->width(), window->height()) / 4);
}

bool IkWindowFrames::wantsFrame(const QWidget *w)
{
    if (!w || !w->isWindow()) return false;
    if (qobject_cast<const QMainWindow *>(w) || w->inherits("KisMainWindow")) return false;
    const Qt::WindowType type = w->windowType();
    if (type == Qt::ToolTip || type == Qt::SplashScreen || type == Qt::Desktop || type == Qt::Drawer) return false;
    if (w->inherits("QRubberBand") || w->inherits("QTipLabel") || w->inherits("QSplashScreen")) return false;
    // Windows that already shape themselves, and full-screen ones, stay as they are.
    if (w->testAttribute(Qt::WA_TranslucentBackground)) return false;
    if (w->isFullScreen() || w->isMaximized()) return false;
    if (w->property("ikNoWindowFrame").toBool()) return false;
    const bool dialog = qobject_cast<const QDialog *>(w) || type == Qt::Dialog || type == Qt::Sheet;
    const bool popup = qobject_cast<const QMenu *>(w) || type == Qt::Popup;
    const bool tool = type == Qt::Tool;
    return dialog || popup || tool;
}

void IkWindowFrames::decorate(QWidget *window)
{
    IkFrameOverlay *overlay = overlayOf(window);
    if (!overlay) overlay = new IkFrameOverlay(window);
    overlay->setGeometry(window->rect());
    overlay->show();
    overlay->raise();
    updateShape(window);
}

void IkWindowFrames::updateShape(QWidget *window)
{
    if (IkFrameOverlay *overlay = overlayOf(window)) {
        overlay->setGeometry(window->rect());
        overlay->raise();
        overlay->update();
    }
    const int r = radiusFor(window);
    QPainterPath path;
    path.addRoundedRect(QRectF(window->rect()), r, r);
    const QRegion shape(path.toFillPolygon().toPolygon());
    if (window->mask() != shape) window->setMask(shape);
}

bool IkWindowFrames::eventFilter(QObject *watched, QEvent *event)
{
    const QEvent::Type t = event->type();
    if (t != QEvent::Show && t != QEvent::Resize && t != QEvent::ChildAdded && t != QEvent::WindowStateChange) {
        return false;
    }
    if (!watched->isWidgetType()) return false;
    QWidget *w = static_cast<QWidget *>(watched);
    if (!w->isWindow()) return false;

    switch (t) {
    case QEvent::Show:
        if (wantsFrame(w)) decorate(w);
        break;
    case QEvent::Resize:
        if (overlayOf(w)) updateShape(w);
        break;
    case QEvent::WindowStateChange:
        if (IkFrameOverlay *overlay = overlayOf(w)) {
            if (!wantsFrame(w)) {
                overlay->hide();
                w->clearMask();
            } else {
                decorate(w);
            }
        }
        break;
    case QEvent::ChildAdded: {
        // Keep the outline above children added later.
        QChildEvent *ce = static_cast<QChildEvent *>(event);
        if (ce->child()->objectName() == QLatin1String(kOverlayName)) break;
        if (IkFrameOverlay *overlay = overlayOf(w)) {
            QPointer<IkFrameOverlay> guard = overlay;
            QTimer::singleShot(0, overlay, [guard]() {
                if (guard) guard->raise();
            });
        }
        break;
    }
    default:
        break;
    }
    return false;
}
