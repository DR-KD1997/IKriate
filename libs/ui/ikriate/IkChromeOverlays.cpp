/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "IkChromeOverlays.h"

#include <QAction>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtMath>

#include <KConfigGroup>
#include <KSharedConfig>
#include <klocalizedstring.h>

#include <KoCanvasResourceProvider.h>
#include <KoCanvasResourcesIds.h>
#include <KoColor.h>
#include <KoCanvasController.h>
#include <KoToolManager.h>

#include "KisDocument.h"
#include "KisMainWindow.h"
#include "KisView.h"
#include "KisViewManager.h"
#include "kis_canvas_resource_provider.h"
#include "kis_favorite_resource_manager.h"
#include "kis_paintop_box.h"
#include <brushengine/kis_paintop_preset.h>

#include "IkChromeWidgets.h"
#include "IkInterfaceManager.h"
#include "IkSkinManager.h"

namespace
{
const qreal kMaxBrushSize = 1000.0;

qreal sizeToSlider(qreal px)
{
    if (px <= 1.0) return 0.0;
    return qBound<qreal>(0.0, std::log(px) / std::log(kMaxBrushSize), 1.0);
}

qreal sliderToSize(qreal t)
{
    return qMax<qreal>(1.0, std::pow(kMaxBrushSize, t));
}

const char *const kGestureCardKey = "GestureHintsDismissed";
}

// ============================================================ IkOverlayChrome

IkOverlayChrome::IkOverlayChrome(IkInterfaceManager *manager, QObject *parent)
    : QObject(parent)
    , m_manager(manager)
{
}

IkOverlayChrome::~IkOverlayChrome()
{
    for (const QPointer<QWidget> &w : m_panels) {
        delete w.data();
    }
}

IkInterfaceManager *IkOverlayChrome::manager() const
{
    return m_manager;
}

QWidget *IkOverlayChrome::host() const
{
    return m_manager->mainWindow();
}

IkPanel *IkOverlayChrome::createPanel(bool pill)
{
    IkPanel *panel = new IkPanel(host());
    panel->setPill(pill);
    panel->hide();
    m_panels << panel;
    return panel;
}

IkChromeButton *IkOverlayChrome::createButton(QWidget *parent, const QString &icon, const QString &tip)
{
    return new IkChromeButton(icon, tip, parent);
}

IkChromeButton *IkOverlayChrome::createToolButton(QWidget *parent, const QString &icon,
                                                  const QString &tip, const QString &toolId)
{
    IkChromeButton *b = createButton(parent, icon, tip);
    connect(b, &QToolButton::clicked, this, [this, toolId]() {
        m_manager->triggerAction(toolId);
    });
    m_toolButtons << qMakePair(QPointer<IkChromeButton>(b), toolId);
    return b;
}

void IkOverlayChrome::bindSizeAndOpacity(IkPillSlider *size, IkPillSlider *opacity)
{
    m_size = size;
    m_opacity = opacity;
    if (size) {
        size->setLabel(i18n("Brush size"));
        connect(size, &IkPillSlider::valueEdited, this, [this](qreal t) {
            if (KisViewManager *vm = m_manager->viewManager()) {
                vm->canvasResourceProvider()->setSize(sliderToSize(t));
            }
        });
    }
    if (opacity) {
        opacity->setLabel(i18n("Opacity"));
        connect(opacity, &IkPillSlider::valueEdited, this, [this](qreal t) {
            if (KisViewManager *vm = m_manager->viewManager()) {
                vm->canvasResourceProvider()->setOpacity(t);
            }
        });
    }
}

