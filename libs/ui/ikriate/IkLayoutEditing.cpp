/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// Interface editing for the IKriate layouts: buttons live in "docks" (rows
// and columns of buttons); while editing, an overlay disables the tools and
// lets the user drag buttons from one dock, or position, to another.

#include "IkLayouts.h"

#include <QBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <KConfigGroup>
#include <KSharedConfig>
#include <klocalizedstring.h>

#include "IkInterfaceManager.h"
#include "IkStyle.h"
#include "IkWidgets.h"

namespace
{
const char kGroup[] = "IKriateArrangement";
constexpr int kDragStart = 8;    ///< pointer travel before a press becomes a drag
constexpr int kDockReach = 28;   ///< drops this close to a dock still land in it
}

/// Covers the window while editing: dims and blocks everything, highlights
/// the docks and their buttons, and handles dragging.
class IkEditOverlay : public QWidget
{
public:
    explicit IkEditOverlay(IkLayout *layout)
        : QWidget(layout->host())
        , m_layout(layout)
    {
        setAttribute(Qt::WA_NoSystemBackground);
        setFocusPolicy(Qt::StrongFocus);

        m_banner = new IkPanel(this);
        m_banner->setPill(true);
        m_banner->setRole(IkStyle::Surface);
        m_banner->setShadow(true);
        QHBoxLayout *bl = new QHBoxLayout(m_banner);
        bl->setContentsMargins(IkStyle::scaled(22), 6, 8, 6);
        bl->setSpacing(IkStyle::scaled(14));
        QLabel *icon = new QLabel(m_banner);
        icon->setPixmap(IkStyle::pixmap(QStringLiteral("edit-interface"), IkStyle::scaled(22),
                                        IkStyle::color(IkStyle::Accent), devicePixelRatioF()));
        bl->addWidget(icon);
        QLabel *text = new QLabel(i18n("Drag buttons to move them"), m_banner);
        text->setFont(IkStyle::font(1.0, QFont::DemiBold));
        bl->addWidget(text);
        IkTextButton *reset = new IkTextButton(i18n("Reset"), IkTextButton::Plain, m_banner);
        reset->setMinimumHeight(IkStyle::touchSize() - 6);
        QObject::connect(reset, &QAbstractButton::clicked, m_layout, [this]() {
            m_layout->resetArrangement();
            sync();
        });
        bl->addWidget(reset);
        IkTextButton *done = new IkTextButton(i18n("Done"), IkTextButton::Filled, m_banner);
        done->setRadius(IkStyle::touchSize() / 2);
        done->setMinimumSize(IkStyle::scaled(96), IkStyle::touchSize() - 6);
        QObject::connect(done, &QAbstractButton::clicked, m_layout, [this]() {
            m_layout->manager()->setEditingInterface(false);
        });
        bl->addWidget(done);
        sync();
        show();
        setFocus();
    }

