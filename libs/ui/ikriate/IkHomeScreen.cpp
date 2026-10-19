/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "IkHomeScreen.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QRadialGradient>
#include <QScroller>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QVBoxLayout>

#include <KConfigGroup>
#include <KSharedConfig>
#include <klocalizedstring.h>

#include <KisRecentFilesManager.h>

#include "KisMainWindow.h"
#include "utils/KisRecentDocumentsModelWrapper.h"

#include "IkInterfaceManager.h"
#include "IkLaunchScreen.h"
#include "IkNewCanvasDialog.h"
#include "IkRecycleBin.h"
#include "IkSettingsPage.h"
#include "IkStyle.h"
#include "IkWidgets.h"

namespace
{
const char *const kFavoritesKey = "FavoriteDocuments";

QString prettyName(const QUrl &url)
{
    const QString file = url.fileName();
    const int dot = file.lastIndexOf(QLatin1Char('.'));
    return dot > 0 ? file.left(dot) : file;
}

QString prettyDate(const QUrl &url)
{
    if (!url.isLocalFile()) return QString();
    const QFileInfo fi(url.toLocalFile());
    if (!fi.exists()) return i18nc("document is missing", "Missing");
    return QLocale().toString(fi.lastModified().date(), QLocale::ShortFormat);
}

// ------------------------------------------------------------------ header

class IkHomeHeader : public QWidget
{
public:
    explicit IkHomeHeader(QWidget *parent) : QWidget(parent)
    {
        setAttribute(Qt::WA_Hover);
        setMouseTracking(true);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    int current {1};
    std::function<void(int)> onTab;
    std::function<void()> onHelp;

    QSize sizeHint() const override { return QSize(800, IkStyle::touchFirst() ? 96 : 84); }

    QRectF tabRect(int i) const
    {
        static const qreal centres[3] = {0.215, 0.5, 0.78};
        const qreal w = qMax<qreal>(width() * 0.12, 90);
        return QRectF(width() * centres[i] - w / 2, 0, w, height());
    }
    QRectF helpRect() const { const qreal s = height() * 0.5; return QRectF(width() - s - 20, height() * 0.18, s, s); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QPalette pal = palette();
        p.fillRect(rect(), IkStyle::color(IkStyle::Chrome, pal));
        const QColor accent = IkStyle::color(IkStyle::Accent, pal);
        const qreal dpr = devicePixelRatioF();

        // Logo with an accent ring, wordmark and underline.
        const qreal ld = height() * 0.86;
        const QRectF logo(14, (height() - ld) / 2, ld, ld);
        p.drawPixmap(logo.adjusted(4, 4, -4, -4), IkStyle::pixmap(QStringLiteral("logo"), qRound(ld - 8), Qt::white, dpr),
                     QRectF(0, 0, (ld - 8) * dpr, (ld - 8) * dpr));
        p.setPen(QPen(accent, qMax<qreal>(3, ld * 0.05)));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(logo.adjusted(2, 2, -2, -2));
        QFont wf = IkStyle::font(2.0, QFont::Bold);
        p.setFont(wf);
        p.setPen(IkStyle::color(IkStyle::Text, pal));
        const qreal wx = logo.right() + 10;
        QFontMetricsF fm(wf);
        p.drawText(QPointF(wx, height() * 0.45), QStringLiteral("IKriate"));
        p.setPen(QPen(accent, 3));
        p.drawLine(QPointF(logo.right() - 2, height() * 0.53), QPointF(wx + fm.horizontalAdvance(QStringLiteral("IKriate")) + 30, height() * 0.53));

        // Tabs: favourites, home, settings
        static const char *icons[3] = {"star", "home", "settings"};
        for (int i = 0; i < 3; ++i) {
            const QRectF r = tabRect(i);
            const int s = qRound(height() * 0.36);
            const QColor c = i == current ? accent : IkStyle::color(IkStyle::MutedText, pal);
            const QRectF ir(r.center().x() - s / 2.0, height() * 0.73 - s, s, s);
            p.drawPixmap(ir, IkStyle::pixmap(QString::fromLatin1(icons[i]), s, c, dpr), QRectF(0, 0, s * dpr, s * dpr));
            if (i == current) {
                p.setPen(Qt::NoPen);
                p.setBrush(accent);
                p.drawRoundedRect(QRectF(r.left(), height() - 4, r.width(), 3.5), 1.5, 1.5);
            }
        }
        const QRectF h = helpRect();
        p.drawPixmap(h, IkStyle::pixmap(QStringLiteral("help"), qRound(h.width()), IkStyle::color(IkStyle::MutedText, pal), dpr),
                     QRectF(0, 0, h.width() * dpr, h.height() * dpr));
    }

    void mouseReleaseEvent(QMouseEvent *e) override
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        const QPointF pos = e->position();
#else
        const QPointF pos = e->localPos();
#endif
        for (int i = 0; i < 3; ++i) {
            if (tabRect(i).contains(pos)) {
                if (onTab) onTab(i);
                return;
            }
        }
        if (helpRect().adjusted(-10, -10, 10, 10).contains(pos) && onHelp) onHelp();
    }
};

// ----------------------------------------------------------------- gallery

class IkFavoritesFilter : public QSortFilterProxyModel
{
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    bool onlyFavorites {false};
    QSet<QString> favorites;

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override
    {
        if (!onlyFavorites) return true;
        const QUrl url = sourceModel()->index(row, 0, parent).data(Qt::UserRole + 1).toUrl();
        return favorites.contains(url.toString());
    }
};

class IkGalleryDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSet<QString> *favorites {nullptr};

