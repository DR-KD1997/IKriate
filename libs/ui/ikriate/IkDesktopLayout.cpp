/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "IkLayouts.h"

#include <QAction>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QRegularExpression>
#include <QMouseEvent>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <klocalizedstring.h>

#include <KoColorSpace.h>
#include <KoCompositeOpRegistry.h>
#include <KoZoomAction.h>
#include <brushengine/kis_paintop_preset.h>
#include <kis_image.h>

#include "KisDocument.h"
#include "KisMainWindow.h"
#include "KisView.h"
#include "KisViewManager.h"
#include "kis_canvas_resource_provider.h"
#include "kis_zoom_manager.h"

#include "IkAdjustmentsPanel.h"
#include "IkBrushLibrary.h"
#include "IkColorPanel.h"
#include "IkInterfaceManager.h"
#include "IkLayersPanel.h"
#include "IkStrokePreview.h"
#include "IkStyle.h"
#include "IkWidgets.h"

namespace
{
QString depthName(const KoColorSpace *cs)
{
    if (!cs) return QString();
    const QString d = cs->colorDepthId().id();
    if (d == QLatin1String("U8")) return QStringLiteral("8-bit");
    if (d == QLatin1String("U16")) return QStringLiteral("16-bit");
    if (d == QLatin1String("F16")) return QStringLiteral("16-bit float");
    if (d == QLatin1String("F32")) return QStringLiteral("32-bit float");
    return d;
}

/// Clickable label (zoom read-out, brush chip).
class IkClickLabel : public QLabel
{
public:
    using QLabel::QLabel;
    std::function<void()> onClick;

protected:
    void mouseReleaseEvent(QMouseEvent *) override { if (onClick) onClick(); }
};
}

