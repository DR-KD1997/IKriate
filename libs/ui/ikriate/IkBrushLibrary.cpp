/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "IkBrushLibrary.h"

#include <QAbstractListModel>
#include <QApplication>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QScrollBar>
#include <QScroller>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include <QFileInfo>
#include <QSet>

#include <KConfigGroup>
#include <KoResourcePaths.h>
#include <KSharedConfig>
#include <klocalizedstring.h>

#include <KisResourceModel.h>
#include <KisResourceTypes.h>
#include <KisTagFilterResourceProxyModel.h>
#include <KisTagModel.h>
#include <KoCanvasResourceProvider.h>
#include <KoCanvasResourcesIds.h>
#include <brushengine/kis_paintop_preset.h>

#include "KisViewManager.h"
#include "kis_canvas_resource_provider.h"
#include "kis_paintop_box.h"

#include "IkInterfaceManager.h"
#include "IkStrokePreview.h"
#include "IkStyle.h"
#include "IkWidgets.h"

namespace
{
const char *const kFavouritesTag = "★ My Favorites";
const char *const kRecentKey = "RecentBrushes";
const int kMaxRecent = 24;

enum Kind { NewBrushes, Recent, Favourites, Tag, All };

struct Category {
    Kind kind;
    QString title;
    QString icon;
    QString tagUrl;
};

/// IKriate brush categories (tag URL, icon). Display names are translated.
QList<Category> builtInCategories()
{
    return {
        {NewBrushes, i18nc("brush category", "New"), QStringLiteral("plus"), QString()},
        {Recent, i18nc("brush category", "Recent"), QStringLiteral("recent"), QString()},
        {Favourites, i18nc("brush category", "Favorites"), QStringLiteral("heart"), QString::fromUtf8(kFavouritesTag)},
        {Tag, i18nc("brush category", "Pencils"), QStringLiteral("pencil"), QStringLiteral("Pencils")},
        {Tag, i18nc("brush category", "Pens"), QStringLiteral("pen"), QStringLiteral("Pens")},
        {Tag, i18nc("brush category", "Calligraphy"), QStringLiteral("calligraphy"), QStringLiteral("Calligraphy")},
        {Tag, i18nc("brush category", "Markers"), QStringLiteral("marker"), QStringLiteral("Markers")},
        {Tag, i18nc("brush category", "Paint"), QStringLiteral("paint"), QStringLiteral("Paint")},
        {Tag, i18nc("brush category", "Watercolor"), QStringLiteral("watercolor"), QStringLiteral("Watercolor")},
        {Tag, i18nc("brush category", "Sprayers"), QStringLiteral("sprayer"), QStringLiteral("Sprayers")},
        {Tag, i18nc("brush category", "Charcoal & Chalk"), QStringLiteral("charcoal"), QStringLiteral("Charcoal & Chalk")},
        {Tag, i18nc("brush category", "Blenders"), QStringLiteral("blender"), QStringLiteral("Blenders")},
        {Tag, i18nc("brush category", "Textures"), QStringLiteral("texture"), QStringLiteral("Textures")},
        {Tag, i18nc("brush category", "Stamps"), QStringLiteral("stamp"), QStringLiteral("Stamps")},
        {Tag, i18nc("brush category", "Shapes"), QStringLiteral("shapes"), QStringLiteral("Shapes")},
        {Tag, i18nc("brush category", "Pixel Art"), QStringLiteral("pixel"), QStringLiteral("Pixel Art")},
        {Tag, i18nc("brush category", "Effects"), QStringLiteral("effects"), QStringLiteral("Effects")},
        {Tag, i18nc("brush category", "Basic"), QStringLiteral("basic"), QStringLiteral("Basic")},
    };
}

bool isBundledStorage(const QString &storage)
{
    return storage.endsWith(QLatin1String("IKriate_Brushes.bundle"))
        || storage.endsWith(QLatin1String("RGBA_Brushes.bundle"))
        || storage.endsWith(QLatin1String("Krita_4_Default_Resources.bundle"))
        || storage.endsWith(QLatin1String("Krita_3_Default_Resources.bundle"))
        || storage.endsWith(QLatin1String("RGBA_brushes.bundle"));
}

/// File names of presets IKriate installs loose (copied into the user folder
/// on first run), so they do not count as the user's own brushes.
const QSet<QString> &shippedFilenames()
{
    static QSet<QString> names = [] {
        QSet<QString> set;
        const QStringList files = KoResourcePaths::findAllAssets("data", QStringLiteral("paintoppresets/*"));
        for (const QString &f : files) set.insert(QFileInfo(f).fileName());
        return set;
    }();
    return names;
}

bool isUserBrush(const KoResourceSP &res)
{
    return !isBundledStorage(res->storageLocation()) && !shippedFilenames().contains(res->filename());
}

QString displayName(const KoResourceSP &res)
{
    // Krita preset names carry a sort prefix ("c) Pencil-2"); hide it.
    QString n = res ? res->name() : QString();
    if (n.size() > 3 && n.at(1) == QLatin1Char(')') && n.at(2) == QLatin1Char(' ')) n = n.mid(3);
    n.replace(QLatin1Char('_'), QLatin1Char(' '));
    return n;
}
}

