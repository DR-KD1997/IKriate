/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "IkInterfaceManager.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCursor>
#include <QDockWidget>
#include <QEvent>
#include <QMenu>
#include <QMenuBar>
#include <QScreen>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>

#include <KConfigGroup>
#include <KSharedConfig>
#include <kactioncollection.h>
#include <klocalizedstring.h>
#include <kxmlguifactory.h>

#include <KisResourceModel.h>
#include <KisResourceTypes.h>
#include <KoResource.h>
#include <kis_icon_utils.h>

#include "KisMainWindow.h"
#include "KisViewManager.h"
#include "canvas/kis_canvas2.h"
#include "input/kis_input_manager.h"
#include "input/kis_input_profile.h"
#include "input/kis_input_profile_manager.h"
#include "kis_config.h"
#include "kis_paintop_box.h"
#include "kis_popup_palette.h"

#include "IkAppearanceDialog.h"
#include "IkBrushStudio.h"
#include "IkChromeOverlays.h"
#include "IkSkinManager.h"

namespace
{
const char *const kGroup = "IKriate";
const char *const kModeKey = "InterfaceMode";
const char *const kClassicStateKey = "ClassicWindowState";
const char *const kChromeHiddenKey = "DesktopChromeHidden";
const char *const kPrevProfileKey = "PreviousInputProfile";
const char *const kTabletProfile = "IKriate Tablet";
const char *const kAutoId = "auto";

KConfigGroup ikConfig()
{
    return KConfigGroup(KSharedConfig::openConfig(), kGroup);
}
}

struct IkInterfaceManager::Private
{
    KisMainWindow *mw {nullptr};
    Mode mode {Classic};
    bool followDefault {true};

    QActionGroup *modeGroup {nullptr};
    QMap<int, QAction *> modeActions;
    QAction *followDefaultAction {nullptr};

    QPointer<QStackedWidget> stack;
    QScopedPointer<IkMinimalChrome> minimal;
    QScopedPointer<IkRadialChrome> radial;
    QPointer<IkBrushStudio> studio;
    QPointer<IkAppearanceDialog> appearance;

    QByteArray classicState;
    bool chromeHidden {false};
    bool restorePending {false};
    bool initialized {false};
    bool canvasOnly {false};

    bool onCanvasPage() const
    {
        return stack && stack->currentIndex() != 0;
    }

    IkOverlayChrome *activeOverlay() const
    {
        if (mode == Minimal) return minimal.data();
        if (mode == Radial) return radial.data();
        return nullptr;
    }
};

IkInterfaceManager::IkInterfaceManager(KisMainWindow *mainWindow)
    : QObject(mainWindow)
    , d(new Private)
{
    setObjectName(QStringLiteral("IkInterfaceManager"));
    d->mw = mainWindow;

    const KConfigGroup cfg = ikConfig();
    const QString stored = cfg.readEntry(kModeKey, QString::fromLatin1(kAutoId));
    d->followDefault = stored.isEmpty() || stored == QLatin1String(kAutoId);
    d->mode = d->followDefault ? platformDefaultMode() : modeFromId(stored, platformDefaultMode());
    d->classicState = QByteArray::fromBase64(cfg.readEntry(kClassicStateKey, QByteArray()));
    // If the floating layouts hid the dockers last session, Krita's own saved
    // window state has them hidden too; the docked layout lives in classicState.
    d->chromeHidden = cfg.readEntry(kChromeHiddenKey, false) && !d->classicState.isEmpty();

    createActions();
    QTimer::singleShot(0, this, &IkInterfaceManager::slotDeferredInit);
}

IkInterfaceManager::~IkInterfaceManager()
{
}

// ------------------------------------------------------------------ statics

IkInterfaceManager::Mode IkInterfaceManager::platformDefaultMode()
{
#ifdef Q_OS_ANDROID
    return Radial;
#else
    return Classic;
#endif
}

QString IkInterfaceManager::modeId(Mode mode)
{
    switch (mode) {
    case Minimal: return QStringLiteral("minimal");
    case Radial: return QStringLiteral("radial");
    case Classic:
    default: return QStringLiteral("classic");
    }
}

