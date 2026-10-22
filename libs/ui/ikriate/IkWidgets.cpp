/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "IkWidgets.h"

#include <QAction>
#include <QApplication>
#include <QDoubleValidator>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMenu>
#include <QScroller>
#include <QScrollArea>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QKeyEvent>
#include <QTabletEvent>
#include <QTimer>
#include <QTouchEvent>
#include <QtMath>
#include <QVBoxLayout>

#include "IkSkinManager.h"

namespace
{
QPointF eventPos(QMouseEvent *e)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return e->position();
#else
    return e->localPos();
#endif
}

QPointF eventPos(QTabletEvent *e)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return e->position();
#else
    return e->posF();
#endif
}

bool touchPoint(QEvent *event, QPointF *pos)
{
    QTouchEvent *te = static_cast<QTouchEvent *>(event);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    if (te->points().isEmpty()) return false;
    *pos = te->points().first().position();
#else
    if (te->touchPoints().isEmpty()) return false;
    *pos = te->touchPoints().first().pos();
#endif
    return true;
}

QPainterPath roundedPath(const QRectF &r, qreal radius, int corners)
{
    QPainterPath path;
    const qreal d = 2 * radius;
    auto rad = [&](int c) { return (corners & c) ? radius : 0.0; };
    const qreal tl = rad(IkPanel::TopLeft), tr = rad(IkPanel::TopRight);
    const qreal bl = rad(IkPanel::BottomLeft), br = rad(IkPanel::BottomRight);
    Q_UNUSED(d);
    path.moveTo(r.left() + tl, r.top());
    path.lineTo(r.right() - tr, r.top());
    if (tr > 0) path.arcTo(QRectF(r.right() - 2 * tr, r.top(), 2 * tr, 2 * tr), 90, -90);
    path.lineTo(r.right(), r.bottom() - br);
    if (br > 0) path.arcTo(QRectF(r.right() - 2 * br, r.bottom() - 2 * br, 2 * br, 2 * br), 0, -90);
    path.lineTo(r.left() + bl, r.bottom());
    if (bl > 0) path.arcTo(QRectF(r.left(), r.bottom() - 2 * bl, 2 * bl, 2 * bl), 270, -90);
    path.lineTo(r.left(), r.top() + tl);
    if (tl > 0) path.arcTo(QRectF(r.left(), r.top(), 2 * tl, 2 * tl), 180, -90);
    path.closeSubpath();
    return path;
}
}

// =================================================================== IkPanel

IkPanel::IkPanel(QWidget *parent)
    : QFrame(parent)
{
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);
}

void IkPanel::setRole(IkStyle::Role role) { m_role = role; update(); }
void IkPanel::setRadius(int radius) { m_radius = radius; update(); }
void IkPanel::setPill(bool pill) { m_pill = pill; update(); }
void IkPanel::setTranslucent(bool translucent) { m_translucent = translucent; update(); }
void IkPanel::setShadow(bool shadow) { m_shadow = shadow; update(); }
void IkPanel::setBorder(bool border) { m_border = border; update(); }
void IkPanel::setRoundedCorners(int corners) { m_corners = corners; update(); }
void IkPanel::setPaintBackground(bool paint) { m_paintBackground = paint; update(); }

void IkPanel::paintEvent(QPaintEvent *)
{
    if (!m_paintBackground) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const int inset = m_shadow ? 6 : 0;
    QRectF r = QRectF(rect()).adjusted(inset, inset / 2.0, -inset, -inset * 1.5);
    qreal radius = m_radius >= 0 ? m_radius : IkStyle::radius();
    if (m_pill) radius = qMin(r.width(), r.height()) / 2.0;
    radius = qMin(radius, qMin(r.width(), r.height()) / 2.0);

    if (m_shadow) IkStyle::drawShadow(p, r, radius, 12, 90);

    QColor bg = IkStyle::color(m_role, palette());
    if (m_translucent) bg.setAlpha(qMin(bg.alpha(), IkSkinManager::instance()->panelOpacity()));
    p.setPen(m_border ? QPen(IkStyle::color(IkStyle::Divider, palette()), 1.0) : Qt::NoPen);
    p.setBrush(bg);
    p.drawPath(roundedPath(r.adjusted(0.5, 0.5, -0.5, -0.5), radius, m_corners));
    if (m_accentOutline) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(IkStyle::accentGradient(r, palette()), 2.0));
        p.drawPath(roundedPath(r.adjusted(1, 1, -1, -1), qMax<qreal>(0, radius - 1), m_corners));
    }
}

void IkPanel::setAccentOutline(bool outline)
{
    m_accentOutline = outline;
    update();
}

// ============================================================== IkIconButton

IkIconButton::IkIconButton(QWidget *parent)
    : QAbstractButton(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover);
}

IkIconButton::IkIconButton(const QString &iconName, const QString &toolTip, QWidget *parent)
    : IkIconButton(parent)
{
    setIconName(iconName);
    setToolTip(toolTip);
    setAccessibleName(toolTip);
}

void IkIconButton::setIconName(const QString &name) { m_iconName = name; update(); }
QString IkIconButton::iconName() const { return m_iconName; }
void IkIconButton::setVariant(Variant v) { m_variant = v; updateGeometry(); update(); }
bool IkIconButton::isActive() const { return m_active; }
void IkIconButton::setIconPixelSize(int px) { m_iconPx = px; update(); }
void IkIconButton::setKeepIconColors(bool keep) { m_keepColors = keep; update(); }
QAction *IkIconButton::boundAction() const { return m_action; }

void IkIconButton::setActive(bool active)
{
    if (m_active == active) return;
    m_active = active;
    update();
}

void IkIconButton::setEdge(int px)
{
    m_edge = px;
    if (px > 0) {
        setFixedSize(px, px);
    } else {
        // Back to the natural size (sizeHint).
        setMinimumSize(0, 0);
        setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        updateGeometry();
    }
}

void IkIconButton::bindAction(QAction *action)
{
    if (m_action) {
        disconnect(m_action, nullptr, this, nullptr);
        disconnect(this, &QAbstractButton::clicked, m_action, nullptr);
    }
    m_action = action;
    if (!action) {
        setEnabled(false);
        return;
    }
    if (toolTip().isEmpty()) {
        const QString tip = action->toolTip().isEmpty() ? action->text().remove('&') : action->toolTip();
        setToolTip(tip);
        setAccessibleName(tip);
    }
    connect(action, &QAction::changed, this, &IkIconButton::syncFromAction);
    connect(action, &QAction::toggled, this, &IkIconButton::syncFromAction);
    connect(this, &QAbstractButton::clicked, action, [action]() { action->trigger(); });
    syncFromAction();
}

void IkIconButton::syncFromAction()
{
    if (!m_action) return;
    setEnabled(m_action->isEnabled());
    if (m_action->isCheckable()) setActive(m_action->isChecked());
}

