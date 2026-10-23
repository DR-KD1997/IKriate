/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "IkSettingsPage.h"
#include "IkTouchGestures.h"

#ifdef Q_OS_ANDROID
#include "KisAndroidScaling.h"
#endif

#include <QApplication>
#include <QColorDialog>
#include <QMenu>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScroller>
#include <QSettings>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

#include <KConfigGroup>
#include <KSharedConfig>
#include <klocalizedstring.h>

#include <KoFileDialog.h>
#include <kis_global.h>
#include <kis_image_config.h>

#include "KisMainWindow.h"
#include "kis_config.h"
#include "opengl/kis_opengl.h"

#include "IkInterfaceManager.h"
#include "IkRecycleBin.h"
#include "IkSkinManager.h"
#include "IkStyle.h"
#include "IkWidgets.h"
#include "IkMenus.h"
#include "IkReferences.h"

namespace
{
enum Section { ThemeSection, LanguageSection, AdvancedSection, CleanUpSection, RecycleSection };

QString humanSize(qint64 bytes)
{
    return QLocale().formattedDataSize(bytes, 1);
}

qint64 folderSize(const QString &path, const QStringList &filters = QStringList())
{
    qint64 total = 0;
    QDirIterator it(path, filters, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

void clearFolder(const QString &path, const QStringList &filters = QStringList())
{
    QDirIterator it(path, filters, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        QFile::remove(it.filePath());
    }
}

QString strokePreviewCache()
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/stroke-previews");
}

/// A rounded card holding settings rows.
IkPanel *card(QWidget *parent)
{
    IkPanel *p = new IkPanel(parent);
    p->setRole(IkStyle::Surface);
    p->setRadius(16);
    return p;
}

/// Paints a schematic of layout @p mode (bars, panels, the page) into @p r.
void paintLayoutSchematic(QPainter &p, const QRectF &r, IkInterfaceManager::Mode m_mode, const QPalette &pal)
{
    {
        // Bars a step lighter than the panels around the preview.
        const QColor chrome = IkStyle::mix(IkStyle::color(IkStyle::Raised, pal), IkStyle::color(IkStyle::Text, pal), 0.16);
        p.save();
        const QColor surround = IkStyle::color(IkStyle::CanvasSurround, pal);
        const QColor accent = IkStyle::color(IkStyle::Accent, pal);
        p.setPen(Qt::NoPen);
        p.setBrush(surround);
        p.drawRoundedRect(r, 10, 10);
        auto box = [&](const QRectF &b, const QColor &c, qreal rad = 6) { p.setBrush(c); p.drawRoundedRect(b, rad, rad); };
        const qreal w = r.width(), h = r.height();
        const QRectF paper(r.left() + w * 0.3, r.top() + h * 0.16, w * 0.4, h * 0.72);
        switch (m_mode) {
        case IkInterfaceManager::Desktop:
            box(QRectF(r.left(), r.top(), w, h * 0.09), chrome, 4);                                    // top bar
            box(QRectF(r.left(), r.top() + h * 0.1, w * 0.06, h * 0.9), chrome, 4);                   // tool rail
            box(QRectF(r.right() - w * 0.24, r.top() + h * 0.1, w * 0.24, h * 0.9), chrome, 4);        // colour + layers
            box(QRectF(r.left() + w * 0.07, r.bottom() - h * 0.09, w * 0.68, h * 0.09), chrome, 4);    // brush bar
            box(QRectF(r.left() + w * 0.012, r.top() + h * 0.2, w * 0.035, h * 0.05), accent, 3);      // active tool
            box(QRectF(r.left() + w * 0.27, r.top() + h * 0.15, w * 0.44, h * 0.66), Qt::white, 2);
            break;
        case IkInterfaceManager::Kriate:
            box(QRectF(r.left(), r.top(), w, h * 0.1), chrome, 4);
            box(QRectF(r.left() + w * 0.012, r.top() + h * 0.2, w * 0.04, h * 0.66), chrome, 8);       // sliders
            box(QRectF(r.right() - w * 0.27, r.top() + h * 0.13, w * 0.26, h * 0.84), chrome, 8);      // brush library
            box(QRectF(r.right() - w * 0.33, r.top() + h * 0.015, w * 0.035, h * 0.07), accent, 3);
            box(paper.translated(-w * 0.03, 0), Qt::white, 2);
            break;
        case IkInterfaceManager::Minimal:
            box(QRectF(r.left() + w * 0.3, r.top() + h * 0.015, w * 0.36, h * 0.08), chrome, 6);       // tools
            box(QRectF(r.left() + w * 0.012, r.top() + h * 0.015, h * 0.08, h * 0.08), chrome, h * 0.04);
            box(QRectF(r.right() - w * 0.2, r.top() + h * 0.015, w * 0.19, h * 0.08), chrome, h * 0.04);
            box(QRectF(r.left() + w * 0.012, r.top() + h * 0.25, w * 0.035, h * 0.5), chrome, 8);
            box(QRectF(r.left() + w * 0.06, r.top() + h * 0.4, h * 0.07, h * 0.07), accent, h * 0.035);
            box(paper, Qt::white, 2);
            break;
        default:
            box(QRectF(r.left(), r.top(), w, h * 0.12), chrome, 4);
            box(QRectF(r.left(), r.top() + h * 0.13, w * 0.1, h * 0.87), chrome, 4);
            box(QRectF(r.right() - w * 0.26, r.top() + h * 0.13, w * 0.26, h * 0.87), chrome, 4);
            box(paper, Qt::white, 2);
            break;
        }
        p.restore();
    }
}

class IkLayoutPreview : public QWidget
{
public:
    IkLayoutPreview(IkInterfaceManager::Mode mode, QWidget *parent) : QWidget(parent), m_mode(mode)
    {
        setMinimumSize(IkStyle::scaled(420), IkStyle::scaled(280));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        paintLayoutSchematic(p, QRectF(rect()).adjusted(6, 6, -6, -6), m_mode, palette());
    }

private:
    IkInterfaceManager::Mode m_mode;
};

/// Small screen preview of a layout; tap to see it larger.
class IkLayoutThumb : public QAbstractButton
{
public:
    IkLayoutThumb(IkInterfaceManager::Mode mode, QWidget *parent) : QAbstractButton(parent), m_mode(mode)
    {
        setCursor(Qt::PointingHandCursor);
        setToolTip(i18nc("show a preview of the layout", "Preview"));
        setFixedSize(IkStyle::scaled(150), IkStyle::scaled(100));
    }
    bool selected {false};

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QPalette pal = palette();
        const QRectF r = QRectF(rect()).adjusted(2, 2, -2, -2);
        paintLayoutSchematic(p, r.adjusted(2, 2, -2, -2), m_mode, pal);
        if (selected || isDown()) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(IkStyle::color(IkStyle::Accent, pal), 2.5));
            p.drawRoundedRect(r, 10, 10);
        }
    }

