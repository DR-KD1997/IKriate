/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef IKWIDGETS_H
#define IKWIDGETS_H

#include <functional>

#include <QAbstractButton>
#include <QFrame>
#include <QPointer>
#include <QStringList>
#include <QWidget>

#include "IkStyle.h"
#include "kritaui_export.h"

class QAction;
class QLabel;
class QLineEdit;
class QMenu;

/**
 * Building blocks of the IKriate interface. They paint themselves from
 * IkStyle (and so from the active skin) instead of the Qt style, which keeps
 * the look identical on Windows, Linux and Android.
 */

/// Rounded background panel.
class KRITAUI_EXPORT IkPanel : public QFrame
{
    Q_OBJECT
public:
    explicit IkPanel(QWidget *parent = nullptr);
    void setRole(IkStyle::Role role);
    void setRadius(int radius); ///< -1: skin radius
    void setPill(bool pill);
    void setTranslucent(bool translucent);
    void setShadow(bool shadow);
    void setBorder(bool border);
    /// Outline in the accent gradient (pop-ups opened from settings).
    void setAccentOutline(bool outline);
    /// Only round some corners (e.g. a panel docked to a window edge).
    enum Corner { TopLeft = 1, TopRight = 2, BottomLeft = 4, BottomRight = 8, AllCorners = 15 };
    void setRoundedCorners(int corners);
    /// No background at all (a plain container that still lays out).
    void setPaintBackground(bool paint);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    bool m_paintBackground {true};
    IkStyle::Role m_role {IkStyle::Surface};
    int m_radius {-1};
    bool m_pill {false};
    bool m_translucent {false};
    bool m_shadow {false};
    bool m_border {false};
    bool m_accentOutline {false};
    int m_corners {AllCorners};
};

/// Icon button in the IKriate styles.
class KRITAUI_EXPORT IkIconButton : public QAbstractButton
{
    Q_OBJECT
public:
    enum Variant {
        Ghost,   ///< no background until hovered or active
        Round,   ///< filled circle (floating buttons over the canvas)
        Tile,    ///< filled rounded square
        Accent,  ///< accent-filled rounded square
        Fab,     ///< large accent-gradient circle with a glow
    };

    explicit IkIconButton(QWidget *parent = nullptr);
    IkIconButton(const QString &iconName, const QString &toolTip, QWidget *parent = nullptr);

    void setIconName(const QString &name);
    QString iconName() const;
    void setVariant(Variant variant);
    void setActive(bool active);
    bool isActive() const;
    void setIconPixelSize(int px);
    void setEdge(int px);
    /// Draws the icon in its own colours (e.g. the multicolour colour icon).
    void setKeepIconColors(bool keep);
    /// Mirrors enabled/checked state of @p action; clicking triggers it.
    void bindAction(QAction *action);
    QAction *boundAction() const;

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        QEnterEvent *event
#else
        QEvent *event
#endif
        ) override;
    void leaveEvent(QEvent *event) override;

private:
    void syncFromAction();

    QString m_iconName;
    Variant m_variant {Ghost};
    bool m_active {false};
    bool m_hover {false};
    bool m_keepColors {false};
    int m_iconPx {-1};
    int m_edge {-1};
    QPointer<QAction> m_action;
};

/// Horizontal slider: thin accent track with a round thumb (desktop brush bar).
class KRITAUI_EXPORT IkSlider : public QWidget
{
    Q_OBJECT
public:
    explicit IkSlider(QWidget *parent = nullptr);
    qreal value() const;
    void setValue(qreal value); ///< 0..1, does not emit
    QSize sizeHint() const override;

Q_SIGNALS:
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
    void setFromPos(qreal x);
    QRectF track() const;
    qreal m_value {0.5};
    bool m_drag {false};
};

/// Thick pill slider (size / opacity at the edge of the canvas).
class KRITAUI_EXPORT IkPillSlider : public QWidget
{
    Q_OBJECT
public:
    explicit IkPillSlider(Qt::Orientation orientation, QWidget *parent = nullptr);
    qreal value() const;
    void setValue(qreal value); ///< 0..1, does not emit
    void setLabel(const QString &label);
    /// Text shown in the bubble while dragging (default: percentage).
    void setReadoutFunction(std::function<QString(qreal)> fn);
    QSize sizeHint() const override;

Q_SIGNALS:
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
    QPointF m_pressPos;
    qreal m_pressValue {0.0};
    std::function<QString(qreal)> m_readout;
};

