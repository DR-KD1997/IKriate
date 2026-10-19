/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "IkLayouts.h"

#include <QHBoxLayout>
#include <QVBoxLayout>

#include <klocalizedstring.h>

#include "IkInterfaceManager.h"
#include "IkStyle.h"
#include "IkWidgets.h"

IkMinimalLayout::IkMinimalLayout(IkInterfaceManager *mgr)
    : IkLayout(mgr)
{
    const int ts = IkStyle::touchSize();
    const int round = ts + 26;

    // ---- home (top-left) ----------------------------------------------------
    m_home = panel(true);
    m_home->setRole(IkStyle::Surface);
    QHBoxLayout *hl = new QHBoxLayout(m_home);
    hl->setContentsMargins(0, 0, 0, 0);
    IkIconButton *home = button(m_home, QStringLiteral("home"), i18n("Gallery"));
    home->setEdge(round);
    home->setIconPixelSize(qRound(ts * 0.66));
    connect(home, &QAbstractButton::clicked, mgr, &IkInterfaceManager::goHome);
    hl->addWidget(home);

    // ---- floating tool strip (top-centre) ---------------------------------------
    m_tools = panel();
    m_tools->setRole(IkStyle::Surface);
    m_tools->setRadius(8);
    m_tools->setRoundedCorners(IkPanel::BottomLeft | IkPanel::BottomRight);
    QHBoxLayout *tl = new QHBoxLayout(m_tools);
    tl->setContentsMargins(14, 4, 14, 6);
    tl->setSpacing(14);
    auto toolsAdd = [&](IkIconButton *b) {
        b->setEdge(ts + 6);
        b->setIconPixelSize(qRound(ts * 0.6));
        tl->addWidget(b);
        return b;
    };
    IkIconButton *colors = toolsAdd(button(m_tools, QStringLiteral("colors"), i18n("Colour")));
    colors->setKeepIconColors(true);
    connect(colors, &QAbstractButton::clicked, this, [this, colors]() { toggleColorPopover(colors, IkPopover::Below); });
    IkIconButton *select = toolsAdd(button(m_tools, QStringLiteral("select-rect"), i18n("Selection")));
    connect(select, &QAbstractButton::clicked, this, [this, select]() { showSelectionPopover(select, IkPopover::Below); });
    m_toolButtons << qMakePair(QPointer<IkIconButton>(select), QStringLiteral("KisToolSelectRectangular"));
    toolsAdd(toolButton(m_tools, QStringLiteral("transform"), i18n("Transform"), QStringLiteral("KisToolTransform")));
    toolsAdd(toolButton(m_tools, QStringLiteral("fill"), i18n("Fill"), QStringLiteral("KritaFill/KisToolFill")));
    IkIconButton *smudge = toolsAdd(button(m_tools, QStringLiteral("smudge"), i18n("Smudge")));
    connect(smudge, &QAbstractButton::clicked, this, [this]() { this->manager()->setPaintMode(IkInterfaceManager::Smudge); });
    m_smudgeButtons << smudge;
    toolsAdd(toolButton(m_tools, QStringLiteral("zoom"), i18n("Zoom"), QStringLiteral("ZoomTool")));
    IkIconButton *all = toolsAdd(button(m_tools, QStringLiteral("tools"), i18n("All tools")));
    connect(all, &QAbstractButton::clicked, this, [this, all]() { showToolsPopover(all, IkPopover::Below); });

    // ---- top-right: assistants + brush settings, layers, more ----------------
    m_right = panel(true);
    m_right->setRole(IkStyle::Surface);
    QHBoxLayout *rl = new QHBoxLayout(m_right);
    rl->setContentsMargins(14, 0, 14, 0);
    rl->setSpacing(10);
    IkIconButton *assist = button(m_right, QStringLiteral("assistant"), i18n("Drawing assistants"));
    assist->setEdge(ts + 6);
    connect(assist, &QAbstractButton::clicked, this, [this]() { this->manager()->activateTool(QStringLiteral("KisAssistantTool")); });
    m_toolButtons << qMakePair(QPointer<IkIconButton>(assist), QStringLiteral("KisAssistantTool"));
    rl->addWidget(assist);
    IkIconButton *settings = button(m_right, QStringLiteral("brush-settings"), i18n("Brush and tool settings"));
    settings->setEdge(ts + 6);
    connect(settings, &QAbstractButton::clicked, this, [this, settings]() {
        if (this->manager()->isBrushToolActive()) this->manager()->showBrushEditor();
        else showToolOptionsPopover(settings, IkPopover::Below);
    });
    rl->addWidget(settings);

    m_layers = panel(true);
    m_layers->setRole(IkStyle::Surface);
    QHBoxLayout *ll = new QHBoxLayout(m_layers);
    ll->setContentsMargins(0, 0, 0, 0);
    IkIconButton *layers = button(m_layers, QStringLiteral("layers"), i18n("Layers"));
    layers->setEdge(round);
    layers->setIconPixelSize(qRound(ts * 0.66));
    connect(layers, &QAbstractButton::clicked, this, [this, layers]() { toggleLayersPopover(layers, IkPopover::Below); });
    ll->addWidget(layers);

    m_more = panel(true);
    m_more->setRole(IkStyle::Surface);
    QHBoxLayout *ml = new QHBoxLayout(m_more);
    ml->setContentsMargins(0, 0, 0, 0);
    IkIconButton *more = button(m_more, QStringLiteral("more"), i18n("More"));
    more->setEdge(round);
    more->setIconPixelSize(qRound(ts * 0.66));
    connect(more, &QAbstractButton::clicked, this, [this, more]() { showMoreMenu(more); });
    ml->addWidget(more);

    // ---- left: size / colour / opacity --------------------------------------------
    m_side = panel();
    m_side->setRole(IkStyle::Surface);
    m_side->setRadius(10);
    QVBoxLayout *sl = new QVBoxLayout(m_side);
    sl->setContentsMargins(6, 10, 6, 10);
    sl->setSpacing(10);
    IkPillSlider *size = new IkPillSlider(Qt::Vertical, m_side);
    bindSize(size);
    sl->addWidget(size, 1, Qt::AlignHCenter);
    m_color = new IkSwatch(m_side);
    m_color->setDiameter(ts - 8);
    m_color->setCurrent(true);
    m_color->setToolTip(i18n("Colour"));
    connect(m_color, &QAbstractButton::clicked, this, [this]() { toggleColorPopover(m_color, IkPopover::Right); });
    bindColorSwatch(m_color);
    sl->addWidget(m_color, 0, Qt::AlignHCenter);
    IkPillSlider *opacity = new IkPillSlider(Qt::Vertical, m_side);
    bindOpacity(opacity);
    sl->addWidget(opacity, 1, Qt::AlignHCenter);

    // Brush and eraser beside the strip.
    m_modes = panel();
    m_modes->setPaintBackground(false);
    QVBoxLayout *mo = new QVBoxLayout(m_modes);
    mo->setContentsMargins(0, 0, 0, 0);
    mo->setSpacing(round);
    IkIconButton *brush = button(m_modes, QStringLiteral("brush"), i18n("Paint (tap again for brushes)"));
    brush->setVariant(IkIconButton::Round);
    brush->setEdge(round);
    connect(brush, &QAbstractButton::clicked, this, [this, brush]() {
        const bool already = this->manager()->paintMode() == IkInterfaceManager::Draw && this->manager()->isBrushToolActive();
        this->manager()->setPaintMode(IkInterfaceManager::Draw);
        this->manager()->ensureBrushTool();
        if (already) toggleBrushPopover(brush, IkPopover::Right);
    });
    m_drawButtons << brush;
    mo->addWidget(brush);
    IkIconButton *eraser = button(m_modes, QStringLiteral("eraser"), i18n("Erase (tap again for brushes)"));
    eraser->setVariant(IkIconButton::Round);
    eraser->setEdge(round);
    connect(eraser, &QAbstractButton::clicked, this, [this, eraser]() {
        const bool already = this->manager()->paintMode() == IkInterfaceManager::Erase;
        this->manager()->setPaintMode(IkInterfaceManager::Erase);
        this->manager()->ensureBrushTool();
        if (already) toggleBrushPopover(eraser, IkPopover::Right);
    });
    m_eraseButtons << eraser;
    mo->addWidget(eraser);

    // ---- undo / redo (bottom-left) -----------------------------------------------------
    m_history = panel(true);
    m_history->setRole(IkStyle::Surface);
    QHBoxLayout *hy = new QHBoxLayout(m_history);
    hy->setContentsMargins(8, 4, 8, 4);
    hy->setSpacing(4);
    for (const auto &a : QList<QPair<QString, QString>>{{QStringLiteral("undo"), QStringLiteral("edit_undo")},
                                                         {QStringLiteral("redo"), QStringLiteral("edit_redo")}}) {
        IkIconButton *b = actionButton(m_history, a.first, a.first == QLatin1String("undo") ? i18n("Undo") : i18n("Redo"), a.second);
        b->setEdge(ts + 6);
        hy->addWidget(b);
    }
}