    /// Follows the window and the panels after a re-layout.
    void sync()
    {
        setGeometry(parentWidget()->rect());
        const QRect area = rect().marginsRemoved(m_layout->reservedMargins());
        const QSize b = m_banner->sizeHint();
        m_banner->setGeometry(area.left() + (area.width() - b.width()) / 2, area.bottom() - IkStyle::scaled(28) - b.height(),
                              b.width(), b.height());
        m_pixmaps.clear();
        raise();
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), QColor(0, 0, 0, 110));

        const QColor accent = IkStyle::color(IkStyle::Accent);
        const qreal r = IkStyle::scaled(10);
        // Docks: dashed outline; the target of a drag is filled.
        for (int i = 0; i < m_layout->m_docks.size(); ++i) {
            const QRect d = dockRect(i);
            if (d.isEmpty()) continue;
            const bool target = m_dragging && i == m_targetDock;
            p.setBrush(target ? IkStyle::withAlpha(accent, 50) : QColor(255, 255, 255, 14));
            QPen pen(target ? accent : IkStyle::withAlpha(accent, 170), target ? 2.0 : 1.5, Qt::DashLine);
            p.setPen(pen);
            p.drawRoundedRect(QRectF(d).adjusted(0.5, 0.5, -0.5, -0.5), r, r);
        }
        // Buttons, undimmed, each on a tile.
        for (int i = 0; i < m_layout->m_docks.size(); ++i) {
            for (IkIconButton *b : m_layout->dockItems(i)) {
                if (!b->isVisible()) continue;
                const QRect g = itemRect(b);
                const bool dragged = m_dragging && b == m_drag;
                p.setPen(QPen(IkStyle::withAlpha(accent, dragged ? 90 : 200), 1.5, dragged ? Qt::DashLine : Qt::SolidLine));
                p.setBrush(dragged ? Qt::NoBrush : QBrush(IkStyle::color(IkStyle::Raised)));
                p.drawRoundedRect(QRectF(g).adjusted(1, 1, -1, -1), r, r);
                if (!dragged) p.drawPixmap(g.topLeft(), pixmapOf(b));
            }
        }
        if (!m_dragging || !m_drag) return;

        // Where the button will go.
        if (m_targetDock >= 0) {
            const QLineF line = insertionLine();
            if (!line.isNull()) {
                p.setPen(QPen(accent, IkStyle::scaled(4), Qt::SolidLine, Qt::RoundCap));
                p.drawLine(line);
            }
        }
        // The button itself, under the finger.
        const QPixmap pm = pixmapOf(m_drag);
        const QSizeF size = QSizeF(pm.size()) / pm.devicePixelRatioF() * 1.15;
        const QRectF ghost(m_pos - QPointF(size.width() / 2, size.height() / 2), size);
        IkStyle::drawShadow(p, ghost, r, 12, 90);
        p.setPen(QPen(accent, 2));
        p.setBrush(IkStyle::color(IkStyle::Raised));
        p.drawRoundedRect(ghost, r, r);
        p.drawPixmap(ghost.toRect(), pm);
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        m_drag = nullptr;
        m_dragging = false;
        if (e->button() != Qt::LeftButton) return;
        const QPoint pos = e->pos();
        for (int i = 0; i < m_layout->m_docks.size() && !m_drag; ++i) {
            for (IkIconButton *b : m_layout->dockItems(i)) {
                if (b->isVisible() && itemRect(b).adjusted(-4, -4, 4, 4).contains(pos)) {
                    m_drag = b;
                    break;
                }
            }
        }
        m_press = pos;
        m_pos = pos;
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (!m_drag) return;
        m_pos = e->pos();
        if (!m_dragging && (m_pos - m_press).manhattanLength() >= kDragStart) m_dragging = true;
        if (m_dragging) {
            m_targetDock = dockAt(m_pos);
            m_targetIndex = m_targetDock >= 0 ? indexAt(m_targetDock, m_pos) : -1;
            update();
        }
    }

    void mouseReleaseEvent(QMouseEvent *) override
    {
        if (m_dragging && m_drag && m_targetDock >= 0) {
            m_layout->moveItem(m_drag, m_targetDock, m_targetIndex);
            m_layout->saveArrangement();
            m_layout->updateDocks();
        }
        m_drag = nullptr;
        m_dragging = false;
        m_targetDock = -1;
        sync();
    }

    void keyPressEvent(QKeyEvent *e) override
    {
        if (e->key() == Qt::Key_Escape || e->key() == Qt::Key_Back) {
            m_layout->manager()->setEditingInterface(false);
            return;
        }
        QWidget::keyPressEvent(e);
    }

