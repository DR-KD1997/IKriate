/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "IkLayersPanel.h"

#include <QAbstractListModel>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QScroller>
#include <QStyledItemDelegate>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <klocalizedstring.h>

#include <KoColorSpace.h>
#include <KoCompositeOp.h>
#include <KoCompositeOpRegistry.h>
#include <kis_base_node.h>
#include <kis_group_layer.h>
#include <kis_image.h>
#include <kis_layer_properties_icons.h>
#include <kis_node.h>
#include <kis_signal_compressor.h>

#include "KisViewManager.h"
#include "kis_node_manager.h"

#include "IkInterfaceManager.h"
#include "IkStyle.h"
#include "IkWidgets.h"

namespace
{
struct Row {
    KisNodeSP node;
    int depth {0};
    QImage thumbnail;
};

const int kThumb = 52;

class IkLayerModel : public QAbstractListModel
{
public:
    using QAbstractListModel::QAbstractListModel;
    QVector<Row> rows;
    KisNodeSP active;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override { return parent.isValid() ? 0 : rows.size(); }
    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() >= rows.size()) return QVariant();
        if (role == Qt::DisplayRole) return rows[index.row()].node->name();
        return QVariant();
    }
    void reset(const QVector<Row> &r)
    {
        beginResetModel();
        rows = r;
        endResetModel();
    }
};

QString blendName(KisNodeSP node)
{
    const KoID id = KoCompositeOpRegistry::instance().getKoID(node->compositeOpId());
    return id.name().isEmpty() ? node->compositeOpId() : id.name();
}

class IkLayerDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    IkLayerModel *model {nullptr};

    static int rowHeight() { return IkStyle::touchFirst() ? 84 : 76; }
    QSize sizeHint(const QStyleOptionViewItem &o, const QModelIndex &) const override { return QSize(o.rect.width(), rowHeight()); }

    QRectF card(const QRect &r) const { return QRectF(r).adjusted(4, 3, -4, -3); }
    QRectF toggleRect(const QRect &r) const { const QRectF c = card(r); return QRectF(c.right() - 52, c.center().y() - 22, 44, 44); }
    QRectF menuRect(const QRect &r) const { const QRectF t = toggleRect(r); return QRectF(t.left() - 48, t.top(), 44, 44); }

    void paint(QPainter *p, const QStyleOptionViewItem &o, const QModelIndex &index) const override
    {
        if (!model || index.row() >= model->rows.size()) return;
        const Row &row = model->rows[index.row()];
        const QPalette pal = o.palette;
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const QRectF c = card(o.rect);
        const bool active = row.node == model->active;
        if (active) {
            p->setPen(QPen(IkStyle::color(IkStyle::Accent, pal), 1.6));
            p->setBrush(IkStyle::mix(IkStyle::color(IkStyle::Surface, pal), IkStyle::color(IkStyle::Accent, pal), 0.12));
            p->drawRoundedRect(c.adjusted(0.8, 0.8, -0.8, -0.8), 10, 10);
        }

        // Thumbnail on a checkerboard-free light card, like paper.
        const qreal indent = row.depth * 14;
        const QRectF thumb(c.left() + 14 + indent, c.center().y() - kThumb / 2.0 + 2, kThumb + 16, kThumb - 4);
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(0xF4, 0xF1, 0xEA));
        p->drawRoundedRect(thumb, 6, 6);
        if (!row.thumbnail.isNull()) {
            QPainterPath clip;
            clip.addRoundedRect(thumb, 6, 6);
            p->setClipPath(clip);
            const QSizeF s = QSizeF(row.thumbnail.size()).scaled(thumb.size(), Qt::KeepAspectRatioByExpanding);
            QRectF t(QPointF(), s);
            t.moveCenter(thumb.center());
            p->drawImage(t, row.thumbnail);
            p->setClipping(false);
        }
        if (row.node->inherits("KisGroupLayer")) {
            const int s = 18;
            p->drawPixmap(QRectF(thumb.right() - s - 2, thumb.bottom() - s - 2, s, s),
                          IkStyle::pixmap(QStringLiteral("group"), s, QColor(0x55, 0x55, 0x55), p->device()->devicePixelRatioF()),
                          QRectF(0, 0, s * p->device()->devicePixelRatioF(), s * p->device()->devicePixelRatioF()));
        }

        const qreal tx = thumb.right() + 16;
        const QRectF text(tx, c.top() + 8, menuRect(o.rect).left() - tx - 4, c.height() - 16);
        p->setPen(row.node->visible() ? IkStyle::color(IkStyle::Text, pal) : IkStyle::color(IkStyle::FaintText, pal));
        p->setFont(IkStyle::font(1.12, QFont::Bold));
        p->drawText(QRectF(text.left(), text.top(), text.width(), text.height() / 2 + 2), Qt::AlignLeft | Qt::AlignBottom,
                    QFontMetrics(p->font()).elidedText(row.node->name(), Qt::ElideRight, int(text.width())));
        p->setFont(IkStyle::font(0.92));
        p->setPen(IkStyle::color(IkStyle::MutedText, pal));
        const QString sub = QStringLiteral("%1 · %2%").arg(blendName(row.node)).arg(qRound(row.node->opacity() * 100.0 / 255));
        p->drawText(QRectF(text.left(), text.center().y() + 2, text.width(), text.height() / 2), Qt::AlignLeft | Qt::AlignTop, sub);

        const qreal dpr = p->device()->devicePixelRatioF();
        // Layer menu (chevron triangle)
        const QRectF m = menuRect(o.rect);
        QPainterPath tri;
        const QPointF mc = m.center();
        tri.moveTo(mc + QPointF(-9, -5));
        tri.lineTo(mc + QPointF(9, -5));
        tri.lineTo(mc + QPointF(0, 6));
        tri.closeSubpath();
        p->setPen(Qt::NoPen);
        p->setBrush(IkStyle::color(IkStyle::Text, pal));
        p->drawPath(tri);
        // Visibility, or the lock when the layer is locked
        const QRectF t = toggleRect(o.rect);
        const QString icon = row.node->userLocked() ? QStringLiteral("lock")
                                                    : row.node->visible() ? QStringLiteral("eye") : QStringLiteral("eye-off");
        const int s = 26;
        p->drawPixmap(QRectF(t.center() - QPointF(s, s) / 2, QSizeF(s, s)),
                      IkStyle::pixmap(icon, s, IkStyle::color(IkStyle::Text, pal), dpr), QRectF(0, 0, s * dpr, s * dpr));
        p->restore();
    }
};
}