void IkOverlayChrome::ensureConnections()
{
    if (m_connected) return;
    KisViewManager *vm = m_manager->viewManager();
    if (!vm || !vm->canvasResourceProvider() || !vm->canvasResourceProvider()->resourceManager()) return;
    m_connected = true;

    connect(vm->canvasResourceProvider()->resourceManager(), &KoCanvasResourceProvider::canvasResourceChanged,
            this, [this](int key, const QVariant &) { slotResourceChanged(key); });
    connect(KoToolManager::instance(), &KoToolManager::changedTool, this, &IkOverlayChrome::slotToolChanged);
    connect(vm, &KisViewManager::viewChanged, this, &IkOverlayChrome::refresh);
    if (vm->paintOpBox() && vm->paintOpBox()->favoriteResourcesManager()) {
        connect(vm->paintOpBox()->favoriteResourcesManager(), &KisFavoriteResourceManager::updatePalettes,
                this, &IkOverlayChrome::refresh);
    }
    connect(IkSkinManager::instance(), &IkSkinManager::sigSkinChanged, this, &IkOverlayChrome::refresh);
}

void IkOverlayChrome::slotResourceChanged(int key)
{
    switch (key) {
    case KoCanvasResource::Size:
    case KoCanvasResource::Opacity:
        refreshSliders();
        break;
    case KoCanvasResource::ForegroundColor:
    case KoCanvasResource::CurrentPaintOpPreset:
        refresh();
        break;
    default:
        break;
    }
}

void IkOverlayChrome::slotToolChanged()
{
    refreshToolButtons();
}

void IkOverlayChrome::refreshToolButtons()
{
    const QString active = KoToolManager::instance()->activeToolId();
    for (const auto &entry : m_toolButtons) {
        if (entry.first) entry.first->setActive(entry.second == active);
    }
}

void IkOverlayChrome::refreshSliders()
{
    KisViewManager *vm = m_manager->viewManager();
    if (!vm || !vm->canvasResourceProvider()) return;
    if (m_size) m_size->setValue(sizeToSlider(vm->canvasResourceProvider()->size()));
    if (m_opacity) m_opacity->setValue(vm->canvasResourceProvider()->opacity());
}

void IkOverlayChrome::refresh()
{
    ensureConnections();
    refreshToolButtons();
    refreshSliders();
    for (const QPointer<QWidget> &w : m_panels) {
        if (w) w->update();
    }
}

void IkOverlayChrome::raisePanels()
{
    for (const QPointer<QWidget> &w : m_panels) {
        if (w) w->raise();
    }
}

void IkOverlayChrome::setVisible(bool visible)
{
    m_visible = visible;
    for (const QPointer<QWidget> &w : m_panels) {
        if (w) w->setVisible(visible && !w->property("ikDismissed").toBool());
    }
    if (visible) {
        refresh();
        raisePanels();
    }
}

bool IkOverlayChrome::isVisible() const
{
    return m_visible;
}

// ============================================================ IkMinimalChrome