IkInterfaceManager::Mode IkInterfaceManager::modeFromId(const QString &id, Mode fallback)
{
    if (id == QLatin1String("classic")) return Classic;
    if (id == QLatin1String("minimal")) return Minimal;
    if (id == QLatin1String("radial")) return Radial;
    return fallback;
}

QString IkInterfaceManager::modeDisplayName(Mode mode)
{
    switch (mode) {
    case Minimal: return i18nc("IKriate interface layout", "Minimal");
    case Radial: return i18nc("IKriate interface layout", "Radial");
    case Classic:
    default: return i18nc("IKriate interface layout", "Classic");
    }
}

QString IkInterfaceManager::modeDescription(Mode mode)
{
    switch (mode) {
    case Minimal:
        return i18n("Full canvas with a thin top bar and a side bar for size, opacity, undo and redo.");
    case Radial:
        return i18n("Full canvas built for touch: hold a finger for the radial palette, "
                    "quick brushes and colours in a bottom dock. Default on Android tablets.");
    case Classic:
    default:
        return i18n("Krita's docked interface: tool box, dockers and tool options. Default on computers.");
    }
}

IkInterfaceManager *IkInterfaceManager::forWindow(QWidget *window)
{
    return window ? window->findChild<IkInterfaceManager *>(QStringLiteral("IkInterfaceManager"),
                                                             Qt::FindDirectChildrenOnly)
                  : nullptr;
}

// ---------------------------------------------------------------- accessors

IkInterfaceManager::Mode IkInterfaceManager::mode() const
{
    return d->mode;
}

bool IkInterfaceManager::followsPlatformDefault() const
{
    return d->followDefault;
}

KisMainWindow *IkInterfaceManager::mainWindow() const
{
    return d->mw;
}

KisViewManager *IkInterfaceManager::viewManager() const
{
    return d->mw ? d->mw->viewManager() : nullptr;
}

QAction *IkInterfaceManager::action(const QString &name) const
{
    return d->mw ? d->mw->actionCollection()->action(name) : nullptr;
}

// ------------------------------------------------------------------ actions

void IkInterfaceManager::createActions()
{
    KisKActionCollection *ac = d->mw->actionCollection();

    d->modeGroup = new QActionGroup(this);
    d->modeGroup->setExclusive(true);
    for (Mode m : {Classic, Minimal, Radial}) {
        QAction *a = new QAction(modeDisplayName(m), this);
        a->setCheckable(true);
        a->setToolTip(modeDescription(m));
        a->setStatusTip(modeDescription(m));
        a->setData(int(m));
        d->modeGroup->addAction(a);
        ac->addAction(QStringLiteral("ikriate_layout_") + modeId(m), a);
        connect(a, &QAction::triggered, this, [this, m]() { setMode(m); });
        d->modeActions.insert(int(m), a);
    }

    d->followDefaultAction = new QAction(i18n("Use the Default for This Device"), this);
    d->followDefaultAction->setCheckable(true);
    ac->addAction(QStringLiteral("ikriate_layout_auto"), d->followDefaultAction);
    connect(d->followDefaultAction, &QAction::triggered, this, &IkInterfaceManager::setFollowPlatformDefault);

    QAction *studio = new QAction(KisIconUtils::loadIcon("paintop_settings_01"), i18n("Brush Studio..."), this);
    studio->setToolTip(i18n("Browse, search and tune brushes"));
    ac->addAction(QStringLiteral("ikriate_brush_studio"), studio);
    ac->setDefaultShortcut(studio, QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_B));
    connect(studio, &QAction::triggered, this, &IkInterfaceManager::showBrushStudio);

    QAction *appearance = new QAction(KisIconUtils::loadIcon("preferences-desktop-color"),
                                      i18n("Interface && Skins..."), this);
    appearance->setToolTip(i18n("Choose the interface layout, skin, accent colour and tint"));
    ac->addAction(QStringLiteral("ikriate_appearance"), appearance);
    connect(appearance, &QAction::triggered, this, &IkInterfaceManager::showAppearanceDialog);

    syncModeActions();
}

void IkInterfaceManager::syncModeActions()
{
    if (QAction *a = d->modeActions.value(int(d->mode))) {
        a->setChecked(true);
    }
    if (d->followDefaultAction) {
        d->followDefaultAction->setChecked(d->followDefault);
    }
}