private:
    IkInterfaceManager::Mode m_mode;
};

/// "Desktop  [PREVIEW]" row of the Layout card.
class IkLayoutRow : public QWidget
{
public:
    IkLayoutRow(IkInterfaceManager::Mode mode, const QString &title, QWidget *parent) : QWidget(parent), m_title(title)
    {
        setCursor(Qt::PointingHandCursor);
        thumb = new IkLayoutThumb(mode, this);
        preview = thumb;
        setMinimumHeight(thumb->height() + IkStyle::scaled(20));
    }
    bool selected {false};
    std::function<void()> onSelect;
    QAbstractButton *preview {nullptr};
    IkLayoutThumb *thumb {nullptr};

protected:
    void resizeEvent(QResizeEvent *) override
    {
        const QSize s = preview->size();
        preview->setGeometry(width() - s.width() - 18, (height() - s.height()) / 2, s.width(), s.height());
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const QPalette pal = palette();
        if (selected) {
            p.fillRect(rect().adjusted(-1, 0, 1, 0), IkStyle::withAlpha(IkStyle::color(IkStyle::Accent, pal), 22));
            p.setPen(QPen(IkStyle::color(IkStyle::AccentDivider, pal), 2));
            p.drawLine(QPointF(0, 1), QPointF(width(), 1));
            p.drawLine(QPointF(0, height() - 1), QPointF(width(), height() - 1));
        }
        p.setPen(IkStyle::color(IkStyle::Accent, pal));
        p.setFont(IkStyle::font(2.0));
        p.drawText(QRectF(22, 0, width() * 0.6, height()), Qt::AlignVCenter | Qt::AlignLeft, m_title);
    }
    void mouseReleaseEvent(QMouseEvent *) override { if (onSelect) onSelect(); }

private:
    QString m_title;
};
}

// ======================================================================

struct IkSettingsPage::Private
{
    QPointer<KisMainWindow> mw;
    QPointer<IkInterfaceManager> manager;
    QList<IkNavRow *> nav;
    QStackedWidget *pages {nullptr};

    // theme page
    IkSegmented *themeMode {nullptr};
    IkColorWell *tint {nullptr};
    IkColorWell *accent {nullptr};
    QList<IkLayoutRow *> layoutRows;
    IkChoiceRow *scale {nullptr};
    IkChoiceRow *font {nullptr};
    QList<int> scales {75, 90, 100, 110, 125, 150, 175, 200};

    // toggles
    IkSwitch *hwAccel {nullptr};
    IkSwitch *ghosting {nullptr};
    IkSwitch *autoSave {nullptr};

    // clean up / recycle
    QVBoxLayout *cleanRows {nullptr};
    QVBoxLayout *binRows {nullptr};
    QWidget *cleanPage {nullptr};
    QWidget *binPage {nullptr};
};