IkMinimalChrome::IkMinimalChrome(IkInterfaceManager *manager, QObject *parent)
    : IkOverlayChrome(manager, parent)
{
    // ---- top bar ---------------------------------------------------------
    m_topBar = createPanel();
    m_topBar->setObjectName("IkMinimalTopBar");
    QHBoxLayout *top = new QHBoxLayout(m_topBar);
    top->setContentsMargins(6, 2, 6, 2);
    top->setSpacing(2);

    QPushButton *gallery = new QPushButton(i18n("Gallery"), m_topBar);
    gallery->setFlat(true);
    gallery->setMinimumHeight(IkChrome::touchSize() - 4);
    gallery->setToolTip(i18n("New, open, recent and save"));
    connect(gallery, &QPushButton::clicked, manager, [manager, gallery]() { manager->showNamedMenu("file", gallery); });
    top->addWidget(gallery);

    IkChromeButton *actions = createButton(m_topBar, "hamburger_menu_dots", i18n("Actions"));
    connect(actions, &QToolButton::clicked, manager, [manager, actions]() { manager->showMainMenu(actions); });
    top->addWidget(actions);

    IkChromeButton *adjust = createButton(m_topBar, "color-adjustment-mode-channels", i18n("Adjustments"));
    connect(adjust, &QToolButton::clicked, manager, [manager, adjust]() { manager->showNamedMenu("Filter", adjust); });
    top->addWidget(adjust);

    top->addWidget(createToolButton(m_topBar, "tool_rect_selection", i18n("Selection"), "KisToolSelectRectangular"));
    top->addWidget(createToolButton(m_topBar, "krita_tool_transform", i18n("Transform"), "KisToolTransform"));

    top->addStretch(1);

    top->addWidget(createToolButton(m_topBar, "krita_tool_freehand", i18n("Paint"), "KritaShape/KisToolBrush"));

    IkChromeButton *smudge = createButton(m_topBar, "colorsmudge", i18n("Smudge"));
    connect(smudge, &QToolButton::clicked, manager, [manager]() {
        manager->triggerAction("KritaShape/KisToolBrush");
        manager->activatePresetMatching({"Blender", "Smudge", "Blending"});
    });
    top->addWidget(smudge);

    IkChromeButton *erase = createButton(m_topBar, "draw-eraser", i18n("Eraser mode"));
    erase->bindAction(manager->action("erase_action"));
    top->addWidget(erase);

    IkChromeButton *studio = createButton(m_topBar, "paintop_settings_01", i18n("Brush Studio"));
    connect(studio, &QToolButton::clicked, manager, &IkInterfaceManager::showBrushStudio);
    top->addWidget(studio);

    IkChromeButton *layers = createButton(m_topBar, "all-layers", i18n("Layers"));
    connect(layers, &QToolButton::clicked, manager, [manager, layers]() { manager->toggleLayersPanel(layers); });
    top->addWidget(layers);

    m_colour = new IkSwatchButton(m_topBar);
    m_colour->setDiameter(26);
    m_colour->setCurrent(true);
    m_colour->setToolTip(i18n("Colour"));
    connect(m_colour, &QAbstractButton::clicked, manager, [manager]() { manager->showRadialPalette(); });
    top->addWidget(m_colour);

    // ---- side bar ----------------------------------------------------------
    m_sideBar = createPanel();
    m_sideBar->setObjectName("IkMinimalSideBar");
    QVBoxLayout *side = new QVBoxLayout(m_sideBar);
    side->setContentsMargins(4, 10, 4, 10);
    side->setSpacing(8);
    IkPillSlider *size = new IkPillSlider(Qt::Vertical, m_sideBar);
    side->addWidget(size, 0, Qt::AlignHCenter);
    IkChromeButton *sampler = createToolButton(m_sideBar, "krita_tool_color_sampler", i18n("Sample colour"),
                                               "KritaSelected/KisToolColorSampler");
    side->addWidget(sampler, 0, Qt::AlignHCenter);
    IkPillSlider *opacity = new IkPillSlider(Qt::Vertical, m_sideBar);
    side->addWidget(opacity, 0, Qt::AlignHCenter);
    IkChromeButton *undo = createButton(m_sideBar, "edit-undo", i18n("Undo"));
    undo->bindAction(manager->action("edit_undo"));
    side->addWidget(undo, 0, Qt::AlignHCenter);
    IkChromeButton *redo = createButton(m_sideBar, "edit-redo", i18n("Redo"));
    redo->bindAction(manager->action("edit_redo"));
    side->addWidget(redo, 0, Qt::AlignHCenter);
    bindSizeAndOpacity(size, opacity);
}

void IkMinimalChrome::layoutPanels(const QRect &area)
{
    const int h = IkChrome::touchSize() + 8;
    m_topBar->setGeometry(area.left(), area.top(), area.width(), h);
    const QSize sideHint = m_sideBar->sizeHint();
    const int sideY = area.top() + h + qMax(12, (area.height() - h - sideHint.height()) / 2);
    m_sideBar->setGeometry(area.left() + 10, sideY, sideHint.width(), sideHint.height());
}

void IkMinimalChrome::refresh()
{
    IkOverlayChrome::refresh();
    KisViewManager *vm = manager()->viewManager();
    if (vm && vm->canvasResourceProvider() && m_colour) {
        m_colour->setColor(vm->canvasResourceProvider()->fgColor().toQColor());
    }
}

// ============================================================= IkRadialChrome