// ------------------------------------------------------------- mode handling

void IkInterfaceManager::slotDeferredInit()
{
    if (d->initialized) return;
    d->initialized = true;

    d->stack = qobject_cast<QStackedWidget *>(d->mw->centralWidget());
    if (d->stack) {
        connect(d->stack.data(), &QStackedWidget::currentChanged, this,
                &IkInterfaceManager::slotStackPageChanged, Qt::QueuedConnection);
        d->stack->installEventFilter(this);
    }
    d->mw->installEventFilter(this);

    if (QAction *canvasOnly = action("view_show_canvas_only")) {
        connect(canvasOnly, &QAction::toggled, this, &IkInterfaceManager::slotCanvasOnlyToggled);
        d->canvasOnly = canvasOnly->isChecked();
    }
    if (KisViewManager *vm = viewManager()) {
        connect(vm, &KisViewManager::viewChanged, this, &IkInterfaceManager::slotLayoutOverlays, Qt::QueuedConnection);
    }

    IkSkinManager::instance()->applyStoredSettings();
    applyMode(true);
}

void IkInterfaceManager::setFollowPlatformDefault(bool follow)
{
    d->followDefault = follow;
    if (follow) {
        ikConfig().writeEntry(kModeKey, QString::fromLatin1(kAutoId));
        setMode(platformDefaultMode());
        d->followDefault = true;
        ikConfig().writeEntry(kModeKey, QString::fromLatin1(kAutoId));
    } else {
        ikConfig().writeEntry(kModeKey, modeId(d->mode));
    }
    syncModeActions();
}

void IkInterfaceManager::setMode(Mode mode)
{
    const bool changed = mode != d->mode;
    d->mode = mode;
    // Picking a layout explicitly stops following the platform default,
    // unless it *is* the default.
    d->followDefault = d->followDefault && mode == platformDefaultMode();
    ikConfig().writeEntry(kModeKey, d->followDefault ? QString::fromLatin1(kAutoId) : modeId(mode));
    if (d->initialized) {
        applyMode(false);
    }
    syncModeActions();
    if (changed) {
        Q_EMIT modeChanged(mode);
    }
}

void IkInterfaceManager::applyMode(bool startup)
{
    Q_UNUSED(startup);
    if (d->mode == Classic) {
        if (d->minimal) d->minimal->setVisible(false);
        if (d->radial) d->radial->setVisible(false);
        if (d->chromeHidden) {
            // The welcome page hides dockers by itself and Krita re-applies its
            // saved layout when a document opens, so restore on the canvas page.
            if (d->onCanvasPage()) {
                restoreDesktopChrome();
            } else {
                d->restorePending = true;
            }
        }
    } else {
        d->restorePending = false;
        if (d->mode == Minimal) {
            if (d->radial) d->radial->setVisible(false);
            if (!d->minimal) d->minimal.reset(new IkMinimalChrome(this));
        } else {
            if (d->minimal) d->minimal->setVisible(false);
            if (!d->radial) d->radial.reset(new IkRadialChrome(this));
        }
        if (d->onCanvasPage()) {
            hideDesktopChrome();
        }
    }
    applyInputProfile();
    updateOverlayVisibility();
}

void IkInterfaceManager::hideDesktopChrome()
{
    if (!d->chromeHidden) {
        // Remember the docked layout so Classic can bring it back exactly.
        d->classicState = d->mw->saveState();
        KConfigGroup cfg = ikConfig();
        cfg.writeEntry(kClassicStateKey, d->classicState.toBase64());
        cfg.writeEntry(kChromeHiddenKey, true);
        cfg.sync();
    }
    d->chromeHidden = true;

    const QList<QDockWidget *> docks = d->mw->dockWidgets();
    for (QDockWidget *dock : docks) {
        // A floating layers panel opened from the overlay stays as it is.
        if (dock->isVisible() && !dock->property("ikOverlayPanel").toBool()) {
            dock->hide();
        }
    }
    const QList<QToolBar *> bars = d->mw->findChildren<QToolBar *>();
    for (QToolBar *bar : bars) {
        if (bar->parentWidget() == d->mw) bar->hide();
    }
    if (d->mw->menuBar()) d->mw->menuBar()->hide();
    if (d->mw->statusBar()) d->mw->statusBar()->hide();
}