private:
    QRect itemRect(QWidget *w) const
    {
        return QRect(w->mapTo(parentWidget(), QPoint(0, 0)), w->size());
    }

    QRect dockRect(int i) const
    {
        const IkLayout::Dock &d = m_layout->m_docks.at(i);
        if (!d.panel || !d.panel->isVisible() || !d.box) return QRect();
        QRect g = d.box->geometry();
        if (g.isEmpty()) g = d.panel->rect();
        const int pad = IkStyle::scaled(3);
        return QRect(d.panel->mapTo(parentWidget(), g.topLeft()), g.size()).adjusted(-pad, -pad, pad, pad);
    }

    static bool vertical(const QBoxLayout *box)
    {
        return box->direction() == QBoxLayout::TopToBottom || box->direction() == QBoxLayout::BottomToTop;
    }

    int dockAt(const QPoint &pos) const
    {
        int best = -1;
        int bestDistance = kDockReach + 1;
        for (int i = 0; i < m_layout->m_docks.size(); ++i) {
            const QRect d = dockRect(i);
            if (d.isEmpty()) continue;
            if (d.contains(pos)) return i;
            const int dx = qMax(0, qMax(d.left() - pos.x(), pos.x() - d.right()));
            const int dy = qMax(0, qMax(d.top() - pos.y(), pos.y() - d.bottom()));
            const int dist = qMax(dx, dy);
            if (dist < bestDistance) {
                bestDistance = dist;
                best = i;
            }
        }
        if (best >= 0) return best;
        // Anywhere on a panel: its nearest dock (e.g. the gap in a bar).
        bestDistance = INT_MAX;
        for (int i = 0; i < m_layout->m_docks.size(); ++i) {
            const IkLayout::Dock &d = m_layout->m_docks.at(i);
            if (!d.panel || !d.panel->isVisible()) continue;
            const QRect panel(d.panel->mapTo(parentWidget(), QPoint(0, 0)), d.panel->size());
            if (!panel.contains(pos)) continue;
            const QRect r = dockRect(i);
            const int dist = qAbs(r.center().x() - pos.x()) + qAbs(r.center().y() - pos.y());
            if (dist < bestDistance) {
                bestDistance = dist;
                best = i;
            }
        }
        return best;
    }

    int indexAt(int dock, const QPoint &pos) const
    {
        const bool v = vertical(m_layout->m_docks.at(dock).box);
        int index = 0;
        for (IkIconButton *b : m_layout->dockItems(dock)) {
            if (b == m_drag) continue;
            const QPoint c = itemRect(b).center();
            if ((v ? c.y() : c.x()) < (v ? pos.y() : pos.x())) ++index;
        }
        return index;
    }

    QLineF insertionLine() const
    {
        const IkLayout::Dock &d = m_layout->m_docks.at(m_targetDock);
        const bool v = vertical(d.box);
        QList<IkIconButton *> items = m_layout->dockItems(m_targetDock);
        items.removeAll(m_drag.data());
        const QRect dock = dockRect(m_targetDock);
        const int inset = IkStyle::scaled(6);
        auto across = [&](qreal at) {
            return v ? QLineF(dock.left() + inset, at, dock.right() - inset, at) : QLineF(at, dock.top() + inset, at, dock.bottom() - inset);
        };
        if (items.isEmpty()) return across(v ? dock.center().y() : dock.center().x());
        const int gap = qMax(2, d.box->spacing()) / 2;
        if (m_targetIndex < items.size()) {
            const QRect g = itemRect(items.at(m_targetIndex));
            return across(v ? g.top() - gap : g.left() - gap);
        }
        const QRect g = itemRect(items.last());
        return across(v ? g.bottom() + gap + 1 : g.right() + gap + 1);
    }

    QPixmap pixmapOf(IkIconButton *b)
    {
        auto it = m_pixmaps.find(b);
        if (it == m_pixmaps.end()) it = m_pixmaps.insert(b, b->grab());
        return it.value();
    }

    IkLayout *m_layout;
    IkPanel *m_banner {nullptr};
    QPointer<IkIconButton> m_drag;
    bool m_dragging {false};
    QPoint m_press;
    QPoint m_pos;
    int m_targetDock {-1};
    int m_targetIndex {-1};
    QHash<IkIconButton *, QPixmap> m_pixmaps;
};

// ---- IkLayout: docks ----------------------------------------------------------

void IkLayout::addDock(const QString &id, QWidget *panel, QBoxLayout *box, std::function<void(IkIconButton *)> style,
                       Qt::Alignment alignment, bool hideWhenEmpty)
{
    Dock d;
    d.id = id;
    d.panel = panel;
    d.box = box;
    d.style = std::move(style);
    d.alignment = alignment;
    d.hideWhenEmpty = hideWhenEmpty;
    m_docks << d;
}

QString IkLayout::configKey(const QString &dockId) const
{
    return QString::fromLatin1(metaObject()->className()) + QLatin1Char('.') + dockId;
}

void IkLayout::finishDocks()
{
    for (int i = 0; i < m_docks.size(); ++i) {
        for (IkIconButton *b : dockItems(i)) {
            if (b->objectName().isEmpty()) b->setObjectName(b->iconName());
            b->setProperty("ikDockItem", true);
            m_docks[i].defaults << b->objectName();
        }
    }
    QHash<QString, QStringList> saved;
    const KConfigGroup g(KSharedConfig::openConfig(), kGroup);
    for (const Dock &d : m_docks) {
        if (g.hasKey(configKey(d.id))) saved.insert(d.id, g.readEntry(configKey(d.id), QStringList()));
    }
    if (!saved.isEmpty()) applyArrangement(saved);
    updateDocks();
}

QList<IkIconButton *> IkLayout::dockItems(int dock) const
{
    QList<IkIconButton *> items;
    const Dock &d = m_docks.at(dock);
    if (!d.box) return items;
    for (int i = 0; i < d.box->count(); ++i) {
        if (IkIconButton *b = qobject_cast<IkIconButton *>(d.box->itemAt(i)->widget())) items << b;
    }
    return items;
}

int IkLayout::dockOf(QWidget *item) const
{
    for (int i = 0; i < m_docks.size(); ++i) {
        if (m_docks.at(i).box && m_docks.at(i).box->indexOf(item) >= 0) return i;
    }
    return -1;
}

int IkLayout::dockCount(const QString &id) const
{
    for (int i = 0; i < m_docks.size(); ++i) {
        if (m_docks.at(i).id == id) return dockItems(i).size();
    }
    return 0;
}