IkRadialChrome::IkRadialChrome(IkInterfaceManager *manager, QObject *parent)
    : IkOverlayChrome(manager, parent)
{
    // ---- top-left: menu + document title ---------------------------------
    m_menuChip = createPanel(true);
    QHBoxLayout *menu = new QHBoxLayout(m_menuChip);
    menu->setContentsMargins(4, 4, 14, 4);
    menu->setSpacing(6);
    IkChromeButton *menuButton = createButton(m_menuChip, "hamburger_menu_dots", i18n("Menu"));
    connect(menuButton, &QToolButton::clicked, manager, [manager, menuButton]() { manager->showMainMenu(menuButton); });
    menu->addWidget(menuButton);
    m_docTitle = new QLabel(m_menuChip);
    QFont f = m_docTitle->font();
    f.setBold(true);
    m_docTitle->setFont(f);
    menu->addWidget(m_docTitle);

    // ---- top-right: undo / redo, layers ----------------------------------
    m_historyChip = createPanel(true);
    QHBoxLayout *hist = new QHBoxLayout(m_historyChip);
    hist->setContentsMargins(4, 4, 4, 4);
    hist->setSpacing(2);
    IkChromeButton *undo = createButton(m_historyChip, "edit-undo", i18n("Undo"));
    undo->bindAction(manager->action("edit_undo"));
    hist->addWidget(undo);
    IkChromeButton *redo = createButton(m_historyChip, "edit-redo", i18n("Redo"));
    redo->bindAction(manager->action("edit_redo"));
    hist->addWidget(redo);

    m_layersChip = createPanel(true);
    QHBoxLayout *lay = new QHBoxLayout(m_layersChip);
    lay->setContentsMargins(4, 4, 4, 4);
    IkChromeButton *layers = createButton(m_layersChip, "all-layers", i18n("Layers"));
    connect(layers, &QToolButton::clicked, manager, [manager, layers]() { manager->toggleLayersPanel(layers); });
    lay->addWidget(layers);

    // ---- left edge: size + opacity strips --------------------------------
    m_sliderPanel = createPanel();
    QVBoxLayout *sliders = new QVBoxLayout(m_sliderPanel);
    sliders->setContentsMargins(4, 8, 4, 8);
    sliders->setSpacing(12);
    IkPillSlider *size = new IkPillSlider(Qt::Vertical, m_sliderPanel);
    IkPillSlider *opacity = new IkPillSlider(Qt::Vertical, m_sliderPanel);
    sliders->addWidget(size, 0, Qt::AlignHCenter);
    sliders->addWidget(opacity, 0, Qt::AlignHCenter);
    bindSizeAndOpacity(size, opacity);

    // ---- bottom: quick dock ----------------------------------------------
    m_dock = createPanel();
    m_dock->setObjectName("IkRadialDock");
    QHBoxLayout *dock = new QHBoxLayout(m_dock);
    dock->setContentsMargins(6, 6, 6, 6);
    dock->setSpacing(4);

    IkChromeButton *radial = createButton(m_dock, "config-popup-palette", i18n("Radial palette (or hold a finger on the canvas)"));
    connect(radial, &QToolButton::clicked, manager, [manager]() { manager->showRadialPalette(); });
    dock->addWidget(radial);
    dock->addWidget(new IkSeparator(Qt::Vertical, m_dock));

    m_presetRow = new QHBoxLayout();
    m_presetRow->setSpacing(4);
    dock->addLayout(m_presetRow);

    IkChromeButton *studio = createButton(m_dock, "paintop_settings_01", i18n("Brush Studio"));
    connect(studio, &QToolButton::clicked, manager, &IkInterfaceManager::showBrushStudio);
    dock->addWidget(studio);
    dock->addWidget(new IkSeparator(Qt::Vertical, m_dock));

    m_colourRow = new QHBoxLayout();
    m_colourRow->setSpacing(2);
    dock->addLayout(m_colourRow);

    IkChromeButton *erase = createButton(m_dock, "draw-eraser", i18n("Eraser mode"));
    erase->bindAction(manager->action("erase_action"));
    dock->addWidget(erase);

    // ---- bottom-right: gesture hints -------------------------------------
    m_gestureCard = createPanel();
    QVBoxLayout *card = new QVBoxLayout(m_gestureCard);
    card->setContentsMargins(14, 10, 8, 12);
    card->setSpacing(4);
    QHBoxLayout *cardHead = new QHBoxLayout();
    QLabel *title = new QLabel(i18n("Gestures"), m_gestureCard);
    title->setFont(f);
    cardHead->addWidget(title);
    cardHead->addStretch(1);
    IkChromeButton *close = createButton(m_gestureCard, "window-close", i18n("Hide gesture hints"));
    close->setFixedSize(IkChrome::touchSize() - 8, IkChrome::touchSize() - 8);
    connect(close, &QToolButton::clicked, this, &IkRadialChrome::dismissGestureCard);
    cardHead->addWidget(close);
    card->addLayout(cardHead);
    const QList<QPair<QString, QString>> hints = {
        {i18n("Hold one finger"), i18n("Radial palette")},
        {i18n("Two-finger tap"), i18n("Undo")},
        {i18n("Three-finger tap"), i18n("Redo")},
        {i18n("Pinch / twist"), i18n("Zoom / rotate")},
        {i18n("Four-finger tap"), i18n("Hide all interface")},
    };
    for (const auto &h : hints) {
        QHBoxLayout *row = new QHBoxLayout();
        QLabel *k = new QLabel(h.first, m_gestureCard);
        k->setForegroundRole(QPalette::PlaceholderText);
        row->addWidget(k);
        row->addStretch(1);
        row->addWidget(new QLabel(h.second, m_gestureCard));
        card->addLayout(row);
    }
    const KConfigGroup cfg(KSharedConfig::openConfig(), "IKriate");
    m_gestureCard->setProperty("ikDismissed", cfg.readEntry(kGestureCardKey, false));

    rebuildDock();
}

