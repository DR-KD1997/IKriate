/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef IKBRUSHSTUDIO_H
#define IKBRUSHSTUDIO_H

#include <QDialog>
#include <QPointer>

#include <KoResource.h>

class IkInterfaceManager;
class KisPresetChooser;
class QComboBox;
class QLabel;
class QSlider;

/**
 * Brush Studio (mockup 04): one sheet to browse tagged brush sets, search,
 * pick a brush and adjust the settings people change most, with a button
 * through to Krita's full brush editor for everything else.
 */
class IkBrushStudio : public QDialog
{
    Q_OBJECT
public:
    IkBrushStudio(IkInterfaceManager *manager, QWidget *parent = nullptr);

    /// Shows the sheet sized to the main window and synced to the current brush.
    void showStudio();

private Q_SLOTS:
    void slotPresetChosen(KoResourceSP resource);
    void slotSyncFromCanvas();

private:
    QSlider *addSlider(class QGridLayout *grid, int row, const QString &label, QLabel **valueLabel);

    IkInterfaceManager *m_manager;
    KisPresetChooser *m_chooser {nullptr};
    QLabel *m_presetName {nullptr};
    QLabel *m_engine {nullptr};
    QSlider *m_size {nullptr};
    QSlider *m_opacity {nullptr};
    QSlider *m_flow {nullptr};
    QLabel *m_sizeValue {nullptr};
    QLabel *m_opacityValue {nullptr};
    QLabel *m_flowValue {nullptr};
    QComboBox *m_smoothing {nullptr};
    bool m_syncing {false};
};

#endif // IKBRUSHSTUDIO_H
