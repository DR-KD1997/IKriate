/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "IkLayouts.h"
#include "IkPractice.h"
#include "IkLayerAdjust.h"

#include <QAction>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QMenu>
#include <QVBoxLayout>
#include <QtMath>

#include <klocalizedstring.h>

#include <KoCanvasResourceProvider.h>
#include <KoCanvasResourcesIds.h>
#include <KoColor.h>
#include <KoCanvasController.h>
#include <KoToolManager.h>

#include "KisMainWindow.h"
#include "KisViewManager.h"
#include "kis_canvas_resource_provider.h"

#include "IkBrushLibrary.h"
#include "IkColorPanel.h"
#include "IkInterfaceManager.h"
#include "IkLayersPanel.h"
#include "IkSkinManager.h"
#include "IkStyle.h"
#include "IkWidgets.h"
#include <QTimer>
#include "IkMenus.h"
#include "IkReferences.h"

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

QString sizeText(qreal px)
{
    return px < 10 ? QString::number(px, 'f', 1) : QString::number(qRound(px));
}
}

IkLayout::IkLayout(IkInterfaceManager *manager)
    : QObject(manager)
    , m_manager(manager)
{
}

IkLayout::~IkLayout()
{
    closePopovers();
    delete m_editOverlay.data();
    for (const QPointer<QWidget> &w : m_panels) delete w.data();
    delete m_colorPop.data();
    delete m_layersPop.data();
    delete m_brushPop.data();
    delete m_toolsPop.data();
    delete m_optionsPop.data();
}

IkInterfaceManager *IkLayout::manager() const { return m_manager; }
QWidget *IkLayout::host() const { return m_manager->mainWindow(); }
bool IkLayout::isVisible() const { return m_visible; }
QMargins IkLayout::reservedMargins() const { return QMargins(); }

void IkLayout::setVisible(bool visible)
{
    m_visible = visible;
    for (const QPointer<QWidget> &w : m_panels) {
        if (w) w->setVisible(visible && !w->property("ikHidden").toBool());
    }
    if (!visible) {
        closePopovers();
        setEditing(false);
    }
    if (visible) {
        refresh();
        for (const QPointer<QWidget> &w : m_panels) if (w) w->raise();
        raisePopovers();
    }
}

IkPanel *IkLayout::panel(bool pill)
{
    IkPanel *p = new IkPanel(host());
    p->setPill(pill);
    p->setRole(IkStyle::Surface);
    p->hide();
    m_panels << p;
    return p;
}

IkIconButton *IkLayout::button(QWidget *parent, const QString &icon, const QString &tip)
{
    return new IkIconButton(icon, tip, parent);
}

IkIconButton *IkLayout::toolButton(QWidget *parent, const QString &icon, const QString &tip, const QString &toolId)
{
    IkIconButton *b = button(parent, icon, tip);
    connect(b, &QAbstractButton::clicked, this, [this, toolId]() {
        m_manager->setPaintMode(IkInterfaceManager::Draw);
        m_manager->activateTool(toolId);
    });
    m_toolButtons << qMakePair(QPointer<IkIconButton>(b), toolId);
    return b;
}

IkIconButton *IkLayout::actionButton(QWidget *parent, const QString &icon, const QString &tip, const QString &action)
{
    IkIconButton *b = button(parent, icon, tip);
    if (QAction *a = m_manager->action(action)) {
        b->bindAction(a);
    } else {
        b->setEnabled(false);
    }
    return b;
}

void IkLayout::bindSize(IkPillSlider *slider)
{
    m_sizePills << slider;
    slider->setLabel(i18n("Brush size"));
    slider->setReadoutFunction([](qreal t) { return sizeText(sliderToSize(t)); });
    connect(slider, &IkPillSlider::valueEdited, this, [this](qreal t) {
        if (KisViewManager *vm = m_manager->viewManager()) vm->canvasResourceProvider()->setSize(sliderToSize(t));
    });
}

