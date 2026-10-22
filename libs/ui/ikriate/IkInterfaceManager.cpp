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
#include <QComboBox>
#include <QDockWidget>
#include <QSpinBox>
#include <QEvent>
#include <QLayout>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMenu>
#include <QMenuBar>
#include <QResizeEvent>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabBar>
#include <QTimer>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QToolBar>

#include <KConfigGroup>
#include <KSharedConfig>
#include <kactioncollection.h>
#include <klocalizedstring.h>
#include <kxmlguifactory.h>

#include <KisResourceModel.h>
#include <KisResourceTypes.h>
#include <KoCanvasResourceProvider.h>
#include <KoCanvasResourcesIds.h>
#include <KoResource.h>
#include <KoToolManager.h>
#include <KoCanvasController.h>
#include <KoToolBase.h>
#include <kis_selection.h>
#include <kis_image.h>
#include <brushengine/kis_paintop_preset.h>

#include "KisMainWindow.h"
#include "KisImportExportErrorCode.h"
#include "KisView.h"
#include "KisDocument.h"
#include "kis_selection_manager.h"
#include <KisRecentFilesManager.h>
#include "KisViewManager.h"
#include "canvas/kis_canvas2.h"
#include "input/kis_input_manager.h"
#include "input/kis_input_profile.h"
#include "input/kis_input_profile_manager.h"
#include "kis_canvas_resource_provider.h"
#include "kis_config.h"
#include "kis_config_notifier.h"
#include "kis_paintop_box.h"
#include "kis_popup_palette.h"

#include "IkBrushLibrary.h"
#include "IkHomeScreen.h"
#include "IkLayouts.h"
#include "IkPractice.h"
#include "IkTouchGestures.h"
#include "IkWindowFrames.h"
#include "IkNewCanvasDialog.h"
#include "IkMenus.h"
#include "IkBrushSettings.h"
#include "IkReferences.h"
#include "IkSettingsPage.h"
#include "IkSkinManager.h"
#include "IkStyle.h"
#include "IkWidgets.h"

namespace
{
const char *const kGroup = "IKriate";
const char *const kModeKey = "InterfaceMode";
const char *const kClassicStateKey = "ClassicWindowState";
const char *const kPrevProfileKey = "PreviousInputProfile";
const char *const kTabletProfile = "IKriate Tablet";
const char *const kAutoId = "auto";
const char *const kBrushTool = "KritaShape/KisToolBrush";
const char *const kMultiBrushTool = "KritaShape/KisToolMultiBrush";

KConfigGroup ikConfig()
{
    return KConfigGroup(KSharedConfig::openConfig(), kGroup);
}

bool classicOffered()
{
    return ikConfig().readEntry("OfferClassicLayout", false);
}
}

struct IkInterfaceManager::Private
{
    KisMainWindow *mw {nullptr};
    Mode mode {Desktop};
    bool followDefault {true};

    QActionGroup *modeGroup {nullptr};
    QMap<int, QAction *> modeActions;
    QAction *followDefaultAction {nullptr};

    QPointer<QStackedWidget> stack;
    QPointer<IkHomeScreen> home;
    QScopedPointer<IkDesktopLayout> desktop;
    QScopedPointer<IkKriateLayout> kriate;
    QScopedPointer<IkMinimalLayout> minimal;
    QPointer<IkPopover> settings;
    QPointer<IkPopover> brushes;

    QByteArray classicState;
    bool chromeHidden {false};
    bool initialized {false};
    bool canvasOnly {false};

    PaintMode paintMode {Draw};
    Symmetry symmetry {SymmetryOff};
    int radialSections {6};
    bool radialMirrored {false};
    bool selectionFill {false};
    bool fillingSelection {false};
    QPointer<QObject> selectionFillWatched;
    bool settingMode {false};
    QString drawPreset;
    QPointer<QWidget> toolOptions;
    QPointer<QWidget> toolOptionsHost;
    bool resourcesConnected {false};

    bool onCanvasPage() const
    {
        return stack && stack->currentIndex() != 0;
    }