    static QSize cell() { return IkStyle::touchFirst() ? QSize(204, 292) : QSize(186, 270); }
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override { return cell(); }

    static QRectF pageRect(const QRect &r)
    {
        const qreal w = r.width() * 0.84;
        return QRectF(r.center().x() - w / 2, r.top() + 14, w, w * 1.37);
    }
    static QRectF menuRect(const QRect &r)
    {
        const QRectF pg = pageRect(r);
        return QRectF(pg.right() - 36, pg.bottom() + 4, 36, 30);
    }

    void paint(QPainter *p, const QStyleOptionViewItem &o, const QModelIndex &index) const override
    {
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        p->setRenderHint(QPainter::SmoothPixmapTransform);
        const QPalette pal = o.palette;
        const QRectF page = pageRect(o.rect);
        const bool hover = o.state & QStyle::State_MouseOver;

        IkStyle::drawShadow(*p, page, 8, 8, 60);
        QPainterPath clip;
        clip.addRoundedRect(page, 8, 8);
        p->setPen(Qt::NoPen);
        p->setBrush(Qt::white);
        p->drawPath(clip);
        const QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
        const QUrl url = index.data(Qt::UserRole + 1).toUrl();
        const QPixmap thumb = icon.pixmap(QSize(qRound(page.width() * 2), qRound(page.height() * 2)));
        // Krita's placeholder for "no thumbnail yet" is a floppy; draw blank paper instead.
        if (!thumb.isNull() && thumb.width() > 48) {
            p->setClipPath(clip);
            const QSizeF s = QSizeF(thumb.size()).scaled(page.size(), Qt::KeepAspectRatioByExpanding);
            QRectF t(QPointF(), s);
            t.moveCenter(page.center());
            p->drawPixmap(t, thumb, QRectF(thumb.rect()));
            p->setClipping(false);
        }
        if (hover) {
            p->setPen(QPen(IkStyle::color(IkStyle::Accent, pal), 2.5));
            p->setBrush(Qt::NoBrush);
            p->drawRoundedRect(page.adjusted(-1, -1, 1, 1), 9, 9);
        }
        if (favorites && favorites->contains(url.toString())) {
            const int s = 22;
            p->drawPixmap(QRectF(page.right() - s - 8, page.top() + 8, s, s),
                          IkStyle::pixmap(QStringLiteral("star"), s, IkStyle::color(IkStyle::Accent, pal), p->device()->devicePixelRatioF()),
                          QRectF(0, 0, s * p->device()->devicePixelRatioF(), s * p->device()->devicePixelRatioF()));
        }

        p->setPen(IkStyle::color(IkStyle::Text, pal));
        p->setFont(IkStyle::font(1.25));
        const QRectF name(o.rect.left() + 4, page.bottom() + 8, o.rect.width() - 8, 30);
        p->drawText(name, Qt::AlignHCenter | Qt::AlignVCenter,
                    QFontMetrics(p->font()).elidedText(prettyName(url), Qt::ElideMiddle, int(name.width() - 40)));
        p->setPen(IkStyle::color(IkStyle::MutedText, pal));
        p->setFont(IkStyle::font(0.85));
        p->drawText(QRectF(name.left(), name.bottom(), name.width(), 22), Qt::AlignHCenter | Qt::AlignTop, prettyDate(url));

        const QRectF m = menuRect(o.rect);
        const int s = 20;
        p->drawPixmap(QRectF(m.center() - QPointF(s, s) / 2, QSizeF(s, s)),
                      IkStyle::pixmap(QStringLiteral("more-vertical"), s, IkStyle::color(IkStyle::MutedText, pal), p->device()->devicePixelRatioF()),
                      QRectF(0, 0, s * p->device()->devicePixelRatioF(), s * p->device()->devicePixelRatioF()));
        p->restore();
    }
};