QSize IkIconButton::sizeHint() const
{
    if (m_edge > 0) return QSize(m_edge, m_edge);
    const int s = IkStyle::touchSize();
    if (m_variant == Fab) return QSize(s * 2, s * 2);
    if (m_variant == Round) return QSize(s + 8, s + 8);
    return QSize(s, s);
}

void IkIconButton::enterEvent(
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QEnterEvent *event
#else
    QEvent *event
#endif
    )
{
    m_hover = true;
    update();
    QAbstractButton::enterEvent(event);
}

void IkIconButton::leaveEvent(QEvent *event)
{
    m_hover = false;
    update();
    QAbstractButton::leaveEvent(event);
}

void IkIconButton::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette pal = palette();
    const bool on = m_active || isChecked();
    const bool pressed = isDown();
    QRectF r = QRectF(rect());

    QColor iconColor = IkStyle::color(IkStyle::Text, pal);
    switch (m_variant) {
    case Ghost: {
        const QRectF b = r.adjusted(3, 3, -3, -3);
        const qreal rad = qMin<qreal>(b.height() / 2.0, IkStyle::radius() * 0.75);
        if (on) {
            p.setPen(Qt::NoPen);
            p.setBrush(IkStyle::color(IkStyle::Accent, pal));
            p.drawRoundedRect(b, rad, rad);
            iconColor = IkStyle::color(IkStyle::AccentText, pal);
        } else if ((m_hover || pressed) && isEnabled()) {
            p.setPen(Qt::NoPen);
            p.setBrush(IkStyle::withAlpha(IkStyle::color(IkStyle::Text, pal), pressed ? 46 : 26));
            p.drawRoundedRect(b, rad, rad);
        }
        break;
    }
    case Round: {
        const qreal d = qMin(r.width(), r.height()) - 2;
        const QRectF c(r.center().x() - d / 2, r.center().y() - d / 2, d, d);
        p.setPen(Qt::NoPen);
        p.setBrush(on ? IkStyle::color(IkStyle::Accent, pal)
                      : (m_hover || pressed) ? IkStyle::color(IkStyle::RaisedHover, pal)
                                             : IkStyle::color(IkStyle::Surface, pal));
        p.drawEllipse(c);
        if (on) iconColor = IkStyle::color(IkStyle::AccentText, pal);
        break;
    }
    case Tile: {
        const QRectF b = r.adjusted(1, 1, -1, -1);
        const qreal rad = qMin<qreal>(8, b.height() / 4);
        p.setPen(Qt::NoPen);
        p.setBrush(on ? IkStyle::color(IkStyle::Accent, pal)
                      : (m_hover || pressed) ? IkStyle::color(IkStyle::RaisedHover, pal)
                                             : IkStyle::color(IkStyle::Raised, pal));
        p.drawRoundedRect(b, rad, rad);
        if (on) iconColor = IkStyle::color(IkStyle::AccentText, pal);
        break;
    }
    case Accent: {
        const QRectF b = r.adjusted(3, 3, -3, -3);
        const qreal rad = qMin<qreal>(b.height() / 2.0, IkStyle::radius() * 0.75);
        p.setPen(Qt::NoPen);
        QColor a = IkStyle::color(IkStyle::Accent, pal);
        if (pressed) a = a.darker(115);
        p.setBrush(a);
        p.drawRoundedRect(b, rad, rad);
        iconColor = IkStyle::color(IkStyle::AccentText, pal);
        break;
    }
    case Fab: {
        const qreal d = qMin(r.width(), r.height()) - 10;
        const QRectF c(r.center().x() - d / 2, r.center().y() - d / 2, d, d);
        // Glow
        for (int i = 10; i > 0; i -= 2) {
            QColor g = IkStyle::color(IkStyle::Accent, pal);
            g.setAlpha(10 + (10 - i) * 4);
            p.setPen(Qt::NoPen);
            p.setBrush(g);
            p.drawEllipse(c.adjusted(-i / 2.0, -i / 2.0, i / 2.0, i / 2.0));
        }
        p.setBrush(IkStyle::accentGradient(c, pal));
        p.drawEllipse(c);
        if (pressed) {
            p.setBrush(QColor(0, 0, 0, 40));
            p.drawEllipse(c);
        }
        iconColor = Qt::white;
        break;
    }
    }

    if (!isEnabled()) iconColor.setAlphaF(iconColor.alphaF() * 0.35);
    if (!m_iconName.isEmpty()) {
        int px = m_iconPx;
        if (px <= 0) {
            px = m_variant == Fab ? qRound(qMin(r.width(), r.height()) * 0.38)
                                  : qRound(qMin(r.width(), r.height()) * 0.5);
        }
        const qreal dpr = devicePixelRatioF();
        QPixmap pm;
        if (m_keepColors && IkStyle::hasIcon(m_iconName)) {
            pm = IkStyle::pixmap(m_iconName, px, iconColor, dpr); // colours in the SVG stay
        } else {
            pm = IkStyle::pixmap(m_iconName, px, iconColor, dpr);
        }
        const QPointF tl = r.center() - QPointF(px, px) / 2.0;
        p.drawPixmap(QRectF(tl, QSizeF(px, px)), pm, QRectF(pm.rect()));
    } else if (!icon().isNull()) {
        const int px = m_iconPx > 0 ? m_iconPx : qRound(qMin(r.width(), r.height()) * 0.5);
        const QPixmap pm = icon().pixmap(QSize(px, px), isEnabled() ? (on ? QIcon::Selected : QIcon::Normal) : QIcon::Disabled);
        p.drawPixmap(QRectF(r.center() - QPointF(px, px) / 2.0, QSizeF(px, px)), pm, QRectF(pm.rect()));
    }
}

// ================================================================= IkSlider

IkSlider::IkSlider(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_AcceptTouchEvents);
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

qreal IkSlider::value() const { return m_value; }

void IkSlider::setValue(qreal v)
{
    v = qBound<qreal>(0.0, v, 1.0);
    if (qFuzzyCompare(1.0 + v, 1.0 + m_value)) return;
    m_value = v;
    update();
}

QSize IkSlider::sizeHint() const
{
    return QSize(220, IkStyle::touchSize() - 8);
}

QRectF IkSlider::track() const
{
    const qreal thumb = height() * 0.62;
    return QRectF(thumb / 2 + 2, height() / 2.0 - 3, width() - thumb - 4, 6);
}

