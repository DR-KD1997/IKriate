/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "IkMenus.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QVBoxLayout>

#include "IkStyle.h"
#include "IkWidgets.h"

// ---- IkGridTile -----------------------------------------------------------------

IkGridTile::IkGridTile(const QString &icon, const QString &label, QWidget *parent)
    : QAbstractButton(parent)
    , m_icon(icon)
{
    setText(label);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_Hover);
    setAccessibleName(label);
}

void IkGridTile::setIconName(const QString &icon)
{
    m_icon = icon;
    update();
}

QSize IkGridTile::sizeHint() const
{
    const QFontMetrics fm(IkStyle::font(0.95));
    return QSize(qMax(IkStyle::scaled(92), fm.horizontalAdvance(text()) + 16), IkStyle::scaled(86));
}

void IkGridTile::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette pal = palette();
    if (isEnabled() && (isDown() || underMouse() || isChecked())) {
        p.setPen(Qt::NoPen);
        p.setBrush(isDown() || isChecked() ? IkStyle::color(IkStyle::AccentSoft, pal) : IkStyle::color(IkStyle::RaisedHover, pal));
        p.drawRoundedRect(QRectF(rect()).adjusted(2, 2, -2, -2), 12, 12);
    }
    const QColor fg = !isEnabled() ? IkStyle::color(IkStyle::FaintText, pal)
                    : isChecked()  ? IkStyle::color(IkStyle::Accent, pal)
                                   : IkStyle::color(IkStyle::MutedText, pal);
    const int icon = IkStyle::scaled(30);
    const QRect iconRect((width() - icon) / 2, height() / 2 - icon - IkStyle::scaled(2), icon, icon);
    p.drawPixmap(iconRect, IkStyle::pixmap(m_icon, icon, fg, devicePixelRatioF()));
    p.setPen(fg);
    p.setFont(IkStyle::font(0.95));
    p.drawText(QRect(0, height() / 2 + IkStyle::scaled(8), width(), height() / 2 - IkStyle::scaled(8)),
               Qt::AlignHCenter | Qt::AlignTop, text());
}

// ---- IkActionGrid -----------------------------------------------------------------

IkActionGrid::IkActionGrid(const QString &title, QWidget *parent, int columns)
    : QWidget(parent)
    , m_columns(columns)
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(IkStyle::scaled(14), IkStyle::scaled(8), IkStyle::scaled(14), IkStyle::scaled(6));
    m_layout->setSpacing(IkStyle::scaled(12));
    m_title = new QLabel(title, this);
    m_title->setFont(IkStyle::font(1.3));
    m_title->setVisible(!title.isEmpty());
    m_layout->addWidget(m_title);
    m_grid = new QGridLayout();
    m_grid->setSpacing(IkStyle::scaled(6));
    m_layout->addLayout(m_grid);
}

void IkActionGrid::setCentredTitle(bool centred)
{
    m_title->setAlignment(centred ? Qt::AlignCenter : Qt::AlignLeft | Qt::AlignVCenter);
    m_title->setFont(IkStyle::font(centred ? 1.45 : 1.3));
}

void IkActionGrid::addHeaderWidget(QWidget *widget)
{
    widget->setParent(this);
    // Between the title and the grid (the grid is the last item).
    m_layout->insertWidget(m_layout->count() - 1, widget);
}

IkGridTile *IkActionGrid::addItem(const QString &icon, const QString &label, std::function<void()> action, bool enabled)
{
    IkGridTile *tile = new IkGridTile(icon, label, this);
    tile->setEnabled(enabled);
    connect(tile, &QAbstractButton::clicked, this, [this, action]() {
        // Close first: the action may open a dialog or another popover.
        if (onTriggered) onTriggered();
        if (action) action();
    });
    m_grid->addWidget(tile, m_count / m_columns, m_count % m_columns);
    ++m_count;
    return tile;
}

// ---- IkHsbPicker -----------------------------------------------------------------

namespace
{
class IkPreviewDot : public QWidget
{
public:
    using QWidget::QWidget;
    QColor color;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const qreal d = qMin(width(), height()) - 4;
        const QRectF r((width() - d) / 2, (height() - d) / 2, d, d);
        p.setPen(QPen(IkStyle::color(IkStyle::Text, palette()), 2));
        p.setBrush(color);
        p.drawEllipse(r);
    }
};
}

