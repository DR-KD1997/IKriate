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

    /// Symmetrical painting: mirror across the vertical axis (X), the
    /// horizontal axis (Y), both, or radial (rotational) with N sections.
    enum TransformMode { TransformBasic, TransformAnchor, TransformDistort, TransformWarp, TransformLiquify };
    enum SmudgeVariant { SmudgeSmear, SmudgeBlur, SmudgeLiquify };
    int transformMode() const;
    int smudgeVariant() const;

    enum Symmetry { SymmetryOff, SymmetryX, SymmetryY, SymmetryXY, SymmetryRadial };
    Q_ENUM(Symmetry)

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
    bool isEditingInterface() const;
    /// The part of the window the canvas shows in (without the Desktop
    /// layout's side panels).
    QRect canvasArea() const;
    KisViewManager *viewManager() const;
    QAction *action(const QString &name) const;
    IkHomeScreen *homeScreen() const;

    PaintMode paintMode() const;
    Symmetry symmetry() const;
    int radialSections() const;
    bool radialMirrored() const;
    bool isBrushToolActive() const;

    /// Moves Krita's tool options into @p host (one host at a time).
    void takeToolOptions(QWidget *host);
    void releaseToolOptions();

public Q_SLOTS:
    void setMode(IkInterfaceManager::Mode mode);
    void setFollowPlatformDefault(bool follow);
    void syncModeActions();

    void setPaintMode(IkInterfaceManager::PaintMode mode);
    void setSymmetry(IkInterfaceManager::Symmetry symmetry, int radialSections = -1, int radialMirrored = -1);
    void activateTool(const QString &toolId);
    /// Transform tool in one of its modes (TransformBasic ... TransformLiquify).
    void activateTransform(int mode);
    /// Mirrors the current layer (or selection) horizontally or vertically.
    void flipLayer(bool horizontal);
    /// Smudge (smear), Blur or Liquify (see SmudgeVariant).
    void setSmudgeVariant(int variant);
    /// "Selection Fill": a lasso whose shape is filled with the foreground
    /// colour as soon as it is closed.
    void activateSelectionFill();
    bool isSelectionFillActive() const;
    void ensureBrushTool();
    void showBrushEditor();
    void createBrushFromCurrent();
    void showBrushLibrary();
    void showSettings();
    /// Closes the current document (asking to save) and returns to the gallery.
    void goHome();

    /// Pops up a menu with the full main menu, the layouts and settings.
    void showMainMenu(QWidget *anchor);
    /// Layout choice (Desktop, Kriate, Minimal, Classic) and Edit Interface.
    void showLayoutMenu(QWidget *anchor);
    /// All of Krita's menus, as one menu.
    void showKritaMenus(QWidget *anchor);
    /// Pops up one top-level menu, e.g. "file" or "Filter".
    void showNamedMenu(const QString &menuName, QWidget *anchor);
    /// Shows the radial pop-up palette, centred on @p globalPos or the canvas.
    void showRadialPalette(const QPoint &globalPos = QPoint());
    void triggerAction(const QString &name);
    /// Recreates the home screen and the layouts, e.g. after the interface
    /// scale or font changed. Safe to call from inside the settings page.
    void rebuildInterface();
    /// Closes pop-overs, the brush library, menus and floating editors of the
    /// canvas layouts (used when returning to the home screen).
    void closeTransientWindows();
    /// Interface editing (move buttons between bars); see IkLayout::setEditing.
    void setEditingInterface(bool editing);
    /// Re-positions the current layout's panels.
    void requestLayout();
    /// Activates the first brush preset whose name contains one of @p fragments.
    bool activatePresetMatching(const QStringList &fragments);

Q_SIGNALS:
    void modeChanged(IkInterfaceManager::Mode mode);
    void paintModeChanged(IkInterfaceManager::PaintMode mode);
    void symmetryChanged(IkInterfaceManager::Symmetry symmetry);
    void editingInterfaceChanged(bool editing);

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
    /// Starts saving the active document when auto-save is on; closes it
    /// (back to the gallery) once saved. False if nothing was started.
    bool autoSaveThenGoHome();

    struct Private;
    QScopedPointer<Private> d;
};

#endif // IKINTERFACEMANAGER_H
