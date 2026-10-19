/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef IKADJUSTMENTSPANEL_H
#define IKADJUSTMENTSPANEL_H

#include <QWidget>

#include "kritaui_export.h"

class IkInterfaceManager;

/// The most used image adjustments and filters as a tappable list; the
/// complete set stays reachable through "All Filters".
class KRITAUI_EXPORT IkAdjustmentsPanel : public QWidget
{
    Q_OBJECT
public:
    explicit IkAdjustmentsPanel(IkInterfaceManager *manager, QWidget *parent = nullptr);
    QSize sizeHint() const override;

Q_SIGNALS:
    void adjustmentStarted();
};

#endif // IKADJUSTMENTSPANEL_H