IkDesktopLayout::IkDesktopLayout(IkInterfaceManager *mgr)
    : IkLayout(mgr)
{
    const int ts = IkStyle::touchSize();
    // The brush chip's stroke is rendered in the background the first time.
    connect(IkStrokePreview::instance(), &IkStrokePreview::previewReady, this, [this]() {
        if (isVisible()) updateBrushChip();
    });

    // ---- top bar ---------------------------------------------------------
    m_top = panel();
    m_top->setRole(IkStyle::Background);
    m_top->setRadius(0);
    QHBoxLayout *top = new QHBoxLayout(m_top);
    top->setContentsMargins(10, 4, 14, 4);
    top->setSpacing(6);
    IkIconButton *menu = button(m_top, QStringLiteral("menu"), i18n("Menu"));
    connect(menu, &QAbstractButton::clicked, this, [this, menu]() { showMoreMenu(menu); });
    top->addWidget(menu);
    top->addSpacing(10);
    QVBoxLayout *titles = new QVBoxLayout();
    titles->setSpacing(0);
    m_title = new QLabel(m_top);
    m_title->setFont(IkStyle::font(1.1, QFont::Bold));
    m_subtitle = new QLabel(m_top);
    m_subtitle->setFont(IkStyle::font(0.85));
    m_subtitle->setForegroundRole(QPalette::PlaceholderText);
    titles->addWidget(m_title);
    titles->addWidget(m_subtitle);
    top->addLayout(titles);
    top->addStretch(1);
    QHBoxLayout *topBox = new QHBoxLayout();
    topBox->setSpacing(6);
    top->addLayout(topBox);
    topBox->addWidget(actionButton(m_top, QStringLiteral("undo"), i18n("Undo"), QStringLiteral("edit_undo")));
    topBox->addWidget(actionButton(m_top, QStringLiteral("redo"), i18n("Redo"), QStringLiteral("edit_redo")));
    topBox->addWidget(symmetryButton(m_top));
    topBox->addWidget(toolButton(m_top, QStringLiteral("reference"), i18n("Reference images"), QStringLiteral("ToolReferenceImages")));
    top->addWidget(new IkSeparatorLine(m_top));
    auto barStyle = [](IkIconButton *b) {
        b->setVariant(IkIconButton::Ghost);
        b->setEdge(-1);
        b->setIconPixelSize(-1);
    };
    addDock(QStringLiteral("top"), m_top, topBox, barStyle);
    IkTextButton *zoom = new IkTextButton(QStringLiteral("100%"), IkTextButton::Filled, m_top);
    zoom->setRadius(10);
    zoom->setFontScale(1.0, QFont::DemiBold);
    zoom->setMinimumSize(ts + 34, ts - 4);
    zoom->setToolTip(i18n("Zoom"));
    connect(zoom, &QAbstractButton::clicked, this, [this, zoom]() {
        QMenu m(zoom);
        for (const auto &a : QList<QPair<QString, QString>>{{i18n("Fit Page"), QStringLiteral("zoom_to_fit")},
                                                           {i18n("Fit Width"), QStringLiteral("zoom_to_fit_width")},
                                                           {i18n("Actual Pixels (100%)"), QStringLiteral("zoom_to_100pct")},
                                                           {i18n("Zoom In"), QStringLiteral("view_zoom_in")},
                                                           {i18n("Zoom Out"), QStringLiteral("view_zoom_out")},
                                                           {i18n("Reset Rotation"), QStringLiteral("reset_canvas_rotation")}}) {
            if (QAction *act = manager()->action(a.second)) {
                QAction *item = m.addAction(a.first);
                connect(item, &QAction::triggered, act, &QAction::trigger);
            }
        }
        m.exec(zoom->mapToGlobal(QPoint(0, zoom->height() + 4)));
    });
    m_zoom = zoom;
    top->addWidget(zoom);
    IkIconButton *exportButton = button(m_top, QStringLiteral("export"), i18n("Save and export"));
    connect(exportButton, &QAbstractButton::clicked, this, [this, exportButton]() {
        QMenu m(exportButton);
        for (const auto &a : QList<QPair<QString, QString>>{{i18n("Save"), QStringLiteral("file_save")},
                                                           {i18n("Save As..."), QStringLiteral("file_save_as")},
                                                           {i18n("Export..."), QStringLiteral("file_export_file")},
                                                           {i18n("Export as PDF..."), QStringLiteral("file_export_pdf")}}) {
            if (QAction *act = manager()->action(a.second)) {
                QAction *item = m.addAction(a.first);
                connect(item, &QAction::triggered, act, &QAction::trigger);
            }
        }
        m.exec(exportButton->mapToGlobal(QPoint(0, exportButton->height() + 4)));
    });
    top->addWidget(exportButton);

    // ---- tool rail -----------------------------------------------------------
    m_rail = panel();
    m_rail->setRole(IkStyle::Background);
    m_rail->setRadius(0);
    QVBoxLayout *rail = new QVBoxLayout(m_rail);
    rail->setContentsMargins(8, 12, 8, 12);
    rail->setSpacing(4);
    QVBoxLayout *railBox = new QVBoxLayout();
    railBox->setSpacing(4);
    rail->addLayout(railBox);
    auto railStyle = [ts](IkIconButton *b) {
        b->setVariant(IkIconButton::Ghost);
        b->setEdge(ts + 8);
        b->setIconPixelSize(-1);
    };
    auto railButton = [&](IkIconButton *b) {
        railStyle(b);
        railBox->addWidget(b, 0, Qt::AlignHCenter);
        return b;
    };
    railButton(toolButton(m_rail, QStringLiteral("move"), i18n("Move"), QStringLiteral("KritaTransform/KisToolMove")));
    IkIconButton *brush = railButton(button(m_rail, QStringLiteral("brush"), i18n("Brush (tap again for the brush library)")));
    connect(brush, &QAbstractButton::clicked, this, [this]() {
        const bool already = manager()->paintMode() == IkInterfaceManager::Draw && manager()->isBrushToolActive();
        manager()->setPaintMode(IkInterfaceManager::Draw);
        manager()->ensureBrushTool();
        setBrushLibraryOpen(already ? !m_libraryOpen : true);
    });
    m_drawButtons << brush;
    IkIconButton *eraser = railButton(button(m_rail, QStringLiteral("eraser"), i18n("Erase with the current brush")));
    connect(eraser, &QAbstractButton::clicked, this, [this]() {
        manager()->setPaintMode(manager()->paintMode() == IkInterfaceManager::Erase ? IkInterfaceManager::Draw
                                                                                     : IkInterfaceManager::Erase);
    });
    m_eraseButtons << eraser;
    IkIconButton *smudge = railButton(button(m_rail, QStringLiteral("smudge"), i18n("Smudge")));
    connect(smudge, &QAbstractButton::clicked, this, [this]() { manager()->setPaintMode(IkInterfaceManager::Smudge); });
    m_smudgeButtons << smudge;
    railButton(toolButton(m_rail, QStringLiteral("fill"), i18n("Fill"), QStringLiteral("KritaFill/KisToolFill")));
    railButton(toolButton(m_rail, QStringLiteral("select-rect"), i18n("Rectangle selection"), QStringLiteral("KisToolSelectRectangular")));
    railButton(toolButton(m_rail, QStringLiteral("select-magic"), i18n("Magic wand selection"), QStringLiteral("KisToolSelectContiguous")));
    railButton(toolButton(m_rail, QStringLiteral("select-shape"), i18n("Select shapes"), QStringLiteral("InteractionTool")));
    railButton(toolButton(m_rail, QStringLiteral("pipette"), i18n("Sample colour"), QStringLiteral("KritaSelected/KisToolColorSampler")));
    IkIconButton *shapes = railButton(button(m_rail, QStringLiteral("shapes"), i18n("All tools")));
    connect(shapes, &QAbstractButton::clicked, this, [this, shapes]() { showToolsPopover(shapes, IkPopover::Right); });
    railButton(toolButton(m_rail, QStringLiteral("text"), i18n("Text"), QStringLiteral("SvgTextTool")));
    railButton(toolButton(m_rail, QStringLiteral("zoom"), i18n("Zoom"), QStringLiteral("ZoomTool")));
    railButton(toolButton(m_rail, QStringLiteral("pan"), i18n("Pan"), QStringLiteral("PanTool")));
    addDock(QStringLiteral("rail"), m_rail, railBox, railStyle, Qt::AlignHCenter);
    rail->addStretch(1);
    QWidget *chips = new QWidget(m_rail);
    chips->setFixedSize(ts + 8, ts + 8);
    m_bg = new IkSwatch(chips);
    m_bg->setShape(true);
    m_bg->setGeometry(16, 16, ts - 8, ts - 8);
    m_fg = new IkSwatch(chips);
    m_fg->setShape(true);
    m_fg->setGeometry(0, 0, ts - 6, ts - 6);
    m_fg->setToolTip(i18n("Colour"));
    connect(m_fg, &QAbstractButton::clicked, this, [this]() { setRightTab(0); });
    connect(m_bg, &QAbstractButton::clicked, this, [this]() { manager()->triggerAction(QStringLiteral("toggle_fg_bg")); });
    rail->addWidget(chips, 0, Qt::AlignHCenter);

    // ---- right panel -----------------------------------------------------------
    m_right = panel();
    m_right->setRole(IkStyle::Surface);
    m_right->setRadius(0);
    QVBoxLayout *right = new QVBoxLayout(m_right);
    right->setContentsMargins(10, 10, 10, 6);
    right->setSpacing(6);
    IkTabBar *tabs = new IkTabBar(m_right);
    tabs->setStyle(IkTabBar::Pills);
    tabs->addTab(QString(), i18n("Color"));
    tabs->addTab(QString(), i18n("Adjustments"));
    tabs->addTab(QString(), i18n("Options"));
    right->addWidget(tabs);
    m_rightStack = new QStackedWidget(m_right);
    IkColorPanel *color = new IkColorPanel(mgr, m_rightStack);
    m_rightStack->addWidget(color);
    m_rightStack->addWidget(new IkAdjustmentsPanel(mgr, m_rightStack));
    m_optionsHost = new QWidget(m_rightStack);
    QVBoxLayout *ol = new QVBoxLayout(m_optionsHost);
    ol->setContentsMargins(0, 0, 0, 0);
    m_rightStack->addWidget(m_optionsHost);
    right->addWidget(m_rightStack, 46);
    right->addWidget(new IkDivider(m_right));
    IkLayersPanel *layers = new IkLayersPanel(mgr, m_right);
    right->addWidget(layers, 54);
    connect(tabs, &IkTabBar::currentChanged, this, [this](int i) { setRightTab(i); });
    m_right->setProperty("ikTabs", QVariant::fromValue<QObject *>(tabs));
    m_right->setProperty("ikColor", QVariant::fromValue<QObject *>(color));
    m_right->setProperty("ikLayers", QVariant::fromValue<QObject *>(layers));

    // ---- bottom brush bar --------------------------------------------------------
    m_bottom = panel();
    m_bottom->setRole(IkStyle::Background);
    m_bottom->setRadius(0);
    QHBoxLayout *bottom = new QHBoxLayout(m_bottom);
    bottom->setContentsMargins(16, 6, 16, 6);
    bottom->setSpacing(14);
    IkPanel *chip = new IkPanel(m_bottom);
    chip->setRole(IkStyle::Raised);
    chip->setRadius(10);
    chip->setCursor(Qt::PointingHandCursor);
    QHBoxLayout *cl = new QHBoxLayout(chip);
    cl->setContentsMargins(12, 2, 16, 2);
    cl->setSpacing(10);
    IkClickLabel *stroke = new IkClickLabel(chip);
    stroke->setFixedSize(IkStyle::scaled(64), ts - 12);
    stroke->onClick = [this]() { setBrushLibraryOpen(!m_libraryOpen); };
    m_brushStroke = stroke;
    IkClickLabel *name = new IkClickLabel(chip);
    name->setFont(IkStyle::font(1.0, QFont::DemiBold));
    name->onClick = [this]() { setBrushLibraryOpen(!m_libraryOpen); };
    m_brushName = name;
    cl->addWidget(stroke);
    cl->addWidget(name);
    bottom->addWidget(chip);
    bottom->addSpacing(10);
    auto sliderBlock = [&](const QString &label, bool isSize) {
        QLabel *l = new QLabel(label, m_bottom);
        l->setForegroundRole(QPalette::PlaceholderText);
        bottom->addWidget(l);
        IkSlider *s = new IkSlider(m_bottom);
        s->setMinimumWidth(IkStyle::scaled(120));
        bottom->addWidget(s, 1);
        QLabel *v = new QLabel(m_bottom);
        v->setMinimumWidth(IkStyle::scaled(56));
        v->setFont(IkStyle::font(1.0, QFont::DemiBold));
        bottom->addWidget(v);
        if (isSize) bindSize(s, v);
        else bindOpacity(s, v);
    };
    sliderBlock(i18n("Size"), true);
    bottom->addSpacing(10);
    sliderBlock(i18n("Opacity"), false);
    bottom->addSpacing(10);
    IkChoiceRow *blend = new IkChoiceRow(i18n("Normal"), m_bottom);
    blend->setCentered(true);
    blend->setFixedWidth(IkStyle::scaled(130));
    const QStringList ops = {COMPOSITE_OVER, COMPOSITE_MULT, COMPOSITE_SCREEN, COMPOSITE_OVERLAY, COMPOSITE_SOFT_LIGHT_PHOTOSHOP,
                             COMPOSITE_DARKEN, COMPOSITE_LIGHTEN, COMPOSITE_DODGE, COMPOSITE_BURN, COMPOSITE_ADD, COMPOSITE_COLOR,
                             COMPOSITE_BEHIND};
    QStringList opNames;
    for (const QString &op : ops) opNames << KoCompositeOpRegistry::instance().getKoID(op).name();
    blend->setChoices(opNames, 0);
    connect(blend, &IkChoiceRow::chosen, this, [this, ops](int i) {
        if (KisViewManager *vm = this->manager()->viewManager()) vm->canvasResourceProvider()->setCurrentCompositeOp(ops.value(i));
    });
    m_bottom->setProperty("ikBlend", QVariant::fromValue<QObject *>(blend));
    m_bottom->setProperty("ikBlendOps", ops);
    bottom->addWidget(blend);
    QHBoxLayout *bottomBox = new QHBoxLayout();
    bottomBox->setSpacing(6);
    bottom->addLayout(bottomBox);
    addDock(QStringLiteral("bottom"), m_bottom, bottomBox, barStyle);

    // ---- brush library (slides out next to the rail) ----------------------------
    m_library = panel();
    m_library->setRole(IkStyle::Surface);
    m_library->setShadow(true);
    m_library->setRadius(18);
    m_library->setProperty("ikHidden", true);
    QVBoxLayout *ll = new QVBoxLayout(m_library);
    ll->setContentsMargins(14, 10, 14, 18);
    m_libraryWidget = new IkBrushLibrary(mgr, m_library);
    ll->addWidget(m_libraryWidget);
    connect(m_libraryWidget, &IkBrushLibrary::presetActivated, this, [this]() { refresh(); });

    finishDocks();
}