void IkSlider::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette pal = palette();
    const QRectF t = track();
    p.setPen(Qt::NoPen);
    p.setBrush(IkStyle::color(IkStyle::Raised, pal));
    p.drawRoundedRect(t, 3, 3);
    QRectF fill = t;
    fill.setWidth(t.width() * m_value);
    p.setBrush(IkStyle::color(IkStyle::Accent, pal));
    p.drawRoundedRect(fill, 3, 3);
    const qreal d = height() * 0.62;
    const QPointF c(t.left() + t.width() * m_value, height() / 2.0);
    p.setBrush(QColor(0, 0, 0, 60));
    p.drawEllipse(c + QPointF(0, 1.5), d / 2, d / 2);
    p.setBrush(QColor(0xF4, 0xF2, 0xEE));
    p.drawEllipse(c, d / 2, d / 2);
}

void IkSlider::setFromPos(qreal x)
{
    const QRectF t = track();
    const qreal v = qBound<qreal>(0.0, (x - t.left()) / t.width(), 1.0);
    if (!qFuzzyCompare(1.0 + v, 1.0 + m_value)) {
        m_value = v;
        update();
        Q_EMIT valueEdited(v);
    }
}

void IkSlider::mousePressEvent(QMouseEvent *e) { m_drag = true; Q_EMIT interactionStarted(); setFromPos(eventPos(e).x()); }
void IkSlider::mouseMoveEvent(QMouseEvent *e) { if (m_drag) setFromPos(eventPos(e).x()); }
void IkSlider::mouseReleaseEvent(QMouseEvent *) { if (m_drag) { m_drag = false; Q_EMIT interactionFinished(); } }

void IkSlider::tabletEvent(QTabletEvent *e)
{
    switch (e->type()) {
    case QEvent::TabletPress: m_drag = true; Q_EMIT interactionStarted(); setFromPos(eventPos(e).x()); break;
    case QEvent::TabletMove: if (m_drag) setFromPos(eventPos(e).x()); break;
    case QEvent::TabletRelease: if (m_drag) { m_drag = false; Q_EMIT interactionFinished(); } break;
    default: break;
    }
    e->accept();
}

bool IkSlider::event(QEvent *e)
{
    QPointF pos;
    switch (e->type()) {
    case QEvent::TouchBegin:
        if (touchPoint(e, &pos)) { m_drag = true; Q_EMIT interactionStarted(); setFromPos(pos.x()); }
        e->accept();
        return true;
    case QEvent::TouchUpdate:
        if (m_drag && touchPoint(e, &pos)) setFromPos(pos.x());
        e->accept();
        return true;
    case QEvent::TouchEnd:
    case QEvent::TouchCancel:
        if (m_drag) { m_drag = false; Q_EMIT interactionFinished(); }
        e->accept();
        return true;
    default:
        return QWidget::event(e);
    }
}

// ============================================================= IkPillSlider

IkPillSlider::IkPillSlider(Qt::Orientation orientation, QWidget *parent)
    : QWidget(parent)
    , m_orientation(orientation)
{
    setAttribute(Qt::WA_AcceptTouchEvents);
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
    m_readout = [](qreal v) { return QString::number(qRound(v * 100)); };
}

qreal IkPillSlider::value() const { return m_value; }

void IkPillSlider::setValue(qreal value)
{
    value = qBound<qreal>(0.0, value, 1.0);
    if (qFuzzyCompare(value + 1.0, m_value + 1.0)) return;
    m_value = value;
    update();
}

void IkPillSlider::setLabel(const QString &label)
{
    setToolTip(label);
    setAccessibleName(label);
}

void IkPillSlider::setReadoutFunction(std::function<QString(qreal)> fn)
{
    m_readout = fn;
}

QSize IkPillSlider::sizeHint() const
{
    const int s = IkStyle::touchSize();
    return m_orientation == Qt::Vertical ? QSize(s - 2, int(s * 5.5)) : QSize(int(s * 5.5), s - 2);
}

QRectF IkPillSlider::trackRect() const
{
    return QRectF(rect()).adjusted(5, 5, -5, -5);
}

void IkPillSlider::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette pal = palette();
    const QRectF track = trackRect();
    const qreal radius = (m_orientation == Qt::Vertical ? track.width() : track.height()) / 2.0;

    p.setPen(Qt::NoPen);
    p.setBrush(IkStyle::mix(IkStyle::color(IkStyle::Raised, pal), IkStyle::color(IkStyle::Text, pal), 0.08));
    p.drawRoundedRect(track, radius, radius);

    QRectF fill = track;
    if (m_orientation == Qt::Vertical) {
        fill.setTop(track.bottom() - qMax(2 * radius, track.height() * m_value));
    } else {
        fill.setWidth(qMax(2 * radius, track.width() * m_value));
    }
    p.setBrush(IkStyle::color(IkStyle::Accent, pal));
    p.drawRoundedRect(fill, radius, radius);

    if (m_dragging && m_readout) {
        p.setPen(IkStyle::color(IkStyle::AccentText, pal));
        p.setFont(IkStyle::font(0.8, QFont::Bold));
        QRectF textRect = fill;
        if (m_orientation == Qt::Vertical) textRect.setHeight(qMin(fill.height(), 2 * radius + 8));
        p.drawText(textRect, Qt::AlignCenter, m_readout(m_value));
    }
}

void IkPillSlider::updateFromPosition(const QPointF &pos)
{
    // Relative dragging (like Procreate): the value follows the movement
    // from where the finger landed, so touching does not make it jump.
    const QRectF track = trackRect();
    const qreal delta = m_orientation == Qt::Vertical ? (m_pressPos.y() - pos.y()) / track.height()
                                                      : (pos.x() - m_pressPos.x()) / track.width();
    const qreal v = qBound<qreal>(0.0, m_pressValue + delta, 1.0);
    if (!qFuzzyCompare(v + 1.0, m_value + 1.0)) {
        m_value = v;
        update();
        Q_EMIT valueEdited(m_value);
    }
}

void IkPillSlider::mousePressEvent(QMouseEvent *event)
{
    m_dragging = true;
    m_pressPos = eventPos(event);
    m_pressValue = m_value;
    update();
    Q_EMIT interactionStarted();
}

void IkPillSlider::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging) updateFromPosition(eventPos(event));
}

void IkPillSlider::mouseReleaseEvent(QMouseEvent *)
{
    if (m_dragging) {
        m_dragging = false;
        update();
        Q_EMIT interactionFinished();
    }
}

