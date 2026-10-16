/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef IKAPPEARANCEDIALOG_H
#define IKAPPEARANCEDIALOG_H

#include <QColor>
#include <QDialog>

#include "IkSkinManager.h"

class IkInterfaceManager;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSlider;

/// Settings > Interface & Skins: layout, skin, accent colour and tint.
class IkAppearanceDialog : public QDialog
{
    Q_OBJECT
public:
    IkAppearanceDialog(IkInterfaceManager *manager, QWidget *parent = nullptr);

    /// Re-read current settings and the list of skins.
    void reload();

private Q_SLOTS:
    void slotApply();
    void slotImportSkin();
    void slotOpenSkinsFolder();
    void slotUpdatePreview();

private:
    IkSkinManager::Settings currentSettings() const;
    void setButtonColor(QPushButton *button, const QColor &color);
    QColor pickColor(QPushButton *button, const QColor &initial, const QString &title);

    IkInterfaceManager *m_manager;
    QButtonGroup *m_layoutGroup {nullptr};
    QCheckBox *m_followDefault {nullptr};
    QComboBox *m_skin {nullptr};
    QLabel *m_skinDescription {nullptr};
    QCheckBox *m_overrideAccent {nullptr};
    QPushButton *m_accentButton {nullptr};
    QPushButton *m_tintButton {nullptr};
    QSlider *m_tintStrength {nullptr};
    QLabel *m_tintValue {nullptr};
    QWidget *m_preview {nullptr};
    QColor m_accent;
    QColor m_tint;
};

#endif // IKAPPEARANCEDIALOG_H