struct IkLayersPanel::Private
{
    IkInterfaceManager *manager {nullptr};
    QListView *view {nullptr};
    IkLayerModel *model {nullptr};
    IkLayerDelegate *delegate {nullptr};
    QWidget *header {nullptr};
    QWidget *footer {nullptr};
    KisSignalCompressor *compressor {nullptr};
    KisSignalCompressor *thumbCompressor {nullptr}; ///< pixels changed: refresh after painting pauses
    QPointer<KisViewManager> vm;
    KisImageWSP image;
};

IkLayersPanel::IkLayersPanel(IkInterfaceManager *manager, QWidget *parent)
    : QWidget(parent)
    , d(new Private)
{
    d->manager = manager;
    d->compressor = new KisSignalCompressor(120, KisSignalCompressor::FIRST_ACTIVE, this);
    connect(d->compressor, &KisSignalCompressor::timeout, this, &IkLayersPanel::rebuild);
    d->thumbCompressor = new KisSignalCompressor(700, KisSignalCompressor::POSTPONE, this);
    connect(d->thumbCompressor, &KisSignalCompressor::timeout, this, &IkLayersPanel::rebuild);

    QVBoxLayout *l = new QVBoxLayout(this);
    l->setContentsMargins(8, 4, 8, 4);
    l->setSpacing(4);

    d->header = new QWidget(this);
    QHBoxLayout *h = new QHBoxLayout(d->header);
    h->setContentsMargins(8, 0, 0, 0);
    QLabel *title = new QLabel(i18n("Layers"), d->header);
    title->setFont(IkStyle::font(1.15, QFont::Bold));
    h->addWidget(title);
    h->addStretch(1);
    IkIconButton *add = new IkIconButton(QStringLiteral("plus"), i18n("New layer"), d->header);
    connect(add, &QAbstractButton::clicked, this, [this]() { if (d->manager) d->manager->triggerAction(QStringLiteral("add_new_paint_layer")); });
    h->addWidget(add);
    IkIconButton *props = new IkIconButton(QStringLiteral("sliders"), i18n("Layer properties"), d->header);
    connect(props, &QAbstractButton::clicked, this, [this]() { if (d->manager) d->manager->triggerAction(QStringLiteral("layer_properties")); });
    h->addWidget(props);
    l->addWidget(d->header);

    d->model = new IkLayerModel(this);
    d->delegate = new IkLayerDelegate(this);
    d->delegate->model = d->model;
    d->view = new QListView(this);
    d->view->setModel(d->model);
    d->view->setItemDelegate(d->delegate);
    d->view->setFrameShape(QFrame::NoFrame);
    d->view->setUniformItemSizes(true);
    d->view->setSelectionMode(QAbstractItemView::NoSelection);
    d->view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    d->view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    IkStyle::makeTransparent(d->view);
    d->view->viewport()->setAutoFillBackground(false);
    d->view->viewport()->installEventFilter(this);
    QScroller::grabGesture(d->view->viewport(), IkStyle::touchFirst() ? QScroller::LeftMouseButtonGesture : QScroller::TouchGesture);
    l->addWidget(d->view, 1);

    d->footer = new QWidget(this);
    QHBoxLayout *f = new QHBoxLayout(d->footer);
    f->setContentsMargins(0, 0, 0, 0);
    f->addStretch(1);
    const QList<QPair<QString, QPair<QString, QString>>> footerButtons = {
        {QStringLiteral("group"), {i18n("Group layers"), QStringLiteral("create_quick_group")}},
        {QStringLiteral("fx"), {i18n("Layer style"), QStringLiteral("layer_style")}},
        {QStringLiteral("trash"), {i18n("Delete layer"), QStringLiteral("remove_layer")}},
    };
    for (const auto &b : footerButtons) {
        IkIconButton *btn = new IkIconButton(b.first, b.second.first, d->footer);
        const QString actionName = b.second.second;
        connect(btn, &QAbstractButton::clicked, this, [this, actionName]() { if (d->manager) d->manager->triggerAction(actionName); });
        f->addWidget(btn);
    }
    l->addWidget(d->footer);

    refresh();
}

