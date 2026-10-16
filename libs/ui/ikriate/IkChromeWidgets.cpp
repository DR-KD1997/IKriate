/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "IkChromeWidgets.h"

#include <QAction>
#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOption>
#include <QTabletEvent>
#include <QTouchEvent>
#include <QtMath>

#include <kis_icon_utils.h>

#include "IkSkinManager.h"

namespace IkChrome
{
int touchSize()
{
#ifdef Q_OS_ANDROID
    return 48;
#else
    return 40;
#endif
}

int panelRadius()
{
    return IkSkinManager::instance()->chromeRadius();
}

int panelOpacity()
{
    return IkSkinManager::instance()->panelOpacity();
}
}

namespace
{
QColor panelColor(const QPalette &pal)
{
    QColor c = pal.color(QPalette::Window);
    c.setAlpha(IkChrome::panelOpacity());
    return c;
}

QColor hoverColor(const QPalette &pal)
{
    QColor c = pal.color(QPalette::WindowText);
    c.setAlpha(28);
    return c;
}

QPointF touchPoint(QTouchEvent *e)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return e->points().isEmpty() ? QPointF() : e->points().first().position();
#else
    return e->touchPoints().isEmpty() ? QPointF() : e->touchPoints().first().pos();
#endif
}
}

// ---------------------------------------------------------------- IkPanel

IkPanel::IkPanel(QWidget *parent)
    : QFrame(parent)
{
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);
}

void IkPanel::setPill(bool pill)
{
    m_pill = pill;
    update();
}

void IkPanel::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal radius = m_pill ? qMin(r.width(), r.height()) / 2.0 : qreal(IkChrome::panelRadius());

    QColor border = palette().color(QPalette::WindowText);
    border.setAlpha(30);
    p.setPen(QPen(border, 1.0));
    p.setBrush(panelColor(palette()));
    p.drawRoundedRect(r, radius, radius);
}

// --------------------------------------------------------- IkChromeButton

IkChromeButton::IkChromeButton(QWidget *parent)
    : QToolButton(parent)
{
    setAutoRaise(true);
    setFocusPolicy(Qt::NoFocus);
    setToolButtonStyle(Qt::ToolButtonIconOnly);
    const int s = IkChrome::touchSize();
    setIconSize(QSize(s / 2 + 2, s / 2 + 2));
    setFixedSize(s, s);
}

IkChromeButton::IkChromeButton(const QString &iconName, const QString &toolTip, QWidget *parent)
    : IkChromeButton(parent)
{
    setIconName(iconName);
    setToolTip(toolTip);
    setAccessibleName(toolTip);
}

void IkChromeButton::setIconName(const QString &iconName)
{
    m_iconName = iconName;
    refreshIcon();
}

void IkChromeButton::refreshIcon()
{
    if (!m_iconName.isEmpty()) {
        setIcon(KisIconUtils::loadIcon(m_iconName));
    } else if (m_action) {
        setIcon(m_action->icon());
    }
}

void IkChromeButton::bindAction(QAction *action)
{
    if (m_action) {
        disconnect(m_action, nullptr, this, nullptr);
        disconnect(this, nullptr, m_action, nullptr);
    }
    m_action = action;
    if (!action) {
        setEnabled(false);
        return;
    }
    if (toolTip().isEmpty()) {
        setToolTip(action->toolTip().isEmpty() ? action->text().remove('&') : action->toolTip());
        setAccessibleName(toolTip());
    }
    connect(action, &QAction::changed, this, &IkChromeButton::syncFromAction);
    connect(action, &QAction::toggled, this, &IkChromeButton::syncFromAction);
    connect(this, &QToolButton::clicked, action, &QAction::trigger);
    syncFromAction();
}

void IkChromeButton::syncFromAction()
{
    if (!m_action) return;
    setEnabled(m_action->isEnabled());
    if (m_action->isCheckable()) {
        setActive(m_action->isChecked());
    }
    if (m_iconName.isEmpty()) {
        setIcon(m_action->icon());
    }
}

void IkChromeButton::setActive(bool active)
{
    if (m_active == active) return;
    m_active = active;
    update();
}

bool IkChromeButton::isActive() const
{
    return m_active;
}

QSize IkChromeButton::sizeHint() const
{
    const int s = IkChrome::touchSize();
    return QSize(s, s);
}