void IkDesktopLayout::setVisible(bool visible)
{
    if (!visible) {
        manager()->releaseToolOptions();
    }
    IkLayout::setVisible(visible);
    if (visible && m_rightTab == 2) manager()->takeToolOptions(m_optionsHost);
}

QMargins IkDesktopLayout::reservedMargins() const
{
    const int ts = IkStyle::touchSize();
    const int top = ts + 16;
    const int rail = ts + 24;
    const int bottom = ts + 16;
    const int right = m_area.width() > 0 ? qBound(300, int(m_area.width() * 0.25), 420) : 360;
    return QMargins(rail, top, right, bottom);
}

void IkDesktopLayout::layoutPanels(const QRect &areaIn)
{
    m_area = areaIn;
    const QMargins m = reservedMargins();
    const QRect a = areaIn;
    m_top->setGeometry(a.left(), a.top(), a.width(), m.top());
    m_rail->setGeometry(a.left(), a.top() + m.top(), m.left(), a.height() - m.top());
    m_right->setGeometry(a.right() - m.right() + 1, a.top() + m.top(), m.right(), a.height() - m.top());
    m_bottom->setGeometry(a.left() + m.left(), a.bottom() - m.bottom() + 1, a.width() - m.left() - m.right(), m.bottom());
    const int lw = qMin(580, qMax(440, int(a.width() * 0.34)));
    m_library->setGeometry(a.left() + m.left() + 2, a.top() + m.top() + 2, lw, a.height() - m.top() - m.bottom() - 4);
    for (QWidget *w : {static_cast<QWidget *>(m_top), static_cast<QWidget *>(m_rail), static_cast<QWidget *>(m_right),
                       static_cast<QWidget *>(m_bottom), static_cast<QWidget *>(m_library)}) {
        w->raise();
    }    raisePopovers();
}