void IkLayout::bindOpacity(IkPillSlider *slider)
{
    m_opacityPills << slider;
    slider->setLabel(i18n("Opacity"));
    slider->setReadoutFunction([](qreal t) { return QStringLiteral("%1%").arg(qRound(t * 100)); });
    connect(slider, &IkPillSlider::valueEdited, this, [this](qreal t) {
        if (KisViewManager *vm = m_manager->viewManager()) vm->canvasResourceProvider()->setOpacity(t);
    });
}

void IkLayout::bindSize(IkSlider *slider, QLabel *readout)
{
    m_sizeSliders << qMakePair(QPointer<IkSlider>(slider), QPointer<QLabel>(readout));
    connect(slider, &IkSlider::valueEdited, this, [this, readout](qreal t) {
        if (KisViewManager *vm = m_manager->viewManager()) vm->canvasResourceProvider()->setSize(sliderToSize(t));
        if (readout) readout->setText(QStringLiteral("%1 px").arg(sizeText(sliderToSize(t))));
    });
}

void IkLayout::bindOpacity(IkSlider *slider, QLabel *readout)
{
    m_opacitySliders << qMakePair(QPointer<IkSlider>(slider), QPointer<QLabel>(readout));
    connect(slider, &IkSlider::valueEdited, this, [this, readout](qreal t) {
        if (KisViewManager *vm = m_manager->viewManager()) vm->canvasResourceProvider()->setOpacity(t);
        if (readout) readout->setText(QStringLiteral("%1%").arg(qRound(t * 100)));
    });
}

void IkLayout::bindColorSwatch(IkSwatch *swatch)
{
    m_swatches << swatch;
}

IkPopover *IkLayout::popover(QPointer<IkPopover> &slot)
{
    if (!slot) {
        slot = new IkPopover(host());
        slot->setRadius(16);
    }
    return slot;
}

void IkLayout::closeTransient()
{
    closePopovers();
    setEditing(false);
}

void IkLayout::raisePopovers()
{
    IkReferences::raiseFloating();
    IkPractice::raiseActive();
    // Every open popover in the window (the layout's own and the ones opened
    // through IkMenus: options, layer actions, reference images, ...).
    for (IkPopover *p : host()->findChildren<IkPopover *>(QString(), Qt::FindDirectChildrenOnly)) {
        if (p->isOpen()) p->raiseToTop();
    }
    syncEditOverlay();
}

void IkLayout::closePopovers()
{
    for (IkPopover *p : {m_colorPop.data(), m_layersPop.data(), m_brushPop.data(), m_toolsPop.data(), m_optionsPop.data(),
                         m_adjustPop.data(), m_symmetryPop.data()}) {
        if (p) p->dismiss();
    }
}

void IkLayout::toggleColorPopover(QWidget *anchor, int placement)
{
    IkPopover *p = popover(m_colorPop);
    if (p->isOpen()) {
        p->dismiss();
        return;
    }
    if (!m_colorPanel) {
        m_colorPanel = new IkColorPanel(m_manager, p);
        p->setContent(m_colorPanel);
    }
    m_colorPanel->refresh();
    p->popup(anchor, IkPopover::Placement(placementFor(anchor, placement)), QSize(380, 520));
}

void IkLayout::toggleAdjustPopover(QWidget *anchor, int placement)
{
    IkPopover *p = popover(m_adjustPop);
    if (p->isOpen()) {
        p->dismiss();
        return;
    }
    // A fresh panel each time: closing it commits (or nothing was changed).
    IkLayerAdjustPanel *panel = new IkLayerAdjustPanel(m_manager, p);
    connect(p, &IkPopover::dismissed, panel, [panel]() {
        panel->commit();
        panel->deleteLater();
    });
    p->setContent(panel);
    p->popup(anchor, IkPopover::Placement(placementFor(anchor, placement)), QSize(IkStyle::scaled(400), p->sizeHint().height()));
}

IkIconButton *IkLayout::symmetryButton(QWidget *parent)
{
    IkIconButton *b = button(parent, QStringLiteral("symmetry"), i18n("Symmetry"));
    connect(b, &QAbstractButton::clicked, this, [this, b]() { toggleSymmetryPopover(b, IkPopover::Below); });
    m_symmetryButtons << b;
    return b;
}

