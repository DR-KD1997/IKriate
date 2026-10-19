/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "IkAdjustmentsPanel.h"

#include <QScrollArea>
#include <QScroller>
#include <QVBoxLayout>

#include <klocalizedstring.h>

#include "IkInterfaceManager.h"
#include "IkStyle.h"
#include "IkWidgets.h"

IkAdjustmentsPanel::IkAdjustmentsPanel(IkInterfaceManager *manager, QWidget *parent)
    : QWidget(parent)
{
    QVBoxLayout *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    QScrollArea *scroll = new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setAutoFillBackground(false);
    QScroller::grabGesture(scroll->viewport(), IkStyle::touchFirst() ? QScroller::LeftMouseButtonGesture : QScroller::TouchGesture);
    outer->addWidget(scroll);

    QWidget *list = new QWidget(scroll);
    QVBoxLayout *l = new QVBoxLayout(list);
    l->setContentsMargins(10, 6, 10, 6);
    l->setSpacing(2);

    struct Item { QString icon; QString text; QString action; };
    const QList<Item> groups[2] = {
        {
            {QStringLiteral("sliders"), i18n("Levels"), QStringLiteral("krita_filter_levels")},
            {QStringLiteral("sliders"), i18n("Curves"), QStringLiteral("krita_filter_perchannel")},
            {QStringLiteral("palette"), i18n("Hue / Saturation / Brightness"), QStringLiteral("krita_filter_hsvadjustment")},
            {QStringLiteral("palette"), i18n("Color Balance"), QStringLiteral("krita_filter_colorbalance")},
            {QStringLiteral("system"), i18n("Desaturate"), QStringLiteral("krita_filter_desaturate")},
            {QStringLiteral("system"), i18n("Invert"), QStringLiteral("krita_filter_invert")},
            {QStringLiteral("palette"), i18n("Gradient Map"), QStringLiteral("krita_filter_gradientmap")},
        },
        {
            {QStringLiteral("watercolor"), i18n("Gaussian Blur"), QStringLiteral("krita_filter_gaussian blur")},
            {QStringLiteral("watercolor"), i18n("Motion Blur"), QStringLiteral("krita_filter_motion blur")},
            {QStringLiteral("effects"), i18n("Sharpen (Unsharp Mask)"), QStringLiteral("krita_filter_unsharp")},
            {QStringLiteral("texture"), i18n("Noise"), QStringLiteral("krita_filter_noise")},
            {QStringLiteral("pixel"), i18n("Pixelize"), QStringLiteral("krita_filter_pixelize")},
            {QStringLiteral("effects"), i18n("Halftone"), QStringLiteral("krita_filter_halftone")},
        },
    };
    const QString titles[2] = {i18n("Colour"), i18n("Effects")};
    for (int g = 0; g < 2; ++g) {
        IkSectionHeader *h = new IkSectionHeader(titles[g], list);
        l->addWidget(h);
        for (const Item &it : groups[g]) {
            IkNavRow *row = new IkNavRow(it.icon, it.text, list);
            const QString action = it.action;
            connect(row, &QAbstractButton::clicked, this, [this, manager, action]() {
                Q_EMIT adjustmentStarted();
                manager->triggerAction(action);
            });
            row->setEnabled(manager->action(action) != nullptr);
            l->addWidget(row);
        }
        l->addSpacing(8);
    }
    IkNavRow *all = new IkNavRow(QStringLiteral("more"), i18n("All Filters..."), list);
    connect(all, &QAbstractButton::clicked, this, [this, manager, all]() {
        Q_EMIT adjustmentStarted();
        manager->showNamedMenu(QStringLiteral("Filter"), all);
    });
    l->addWidget(all);
    l->addStretch(1);
    scroll->setWidget(list);
}

QSize IkAdjustmentsPanel::sizeHint() const
{
    return QSize(340, 520);
}
