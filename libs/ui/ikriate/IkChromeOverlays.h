/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef IKCHROMEOVERLAYS_H
#define IKCHROMEOVERLAYS_H

#include <QList>
#include <QObject>
#include <QPointer>
#include <QRect>

class IkInterfaceManager;
class IkChromeButton;
class IkPanel;
class IkPillSlider;
class IkPresetButton;
class IkSwatchButton;
class QHBoxLayout;
class QLabel;
class QWidget;

/**
 * Floating controls drawn over the canvas for the Minimal and Radial layouts.
 * The panels are children of the main window (not of the canvas), so they
 * never intercept pen input outside their own rectangles.
 */
class IkOverlayChrome : public QObject
{
    Q_OBJECT
public:
    explicit IkOverlayChrome(IkInterfaceManager *manager, QObject *parent = nullptr);
    ~IkOverlayChrome() override;

    void setVisible(bool visible);
    bool isVisible() const;
    /// Place the panels inside @p area (main-window coordinates).
    virtual void layoutPanels(const QRect &area) = 0;

public Q_SLOTS:
    /// Re-read sizes, colours, favourites and tool state.
    virtual void refresh();

protected:
    IkInterfaceManager *manager() const;
    QWidget *host() const;
    IkPanel *createPanel(bool pill = false);
    IkChromeButton *createButton(QWidget *parent, const QString &icon, const QString &tip);
    IkChromeButton *createToolButton(QWidget *parent, const QString &icon, const QString &tip,
                                     const QString &toolId);
    void bindSizeAndOpacity(IkPillSlider *size, IkPillSlider *opacity);
    void refreshToolButtons();
    void refreshSliders();
    void raisePanels();

    QList<QPointer<QWidget>> m_panels;

private Q_SLOTS:
    void slotResourceChanged(int key);
    void slotToolChanged();

private:
    IkInterfaceManager *m_manager;
    bool m_visible {false};
    bool m_connected {false};
    QList<QPair<QPointer<IkChromeButton>, QString>> m_toolButtons;
    QPointer<IkPillSlider> m_size;
    QPointer<IkPillSlider> m_opacity;
    void ensureConnections();
};

/// Iteration 02: thin top bar + left side bar (Procreate-like).
class IkMinimalChrome : public IkOverlayChrome
{
    Q_OBJECT
public:
    explicit IkMinimalChrome(IkInterfaceManager *manager, QObject *parent = nullptr);
    void layoutPanels(const QRect &area) override;

public Q_SLOTS:
    void refresh() override;

private:
    IkPanel *m_topBar {nullptr};
    IkPanel *m_sideBar {nullptr};
    IkSwatchButton *m_colour {nullptr};
};

/// Iteration 03: corner chips, edge sliders, quick dock, gesture hints.
class IkRadialChrome : public IkOverlayChrome
{
    Q_OBJECT
public:
    explicit IkRadialChrome(IkInterfaceManager *manager, QObject *parent = nullptr);
    void layoutPanels(const QRect &area) override;

public Q_SLOTS:
    void refresh() override;

private Q_SLOTS:
    void dismissGestureCard();

private:
    void rebuildDock();

    IkPanel *m_menuChip {nullptr};
    IkPanel *m_historyChip {nullptr};
    IkPanel *m_layersChip {nullptr};
    IkPanel *m_sliderPanel {nullptr};
    IkPanel *m_dock {nullptr};
    IkPanel *m_gestureCard {nullptr};
    QLabel *m_docTitle {nullptr};
    QHBoxLayout *m_presetRow {nullptr};
    QHBoxLayout *m_colourRow {nullptr};
    QList<QPointer<IkPresetButton>> m_presetButtons;
    QList<QPointer<IkSwatchButton>> m_swatches;
};

#endif // IKCHROMEOVERLAYS_H