// ============================================================ category list

class IkCategoryList : public QWidget
{
public:
    explicit IkCategoryList(QWidget *parent) : QWidget(parent)
    {
        setMouseTracking(true);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    }

    QList<Category> categories;
    int current {1};
    bool compact {false};
    std::function<void(int)> onSelect;

    int rowHeight() const { return IkStyle::touchSize() + 24; }

    QSize sizeHint() const override
    {
        QFontMetrics fm(IkStyle::font(1.1));
        int w = 0;
        for (const Category &c : categories) w = qMax(w, fm.horizontalAdvance(c.title));
        return QSize(compact ? rowHeight() : w + 88, rowHeight() * categories.size() + 8);
    }

    QSize minimumSizeHint() const override { return QSize(sizeHint().width(), rowHeight() * 3); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QPalette pal = palette();
        p.setFont(IkStyle::font(1.1));
        for (int i = 0; i < categories.size(); ++i) {
            const QRect r(0, 4 + i * rowHeight(), width(), rowHeight());
            const bool sel = i == current;
            const int d = qMin(r.height() - 16, IkStyle::touchSize());
            const QRectF circle(r.left() + 12, r.center().y() - d / 2.0, d, d);
            if (sel) {
                p.setPen(Qt::NoPen);
                p.setBrush(IkStyle::color(IkStyle::Accent, pal));
                p.drawEllipse(circle);
            }
            const int icon = qRound(d * 0.5);
            const QColor ic = sel ? IkStyle::color(IkStyle::AccentText, pal) : IkStyle::color(IkStyle::Text, pal);
            p.drawPixmap(QRectF(circle.center() - QPointF(icon, icon) / 2.0, QSizeF(icon, icon)),
                         IkStyle::pixmap(categories[i].icon, icon, ic, devicePixelRatioF()),
                         QRectF(0, 0, icon * devicePixelRatioF(), icon * devicePixelRatioF()));
            if (!compact) {
                p.setPen(sel ? IkStyle::color(IkStyle::Text, pal) : IkStyle::color(IkStyle::Text, pal));
                p.drawText(QRectF(circle.right() + 16, r.top(), r.width() - circle.right() - 16, r.height()),
                           Qt::AlignVCenter | Qt::AlignLeft, categories[i].title);
            }
        }
    }

    void mouseReleaseEvent(QMouseEvent *e) override
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        const int y = qRound(e->position().y());
#else
        const int y = e->pos().y();
#endif
        const int row = (y - 4) / rowHeight();
        if (row >= 0 && row < categories.size() && onSelect) onSelect(row);
    }
};

// ============================================================== preset model

class IkPresetModel : public QAbstractListModel
{
public:
    using QAbstractListModel::QAbstractListModel;
    QVector<KoResourceSP> presets;
    QSet<QString> favourites; // md5 sums
    QString currentName;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : presets.size();
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid() || index.row() >= presets.size()) return QVariant();
        const KoResourceSP &res = presets[index.row()];
        switch (role) {
        case Qt::DisplayRole: return displayName(res);
        case Qt::ToolTipRole: return displayName(res);
        case Qt::UserRole: return res->name() == currentName;
        case Qt::UserRole + 1: return favourites.contains(res->md5Sum(false));
        default: return QVariant();
        }
    }

    void setPresets(const QVector<KoResourceSP> &list)
    {
        beginResetModel();
        presets = list;
        endResetModel();
    }

    void refreshAll()
    {
        if (!presets.isEmpty()) Q_EMIT dataChanged(index(0), index(presets.size() - 1));
    }
};

// ============================================================ preset delegate

class IkPresetDelegate : public QStyledItemDelegate
{
public:
    explicit IkPresetDelegate(QObject *parent) : QStyledItemDelegate(parent) {}
    IkPresetModel *model {nullptr};
    QPersistentModelIndex hover;

