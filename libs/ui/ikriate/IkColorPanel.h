/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef IKCOLORPANEL_H
#define IKCOLORPANEL_H

#include <QWidget>

#include <KoColor.h>

#include "kritaui_export.h"

class IkInterfaceManager;

/**
 * Colour panel: foreground/background chips, hue/saturation/brightness
 * sliders with values, a colour ring with triangle, and recent colours.
 * Edits the canvas foreground colour.
 */
class KRITAUI_EXPORT IkColorPanel : public QWidget
{
    Q_OBJECT
public:
    explicit IkColorPanel(IkInterfaceManager *manager, QWidget *parent = nullptr);
    ~IkColorPanel() override;

    /// Hide the slider block (compact popover).
    void setShowSliders(bool show);
    QSize sizeHint() const override;

public Q_SLOTS:
    void refresh();

private:
    void connectToCanvas();
    void setForeground(const KoColor &color);
    void updateFromColor(const KoColor &color);
    void rebuildSwatches();

    struct Private;
    Private *d;
};

#endif // IKCOLORPANEL_H