IkSettingsPage::IkSettingsPage(KisMainWindow *mainWindow, QWidget *parent)
    : QWidget(parent)
    , d(new Private)
{
    d->mw = mainWindow;
    QHBoxLayout *outer = new QHBoxLayout(this);
    outer->setContentsMargins(20, 24, 20, 20);
    outer->setSpacing(24);

    // ---- left: navigation cards ------------------------------------------
    QVBoxLayout *left = new QVBoxLayout();
    left->setSpacing(18);
    auto navCard = [&](const QList<QPair<QString, QString>> &items, const QList<int> &sections) {
        IkPanel *c = card(this);
        QVBoxLayout *cl = new QVBoxLayout(c);
        cl->setContentsMargins(14, 16, 14, 16);
        cl->setSpacing(4);
        for (int i = 0; i < items.size(); ++i) {
            IkNavRow *row = new IkNavRow(items[i].first, items[i].second, c);
            const int section = sections.value(i, -1);
            if (section >= 0) {
                connect(row, &QAbstractButton::clicked, this, [this, section]() { showSection(section); });
                row->setProperty("ikSection", section);
                d->nav << row;
            }
            cl->addWidget(row);
            if (items[i].first == QLatin1String("rocket")) {
                d->hwAccel = new IkSwitch(row);
                row->setTrailing(d->hwAccel);
            } else if (items[i].first == QLatin1String("ghost")) {
                d->ghosting = new IkSwitch(row);
                row->setTrailing(d->ghosting);
            } else if (items[i].first == QLatin1String("save")) {
                d->autoSave = new IkSwitch(row);
                row->setTrailing(d->autoSave);
                row->setToolTip(i18n("Save your changes automatically, without asking, when you return to the gallery."));
            }
        }
        left->addWidget(c);
        return c;
    };
    navCard({{QStringLiteral("theme"), i18n("Theme")}, {QStringLiteral("language"), i18n("Language")},
             {QStringLiteral("advanced"), i18n("Advanced")}},
            {ThemeSection, LanguageSection, AdvancedSection});
    navCard({{QStringLiteral("cleanup"), i18n("Clean Up")}, {QStringLiteral("rocket"), i18n("Hardware Acceleration")},
             {QStringLiteral("ghost"), i18n("Remove Brush Ghosting")}, {QStringLiteral("save"), i18n("Auto-Save")}},
            {CleanUpSection});
    navCard({{QStringLiteral("recycle"), i18n("Recycle Bin")}}, {RecycleSection});
    left->addStretch(1);
    QWidget *leftHost = new QWidget(this);
    leftHost->setLayout(left);
    leftHost->setFixedWidth(IkStyle::scaled(IkStyle::touchFirst() ? 360 : 340));
    outer->addWidget(leftHost);

    connect(d->hwAccel, &QAbstractButton::toggled, this, [this](bool on) {
        KisOpenGL::setUserPreferredOpenGLRendererConfig(on ? KisOpenGL::RendererAuto : KisOpenGL::RendererNone);
        notifyRestart();
    });
    connect(d->ghosting, &QAbstractButton::toggled, this, [](bool on) { setBrushGhostingRemoved(on); });
    connect(d->autoSave, &QAbstractButton::toggled, this, [](bool on) {
        KConfigGroup(KSharedConfig::openConfig(), "IKriate").writeEntry("AutoSave", on);
    });

    // ---- right: pages -------------------------------------------------------
    d->pages = new QStackedWidget(this);
    auto scrolled = [this](QWidget *w) {
        QScrollArea *s = new QScrollArea(d->pages);
        s->setWidget(w);
        s->setWidgetResizable(true);
        s->setFrameShape(QFrame::NoFrame);
        s->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        s->viewport()->setAutoFillBackground(false);
        IkStyle::makeTransparent(s);
        w->setAutoFillBackground(false);
        QScroller::grabGesture(s->viewport(), IkStyle::touchFirst() ? QScroller::LeftMouseButtonGesture : QScroller::TouchGesture);
        return s;
    };
    d->pages->addWidget(scrolled(buildThemePage()));
    d->pages->addWidget(scrolled(buildLanguagePage()));
    d->pages->addWidget(scrolled(buildAdvancedPage()));
    d->pages->addWidget(scrolled(buildCleanUpPage()));
    d->pages->addWidget(scrolled(buildRecycleBinPage()));
    outer->addWidget(d->pages, 1);

    showSection(ThemeSection);
}

IkSettingsPage::~IkSettingsPage()
{
    delete d;
}

void IkSettingsPage::setManager(IkInterfaceManager *manager)
{
    d->manager = manager;
    reload();
}

bool IkSettingsPage::brushGhostingRemoved()
{
    return KisConfig(true).newOutlineStyle() == OUTLINE_NONE;
}

void IkSettingsPage::setBrushGhostingRemoved(bool removed)
{
    KisConfig cfg(false);
    if (removed) {
        if (cfg.newOutlineStyle() != OUTLINE_NONE) cfg.setLastUsedOutlineStyle(cfg.newOutlineStyle());
        cfg.setNewOutlineStyle(OUTLINE_NONE);
        cfg.setEraserOutlineStyle(OUTLINE_NONE);
    } else {
        OutlineStyle last = cfg.lastUsedOutlineStyle();
        if (last == OUTLINE_NONE) last = OUTLINE_FULL;
        cfg.setNewOutlineStyle(last);
        cfg.setEraserOutlineStyle(last);
    }
}

void IkSettingsPage::notifyRestart()
{
    QMessageBox::information(this, i18n("Restart Needed"), i18n("This change takes effect the next time IKriate starts."));
}

int IkSettingsPage::currentSection() const
{
    return d->pages->currentIndex();
}

void IkSettingsPage::openSection(int section)
{
    showSection(section);
}

void IkSettingsPage::showSection(int section)
{
    for (IkNavRow *row : d->nav) row->setSelected(row->property("ikSection").toInt() == section);
    d->pages->setCurrentIndex(section);
    if (section == CleanUpSection) refreshCleanUp();
    if (section == RecycleSection) refreshRecycleBin();
}

void IkSettingsPage::reload()
{
    const IkSkinManager::Settings s = IkSkinManager::instance()->settings();
    d->themeMode->setCurrentIndex(int(s.themeMode));
    d->tint->setColor(s.tint);
    d->tint->setEmpty(s.tintStrength <= 0);
    // What the interface actually shows (the skin's accent, or the override).
    d->accent->setColor(qApp->palette().color(QPalette::Highlight));
    const int mode = d->manager ? int(d->manager->preferredMode()) : int(IkInterfaceManager::platformDefaultMode());
    for (int i = 0; i < d->layoutRows.size(); ++i) {
        d->layoutRows[i]->selected = i == mode;
        d->layoutRows[i]->thumb->selected = i == mode;
        d->layoutRows[i]->thumb->update();
        d->layoutRows[i]->update();
    }
    const int scale = KConfigGroup(KSharedConfig::openConfig(), "IKriate").readEntry("InterfaceScale", 100);
    d->scale->setValueText(QStringLiteral("%1%").arg(scale));
    d->font->setValueText(qApp->font().family());
    {
        QSignalBlocker b1(d->hwAccel), b2(d->ghosting), b3(d->autoSave);
        d->autoSave->setChecked(KConfigGroup(KSharedConfig::openConfig(), "IKriate").readEntry("AutoSave", true));
        d->hwAccel->setChecked(KisOpenGL::getUserPreferredOpenGLRendererConfig() != KisOpenGL::RendererNone);
        d->ghosting->setChecked(brushGhostingRemoved());
    }
}