void IkDesktopLayout::closeTransient()
{
    IkLayout::closeTransient();
    if (m_libraryOpen) setBrushLibraryOpen(false);
}

void IkDesktopLayout::setBrushLibraryOpen(bool open)
{
    m_libraryOpen = open;
    m_library->setProperty("ikHidden", !open);
    m_library->setVisible(open && isVisible());
    if (open) {
        m_libraryWidget->reload();
        m_library->raise();
        raisePopovers();
    }
}

void IkDesktopLayout::setRightTab(int tab)
{
    if (QObject *t = m_right->property("ikTabs").value<QObject *>()) {
        IkTabBar *tabs = static_cast<IkTabBar *>(t);
        if (tabs->currentIndex() != tab) {
            QSignalBlocker b(tabs);
            tabs->setCurrentIndex(tab);
        }
    }
    if (m_rightTab == 2 && tab != 2) manager()->releaseToolOptions();
    m_rightTab = tab;
    m_rightStack->setCurrentIndex(tab);
    if (tab == 2) manager()->takeToolOptions(m_optionsHost);
}

void IkDesktopLayout::updateBrushChip()
{
    KisViewManager *vm = manager()->viewManager();
    if (!vm || !vm->canvasResourceProvider()) return;
    KisPaintOpPresetSP preset = vm->canvasResourceProvider()->currentPreset();
    if (!preset) return;
    QString n = preset->name();
    if (n.size() > 3 && n.at(1) == QLatin1Char(')')) n = n.mid(3);
    m_brushName->setText(n.replace(QLatin1Char('_'), QLatin1Char(' ')));
    const qreal dpr = m_brushStroke->devicePixelRatioF();
    const QImage mask = IkStrokePreview::instance()->preview(preset, m_brushStroke->size() * dpr);
    if (mask.isNull()) {
        m_brushStroke->clear();
        return;
    }
    QImage img = IkStrokePreview::tinted(mask, IkStyle::color(IkStyle::Text));
    img.setDevicePixelRatio(dpr);
    m_brushStroke->setPixmap(QPixmap::fromImage(img));
}