    IkLayout *layout(Mode m) const
    {
        switch (m) {
        case Desktop: return desktop.data();
        case Kriate: return kriate.data();
        case Minimal: return minimal.data();
        default: return nullptr;
        }
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
    if (d->mode == Classic && !classicOffered()) d->mode = platformDefaultMode();
    d->classicState = QByteArray::fromBase64(cfg.readEntry(kClassicStateKey, QByteArray()));
    d->radialSections = qBound(2, cfg.readEntry("RadialSections", 6), 32);
    d->radialMirrored = cfg.readEntry("RadialMirrored", false);

    // Interface scale and font (desktop; Android scales through the density).
    if (!qFuzzyCompare(IkStyle::uiScale(), 1.0)) {
        IkStyle::applyInterfaceFont();
    }

    createActions();
    new IkTouchGestures(this);
    new IkWindowFrames(this);
    QTimer::singleShot(0, this, &IkInterfaceManager::slotDeferredInit);
}

void IkInterfaceManager::rebuildInterface()
{
    QTimer::singleShot(0, this, [this]() {
        if (!d->stack) return;
        const bool onCanvas = d->onCanvasPage();
        const IkHomeScreen::Page page = d->home ? d->home->currentPage() : IkHomeScreen::GalleryPage;
        const int section = d->home ? d->home->settingsSection() : 0;
        const bool settingsOpen = d->settings && d->settings->isOpen();
        for (QPointer<IkPopover> *p : {&d->settings, &d->brushes}) {
            if (*p) {
                (*p)->dismiss();
                (*p)->deleteLater();
                *p = nullptr;
            }
        }
        releaseToolOptions();
        d->desktop.reset();
        d->kriate.reset();
        d->minimal.reset();
        IkStyle::clearCache();

        if (d->home) {
            IkHomeScreen *old = d->home;
            IkHomeScreen *fresh = new IkHomeScreen(d->mw);
            d->stack->insertWidget(0, fresh);
            d->stack->removeWidget(old);
            old->hide();
            old->deleteLater();
            d->home = fresh;
            fresh->setManager(this);
            fresh->showPage(page);
            fresh->showSettingsSection(section);
            if (!onCanvas) d->stack->setCurrentIndex(0);
        }
        applyMode();
        if (settingsOpen && onCanvas) showSettings();
    });
}

IkInterfaceManager::~IkInterfaceManager()
{
    releaseToolOptions();
}

// ------------------------------------------------------------------ statics

IkInterfaceManager::Mode IkInterfaceManager::platformDefaultMode()
{
#ifdef Q_OS_ANDROID
    return Kriate;
#else
    return IkStyle::touchFirst() ? Kriate : Desktop;
#endif
}

QString IkInterfaceManager::modeId(Mode mode)
{
    switch (mode) {
    case Kriate: return QStringLiteral("kriate");
    case Minimal: return QStringLiteral("minimal");
    case Classic: return QStringLiteral("classic-docker");
    case Desktop:
    default: return QStringLiteral("desktop");
    }
}

IkInterfaceManager::Mode IkInterfaceManager::modeFromId(const QString &id, Mode fallback)
{
    if (id == QLatin1String("desktop")) return Desktop;
    if (id == QLatin1String("kriate")) return Kriate;
    if (id == QLatin1String("minimal")) return Minimal;
    if (id == QLatin1String("classic-docker")) return Classic;
    // Layout names from earlier IKriate builds.
    if (id == QLatin1String("classic")) return Desktop;
    if (id == QLatin1String("radial")) return Kriate;
    return fallback;
}

QString IkInterfaceManager::modeDisplayName(Mode mode)
{
    switch (mode) {
    case Kriate: return i18nc("IKriate interface layout", "Kriate");
    case Minimal: return i18nc("IKriate interface layout", "Minimal");
    case Classic: return i18nc("IKriate interface layout", "Classic");
    case Desktop:
    default: return i18nc("IKriate interface layout", "Desktop");
    }
}

QString IkInterfaceManager::modeDescription(Mode mode)
{
    switch (mode) {
    case Kriate:
        return i18n("Built for tablets: tools along the top, your brushes docked on the right and size and opacity "
                    "at your thumb. Default on Android tablets.");
    case Minimal:
        return i18n("Almost nothing but the canvas: a few floating buttons, size and opacity, brush and eraser.");
    case Classic:
        return i18n("Movable dockers, tool box and text menus.");
    case Desktop:
    default:
        return i18n("Tools on the left, colour and layers on the right, brush size and opacity along the bottom. "
                    "Default on computers.");
    }
}

IkInterfaceManager *IkInterfaceManager::forWindow(QWidget *window)
{
    return window ? window->findChild<IkInterfaceManager *>(QStringLiteral("IkInterfaceManager"), Qt::FindDirectChildrenOnly)
                  : nullptr;
}

// ---------------------------------------------------------------- accessors

IkInterfaceManager::Mode IkInterfaceManager::mode() const { return d->mode; }
IkInterfaceManager::Mode IkInterfaceManager::preferredMode() const { return d->mode; }
bool IkInterfaceManager::followsPlatformDefault() const { return d->followDefault; }
KisMainWindow *IkInterfaceManager::mainWindow() const { return d->mw; }
KisViewManager *IkInterfaceManager::viewManager() const { return d->mw ? d->mw->viewManager() : nullptr; }
IkHomeScreen *IkInterfaceManager::homeScreen() const { return d->home; }
IkInterfaceManager::PaintMode IkInterfaceManager::paintMode() const { return d->paintMode; }

QAction *IkInterfaceManager::action(const QString &name) const
{
    return d->mw ? d->mw->actionCollection()->action(name) : nullptr;
}

bool IkInterfaceManager::isBrushToolActive() const
{
    const QString id = KoToolManager::instance()->activeToolId();
    return id == QLatin1String(kBrushTool) || id == QLatin1String(kMultiBrushTool);
}

IkInterfaceManager::Symmetry IkInterfaceManager::symmetry() const
{
    // Mirror painting belongs to each view, so read it from the actions.
    if (d->symmetry == SymmetryRadial && KoToolManager::instance()->activeToolId() == QLatin1String(kMultiBrushTool)) {
        return SymmetryRadial;
    }
    QAction *h = action(QStringLiteral("hmirror_action"));
    QAction *v = action(QStringLiteral("vmirror_action"));
    const bool x = h && h->isChecked();
    const bool y = v && v->isChecked();
    return x && y ? SymmetryXY : x ? SymmetryX : y ? SymmetryY : SymmetryOff;
}
int IkInterfaceManager::radialSections() const { return d->radialSections; }
bool IkInterfaceManager::radialMirrored() const { return d->radialMirrored; }

void IkInterfaceManager::setSymmetry(Symmetry symmetry, int radialSections, int radialMirrored)
{
    if (radialSections > 0) d->radialSections = qBound(2, radialSections, 32);
    if (radialMirrored >= 0) d->radialMirrored = radialMirrored;
    const bool wasRadial = d->symmetry == SymmetryRadial;
    d->symmetry = symmetry;

    // X / Y: Krita's canvas mirror painting.
    auto setChecked = [this](const char *name, bool on) {
        if (QAction *a = action(QString::fromLatin1(name))) {
            if (a->isCheckable() && a->isChecked() != on) a->trigger();
        }
    };
    setChecked("hmirror_action", symmetry == SymmetryX || symmetry == SymmetryXY);
    setChecked("vmirror_action", symmetry == SymmetryY || symmetry == SymmetryXY);

    // Radial: the multi-brush tool in symmetry (or snowflake) mode.
    KConfigGroup multi(KSharedConfig::openConfig(), QString::fromLatin1(kMultiBrushTool));
    multi.writeEntry("transformMode", d->radialMirrored ? 3 : 0);  // SNOWFLAKE : SYMMETRY
    multi.writeEntry("handsCount", d->radialSections);
    if (symmetry == SymmetryRadial) {
        activateTool(QString::fromLatin1(kMultiBrushTool));
        // The tool reads its settings when its options are first built;
        // update the live controls too if they exist already.
        QTimer::singleShot(0, this, [this]() {
            for (QComboBox *c : d->mw->findChildren<QComboBox *>(QStringLiteral("multihandTypeCombobox"))) {
                c->setCurrentIndex(d->radialMirrored ? 3 : 0);
            }
            for (QSpinBox *b : d->mw->findChildren<QSpinBox *>(QStringLiteral("brushCountSpinBox"))) {
                b->setValue(d->radialSections);
            }
            for (QAbstractSpinBox *b : d->mw->findChildren<QAbstractSpinBox *>(QStringLiteral("brushCountSpinBox"))) {
                b->setProperty("value", d->radialSections);
            }
        });
    } else if (wasRadial && KoToolManager::instance()->activeToolId() == QLatin1String(kMultiBrushTool)) {
        activateTool(QString::fromLatin1(kBrushTool));
    }
    KConfigGroup ik = ikConfig();
    ik.writeEntry("Symmetry", int(symmetry));
    ik.writeEntry("RadialSections", d->radialSections);
    ik.writeEntry("RadialMirrored", d->radialMirrored);
    Q_EMIT symmetryChanged(symmetry);
}

// ------------------------------------------------------------------ actions

void IkInterfaceManager::createActions()
{
    KisKActionCollection *ac = d->mw->actionCollection();

    d->modeGroup = new QActionGroup(this);
    d->modeGroup->setExclusive(true);
    for (Mode m : {Desktop, Kriate, Minimal, Classic}) {
        QAction *a = new QAction(modeDisplayName(m), this);
        a->setCheckable(true);
        a->setToolTip(modeDescription(m));
        a->setStatusTip(modeDescription(m));
        a->setData(int(m));
        d->modeGroup->addAction(a);
        ac->addAction(QStringLiteral("ikriate_layout_") + modeId(m).replace(QLatin1Char('-'), QLatin1Char('_')), a);
        connect(a, &QAction::triggered, this, [this, m]() { setMode(m); });
        d->modeActions.insert(int(m), a);
    }

    d->followDefaultAction = new QAction(i18n("Use the Default for This Device"), this);
    d->followDefaultAction->setCheckable(true);
    ac->addAction(QStringLiteral("ikriate_layout_auto"), d->followDefaultAction);
    connect(d->followDefaultAction, &QAction::triggered, this, &IkInterfaceManager::setFollowPlatformDefault);

    QAction *library = new QAction(IkStyle::icon(QStringLiteral("brush")), i18n("Brush Library..."), this);
    library->setToolTip(i18n("Browse brushes by category"));
    ac->addAction(QStringLiteral("ikriate_brush_studio"), library);
    ac->setDefaultShortcut(library, QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_B));
    connect(library, &QAction::triggered, this, &IkInterfaceManager::showBrushLibrary);