void IkSettingsPage::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    // A new skin, accent or tint arrives as a palette change: keep the
    // Tint and Accent chips in step with the colours on screen.
    if (event->type() == QEvent::PaletteChange && d->accent) {
        const IkSkinManager::Settings s = IkSkinManager::instance()->settings();
        d->accent->setColor(qApp->palette().color(QPalette::Highlight));
        d->tint->setColor(s.tint);
        d->tint->setEmpty(s.tintStrength <= 0);
    }
}

void IkSettingsPage::applyScale(int pct)
{
    if (pct == KConfigGroup(KSharedConfig::openConfig(), "IKriate").readEntry("InterfaceScale", 100)) return;
    KConfigGroup(KSharedConfig::openConfig(), "IKriate").writeEntry("InterfaceScale", pct);
    KSharedConfig::openConfig()->sync();
    d->scale->setValueText(QStringLiteral("%1%").arg(pct));
#ifdef Q_OS_ANDROID
    // Android scales everything through the screen density, live; nothing
    // needs rebuilding.
    if (KisAndroidScaling *scaling = KisAndroidScaling::instance()) scaling->setInterfaceScalePercent(pct);
#else
    IkStyle::setUiScale(pct / 100.0);
    IkStyle::applyInterfaceFont();
    if (d->manager) d->manager->rebuildInterface();
#endif
}