IkIconButton *IkLayout::transformButton(QWidget *parent)
{
    IkIconButton *b = button(parent, QStringLiteral("transform"), i18n("Transform"));
    connect(b, &QAbstractButton::clicked, this, [this, b]() {
        m_manager->activateTransform(m_manager->transformMode());
        showTransformOptions(b);
    });
    m_toolButtons << qMakePair(QPointer<IkIconButton>(b), QStringLiteral("KisToolTransform"));
    return b;
}

void IkLayout::showOptionStrip(QWidget *anchor, const QList<StripItem> &items)
{
    closePopovers();
    QWidget *strip = new QWidget();
    QHBoxLayout *l = new QHBoxLayout(strip);
    l->setContentsMargins(4, 2, 4, 2);
    l->setSpacing(IkStyle::scaled(10));
    QPointer<IkPopover> pop = IkMenus::create(host());
    for (const StripItem &item : items) {
        IkIconButton *b = new IkIconButton(item.icon, item.tip, strip);
        b->setVariant(IkIconButton::Tile);
        b->setEdge(IkStyle::touchSize() + IkStyle::scaled(10));
        b->setIconPixelSize(IkStyle::scaled(28));
        b->setActive(item.active);
        const std::function<void()> action = item.action;
        connect(b, &QAbstractButton::clicked, this, [pop, action]() {
            if (pop) pop->dismiss();
            if (action) action();
        });
        l->addWidget(b);
    }
    pop->setContent(strip);
    pop->popup(anchor, IkPopover::Placement(placementFor(anchor, IkPopover::Below)));
}

void IkLayout::showTransformOptions(QWidget *anchor)
{
    IkInterfaceManager *m = m_manager;
    const int mode = m->transformMode();
    auto activate = [m](int mode) { return [m, mode]() { m->activateTransform(mode); }; };
    showOptionStrip(anchor, {
        {QStringLiteral("transform-basic"), i18n("Basic: move, scale and rotate"), activate(IkInterfaceManager::TransformBasic),
         mode == IkInterfaceManager::TransformBasic},
        {QStringLiteral("transform-anchor"), i18n("Anchor: pin points and bend around them"), activate(IkInterfaceManager::TransformAnchor),
         mode == IkInterfaceManager::TransformAnchor},
        {QStringLiteral("transform-distort"), i18n("Distort: move the corners (perspective)"), activate(IkInterfaceManager::TransformDistort),
         mode == IkInterfaceManager::TransformDistort},
        {QStringLiteral("transform-warp"), i18n("Warp"), activate(IkInterfaceManager::TransformWarp),
         mode == IkInterfaceManager::TransformWarp},
        {QStringLiteral("transform-flip"), i18n("Flip"), [this, anchor]() {
             QPointer<QWidget> a = anchor;
             QTimer::singleShot(0, this, [this, a]() {
                 if (!a) return;
                 showOptionStrip(a, {{QStringLiteral("transform-flip"), i18n("Flip horizontally"),
                                      [this]() { m_manager->flipLayer(true); }, false},
                                     {QStringLiteral("flip-v"), i18n("Flip vertically"),
                                      [this]() { m_manager->flipLayer(false); }, false}});
             });
         }, false},
    });
}

void IkLayout::showSmudgeOptions(QWidget *anchor)
{
    IkInterfaceManager *m = m_manager;
    const int v = m->smudgeVariant();
    auto choose = [m](int variant) { return [m, variant]() { m->setSmudgeVariant(variant); }; };
    showOptionStrip(anchor, {
        {QStringLiteral("smudge"), i18n("Smudge"), choose(IkInterfaceManager::SmudgeSmear), v == IkInterfaceManager::SmudgeSmear},
        {QStringLiteral("blur"), i18n("Blur"), choose(IkInterfaceManager::SmudgeBlur), v == IkInterfaceManager::SmudgeBlur},
        {QStringLiteral("liquify"), i18n("Liquify"), choose(IkInterfaceManager::SmudgeLiquify), v == IkInterfaceManager::SmudgeLiquify},
    });
}

