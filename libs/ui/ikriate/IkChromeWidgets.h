/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef IKCHROMEWIDGETS_H
#define IKCHROMEWIDGETS_H

#include <QAbstractButton>
#include <QFrame>
#include <QImage>
#include <QPointer>
#include <QToolButton>

class QAction;

/**
 * Small, touch-sized building blocks for IKriate's floating interface chrome
 * (the Minimal and Radial layouts). They draw themselves from the current
 * application palette, so skins and tinting apply to them automatically.
 */
namespace IkChrome
{
/// Minimum touch target edge in device-independent pixels.
int touchSize();
/// Corner radius and panel opacity of the active skin.
int panelRadius();
int panelOpacity();
}

/// Rounded, slightly translucent panel that floats over the canvas.
class IkPanel : public QFrame
{
    Q_OBJECT
public:
    explicit IkPanel(QWidget *parent = nullptr);
    /// Fully rounded ends (a "pill"), used for the small corner chips.
    void setPill(bool pill);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    bool m_pill {false};
};

/// Square icon button for the chrome. Can mirror a QAction (icon, tooltip,
/// checked state) or act on its own.
class IkChromeButton : public QToolButton
{
    Q_OBJECT
public:
    explicit IkChromeButton(QWidget *parent = nullptr);
    IkChromeButton(const QString &iconName, const QString &toolTip, QWidget *parent = nullptr);

    void setIconName(const QString &iconName);
    /// Mirrors checked state and enabled state of @p action; clicking triggers it.
    void bindAction(QAction *action);
    /// Highlight independent of QAbstractButton::isChecked (e.g. "active tool").
    void setActive(bool active);
    bool isActive() const;

    QSize sizeHint() const override;
    void refreshIcon();

protected:
    void paintEvent(QPaintEvent *event) override;
    void changeEvent(QEvent *event) override;

private Q_SLOTS:
    void syncFromAction();

private:
    QString m_iconName;
    QPointer<QAction> m_action;
    bool m_active {false};
};

/// Vertical pill slider tuned for fingers and pens: tap or drag anywhere on
/// the track. Value range is 0..1; mapping to real units is done by the owner.
class IkPillSlider : public QWidget
{
    Q_OBJECT
public:
    explicit IkPillSlider(Qt::Orientation orientation, QWidget *parent = nullptr);

    qreal value() const;
    void setValue(qreal value); // does not emit valueEdited
    void setLabel(const QString &label);
    QSize sizeHint() const override;

Q_SIGNALS:
    /// Emitted while the user drags or taps.
    void valueEdited(qreal value);
    void interactionStarted();
    void interactionFinished();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void tabletEvent(QTabletEvent *event) override;
    bool event(QEvent *event) override;

private:
    void updateFromPosition(const QPointF &pos);
    QRectF trackRect() const;

    Qt::Orientation m_orientation;
    qreal m_value {0.5};
    bool m_dragging {false};
    QString m_label;
};

/// Round colour chip. Shows a ring when it represents the current colour.
class IkSwatchButton : public QAbstractButton
{
    Q_OBJECT
public:
    explicit IkSwatchButton(QWidget *parent = nullptr);
    void setColor(const QColor &color);
    QColor color() const;
    void setCurrent(bool current);
    void setDiameter(int diameter);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QColor m_color {Qt::black};
    bool m_current {false};
    int m_diameter {28};
};

/// Thumbnail button for a brush preset.
class IkPresetButton : public QAbstractButton
{
    Q_OBJECT
public:
    explicit IkPresetButton(QWidget *parent = nullptr);
    void setPreset(const QImage &image, const QString &name);
    void setActive(bool active);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QImage m_image;
    bool m_active {false};
};

/// Thin vertical or horizontal separator line for chrome rows.
class IkSeparator : public QWidget
{
public:
    explicit IkSeparator(Qt::Orientation orientation, QWidget *parent = nullptr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    Qt::Orientation m_orientation;
};

#endif // IKCHROMEWIDGETS_H