    QAction *settings = new QAction(IkStyle::icon(QStringLiteral("settings")), i18n("Interface && Skins..."), this);
    settings->setToolTip(i18n("Theme, layout, interface scale, language and more"));
    ac->addAction(QStringLiteral("ikriate_appearance"), settings);
    connect(settings, &QAction::triggered, this, &IkInterfaceManager::showSettings);

    QAction *home = new QAction(IkStyle::icon(QStringLiteral("home")), i18n("Gallery"), this);
    ac->addAction(QStringLiteral("ikriate_home"), home);
    connect(home, &QAction::triggered, this, &IkInterfaceManager::goHome);

    syncModeActions();
}

void IkInterfaceManager::syncModeActions()
{
    if (QAction *a = d->modeActions.value(int(d->mode))) a->setChecked(true);
    if (QAction *classic = d->modeActions.value(int(Classic))) {
        classic->setVisible(classicOffered());
        if (!classicOffered() && d->mode == Classic) setMode(platformDefaultMode());
    }
    if (d->followDefaultAction) d->followDefaultAction->setChecked(d->followDefault);
}

// ------------------------------------------------------------- mode handling

void IkInterfaceManager::slotDeferredInit()
{
    if (d->initialized) return;
    d->initialized = true;

    d->stack = qobject_cast<QStackedWidget *>(d->mw->centralWidget());
    if (d->stack) {
        connect(d->stack.data(), &QStackedWidget::currentChanged, this, &IkInterfaceManager::slotStackPageChanged,
                Qt::QueuedConnection);
        d->stack->installEventFilter(this);
    }
    d->home = d->mw->findChild<IkHomeScreen *>();
    if (d->home) d->home->setManager(this);
    d->mw->installEventFilter(this);

    if (QAction *canvasOnly = action(QStringLiteral("view_show_canvas_only"))) {
        connect(canvasOnly, &QAction::toggled, this, &IkInterfaceManager::slotCanvasOnlyToggled);
        d->canvasOnly = canvasOnly->isChecked();
    }
    if (KisViewManager *vm = viewManager()) {
        connect(vm, &KisViewManager::viewChanged, this, &IkInterfaceManager::slotLayoutPanels, Qt::QueuedConnection);
        connect(vm, &KisViewManager::viewChanged, this, [this]() {
            if (!d->resourcesConnected && viewManager() && viewManager()->canvasResourceProvider()
                && viewManager()->canvasResourceProvider()->resourceManager()) {
                d->resourcesConnected = true;
                connect(viewManager()->canvasResourceProvider()->resourceManager(),
                        &KoCanvasResourceProvider::canvasResourceChanged, this, [this](int key, const QVariant &) {
                            if (key == KoCanvasResource::EraserMode) slotEraserModeChanged();
                            if (key == KoCanvasResource::CurrentPaintOpPreset) slotPresetChanged();
                        });
            }
        });
    }

    IkSkinManager::instance()->applyStoredSettings();
    QMetaObject::invokeMethod(d->mw, "updateTheme", Qt::QueuedConnection);
    QMetaObject::invokeMethod(d->mw, "themeChanged", Qt::QueuedConnection);
    // The area around the canvas follows the skin unless the user picked a
    // colour in the canvas preferences.
    auto syncCanvasSurround = []() {
        KConfigGroup ik = KSharedConfig::openConfig()->group("IKriate");
        KisConfig cfg(false);
        const QColor previous = ik.readEntry("AutoCanvasBorder", QColor());
        const QColor current = cfg.canvasBorderColor();
        if (current != cfg.canvasBorderColor(true) && current != previous) return; // user's own choice
        const QColor surround = IkStyle::color(IkStyle::CanvasSurround);
        if (surround == current) return;
        cfg.setCanvasBorderColor(surround);
        ik.writeEntry("AutoCanvasBorder", surround);
        KisConfigNotifier::instance()->notifyConfigChanged();
    };
    connect(IkSkinManager::instance(), &IkSkinManager::sigSkinChanged, this, [syncCanvasSurround]() {
        IkStyle::clearCache();
        // The palette is applied after the signal; read it on the next turn.
        QTimer::singleShot(0, qApp, syncCanvasSurround);
    });
    QTimer::singleShot(0, qApp, syncCanvasSurround);
    applyMode();
}