void IkLayout::moveItem(IkIconButton *item, int dock, int index)
{
    Dock &dst = m_docks[dock];
    const int from = dockOf(item);
    if (from < 0 || !dst.box) return;
    if (dst.placeholder) {
        dst.box->removeWidget(dst.placeholder);
        delete dst.placeholder.data();
    }
    m_docks[from].box->removeWidget(item);
    QWidget *parent = dst.box->parentWidget() ? dst.box->parentWidget() : dst.panel.data();
    if (item->parentWidget() != parent) item->setParent(parent);
    index = qBound(0, index, int(dockItems(dock).size()));
    dst.box->insertWidget(index, item, 0, dst.alignment);
    if (dst.style) dst.style(item);
    item->setVisible(true);
}

void IkLayout::applyArrangement(const QHash<QString, QStringList> &arrangement)
{
    QHash<QString, IkIconButton *> byId;
    for (int i = 0; i < m_docks.size(); ++i) {
        for (IkIconButton *b : dockItems(i)) byId.insert(b->objectName(), b);
    }
    for (int i = 0; i < m_docks.size(); ++i) {
        const QStringList ids = arrangement.value(m_docks.at(i).id);
        int position = 0;
        for (const QString &id : ids) {
            if (IkIconButton *b = byId.value(id)) moveItem(b, i, position++);
        }
    }
}

void IkLayout::saveArrangement() const
{
    KConfigGroup g(KSharedConfig::openConfig(), kGroup);
    for (int i = 0; i < m_docks.size(); ++i) {
        QStringList ids;
        for (IkIconButton *b : dockItems(i)) ids << b->objectName();
        g.writeEntry(configKey(m_docks.at(i).id), ids);
    }
    g.sync();
}

void IkLayout::resetArrangement()
{
    KConfigGroup g(KSharedConfig::openConfig(), kGroup);
    QHash<QString, QStringList> defaults;
    for (const Dock &d : m_docks) {
        g.deleteEntry(configKey(d.id));
        defaults.insert(d.id, d.defaults);
    }
    g.sync();
    applyArrangement(defaults);
    updateDocks();
}

void IkLayout::updateDocks()
{
    const int ts = IkStyle::touchSize();
    for (Dock &d : m_docks) {
        if (!d.box) continue;
        int count = 0;
        for (int i = 0; i < d.box->count(); ++i) {
            if (qobject_cast<IkIconButton *>(d.box->itemAt(i)->widget())) ++count;
        }
        const bool wantPlaceholder = m_editing && count == 0;
        if (wantPlaceholder && !d.placeholder) {
            QWidget *parent = d.box->parentWidget() ? d.box->parentWidget() : d.panel.data();
            QWidget *ph = new QWidget(parent);
            ph->setFixedSize(ts, ts);
            d.box->addWidget(ph, 0, d.alignment);
            ph->show();
            d.placeholder = ph;
        } else if (!wantPlaceholder && d.placeholder) {
            d.box->removeWidget(d.placeholder);
            delete d.placeholder.data();
        }
        if (d.hideWhenEmpty && d.panel) {
            const bool hidden = count == 0 && !m_editing;
            d.panel->setProperty("ikHidden", hidden);
            d.panel->setVisible(isVisible() && !hidden);
        }
        if (d.box->parentWidget()) d.box->activate();
    }
    m_manager->requestLayout();
}

// ---- IkLayout: editing ----------------------------------------------------------

bool IkLayout::isEditing() const
{
    return m_editing;
}

void IkLayout::setEditing(bool editing)
{
    if (editing == m_editing) return;
    if (editing && (!m_visible || m_docks.isEmpty())) return;
    m_editing = editing;
    if (editing) {
        closePopovers();
        m_editOverlay = new IkEditOverlay(this);
    } else {
        delete m_editOverlay.data();
    }
    updateDocks();
    syncEditOverlay();
    Q_EMIT m_manager->editingInterfaceChanged(editing);
}

void IkLayout::syncEditOverlay()
{
    if (m_editOverlay) static_cast<IkEditOverlay *>(m_editOverlay.data())->sync();
}

int IkLayout::placementFor(QWidget *anchor, int fallback) const
{
    if (!anchor || !anchor->property("ikDockItem").toBool() || !anchor->parentWidget()) return fallback;
    QWidget *panel = anchor->parentWidget();
    while (panel->parentWidget() && panel->parentWidget() != host()) panel = panel->parentWidget();
    const QRect r(panel->mapTo(host(), QPoint(0, 0)), panel->size());
    const QRect w = host()->rect();
    if (r.height() > r.width()) {
        return r.center().x() < w.center().x() ? IkPopover::Right : IkPopover::Left;
    }
    return r.center().y() < w.center().y() ? IkPopover::Below : IkPopover::Above;
}
