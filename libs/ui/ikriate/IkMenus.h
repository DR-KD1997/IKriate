/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef IKMENUS_H
#define IKMENUS_H

#include <functional>

#include <QAbstractButton>
#include <QColor>
#include <QList>
#include <QWidget>

#include "kritaui_export.h"

class QGridLayout;
class QVBoxLayout;
class QLabel;
class QLineEdit;
class IkPopover;
class IkSlider;
class IkChannelSlider;

/// Icon above a label; the tiles of IkActionGrid.
class KRITAUI_EXPORT IkGridTile : public QAbstractButton
{
    Q_OBJECT
public:
    IkGridTile(const QString &icon, const QString &label, QWidget *parent = nullptr);
    void setIconName(const QString &icon);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_icon;
};

/// A titled grid of icon tiles (Options, layer actions). Extra controls
/// (a blend mode row, an opacity slider) can go between title and grid.
class KRITAUI_EXPORT IkActionGrid : public QWidget
{
    Q_OBJECT
public:
    explicit IkActionGrid(const QString &title, QWidget *parent = nullptr, int columns = 3);
    void setCentredTitle(bool centred);
    void addHeaderWidget(QWidget *widget);
    IkGridTile *addItem(const QString &icon, const QString &label, std::function<void()> action, bool enabled = true);
    /// Called after any tile runs (used to close the popover).
    std::function<void()> onTriggered;

private:
    QLabel *m_title {nullptr};
    QVBoxLayout *m_layout {nullptr};
    QGridLayout *m_grid {nullptr};
    int m_columns {3};
    int m_count {0};
};

/// Hue, saturation and brightness sliders with value boxes, a row of
/// swatches and a large preview; optionally a strength slider below.
class KRITAUI_EXPORT IkHsbPicker : public QWidget
{
    Q_OBJECT
public:
    explicit IkHsbPicker(QWidget *parent = nullptr);
    void setColor(const QColor &color);
    QColor color() const;
    void setSwatches(const QList<QColor> &colors);
    /// Shows the strength slider (0-100 %).
    void setStrength(int percent);
    int strength() const;

    std::function<void(const QColor &)> onColorChanged;
    std::function<void(int)> onStrengthChanged;

private:
    void sync();

    qreal m_h {0}, m_s {0}, m_v {1};
    IkChannelSlider *m_sliders[3] {nullptr, nullptr, nullptr};
    QLineEdit *m_fields[3] {nullptr, nullptr, nullptr};
    QWidget *m_preview {nullptr};
    QWidget *m_swatchHost {nullptr};
    QWidget *m_strengthHost {nullptr};
    QLabel *m_strengthLabel {nullptr};
    IkSlider *m_strength {nullptr};
};

namespace IkMenus
{
/// Opens @p content in a popover next to @p anchor (centred when null);
/// the popover deletes itself when dismissed.
KRITAUI_EXPORT IkPopover *popup(QWidget *content, QWidget *anchor, int placement, const QSize &size = QSize());
/// Popover that owns nothing yet; fill it with popover->setContent().
KRITAUI_EXPORT IkPopover *create(QWidget *window);
}

#endif // IKMENUS_H
