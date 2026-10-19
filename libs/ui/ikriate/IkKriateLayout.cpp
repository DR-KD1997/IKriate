/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "IkLayouts.h"

#include <QHBoxLayout>
#include <QVBoxLayout>

#include <KConfigGroup>
#include <KSharedConfig>
#include <klocalizedstring.h>

#include "IkBrushLibrary.h"
#include "IkInterfaceManager.h"
#include "IkStyle.h"
#include "IkWidgets.h"

IkKriateLayout::IkKriateLayout(IkInterfaceManager *mgr)
    : IkLayout(mgr)
{
    const int ts = IkStyle::touchSize();

    // ---- top bar -----------------------------------------------------------
    m_top = panel();
    m_top->setRole(IkStyle::Surface);
    m_top->setRadius(14);
    QHBoxLayout *top = new QHBoxLayout(m_top);
    top->setContentsMargins(10, 6, 10, 6);
    top->setSpacing(10);
    auto add = [&](IkIconButton *b) {
        b->setEdge(ts + 10);
        b->setIconPixelSize(qRound(ts * 0.62));
        top->addWidget(b);
        return b;
    };
    IkIconButton *home = add(button(m_top, QStringLiteral("home"), i18n("Gallery")));
    connect(home, &QAbstractButton::clicked, mgr, &IkInterfaceManager::goHome);
    IkIconButton *colors = add(button(m_top, QStringLiteral("colors"), i18n("Colour")));
    colors->setKeepIconColors(true);
    connect(colors, &QAbstractButton::clicked, this, [this, colors]() { toggleColorPopover(colors, IkPopover::Below); });
    IkIconButton *select = add(button(m_top, QStringLiteral("select-rect"), i18n("Selection")));
    connect(select, &QAbstractButton::clicked, this, [this, select]() { showSelectionPopover(select, IkPopover::Below); });
    m_toolButtons << qMakePair(QPointer<IkIconButton>(select), QStringLiteral("KisToolSelectRectangular"));
    add(toolButton(m_top, QStringLiteral("transform"), i18n("Transform"), QStringLiteral("KisToolTransform")));
    top->addStretch(1);

    IkIconButton *brush = add(button(m_top, QStringLiteral("brush"), i18n("Paint (tap again for brushes)")));
    connect(brush, &QAbstractButton::clicked, this, [this]() {
        const bool already = manager()->paintMode() == IkInterfaceManager::Draw && manager()->isBrushToolActive();
        manager()->setPaintMode(IkInterfaceManager::Draw);
        manager()->ensureBrushTool();
        setLibraryOpen(already ? !m_libraryOpen : true);
    });
    m_drawButtons << brush;
    IkIconButton *eraser = add(button(m_top, QStringLiteral("eraser"), i18n("Erase (tap again for brushes)")));
    connect(eraser, &QAbstractButton::clicked, this, [this]() {
        const bool already = manager()->paintMode() == IkInterfaceManager::Erase;
        manager()->setPaintMode(IkInterfaceManager::Erase);
        manager()->ensureBrushTool();
        if (already) setLibraryOpen(!m_libraryOpen);
    });
    m_eraseButtons << eraser;
    IkIconButton *smudge = add(button(m_top, QStringLiteral("smudge"), i18n("Smudge (tap again for brushes)")));
    connect(smudge, &QAbstractButton::clicked, this, [this]() {
        const bool already = manager()->paintMode() == IkInterfaceManager::Smudge;
        manager()->setPaintMode(IkInterfaceManager::Smudge);
        if (already) setLibraryOpen(!m_libraryOpen);
    });
    m_smudgeButtons << smudge;
    IkIconButton *tools = add(button(m_top, QStringLiteral("tools"), i18n("All tools")));
    connect(tools, &QAbstractButton::clicked, this, [this, tools]() { showToolsPopover(tools, IkPopover::Below); });
    IkIconButton *layers = add(button(m_top, QStringLiteral("layers"), i18n("Layers")));
    connect(layers, &QAbstractButton::clicked, this, [this, layers]() { toggleLayersPopover(layers, IkPopover::Below); });
    IkIconButton *more = add(button(m_top, QStringLiteral("more-vertical"), i18n("More")));
    connect(more, &QAbstractButton::clicked, this, [this, more]() { showMoreMenu(more); });

    // ---- left strip: size, colour, opacity, undo, redo ----------------------
    m_side = panel();
    m_side->setRole(IkStyle::Surface);
    m_side->setRadius(14);
    QVBoxLayout *side = new QVBoxLayout(m_side);
    side->setContentsMargins(8, 14, 8, 14);
    side->setSpacing(12);
    IkPillSlider *size = new IkPillSlider(Qt::Vertical, m_side);
    bindSize(size);
    side->addWidget(size, 3, Qt::AlignHCenter);
    m_color = new IkSwatch(m_side);
    m_color->setDiameter(ts - 6);
    m_color->setCurrent(true);
    m_color->setToolTip(i18n("Sample a colour from the canvas"));
    connect(m_color, &QAbstractButton::clicked, this, [this]() {
        manager()->activateTool(QStringLiteral("KritaSelected/KisToolColorSampler"));
    });
    bindColorSwatch(m_color);
    side->addWidget(m_color, 0, Qt::AlignHCenter);
    IkPillSlider *opacity = new IkPillSlider(Qt::Vertical, m_side);
    bindOpacity(opacity);
    side->addWidget(opacity, 3, Qt::AlignHCenter);
    side->addSpacing(6);
    for (const auto &a : QList<QPair<QString, QString>>{{QStringLiteral("undo"), QStringLiteral("edit_undo")},
                                                         {QStringLiteral("redo"), QStringLiteral("edit_redo")}}) {
        IkIconButton *b = actionButton(m_side, a.first, a.first == QLatin1String("undo") ? i18n("Undo") : i18n("Redo"), a.second);
        b->setEdge(ts);
        side->addWidget(b, 0, Qt::AlignHCenter);
    }

    // ---- docked brush library ----------------------------------------------
    m_library = panel();
    m_library->setRole(IkStyle::Surface);
    m_library->setRadius(16);
    QVBoxLayout *ll = new QVBoxLayout(m_library);
    ll->setContentsMargins(8, 8, 8, 12);
    m_libraryWidget = new IkBrushLibrary(mgr, m_library);
    ll->addWidget(m_libraryWidget);
    m_libraryOpen = KConfigGroup(KSharedConfig::openConfig(), "IKriate").readEntry("KriateBrushLibraryOpen", true);
    m_library->setProperty("ikHidden", !m_libraryOpen);
}