void IkPillSlider::tabletEvent(QTabletEvent *event)
{
    const QPointF pos = eventPos(event);
    switch (event->type()) {
    case QEvent::TabletPress:
        m_dragging = true;
        m_pressPos = pos;
        m_pressValue = m_value;
        update();
        Q_EMIT interactionStarted();
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
    QPointF pos;
    switch (event->type()) {
    case QEvent::TouchBegin:
        if (touchPoint(event, &pos)) {
            m_dragging = true;
            m_pressPos = pos;
            m_pressValue = m_value;
            update();
            Q_EMIT interactionStarted();
        }
        event->accept();
        return true;
    case QEvent::TouchUpdate:
        if (m_dragging && touchPoint(event, &pos)) updateFromPosition(pos);
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

// =============================================================== IkSegmented

IkSegmented::IkSegmented(QWidget *parent)
    : QWidget(parent)
{
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void IkSegmented::addSegment(const QString &iconName, const QString &text)
{
    m_segments << Segment{iconName, text};
    updateGeometry();
    update();
}

int IkSegmented::currentIndex() const { return m_current; }

void IkSegmented::setCurrentIndex(int index)
{
    m_current = index;
    update();
}

QSize IkSegmented::sizeHint() const
{
    QFontMetrics fm(IkStyle::font(1.15));
    int w = 0;
    for (const Segment &s : m_segments) w += fm.horizontalAdvance(s.text) + 80;
    return QSize(w, IkStyle::touchSize() + 6);
}

QRectF IkSegmented::segmentRect(int i) const
{
    const qreal w = qreal(width() - 2) / qMax(1, m_segments.size());
    return QRectF(1 + i * w, 1, w, height() - 2);
}

void IkSegmented::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette pal = palette();
    const QRectF outer = QRectF(rect()).adjusted(1, 1, -1, -1);
    const qreal rad = qMin<qreal>(10, outer.height() / 4);
    p.setPen(QPen(IkStyle::color(IkStyle::AccentDivider, pal), 2));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(outer, rad, rad);

    p.setFont(IkStyle::font(1.15));
    for (int i = 0; i < m_segments.size(); ++i) {
        const QRectF r = segmentRect(i);
        if (i == m_current) {
            QPainterPath clip;
            clip.addRoundedRect(outer, rad, rad);
            p.save();
            p.setClipPath(clip);
            p.fillRect(r, IkStyle::color(IkStyle::AccentSoft, pal));
            p.restore();
        }
        const Segment &s = m_segments[i];
        const int icon = qRound(r.height() * 0.5);
        QFontMetrics fm(p.font());
        const qreal tw = fm.horizontalAdvance(s.text);
        const qreal total = icon + 12 + tw;
        qreal x = r.center().x() - total / 2;
        const QColor c = IkStyle::color(i == m_current ? IkStyle::Text : IkStyle::MutedText, pal);
        p.drawPixmap(QRectF(x, r.center().y() - icon / 2.0, icon, icon),
                     IkStyle::pixmap(s.icon, icon, IkStyle::color(IkStyle::Accent, pal), devicePixelRatioF()),
                     QRectF(0, 0, icon * devicePixelRatioF(), icon * devicePixelRatioF()));
        x += icon + 12;
        p.setPen(c);
        p.drawText(QRectF(x, r.top(), tw + 2, r.height()), Qt::AlignVCenter | Qt::AlignLeft, s.text);
    }
}

void IkSegmented::mouseReleaseEvent(QMouseEvent *event)
{
    for (int i = 0; i < m_segments.size(); ++i) {
        if (segmentRect(i).contains(eventPos(event))) {
            if (i != m_current) {
                m_current = i;
                update();
                Q_EMIT currentChanged(i);
            }
            return;
        }
    }
}

// ================================================================== IkSwitch

IkSwitch::IkSwitch(QWidget *parent)
    : QAbstractButton(parent)
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
}

QSize IkSwitch::sizeHint() const
{
    return QSize(60, 32);
}

void IkSwitch::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette pal = palette();
    const QRectF r = QRectF(rect()).adjusted(1, 3, -1, -3);
    const qreal rad = r.height() / 2;
    const QColor accent = IkStyle::color(IkStyle::Accent, pal);
    p.setPen(Qt::NoPen);
    p.setBrush(isChecked() ? IkStyle::mix(accent, IkStyle::color(IkStyle::Background, pal), 0.55)
                           : IkStyle::mix(accent, IkStyle::color(IkStyle::Background, pal), 0.8));
    p.drawRoundedRect(r, rad, rad);
    const qreal d = r.height() - 4;
    const QPointF c(isChecked() ? r.right() - 2 - d / 2 : r.left() + 2 + d / 2, r.center().y());
    p.setBrush(isChecked() ? accent : IkStyle::mix(accent, IkStyle::color(IkStyle::Background, pal), 0.6));
    p.drawEllipse(c, d / 2, d / 2);
}

// ================================================================== IkTabBar

IkTabBar::IkTabBar(QWidget *parent)
    : QWidget(parent)
{
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void IkTabBar::setStyle(Style style) { m_style = style; update(); }
void IkTabBar::setExpanding(bool expanding) { m_expanding = expanding; update(); }
int IkTabBar::currentIndex() const { return m_current; }

int IkTabBar::addTab(const QString &iconName, const QString &text, const QString &toolTip)
{
    m_tabs << Tab{iconName, text, toolTip};
    updateGeometry();
    update();
    return m_tabs.size() - 1;
}

void IkTabBar::setCurrentIndex(int index)
{
    if (index < 0 || index >= m_tabs.size()) return;
    const bool changed = index != m_current;
    m_current = index;
    update();
    if (changed) Q_EMIT currentChanged(index);
}

qreal IkTabBar::tabWidth(int i) const
{
    QFontMetrics fm(IkStyle::font(1.1, QFont::DemiBold));
    const Tab &t = m_tabs[i];
    qreal w = 24;
    if (!t.icon.isEmpty()) w += height() * 0.5;
    if (!t.text.isEmpty()) w += fm.horizontalAdvance(t.text) + (t.icon.isEmpty() ? 0 : 8);
    return w;
}

QRectF IkTabBar::tabRect(int i) const
{
    if (m_expanding) {
        const qreal w = qreal(width()) / qMax(1, m_tabs.size());
        return QRectF(i * w, 0, w, height());
    }
    qreal x = 0;
    for (int k = 0; k < i; ++k) x += tabWidth(k) + 6;
    return QRectF(x, 0, tabWidth(i), height());
}

QSize IkTabBar::sizeHint() const
{
    qreal w = 0;
    for (int i = 0; i < m_tabs.size(); ++i) w += tabWidth(i) + 6;
    return QSize(qRound(w), IkStyle::touchSize() + 4);
}

void IkTabBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette pal = palette();
    p.setFont(IkStyle::font(1.1, QFont::DemiBold));
    for (int i = 0; i < m_tabs.size(); ++i) {
        const QRectF r = tabRect(i);
        const bool cur = i == m_current;
        const Tab &t = m_tabs[i];
        QColor fg = cur ? (m_style == Underline && t.text.isEmpty() ? IkStyle::color(IkStyle::Accent, pal)
                                                                    : IkStyle::color(IkStyle::Text, pal))
                        : IkStyle::color(IkStyle::MutedText, pal);
        if (m_style == Pills && cur) {
            p.setPen(Qt::NoPen);
            p.setBrush(IkStyle::color(IkStyle::Raised, pal));
            p.drawRoundedRect(r.adjusted(2, 4, -2, -4), 10, 10);
            fg = IkStyle::color(IkStyle::Text, pal);
        }
        const int icon = qRound(height() * 0.5);
        QFontMetrics fm(p.font());
        const qreal tw = t.text.isEmpty() ? 0 : fm.horizontalAdvance(t.text);
        const qreal total = (t.icon.isEmpty() ? 0 : icon) + (t.icon.isEmpty() || t.text.isEmpty() ? 0 : 8) + tw;
        qreal x = r.center().x() - total / 2;
        if (!t.icon.isEmpty()) {
            p.drawPixmap(QRectF(x, r.center().y() - icon / 2.0, icon, icon),
                         IkStyle::pixmap(t.icon, icon, fg, devicePixelRatioF()),
                         QRectF(0, 0, icon * devicePixelRatioF(), icon * devicePixelRatioF()));
            x += icon + 8;
        }
        if (!t.text.isEmpty()) {
            p.setPen(fg);
            p.drawText(QRectF(x, r.top(), tw + 2, r.height()), Qt::AlignVCenter | Qt::AlignLeft, t.text);
        }
        if (m_style == Underline && cur) {
            const qreal uw = qMax<qreal>(r.width() * 0.6, total + 24);
            QRectF u(r.center().x() - uw / 2, r.bottom() - 4, uw, 4);
            p.setPen(Qt::NoPen);
            p.setBrush(IkStyle::color(IkStyle::Accent, pal));
            p.drawRoundedRect(u, 2, 2);
        }
    }
}

void IkTabBar::mouseReleaseEvent(QMouseEvent *event)
{
    for (int i = 0; i < m_tabs.size(); ++i) {
        if (tabRect(i).contains(eventPos(event))) {
            setCurrentIndex(i);
            return;
        }
    }
}

// =============================================================== IkColorWell

IkColorWell::IkColorWell(const QString &label, QWidget *parent)
    : QAbstractButton(parent)
    , m_label(label)
{
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setAccessibleName(label);
}

void IkColorWell::setColor(const QColor &c) { m_color = c; update(); }
QColor IkColorWell::color() const { return m_color; }
void IkColorWell::setEmpty(bool empty) { m_empty = empty; update(); }

QSize IkColorWell::sizeHint() const
{
    QFontMetrics fm(IkStyle::font(1.6));
    return QSize(fm.horizontalAdvance(m_label) + 130, IkStyle::touchSize() + 30);
}

void IkColorWell::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette pal = palette();
    const QRectF r = QRectF(rect()).adjusted(1.5, 1.5, -1.5, -1.5);
    p.setPen(QPen(isDown() ? IkStyle::color(IkStyle::Accent, pal) : IkStyle::color(IkStyle::AccentDivider, pal), 2.2));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(r, 14, 14);
    p.setPen(IkStyle::color(IkStyle::Accent, pal));
    p.setFont(IkStyle::font(1.6));
    p.drawText(r.adjusted(20, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, m_label);
    const qreal d = r.height() - 22;
    const QRectF chip(r.right() - 26 - d, r.center().y() - d / 2, d, d);
    p.setPen(QPen(IkStyle::color(IkStyle::Text, pal), 1.2));
    p.setBrush(m_empty ? QColor(Qt::transparent) : m_color);
    p.drawEllipse(chip);
    if (m_empty) {
        p.drawLine(chip.bottomLeft() + QPointF(d * 0.15, -d * 0.15), chip.topRight() + QPointF(-d * 0.15, d * 0.15));
    }
}

// ================================================================== IkSwatch

IkSwatch::IkSwatch(QWidget *parent)
    : QAbstractButton(parent)
{
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
}

void IkSwatch::setColor(const QColor &color)
{
    m_color = color;
    setToolTip(color.name());
    setAccessibleName(color.name());
    update();
}

QColor IkSwatch::color() const { return m_color; }
void IkSwatch::setCurrent(bool current) { m_current = current; update(); }
void IkSwatch::setShape(bool roundedSquare) { m_square = roundedSquare; update(); }
void IkSwatch::setDiameter(int px) { m_diameter = px; updateGeometry(); update(); }

QSize IkSwatch::sizeHint() const
{
    return QSize(m_diameter + 8, m_diameter + 8);
}

void IkSwatch::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette pal = palette();
    const QRectF r = m_square ? QRectF(rect()).adjusted(3, 3, -3, -3)
                              : QRectF(QRectF(rect()).center() - QPointF(m_diameter, m_diameter) / 2.0,
                                       QSizeF(m_diameter, m_diameter));
    if (m_current) {
        p.setPen(QPen(IkStyle::color(IkStyle::Text, pal), 2));
        p.setBrush(Qt::NoBrush);
        if (m_square) p.drawRoundedRect(r.adjusted(-2, -2, 2, 2), 9, 9);
        else p.drawEllipse(r.adjusted(-3, -3, 3, 3));
    }
    p.setPen(QPen(IkStyle::color(IkStyle::Divider, pal), 1));
    p.setBrush(m_color);
    if (m_square) p.drawRoundedRect(r, 7, 7);
    else p.drawEllipse(r);
}