void IkMinimalLayout::layoutPanels(const QRect &a)
{
    const int ts = IkStyle::touchSize();
    const int round = ts + 26;
    const int m = 14;
    m_home->setGeometry(a.left() + m, a.top() + m, round, round);
    m_tools->adjustSize();
    const QSize tsz = m_tools->sizeHint();
    m_tools->setGeometry(a.left() + (a.width() - tsz.width()) / 2, a.top(), tsz.width(), tsz.height());
    m_more->setGeometry(a.right() - m - round + 1, a.top() + m, round, round);
    m_layers->setGeometry(m_more->x() - 14 - round, a.top() + m, round, round);
    const QSize rsz = m_right->sizeHint();
    m_right->setGeometry(m_layers->x() - 14 - rsz.width(), a.top() + m, rsz.width(), round);

    const int sideW = ts + 4;
    const int sideTop = a.top() + int(a.height() * 0.26);
    const int sideH = qMin(int(a.height() * 0.55), a.bottom() - sideTop - round - 3 * m);
    m_side->setGeometry(a.left() + m + 4, sideTop, sideW, sideH);
    const int modesH = 3 * round;
    m_modes->setGeometry(m_side->geometry().right() + 14, sideTop + (sideH - modesH) / 2, round, modesH);
    m_history->adjustSize();
    const QSize hsz = m_history->sizeHint();
    m_history->setGeometry(a.left() + m + 4, a.bottom() - m - hsz.height(), hsz.width(), hsz.height());
    for (QWidget *w : {static_cast<QWidget *>(m_home), static_cast<QWidget *>(m_tools), static_cast<QWidget *>(m_right),
                       static_cast<QWidget *>(m_layers), static_cast<QWidget *>(m_more), static_cast<QWidget *>(m_side),
                       static_cast<QWidget *>(m_modes), static_cast<QWidget *>(m_history)}) {
        w->raise();
    }
    raisePopovers();
}

void IkMinimalLayout::refresh()
{
    IkLayout::refresh();
}