/// Row of mutually exclusive options with icon and text (theme mode).
class KRITAUI_EXPORT IkSegmented : public QWidget
{
    Q_OBJECT
public:
    explicit IkSegmented(QWidget *parent = nullptr);
    void addSegment(const QString &iconName, const QString &text);
    int currentIndex() const;
    void setCurrentIndex(int index); ///< does not emit
    QSize sizeHint() const override;

Q_SIGNALS:
    void currentChanged(int index);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QRectF segmentRect(int i) const;
    struct Segment { QString icon; QString text; };
    QList<Segment> m_segments;
    int m_current {0};
};

/// On/off switch.
class KRITAUI_EXPORT IkSwitch : public QAbstractButton
{
    Q_OBJECT
public:
    explicit IkSwitch(QWidget *parent = nullptr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
};

/// Tab strip with an accent underline under the current tab. Tabs can show
/// text, an icon, or both.
class KRITAUI_EXPORT IkTabBar : public QWidget
{
    Q_OBJECT
public:
    enum Style { Underline, Pills };
    explicit IkTabBar(QWidget *parent = nullptr);
    void setStyle(Style style);
    int addTab(const QString &iconName, const QString &text, const QString &toolTip = QString());
    int currentIndex() const;
    void setCurrentIndex(int index); ///< emits currentChanged
    /// Distribute tabs over the full width instead of packing them left.
    void setExpanding(bool expanding);
    QSize sizeHint() const override;

Q_SIGNALS:
    void currentChanged(int index);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QRectF tabRect(int i) const;
    qreal tabWidth(int i) const;
    struct Tab { QString icon; QString text; QString tip; };
    QList<Tab> m_tabs;
    int m_current {0};
    bool m_expanding {false};
    Style m_style {Underline};
};

/// Rounded box with a label and a round colour chip (Tint / Accent).
class KRITAUI_EXPORT IkColorWell : public QAbstractButton
{
    Q_OBJECT
public:
    explicit IkColorWell(const QString &label, QWidget *parent = nullptr);
    void setColor(const QColor &color);
    QColor color() const;
    /// Shows the chip as "none" (used when the tint is off).
    void setEmpty(bool empty);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_label;
    QColor m_color;
    bool m_empty {false};
};

/// Round colour chip.
class KRITAUI_EXPORT IkSwatch : public QAbstractButton
{
    Q_OBJECT
public:
    explicit IkSwatch(QWidget *parent = nullptr);
    void setColor(const QColor &color);
    QColor color() const;
    void setCurrent(bool current);
    void setShape(bool roundedSquare);
    void setDiameter(int px);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QColor m_color {Qt::black};
    bool m_current {false};
    bool m_square {false};
    int m_diameter {28};
};

/// Pill showing "Label  value  ▾"; clicking opens a list of choices.
class KRITAUI_EXPORT IkChoiceRow : public QAbstractButton
{
    Q_OBJECT
public:
    explicit IkChoiceRow(const QString &label, QWidget *parent = nullptr);
    void setChoices(const QStringList &choices, int current);
    void setCurrentIndex(int index); ///< does not emit
    int currentIndex() const;
    void setValueText(const QString &text);
    /// Centre the label and drop the value (used as a plain dropdown).
    void setCentered(bool centered);
    void setBordered(bool bordered);
    QSize sizeHint() const override;

Q_SIGNALS:
    void chosen(int index);

protected:
    void paintEvent(QPaintEvent *event) override;

private Q_SLOTS:
    void openMenu();

private:
    QString m_label;
    QString m_value;
    QStringList m_choices;
    int m_current {-1};
    bool m_centered {false};
    bool m_bordered {false};
};

/// Pill showing "Label   [editable number]".
class KRITAUI_EXPORT IkNumberRow : public QWidget
{
    Q_OBJECT
public:
    IkNumberRow(const QString &label, QWidget *parent = nullptr);
    void setRange(double min, double max, int decimals = 0);
    double value() const;
    void setValue(double value); ///< does not emit
    QLineEdit *editor() const;
    QSize sizeHint() const override;

Q_SIGNALS:
    void valueChanged(double value);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_label;
    QLineEdit *m_edit {nullptr};
    double m_min {0}, m_max {1e9};
    int m_decimals {0};
};

/// Section title in the accent colour with a fading underline.
class KRITAUI_EXPORT IkSectionHeader : public QWidget
{
    Q_OBJECT
public:
    explicit IkSectionHeader(const QString &text, QWidget *parent = nullptr);
    void setLarge(bool large);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_text;
    bool m_large {false};
};

/// Row with an icon and a label, optionally with a trailing widget
/// (used in the settings navigation).
class KRITAUI_EXPORT IkNavRow : public QAbstractButton
{
    Q_OBJECT
public:
    IkNavRow(const QString &iconName, const QString &text, QWidget *parent = nullptr);
    void setSelected(bool selected);
    void setTrailing(QWidget *widget);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    QString m_icon;
    bool m_selected {false};
    QPointer<QWidget> m_trailing;
};

/// Text button painted in the IKriate style (CONFIRM, PREVIEW, links).
class KRITAUI_EXPORT IkTextButton : public QAbstractButton
{
    Q_OBJECT
public:
    enum Variant { Plain, Filled, Link };
    IkTextButton(const QString &text, Variant variant, QWidget *parent = nullptr);
    void setFontScale(qreal scale, int weight = -1);
    void setRadius(int radius);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int m_radius {0};
    Variant m_variant;
    qreal m_scale {1.0};
    int m_weight {-1};
};

/// One-pixel horizontal divider in the divider colour.
class KRITAUI_EXPORT IkDivider : public QWidget
{
public:
    explicit IkDivider(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
};

/// Thin vertical divider between groups of buttons.
class KRITAUI_EXPORT IkSeparatorLine : public QWidget
{
public:
    explicit IkSeparatorLine(QWidget *parent = nullptr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
};

/**
 * Panel that floats over the window next to an anchor (colour, layers and
 * brush panels in the touch layouts). Lives inside the main window rather
 * than as a separate top-level window, which behaves better on Android.
 * A tap outside closes it.
 */
class KRITAUI_EXPORT IkPopover : public IkPanel
{
    Q_OBJECT
public:
    enum Placement { Below, Above, Left, Right, Center };