void IkLayout::toggleSymmetryPopover(QWidget *anchor, int placement)
{
    IkPopover *p = popover(m_symmetryPop);
    if (p->isOpen()) {
        p->dismiss();
        return;
    }
    QWidget *content = new QWidget(p);
    QVBoxLayout *l = new QVBoxLayout(content);
    l->setContentsMargins(4, 4, 4, 4);
    l->setSpacing(2);
    QLabel *title = new QLabel(i18n("Symmetry"), content);
    title->setFont(IkStyle::font(1.15, QFont::Bold));
    title->setContentsMargins(10, 4, 10, 6);
    l->addWidget(title);
    const IkInterfaceManager::Symmetry current = m_manager->symmetry();
    struct Opt { IkInterfaceManager::Symmetry s; QString icon; QString text; };
    const QList<Opt> opts = {{IkInterfaceManager::SymmetryOff, QStringLiteral("close"), i18n("Off")},
                             {IkInterfaceManager::SymmetryX, QStringLiteral("symmetry-x"), i18n("Left and right (X)")},
                             {IkInterfaceManager::SymmetryY, QStringLiteral("symmetry-y"), i18n("Top and bottom (Y)")},
                             {IkInterfaceManager::SymmetryXY, QStringLiteral("symmetry"), i18n("Both (X and Y)")},
                             {IkInterfaceManager::SymmetryRadial, QStringLiteral("symmetry-radial"), i18n("Radial")}};
    for (const Opt &o : opts) {
        IkNavRow *row = new IkNavRow(o.icon, o.text, content);
        row->setSelected(o.s == current);
        const IkInterfaceManager::Symmetry s = o.s;
        connect(row, &QAbstractButton::clicked, this, [this, p, s]() {
            m_manager->setSymmetry(s);
            p->dismiss();
        });
        l->addWidget(row);
    }
    // Radial options.
    const QList<int> sections = {2, 3, 4, 5, 6, 8, 10, 12, 16};
    IkChoiceRow *count = new IkChoiceRow(i18n("Radial sections"), content);
    QStringList names;
    for (int n : sections) names << QString::number(n);
    count->setChoices(names, qMax(0, sections.indexOf(m_manager->radialSections())));
    connect(count, &IkChoiceRow::chosen, this, [this, sections](int i) {
        m_manager->setSymmetry(IkInterfaceManager::SymmetryRadial, sections.value(i, 6));
    });
    l->addWidget(count);
    IkNavRow *mirrored = new IkNavRow(QStringLiteral("symmetry-x"), i18n("Mirror each section"), content);
    IkSwitch *sw = new IkSwitch(mirrored);
    sw->setChecked(m_manager->radialMirrored());
    mirrored->setTrailing(sw);
    connect(mirrored, &QAbstractButton::clicked, sw, [sw]() { sw->toggle(); });
    connect(sw, &QAbstractButton::toggled, this, [this](bool on) {
        m_manager->setSymmetry(IkInterfaceManager::SymmetryRadial, -1, on ? 1 : 0);
    });
    l->addWidget(mirrored);
    p->setContent(content);
    p->popup(anchor, IkPopover::Placement(placementFor(anchor, placement)), QSize(IkStyle::scaled(300), p->sizeHint().height()));
}

void IkLayout::toggleLayersPopover(QWidget *anchor, int placement)
{
    IkPopover *p = popover(m_layersPop);
    if (p->isOpen()) {
        p->dismiss();
        return;
    }
    if (!m_layersPanel) {
        m_layersPanel = new IkLayersPanel(m_manager, p);
        p->setContent(m_layersPanel);
    }
    m_layersPanel->refresh();
    const int h = qMin(620, host()->height() - 120);
    p->popup(anchor, IkPopover::Placement(placementFor(anchor, placement)), QSize(420, h));
}

void IkLayout::toggleBrushPopover(QWidget *anchor, int placement)
{
    IkPopover *p = popover(m_brushPop);
    if (p->isOpen()) {
        p->dismiss();
        return;
    }
    if (!m_brushLibrary) {
        m_brushLibrary = new IkBrushLibrary(m_manager, p);
        p->setContent(m_brushLibrary);
    }
    m_brushLibrary->reload();
    const int h = qMin(720, host()->height() - 120);
    p->popup(anchor, IkPopover::Placement(placementFor(anchor, placement)), QSize(560, h));
}

