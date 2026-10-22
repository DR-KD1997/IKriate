/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef IKBRUSHSETTINGS_H
#define IKBRUSHSETTINGS_H

#include <QWidget>

#include "kritaui_export.h"

class IkInterfaceManager;

/**
 * The brush settings of the IKriate layouts: the most used settings of the
 * current brush on five pages (Stroke, Head, Texture, Paint, Special), with
 * sliders, switches and choices instead of Krita's option tree.
 *
 * It edits the current preset's settings directly; Krita's own option
 * widgets follow along through the preset's update signals.
 */
class KRITAUI_EXPORT IkBrushSettingsPanel : public QWidget
{
    Q_OBJECT
public:
    explicit IkBrushSettingsPanel(IkInterfaceManager *manager, QWidget *editor, QWidget *parent = nullptr);
    ~IkBrushSettingsPanel() override;

    /// Re-reads every value from the current brush.
    void reload();
    /// Where the live stroke preview goes.
    QWidget *previewHost() const;

protected:
    void showEvent(QShowEvent *event) override;

private:
    struct Private;
    Private *d;
};

namespace IkBrushSettings
{
/// Puts the IKriate settings panel into Krita's brush editor (@p on), or
/// gives the editor back its own option tree (Classic layout).
KRITAUI_EXPORT void adoptEditor(QWidget *editor, IkInterfaceManager *manager, bool on);
}

#endif // IKBRUSHSETTINGS_H