void IkInterfaceManager::setFollowPlatformDefault(bool follow)
{
    d->followDefault = follow;
    if (follow) {
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
    if (mode == Classic && !classicOffered()) mode = platformDefaultMode();
    const bool changed = mode != d->mode;
    if (changed && d->onCanvasPage() && d->mode == Classic) {
        // Remember the docked layout so Classic can bring it back exactly.
        d->classicState = d->mw->saveState();
        ikConfig().writeEntry(kClassicStateKey, d->classicState.toBase64());
    }
    d->mode = mode;
    d->followDefault = d->followDefault && mode == platformDefaultMode();
    ikConfig().writeEntry(kModeKey, d->followDefault ? QString::fromLatin1(kAutoId) : modeId(mode));
    ikConfig().sync();
    if (d->initialized) applyMode();
    syncModeActions();
    if (changed) Q_EMIT modeChanged(mode);
}

static IkLayout *ensureLayout(IkInterfaceManager *self, IkInterfaceManager::Mode m,
                       QScopedPointer<IkDesktopLayout> &desktop, QScopedPointer<IkKriateLayout> &kriate,
                       QScopedPointer<IkMinimalLayout> &minimal)
{
    switch (m) {
    case IkInterfaceManager::Desktop:
        if (!desktop) desktop.reset(new IkDesktopLayout(self));
        return desktop.data();
    case IkInterfaceManager::Kriate:
        if (!kriate) kriate.reset(new IkKriateLayout(self));
        return kriate.data();
    case IkInterfaceManager::Minimal:
        if (!minimal) minimal.reset(new IkMinimalLayout(self));
        return minimal.data();
    default:
        return nullptr;
    }
}

void IkInterfaceManager::applyMode()
{
    if (d->onCanvasPage() && d->mode == Classic && !d->canvasOnly) {
        restoreDesktopChrome();
    } else {
        hideDesktopChrome();
    }
    if (d->mode != Classic && d->onCanvasPage()) {
        ensureLayout(this, d->mode, d->desktop, d->kriate, d->minimal);
    }
    applyInputProfile();
    updateLayoutVisibility();
    if (d->mode == Classic) {
        // The docker interface gets Krita's own brush editor back.
        for (QWidget *top : QApplication::topLevelWidgets()) {
            QList<QWidget *> candidates = top->findChildren<QWidget *>();
            candidates.prepend(top);
            for (QWidget *w : candidates) {
                if (w->inherits("KisPaintOpPresetsEditor")) IkBrushSettings::adoptEditor(w, this, false);
            }
        }
        for (QWidget *w : d->mw->findChildren<QWidget *>()) {
            if (w->inherits("KisPaintOpPresetsEditor")) IkBrushSettings::adoptEditor(w, this, false);
        }
    }
}

void IkInterfaceManager::hideDesktopChrome()
{
    if (!d->chromeHidden && d->onCanvasPage() && d->mode == Classic) {
        d->classicState = d->mw->saveState();
        ikConfig().writeEntry(kClassicStateKey, d->classicState.toBase64());
    }
    d->chromeHidden = true;
    const QList<QDockWidget *> docks = d->mw->dockWidgets();
    for (QDockWidget *dock : docks) {
        if (dock->isVisible()) dock->hide();
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
    releaseToolOptions();
    if (!d->classicState.isEmpty()) {
        d->mw->restoreState(d->classicState);
    } else {
        for (const char *id : {"ToolBox", "sharedtooldocker", "KisLayerBox", "PresetDocker", "ColorSelectorNg"}) {
            if (QDockWidget *dock = d->mw->dockWidget(QString::fromLatin1(id))) dock->show();
        }
        const QList<QToolBar *> bars = d->mw->findChildren<QToolBar *>();
        for (QToolBar *bar : bars) {
            const QString n = bar->objectName();
            if (n == QLatin1String("mainToolBar") || n == QLatin1String("BrushesAndStuff")) bar->show();
        }
    }
    if (d->mw->menuBar()) d->mw->menuBar()->show();
    if (d->mw->statusBar()) d->mw->statusBar()->setVisible(KisConfig(true).showStatusBar());
    d->chromeHidden = false;
}

void IkInterfaceManager::applyInputProfile()
{
    KisInputProfileManager *pm = KisInputProfileManager::instance();
    KisInputProfile *current = pm->currentProfile();
    KConfigGroup cfg = ikConfig();
    // All IKriate layouts use the IKriate profile (its taps are handled by
    // IkTouchGestures); the classic docker interface keeps Krita's profiles.
    const bool touchLayout = d->mode != Classic;
    if (touchLayout) {
        KisInputProfile *tablet = pm->profile(QString::fromLatin1(kTabletProfile));
        if (tablet && current != tablet) {
            if (current) cfg.writeEntry(kPrevProfileKey, current->name());
            pm->setCurrentProfile(tablet);
        }
    } else if (current && current->name() == QLatin1String(kTabletProfile)) {
        const QString prev = cfg.readEntry(kPrevProfileKey, QString());
        KisInputProfile *p = prev.isEmpty() ? nullptr : pm->profile(prev);
        if (!p) {
            const QList<KisInputProfile *> all = pm->profiles();
            for (KisInputProfile *candidate : all) {
                if (candidate->name() != QLatin1String(kTabletProfile)) { p = candidate; break; }
            }
        }
        if (p) pm->setCurrentProfile(p);
    }
}

void IkInterfaceManager::setDocumentTabsVisible(bool visible)
{
    if (!d->stack) return;
    QMdiArea *mdi = d->stack->findChild<QMdiArea *>();
    if (!mdi) return;
    // QMdiArea reserves the tab bar's size hint even while the bar is hidden,
    // so hidden tabs are also collapsed to zero height.
    static const QString collapse = QStringLiteral(
        "\nQTabBar::tab { height: 0px; min-height: 0px; max-height: 0px; padding: 0px; margin: 0px; border: none; }");
    const QList<QTabBar *> tabs = mdi->findChildren<QTabBar *>(QString(), Qt::FindDirectChildrenOnly);
    for (QTabBar *t : tabs) {
        QString sheet = t->styleSheet();
        const bool collapsed = sheet.endsWith(collapse);
        if (visible && collapsed) {
            sheet.chop(collapse.size());
            t->setStyleSheet(sheet);
        } else if (!visible && !collapsed) {
            t->setStyleSheet(sheet + collapse);
        }
        t->setVisible(visible);
    }
    // QMdiArea only re-reads the size hint on some events; set the margin now.
    if (mdi->viewMode() == QMdiArea::TabbedView) {
        struct MarginAccess : public QMdiArea {
            using QAbstractScrollArea::setViewportMargins;
        };
        void (QAbstractScrollArea::*setMargins)(int, int, int, int) = &MarginAccess::setViewportMargins;
        const int top = visible && !tabs.isEmpty() ? tabs.first()->sizeHint().height() : 0;
        (mdi->*setMargins)(0, top, 0, 0);
    }
}

void IkInterfaceManager::updateLayoutVisibility()
{
    const bool canvas = d->onCanvasPage() && d->mw->activeView();
    const bool show = canvas && !d->canvasOnly;
    for (Mode m : {Desktop, Kriate, Minimal}) {
        if (IkLayout *l = d->layout(m)) {
            const bool vis = show && d->mode == m;
            if (l->isVisible() != vis) l->setVisible(vis);
        }
    }
    setDocumentTabsVisible(d->mode == Classic || !canvas);
    slotLayoutPanels();
}

void IkInterfaceManager::slotStackPageChanged()
{
    if (!d->onCanvasPage()) closeTransientWindows();
    applyMode();
    IkReferences::setFloatingVisible(this, d->onCanvasPage() && d->mode != Classic);
}

void IkInterfaceManager::closeTransientWindows()
{
    IkPractice::stopActive();
    // IKriate pop-overs (layout pop-overs, brush library, settings, practice, ...)
    const QList<IkPopover *> popovers = d->mw->findChildren<IkPopover *>();
    for (IkPopover *p : popovers) {
        if (p->isOpen()) p->dismiss();
    }
    for (Mode m : {Desktop, Kriate, Minimal}) {
        if (IkLayout *l = d->layout(m)) l->closeTransient();
    }
    // Krita's brush editor and any open menus or pop-up windows.
    if (QAction *editor = action(QStringLiteral("show_brush_editor"))) {
        if (editor->isCheckable() && editor->isChecked()) editor->trigger();
    }
    const QList<QWidget *> tops = QApplication::topLevelWidgets();
    for (QWidget *w : tops) {
        if (w == d->mw || !w->isVisible() || w->isModal()) continue;
        if (w->inherits("KisPaintOpPresetsEditor") || qobject_cast<QMenu *>(w) || (w->windowFlags() & Qt::Popup) == Qt::Popup
            || (w->windowFlags() & Qt::Tool) == Qt::Tool) {
            w->hide();
        }
    }
    const QList<QWidget *> kids = d->mw->findChildren<QWidget *>();
    for (QWidget *w : kids) {
        if (w->isWindow() && w->isVisible() && w->inherits("KisPaintOpPresetsEditor")) w->hide();
    }
}

void IkInterfaceManager::slotCanvasOnlyToggled(bool on)
{
    d->canvasOnly = on;
    updateLayoutVisibility();
}

void IkInterfaceManager::slotLayoutPanels()
{
    if (!d->stack) return;
    IkLayout *l = d->layout(d->mode);
    const bool active = l && l->isVisible();
    const QRect area = d->stack->geometry();
    if (active) l->layoutPanels(area);
    // Panels of the Desktop layout sit beside the canvas, not over it.
    const QMargins margins = (active && d->onCanvasPage()) ? l->reservedMargins() : QMargins();
    if (d->stack->contentsMargins() != margins) d->stack->setContentsMargins(margins);
    if (qEnvironmentVariableIsSet("IKRIATE_DEBUG_WIDGETS")) {
        for (const QPoint &pt : {QPoint(800, 15), QPoint(800, 300)}) {
            QWidget *w = d->mw->childAt(pt);
            QString chain;
            for (QWidget *p = w; p && p != d->mw; p = p->parentWidget()) {
                chain += QStringLiteral(" < %1(%2) [%3,%4 %5x%6]").arg(QString::fromLatin1(p->metaObject()->className()), p->objectName())
                             .arg(p->geometry().x()).arg(p->geometry().y()).arg(p->width()).arg(p->height());
            }
            qDebug("IKriate widget at %d,%d:%s", pt.x(), pt.y(), qPrintable(chain));
        }
        if (QMdiArea *mdi = d->stack->findChild<QMdiArea *>()) {
            for (QTabBar *t : mdi->findChildren<QTabBar *>()) {
                qDebug("IKriate tabbar parent=%s hint=%dx%d hidden=%d count=%d sheet=%s", t->parentWidget()->metaObject()->className(),
                       t->sizeHint().width(), t->sizeHint().height(), t->isHidden(), t->count(), qPrintable(t->styleSheet().right(140)));
            }
            qDebug("IKriate mdi viewMode=%d", int(mdi->viewMode()));
        }
    }
}

bool IkInterfaceManager::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::Resize:
    case QEvent::LayoutRequest:
    case QEvent::WindowStateChange:
        if (d->mode != Classic) QTimer::singleShot(0, this, &IkInterfaceManager::slotLayoutPanels);
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

// -------------------------------------------------------------- paint modes

void IkInterfaceManager::setPaintMode(PaintMode mode)
{
    KisViewManager *vm = viewManager();
    const PaintMode old = d->paintMode;
    d->settingMode = true;
    if (mode == Smudge && old != Smudge) {
        if (vm && vm->canvasResourceProvider() && vm->canvasResourceProvider()->currentPreset()) {
            d->drawPreset = vm->canvasResourceProvider()->currentPreset()->name();
        }
        const QString smudge = ikConfig().readEntry("SmudgePreset", QString());
        ensureBrushTool();
        if (smudge.isEmpty() || !activatePresetMatching({smudge})) {
            activatePresetMatching({QStringLiteral("Blender_Basic"), QStringLiteral("Blender Basic"),
                                    QStringLiteral("Blender"), QStringLiteral("Smear")});
        }
    } else if (old == Smudge && mode != Smudge && !d->drawPreset.isEmpty()) {
        activatePresetMatching({d->drawPreset});
    }
    if (QAction *erase = action(QStringLiteral("erase_action"))) {
        if (erase->isChecked() != (mode == Erase)) erase->setChecked(mode == Erase);
    }
    d->paintMode = mode;
    d->settingMode = false;
    if (mode != Draw || old != Draw) Q_EMIT paintModeChanged(mode);
}

void IkInterfaceManager::slotEraserModeChanged()
{
    if (d->settingMode) return;
    KisViewManager *vm = viewManager();
    if (!vm || !vm->canvasResourceProvider()) return;
    const bool erasing = vm->canvasResourceProvider()->eraserMode();
    if (erasing && d->paintMode != Erase) {
        d->paintMode = Erase;
        Q_EMIT paintModeChanged(Erase);
    } else if (!erasing && d->paintMode == Erase) {
        d->paintMode = Draw;
        Q_EMIT paintModeChanged(Draw);
    }
}

void IkInterfaceManager::slotPresetChanged()
{
    KisViewManager *vm = viewManager();
    if (!vm || !vm->canvasResourceProvider() || !vm->canvasResourceProvider()->currentPreset()) return;
    const QString name = vm->canvasResourceProvider()->currentPreset()->name();
    IkBrushLibrary::noteRecent(name);
    if (d->settingMode) return;
    if (d->paintMode == Smudge) {
        // Remember the smear brush; the blur brushes have their own entry.
        if (!name.contains(QLatin1String("Blur"), Qt::CaseInsensitive)) ikConfig().writeEntry("SmudgePreset", name);
    } else {
        d->drawPreset = name;
    }
}

void IkInterfaceManager::activateTool(const QString &toolId)
{
    // Any explicit tool choice ends Selection Fill (it re-enables itself
    // after this call when it is the one being chosen).
    d->selectionFill = false;
    KoToolManager::instance()->switchToolRequested(toolId);
}

int IkInterfaceManager::transformMode() const
{
    return ikConfig().readEntry("TransformMode", int(TransformBasic));
}

int IkInterfaceManager::smudgeVariant() const
{
    return ikConfig().readEntry("SmudgeVariant", int(SmudgeSmear));
}

void IkInterfaceManager::activateTransform(int mode)
{
    // Krita's transform tool modes (KisToolTransform::TransformToolMode).
    enum { Free = 0, Warp = 1, Cage = 2, Liquify = 3, Perspective = 4 };
    int kritaMode = Free;
    switch (mode) {
    case TransformAnchor: kritaMode = Cage; break;       // pin anchor points, drag the shape around them
    case TransformDistort: kritaMode = Perspective; break;
    case TransformWarp: kritaMode = Warp; break;
    case TransformLiquify: kritaMode = Liquify; break;
    default: break;
    }
    if (mode != TransformLiquify) ikConfig().writeEntry("TransformMode", mode);
    setPaintMode(Draw);
    activateTool(QStringLiteral("KisToolTransform"));
    // The tool object exists once it has been activated on this canvas.
    QTimer::singleShot(0, this, [this, kritaMode]() {
        KisViewManager *vm = viewManager();
        if (!vm || !vm->canvasBase()) return;
        if (KoToolBase *tool = KoToolManager::instance()->toolById(vm->canvasBase(), QStringLiteral("KisToolTransform"))) {
            tool->setProperty("transformMode", kritaMode);
        }
    });
}

void IkInterfaceManager::flipLayer(bool horizontal)
{
    KisViewManager *vm = viewManager();
    const bool hasSelection = vm && vm->selection();
    if (hasSelection) {
        triggerAction(horizontal ? QStringLiteral("mirrorSelectionX") : QStringLiteral("mirrorSelectionY"));
    } else {
        triggerAction(horizontal ? QStringLiteral("mirrorNodeX") : QStringLiteral("mirrorNodeY"));
    }
}

void IkInterfaceManager::setSmudgeVariant(int variant)
{
    ikConfig().writeEntry("SmudgeVariant", variant);
    if (variant == SmudgeLiquify) {
        activateTransform(TransformLiquify);
        return;
    }
    setPaintMode(Smudge);
    ensureBrushTool();
    d->settingMode = true;
    if (variant == SmudgeBlur) {
        activatePresetMatching({QStringLiteral("Blender_Blur"), QStringLiteral("Blender Blur"), QStringLiteral("Filter_Blur")});
    } else {
        const QString smear = ikConfig().readEntry("SmudgePreset", QString());
        if (smear.isEmpty() || smear.contains(QLatin1String("Blur"), Qt::CaseInsensitive) || !activatePresetMatching({smear})) {
            activatePresetMatching({QStringLiteral("Blender_Basic"), QStringLiteral("Blender Basic"), QStringLiteral("Blender_Smear")});
        }
    }
    d->settingMode = false;
    Q_EMIT paintModeChanged(d->paintMode);
}

bool IkInterfaceManager::isSelectionFillActive() const
{
    return d->selectionFill && KoToolManager::instance()->activeToolId() == QLatin1String("KisToolSelectOutline");
}

void IkInterfaceManager::activateSelectionFill()
{
    KisViewManager *vm = viewManager();
    if (!vm || !vm->selectionManager()) return;
    setPaintMode(Draw);
    // A fresh lasso each time: start without a selection.
    if (vm->selection()) triggerAction(QStringLiteral("deselect"));
    activateTool(QStringLiteral("KisToolSelectOutline"));
    d->selectionFill = true;
    if (d->selectionFillWatched != vm->selectionManager()) {
        d->selectionFillWatched = vm->selectionManager();
        connect(vm->selectionManager(), &KisSelectionManager::currentSelectionChanged, this, [this]() {
            if (!d->selectionFill || d->fillingSelection || !isSelectionFillActive()) return;
            d->fillingSelection = true;
            // Selections and fills are applied in the background: let the
            // lasso land, fill it, then let the fill land before deselecting.
            QTimer::singleShot(0, this, [this]() {
                KisViewManager *v = viewManager();
                KisImageSP image; if (v) image = v->image();
                if (image) image->waitForDone();
                // The lasso announces an empty selection when it starts.
                if (v && v->selection() && !v->selection()->selectedExactRect().isEmpty()) {
                    triggerAction(QStringLiteral("fill_selection_foreground_color"));
                    if (image) image->waitForDone();
                    triggerAction(QStringLiteral("deselect"));
                    if (image) image->waitForDone();
                }
                QTimer::singleShot(0, this, [this]() { d->fillingSelection = false; });
            });
        });
        connect(KoToolManager::instance(), &KoToolManager::changedTool, this, [this]() {
            if (KoToolManager::instance()->activeToolId() != QLatin1String("KisToolSelectOutline")) d->selectionFill = false;
        });
    }
    Q_EMIT paintModeChanged(d->paintMode);
}

void IkInterfaceManager::ensureBrushTool()
{
    if (!isBrushToolActive()) {
        activateTool(QString::fromLatin1(d->symmetry == SymmetryRadial ? kMultiBrushTool : kBrushTool));
    }
}

// -------------------------------------------------------------- tool options

void IkInterfaceManager::takeToolOptions(QWidget *host)
{
    if (!host) return;
    QDockWidget *dock = d->mw->dockWidget(QStringLiteral("sharedtooldocker"));
    if (!d->toolOptions && dock) d->toolOptions = dock->widget();
    if (!d->toolOptions) return;
    if (d->toolOptions->parentWidget() != host) {
        d->toolOptions->setParent(host);
        if (host->layout()) host->layout()->addWidget(d->toolOptions);
        d->toolOptions->show();
    }
    d->toolOptionsHost = host;
}

void IkInterfaceManager::releaseToolOptions()
{
    if (!d->toolOptions || !d->mw) return;
    QDockWidget *dock = d->mw->dockWidget(QStringLiteral("sharedtooldocker"));
    if (dock && dock->widget() != d->toolOptions) {
        dock->setWidget(d->toolOptions);
    }
    d->toolOptionsHost = nullptr;
}

// ----------------------------------------------------------------- commands

void IkInterfaceManager::triggerAction(const QString &name)
{
    if (QAction *a = action(name)) {
        a->trigger();
    } else if (name.contains(QLatin1Char('/')) || name.startsWith(QLatin1String("KisTool")) || name.endsWith(QLatin1String("Tool"))) {
        activateTool(name);
    }
}

namespace
{
/// Where auto-save puts artworks that have never been saved.
QString autoSaveFolder()
{
#ifdef Q_OS_ANDROID
    QString base = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
#else
    QString base = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
#endif
    if (base.isEmpty()) base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString dir = base + QStringLiteral("/IKriate");
    QDir().mkpath(dir);
    return dir;
}
}

bool IkInterfaceManager::autoSaveThenGoHome()
{
    if (!ikConfig().readEntry("AutoSave", true)) return false;
    KisView *view = d->mw->activeView();
    KisDocument *doc = view ? view->document() : nullptr;
    if (!doc || !doc->isModified()) return false;

    // Save as .kra: into the same file when it is one, otherwise next to it
    // (or into the IKriate folder), so layers are never flattened.
    QString path = doc->path();
    const bool isKra = path.endsWith(QLatin1String(".kra"), Qt::CaseInsensitive);
    if (!isKra) {
        const QFileInfo info(path);
        QString name = info.completeBaseName();
        if (name.isEmpty()) name = i18nc("file name of an auto-saved artwork", "Artwork");
        const QString dir = (!path.isEmpty() && !path.startsWith(QLatin1String("content:")) && QFileInfo(info.absolutePath()).isWritable())
            ? info.absolutePath()
            : autoSaveFolder();
        path = QStringLiteral("%1/%2.kra").arg(dir, name);
        for (int i = 2; QFileInfo::exists(path); ++i) path = QStringLiteral("%1/%2 %3.kra").arg(dir, name).arg(i);
    }

    QPointer<KisDocument> guard = doc;
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = connect(doc, &KisDocument::sigCompleteBackgroundSaving, this,
                          [this, guard, connection](const KritaUtils::ExportFileJob &job, KisImportExportErrorCode status,
                                                    const QString &, const QString &) {
                              disconnect(*connection);
                              const QString saved = job.filePath;
                              QTimer::singleShot(0, this, [this, guard, saved, ok = status.isOk()]() {
                                  if (ok && guard) guard->setModified(false);
                                  // The gallery lists recent files.
                                  if (ok && !saved.isEmpty()) KisRecentFilesManager::instance()->add(QUrl::fromLocalFile(saved));
                                  // Not saved: fall back to the normal close (it asks).
                                  triggerAction(QStringLiteral("file_close"));
                              });
                          });
    const bool started = isKra ? doc->save(false, nullptr) : doc->saveAs(path, QByteArrayLiteral("application/x-krita"), false);
    if (!started) {
        disconnect(*connection);
        return false;
    }
    return true;
}

void IkInterfaceManager::goHome()
{
    closeTransientWindows();
    if (autoSaveThenGoHome()) return;
    if (d->mw->activeView()) {
        triggerAction(QStringLiteral("file_close"));
    } else if (d->home) {
        d->home->showPage(IkHomeScreen::GalleryPage);
    }
}

bool IkInterfaceManager::isEditingInterface() const
{
    IkLayout *l = d->layout(d->mode);
    return l && l->isEditing();
}

void IkInterfaceManager::setEditingInterface(bool editing)
{
    IkLayout *l = d->layout(d->mode);
    if (!l) return;
    if (editing) closeTransientWindows();
    l->setEditing(editing && d->onCanvasPage());
}

void IkInterfaceManager::requestLayout()
{
    slotLayoutPanels();
}

void IkInterfaceManager::showMainMenu(QWidget *anchor)
{
    // "Options": a grid of the common file and app actions. The full set of
    // Krita menus stays one tap away under Tools.
    IkActionGrid *grid = new IkActionGrid(i18n("Options"));
    QPointer<IkPopover> pop = IkMenus::create(d->mw);
    grid->onTriggered = [pop]() { if (pop) pop->dismiss(); };
    QPointer<QWidget> anchorGuard = anchor;
    auto later = [this](std::function<void()> f) { QTimer::singleShot(0, this, f); };
    grid->addItem(QStringLiteral("plus"), i18nc("new canvas", "New"), [this, later]() {
        later([this]() { IkNewCanvasDialog::showNewCanvas(d->mw); });
    });
    grid->addItem(QStringLiteral("open"), i18n("Open"), [this, later]() { later([this]() { triggerAction(QStringLiteral("file_open")); }); });
    grid->addItem(QStringLiteral("save"), i18n("Save"), [this, later]() { later([this]() { triggerAction(QStringLiteral("file_save")); }); },
                  d->mw->activeView() != nullptr);
    grid->addItem(QStringLiteral("import"), i18n("Import"), [this, later]() {
        later([this]() { triggerAction(QStringLiteral("import_layer_from_file")); });
    }, d->mw->activeView() != nullptr);
    grid->addItem(QStringLiteral("layout"), i18n("Layout"), [this, later, anchorGuard]() {
        later([this, anchorGuard]() { showLayoutMenu(anchorGuard); });
    });
    grid->addItem(QStringLiteral("share"), i18n("Export"), [this, later]() {
        later([this]() { triggerAction(QStringLiteral("file_export_file")); });
    }, d->mw->activeView() != nullptr);
    grid->addItem(QStringLiteral("wrench"), i18n("Tools"), [this, later, anchorGuard]() {
        later([this, anchorGuard]() { showKritaMenus(anchorGuard); });
    });
    grid->addItem(QStringLiteral("help"), i18n("Help"), [this, later, anchorGuard]() {
        later([this, anchorGuard]() { showNamedMenu(QStringLiteral("help"), anchorGuard); });
    });
    grid->addItem(QStringLiteral("settings"), i18n("Settings"), [this, later]() { later([this]() { showSettings(); }); });
    grid->addItem(QStringLiteral("home"), i18n("Gallery"), [this, later]() { later([this]() { goHome(); }); });
    grid->addItem(QStringLiteral("brush"), i18n("Brushes"), [this, later]() { later([this]() { showBrushLibrary(); }); });
    const bool editing = isEditingInterface();
    grid->addItem(QStringLiteral("edit-interface"), editing ? i18n("Done Editing") : i18n("Edit Interface"), [this, later, editing]() {
        later([this, editing]() { setEditingInterface(!editing); });
    }, d->layout(d->mode) && d->onCanvasPage());
    pop->setContent(grid);
    pop->popup(anchor, IkPopover::Below);
}

void IkInterfaceManager::showLayoutMenu(QWidget *anchor)
{
    IkActionGrid *grid = new IkActionGrid(i18n("Layout"));
    QPointer<IkPopover> pop = IkMenus::create(d->mw);
    grid->onTriggered = [pop]() { if (pop) pop->dismiss(); };
    const struct { Mode mode; const char *icon; } modes[] = {{Desktop, "system"}, {Kriate, "brush"}, {Minimal, "minus"}, {Classic, "grid"}};
    for (const auto &m : modes) {
        QAction *a = d->modeActions.value(int(m.mode));
        if (!a || !a->isVisible()) continue;
        IkGridTile *t = grid->addItem(QString::fromLatin1(m.icon), modeDisplayName(m.mode), [a]() { a->trigger(); });
        t->setCheckable(true);
        t->setChecked(d->mode == m.mode);
    }
    grid->addItem(QStringLiteral("edit-interface"), i18n("Edit Interface"), [this]() {
        QTimer::singleShot(0, this, [this]() { setEditingInterface(true); });
    }, d->layout(d->mode) && d->onCanvasPage());
    pop->setContent(grid);
    pop->popup(anchor, IkPopover::Below);
}

void IkInterfaceManager::showKritaMenus(QWidget *anchor)
{
    // Every Krita menu (File, Edit, View, Image, Layer, Select, Filter, ...).
    QMenu menu(d->mw);
    if (d->mw->menuBar()) {
        const QList<QAction *> top = d->mw->menuBar()->actions();
        for (QAction *a : top) {
            if (a->isVisible() && a->menu()) menu.addAction(a);
        }
    }
    const QPoint pos = anchor ? anchor->mapToGlobal(QPoint(0, anchor->height() + 4)) : QCursor::pos();
    menu.exec(pos);
}

void IkInterfaceManager::showNamedMenu(const QString &menuName, QWidget *anchor)
{
    QMenu *menu = nullptr;
    if (d->mw->guiFactory()) menu = qobject_cast<QMenu *>(d->mw->guiFactory()->container(menuName, d->mw));
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
    if (vm->inputManager()) vm->inputManager()->registerPopupWidget(palette);
    palette->popup(pos);
}

bool IkInterfaceManager::activatePresetMatching(const QStringList &fragments)
{
    KisViewManager *vm = viewManager();
    if (!vm || !vm->paintOpBox()) return false;
    KisResourceModel model(ResourceType::PaintOpPresets);
    // Exact names first, then partial matches.
    for (int pass = 0; pass < 2; ++pass) {
        for (const QString &fragment : fragments) {
            for (int row = 0; row < model.rowCount(); ++row) {
                const QModelIndex idx = model.index(row, 0);
                const QString name = model.data(idx, Qt::UserRole + KisAbstractResourceModel::Name).toString();
                const bool match = pass == 0 ? name == fragment : name.contains(fragment, Qt::CaseInsensitive);
                if (match) {
                    if (KoResourceSP res = model.resourceForIndex(idx)) {
                        vm->paintOpBox()->resourceSelected(res);
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

void IkInterfaceManager::showBrushEditor()
{
    ensureBrushTool();
    QAction *editor = action(QStringLiteral("show_brush_editor"));
    if (!editor) return;
    editor->trigger();
    // The editor normally hangs off a tool bar button that the IKriate layouts
    // hide; centre it in the window instead.
    QTimer::singleShot(0, this, [this]() {
        // The editor may sit inside a pop-up frame: look inside every window.
        for (QWidget *top : QApplication::topLevelWidgets()) {
            if (!top->isVisible()) continue;
            QList<QWidget *> candidates = top->findChildren<QWidget *>();
            candidates.prepend(top);
            for (QWidget *w : candidates) {
                if (w->inherits("KisPaintOpPresetsEditor") && w->isVisible()) IkBrushSettings::adoptEditor(w, this, true);
            }
        }
        const QList<QWidget *> tops = QApplication::topLevelWidgets();
        for (QWidget *w : tops) {
            if (w->isVisible() && w->inherits("KisPaintOpPresetsEditor")) {
                const QRect g = d->mw->geometry();
                const QSize s = w->size().boundedTo(g.size() - QSize(40, 40));
                w->resize(s);
                w->move(g.center() - QPoint(s.width() / 2, s.height() / 2));
            }
        }
        const QList<QWidget *> kids = d->mw->findChildren<QWidget *>();
        for (QWidget *w : kids) {
            if (w->isVisible() && w->inherits("KisPaintOpPresetsEditor") && w->isWindow()) {
                const QRect g = d->mw->geometry();
                const QSize s = w->size().boundedTo(g.size() - QSize(40, 40));
                w->resize(s);
                w->move(g.center() - QPoint(s.width() / 2, s.height() / 2));
            }
        }
    });
}

void IkInterfaceManager::createBrushFromCurrent()
{
    KisViewManager *vm = viewManager();
    if (!vm || !vm->canvasResourceProvider() || !vm->canvasResourceProvider()->currentPreset()) return;
    KisPaintOpPresetSP current = vm->canvasResourceProvider()->currentPreset();
    KoResourceSP copy = current->clone();
    copy->setName(current->name() + i18nc("suffix for a new brush based on another", " (new)"));
    copy->setFilename(QString());
    KisResourceModel model(ResourceType::PaintOpPresets);
    if (model.addResourceDeduplicateFileName(copy)) {
        vm->paintOpBox()->resourceSelected(copy);
        showBrushEditor();
    }
}

void IkInterfaceManager::showBrushLibrary()
{
    if (!d->brushes) {
        d->brushes = new IkPopover(d->mw);
        d->brushes->setRadius(16);
        IkBrushLibrary *lib = new IkBrushLibrary(this, d->brushes);
        d->brushes->setContent(lib);
    }
    if (IkBrushLibrary *lib = qobject_cast<IkBrushLibrary *>(d->brushes->content())) lib->reload();
    d->brushes->popup(nullptr, IkPopover::Center, QSize(600, qMin(760, d->mw->height() - 80)));
}

void IkInterfaceManager::showSettings()
{
    if (d->home && !d->mw->activeView()) {
        d->home->showPage(IkHomeScreen::SettingsPage);
        return;
    }
    if (!d->settings) {
        d->settings = new IkPopover(d->mw);
        d->settings->setRole(IkStyle::Background);
        d->settings->setRadius(18);
        d->settings->setDimBackground(true);
        IkSettingsPage *page = new IkSettingsPage(d->mw, d->settings);
        page->setManager(this);
        d->settings->setContent(page);
    }
    if (IkSettingsPage *page = qobject_cast<IkSettingsPage *>(d->settings->content())) page->reload();
    d->settings->popup(nullptr, IkPopover::Center, d->mw->size() * 0.92);
}