    explicit IkPopover(QWidget *window);
    ~IkPopover() override;

    void setContent(QWidget *content);
    QWidget *content() const;
    /// Shows next to @p anchor (or centred in the window when null).
    void popup(QWidget *anchor, Placement placement = Below, const QSize &size = QSize());
    /// Puts an open popover (and its scrim) back above the layout panels.
    void raiseToTop();
    void dismiss();
    bool isOpen() const;
    /// Keep open when the user taps outside (e.g. while painting).
    void setModal(bool modal);
    /// Translucent dark backdrop behind the popover (dialog-like sheets).
    void setDimBackground(bool dim);
    /// An X at the top right that closes the popover (window-like sheets).
    void setCloseButton(bool show);

Q_SIGNALS:
    void dismissed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void place();
    QPointer<QWidget> m_window;
    QPointer<QWidget> m_anchor;
    QPointer<QWidget> m_content;
    QWidget *m_scrim {nullptr};
    Placement m_placement {Below};
    QSize m_requested;
    bool m_modal {false};
    bool m_dim {false};
    QPointer<QWidget> m_close;
};


/// One hue / saturation / brightness slider: gradient track with a triangle
/// marker below it. Values are 0..1 (hue below 1).
class KRITAUI_EXPORT IkChannelSlider : public QWidget
{
public:
    enum Channel { Hue, Saturation, Brightness };
    IkChannelSlider(Channel channel, QWidget *parent);
    std::function<void(qreal)> onEdited;
    qreal h {0}, s {0}, v {0};
    qreal value() const;
    QSize sizeHint() const override;

protected:
    QRectF track() const;
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *) override;

private:
    void setFrom(qreal px);
    Channel m_channel;
    bool m_drag {false};
};

#endif // IKWIDGETS_H