IkHsbPicker::IkHsbPicker(QWidget *parent)
    : QWidget(parent)
{
    QVBoxLayout *l = new QVBoxLayout(this);
    l->setContentsMargins(IkStyle::scaled(10), IkStyle::scaled(8), IkStyle::scaled(10), IkStyle::scaled(6));
    l->setSpacing(IkStyle::scaled(10));

    QGridLayout *g = new QGridLayout();
    g->setHorizontalSpacing(IkStyle::scaled(12));
    g->setVerticalSpacing(IkStyle::scaled(8));
    const char *letters[3] = {"H", "S", "B"};
    const QString units[3] = {QStringLiteral("°"), QStringLiteral("%"), QStringLiteral("%")};
    for (int i = 0; i < 3; ++i) {
        QLabel *letter = new QLabel(QString::fromLatin1(letters[i]), this);
        letter->setFont(IkStyle::font(0.9, QFont::Bold));
        g->addWidget(letter, i, 0);
        m_sliders[i] = new IkChannelSlider(IkChannelSlider::Channel(i), this);
        m_sliders[i]->setMinimumWidth(IkStyle::scaled(240));
        m_sliders[i]->setFixedHeight(IkStyle::scaled(28));
        g->addWidget(m_sliders[i], i, 1);
        m_fields[i] = new QLineEdit(this);
        m_fields[i]->setFixedWidth(IkStyle::scaled(60));
        m_fields[i]->setAlignment(Qt::AlignRight);
        m_fields[i]->setFrame(false);
        g->addWidget(m_fields[i], i, 2);
        QLabel *unit = new QLabel(units[i], this);
        g->addWidget(unit, i, 3);
        m_sliders[i]->onEdited = [this, i](qreal v) {
            (i == 0 ? m_h : i == 1 ? m_s : m_v) = v;
            sync();
            if (onColorChanged) onColorChanged(color());
        };
        connect(m_fields[i], &QLineEdit::editingFinished, this, [this, i]() {
            bool ok = false;
            const double raw = m_fields[i]->text().toDouble(&ok);
            if (!ok) return;
            (i == 0 ? m_h : i == 1 ? m_s : m_v) = qBound(0.0, i == 0 ? raw / 360.0 : raw / 100.0, i == 0 ? 0.9999 : 1.0);
            sync();
            if (onColorChanged) onColorChanged(color());
        });
    }
    l->addLayout(g);

    QHBoxLayout *bottom = new QHBoxLayout();
    bottom->setSpacing(IkStyle::scaled(12));
    QVBoxLayout *left = new QVBoxLayout();
    left->setSpacing(IkStyle::scaled(10));
    m_swatchHost = new QWidget(this);
    QHBoxLayout *sl = new QHBoxLayout(m_swatchHost);
    sl->setContentsMargins(0, 0, 0, 0);
    sl->setSpacing(IkStyle::scaled(10));
    left->addWidget(m_swatchHost);
    m_strengthHost = new QWidget(this);
    QVBoxLayout *st = new QVBoxLayout(m_strengthHost);
    st->setContentsMargins(0, 0, 0, 0);
    st->setSpacing(2);
    m_strengthLabel = new QLabel(m_strengthHost);
    m_strengthLabel->setAlignment(Qt::AlignCenter);
    m_strengthLabel->setForegroundRole(QPalette::Highlight);
    st->addWidget(m_strengthLabel);
    m_strength = new IkSlider(m_strengthHost);
    st->addWidget(m_strength);
    connect(m_strength, &IkSlider::valueEdited, this, [this](qreal v) {
        const int pct = qRound(v * 100);
        m_strengthLabel->setText(QStringLiteral("%1%").arg(pct));
        if (onStrengthChanged) onStrengthChanged(pct);
    });
    m_strengthHost->hide();
    left->addWidget(m_strengthHost);
    bottom->addLayout(left, 1);
    IkPreviewDot *dot = new IkPreviewDot(this);
    dot->setFixedSize(IkStyle::scaled(84), IkStyle::scaled(84));
    m_preview = dot;
    bottom->addWidget(dot, 0, Qt::AlignVCenter);
    l->addLayout(bottom);

    setSwatches({QColor(0xF2, 0x2C, 0x2C), QColor(0xFF, 0x9A, 0x1A), QColor(0xA8, 0x55, 0xF7), QColor(0x1F, 0xF0, 0xA0),
                 QColor(0xF3, 0xE6, 0xD2), QColor(0x22, 0x22, 0x22)});
    sync();
}

void IkHsbPicker::setSwatches(const QList<QColor> &colors)
{
    QLayout *sl = m_swatchHost->layout();
    while (QLayoutItem *it = sl->takeAt(0)) {
        delete it->widget();
        delete it;
    }
    for (const QColor &c : colors) {
        IkSwatch *sw = new IkSwatch(m_swatchHost);
        sw->setShape(true);
        sw->setDiameter(IkStyle::scaled(34));
        sw->setColor(c);
        connect(sw, &QAbstractButton::clicked, this, [this, c]() {
            setColor(c);
            if (onColorChanged) onColorChanged(color());
        });
        sl->addWidget(sw);
    }
    static_cast<QHBoxLayout *>(sl)->addStretch(1);
}

void IkHsbPicker::setColor(const QColor &c)
{
    if (!c.isValid()) return;
    qreal h, s, v;
    c.getHsvF(&h, &s, &v);
    if (h >= 0) m_h = qMin<qreal>(h, 0.9999);
    m_s = s;
    m_v = v;
    sync();
}

QColor IkHsbPicker::color() const
{
    return QColor::fromHsvF(m_h, m_s, m_v);
}

void IkHsbPicker::setStrength(int percent)
{
    m_strengthHost->show();
    m_strength->setValue(percent / 100.0);
    m_strengthLabel->setText(QStringLiteral("%1%").arg(percent));
}

int IkHsbPicker::strength() const
{
    return qRound(m_strength->value() * 100);
}

void IkHsbPicker::sync()
{
    const qreal vals[3] = {m_h, m_s, m_v};
    for (int i = 0; i < 3; ++i) {
        m_sliders[i]->h = m_h;
        m_sliders[i]->s = m_s;
        m_sliders[i]->v = m_v;
        m_sliders[i]->update();
        if (!m_fields[i]->hasFocus()) m_fields[i]->setText(QString::number(qRound(vals[i] * (i == 0 ? 360.0 : 100.0))));
    }
    static_cast<IkPreviewDot *>(m_preview)->color = color();
    m_preview->update();
}

// ---- IkMenus -----------------------------------------------------------------------

namespace IkMenus
{
IkPopover *create(QWidget *window)
{
    IkPopover *pop = new IkPopover(window);
    pop->setRadius(18);
    QObject::connect(pop, &IkPopover::dismissed, pop, &QObject::deleteLater);
    return pop;
}

IkPopover *popup(QWidget *content, QWidget *anchor, int placement, const QSize &size)
{
    QWidget *window = anchor ? anchor->window() : content->window();
    IkPopover *pop = create(window);
    pop->setContent(content);
    pop->popup(anchor, IkPopover::Placement(placement), size);
    return pop;
}
}
