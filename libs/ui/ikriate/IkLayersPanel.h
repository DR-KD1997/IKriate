/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef IKLAYERSPANEL_H
#define IKLAYERSPANEL_H

#include <QWidget>

#include <kis_types.h>

#include "kritaui_export.h"

class IkInterfaceManager;

/**
 * Layer list in the IKriate style: thumbnail, name, blending mode and
 * opacity, a menu per layer and visibility/lock toggles. Layer commands go
 * through Krita's own actions, so undo and every layer type keep working.
 */
class KRITAUI_EXPORT IkLayersPanel : public QWidget
{
    Q_OBJECT
public:
    explicit IkLayersPanel(IkInterfaceManager *manager, QWidget *parent = nullptr);
    ~IkLayersPanel() override;

    /// Show the "Layers" title row (off when the panel sits under a tab bar).
    void setShowHeader(bool show);
    /// Show the folder / effects / delete row at the bottom.
    void setShowFooter(bool show);
    QSize sizeHint() const override;

public Q_SLOTS:
    void refresh();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void connectToImage();
    void rebuild();
    void activate(KisNodeSP node);
    void showLayerMenu(KisNodeSP node, const QPoint &globalPos);

    struct Private;
    Private *d;
};

#endif // IKLAYERSPANEL_H