void IkInterfaceManager::restoreDesktopChrome()
{
    if (!d->classicState.isEmpty()) {
        d->mw->restoreState(d->classicState);
    } else {
        // Nothing saved: at least show the tool box and the default dockers.
        for (const char *id : {"ToolBox", "sharedtooldocker", "KisLayerBox"}) {
            if (QDockWidget *dock = d->mw->dockWidget(QString::fromLatin1(id))) dock->show();
        }
    }
    if (QDockWidget *layers = d->mw->dockWidget(QStringLiteral("KisLayerBox"))) {
        layers->setProperty("ikOverlayPanel", false);
    }
    if (d->mw->menuBar()) d->mw->menuBar()->show();
    if (d->mw->statusBar()) d->mw->statusBar()->setVisible(KisConfig(true).showStatusBar());
    d->chromeHidden = false;
    KConfigGroup cfg = ikConfig();
    cfg.writeEntry(kChromeHiddenKey, false);
    cfg.sync();
}

void IkInterfaceManager::applyInputProfile()
{
    KisInputProfileManager *pm = KisInputProfileManager::instance();
    KisInputProfile *current = pm->currentProfile();
    KConfigGroup cfg = ikConfig();
    if (d->mode == Radial) {
        KisInputProfile *tablet = pm->profile(QString::fromLatin1(kTabletProfile));
        if (tablet && current != tablet) {
            if (current) cfg.writeEntry(kPrevProfileKey, current->name());
            pm->setCurrentProfile(tablet);
        }
    } else if (current && current->name() == QLatin1String(kTabletProfile)) {
        const QString prev = cfg.readEntry(kPrevProfileKey, QStringLiteral("Krita Default"));
        if (KisInputProfile *p = pm->profile(prev)) {
            pm->setCurrentProfile(p);
        }
    }
}

void IkInterfaceManager::updateOverlayVisibility()
{
    const bool show = d->onCanvasPage() && !d->canvasOnly && d->mw->activeView();
    if (d->minimal) d->minimal->setVisible(show && d->mode == Minimal);
    if (d->radial) d->radial->setVisible(show && d->mode == Radial);
    slotLayoutOverlays();
}

void IkInterfaceManager::slotStackPageChanged()
{
    // Krita re-applies its saved docker layout when a document opens; in the
    // floating layouts, put the canvas back in front.
    if (d->mode != Classic && d->onCanvasPage()) {
        hideDesktopChrome();
    } else if (d->mode == Classic && d->restorePending && d->onCanvasPage()) {
        d->restorePending = false;
        restoreDesktopChrome();
    }
    updateOverlayVisibility();
}

void IkInterfaceManager::slotCanvasOnlyToggled(bool on)
{
    d->canvasOnly = on;
    updateOverlayVisibility();
}

void IkInterfaceManager::slotLayoutOverlays()
{
    IkOverlayChrome *overlay = d->activeOverlay();
    if (!overlay || !overlay->isVisible()) return;

    QRect area;
    KisViewManager *vm = viewManager();
    QWidget *canvas = vm ? vm->canvas() : nullptr;
    if (canvas && canvas->isVisible()) {
        area = QRect(canvas->mapTo(d->mw, QPoint(0, 0)), canvas->size());
    } else if (d->mw->centralWidget()) {
        area = d->mw->centralWidget()->geometry();
    } else {
        area = d->mw->rect();
    }
    overlay->layoutPanels(area);
}

bool IkInterfaceManager::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::Resize:
    case QEvent::LayoutRequest:
    case QEvent::WindowStateChange:
        if (d->mode != Classic) {
            QTimer::singleShot(0, this, &IkInterfaceManager::slotLayoutOverlays);
        }
        break;
    case QEvent::Show:
        if (watched == d->mw && d->initialized) {
            // KMainWindow re-applies saved dockers when shown; re-assert the layout.
            QTimer::singleShot(0, this, &IkInterfaceManager::slotStackPageChanged);
        }
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

// ----------------------------------------------------------------- commands

void IkInterfaceManager::triggerAction(const QString &name)
{
    if (QAction *a = action(name)) {
        a->trigger();
    }
}