// =============================================================== IkChoiceRow

IkChoiceRow::IkChoiceRow(const QString &label, QWidget *parent)
    : QAbstractButton(parent)
    , m_label(label)
{
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setAccessibleName(label);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(this, &QAbstractButton::clicked, this, &IkChoiceRow::openMenu);
}

void IkChoiceRow::setChoices(const QStringList &choices, int current)
{
    m_choices = choices;
    setCurrentIndex(current);
}

void IkChoiceRow::setCurrentIndex(int index)
{
    m_current = index;
    m_value = (index >= 0 && index < m_choices.size()) ? m_choices[index] : QString();
    update();
}

int IkChoiceRow::currentIndex() const { return m_current; }
void IkChoiceRow::setValueText(const QString &text) { m_value = text; update(); }
void IkChoiceRow::setCentered(bool centered) { m_centered = centered; update(); }
void IkChoiceRow::setBordered(bool bordered) { m_bordered = bordered; update(); }

QSize IkChoiceRow::sizeHint() const
{
    QFontMetrics fm(IkStyle::font(1.15));
    return QSize(fm.horizontalAdvance(m_label + m_value) + 90, IkStyle::touchSize() + 4);
}

void IkChoiceRow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette pal = palette();
    const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
    const qreal rad = m_bordered ? 14 : r.height() / 2;
    if (m_bordered) {
        p.setPen(QPen(isDown() ? IkStyle::color(IkStyle::Accent, pal) : IkStyle::color(IkStyle::AccentDivider, pal), 2.2));
        p.setBrush(Qt::NoBrush);
    } else {
        p.setPen(Qt::NoPen);
        p.setBrush(isDown() ? IkStyle::color(IkStyle::RaisedHover, pal) : IkStyle::color(IkStyle::Raised, pal));
    }
    p.drawRoundedRect(r, rad, rad);

    const qreal arrow = r.height() * 0.32;
    const QPointF ac(r.right() - 16 - arrow / 2, r.center().y());
    QPainterPath tri;
    tri.moveTo(ac + QPointF(-arrow / 2, -arrow / 3));
    tri.lineTo(ac + QPointF(arrow / 2, -arrow / 3));
    tri.lineTo(ac + QPointF(0, arrow / 2.5));
    tri.closeSubpath();
    p.setPen(Qt::NoPen);
    p.setBrush(m_bordered ? IkStyle::color(IkStyle::Accent, pal) : IkStyle::color(IkStyle::Text, pal));
    p.drawPath(tri);

    p.setFont(m_bordered ? IkStyle::font(1.6) : IkStyle::font(1.1, QFont::DemiBold));
    if (m_centered) {
        p.setPen(IkStyle::color(IkStyle::Text, pal));
        p.drawText(r.adjusted(0, 0, -arrow - 10, 0), Qt::AlignCenter, m_value.isEmpty() ? m_label : m_value);
        return;
    }
    p.setPen(m_bordered ? IkStyle::color(IkStyle::Accent, pal) : IkStyle::color(IkStyle::Text, pal));
    p.drawText(r.adjusted(18, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, m_label);
    p.setPen(m_bordered ? IkStyle::color(IkStyle::Accent, pal) : IkStyle::color(IkStyle::Text, pal));
    p.drawText(r.adjusted(0, 0, -(arrow + 32), 0), Qt::AlignVCenter | Qt::AlignRight, m_value);
}