void IkDesktopLayout::refresh()
{
    IkLayout::refresh();
    if (QObject *c = m_right->property("ikColor").value<QObject *>()) static_cast<IkColorPanel *>(c)->refresh();
    if (QObject *l = m_right->property("ikLayers").value<QObject *>()) static_cast<IkLayersPanel *>(l)->refresh();

    KisViewManager *vm = manager()->viewManager();
    if (vm && vm->canvasResourceProvider()) {
        const QColor fg = vm->canvasResourceProvider()->fgColor().toQColor();
        const QColor bg = vm->canvasResourceProvider()->bgColor().toQColor();
        m_fg->setColor(fg);
        m_bg->setColor(bg);
        updateBrushChip();
        if (QObject *b = m_bottom->property("ikBlend").value<QObject *>()) {
            const QStringList ops = m_bottom->property("ikBlendOps").toStringList();
            const int idx = ops.indexOf(vm->canvasResourceProvider()->currentCompositeOp());
            IkChoiceRow *blend = static_cast<IkChoiceRow *>(b);
            if (idx >= 0) blend->setCurrentIndex(idx);
            else blend->setValueText(KoCompositeOpRegistry::instance().getKoID(vm->canvasResourceProvider()->currentCompositeOp()).name());
        }
    }
}

void IkDesktopLayout::refreshTools()
{
    IkLayout::refreshTools();
    if (m_libraryOpen && !manager()->isBrushToolActive()) setBrushLibraryOpen(false);
}

