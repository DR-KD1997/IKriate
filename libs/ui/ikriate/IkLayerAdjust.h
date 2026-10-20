/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef IKLAYERADJUST_H
#define IKLAYERADJUST_H

#include <QWidget>

#include "kritaui_export.h"

class IkInterfaceManager;

/**
 * Quick colour adjustments for the selected layer: Hue / Saturation /
 * Brightness and Colour Balance (cyan-red, magenta-green, yellow-blue).
 *
 * Moving a slider previews the change on the canvas (Krita's filter preview
 * stroke); Apply, switching between the two adjustments or closing the panel
 * commits it as one undoable step; Reset discards it.
 */
class KRITAUI_EXPORT IkLayerAdjustPanel : public QWidget
{
    Q_OBJECT
public:
    explicit IkLayerAdjustPanel(IkInterfaceManager *manager, QWidget *parent = nullptr);
    ~IkLayerAdjustPanel() override;

public Q_SLOTS:
    /// Commits the previewed adjustment, if any.
    void commit();
    /// Discards the previewed adjustment and centres the sliders.
    void reset();

private:
    void preview();
    void setMode(int mode);

    struct Private;
    Private *d;
};

#endif // IKLAYERADJUST_H