IkLayersPanel::~IkLayersPanel()
{
    delete d;
}

void IkLayersPanel::setShowHeader(bool show) { d->header->setVisible(show); }
void IkLayersPanel::setShowFooter(bool show) { d->footer->setVisible(show); }

QSize IkLayersPanel::sizeHint() const
{
    return QSize(380, 420);
}

void IkLayersPanel::connectToImage()
{
    KisViewManager *vm = d->manager ? d->manager->viewManager() : nullptr;
    if (vm != d->vm) {
        if (d->vm) disconnect(d->vm, nullptr, this, nullptr);
        d->vm = vm;
        if (vm) {
            connect(vm, &KisViewManager::viewChanged, this, &IkLayersPanel::refresh);
            if (vm->nodeManager()) {
                connect(vm->nodeManager(), &KisNodeManager::sigNodeActivated, this, [this](KisNodeSP) { d->compressor->start(); });
            }
        }
    }
    KisImageWSP image = vm ? vm->image() : KisImageWSP();
    if (image != d->image) {
        if (d->image) disconnect(d->image.data(), nullptr, this, nullptr);
        d->image = image;
        if (image) {
            KisImage *img = image.data();
            connect(img, &KisImage::sigLayersChangedAsync, d->compressor, &KisSignalCompressor::start, Qt::QueuedConnection);
            connect(img, &KisImage::sigNodeChanged, d->compressor, [this](KisNodeSP) { d->compressor->start(); }, Qt::QueuedConnection);
            connect(img, &KisImage::sigNodeAddedAsync, d->compressor, [this](KisNodeSP, KisNodeAdditionFlags) { d->compressor->start(); }, Qt::QueuedConnection);
            connect(img, &KisImage::sigRemoveNodeAsync, d->compressor, [this](KisNodeSP) { d->compressor->start(); }, Qt::QueuedConnection);
            connect(img, &KisImage::sigImageUpdated, d->thumbCompressor, [this](const QRect &) { d->thumbCompressor->start(); }, Qt::QueuedConnection);
        }
    }
}