class IkGalleryPage : public QWidget
{
public:
    IkGalleryPage(const QString &title, bool favoritesOnly, QWidget *parent) : QWidget(parent)
    {
        QVBoxLayout *l = new QVBoxLayout(this);
        l->setContentsMargins(40, 26, 40, 0);
        l->setSpacing(12);
        IkSectionHeader *header = new IkSectionHeader(title, this);
        header->setLarge(true);
        l->addWidget(header);

        filter = new IkFavoritesFilter(this);
        filter->onlyFavorites = favoritesOnly;
        filter->setSourceModel(&KisRecentDocumentsModelWrapper::instance()->model());
        delegate = new IkGalleryDelegate(this);
        delegate->favorites = &filter->favorites;

        view = new QListView(this);
        view->setViewMode(QListView::IconMode);
        view->setResizeMode(QListView::Adjust);
        view->setMovement(QListView::Static);
        view->setUniformItemSizes(true);
        view->setGridSize(IkGalleryDelegate::cell() + QSize(18, 10));
        view->setModel(filter);
        view->setItemDelegate(delegate);
        view->setFrameShape(QFrame::NoFrame);
        view->setSelectionMode(QAbstractItemView::NoSelection);
        view->setMouseTracking(true);
        view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        IkStyle::makeTransparent(view);
        view->viewport()->setAutoFillBackground(false);
        view->viewport()->setAttribute(Qt::WA_Hover);
        QScroller::grabGesture(view->viewport(), IkStyle::touchFirst() ? QScroller::LeftMouseButtonGesture : QScroller::TouchGesture);
        l->addWidget(view, 1);

        empty = new QLabel(this);
        empty->setAlignment(Qt::AlignCenter);
        empty->setWordWrap(true);
        empty->setForegroundRole(QPalette::PlaceholderText);
        empty->setFont(IkStyle::font(1.15));
        empty->setText(favoritesOnly ? i18n("Star artworks in the gallery to keep them here.")
                                     : i18n("Your artworks will appear here.\nTap + to start a new canvas."));
        l->addWidget(empty, 1);
        l->addSpacing(110); // room for the floating "+" bar
    }

    void refreshFavorites(const QList<QUrl> &favs)
    {
        filter->favorites.clear();
        for (const QUrl &u : favs) filter->favorites.insert(u.toString());
        filter->invalidate();
        const bool none = filter->rowCount() == 0;
        empty->setVisible(none);
        view->setVisible(!none);
        view->viewport()->update();
    }

    QListView *view {nullptr};
    IkFavoritesFilter *filter {nullptr};
    IkGalleryDelegate *delegate {nullptr};
    QLabel *empty {nullptr};
};

// ------------------------------------------------------- floating controls

/// Rounded bar behind the "+" button, with the palette and profile buttons.
class IkDockPill : public QWidget
{
public:
    explicit IkDockPill(QWidget *parent) : QWidget(parent) {}

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
        IkStyle::drawShadow(p, r, r.height() / 2, 10, 70);
        p.setPen(Qt::NoPen);
        p.setBrush(IkStyle::color(IkStyle::Raised));
        p.drawRoundedRect(r, r.height() / 2, r.height() / 2);
    }
};

/// Soft accent glow behind the open create menu.
class IkGlow : public QWidget
{
public:
    explicit IkGlow(QWidget *parent) : QWidget(parent) { setAttribute(Qt::WA_TransparentForMouseEvents); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        QRadialGradient g(QPointF(width() / 2.0, height() * 0.72), width() * 0.5);
        g.setColorAt(0.0, IkStyle::withAlpha(IkStyle::color(IkStyle::Accent), 90));
        g.setColorAt(0.45, IkStyle::withAlpha(IkStyle::color(IkStyle::Accent), 30));
        g.setColorAt(1.0, QColor(0, 0, 0, 0));
        p.fillRect(rect(), g);
    }
};

