/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef IKLAYOUTS_H
#define IKLAYOUTS_H

#include <functional>

#include <QHash>
#include <QList>
#include <QStringList>
#include <QMargins>
#include <QObject>
#include <QPair>
#include <QPointer>
#include <QRect>

#include "kritaui_export.h"

class IkInterfaceManager;
class IkPanel;
class IkIconButton;
class IkPillSlider;
class IkSlider;
class IkPopover;
class IkBrushLibrary;
class IkColorPanel;
class IkLayersPanel;
class IkAdjustmentsPanel;
class IkSwatch;
class IkTextButton;
class QLabel;
class QWidget;
class QStackedWidget;
class QBoxLayout;
class IkEditOverlay;

/**
 * One interface layout: a set of panels drawn over (or around) the canvas.
 *
 * The panels are children of the main window and positioned by the layout,
 * so they work identically on every platform. A layout can reserve window
 * margins (the Desktop layout does), which shrinks the canvas area so panels
 * sit beside the canvas rather than on top of it.
 */
class KRITAUI_EXPORT IkLayout : public QObject
{
    Q_OBJECT
public:
    explicit IkLayout(IkInterfaceManager *manager);
    ~IkLayout() override;

    IkInterfaceManager *manager() const;
    QWidget *host() const;

    virtual void setVisible(bool visible);
    bool isVisible() const;
    /// Places panels; @p area is the central area of the window.
    virtual void layoutPanels(const QRect &area) = 0;
    /// Space to keep free around the canvas.
    virtual QMargins reservedMargins() const;
    /// Re-reads tool, colour, brush and document state.
    virtual void refresh();
    /// Called when the skin changes (colours, icons).
    virtual void restyle();
    /// Closes everything temporary the layout shows (pop-overs, libraries)
    /// and leaves interface editing.
    virtual void closeTransient();

    /// Interface editing: tools are disabled and buttons can be dragged
    /// between the layout's docks (rows and columns of buttons).
    void setEditing(bool editing);
    bool isEditing() const;
    /// Puts every button back where the layout first had it.
    void resetArrangement();
    /// Keeps the editing overlay above the panels after a re-layout.
    void syncEditOverlay();

protected:
    IkPanel *panel(bool pill = false);
    IkIconButton *button(QWidget *parent, const QString &icon, const QString &tip);
    /// Button that activates tool @p toolId and highlights while it is active.
    IkIconButton *toolButton(QWidget *parent, const QString &icon, const QString &tip, const QString &toolId);
    /// Button bound to a Krita action by name.
    IkIconButton *actionButton(QWidget *parent, const QString &icon, const QString &tip, const QString &action);

    void bindSize(IkPillSlider *slider);
    void bindOpacity(IkPillSlider *slider);
    void bindSize(IkSlider *slider, QLabel *readout);
    void bindOpacity(IkSlider *slider, QLabel *readout);
    /// Swatch showing the foreground colour; tap opens the colour panel.
    void bindColorSwatch(IkSwatch *swatch);

    /// Popovers shared by the touch layouts.
    void toggleColorPopover(QWidget *anchor, int placement);
    /// Hue/saturation/brightness and colour balance of the selected layer.
    void toggleAdjustPopover(QWidget *anchor, int placement);
    /// X / Y / radial symmetry; the button lights up while symmetry is on.
    IkIconButton *symmetryButton(QWidget *parent);
    void toggleSymmetryPopover(QWidget *anchor, int placement);
    void toggleLayersPopover(QWidget *anchor, int placement);
    void toggleBrushPopover(QWidget *anchor, int placement);
    void showToolsPopover(QWidget *anchor, int placement);
    void showSelectionPopover(QWidget *anchor, int placement);
    void showToolOptionsPopover(QWidget *anchor, int placement);
    void showMoreMenu(QWidget *anchor);
    void closePopovers();
    /// Keeps open popovers above the panels after the panels are raised.
    void raisePopovers();

    /// Registers @p box (inside @p panel) as a dock whose buttons can be
    /// rearranged; @p style resizes a button that is dropped into it.
    void addDock(const QString &id, QWidget *panel, QBoxLayout *box, std::function<void(IkIconButton *)> style,
                 Qt::Alignment alignment = Qt::Alignment(), bool hideWhenEmpty = false);
    /// Call once all docks are filled: remembers the default arrangement and
    /// applies the one the user saved.
    void finishDocks();
    /// Number of buttons currently in dock @p id.
    int dockCount(const QString &id) const;
    /// Where to open a popover for @p anchor; follows the button if it was
    /// moved to a column or to the other side of the window.
    int placementFor(QWidget *anchor, int fallback) const;

    void ensureConnections();
    virtual void refreshTools();
    virtual void refreshSliders();
    virtual void refreshColor();
    virtual void refreshDocument();