void IkChromeButton::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(2, 2, -2, -2);
    const qreal radius = qMin<qreal>(IkChrome::panelRadius() - 2, r.height() / 2.0);

    if (m_active || isChecked() || isDown()) {
        p.setPen(Qt::NoPen);
        p.setBrush(palette().color(QPalette::Highlight));
        p.drawRoundedRect(r, radius, radius);
    } else if (underMouse() && isEnabled()) {
        p.setPen(Qt::NoPen);
        p.setBrush(hoverColor(palette()));
        p.drawRoundedRect(r, radius, radius);
    }

    const QIcon::Mode mode = !isEnabled() ? QIcon::Disabled
                           : (m_active || isChecked()) ? QIcon::Selected
                           : QIcon::Normal;
    const QPixmap pm = icon().pixmap(iconSize(), mode);
    const QPointF topLeft = QRectF(rect()).center() - QPointF(iconSize().width(), iconSize().height()) / 2.0;
    p.drawPixmap(topLeft, pm);
}

void IkChromeButton::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange) {
        refreshIcon();
    }
    QToolButton::changeEvent(event);
}

// ------------------------------------------------------------ IkPillSlider

IkPillSlider::IkPillSlider(Qt::Orientation orientation, QWidget *parent)
    : QWidget(parent)
    , m_orientation(orientation)
{
    setAttribute(Qt::WA_AcceptTouchEvents);
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
}

qreal IkPillSlider::value() const
{
    return m_value;
}

void IkPillSlider::setValue(qreal value)
{
    value = qBound<qreal>(0.0, value, 1.0);
    if (qFuzzyCompare(value + 1.0, m_value + 1.0)) return;
    m_value = value;
    update();
}

void IkPillSlider::setLabel(const QString &label)
{
    m_label = label;
    setToolTip(label);
    setAccessibleName(label);
}

QSize IkPillSlider::sizeHint() const
{
    const int s = IkChrome::touchSize();
    return m_orientation == Qt::Vertical ? QSize(s - 8, 4 * s) : QSize(4 * s, s - 8);
}

QRectF IkPillSlider::trackRect() const
{
    return QRectF(rect()).adjusted(4, 4, -4, -4);
}

void IkPillSlider::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF track = trackRect();
    const qreal radius = (m_orientation == Qt::Vertical ? track.width() : track.height()) / 2.0;

    QColor groove = palette().color(QPalette::WindowText);
    groove.setAlpha(45);
    p.setPen(Qt::NoPen);
    p.setBrush(groove);
    p.drawRoundedRect(track, radius, radius);

    QRectF fill = track;
    if (m_orientation == Qt::Vertical) {
        fill.setTop(track.bottom() - qMax(2 * radius, track.height() * m_value));
    } else {
        fill.setWidth(qMax(2 * radius, track.width() * m_value));
    }
    p.setBrush(palette().color(QPalette::Highlight));
    p.drawRoundedRect(fill, radius, radius);

    if (m_dragging) {
        // Value read-out while dragging, drawn inside the track end.
        p.setPen(palette().color(QPalette::HighlightedText));
        QFont f = font();
        f.setPointSizeF(qMax<qreal>(6.0, f.pointSizeF() * 0.75));
        f.setBold(true);
        p.setFont(f);
        const QString txt = QString::number(qRound(m_value * 100));
        QRectF textRect = fill;
        if (m_orientation == Qt::Vertical) {
            textRect.setHeight(qMin(fill.height(), 2 * radius + 6));
        }
        p.drawText(textRect, Qt::AlignCenter, txt);
    }
}

void IkPillSlider::updateFromPosition(const QPointF &pos)
{
    const QRectF track = trackRect();
    qreal v = m_orientation == Qt::Vertical
            ? (track.bottom() - pos.y()) / track.height()
            : (pos.x() - track.left()) / track.width();
    v = qBound<qreal>(0.0, v, 1.0);
    if (!qFuzzyCompare(v + 1.0, m_value + 1.0)) {
        m_value = v;
        update();
        Q_EMIT valueEdited(m_value);
    }
}

void IkPillSlider::mousePressEvent(QMouseEvent *event)
{
    m_dragging = true;
    Q_EMIT interactionStarted();
    updateFromPosition(event->pos());
    event->accept();
}

void IkPillSlider::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging) {
        updateFromPosition(event->pos());
    }
    event->accept();
}

void IkPillSlider::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_dragging) {
        m_dragging = false;
        update();
        Q_EMIT interactionFinished();
    }
    event->accept();
}

void IkPillSlider::tabletEvent(QTabletEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QPointF pos = event->position();
#else
    const QPointF pos = event->posF();
#endif
    switch (event->type()) {
    case QEvent::TabletPress:
        m_dragging = true;
        Q_EMIT interactionStarted();
        updateFromPosition(pos);
        break;
    case QEvent::TabletMove:
        if (m_dragging) updateFromPosition(pos);
        break;
    case QEvent::TabletRelease:
        if (m_dragging) {
            m_dragging = false;
            update();
            Q_EMIT interactionFinished();
        }
        break;
    default:
        break;
    }
    event->accept();
}