    static int cardHeight() { return IkStyle::touchFirst() ? 112 : 100; }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &) const override
    {
        return QSize(option.rect.width(), cardHeight());
    }

    QRectF cardRect(const QRect &r) const { return QRectF(r).adjusted(4, 4, -6, -4); }
    QRectF moreRect(const QRect &r) const { const QRectF c = cardRect(r); return QRectF(c.right() - 40, c.top() + 4, 34, 28); }
    QRectF heartRect(const QRect &r) const { const QRectF c = cardRect(r); return QRectF(c.right() - 40, c.bottom() - 34, 34, 30); }
    QSize previewSize(const QRect &r) const
    {
        const QRectF c = cardRect(r);
        return QSize(qRound(c.width() - 70), qRound(c.height() - 34));
    }

    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const QPalette pal = option.palette;
        const QRectF card = cardRect(option.rect);
        const bool active = index.data(Qt::UserRole).toBool();
        const bool hovered = hover == index;
        const bool fav = index.data(Qt::UserRole + 1).toBool();

        if (active) {
            p->setPen(Qt::NoPen);
            p->setBrush(IkStyle::color(IkStyle::Accent, pal));
            p->drawRoundedRect(card, 8, 8);
        } else if (hovered) {
            p->setPen(QPen(IkStyle::color(IkStyle::Divider, pal), 1));
            p->setBrush(IkStyle::color(IkStyle::Surface, pal).darker(115));
            p->drawRoundedRect(card, 8, 8);
        }

        const QColor ink = active ? IkStyle::mix(IkStyle::color(IkStyle::Accent, pal), Qt::black, 0.78)
                                  : IkStyle::mix(IkStyle::color(IkStyle::Text, pal), IkStyle::color(IkStyle::Background, pal), 0.15);
        if (model && index.row() < model->presets.size()) {
            KisPaintOpPresetSP preset = model->presets[index.row()].dynamicCast<KisPaintOpPreset>();
            const QSize ps = previewSize(option.rect);
            const qreal dpr = p->device()->devicePixelRatioF();
            const QImage mask = IkStrokePreview::instance()->preview(preset, ps * dpr);
            if (!mask.isNull()) {
                const QImage img = IkStrokePreview::tinted(mask, ink);
                p->drawImage(QRectF(card.left() + 18, card.top() + 6, ps.width(), ps.height()), img);
            }
        }
        p->setPen(active ? IkStyle::mix(IkStyle::color(IkStyle::Accent, pal), Qt::black, 0.78)
                         : IkStyle::color(IkStyle::Text, pal));
        p->setFont(IkStyle::font(0.9));
        const QRectF nameRect = card.adjusted(12, 0, -48, -8);
        p->drawText(nameRect, Qt::AlignBottom | Qt::AlignHCenter,
                    QFontMetrics(p->font()).elidedText(index.data().toString(), Qt::ElideRight, int(nameRect.width())));

        // Menu and favourite buttons are always visible (no hover on tablets).
        const QColor idle = IkStyle::color(fav ? IkStyle::Text : IkStyle::FaintText, pal);
        {
            const QColor ic = active ? IkStyle::mix(IkStyle::color(IkStyle::Accent, pal), Qt::black, 0.7)
                                     : hovered ? IkStyle::color(IkStyle::Text, pal) : IkStyle::color(IkStyle::FaintText, pal);
            const QRectF more = moreRect(option.rect);
            const int s = 20;
            p->drawPixmap(QRectF(more.center() - QPointF(s, s) / 2, QSizeF(s, s)),
                          IkStyle::pixmap(QStringLiteral("more"), s, ic, p->device()->devicePixelRatioF()),
                          QRectF(0, 0, s * p->device()->devicePixelRatioF(), s * p->device()->devicePixelRatioF()));
        }
        {
            const QRectF hr = heartRect(option.rect);
            const qreal s = 18;
            QPainterPath heart;
            const QPointF c = hr.center();
            heart.moveTo(c.x(), c.y() + s * 0.42);
            heart.cubicTo(c.x() - s * 0.75, c.y() - s * 0.05, c.x() - s * 0.4, c.y() - s * 0.6, c.x(), c.y() - s * 0.2);
            heart.cubicTo(c.x() + s * 0.4, c.y() - s * 0.6, c.x() + s * 0.75, c.y() - s * 0.05, c.x(), c.y() + s * 0.42);
            const QColor hc = active ? IkStyle::mix(IkStyle::color(IkStyle::Accent, pal), Qt::black, 0.7)
                                     : hovered ? IkStyle::color(IkStyle::Text, pal) : idle;
            p->setPen(QPen(hc, 1.6));
            p->setBrush(fav ? hc : QColor(Qt::transparent));
            p->drawPath(heart);
        }
        p->restore();
    }
};