void IkRadialChrome::dismissGestureCard()
{
    m_gestureCard->setProperty("ikDismissed", true);
    m_gestureCard->hide();
    KConfigGroup cfg(KSharedConfig::openConfig(), "IKriate");
    cfg.writeEntry(kGestureCardKey, true);
}

void IkRadialChrome::rebuildDock()
{
    KisViewManager *vm = manager()->viewManager();
    KisFavoriteResourceManager *fav = (vm && vm->paintOpBox()) ? vm->paintOpBox()->favoriteResourcesManager() : nullptr;

    // Favourite presets (up to 6 in the dock; the radial palette shows all).
    const int presetCount = fav ? qMin(6, fav->numFavoritePresets()) : 0;
    const QList<QImage> images = fav ? fav->favoritePresetImages() : QList<QImage>();
    const QVector<QString> names = fav ? fav->favoritePresetNamesList() : QVector<QString>();
    const QString current = (vm && vm->canvasResourceProvider() && vm->canvasResourceProvider()->currentPreset())
            ? vm->canvasResourceProvider()->currentPreset()->name() : QString();

    while (m_presetButtons.size() < presetCount) {
        IkPresetButton *b = new IkPresetButton(m_dock);
        const int index = m_presetButtons.size();
        connect(b, &QAbstractButton::clicked, this, [this, index]() {
            KisViewManager *vm = manager()->viewManager();
            if (vm && vm->paintOpBox() && vm->paintOpBox()->favoriteResourcesManager()) {
                vm->paintOpBox()->favoriteResourcesManager()->slotChangeActivePaintop(index);
            }
        });
        m_presetRow->addWidget(b);
        m_presetButtons << b;
    }
    for (int i = 0; i < m_presetButtons.size(); ++i) {
        IkPresetButton *b = m_presetButtons[i];
        if (!b) continue;
        b->setVisible(i < presetCount);
        if (i < presetCount) {
            const QString name = i < names.size() ? names[i] : QString();
            b->setPreset(i < images.size() ? images[i] : QImage(), name);
            b->setActive(!name.isEmpty() && name == current);
        }
    }

    // Current colour + recent colours.
    const int recent = fav ? qMin(4, fav->recentColorsTotal()) : 0;
    const int swatchCount = 1 + recent;
    while (m_swatches.size() < swatchCount) {
        IkSwatchButton *s = new IkSwatchButton(m_dock);
        const bool isCurrent = m_swatches.isEmpty();
        s->setDiameter(isCurrent ? 30 : 22);
        s->setCurrent(isCurrent);
        connect(s, &QAbstractButton::clicked, this, [this, s, isCurrent]() {
            if (isCurrent) {
                manager()->showRadialPalette();
                return;
            }
            KisViewManager *vm = manager()->viewManager();
            if (vm && vm->canvasResourceProvider()) {
                vm->canvasResourceProvider()->setFGColor(KoColor(s->color(), vm->canvasResourceProvider()->fgColor().colorSpace()));
            }
        });
        m_colourRow->addWidget(s);
        m_swatches << s;
    }
    for (int i = 0; i < m_swatches.size(); ++i) {
        IkSwatchButton *s = m_swatches[i];
        if (!s) continue;
        s->setVisible(i < swatchCount);
        if (i == 0) {
            if (vm && vm->canvasResourceProvider()) s->setColor(vm->canvasResourceProvider()->fgColor().toQColor());
        } else if (i < swatchCount && fav) {
            s->setColor(fav->recentColorAt(i - 1).toQColor());
        }
    }
    m_dock->adjustSize();
}