void IkLayout::showToolsPopover(QWidget *anchor, int placement)
{
    IkPopover *p = popover(m_toolsPop);
    if (p->isOpen()) {
        p->dismiss();
        return;
    }
    QWidget *grid = new QWidget(p);
    QGridLayout *g = new QGridLayout(grid);
    g->setSpacing(6);
    struct T { QString icon; QString tip; QString id; };
    const QList<T> tools = {
        {QStringLiteral("brush"), i18n("Freehand brush"), QStringLiteral("KritaShape/KisToolBrush")},
        {QStringLiteral("bezier"), i18n("Bezier curve"), QStringLiteral("KisToolPath")},
        {QStringLiteral("minus"), i18n("Line"), QStringLiteral("KritaShape/KisToolLine")},
        {QStringLiteral("shapes"), i18n("Rectangle"), QStringLiteral("KritaShape/KisToolRectangle")},
        {QStringLiteral("basic"), i18n("Ellipse"), QStringLiteral("KritaShape/KisToolEllipse")},
        {QStringLiteral("fill"), i18n("Fill"), QStringLiteral("KritaFill/KisToolFill")},
        {QStringLiteral("palette"), i18n("Gradient"), QStringLiteral("KritaFill/KisToolGradient")},
        {QStringLiteral("pipette"), i18n("Sample colour"), QStringLiteral("KritaSelected/KisToolColorSampler")},
        {QStringLiteral("text"), i18n("Text"), QStringLiteral("SvgTextTool")},
        {QStringLiteral("move"), i18n("Move"), QStringLiteral("KritaTransform/KisToolMove")},
        {QStringLiteral("transform"), i18n("Transform"), QStringLiteral("KisToolTransform")},
        {QStringLiteral("select-shape"), i18n("Select shapes"), QStringLiteral("InteractionTool")},
        {QStringLiteral("assistant"), i18n("Drawing assistants"), QStringLiteral("KisAssistantTool")},
        {QStringLiteral("reference"), i18n("Reference images"), QStringLiteral("ToolReferenceImages")},
        {QStringLiteral("selection-fill"), i18n("Selection Fill: lasso a shape to fill it"), QStringLiteral("ikriate:selection-fill")},
        {QStringLiteral("lasso"), i18n("Freehand selection"), QStringLiteral("KisToolSelectOutline")},
    };
    int i = 0;
    for (const T &t : tools) {
        IkIconButton *b = button(grid, t.icon, t.tip);
        b->setVariant(IkIconButton::Tile);
        b->setEdge(IkStyle::touchSize() + 10);
        const bool fillActive = m_manager->isSelectionFillActive();
        b->setActive(t.id == QLatin1String("ikriate:selection-fill") ? fillActive
                     : (KoToolManager::instance()->activeToolId() == t.id && !(fillActive && t.id == QLatin1String("KisToolSelectOutline"))));
        const QString id = t.id;
        QPointer<QWidget> anchorGuard = anchor;
        connect(b, &QAbstractButton::clicked, this, [this, p, id, anchorGuard]() {
            p->dismiss();
            if (id == QLatin1String("ikriate:selection-fill")) {
                m_manager->activateSelectionFill();
            } else if (id == QLatin1String("ToolReferenceImages")) {
                IkReferences::buttonClicked(m_manager, anchorGuard);
            } else if (id == QLatin1String("KisToolTransform")) {
                m_manager->activateTransform(m_manager->transformMode());
            } else {
                m_manager->setPaintMode(IkInterfaceManager::Draw);
                m_manager->activateTool(id);
            }
        });
        g->addWidget(b, i / 4, i % 4);
        ++i;
    }
    QWidget *old = p->content();
    p->setContent(grid);
    delete old;
    p->popup(anchor, IkPopover::Placement(placementFor(anchor, placement)));
}