// ============================================================ the library

struct IkBrushLibrary::Private
{
    IkInterfaceManager *manager {nullptr};
    IkCategoryList *categories {nullptr};
    QListView *view {nullptr};
    IkPresetModel *model {nullptr};
    IkPresetDelegate *delegate {nullptr};
    QScrollArea *categoryScroll {nullptr};
    bool connected {false};
};

IkBrushLibrary::IkBrushLibrary(IkInterfaceManager *manager, QWidget *parent)
    : QWidget(parent)
    , d(new Private)
{
    d->manager = manager;
    setObjectName(QStringLiteral("IkBrushLibrary"));

    QVBoxLayout *outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 4, 4, 8);
    outer->setSpacing(4);

    QHBoxLayout *head = new QHBoxLayout();
    head->addStretch(1);
    IkIconButton *add = new IkIconButton(QStringLiteral("plus"), i18n("New brush from the current one"), this);
    connect(add, &QAbstractButton::clicked, this, [this]() {
        if (d->manager) d->manager->createBrushFromCurrent();
    });
    head->addWidget(add);
    IkIconButton *edit = new IkIconButton(QStringLiteral("sliders"), i18n("Brush settings"), this);
    connect(edit, &QAbstractButton::clicked, this, [this]() {
        if (d->manager) d->manager->showBrushEditor();
    });
    head->addWidget(edit);
    outer->addLayout(head);

    QHBoxLayout *body = new QHBoxLayout();
    body->setSpacing(4);
    outer->addLayout(body, 1);

    d->categories = new IkCategoryList(this);
    d->categories->categories = builtInCategories();
    d->categories->onSelect = [this](int row) { selectCategory(row); };
    d->categoryScroll = new QScrollArea(this);
    d->categoryScroll->setWidget(d->categories);
    d->categoryScroll->setWidgetResizable(true);
    d->categoryScroll->setFrameShape(QFrame::NoFrame);
    d->categoryScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    d->categoryScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    d->categoryScroll->viewport()->setAutoFillBackground(false);
    d->categoryScroll->setAutoFillBackground(false);
    d->categoryScroll->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    QScroller::grabGesture(d->categoryScroll->viewport(), IkStyle::touchFirst() ? QScroller::LeftMouseButtonGesture
                                                                             : QScroller::TouchGesture);
    body->addWidget(d->categoryScroll);

    d->model = new IkPresetModel(this);
    d->delegate = new IkPresetDelegate(this);
    d->delegate->model = d->model;
    d->view = new QListView(this);
    d->view->setModel(d->model);
    d->view->setItemDelegate(d->delegate);
    d->view->setFrameShape(QFrame::NoFrame);
    d->view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    d->view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    d->view->setUniformItemSizes(true);
    d->view->setMouseTracking(true);
    d->view->setSelectionMode(QAbstractItemView::NoSelection);
    d->view->viewport()->setAutoFillBackground(false);
    d->view->setAutoFillBackground(false);
    IkStyle::makeTransparent(d->view);
    QScroller::grabGesture(d->view->viewport(), IkStyle::touchFirst() ? QScroller::LeftMouseButtonGesture
                                                                      : QScroller::TouchGesture);
    d->view->viewport()->installEventFilter(this);
    body->addWidget(d->view, 1);

    connect(d->view, &QAbstractItemView::entered, this, [this](const QModelIndex &idx) {
        d->delegate->hover = idx;
        d->view->viewport()->update();
    });
    connect(IkStrokePreview::instance(), &IkStrokePreview::previewReady, this, [this]() {
        d->view->viewport()->update();
    });

    reload();
}

IkBrushLibrary::~IkBrushLibrary()
{
    delete d;
}

void IkBrushLibrary::setCompact(bool compact)
{
    d->categories->compact = compact;
    d->categories->updateGeometry();
    d->categoryScroll->setFixedWidth(d->categories->sizeHint().width());
    update();
}

QSize IkBrushLibrary::sizeHint() const
{
    return QSize(d->categories->sizeHint().width() + 340, 640);
}