void IkLayersPanel::refresh()
{
    connectToImage();
    rebuild();
}

void IkLayersPanel::rebuild()
{
    QVector<Row> rows;
    KisImageSP image = d->image.toStrongRef();
    if (image && image->root()) {
        std::function<void(KisNodeSP, int)> walk = [&](KisNodeSP parent, int depth) {
            for (KisNodeSP child = parent->lastChild(); child; child = child->prevSibling()) {
                if (child->isFakeNode()) continue;
                // Masks and decorations that the user cannot see as layers stay hidden.
                if (!child->inherits("KisLayer") && !child->inherits("KisMask")) continue;
                Row r;
                r.node = child;
                r.depth = depth;
                r.thumbnail = child->createThumbnail(kThumb * 2, kThumb * 2, Qt::KeepAspectRatio);
                rows << r;
                if (child->inherits("KisGroupLayer") && !child->collapsed()) walk(child, depth + 1);
                else if (child->inherits("KisLayer") && child->childCount() > 0) walk(child, depth + 1);
            }
        };
        walk(image->root(), 0);
    }
    d->model->active = (d->vm && d->vm->nodeManager()) ? d->vm->nodeManager()->activeNode() : KisNodeSP();
    d->model->reset(rows);
}

void IkLayersPanel::activate(KisNodeSP node)
{
    if (d->vm && d->vm->nodeManager() && node) {
        d->vm->nodeManager()->slotUiActivatedNode(node);
        d->model->active = node;
        d->view->viewport()->update();
    }
}