QWidget *IkSettingsPage::buildThemePage()
{
    QWidget *page = new QWidget();
    QVBoxLayout *l = new QVBoxLayout(page);
    l->setContentsMargins(0, 0, 4, 0);
    l->setSpacing(24);

    // ---- Theme colours ------------------------------------------------------
    IkPanel *colors = card(page);
    QVBoxLayout *cl = new QVBoxLayout(colors);
    cl->setContentsMargins(40, 20, 40, 26);
    cl->setSpacing(22);
    cl->addWidget(new IkSectionHeader(i18n("Theme Colors"), colors));
    d->themeMode = new IkSegmented(colors);
    d->themeMode->addSegment(QStringLiteral("system"), i18n("Follow System"));
    d->themeMode->addSegment(QStringLiteral("light"), i18n("Light Mode"));
    d->themeMode->addSegment(QStringLiteral("dark"), i18n("Dark Mode"));
    cl->addWidget(d->themeMode);
    connect(d->themeMode, &IkSegmented::currentChanged, this, [](int index) {
        IkSkinManager::Settings s = IkSkinManager::instance()->settings();
        s.themeMode = IkSkinManager::ThemeMode(index);
        s.skinId.clear();
        IkSkinManager::instance()->setSettings(s);
    });
    QHBoxLayout *wells = new QHBoxLayout();
    d->tint = new IkColorWell(i18n("Tint"), colors);
    d->accent = new IkColorWell(i18n("Accent"), colors);
    wells->addWidget(d->tint);
    wells->addStretch(1);
    wells->addWidget(d->accent);
    cl->addLayout(wells);
    connect(d->tint, &QAbstractButton::clicked, this, [this]() {
        const IkSkinManager::Settings s = IkSkinManager::instance()->settings();
        IkHsbPicker *picker = new IkHsbPicker();
        picker->setColor(s.tint.isValid() ? s.tint : QColor(0xA8, 0x55, 0xF7));
        picker->setStrength(s.tintStrength);
        // Re-skinning restyles the whole app, so apply after a short pause.
        QTimer *apply = new QTimer(picker);
        apply->setSingleShot(true);
        apply->setInterval(180);
        connect(apply, &QTimer::timeout, this, [this, picker]() {
            IkSkinManager::Settings st = IkSkinManager::instance()->settings();
            st.tint = picker->color();
            st.tintStrength = picker->strength();
            st.skinId.clear();
            IkSkinManager::instance()->setSettings(st);
            d->tint->setColor(st.tint);
            d->tint->setEmpty(st.tintStrength <= 0);
        });
        picker->onColorChanged = [apply, picker](const QColor &) {
            if (picker->strength() <= 0) picker->setStrength(30);
            apply->start();
        };
        picker->onStrengthChanged = [apply](int) { apply->start(); };
        IkPopover *pop = IkMenus::popup(picker, d->tint, IkPopover::Below);
        pop->setAccentOutline(true);
    });
    connect(d->accent, &QAbstractButton::clicked, this, [this]() {
        IkHsbPicker *picker = new IkHsbPicker();
        picker->setColor(d->accent->color());
        QTimer *apply = new QTimer(picker);
        apply->setSingleShot(true);
        apply->setInterval(180);
        connect(apply, &QTimer::timeout, this, [this, picker]() {
            IkSkinManager::Settings st = IkSkinManager::instance()->settings();
            st.overrideAccent = true;
            st.accent = picker->color();
            st.skinId.clear();
            IkSkinManager::instance()->setSettings(st);
            d->accent->setColor(st.accent);
        });
        picker->onColorChanged = [apply](const QColor &) { apply->start(); };
        QWidget *box = new QWidget();
        QVBoxLayout *bl = new QVBoxLayout(box);
        bl->setContentsMargins(0, 0, 0, 0);
        picker->setParent(box);
        bl->addWidget(picker);
        IkTextButton *themeAccent = new IkTextButton(i18n("Use the Theme's Accent"), IkTextButton::Link, box);
        themeAccent->setEnabled(IkSkinManager::instance()->settings().overrideAccent);
        bl->addWidget(themeAccent, 0, Qt::AlignHCenter);
        IkPopover *pop = IkMenus::popup(box, d->accent, IkPopover::Below);
        pop->setAccentOutline(true);
        QPointer<IkPopover> guard = pop;
        connect(themeAccent, &QAbstractButton::clicked, this, [this, guard]() {
            IkSkinManager::Settings st = IkSkinManager::instance()->settings();
            st.overrideAccent = false;
            st.skinId.clear();
            IkSkinManager::instance()->setSettings(st);
            if (guard) guard->dismiss();
            reload();
        });
    });
    l->addWidget(colors);

    // ---- Layout + Interface ------------------------------------------------
    QHBoxLayout *row = new QHBoxLayout();
    row->setSpacing(24);
    IkPanel *layouts = card(page);
    QVBoxLayout *ll = new QVBoxLayout(layouts);
    ll->setContentsMargins(26, 20, 26, 20);
    ll->setSpacing(0);
    ll->addWidget(new IkSectionHeader(i18n("Layout"), layouts));
    ll->addSpacing(14);
    for (IkInterfaceManager::Mode m : {IkInterfaceManager::Desktop, IkInterfaceManager::Kriate, IkInterfaceManager::Minimal}) {
        IkLayoutRow *r = new IkLayoutRow(m, IkInterfaceManager::modeDisplayName(m), layouts);
        r->setToolTip(IkInterfaceManager::modeDescription(m));
        r->onSelect = [this, m]() {
            if (d->manager) d->manager->setMode(m);
            reload();
        };
        connect(r->preview, &QAbstractButton::clicked, this, [this, m, r]() {
            IkPopover *pop = new IkPopover(window());
            QWidget *content = new QWidget(pop);
            QVBoxLayout *pl = new QVBoxLayout(content);
            QLabel *t = new QLabel(IkInterfaceManager::modeDisplayName(m), content);
            t->setFont(IkStyle::font(1.3, QFont::Bold));
            pl->addWidget(t);
            QLabel *desc = new QLabel(IkInterfaceManager::modeDescription(m), content);
            desc->setWordWrap(true);
            desc->setForegroundRole(QPalette::PlaceholderText);
            pl->addWidget(desc);
            pl->addWidget(new IkLayoutPreview(m, content), 1);
            pop->setContent(content);
            pop->setDimBackground(true);
            pop->setCloseButton(true);
            connect(pop, &IkPopover::dismissed, pop, &QObject::deleteLater);
            pop->popup(nullptr, IkPopover::Center, QSize(560, 500));
            Q_UNUSED(r);
        });
        d->layoutRows << r;
        ll->addWidget(r);
    }
    ll->addStretch(1);
    row->addWidget(layouts, 1);

    IkPanel *iface = card(page);
    QVBoxLayout *il = new QVBoxLayout(iface);
    il->setContentsMargins(26, 20, 26, 26);
    il->setSpacing(22);
    il->addWidget(new IkSectionHeader(i18n("Interface"), iface));
    d->scale = new IkChoiceRow(i18n("Scale"), iface);
    d->scale->setBordered(true);
    d->scale->setMinimumHeight(IkStyle::touchSize() + 30);
    // No list: tapping opens a slider (75-200 %, in steps of 5).
    connect(d->scale, &IkChoiceRow::chosen, this, [this](int) {
        const int current = KConfigGroup(KSharedConfig::openConfig(), "IKriate").readEntry("InterfaceScale", 100);
        QWidget *box = new QWidget();
        QVBoxLayout *bl = new QVBoxLayout(box);
        bl->setContentsMargins(IkStyle::scaled(16), IkStyle::scaled(8), IkStyle::scaled(16), IkStyle::scaled(8));
        bl->setSpacing(IkStyle::scaled(10));
        QLabel *value = new QLabel(QStringLiteral("%1%").arg(current), box);
        value->setAlignment(Qt::AlignCenter);
        value->setFont(IkStyle::font(1.3));
        value->setForegroundRole(QPalette::Highlight);
        bl->addWidget(value);
        IkSlider *slider = new IkSlider(box);
        const int lo = 75, hi = 200;
        slider->setValue(qreal(current - lo) / (hi - lo));
        bl->addWidget(slider);
        auto pctOf = [lo, hi](qreal t) { return lo + qRound(t * (hi - lo) / 5.0) * 5; };
        connect(slider, &IkSlider::valueEdited, box, [value, pctOf](qreal t) { value->setText(QStringLiteral("%1%").arg(pctOf(t))); });
        IkPopover *pop = IkMenus::popup(box, d->scale, IkPopover::Below, QSize(d->scale->width(), box->sizeHint().height() + 50));
        pop->setAccentOutline(true);
        QPointer<IkPopover> guard = pop;
#ifdef Q_OS_ANDROID
        // Live while the slider moves (at most every 120 ms; each change
        // re-renders the whole window).
        QTimer *throttle = new QTimer(box);
        throttle->setSingleShot(true);
        throttle->setInterval(120);
        connect(throttle, &QTimer::timeout, this, [this, slider, pctOf]() { applyScale(pctOf(slider->value())); });
        connect(slider, &IkSlider::valueEdited, box, [throttle](qreal) {
            if (!throttle->isActive()) throttle->start();
        });
        connect(slider, &IkSlider::interactionFinished, this, [this, slider, pctOf, throttle]() {
            throttle->stop();
            applyScale(pctOf(slider->value()));
        });
        Q_UNUSED(guard);
#else
        // Rebuilding the interface is heavy: apply when the finger lifts.
        connect(slider, &IkSlider::interactionFinished, this, [this, slider, pctOf, guard]() {
            const int pct = pctOf(slider->value());
            if (guard) guard->dismiss();
            applyScale(pct);
        });
#endif
    });
    il->addWidget(d->scale);
    d->font = new IkChoiceRow(i18n("Font"), iface);
    d->font->setBordered(true);
    d->font->setMinimumHeight(IkStyle::touchSize() + 30);
    connect(d->font, &IkChoiceRow::chosen, this, [this](int) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        const QStringList families = QFontDatabase::families();
#else
        const QStringList families = QFontDatabase().families();
#endif
        QStringList common;
        for (const QString &f : {QStringLiteral("Roboto"), QStringLiteral("Noto Sans"), QStringLiteral("Inter"),
                                 QStringLiteral("Segoe UI"), QStringLiteral("Arial"), QStringLiteral("Open Sans"),
                                 QStringLiteral("Ubuntu"), QStringLiteral("Cantarell"), QStringLiteral("DejaVu Sans")}) {
            if (families.contains(f)) common << f;
        }
        QMenu menu(this);
        QAction *system = menu.addAction(i18n("System Default"));
        menu.addSeparator();
        for (const QString &f : common) {
            QAction *a = menu.addAction(f);
            a->setFont(QFont(f));
            a->setData(f);
        }
        QAction *picked = menu.exec(d->font->mapToGlobal(QPoint(0, d->font->height())));
        if (!picked) return;
        KisConfig cfg(false);
        if (picked == system) {
            cfg.writeEntry<bool>("use_custom_system_font", false);
        } else {
            cfg.writeEntry<bool>("use_custom_system_font", true);
            cfg.writeEntry<QString>("custom_system_font", picked->data().toString());
        }
        // Applied live: the interface is rebuilt with the new font.
        IkStyle::applyInterfaceFont();
        if (d->manager) d->manager->rebuildInterface();
    });
    il->addWidget(d->font);
    IkChoiceRow *refs = new IkChoiceRow(i18n("Reference Images"), iface);
    refs->setBordered(true);
    refs->setMinimumHeight(IkStyle::touchSize() + 30);
    refs->setChoices({i18n("On the Canvas"), i18n("Floating")}, int(IkReferences::placement()));
    refs->setToolTip(i18n("On the canvas: images move and zoom with the canvas. Floating: images stay where you put them while the canvas moves."));
    connect(refs, &IkChoiceRow::chosen, this, [](int i) { IkReferences::setPlacement(IkReferences::Placement(i)); });
    il->addWidget(refs);
    il->addStretch(1);
    row->addWidget(iface, 1);
    l->addLayout(row);

    // ---- skins ----
    IkPanel *skins = card(page);
    QVBoxLayout *sl = new QVBoxLayout(skins);
    sl->setContentsMargins(40, 20, 40, 26);
    sl->setSpacing(4);
    sl->addWidget(new IkSectionHeader(i18n("Skins"), skins));
    QLabel *hint = new QLabel(i18n("Light and dark skins used by the theme setting. Add your own .ikskin or .colors files."), skins);
    hint->setWordWrap(true);
    hint->setForegroundRole(QPalette::PlaceholderText);
    sl->addWidget(hint);
    QWidget *skinList = new QWidget(skins);
    QVBoxLayout *skinRows = new QVBoxLayout(skinList);
    skinRows->setContentsMargins(0, 8, 0, 8);
    skinRows->setSpacing(2);
    sl->addWidget(skinList);
    auto rebuildSkins = [this, skinList, skinRows]() {
        while (QLayoutItem *it = skinRows->takeAt(0)) {
            delete it->widget();
            delete it;
        }
        const IkSkinManager::Settings s = IkSkinManager::instance()->settings();
        for (const IkSkin &skin : IkSkinManager::instance()->availableSkins()) {
            const bool dark = IkSkinManager::isDarkSkin(skin);
            const bool used = skin.id == (dark ? s.darkSkinId : s.lightSkinId);
            IkNavRow *r = new IkNavRow(used ? QStringLiteral("check") : (dark ? QStringLiteral("dark") : QStringLiteral("light")),
                                       skin.name, skinList);
            r->setSelected(used);
            r->setToolTip(skin.description);
            const QString id = skin.id;
            connect(r, &QAbstractButton::clicked, this, [this, id]() {
                IkSkinManager::Settings st = IkSkinManager::instance()->settings();
                st.skinId = id;
                IkSkinManager::instance()->setSettings(st);
                QTimer::singleShot(0, this, [this]() { d->pages->setCurrentIndex(ThemeSection); });
            });
            skinRows->addWidget(r);
        }
    };
    rebuildSkins();
    connect(IkSkinManager::instance(), &IkSkinManager::sigSkinChanged, skinList, rebuildSkins);
    QHBoxLayout *buttons = new QHBoxLayout();
    QPushButton *import = new QPushButton(i18n("Import Skin..."), skins);
    QPushButton *folder = new QPushButton(i18n("Open Skins Folder"), skins);
    buttons->addWidget(import);
    buttons->addWidget(folder);
    buttons->addStretch(1);
    sl->addLayout(buttons);
    connect(import, &QPushButton::clicked, this, [this, rebuildSkins]() {
        KoFileDialog dlg(this, KoFileDialog::OpenFile, QStringLiteral("IKriateImportSkin"));
        dlg.setCaption(i18n("Import Skin"));
        dlg.setNameFilter(i18n("Skins (*.ikskin *.colors)"));
        const QString file = dlg.filename();
        if (file.isEmpty()) return;
        QString error;
        const QString id = IkSkinManager::instance()->importSkin(file, &error);
        if (id.isEmpty()) {
            QMessageBox::warning(this, i18n("Import Skin"), error);
            return;
        }
        IkSkinManager::Settings st = IkSkinManager::instance()->settings();
        st.skinId = id;
        IkSkinManager::instance()->setSettings(st);
        rebuildSkins();
    });
    connect(folder, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(IkSkinManager::instance()->userSkinsFolder()));
    });
    skins->setMinimumWidth(IkStyle::scaled(600));
    skins->setMaximumWidth(IkStyle::scaled(760));
    l->addWidget(skins, 0, Qt::AlignHCenter);
    l->addStretch(1);
    return page;
}