/// Icon tile with a caption next to or above it (create menu).
class IkCreateItem : public QWidget
{
public:
    enum Caption { Above, Left, Right };
    IkCreateItem(const QString &icon, const QString &text, Caption caption, QWidget *parent)
        : QWidget(parent), m_caption(caption)
    {
        button = new IkIconButton(icon, text, this);
        button->setVariant(IkIconButton::Tile);
        const int s = IkStyle::touchFirst() ? 64 : 56;
        button->setEdge(s);
        label = new QLabel(text, this);
        label->setFont(IkStyle::font(1.7));
        label->setForegroundRole(QPalette::WindowText);
        label->adjustSize();
        if (caption == Above) {
            resize(qMax(s, label->width()), s + label->height() + 4);
            label->move((width() - label->width()) / 2, 0);
            button->move((width() - s) / 2, label->height() + 4);
        } else if (caption == Left) {
            resize(label->width() + 16 + s, qMax(s, label->height()));
            label->move(0, (height() - label->height()) / 2);
            button->move(label->width() + 16, (height() - s) / 2);
        } else {
            resize(label->width() + 16 + s, qMax(s, label->height()));
            button->move(0, (height() - s) / 2);
            label->move(s + 16, (height() - label->height()) / 2);
        }
    }
    IkIconButton *button {nullptr};
    QLabel *label {nullptr};

private:
    Caption m_caption;
};
}

// ================================================================== screen

struct IkHomeScreen::Private
{
    QPointer<KisMainWindow> mw;
    QPointer<IkInterfaceManager> manager;
    IkHomeHeader *header {nullptr};
    QStackedWidget *pages {nullptr};
    IkGalleryPage *favorites {nullptr};
    IkGalleryPage *gallery {nullptr};
    IkSettingsPage *settings {nullptr};
    IkDockPill *pill {nullptr};
    IkIconButton *palette {nullptr};
    IkIconButton *profile {nullptr};
    IkIconButton *fab {nullptr};
    IkGlow *glow {nullptr};
    IkCreateItem *newItem {nullptr};
    IkCreateItem *templateItem {nullptr};
    IkCreateItem *openItem {nullptr};
    bool menuOpen {false};
    bool firstShow {true};
};

IkHomeScreen::IkHomeScreen(KisMainWindow *mainWindow)
    : QWidget(mainWindow)
    , d(new Private)
{
    d->mw = mainWindow;
    setObjectName(QStringLiteral("IkHomeScreen"));
    setAutoFillBackground(false);

    QVBoxLayout *l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(0);
    d->header = new IkHomeHeader(this);
    d->header->onTab = [this](int i) { showPage(Page(i)); };
    d->header->onHelp = [this]() {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/DR-KD1997/IKriate#readme")));
    };
    l->addWidget(d->header);

    d->pages = new QStackedWidget(this);
    d->favorites = new IkGalleryPage(i18nc("home tab title", "FAVORITES"), true, d->pages);
    d->gallery = new IkGalleryPage(i18nc("home tab title", "GALLERY"), false, d->pages);
    d->settings = new IkSettingsPage(mainWindow, d->pages);
    d->pages->addWidget(d->favorites);
    d->pages->addWidget(d->gallery);
    d->pages->addWidget(d->settings);
    l->addWidget(d->pages, 1);

    for (IkGalleryPage *g : {d->favorites, d->gallery}) {
        g->view->viewport()->installEventFilter(this);
        connect(g->view, &QAbstractItemView::clicked, this, [this, g](const QModelIndex &idx) {
            Q_UNUSED(g);
            openDocument(idx.data(Qt::UserRole + 1).toUrl());
        });
    }
    connect(&KisRecentDocumentsModelWrapper::instance()->model(), &QAbstractItemModel::rowsInserted, this, &IkHomeScreen::reload);
    connect(&KisRecentDocumentsModelWrapper::instance()->model(), &QAbstractItemModel::rowsRemoved, this, &IkHomeScreen::reload);
    connect(&KisRecentDocumentsModelWrapper::instance()->model(), &QAbstractItemModel::modelReset, this, &IkHomeScreen::reload);

    // Floating create bar
    d->glow = new IkGlow(this);
    d->glow->hide();
    d->pill = new IkDockPill(this);
    d->palette = new IkIconButton(QStringLiteral("palette"), i18n("Brushes and resources"), d->pill);
    d->palette->setIconPixelSize(28);
    d->profile = new IkIconButton(QStringLiteral("user"), i18n("About IKriate"), d->pill);
    d->profile->setIconPixelSize(28);
    connect(d->palette, &QAbstractButton::clicked, this, [this]() {
        if (d->manager) d->manager->triggerAction(QStringLiteral("manage_resources"));
    });
    connect(d->profile, &QAbstractButton::clicked, this, [this]() {
        if (d->manager) d->manager->triggerAction(QStringLiteral("help_about_app"));
    });
    d->fab = new IkIconButton(QStringLiteral("plus"), i18n("Create"), this);
    d->fab->setVariant(IkIconButton::Fab);
    d->fab->setEdge(IkStyle::touchFirst() ? 112 : 100);
    connect(d->fab, &QAbstractButton::clicked, this, [this]() { setCreateMenuOpen(!d->menuOpen); });

    d->newItem = new IkCreateItem(QStringLiteral("file-new"), i18n("New"), IkCreateItem::Above, this);
    d->templateItem = new IkCreateItem(QStringLiteral("template"), i18n("Template"), IkCreateItem::Left, this);
    d->openItem = new IkCreateItem(QStringLiteral("open"), i18n("Open"), IkCreateItem::Right, this);
    connect(d->newItem->button, &QAbstractButton::clicked, this, [this]() {
        setCreateMenuOpen(false);
        IkNewCanvasDialog::showNewCanvas(d->mw);
    });
    connect(d->templateItem->button, &QAbstractButton::clicked, this, [this]() {
        setCreateMenuOpen(false);
        IkNewCanvasDialog::showTemplates(d->mw);
    });
    connect(d->openItem->button, &QAbstractButton::clicked, this, [this]() {
        setCreateMenuOpen(false);
        if (d->mw) d->mw->slotFileOpen();
    });
    setCreateMenuOpen(false);
    installEventFilter(this);

    showPage(GalleryPage);
    reload();
}