void IkKriateLayout::setVisible(bool visible)
{
    IkLayout::setVisible(visible);
    if (visible && m_libraryOpen) m_libraryWidget->reload();
}

void IkKriateLayout::setLibraryOpen(bool open)
{
    m_libraryOpen = open;
    KConfigGroup(KSharedConfig::openConfig(), "IKriate").writeEntry("KriateBrushLibraryOpen", open);
    m_library->setProperty("ikHidden", !open);
    m_library->setVisible(open && isVisible());
    if (open) m_libraryWidget->reload();
}

void IkKriateLayout::layoutPanels(const QRect &a)
{
    m_area = a;
    const int ts = IkStyle::touchSize();
    const int m = 10;
    const int topH = ts + 22;
    m_top->setGeometry(a.left() + m / 2, a.top() + m / 2, a.width() - m, topH);
    const int sideW = ts + 18;
    const int sideTop = a.top() + topH + qMax(16, int(a.height() * 0.1));
    const int sideH = qMin(int(a.height() * 0.72), a.bottom() - sideTop - 16);
    m_side->setGeometry(a.left() + m, sideTop, sideW, sideH);
    const int lw = qBound(360, int(a.width() * 0.27), 520);
    m_library->setGeometry(a.right() - lw - m + 1, a.top() + topH + m + 4, lw, a.height() - topH - 3 * m - 10);
    for (QWidget *w : {static_cast<QWidget *>(m_top), static_cast<QWidget *>(m_side), static_cast<QWidget *>(m_library)}) {
        w->raise();
    }
    raisePopovers();
}

void IkKriateLayout::refresh()
{
    IkLayout::refresh();
    if (m_libraryOpen) m_libraryWidget->reload();
}

void IkKriateLayout::refreshTools()
{
    IkLayout::refreshTools();
}