void IkLayersPanel::showLayerMenu(KisNodeSP node, const QPoint &globalPos)
{
    activate(node);
    KisImageSP image = d->image.toStrongRef();
    QMenu menu(this);

    // Opacity slider in the menu
    QWidgetAction *opacityAction = new QWidgetAction(&menu);
    QWidget *ow = new QWidget(&menu);
    QHBoxLayout *ol = new QHBoxLayout(ow);
    ol->setContentsMargins(12, 4, 12, 4);
    QLabel *ol1 = new QLabel(i18n("Opacity"), ow);
    ol->addWidget(ol1);
    IkSlider *opacity = new IkSlider(ow);
    opacity->setMinimumWidth(160);
    opacity->setValue(node->opacity() / 255.0);
    QLabel *ov = new QLabel(QStringLiteral("%1%").arg(qRound(node->opacity() / 2.55)), ow);
    ov->setMinimumWidth(40);
    ol->addWidget(opacity, 1);
    ol->addWidget(ov);
    const quint8 originalOpacity = node->opacity();
    connect(opacity, &IkSlider::valueEdited, this, [node, ov](qreal v) mutable {
        // Live preview while dragging; one undoable change when released.
        node->setOpacity(quint8(qRound(v * 255)));
        node->setDirty();
        ov->setText(QStringLiteral("%1%").arg(qRound(v * 100)));
    });
    connect(opacity, &IkSlider::interactionFinished, this, [this, node, opacity, originalOpacity]() mutable {
        const qint32 finalOpacity = qRound(opacity->value() * 255);
        node->setOpacity(originalOpacity);
        if (d->vm && d->vm->nodeManager()) d->vm->nodeManager()->setNodeOpacity(node, finalOpacity);
    });
    opacityAction->setDefaultWidget(ow);
    menu.addAction(opacityAction);

    // Blending modes (the common ones; the full list is in Layer Properties)
    QMenu *blend = menu.addMenu(i18n("Blending Mode: %1", blendName(node)));
    const QStringList ops = {COMPOSITE_OVER, COMPOSITE_MULT, COMPOSITE_SCREEN, COMPOSITE_OVERLAY, COMPOSITE_SOFT_LIGHT_PHOTOSHOP,
                             COMPOSITE_HARD_LIGHT, COMPOSITE_DARKEN, COMPOSITE_LIGHTEN, COMPOSITE_DODGE, COMPOSITE_BURN,
                             COMPOSITE_ADD, COMPOSITE_DIFF, COMPOSITE_COLOR, COMPOSITE_LUMINIZE, COMPOSITE_HUE, COMPOSITE_SATURATION};
    for (const QString &op : ops) {
        QAction *a = blend->addAction(KoCompositeOpRegistry::instance().getKoID(op).name());
        a->setCheckable(true);
        a->setChecked(node->compositeOpId() == op);
        connect(a, &QAction::triggered, this, [this, node, op]() {
            const KoCompositeOp *cop = node->colorSpace() ? node->colorSpace()->compositeOp(op) : nullptr;
            if (cop && d->vm && d->vm->nodeManager()) d->vm->nodeManager()->setNodeCompositeOp(node, cop);
        });
    }
    menu.addSeparator();
    QAction *rename = menu.addAction(i18n("Rename..."));
    connect(rename, &QAction::triggered, this, [this, node]() {
        bool ok = false;
        const QString name = QInputDialog::getText(this, i18n("Rename Layer"), i18n("Name:"), QLineEdit::Normal, node->name(), &ok);
        if (ok && !name.isEmpty() && d->vm && d->vm->nodeManager()) d->vm->nodeManager()->setNodeName(node, name);
    });
    auto addCommand = [&](const QString &text, const QString &actionName) {
        QAction *a = menu.addAction(text);
        connect(a, &QAction::triggered, this, [this, actionName]() { if (d->manager) d->manager->triggerAction(actionName); });
        return a;
    };
    addCommand(i18n("Duplicate"), QStringLiteral("duplicatelayer"));
    addCommand(i18n("Merge Down"), QStringLiteral("merge_layer"));
    addCommand(i18n("Move Up"), QStringLiteral("move_layer_up"));
    addCommand(i18n("Move Down"), QStringLiteral("move_layer_down"));
    menu.addSeparator();
    QAction *clip = addCommand(i18n("Clip to Layer Below"), QStringLiteral("toggle_layer_inherit_alpha"));
    clip->setCheckable(true);
    clip->setChecked(KisLayerPropertiesIcons::nodeProperty(node, KisLayerPropertiesIcons::inheritAlpha, false).toBool());
    QAction *alpha = addCommand(i18n("Lock Transparency"), QStringLiteral("toggle_layer_alpha_lock"));
    alpha->setCheckable(true);
    alpha->setChecked(KisLayerPropertiesIcons::nodeProperty(node, KisLayerPropertiesIcons::alphaLocked, false).toBool());
    QAction *lock = addCommand(i18n("Lock Layer"), QStringLiteral("toggle_layer_lock"));
    lock->setCheckable(true);
    lock->setChecked(node->userLocked());
    menu.addSeparator();
    addCommand(i18n("Clear"), QStringLiteral("clear"));
    addCommand(i18n("Properties..."), QStringLiteral("layer_properties"));
    addCommand(i18n("Delete"), QStringLiteral("remove_layer"));
    Q_UNUSED(image);
    menu.exec(globalPos);
}

bool IkLayersPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == d->view->viewport() && event->type() == QEvent::MouseButtonRelease) {
        QScroller *scroller = QScroller::scroller(d->view->viewport());
        if (scroller && scroller->state() == QScroller::Scrolling) return false;
        QMouseEvent *me = static_cast<QMouseEvent *>(event);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        const QPoint pos = me->position().toPoint();
#else
        const QPoint pos = me->pos();
#endif
        const QModelIndex idx = d->view->indexAt(pos);
        if (!idx.isValid() || idx.row() >= d->model->rows.size()) return false;
        KisNodeSP node = d->model->rows[idx.row()].node;
        const QRect r = d->view->visualRect(idx);
        KisImageSP image = d->image.toStrongRef();
        if (d->delegate->toggleRect(r).contains(pos) && image) {
            if (node->userLocked()) {
                KisLayerPropertiesIcons::setNodePropertyAutoUndo(node, KisLayerPropertiesIcons::locked, false, image);
            } else {
                KisLayerPropertiesIcons::setNodePropertyAutoUndo(node, KisLayerPropertiesIcons::visible, !node->visible(), image);
            }
            d->compressor->start();
        } else if (d->delegate->menuRect(r).contains(pos) || me->button() == Qt::RightButton) {
            showLayerMenu(node, d->view->viewport()->mapToGlobal(pos));
        } else {
            activate(node);
        }
        return true;
    }
    return QWidget::eventFilter(watched, event);
}