IkHomeScreen::~IkHomeScreen()
{
    delete d;
}

void IkHomeScreen::setManager(IkInterfaceManager *manager)
{
    d->manager = manager;
    d->settings->setManager(manager);
}

void IkHomeScreen::showPage(Page page)
{
    d->header->current = int(page);
    d->header->update();
    d->pages->setCurrentIndex(int(page));
    if (page == SettingsPage) d->settings->reload();
    // The create bar is for the gallery pages.
    const bool bar = page != SettingsPage;
    d->pill->setVisible(bar);
    d->fab->setVisible(bar);
    if (!bar) setCreateMenuOpen(false);
    layoutFloatingButtons();
}

QList<QUrl> IkHomeScreen::favoriteDocuments()
{
    const QStringList list = KConfigGroup(KSharedConfig::openConfig(), "IKriate").readEntry(kFavoritesKey, QStringList());
    QList<QUrl> urls;
    for (const QString &s : list) urls << QUrl(s);
    return urls;
}

void IkHomeScreen::setFavoriteDocument(const QUrl &url, bool favorite)
{
    KConfigGroup cfg(KSharedConfig::openConfig(), "IKriate");
    QStringList list = cfg.readEntry(kFavoritesKey, QStringList());
    list.removeAll(url.toString());
    if (favorite) list.prepend(url.toString());
    cfg.writeEntry(kFavoritesKey, list);
}

void IkHomeScreen::reload()
{
    const QList<QUrl> favs = favoriteDocuments();
    d->favorites->refreshFavorites(favs);
    d->gallery->refreshFavorites(favs);
}

void IkHomeScreen::openDocument(const QUrl &url)
{
    if (!d->mw || !url.isValid()) return;
    if (url.isLocalFile() && !QFileInfo::exists(url.toLocalFile())) {
        if (QMessageBox::question(this, i18n("Missing Artwork"),
                                  i18n("\"%1\" can no longer be found. Remove it from the gallery?", prettyName(url)))
            == QMessageBox::Yes) {
            KisRecentFilesManager::instance()->remove(url);
        }
        return;
    }
    d->mw->slotFileOpenRecent(url);
}