void IkInterfaceManager::showMainMenu(QWidget *anchor)
{
    QMenu menu(d->mw);
    if (d->mw->menuBar()) {
        const QList<QAction *> top = d->mw->menuBar()->actions();
        for (QAction *a : top) {
            if (a->isVisible()) menu.addAction(a);
        }
    }
    menu.addSeparator();
    QMenu *layouts = menu.addMenu(i18n("Interface Layout"));
    for (Mode m : {Classic, Minimal, Radial}) layouts->addAction(d->modeActions.value(int(m)));
    layouts->addSeparator();
    layouts->addAction(d->followDefaultAction);
    menu.addAction(action("ikriate_brush_studio"));
    menu.addAction(action("ikriate_appearance"));

    const QPoint pos = anchor ? anchor->mapToGlobal(QPoint(0, anchor->height() + 4)) : QCursor::pos();
    menu.exec(pos);
}

void IkInterfaceManager::showNamedMenu(const QString &menuName, QWidget *anchor)
{
    QMenu *menu = nullptr;
    if (d->mw->guiFactory()) {
        menu = qobject_cast<QMenu *>(d->mw->guiFactory()->container(menuName, d->mw));
    }
    if (!menu) {
        showMainMenu(anchor);
        return;
    }
    const QPoint pos = anchor ? anchor->mapToGlobal(QPoint(0, anchor->height() + 4)) : QCursor::pos();
    menu->exec(pos);
}

void IkInterfaceManager::showRadialPalette(const QPoint &globalPos)
{
    KisViewManager *vm = viewManager();
    KisCanvas2 *canvas = vm ? vm->canvasBase() : nullptr;
    if (!canvas || !canvas->popupPalette() || !canvas->canvasWidget()) return;

    KisPopupPalette *palette = canvas->popupPalette();
    if (palette->onScreen()) {
        palette->dismiss();
        return;
    }
    QWidget *cw = canvas->canvasWidget();
    const QPoint pos = globalPos.isNull() ? cw->rect().center() : cw->mapFromGlobal(globalPos);
    if (vm->inputManager()) {
        vm->inputManager()->registerPopupWidget(palette);
    }
    palette->popup(pos);
}

void IkInterfaceManager::toggleLayersPanel(QWidget *anchor)
{
    QDockWidget *dock = d->mw->dockWidget(QStringLiteral("KisLayerBox"));
    if (!dock) return;

    if (d->mode == Classic) {
        dock->setVisible(!dock->isVisible());
        return;
    }
    if (dock->isVisible()) {
        dock->hide();
        return;
    }
    dock->setProperty("ikOverlayPanel", true);
    dock->setFloating(true);
    const QRect avail = d->mw->geometry();
    const QSize size(qMin(360, avail.width() / 2), qMin(560, avail.height() * 2 / 3));
    dock->resize(size);
    if (anchor) {
        const QPoint g = anchor->mapToGlobal(QPoint(anchor->width(), anchor->height() + 8));
        dock->move(qMax(avail.left(), g.x() - size.width()), g.y());
    }
    dock->show();
    dock->raise();
}

bool IkInterfaceManager::activatePresetMatching(const QStringList &fragments)
{
    KisViewManager *vm = viewManager();
    if (!vm || !vm->paintOpBox()) return false;
    KisResourceModel model(ResourceType::PaintOpPresets);
    for (const QString &fragment : fragments) {
        for (int row = 0; row < model.rowCount(); ++row) {
            const QModelIndex idx = model.index(row, 0);
            const QString name = model.data(idx, Qt::UserRole + KisAbstractResourceModel::Name).toString();
            if (name.contains(fragment, Qt::CaseInsensitive)) {
                KoResourceSP res = model.resourceForIndex(idx);
                if (res) {
                    vm->paintOpBox()->resourceSelected(res);
                    return true;
                }
            }
        }
    }
    return false;
}

void IkInterfaceManager::showBrushStudio()
{
    if (!d->studio) {
        d->studio = new IkBrushStudio(this, d->mw);
    }
    d->studio->showStudio();
}

void IkInterfaceManager::showAppearanceDialog()
{
    if (!d->appearance) {
        d->appearance = new IkAppearanceDialog(this, d->mw);
    }
    d->appearance->reload();
    d->appearance->show();
    d->appearance->raise();
    d->appearance->activateWindow();
}
