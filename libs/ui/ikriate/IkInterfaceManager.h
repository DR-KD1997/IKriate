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
class IkHomeScreen;

/**
 * Owns IKriate's interface for one main window: the home screen and the
 * selectable layouts.
 *
 *  - Desktop: top bar, tool rail, colour and layers panel, brush bar
 *             (default on computers)
 *  - Kriate:  touch-first; tool bar on top, docked brush library, size and
 *             opacity strip (default on Android tablets)
 *  - Minimal: just a few floating buttons around the canvas
 *  - Classic: the docker interface with text menus, for power users
 *             (offered when enabled under Settings › Advanced)
 *
 * It also owns the draw / erase / smudge mode, which applies to whichever
 * brush is selected.
 */
class KRITAUI_EXPORT IkInterfaceManager : public QObject
{
    Q_OBJECT
public:
    enum Mode {
        Desktop = 0,
        Kriate = 1,
        Minimal = 2,
        Classic = 3,
    };
    Q_ENUM(Mode)

    enum PaintMode { Draw, Erase, Smudge };
    Q_ENUM(PaintMode)

    explicit IkInterfaceManager(KisMainWindow *mainWindow);
    ~IkInterfaceManager() override;

    static Mode platformDefaultMode();
    static QString modeId(Mode mode);
    static Mode modeFromId(const QString &id, Mode fallback);
    static QString modeDisplayName(Mode mode);
    static QString modeDescription(Mode mode);

    /// The manager attached to @p window, if any.
    static IkInterfaceManager *forWindow(QWidget *window);

    /// The layout in use on the canvas.
    Mode mode() const;
    /// The layout the user chose (same as mode()).
    Mode preferredMode() const;
    bool followsPlatformDefault() const;

    KisMainWindow *mainWindow() const;
    KisViewManager *viewManager() const;
    QAction *action(const QString &name) const;
    IkHomeScreen *homeScreen() const;

    PaintMode paintMode() const;
    bool isBrushToolActive() const;

    /// Moves Krita's tool options into @p host (one host at a time).
    void takeToolOptions(QWidget *host);
    void releaseToolOptions();

public Q_SLOTS:
    void setMode(IkInterfaceManager::Mode mode);
    void setFollowPlatformDefault(bool follow);
    void syncModeActions();

    void setPaintMode(IkInterfaceManager::PaintMode mode);
    void activateTool(const QString &toolId);
    void ensureBrushTool();
    void showBrushEditor();
    void createBrushFromCurrent();
    void showBrushLibrary();
    void showSettings();
    /// Closes the current document (asking to save) and returns to the gallery.
    void goHome();

    /// Pops up a menu with the full main menu, the layouts and settings.
    void showMainMenu(QWidget *anchor);
    /// Pops up one top-level menu, e.g. "file" or "Filter".
    void showNamedMenu(const QString &menuName, QWidget *anchor);
    /// Shows the radial pop-up palette, centred on @p globalPos or the canvas.
    void showRadialPalette(const QPoint &globalPos = QPoint());
    void triggerAction(const QString &name);
    /// Activates the first brush preset whose name contains one of @p fragments.
    bool activatePresetMatching(const QStringList &fragments);

Q_SIGNALS:
    void modeChanged(IkInterfaceManager::Mode mode);
    void paintModeChanged(IkInterfaceManager::PaintMode mode);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private Q_SLOTS:
    void slotDeferredInit();
    void slotStackPageChanged();
    void slotCanvasOnlyToggled(bool on);
    void slotLayoutPanels();
    void slotEraserModeChanged();
    void slotPresetChanged();

private:
    void createActions();
    void applyMode();
    void hideDesktopChrome();
    void restoreDesktopChrome();
    void updateLayoutVisibility();
    void applyInputProfile();
    void setDocumentTabsVisible(bool visible);

    struct Private;
    QScopedPointer<Private> d;
};

#endif // IKINTERFACEMANAGER_H
