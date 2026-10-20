/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef IKSETTINGSPAGE_H
#define IKSETTINGSPAGE_H

#include <QPointer>
#include <QWidget>

#include "kritaui_export.h"

class KisMainWindow;
class IkInterfaceManager;

/**
 * The Settings tab of the home screen: theme (system/light/dark, tint,
 * accent), layout, interface scale and font, language, advanced options,
 * clean-up, hardware acceleration, brush-outline ghosting and the recycle bin.
 */
class KRITAUI_EXPORT IkSettingsPage : public QWidget
{
    Q_OBJECT
public:
    explicit IkSettingsPage(KisMainWindow *mainWindow, QWidget *parent = nullptr);
    ~IkSettingsPage() override;

    void setManager(IkInterfaceManager *manager);

    /// Brush outline off, which removes outlines lingering after the pen lifts.
    static bool brushGhostingRemoved();
    static void setBrushGhostingRemoved(bool removed);

    int currentSection() const;
    void openSection(int section);

public Q_SLOTS:
    void reload();

private:
    void showSection(int section);
    QWidget *buildThemePage();
    QWidget *buildLanguagePage();
    QWidget *buildAdvancedPage();
    QWidget *buildCleanUpPage();
    QWidget *buildRecycleBinPage();
    void refreshCleanUp();
    void refreshRecycleBin();
    void notifyRestart();

    struct Private;
    Private *d;
};

#endif // IKSETTINGSPAGE_H
