/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef IKINTERFACEMANAGER_H
#define IKINTERFACEMANAGER_H

#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QScopedPointer>

#include "kritaui_export.h"

class KisMainWindow;
class KisViewManager;
class QAction;
class QMenu;
class QWidget;

/**
 * Owns IKriate's selectable interface layouts for one main window.
 *
 *  - Classic: Krita's docked interface (default on desktop builds)
 *  - Minimal: full-bleed canvas with a thin top bar and a side bar
 *  - Radial:  full-bleed canvas, corner chips, quick dock and a
 *             long-press radial palette (default on Android tablets)
 *
 * Every layout is available on every build; only the first-run default
 * depends on the platform. The Brush Studio sheet and the Interface & Skins
 * dialog are reachable from all of them.
 */
class KRITAUI_EXPORT IkInterfaceManager : public QObject
{
    Q_OBJECT
public:
    enum Mode {
        Classic = 0,
        Minimal = 1,
        Radial = 2,
    };
    Q_ENUM(Mode)

    explicit IkInterfaceManager(KisMainWindow *mainWindow);
    ~IkInterfaceManager() override;

    static Mode platformDefaultMode();
    static QString modeId(Mode mode);
    static Mode modeFromId(const QString &id, Mode fallback);
    static QString modeDisplayName(Mode mode);
    static QString modeDescription(Mode mode);

    /// The manager attached to @p window, if any.
    static IkInterfaceManager *forWindow(QWidget *window);

    Mode mode() const;
    /// True when the mode follows the platform default rather than an explicit choice.
    bool followsPlatformDefault() const;

    KisMainWindow *mainWindow() const;
    KisViewManager *viewManager() const;
    QAction *action(const QString &name) const;

public Q_SLOTS:
    void setMode(IkInterfaceManager::Mode mode);
    void setFollowPlatformDefault(bool follow);
    void showBrushStudio();
    void showAppearanceDialog();

    /// Pops up a menu built from the (possibly hidden) main menu bar.
    void showMainMenu(QWidget *anchor);
    /// Pops up one top-level menu, e.g. "file" or "Filter".
    void showNamedMenu(const QString &menuName, QWidget *anchor);
    /// Shows Krita's radial pop-up palette, centred on @p globalPos or the canvas.
    void showRadialPalette(const QPoint &globalPos = QPoint());
    /// Toggles the layers docker as a floating panel next to @p anchor.
    void toggleLayersPanel(QWidget *anchor);
    void triggerAction(const QString &name);
    /// Activates the first brush preset whose name contains one of @p fragments.
    bool activatePresetMatching(const QStringList &fragments);

Q_SIGNALS:
    void modeChanged(IkInterfaceManager::Mode mode);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private Q_SLOTS:
    void slotDeferredInit();
    void slotStackPageChanged();
    void slotCanvasOnlyToggled(bool on);
    void slotLayoutOverlays();

private:
    void createActions();
    void applyMode(bool startup);
    void hideDesktopChrome();
    void restoreDesktopChrome();
    void updateOverlayVisibility();
    void applyInputProfile();
    void syncModeActions();

    struct Private;
    QScopedPointer<Private> d;
};

#endif // IKINTERFACEMANAGER_H