bool IkPillSlider::event(QEvent *event)
{
    switch (event->type()) {
    case QEvent::TouchBegin: {
        m_dragging = true;
        Q_EMIT interactionStarted();
        updateFromPosition(touchPoint(static_cast<QTouchEvent *>(event)));
        event->accept();
        return true;
    }
    case QEvent::TouchUpdate:
        if (m_dragging) updateFromPosition(touchPoint(static_cast<QTouchEvent *>(event)));
        event->accept();
        return true;
    case QEvent::TouchEnd:
    case QEvent::TouchCancel:
        if (m_dragging) {
            m_dragging = false;
            update();
            Q_EMIT interactionFinished();
        }
        event->accept();
        return true;
    default:
        return QWidget::event(event);
    }
}

// ---------------------------------------------------------- IkSwatchButton

IkSwatchButton::IkSwatchButton(QWidget *parent)
    : QAbstractButton(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
}

void IkSwatchButton::setColor(const QColor &color)
{
    m_color = color;
    setToolTip(color.name());
    setAccessibleName(color.name());
    update();
}

QColor IkSwatchButton::color() const
{
    return m_color;
}

void IkSwatchButton::setCurrent(bool current)
{
    m_current = current;
    update();
}

void IkSwatchButton::setDiameter(int diameter)
{
    m_diameter = diameter;
    updateGeometry();
    update();
}

QSize IkSwatchButton::sizeHint() const
{
    const int s = qMax(m_diameter + 8, IkChrome::touchSize() - 8);
    return QSize(s, s);
}

void IkSwatchButton::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPointF c = QRectF(rect()).center();
    const qreal r = m_diameter / 2.0;

    if (m_current) {
        p.setPen(QPen(palette().color(QPalette::WindowText), 2.0));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(c, r + 3, r + 3);
    }
    QColor edge = palette().color(QPalette::WindowText);
    edge.setAlpha(60);
    p.setPen(QPen(edge, 1.0));
    p.setBrush(m_color);
    p.drawEllipse(c, r, r);
}

// ---------------------------------------------------------- IkPresetButton

IkPresetButton::IkPresetButton(QWidget *parent)
    : QAbstractButton(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
}

void IkPresetButton::setPreset(const QImage &image, const QString &name)
{
    m_image = image;
    setToolTip(name);
    setAccessibleName(name);
    update();
}

void IkPresetButton::setActive(bool active)
{
    m_active = active;
    update();
}

QSize IkPresetButton::sizeHint() const
{
    const int s = IkChrome::touchSize();
    return QSize(s + 8, s);
}

void IkPresetButton::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QRectF r = QRectF(rect()).adjusted(2, 2, -2, -2);
    const qreal radius = qMin<qreal>(IkChrome::panelRadius() - 4, 10);

    QPainterPath clip;
    clip.addRoundedRect(r, radius, radius);
    p.save();
    p.setClipPath(clip);
    p.fillRect(r, palette().color(QPalette::Base));
    if (!m_image.isNull()) {
        const QSizeF s = QSizeF(m_image.size()).scaled(r.size(), Qt::KeepAspectRatio);
        QRectF target(QPointF(), s);
        target.moveCenter(r.center());
        p.drawImage(target, m_image);
    }
    p.restore();

    if (m_active || isDown()) {
        p.setPen(QPen(palette().color(QPalette::Highlight), 2.5));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r, radius, radius);
    }
}

// ------------------------------------------------------------- IkSeparator

IkSeparator::IkSeparator(Qt::Orientation orientation, QWidget *parent)
    : QWidget(parent)
    , m_orientation(orientation)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
}

QSize IkSeparator::sizeHint() const
{
    const int s = IkChrome::touchSize() - 12;
    return m_orientation == Qt::Vertical ? QSize(9, s) : QSize(s, 9);
}

void IkSeparator::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    QColor c = palette().color(QPalette::WindowText);
    c.setAlpha(50);
    p.setPen(QPen(c, 1.0));
    const QRectF r = rect();
    if (m_orientation == Qt::Vertical) {
        p.drawLine(QPointF(r.center().x(), r.top() + 4), QPointF(r.center().x(), r.bottom() - 4));
    } else {
        p.drawLine(QPointF(r.left() + 4, r.center().y()), QPointF(r.right() - 4, r.center().y()));
    }
}