void IkHomeScreen::showDocumentMenu(const QUrl &url, const QPoint &globalPos)
{
    QMenu menu(this);
    QAction *open = menu.addAction(IkStyle::icon(QStringLiteral("open")), i18n("Open"));
    const bool fav = favoriteDocuments().contains(url);
    QAction *star = menu.addAction(IkStyle::icon(QStringLiteral("star")), fav ? i18n("Remove from Favorites") : i18n("Add to Favorites"));
    QAction *folder = nullptr;
#ifndef Q_OS_ANDROID
    if (url.isLocalFile()) folder = menu.addAction(IkStyle::icon(QStringLiteral("open")), i18n("Show in Folder"));
#endif
    menu.addSeparator();
    QAction *forget = menu.addAction(i18n("Remove from Gallery"));
    QAction *bin = nullptr;
    if (url.isLocalFile() && QFileInfo::exists(url.toLocalFile())) {
        bin = menu.addAction(IkStyle::icon(QStringLiteral("trash")), i18n("Move to Recycle Bin"));
    }
    QAction *picked = menu.exec(globalPos);
    if (!picked) return;
    if (picked == open) {
        openDocument(url);
    } else if (picked == star) {
        setFavoriteDocument(url, !fav);
        reload();
    } else if (picked == folder) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(url.toLocalFile()).absolutePath()));
    } else if (picked == forget) {
        KisRecentFilesManager::instance()->remove(url);
        setFavoriteDocument(url, false);
    } else if (picked == bin) {
        QString error;
        if (IkRecycleBin::moveToBin(url.toLocalFile(), &error)) {
            KisRecentFilesManager::instance()->remove(url);
            setFavoriteDocument(url, false);
        } else {
            QMessageBox::warning(this, i18n("Recycle Bin"), error);
        }
    }
}

void IkHomeScreen::setCreateMenuOpen(bool open)
{
    d->menuOpen = open;
    d->glow->setVisible(open);
    d->newItem->setVisible(open);
    d->templateItem->setVisible(open);
    d->openItem->setVisible(open);
    d->fab->setIconName(open ? QStringLiteral("close") : QStringLiteral("plus"));
    layoutFloatingButtons();
}

void IkHomeScreen::layoutFloatingButtons()
{
    const int pillW = IkStyle::touchFirst() ? 420 : 380;
    const int pillH = IkStyle::touchFirst() ? 88 : 80;
    const int cx = width() / 2;
    const int bottom = height() - 28;
    d->pill->setGeometry(cx - pillW / 2, bottom - pillH, pillW, pillH);
    const int b = pillH - 16;
    d->palette->setEdge(b);
    d->profile->setEdge(b);
    d->palette->move(pillW / 2 - 96 - b / 2 - 16, 8);
    d->profile->move(pillW / 2 + 96 - b / 2 + 16, 8);
    const int fs = d->fab->width();
    d->fab->move(cx - fs / 2, bottom - pillH / 2 - fs / 2 - 4);

    const QRect fab = d->fab->geometry();
    d->glow->setGeometry(cx - 320, fab.top() - 300, 640, 470);
    d->newItem->move(cx - d->newItem->width() / 2, fab.top() - d->newItem->height() - 22);
    const int sideY = fab.top() - d->templateItem->height() / 2 - 32;
    d->templateItem->move(cx - 70 - d->templateItem->width(), sideY);
    d->openItem->move(cx + 70, sideY);

    for (QWidget *w : {static_cast<QWidget *>(d->glow), static_cast<QWidget *>(d->pill), static_cast<QWidget *>(d->fab),
                       static_cast<QWidget *>(d->newItem), static_cast<QWidget *>(d->templateItem),
                       static_cast<QWidget *>(d->openItem)}) {
        w->raise();
    }
}

void IkHomeScreen::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    layoutFloatingButtons();
}

void IkHomeScreen::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    reload();
    if (d->firstShow) {
        d->firstShow = false;
        // The home screen is the first thing users see: the Android launch
        // logo can go now.
        IkLaunchScreen::hide();
    }
}

void IkHomeScreen::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), IkStyle::color(IkStyle::Background, palette()));
}

bool IkHomeScreen::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == this && event->type() == QEvent::MouseButtonPress && d->menuOpen) {
        setCreateMenuOpen(false);
    }
    for (IkGalleryPage *g : {d->favorites, d->gallery}) {
        if (watched == g->view->viewport() && event->type() == QEvent::MouseButtonRelease) {
            QMouseEvent *me = static_cast<QMouseEvent *>(event);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            const QPoint pos = me->position().toPoint();
#else
            const QPoint pos = me->pos();
#endif
            if (d->menuOpen) {
                setCreateMenuOpen(false);
                return true;
            }
            const QModelIndex idx = g->view->indexAt(pos);
            if (idx.isValid() && (me->button() == Qt::RightButton
                                  || IkGalleryDelegate::menuRect(g->view->visualRect(idx)).contains(pos))) {
                showDocumentMenu(idx.data(Qt::UserRole + 1).toUrl(), g->view->viewport()->mapToGlobal(pos));
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}