void IkDesktopLayout::refreshDocument()
{
    KisMainWindow *mw = manager()->mainWindow();
    KisView *view = mw ? mw->activeView() : nullptr;
    KisDocument *doc = view ? view->document() : nullptr;
    if (!doc) {
        m_title->setText(QString());
        m_subtitle->setText(QString());
        return;
    }
    const QString path = doc->path();
    m_title->setText(path.isEmpty() ? i18n("Untitled") : QFileInfo(path).fileName());
    if (KisImageSP image = doc->image()) {
        m_subtitle->setText(QStringLiteral("%1 × %2 px · %3 dpi · %4 %5")
                                .arg(image->width())
                                .arg(image->height())
                                .arg(qRound(image->xRes() * 72.0))
                                .arg(image->colorSpace()->colorModelId().id(), depthName(image->colorSpace())));
    }
    {
        if (view->zoomManager() && view->zoomManager()->zoomAction()) {
            KoZoomAction *za = view->zoomManager()->zoomAction();
            disconnect(za, &KoZoomAction::sigInternalUpdateZoomLevelsComboState, m_zoom, nullptr);
            connect(za, &KoZoomAction::sigInternalUpdateZoomLevelsComboState, m_zoom,
                    [this](const QStringList &, int, const QString &text) {
                        // "Fit View (24.0%)" -> "24%"
                        QString t = text;
                        static const QRegularExpression pct(QStringLiteral("([0-9]+(?:[.,][0-9]+)?)\\s*%"));
                        const QRegularExpressionMatch m = pct.match(text);
                        if (m.hasMatch()) {
                            QString n = m.captured(1);
                            n.replace(QLatin1Char(','), QLatin1Char('.'));
                            t = QStringLiteral("%1%").arg(qRound(n.toDouble()));
                        }
                        m_zoom->setText(t);
                        m_zoom->updateGeometry();
                    });
        }
    }
}