    QList<QPointer<QWidget>> m_panels;
    QList<QPair<QPointer<IkIconButton>, QString>> m_toolButtons;
    QList<QPointer<IkIconButton>> m_drawButtons, m_eraseButtons, m_smudgeButtons;

private:
    friend class IkEditOverlay;
    struct Dock {
        QString id;
        QPointer<QWidget> panel;
        QBoxLayout *box {nullptr};
        std::function<void(IkIconButton *)> style;
        Qt::Alignment alignment;
        bool hideWhenEmpty {false};
        QPointer<QWidget> placeholder;
        QStringList defaults;
    };
    IkPopover *popover(QPointer<IkPopover> &slot);
    QList<IkIconButton *> dockItems(int dock) const;
    int dockOf(QWidget *item) const;
    void moveItem(IkIconButton *item, int dock, int index);
    void applyArrangement(const QHash<QString, QStringList> &arrangement);
    void saveArrangement() const;
    void updateDocks();
    QString configKey(const QString &dockId) const;

    QList<Dock> m_docks;
    QPointer<QWidget> m_editOverlay;
    bool m_editing {false};

    IkInterfaceManager *m_manager;
    bool m_visible {false};
    bool m_connected {false};
    QList<QPointer<IkPillSlider>> m_sizePills, m_opacityPills;
    QList<QPair<QPointer<IkSlider>, QPointer<QLabel>>> m_sizeSliders, m_opacitySliders;
    QList<QPointer<IkSwatch>> m_swatches;
    QPointer<IkPopover> m_colorPop, m_layersPop, m_brushPop, m_toolsPop, m_optionsPop, m_adjustPop, m_symmetryPop;
    QList<QPointer<IkIconButton>> m_symmetryButtons;
    QPointer<IkColorPanel> m_colorPanel;
    QPointer<IkLayersPanel> m_layersPanel;
    QPointer<IkBrushLibrary> m_brushLibrary;
};

/// Desktop: top bar, tool rail, brush library, colour/adjustments/options
/// and layers on the right, brush bar at the bottom.
class KRITAUI_EXPORT IkDesktopLayout : public IkLayout
{
    Q_OBJECT
public:
    explicit IkDesktopLayout(IkInterfaceManager *manager);
    void setVisible(bool visible) override;
    void layoutPanels(const QRect &area) override;
    QMargins reservedMargins() const override;
    void refresh() override;
    void closeTransient() override;

protected:
    void refreshDocument() override;
    void refreshTools() override;

private:
    void updateBrushChip();
    void setBrushLibraryOpen(bool open);
    void setRightTab(int tab);

    IkPanel *m_top {nullptr};
    IkPanel *m_rail {nullptr};
    IkPanel *m_right {nullptr};
    IkPanel *m_bottom {nullptr};
    IkPanel *m_library {nullptr};
    IkBrushLibrary *m_libraryWidget {nullptr};
    QLabel *m_title {nullptr};
    QLabel *m_subtitle {nullptr};
    IkTextButton *m_zoom {nullptr};
    QLabel *m_brushName {nullptr};
    QLabel *m_brushStroke {nullptr};
    QStackedWidget *m_rightStack {nullptr};
    QWidget *m_optionsHost {nullptr};
    IkSwatch *m_fg {nullptr};
    IkSwatch *m_bg {nullptr};
    int m_rightTab {0};
    bool m_libraryOpen {false};
    QRect m_area;
};

/// Kriate: touch-first. Top bar with tools and brush/eraser/smudge, docked
/// brush library on the right, size/opacity strip on the left.
class KRITAUI_EXPORT IkKriateLayout : public IkLayout
{
    Q_OBJECT
public:
    explicit IkKriateLayout(IkInterfaceManager *manager);
    void setVisible(bool visible) override;
    void layoutPanels(const QRect &area) override;
    void refresh() override;

protected:
    void refreshTools() override;

private:
    void setLibraryOpen(bool open);

    IkPanel *m_top {nullptr};
    IkPanel *m_side {nullptr};
    IkPanel *m_library {nullptr};
    IkBrushLibrary *m_libraryWidget {nullptr};
    IkSwatch *m_color {nullptr};
    bool m_libraryOpen {true};
    QRect m_area;
};

/// Minimal: floating tool strip, round buttons in the corners, size/opacity
/// strip with brush and eraser beside it; nothing else over the canvas.
class KRITAUI_EXPORT IkMinimalLayout : public IkLayout
{
    Q_OBJECT
public:
    explicit IkMinimalLayout(IkInterfaceManager *manager);
    void layoutPanels(const QRect &area) override;
    void refresh() override;

private:
    IkPanel *m_home {nullptr};
    IkPanel *m_tools {nullptr};
    IkPanel *m_right {nullptr};
    IkPanel *m_layers {nullptr};
    IkPanel *m_more {nullptr};
    IkPanel *m_side {nullptr};
    IkPanel *m_modes {nullptr};
    IkPanel *m_history {nullptr};
    IkSwatch *m_color {nullptr};
};

#endif // IKLAYOUTS_H