namespace
{
/// One choice in an IkChoiceRow's list: text, check mark when current.
class IkChoiceItem : public QAbstractButton
{
public:
    IkChoiceItem(const QString &text, bool current, QWidget *parent) : QAbstractButton(parent), m_current(current)
    {
        setText(text);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        setFocusPolicy(Qt::NoFocus);
        setMinimumHeight(IkStyle::touchFirst() ? IkStyle::touchSize() : IkStyle::scaled(38));
    }
    QSize sizeHint() const override
    {
        return QSize(QFontMetrics(IkStyle::font(1.05)).horizontalAdvance(text()) + IkStyle::scaled(64), minimumHeight());
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QPalette pal = palette();
        if (m_current || underMouse() || isDown()) {
            p.setPen(Qt::NoPen);
            p.setBrush(m_current ? IkStyle::color(IkStyle::AccentSoft, pal) : IkStyle::color(IkStyle::RaisedHover, pal));
            p.drawRoundedRect(QRectF(rect()).adjusted(2, 1, -2, -1), 10, 10);
        }
        p.setFont(IkStyle::font(1.05, m_current ? QFont::DemiBold : -1));
        p.setPen(m_current ? IkStyle::color(IkStyle::Accent, pal) : IkStyle::color(IkStyle::Text, pal));
        p.drawText(rect().adjusted(IkStyle::scaled(16), 0, -IkStyle::scaled(40), 0), Qt::AlignVCenter | Qt::AlignLeft, text());
        if (m_current) {
            const int s = IkStyle::scaled(18);
            p.drawPixmap(QRect(width() - s - IkStyle::scaled(14), (height() - s) / 2, s, s),
                         IkStyle::pixmap(QStringLiteral("check"), s, IkStyle::color(IkStyle::Accent, pal), devicePixelRatioF()));
        }
    }

private:
    bool m_current;
};
}

void IkChoiceRow::openMenu()
{
    if (m_choices.isEmpty()) {
        Q_EMIT chosen(-1);
        return;
    }
    // A styled list in a popover (instead of a plain QMenu).
    IkPopover *pop = new IkPopover(window());
    pop->setRadius(16);
    pop->setAccentOutline(true);
    connect(pop, &IkPopover::dismissed, pop, &QObject::deleteLater);
    QScrollArea *scroll = new QScrollArea(pop);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    IkStyle::makeTransparent(scroll);
    QWidget *list = new QWidget(scroll);
    QVBoxLayout *l = new QVBoxLayout(list);
    l->setContentsMargins(4, 4, 4, 4);
    l->setSpacing(2);
    int widest = width();
    QPointer<IkPopover> guard = pop;
    for (int i = 0; i < m_choices.size(); ++i) {
        IkChoiceItem *item = new IkChoiceItem(m_choices[i], i == m_current, list);
        widest = qMax(widest, item->sizeHint().width() + 40);
        connect(item, &QAbstractButton::clicked, this, [this, i, guard]() {
            if (guard) guard->dismiss();
            setCurrentIndex(i);
            Q_EMIT chosen(i);
        });
        l->addWidget(item);
    }
    scroll->setWidget(list);
    QScroller::grabGesture(scroll->viewport(), IkStyle::touchFirst() ? QScroller::LeftMouseButtonGesture : QScroller::TouchGesture);
    pop->setContent(scroll);
    const int rows = list->sizeHint().height() + 2 * 20 + 10;
    pop->popup(this, IkPopover::Below, QSize(qMin(widest, window()->width() - 40), qMin(rows, window()->height() * 2 / 3)));
}

// =============================================================== IkNumberRow

IkNumberRow::IkNumberRow(const QString &label, QWidget *parent)
    : QWidget(parent)
    , m_label(label)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_edit = new QLineEdit(this);
    m_edit->setFrame(false);
    m_edit->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_edit->setFont(IkStyle::font(1.1, QFont::DemiBold));
    {
        QPalette ep = m_edit->palette();
        ep.setColor(QPalette::Base, Qt::transparent);
        m_edit->setPalette(ep);
    }
    m_edit->setInputMethodHints(Qt::ImhFormattedNumbersOnly);
    m_edit->setAccessibleName(label);
    QHBoxLayout *l = new QHBoxLayout(this);
    QFontMetrics fm(IkStyle::font(1.1, QFont::DemiBold));
    l->setContentsMargins(fm.horizontalAdvance(label) + 30, 2, 18, 2);
    l->addWidget(m_edit);
    connect(m_edit, &QLineEdit::editingFinished, this, [this]() {
        bool ok = false;
        double v = m_edit->text().toDouble(&ok);
        if (!ok) v = m_min;
        v = qBound(m_min, v, m_max);
        m_edit->setText(QString::number(v, 'f', m_decimals));
        Q_EMIT valueChanged(v);
    });
}

void IkNumberRow::setRange(double min, double max, int decimals)
{
    m_min = min;
    m_max = max;
    m_decimals = decimals;
    m_edit->setValidator(new QDoubleValidator(min, max, decimals, m_edit));
}

double IkNumberRow::value() const
{
    return qBound(m_min, m_edit->text().toDouble(), m_max);
}

void IkNumberRow::setValue(double v)
{
    m_edit->setText(QString::number(qBound(m_min, v, m_max), 'f', m_decimals));
}