void IkLayout::showSelectionPopover(QWidget *anchor, int placement)
{
    IkPopover *p = popover(m_toolsPop);
    if (p->isOpen()) {
        p->dismiss();
        return;
    }
    QWidget *w = new QWidget(p);
    QVBoxLayout *l = new QVBoxLayout(w);
    l->setSpacing(6);
    QHBoxLayout *tools = new QHBoxLayout();
    struct T { QString icon; QString tip; QString id; };
    const QList<T> list = {
        {QStringLiteral("select-rect"), i18n("Rectangle"), QStringLiteral("KisToolSelectRectangular")},
        {QStringLiteral("basic"), i18n("Ellipse"), QStringLiteral("KisToolSelectElliptical")},
        {QStringLiteral("lasso"), i18n("Freehand"), QStringLiteral("KisToolSelectOutline")},
        {QStringLiteral("shapes"), i18n("Polygon"), QStringLiteral("KisToolSelectPolygonal")},
        {QStringLiteral("select-magic"), i18n("Magic wand"), QStringLiteral("KisToolSelectContiguous")},
        {QStringLiteral("palette"), i18n("Similar colour"), QStringLiteral("KisToolSelectSimilar")},
    };
    for (const T &t : list) {
        IkIconButton *b = button(w, t.icon, t.tip);
        b->setVariant(IkIconButton::Tile);
        b->setEdge(IkStyle::touchSize() + 8);
        b->setActive(KoToolManager::instance()->activeToolId() == t.id);
        const QString id = t.id;
        connect(b, &QAbstractButton::clicked, this, [this, p, id]() {
            m_manager->activateTool(id);
            p->dismiss();
        });
        tools->addWidget(b);
    }
    l->addLayout(tools);
    QHBoxLayout *actions = new QHBoxLayout();
    for (const auto &a : QList<QPair<QString, QString>>{{i18n("All"), QStringLiteral("select_all")},
                                                         {i18n("Deselect"), QStringLiteral("deselect")},
                                                         {i18n("Invert"), QStringLiteral("invert_selection")},
                                                         {i18n("Reselect"), QStringLiteral("reselect")}}) {
        QPushButton *b = new QPushButton(a.first, w);
        b->setMinimumHeight(IkStyle::touchSize() - 4);
        const QString name = a.second;
        connect(b, &QPushButton::clicked, this, [this, p, name]() {
            m_manager->triggerAction(name);
            p->dismiss();
        });
        actions->addWidget(b);
    }
    l->addLayout(actions);
    QWidget *old = p->content();
    p->setContent(w);
    delete old;
    p->popup(anchor, IkPopover::Placement(placementFor(anchor, placement)));
}

void IkLayout::showToolOptionsPopover(QWidget *anchor, int placement)
{
    IkPopover *p = popover(m_optionsPop);
    if (p->isOpen()) {
        p->dismiss();
        return;
    }
    QWidget *host = p->content();
    if (!host) {
        host = new QWidget(p);
        QVBoxLayout *l = new QVBoxLayout(host);
        l->setContentsMargins(0, 0, 0, 0);
        p->setContent(host);
        connect(p, &IkPopover::dismissed, this, [this]() { m_manager->releaseToolOptions(); });
    }
    m_manager->takeToolOptions(host);
    p->popup(anchor, IkPopover::Placement(placementFor(anchor, placement)), QSize(400, qMin(600, this->host()->height() - 140)));
}

void IkLayout::showMoreMenu(QWidget *anchor)
{
    m_manager->showMainMenu(anchor);
}