QWidget *IkSettingsPage::buildLanguagePage()
{
    QWidget *page = new QWidget();
    QVBoxLayout *l = new QVBoxLayout(page);
    l->setContentsMargins(0, 0, 4, 0);
    IkPanel *c = card(page);
    QVBoxLayout *cl = new QVBoxLayout(c);
    cl->setContentsMargins(40, 20, 40, 26);
    cl->setSpacing(6);
    cl->addWidget(new IkSectionHeader(i18n("Language"), c));
    cl->addSpacing(8);

    const QString overrideFile = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/klanguageoverridesrc");
    QSettings override(overrideFile, QSettings::IniFormat);
    const QString current = override.value(QStringLiteral("Language/") + qAppName()).toString();

    QStringList codes = KLocalizedString::availableApplicationTranslations().values();
    codes.sort();
    QList<QPair<QString, QString>> langs;
    langs << qMakePair(QString(), i18n("System Default"));
    langs << qMakePair(QStringLiteral("en_US"), QStringLiteral("English"));
    for (const QString &code : codes) {
        const QLocale loc(code);
        QString name = loc.nativeLanguageName();
        if (name.isEmpty()) name = code;
        name[0] = name[0].toUpper();
        if (code.contains(QLatin1Char('_')) || code.contains(QLatin1Char('@'))) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 2, 0)
            const QString country = loc.nativeTerritoryName();
#else
            const QString country = loc.nativeCountryName();
#endif
            if (!country.isEmpty()) name += QStringLiteral(" (%1)").arg(country);
        }
        langs << qMakePair(code, name);
    }
    QGridLayout *grid = new QGridLayout();
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(4);
    int i = 0;
    QList<IkNavRow *> rows;
    for (const auto &lang : langs) {
        IkNavRow *row = new IkNavRow(lang.first == current ? QStringLiteral("check") : QStringLiteral("language"), lang.second, c);
        row->setSelected(lang.first == current);
        const QString code = lang.first;
        connect(row, &QAbstractButton::clicked, this, [this, code, overrideFile]() {
            QSettings o(overrideFile, QSettings::IniFormat);
            if (code.isEmpty()) o.remove(QStringLiteral("Language/") + qAppName());
            else o.setValue(QStringLiteral("Language/") + qAppName(), code);
            o.sync();
            notifyRestart();
        });
        grid->addWidget(row, i / 2, i % 2);
        rows << row;
        ++i;
    }
    cl->addLayout(grid);
    l->addWidget(c);
    l->addStretch(1);
    return page;
}