QLineEdit *IkNumberRow::editor() const { return m_edit; }

QSize IkNumberRow::sizeHint() const
{
    return QSize(220, IkStyle::touchSize() + 4);
}

void IkNumberRow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette pal = palette();
    const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
    p.setPen(m_edit->hasFocus() ? QPen(IkStyle::color(IkStyle::Accent, pal), 1.5) : Qt::NoPen);
    p.setBrush(IkStyle::color(IkStyle::Raised, pal));
    p.drawRoundedRect(r, r.height() / 2, r.height() / 2);
    p.setPen(IkStyle::color(IkStyle::Text, pal));
    p.setFont(IkStyle::font(1.1, QFont::DemiBold));
    p.drawText(r.adjusted(18, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, m_label);
}

// ============================================================ IkSectionHeader

IkSectionHeader::IkSectionHeader(const QString &text, QWidget *parent)
    : QWidget(parent)
    , m_text(text)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void IkSectionHeader::setLarge(bool large) { m_large = large; updateGeometry(); update(); }

QSize IkSectionHeader::sizeHint() const
{
    QFontMetrics fm(m_large ? IkStyle::titleFont() : IkStyle::sectionFont());
    return QSize(fm.horizontalAdvance(m_text) + 20, fm.height() + 18);
}

void IkSectionHeader::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette pal = palette();
    const QFont f = m_large ? IkStyle::titleFont() : IkStyle::sectionFont();
    p.setFont(f);
    QFontMetrics fm(f);
    const QColor accent = IkStyle::color(IkStyle::Accent, pal);
    p.setPen(accent);
    p.drawText(QRectF(0, 0, width(), height() - 8), Qt::AlignLeft | Qt::AlignVCenter, m_text);
    QLinearGradient g(0, 0, width(), 0);
    g.setColorAt(0.0, IkStyle::withAlpha(accent, 40));
    g.setColorAt(0.08, IkStyle::withAlpha(accent, 170));
    g.setColorAt(0.85, IkStyle::withAlpha(accent, 150));
    g.setColorAt(1.0, IkStyle::withAlpha(accent, 0));
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawRoundedRect(QRectF(0, height() - 5, width(), 3), 1.5, 1.5);
}

// ================================================================= IkNavRow

IkNavRow::IkNavRow(const QString &iconName, const QString &text, QWidget *parent)
    : QAbstractButton(parent)
    , m_icon(iconName)
{
    setText(text);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void IkNavRow::setSelected(bool selected) { m_selected = selected; update(); }

void IkNavRow::setTrailing(QWidget *widget)
{
    m_trailing = widget;
    widget->setParent(this);
    widget->show();
    resizeEvent(nullptr);
}

QSize IkNavRow::sizeHint() const
{
    return QSize(220, IkStyle::touchSize() + 6);
}

void IkNavRow::resizeEvent(QResizeEvent *)
{
    if (m_trailing) {
        const QSize s = m_trailing->sizeHint();
        m_trailing->setGeometry(width() - s.width() - 10, (height() - s.height()) / 2, s.width(), s.height());
    }
}

void IkNavRow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette pal = palette();
    const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
    if (m_selected || isDown()) {
        p.setPen(Qt::NoPen);
        p.setBrush(IkStyle::color(IkStyle::Raised, pal));
        p.drawRoundedRect(r, 6, 6);
    }
    const int icon = qRound(r.height() * 0.5);
    p.drawPixmap(QRectF(r.left() + 12, r.center().y() - icon / 2.0, icon, icon),
                 IkStyle::pixmap(m_icon, icon, IkStyle::color(IkStyle::Accent, pal), devicePixelRatioF()),
                 QRectF(0, 0, icon * devicePixelRatioF(), icon * devicePixelRatioF()));
    p.setPen(IkStyle::color(IkStyle::Text, pal));
    p.setFont(IkStyle::font(1.1));
    p.drawText(r.adjusted(icon + 30, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, text());
}

// ============================================================== IkTextButton

IkTextButton::IkTextButton(const QString &text, Variant variant, QWidget *parent)
    : QAbstractButton(parent)
    , m_variant(variant)
{
    setText(text);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
}

void IkTextButton::setFontScale(qreal scale, int weight)
{
    m_scale = scale;
    m_weight = weight;
    updateGeometry();
    update();
}

void IkTextButton::setRadius(int radius)
{
    m_radius = radius;
    update();
}

QSize IkTextButton::sizeHint() const
{
    QFontMetrics fm(IkStyle::font(m_scale, m_weight));
    const int pad = m_variant == Filled ? 36 : 16;
    return QSize(fm.horizontalAdvance(text()) + pad, qMax(fm.height() + (m_variant == Filled ? 30 : 12), IkStyle::touchSize()));
}

void IkTextButton::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette pal = palette();
    const QRectF r = QRectF(rect());
    if (m_variant == Filled) {
        p.setPen(Qt::NoPen);
        p.setBrush(isDown() ? IkStyle::color(IkStyle::RaisedHover, pal) : IkStyle::color(IkStyle::Raised, pal));
        p.drawRoundedRect(r, m_radius, m_radius);
    }
    QColor c = m_variant == Link ? IkStyle::color(IkStyle::MutedText, pal) : IkStyle::color(IkStyle::Text, pal);
    if (isDown()) c = IkStyle::color(IkStyle::Accent, pal);
    if (!isEnabled()) c.setAlphaF(0.4);
    p.setPen(c);
    p.setFont(IkStyle::font(m_scale, m_weight));
    p.drawText(r, (m_variant == Link ? Qt::AlignLeft : Qt::AlignHCenter) | Qt::AlignVCenter, text());
}

// ================================================================= IkDivider

IkDivider::IkDivider(QWidget *parent)
    : QWidget(parent)
{
    setFixedHeight(1);
    setAttribute(Qt::WA_TransparentForMouseEvents);
}

void IkDivider::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), IkStyle::color(IkStyle::Divider, palette()));
}

// =========================================================== IkSeparatorLine

IkSeparatorLine::IkSeparatorLine(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
}

QSize IkSeparatorLine::sizeHint() const
{
    return QSize(13, IkStyle::touchSize() - 12);
}

void IkSeparatorLine::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setPen(QPen(IkStyle::color(IkStyle::Divider, palette()), 1.5));
    p.drawLine(QPointF(width() / 2.0, 6), QPointF(width() / 2.0, height() - 6));
}

// ================================================================= IkPopover