void IkRadialChrome::refresh()
{
    IkOverlayChrome::refresh();
    rebuildDock();

    QString title = i18n("Untitled");
    if (KisMainWindow *mw = manager()->mainWindow()) {
        if (KisView *view = mw->activeView()) {
            if (view->document()) {
                const QString path = view->document()->path();
                title = path.isEmpty() ? view->document()->caption() : QFileInfo(path).completeBaseName();
            }
        }
    }
    if (m_docTitle) m_docTitle->setText(title);
    if (m_menuChip) m_menuChip->adjustSize();

    // Panels may have changed size; ask the manager to re-place them.
    if (isVisible()) {
        QMetaObject::invokeMethod(manager(), "slotLayoutOverlays", Qt::QueuedConnection);
    }
}

void IkRadialChrome::layoutPanels(const QRect &area)
{
    const int m = 14;
    m_menuChip->adjustSize();
    m_menuChip->move(area.left() + m, area.top() + m);
    m_menuChip->resize(qMin(m_menuChip->sizeHint().width(), area.width() / 2), m_menuChip->sizeHint().height());

    m_layersChip->adjustSize();
    m_layersChip->move(area.right() - m - m_layersChip->width(), area.top() + m);
    m_historyChip->adjustSize();
    m_historyChip->move(m_layersChip->x() - 8 - m_historyChip->width(), area.top() + m);

    m_sliderPanel->adjustSize();
    m_sliderPanel->move(area.left() + m, area.top() + (area.height() - m_sliderPanel->height()) / 2);

    m_dock->adjustSize();
    const QSize dockSize(qMin(m_dock->sizeHint().width(), area.width() - 2 * m), m_dock->sizeHint().height());
    m_dock->setGeometry(area.left() + (area.width() - dockSize.width()) / 2,
                        area.bottom() - m - dockSize.height(), dockSize.width(), dockSize.height());

    m_gestureCard->adjustSize();
    const int cardW = qMax(240, m_gestureCard->sizeHint().width());
    QRect card(area.right() - m - cardW, m_dock->y() - 12 - m_gestureCard->sizeHint().height(),
               cardW, m_gestureCard->sizeHint().height());
    // Keep clear of the dock on narrow screens.
    if (card.left() < m_dock->geometry().right() + 12) {
        card.moveBottom(m_dock->y() - 12);
    } else {
        card.moveBottom(area.bottom() - m);
    }
    m_gestureCard->setGeometry(card);
}