QWidget *IkSettingsPage::buildAdvancedPage()
{
    QWidget *page = new QWidget();
    QVBoxLayout *l = new QVBoxLayout(page);
    l->setContentsMargins(0, 0, 4, 0);
    l->setSpacing(24);

    IkPanel *c = card(page);
    QVBoxLayout *cl = new QVBoxLayout(c);
    cl->setContentsMargins(40, 20, 40, 26);
    cl->setSpacing(4);
    cl->addWidget(new IkSectionHeader(i18n("Advanced"), c));
    cl->addSpacing(8);
    struct Item { QString icon; QString text; QString action; };
    const QList<Item> items = {
        {QStringLiteral("settings"), i18n("All Settings..."), QStringLiteral("options_configure")},
        {QStringLiteral("tools"), i18n("Keyboard Shortcuts..."), QStringLiteral("options_configure_keybinding")},
        {QStringLiteral("palette"), i18n("Brushes and Resources..."), QStringLiteral("manage_resources")},
        {QStringLiteral("sliders"), i18n("Resource Libraries (Bundles)..."), QStringLiteral("manage_bundles")},
    };
    for (const Item &it : items) {
        IkNavRow *row = new IkNavRow(it.icon, it.text, c);
        const QString action = it.action;
        connect(row, &QAbstractButton::clicked, this, [this, action]() {
            if (d->manager) d->manager->triggerAction(action);
        });
        cl->addWidget(row);
    }
    IkNavRow *classic = new IkNavRow(QStringLiteral("layers"), i18n("Classic docker layout on computers"), c);
    classic->setToolTip(i18n("Adds the classic layout with movable dockers and text menus to the layout choices."));
    IkSwitch *classicSwitch = new IkSwitch(classic);
    classicSwitch->setChecked(KConfigGroup(KSharedConfig::openConfig(), "IKriate").readEntry("OfferClassicLayout", false));
    classic->setTrailing(classicSwitch);
    connect(classicSwitch, &QAbstractButton::toggled, this, [this](bool on) {
        KConfigGroup(KSharedConfig::openConfig(), "IKriate").writeEntry("OfferClassicLayout", on);
        if (d->manager) d->manager->syncModeActions();
    });
    cl->addWidget(classic);
    l->addWidget(c);

    // ---- touch gestures ----
    IkPanel *g = card(page);
    QVBoxLayout *gl = new QVBoxLayout(g);
    gl->setContentsMargins(40, 20, 40, 26);
    gl->setSpacing(8);
    gl->addWidget(new IkSectionHeader(i18n("Touch Gestures"), g));
    QLabel *gIntro = new QLabel(i18n("One finger pans, two fingers zoom and rotate. Choose what multi-finger taps do on the canvas."), g);
    gIntro->setWordWrap(true);
    gIntro->setForegroundRole(QPalette::PlaceholderText);
    gl->addWidget(gIntro);
    const QStringList functions = IkTouchGestures::functionIds();
    QStringList functionNames;
    for (const QString &f : functions) functionNames << IkTouchGestures::functionName(f);
    for (int i = 0; i < IkTouchGestures::GestureCount; ++i) {
        const auto gesture = IkTouchGestures::Gesture(i);
        IkChoiceRow *row = new IkChoiceRow(IkTouchGestures::gestureName(gesture), g);
        row->setBordered(true);
        row->setMinimumHeight(IkStyle::touchSize() + 6);
        row->setChoices(functionNames, qMax(0, functions.indexOf(IkTouchGestures::assignedFunction(gesture))));
        connect(row, &IkChoiceRow::chosen, this, [gesture, functions](int index) {
            IkTouchGestures::assignFunction(gesture, functions.value(index, QStringLiteral("none")));
        });
        gl->addWidget(row);
    }
    l->addWidget(g);

    l->addStretch(1);
    return page;
}