void IkLayout::ensureConnections()
{
    if (m_connected) return;
    KisViewManager *vm = m_manager->viewManager();
    if (!vm || !vm->canvasResourceProvider() || !vm->canvasResourceProvider()->resourceManager()) return;
    m_connected = true;
    connect(vm->canvasResourceProvider()->resourceManager(), &KoCanvasResourceProvider::canvasResourceChanged, this,
            [this](int key, const QVariant &) {
                if (!m_visible) return;
                switch (key) {
                case KoCanvasResource::Size:
                case KoCanvasResource::Opacity:
                    refreshSliders();
                    break;
                case KoCanvasResource::ForegroundColor:
                case KoCanvasResource::BackgroundColor:
                    refreshColor();
                    break;
                case KoCanvasResource::CurrentPaintOpPreset:
                case KoCanvasResource::EraserMode:
                    refreshTools();
                    refreshSliders();
                    break;
                default:
                    break;
                }
            });
    connect(KoToolManager::instance(), &KoToolManager::changedTool, this, [this]() { if (m_visible) refreshTools(); });
    connect(vm, &KisViewManager::viewChanged, this, [this]() { if (m_visible) refresh(); });
    connect(m_manager, &IkInterfaceManager::paintModeChanged, this, [this]() { if (m_visible) refreshTools(); });
    connect(m_manager, &IkInterfaceManager::symmetryChanged, this, [this]() { if (m_visible) refreshTools(); });
    for (const char *name : {"hmirror_action", "vmirror_action"}) {
        if (QAction *a = m_manager->action(QString::fromLatin1(name))) {
            connect(a, &QAction::toggled, this, [this]() { if (m_visible) refreshTools(); });
        }
    }
    connect(IkSkinManager::instance(), &IkSkinManager::sigSkinChanged, this, &IkLayout::restyle);
}

void IkLayout::refresh()
{
    ensureConnections();
    refreshTools();
    refreshSliders();
    refreshColor();
    refreshDocument();
}

void IkLayout::restyle()
{
    for (const QPointer<QWidget> &w : m_panels) {
        if (!w) continue;
        w->update();
        const QList<QWidget *> kids = w->findChildren<QWidget *>();
        for (QWidget *k : kids) k->update();
    }
}

void IkLayout::refreshTools()
{
    const QString active = KoToolManager::instance()->activeToolId();
    const IkInterfaceManager::PaintMode mode = m_manager->paintMode();
    const bool brushTool = active == QLatin1String("KritaShape/KisToolBrush") || active == QLatin1String("KritaShape/KisToolMultiBrush");
    const bool symmetric = m_manager->symmetry() != IkInterfaceManager::SymmetryOff;
    for (const auto &b : m_symmetryButtons) if (b) b->setActive(symmetric);
    for (const auto &entry : m_toolButtons) {
        if (!entry.first) continue;
        bool on = entry.second == active;
        if (on && brushTool) on = mode == IkInterfaceManager::Draw;
        entry.first->setActive(on);
    }
    for (const auto &b : m_drawButtons) if (b) b->setActive(brushTool && mode == IkInterfaceManager::Draw);
    for (const auto &b : m_eraseButtons) if (b) b->setActive(mode == IkInterfaceManager::Erase);
    for (const auto &b : m_smudgeButtons) if (b) b->setActive(brushTool && mode == IkInterfaceManager::Smudge);
}

void IkLayout::refreshSliders()
{
    KisViewManager *vm = m_manager->viewManager();
    if (!vm || !vm->canvasResourceProvider()) return;
    const qreal size = vm->canvasResourceProvider()->size();
    const qreal opacity = vm->canvasResourceProvider()->opacity();
    for (const auto &s : m_sizePills) if (s) s->setValue(sizeToSlider(size));
    for (const auto &s : m_opacityPills) if (s) s->setValue(opacity);
    for (const auto &s : m_sizeSliders) {
        if (s.first) s.first->setValue(sizeToSlider(size));
        if (s.second) s.second->setText(QStringLiteral("%1 px").arg(sizeText(size)));
    }
    for (const auto &s : m_opacitySliders) {
        if (s.first) s.first->setValue(opacity);
        if (s.second) s.second->setText(QStringLiteral("%1%").arg(qRound(opacity * 100)));
    }
}

void IkLayout::refreshColor()
{
    KisViewManager *vm = m_manager->viewManager();
    if (!vm || !vm->canvasResourceProvider()) return;
    const QColor fg = vm->canvasResourceProvider()->fgColor().toQColor();
    for (const auto &s : m_swatches) if (s) s->setColor(fg);
}

void IkLayout::refreshDocument()
{
}