namespace
{
/// Invisible layer under an open popover that closes it when tapped.
class IkScrim : public QWidget
{
public:
    explicit IkScrim(QWidget *parent) : QWidget(parent)
    {
        setAttribute(Qt::WA_NoSystemBackground);
    }
    std::function<void()> onTap;
    bool dim {false};

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (!dim) return;
        QPainter p(this);
        p.fillRect(rect(), QColor(0, 0, 0, 110));
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        e->accept();
        if (onTap) QTimer::singleShot(0, this, [this]() { if (onTap) onTap(); });
    }
    void tabletEvent(QTabletEvent *e) override
    {
        e->accept();
        if (e->type() == QEvent::TabletPress && onTap) QTimer::singleShot(0, this, [this]() { if (onTap) onTap(); });
    }
};
}

IkPopover::IkPopover(QWidget *window)
    : IkPanel(window)
    , m_window(window)
{
    setRole(IkStyle::Surface);
    setShadow(true);
    hide();
    IkScrim *scrim = new IkScrim(window);
    scrim->hide();
    scrim->onTap = [this]() { if (!m_modal) dismiss(); };
    m_scrim = scrim;
    window->installEventFilter(this);
    QVBoxLayout *l = new QVBoxLayout(this);
    // Room for the drawn shadow (see IkPanel::paintEvent).
    l->setContentsMargins(6 + 10, 3 + 10, 6 + 10, 9 + 10);
}

IkPopover::~IkPopover()
{
    delete m_scrim;
}

void IkPopover::raiseToTop()
{
    if (!isVisible()) return;
    m_scrim->raise();
    raise();
}

void IkPopover::setContent(QWidget *content)
{
    if (m_content == content) return;
    if (m_content) {
        layout()->removeWidget(m_content);
        m_content->setParent(nullptr);
    }
    m_content = content;
    if (content) {
        content->setParent(this);
        layout()->addWidget(content);
        content->show();
    }
}

QWidget *IkPopover::content() const { return m_content; }
void IkPopover::setModal(bool modal) { m_modal = modal; }

void IkPopover::setDimBackground(bool dim)
{
    m_dim = dim;
    static_cast<IkScrim *>(m_scrim)->dim = dim;
}

bool IkPopover::isOpen() const { return isVisible(); }

void IkPopover::popup(QWidget *anchor, Placement placement, const QSize &size)
{
    m_anchor = anchor;
    m_placement = anchor ? placement : Center;
    m_requested = size;
    m_scrim->setGeometry(m_window->rect());
    m_scrim->show();
    m_scrim->raise();
    show();
    raise();
    // Escape (Back on Android) closes the popover. Watched application-wide
    // while open, because the canvas and tools also bind Escape.
    qApp->installEventFilter(this);
    place();
}

void IkPopover::place()
{
    if (!m_window) return;
    const QRect win = m_window->rect().adjusted(8, 8, -8, -8);
    QSize s = m_requested.isValid() ? m_requested : sizeHint();
    s = s.boundedTo(win.size());
    QRect r(QPoint(), s);
    if (m_anchor && m_placement != Center) {
        const QRect a(m_anchor->mapTo(m_window, QPoint(0, 0)), m_anchor->size());
        switch (m_placement) {
        case Below: r.moveTop(a.bottom() + 2); r.moveLeft(a.center().x() - s.width() / 2); break;
        case Above: r.moveBottom(a.top() - 2); r.moveLeft(a.center().x() - s.width() / 2); break;
        case Left: r.moveRight(a.left() - 2); r.moveTop(a.top() - 10); break;
        case Right: r.moveLeft(a.right() + 2); r.moveTop(a.top() - 10); break;
        case Center: break;
        }
    } else {
        r.moveCenter(win.center());
    }
    if (r.right() > win.right()) r.moveRight(win.right());
    if (r.left() < win.left()) r.moveLeft(win.left());
    if (r.bottom() > win.bottom()) r.moveBottom(win.bottom());
    if (r.top() < win.top()) r.moveTop(win.top());
    setGeometry(r);
}

void IkPopover::dismiss()
{
    if (!isVisible()) return;
    hide();
    m_scrim->hide();
    qApp->removeEventFilter(this);
    Q_EMIT dismissed();
}

bool IkPopover::eventFilter(QObject *watched, QEvent *event)
{
    if ((event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress) && isVisible() && !m_modal) {
        const int key = static_cast<QKeyEvent *>(event)->key();
        if (key == Qt::Key_Escape || key == Qt::Key_Back) {
            event->accept();
            if (event->type() == QEvent::KeyPress) dismiss();
            return true;
        }
    }
    if (watched == m_window && event->type() == QEvent::Resize && isVisible()) {
        m_scrim->setGeometry(m_window->rect());
        place();
    }
    return IkPanel::eventFilter(watched, event);
}

// ---- IkChannelSlider ----------------------------------------------------------

IkChannelSlider::IkChannelSlider(Channel channel, QWidget *parent)
    : QWidget(parent), m_channel(channel)
{
    setAttribute(Qt::WA_AcceptTouchEvents);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

qreal IkChannelSlider::value() const { return m_channel == Hue ? h : m_channel == Saturation ? s : v; }
QSize IkChannelSlider::sizeHint() const { return QSize(200, 24); }
QRectF IkChannelSlider::track() const { return QRectF(6, 3, width() - 12, 9); }

void IkChannelSlider::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF t = track();
    QLinearGradient g(t.topLeft(), t.topRight());
    if (m_channel == Hue) {
        for (int i = 0; i <= 6; ++i) g.setColorAt(i / 6.0, QColor::fromHsvF(qMin(0.9999, i / 6.0), 1, 1));
    } else if (m_channel == Saturation) {
        g.setColorAt(0, QColor::fromHsvF(h, 0, v));
        g.setColorAt(1, QColor::fromHsvF(h, 1, v));
    } else {
        g.setColorAt(0, Qt::black);
        g.setColorAt(1, QColor::fromHsvF(h, s, 1));
    }
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawRoundedRect(t, 4.5, 4.5);
    const qreal x = t.left() + t.width() * value();
    QPainterPath tri;
    tri.moveTo(x, t.bottom() + 1);
    tri.lineTo(x - 5.5, t.bottom() + 10);
    tri.lineTo(x + 5.5, t.bottom() + 10);
    tri.closeSubpath();
    p.setBrush(IkStyle::color(IkStyle::Text, palette()));
    p.drawPath(tri);
}

void IkChannelSlider::setFrom(qreal px)
{
    const QRectF t = track();
    const qreal val = qBound<qreal>(0.0, (px - t.left()) / t.width(), m_channel == Hue ? 0.9999 : 1.0);
    if (onEdited) onEdited(val);
}

static qreal ikPosX(QMouseEvent *e)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return e->position().x();
#else
    return e->localPos().x();
#endif
}

void IkChannelSlider::mousePressEvent(QMouseEvent *e) { m_drag = true; setFrom(ikPosX(e)); }
void IkChannelSlider::mouseMoveEvent(QMouseEvent *e) { if (m_drag) setFrom(ikPosX(e)); }
void IkChannelSlider::mouseReleaseEvent(QMouseEvent *) { m_drag = false; }