void IkBrushLibrary::noteRecent(const QString &presetName)
{
    if (presetName.isEmpty()) return;
    KConfigGroup cfg(KSharedConfig::openConfig(), "IKriate");
    QStringList list = cfg.readEntry(kRecentKey, QStringList());
    list.removeAll(presetName);
    list.prepend(presetName);
    while (list.size() > kMaxRecent) list.removeLast();
    cfg.writeEntry(kRecentKey, list);
}

QStringList IkBrushLibrary::recentPresets()
{
    return KConfigGroup(KSharedConfig::openConfig(), "IKriate").readEntry(kRecentKey, QStringList());
}

void IkBrushLibrary::reload()
{
    if (!d->connected && d->manager && d->manager->viewManager()) {
        KisViewManager *vm = d->manager->viewManager();
        if (vm->canvasResourceProvider() && vm->canvasResourceProvider()->resourceManager()) {
            d->connected = true;
            connect(vm->canvasResourceProvider()->resourceManager(), &KoCanvasResourceProvider::canvasResourceChanged,
                    this, [this](int key, const QVariant &) {
                        if (key == KoCanvasResource::CurrentPaintOpPreset) slotCurrentPresetChanged();
                    });
            IkStrokePreview::instance()->setCanvasResourcesInterface(
                vm->canvasResourceProvider()->resourceManager()->canvasResourcesInterface());
        }
    }
    // Default to Recent when there is history, otherwise the first brush category.
    if (d->categories->current == 1 && recentPresets().isEmpty()) {
        d->categories->current = 3;
    }
    setCompact(d->categories->compact);
    populatePresets();
    slotCurrentPresetChanged();
}

void IkBrushLibrary::selectCategory(int row)
{
    if (row < 0 || row >= d->categories->categories.size()) return;
    d->categories->current = row;
    d->categories->update();
    populatePresets();
    d->view->scrollToTop();
}

void IkBrushLibrary::populatePresets()
{
    const Category cat = d->categories->categories.value(d->categories->current);
    QVector<KoResourceSP> list;

    KisTagModel tagModel(ResourceType::PaintOpPresets);
    {
        // Favourites
        d->model->favourites.clear();
        if (KisTagSP fav = tagModel.tagForUrl(QString::fromUtf8(kFavouritesTag))) {
            KisTagFilterResourceProxyModel favModel(ResourceType::PaintOpPresets);
            favModel.setTagFilter(fav);
            for (int i = 0; i < favModel.rowCount(); ++i) {
                if (KoResourceSP r = favModel.resourceForIndex(favModel.index(i, 0))) d->model->favourites.insert(r->md5Sum(false));
            }
        }
    }

    switch (cat.kind) {
    case Recent: {
        KisResourceModel all(ResourceType::PaintOpPresets);
        const QStringList names = recentPresets();
        for (const QString &n : names) {
            const QVector<KoResourceSP> found = all.resourcesForName(n);
            if (!found.isEmpty()) list << found.first();
        }
        break;
    }
    case NewBrushes:
    case All: {
        KisResourceModel all(ResourceType::PaintOpPresets);
        for (int i = 0; i < all.rowCount(); ++i) {
            KoResourceSP r = all.resourceForIndex(all.index(i, 0));
            if (!r) continue;
            if (cat.kind == All || isUserBrush(r)) list << r;
        }
        break;
    }
    case Favourites:
    case Tag: {
        if (KisTagSP tag = tagModel.tagForUrl(cat.tagUrl)) {
            KisTagFilterResourceProxyModel m(ResourceType::PaintOpPresets);
            m.setTagFilter(tag);
            for (int i = 0; i < m.rowCount(); ++i) {
                if (KoResourceSP r = m.resourceForIndex(m.index(i, 0))) list << r;
            }
        }
        break;
    }
    }
    d->model->setPresets(list);
}

void IkBrushLibrary::slotCurrentPresetChanged()
{
    KisViewManager *vm = d->manager ? d->manager->viewManager() : nullptr;
    const QString name = (vm && vm->canvasResourceProvider() && vm->canvasResourceProvider()->currentPreset())
        ? vm->canvasResourceProvider()->currentPreset()->name() : QString();
    if (name != d->model->currentName) {
        d->model->currentName = name;
        d->model->refreshAll();
    }
}

void IkBrushLibrary::slotPresetClicked(const QModelIndex &index)
{
    if (!index.isValid() || !d->manager || !d->manager->viewManager()) return;
    KoResourceSP res = d->model->presets.value(index.row());
    KisViewManager *vm = d->manager->viewManager();
    if (!res || !vm->paintOpBox()) return;
    vm->paintOpBox()->resourceSelected(res);
    noteRecent(res->name());
    if (d->manager) d->manager->ensureBrushTool();
    Q_EMIT presetActivated();
}

