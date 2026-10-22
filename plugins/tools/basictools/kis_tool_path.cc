/*
 *  SPDX-FileCopyrightText: 2007 Sven Langkamp <sven.langkamp@gmail.com>
 *  SPDX-FileCopyrightText: 2010 Cyrille Berger <cberger@cberger.net>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "kis_tool_path.h"
#include <KoPathShape.h>
#include <KoPathPoint.h>
#include <KoViewConverter.h>
#include <QPainter>
#include <QPainterPath>
#include <KoCanvasBase.h>
#include <kis_cursor.h>
#include <KisViewManager.h>
#include <canvas/kis_canvas2.h>
#include <kis_canvas_resource_provider.h>


KisToolPath::KisToolPath(KoCanvasBase * canvas)
    : DelegatedPathTool(canvas, Qt::ArrowCursor,
                        new __KisToolPathLocalTool(canvas, this))
{
    setIsOpacityPresetMode(true);
    KisCanvas2 *kritaCanvas = dynamic_cast<KisCanvas2*>(canvas);

    connect(kritaCanvas->viewManager()->canvasResourceProvider(), SIGNAL(sigEffectiveCompositeOpChanged()), SLOT(resetCursorStyle()));

}

void KisToolPath::resetCursorStyle()
{
    if (isEraser() && (nodePaintAbility() == PAINT)) {
        useCursor(KisCursor::eraserCursor());
    } else {
        DelegatedPathTool::resetCursorStyle();
    }

    overrideCursorIfNotEditable();
}

void KisToolPath::requestStrokeEnd()
{
    localTool()->endPathWithoutLastPoint();
}

void KisToolPath::requestStrokeCancellation()
{
    localTool()->cancelPath();
}

KisPopupWidgetInterface* KisToolPath::popupWidget()
{
    return localTool()->pathStarted() ? nullptr : DelegatedPathTool::popupWidget();
}

void KisToolPath::mousePressEvent(KoPointerEvent *event)
{
    Q_UNUSED(event)
}

// Install an event filter to catch right-click events.
// The simplest way to accommodate the popup palette binding.
// This code is duplicated in kis_tool_select_path.cc
bool KisToolPath::eventFilter(QObject *obj, QEvent *event)
{
    Q_UNUSED(obj);
    if (!localTool()->pathStarted()) {
        return false;
    }
    if (event->type() == QEvent::MouseButtonPress ||
            event->type() == QEvent::MouseButtonDblClick) {
        QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::RightButton) {
            localTool()->removeLastPoint();
            return true;
        }
    } else if (event->type() == QEvent::TabletPress) {
        QTabletEvent *tabletEvent = static_cast<QTabletEvent*>(event);
        if (tabletEvent->button() == Qt::RightButton) {
            localTool()->removeLastPoint();
            return true;
        }
    }
    return false;
}

void KisToolPath::beginAlternateAction(KoPointerEvent *event, AlternateAction action) {
    DelegatedPathTool::beginAlternateAction(event, action);
    if (!nodeEditable()) return;

    if (nodePaintAbility() == KisToolPath::MYPAINTBRUSH_UNPAINTABLE) {
        KisCanvas2 * kiscanvas = static_cast<KisCanvas2*>(canvas());
        QString message = i18n("The MyPaint Brush Engine is not available for this colorspace");
        kiscanvas->viewManager()->showFloatingMessage(message, koIcon("object-locked"));
        event->ignore();
        return;
    }
}

void KisToolPath::beginPrimaryAction(KoPointerEvent* event)
{
    if (!nodeEditable()) return;
    m_buttonPress = false;
    if (localTool()->pathStarted()) {
        // IKriate: the round buttons next to the last point.
        const QPointF viewPoint = canvas()->viewConverter()->documentToView(event->point);
        switch (localTool()->buttonAt(viewPoint)) {
        case __KisToolPathLocalTool::FinishButton:
            m_buttonPress = true;
            localTool()->endPathWithoutLastPoint();
            return;
        case __KisToolPathLocalTool::CloseButton:
            m_buttonPress = true;
            localTool()->closeAndEndPath();
            return;
        default:
            break;
        }
    }
    DelegatedPathTool::mousePressEvent(event);
}

void KisToolPath::continuePrimaryAction(KoPointerEvent *event)
{
    if (m_buttonPress) return;
    mouseMoveEvent(event);
}

void KisToolPath::endPrimaryAction(KoPointerEvent *event)
{
    if (m_buttonPress) {
        m_buttonPress = false;
        return;
    }
    mouseReleaseEvent(event);
}

void KisToolPath::beginPrimaryDoubleClickAction(KoPointerEvent *event)
{
    DelegatedPathTool::mouseDoubleClickEvent(event);
}

QList<QPointer<QWidget> > KisToolPath::createOptionWidgets()
{
    QList<QPointer<QWidget> > widgets = DelegatedPathTool::createOptionWidgets();
    return widgets;
}


__KisToolPathLocalTool::__KisToolPathLocalTool(KoCanvasBase * canvas, KisToolPath* parentTool)
    : KoCreatePathTool(canvas)
    , m_parentTool(parentTool) {
    setIsOpacityPresetMode(true);
}

void __KisToolPathLocalTool::paintPath(KoPathShape &pathShape, QPainter &painter, const KoViewConverter &converter)
{
    Q_UNUSED(converter);

    QTransform matrix;
    matrix.scale(m_parentTool->image()->xRes(), m_parentTool->image()->yRes());
    matrix.translate(pathShape.position().x(), pathShape.position().y());
    const QPainterPath viewPath = m_parentTool->pixelToView(matrix.map(pathShape.outline()));
    // IKriate: the curve as the brush will paint it, then its outline and
    // the points along it.
    m_parentTool->paintBrushPreview(&painter, viewPath);
    m_parentTool->paintToolOutline(&painter, viewPath);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(150, 165, 190), 2.0));
    painter.setBrush(Qt::white);
    KoPathPoint *active = activePathPoint();
    const int count = pathShape.pointCount();
    for (int i = 1; i < count; ++i) {
        KoPathPoint *p = pathShape.pointByIndex(KoPathPointIndex(0, i));
        if (!p || p == active) continue;
        const QPointF v = m_parentTool->pixelToView(matrix.map(p->point()));
        painter.drawEllipse(v, 6.5, 6.5);
    }
    painter.restore();
}

void __KisToolPathLocalTool::paint(QPainter &painter, const KoViewConverter &converter)
{
    KoCreatePathTool::paint(painter, converter);

    m_buttonsShown = false;
    KoPathShape *shape = pathInProgress();
    if (!pathStarted() || !shape || shape->pointCount() < 2) return;
    // Next to the last placed point (the active one follows the pointer).
    KoPathPoint *last = shape->pointByIndex(KoPathPointIndex(0, shape->pointCount() - 2));
    if (!last) return;
    const QPointF anchor = converter.documentToView(shape->shapeToDocument(last->point()));
    m_closeCentre = anchor + QPointF(38, -38);
    m_finishCentre = anchor + QPointF(76, -10);
    m_buttonsShown = true;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    const qreal r = 18;
    for (const QPointF &c : {m_closeCentre, m_finishCentre}) {
        painter.setPen(QPen(QColor(150, 165, 190), 3.0));
        painter.setBrush(QColor(250, 250, 252));
        painter.drawEllipse(c, r, r);
    }
    const QColor ink(40, 40, 52);
    painter.setPen(QPen(ink, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    // Close: a ring with a dot (back to the first point).
    painter.drawEllipse(m_closeCentre, 8, 8);
    painter.setBrush(ink);
    painter.drawEllipse(m_closeCentre, 2.5, 2.5);
    // Finish: a check mark.
    painter.setBrush(Qt::NoBrush);
    QPainterPath check;
    check.moveTo(m_finishCentre + QPointF(-7, 0));
    check.lineTo(m_finishCentre + QPointF(-2, 5));
    check.lineTo(m_finishCentre + QPointF(7, -5));
    painter.drawPath(check);
    painter.restore();
}

QRectF __KisToolPathLocalTool::decorationsRect() const
{
    QRectF rect = KoCreatePathTool::decorationsRect();
    if (m_buttonsShown) {
        QRectF buttons = QRectF(m_closeCentre, QSizeF()).united(QRectF(m_finishCentre, QSizeF())).adjusted(-30, -30, 30, 30);
        rect |= canvas()->viewConverter()->viewToDocument(buttons);
    }
    return rect;
}

__KisToolPathLocalTool::Button __KisToolPathLocalTool::buttonAt(const QPointF &viewPoint) const
{
    if (!m_buttonsShown) return NoButton;
    const qreal reach = 24;
    if (QLineF(viewPoint, m_finishCentre).length() <= reach) return FinishButton;
    if (QLineF(viewPoint, m_closeCentre).length() <= reach) return CloseButton;
    return NoButton;
}

void __KisToolPathLocalTool::addPathShape(KoPathShape* pathShape)
{
    if (!KoCreatePathTool::tryMergeInPathShape(pathShape)) {
        m_parentTool->addPathShape(pathShape, kundo2_i18n("Draw Bezier Curve"));
    }
}