QWidget *IkSettingsPage::buildCleanUpPage()
{
    QWidget *page = new QWidget();
    QVBoxLayout *l = new QVBoxLayout(page);
    l->setContentsMargins(0, 0, 4, 0);
    IkPanel *c = card(page);
    QVBoxLayout *cl = new QVBoxLayout(c);
    cl->setContentsMargins(40, 20, 40, 26);
    cl->setSpacing(8);
    cl->addWidget(new IkSectionHeader(i18n("Clean Up"), c));
    QLabel *hint = new QLabel(i18n("Free space taken by caches and temporary files. Artworks are never touched."), c);
    hint->setWordWrap(true);
    hint->setForegroundRole(QPalette::PlaceholderText);
    cl->addWidget(hint);
    d->cleanRows = new QVBoxLayout();
    cl->addLayout(d->cleanRows);
    l->addWidget(c);
    l->addStretch(1);
    d->cleanPage = page;
    return page;
}

void IkSettingsPage::refreshCleanUp()
{
    while (QLayoutItem *it = d->cleanRows->takeAt(0)) {
        delete it->widget();
        delete it;
    }
    struct Target { QString title; QString path; QStringList filters; };
    const QList<Target> targets = {
        {i18n("Brush previews"), strokePreviewCache(), {}},
        {i18n("Swap files of closed documents"), KisImageConfig(true).swapDir(), {QStringLiteral("*.swap"), QStringLiteral("KRITA_SWAP_FILE_*")}},
        {i18n("Temporary files"), QDir::tempPath(), {QStringLiteral("krita-*"), QStringLiteral("ikriate-*"), QStringLiteral("*.kra.tmp")}},
    };
    for (const Target &t : targets) {
        QWidget *row = new QWidget(d->cleanPage);
        QHBoxLayout *rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 4, 0, 4);
        QLabel *label = new QLabel(QStringLiteral("%1 — %2").arg(t.title, humanSize(folderSize(t.path, t.filters))), row);
        label->setFont(IkStyle::font(1.1));
        rl->addWidget(label, 1);
        QPushButton *clear = new QPushButton(i18n("Clear"), row);
        const Target target = t;
        connect(clear, &QPushButton::clicked, this, [this, target]() {
            clearFolder(target.path, target.filters);
            QTimer::singleShot(0, this, &IkSettingsPage::refreshCleanUp);
        });
        rl->addWidget(clear);
        d->cleanRows->addWidget(row);
    }
}

QWidget *IkSettingsPage::buildRecycleBinPage()
{
    QWidget *page = new QWidget();
    QVBoxLayout *l = new QVBoxLayout(page);
    l->setContentsMargins(0, 0, 4, 0);
    IkPanel *c = card(page);
    QVBoxLayout *cl = new QVBoxLayout(c);
    cl->setContentsMargins(40, 20, 40, 26);
    cl->setSpacing(8);
    cl->addWidget(new IkSectionHeader(i18n("Recycle Bin"), c));
    d->binRows = new QVBoxLayout();
    cl->addLayout(d->binRows);
    QHBoxLayout *b = new QHBoxLayout();
    b->addStretch(1);
    QPushButton *empty = new QPushButton(i18n("Empty Recycle Bin"), c);
    connect(empty, &QPushButton::clicked, this, [this]() {
        if (QMessageBox::question(this, i18n("Empty Recycle Bin"), i18n("Permanently delete all artworks in the recycle bin?"))
            == QMessageBox::Yes) {
            IkRecycleBin::empty();
            refreshRecycleBin();
        }
    });
    b->addWidget(empty);
    cl->addLayout(b);
    l->addWidget(c);
    l->addStretch(1);
    d->binPage = page;
    return page;
}

void IkSettingsPage::refreshRecycleBin()
{
    while (QLayoutItem *it = d->binRows->takeAt(0)) {
        delete it->widget();
        delete it;
    }
    const QList<IkRecycleBin::Entry> entries = IkRecycleBin::entries();
    if (entries.isEmpty()) {
        QLabel *none = new QLabel(i18n("The recycle bin is empty."), d->binPage);
        none->setForegroundRole(QPalette::PlaceholderText);
        d->binRows->addWidget(none);
        return;
    }
    for (const IkRecycleBin::Entry &e : entries) {
        QWidget *row = new QWidget(d->binPage);
        QHBoxLayout *rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 4, 0, 4);
        QLabel *label = new QLabel(QStringLiteral("<b>%1</b><br><small>%2 · %3</small>")
                                       .arg(QFileInfo(e.originalPath.isEmpty() ? e.binPath : e.originalPath).fileName().toHtmlEscaped(),
                                            QLocale().toString(e.deleted, QLocale::ShortFormat), humanSize(e.size)),
                                   row);
        rl->addWidget(label, 1);
        QPushButton *restore = new QPushButton(i18n("Restore"), row);
        QPushButton *remove = new QPushButton(i18n("Delete"), row);
        connect(restore, &QPushButton::clicked, this, [this, e]() {
            QString error;
            if (!IkRecycleBin::restore(e, &error)) QMessageBox::warning(this, i18n("Restore"), error);
            QTimer::singleShot(0, this, &IkSettingsPage::refreshRecycleBin);
        });
        connect(remove, &QPushButton::clicked, this, [this, e]() {
            IkRecycleBin::remove(e);
            QTimer::singleShot(0, this, &IkSettingsPage::refreshRecycleBin);
        });
        rl->addWidget(restore);
        rl->addWidget(remove);
        d->binRows->addWidget(row);
    }
}