void IkBrushLibrary::toggleFavourite(KoResourceSP res)
{
    KisTagModel tagModel(ResourceType::PaintOpPresets);
    KisTagSP fav = tagModel.tagForUrl(QString::fromUtf8(kFavouritesTag));
    if (!fav || !res) return;
    KisTagFilterResourceProxyModel m(ResourceType::PaintOpPresets);
    if (d->model->favourites.contains(res->md5Sum(false))) {
        m.untagResources(fav, {res->resourceId()});
        d->model->favourites.remove(res->md5Sum(false));
    } else {
        m.tagResources(fav, {res->resourceId()});
        d->model->favourites.insert(res->md5Sum(false));
    }
    if (d->categories->categories.value(d->categories->current).kind == Favourites) {
        populatePresets();
    } else {
        d->model->refreshAll();
    }
}

void IkBrushLibrary::slotContextMenu(const QModelIndex &index, const QPoint &globalPos)
{
    KoResourceSP res = d->model->presets.value(index.row());
    if (!res) return;
    QMenu menu(this);
    QAction *use = menu.addAction(i18n("Use"));
    QAction *edit = menu.addAction(i18n("Brush Settings..."));
    QAction *fav = menu.addAction(d->model->favourites.contains(res->md5Sum(false)) ? i18n("Remove from Favorites")
                                                                                   : i18n("Add to Favorites"));
    QAction *dup = menu.addAction(i18n("Duplicate"));
    QAction *rename = menu.addAction(i18n("Rename..."));
    QAction *remove = menu.addAction(i18n("Delete"));
    const bool own = isUserBrush(res);
    rename->setEnabled(own);
    remove->setEnabled(own);
    QAction *picked = menu.exec(globalPos);
    if (!picked) return;
    if (picked == use || picked == edit) {
        slotPresetClicked(index);
        if (picked == edit && d->manager) d->manager->showBrushEditor();
    } else if (picked == fav) {
        toggleFavourite(res);
    } else if (picked == dup) {
        KisResourceModel model(ResourceType::PaintOpPresets);
        KoResourceSP copy = res->clone();
        copy->setName(res->name() + i18nc("suffix for a duplicated brush", " copy"));
        copy->setFilename(QString());
        if (model.addResourceDeduplicateFileName(copy)) {
            selectCategory(0); // "New"
        }
    } else if (picked == rename) {
        bool ok = false;
        const QString name = QInputDialog::getText(this, i18n("Rename Brush"), i18n("Name:"), QLineEdit::Normal,
                                                   displayName(res), &ok);
        if (ok && !name.trimmed().isEmpty()) {
            KisResourceModel model(ResourceType::PaintOpPresets);
            model.renameResource(res, name.trimmed());
            populatePresets();
        }
    } else if (picked == remove) {
        if (QMessageBox::question(this, i18n("Delete Brush"), i18n("Delete the brush \"%1\"?", displayName(res)))
            == QMessageBox::Yes) {
            KisResourceModel model(ResourceType::PaintOpPresets);
            model.setResourceInactive(model.indexForResource(res));
            populatePresets();
        }
    }
}

bool IkBrushLibrary::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == d->view->viewport()) {
        if (event->type() == QEvent::Leave) {
            d->delegate->hover = QModelIndex();
            d->view->viewport()->update();
        } else if (event->type() == QEvent::MouseButtonRelease) {
            QMouseEvent *me = static_cast<QMouseEvent *>(event);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            const QPoint pos = me->position().toPoint();
#else
            const QPoint pos = me->pos();
#endif
            // Ignore the release that ends a kinetic scroll.
            QScroller *scroller = QScroller::scroller(d->view->viewport());
            if (scroller && scroller->state() == QScroller::Scrolling) return false;
            const QModelIndex idx = d->view->indexAt(pos);
            if (!idx.isValid()) return false;
            const QRect r = d->view->visualRect(idx);
            d->delegate->hover = idx;
            if (d->delegate->heartRect(r).contains(pos)) {
                toggleFavourite(d->model->presets.value(idx.row()));
            } else if (d->delegate->moreRect(r).contains(pos) || me->button() == Qt::RightButton) {
                slotContextMenu(idx, d->view->viewport()->mapToGlobal(pos));
            } else {
                slotPresetClicked(idx);
            }
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}
