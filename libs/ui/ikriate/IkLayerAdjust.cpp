/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "IkLayerAdjust.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QVBoxLayout>

#include <KisGlobalResourcesInterface.h>
#include <filter/kis_filter.h>
#include <filter/kis_filter_configuration.h>
#include <filter/kis_filter_registry.h>
#include <klocalizedstring.h>

#include "KisViewManager.h"
#include "kis_filter_manager.h"

#include "IkInterfaceManager.h"
#include "IkStyle.h"
#include "IkWidgets.h"

namespace
{
struct Channel {
    QString label;      ///< shown left of the slider
    QString property;   ///< filter configuration key
    int range;          ///< slider runs -range..range
    QString unit;
};

/// Slider centred on zero, with its value on the right.
class IkCentredSlider : public QWidget
{
public:
    IkCentredSlider(const Channel &c, QWidget *parent) : QWidget(parent), channel(c)
    {
        QHBoxLayout *l = new QHBoxLayout(this);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(10);
        QLabel *name = new QLabel(c.label, this);
        name->setMinimumWidth(IkStyle::scaled(86));
        l->addWidget(name);
        slider = new IkSlider(this);
        slider->setValue(0.5);
        l->addWidget(slider, 1);
        readout = new QLabel(this);
        readout->setMinimumWidth(IkStyle::scaled(48));
        readout->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        readout->setFont(IkStyle::font(1.0, QFont::DemiBold));
        l->addWidget(readout);
        setValue(0);
        QObject::connect(slider, &IkSlider::valueEdited, this, [this](qreal v) {
            int value = qRound((v * 2.0 - 1.0) * channel.range);
            if (qAbs(value) <= channel.range / 40) value = 0; // snap to the centre
            m_value = value;
            readout->setText(text());
            if (onChanged) onChanged();
        });
    }

    int value() const { return m_value; }
    void setValue(int v)
    {
        m_value = v;
        slider->setValue((qreal(v) / channel.range + 1.0) / 2.0);
        readout->setText(text());
    }
    QString text() const { return (m_value > 0 ? QStringLiteral("+") : QString()) + QString::number(m_value) + channel.unit; }

    Channel channel;
    IkSlider *slider {nullptr};
    QLabel *readout {nullptr};
    std::function<void()> onChanged;

private:
    int m_value {0};
};
}

struct IkLayerAdjustPanel::Private
{
    QPointer<IkInterfaceManager> manager;
    IkSegmented *modes {nullptr};
    QWidget *pages[2] {nullptr, nullptr};
    QList<IkCentredSlider *> hsv;
    QList<IkCentredSlider *> balance;
    int mode {0};
    bool previewing {false};

    KisFilterManager *filterManager() const
    {
        KisViewManager *vm = manager ? manager->viewManager() : nullptr;
        return vm ? vm->filterManager() : nullptr;
    }
};

IkLayerAdjustPanel::IkLayerAdjustPanel(IkInterfaceManager *manager, QWidget *parent)
    : QWidget(parent)
    , d(new Private)
{
    d->manager = manager;
    QVBoxLayout *l = new QVBoxLayout(this);
    l->setContentsMargins(6, 4, 6, 4);
    l->setSpacing(12);

    QLabel *title = new QLabel(i18n("Adjust Layer"), this);
    title->setFont(IkStyle::font(1.15, QFont::Bold));
    l->addWidget(title);

    d->modes = new IkSegmented(this);
    d->modes->addSegment(QStringLiteral("colors"), i18nc("hue, saturation, brightness", "HSB"));
    d->modes->addSegment(QStringLiteral("sliders"), i18n("Colour Balance"));
    connect(d->modes, &IkSegmented::currentChanged, this, [this](int i) { setMode(i); });
    l->addWidget(d->modes);

    const QList<Channel> hsv = {{i18n("Hue"), QStringLiteral("h"), 180, QStringLiteral("°")},
                                {i18n("Saturation"), QStringLiteral("s"), 100, QStringLiteral("%")},
                                {i18n("Brightness"), QStringLiteral("v"), 100, QStringLiteral("%")}};
    const QList<Channel> balance = {{i18n("Cyan · Red"), QStringLiteral("cyan_red"), 100, QString()},
                                    {i18n("Magenta · Green"), QStringLiteral("magenta_green"), 100, QString()},
                                    {i18n("Yellow · Blue"), QStringLiteral("yellow_blue"), 100, QString()}};
    for (int page = 0; page < 2; ++page) {
        d->pages[page] = new QWidget(this);
        QVBoxLayout *pl = new QVBoxLayout(d->pages[page]);
        pl->setContentsMargins(0, 0, 0, 0);
        pl->setSpacing(10);
        for (const Channel &c : page == 0 ? hsv : balance) {
            IkCentredSlider *s = new IkCentredSlider(c, d->pages[page]);
            s->setMinimumHeight(IkStyle::touchSize() - 8);
            s->onChanged = [this]() { preview(); };
            pl->addWidget(s);
            (page == 0 ? d->hsv : d->balance) << s;
        }
        l->addWidget(d->pages[page]);
    }
    d->pages[1]->hide();

    QHBoxLayout *buttons = new QHBoxLayout();
    IkTextButton *reset = new IkTextButton(i18n("Reset"), IkTextButton::Plain, this);
    connect(reset, &QAbstractButton::clicked, this, &IkLayerAdjustPanel::reset);
    IkTextButton *apply = new IkTextButton(i18n("Apply"), IkTextButton::Filled, this);
    apply->setRadius(8);
    apply->setMinimumHeight(IkStyle::touchSize() - 6);
    connect(apply, &QAbstractButton::clicked, this, &IkLayerAdjustPanel::commit);
    buttons->addWidget(reset);
    buttons->addStretch(1);
    buttons->addWidget(apply);
    l->addLayout(buttons);
}

IkLayerAdjustPanel::~IkLayerAdjustPanel()
{
    commit();
    delete d;
}

void IkLayerAdjustPanel::setMode(int mode)
{
    if (mode == d->mode) return;
    commit();
    d->mode = mode;
    d->pages[0]->setVisible(mode == 0);
    d->pages[1]->setVisible(mode == 1);
}

void IkLayerAdjustPanel::preview()
{
    KisFilterManager *fm = d->filterManager();
    if (!fm) return;
    const bool hsvMode = d->mode == 0;
    KisFilterSP filter = KisFilterRegistry::instance()->value(hsvMode ? QStringLiteral("hsvadjustment") : QStringLiteral("colorbalance"));
    if (!filter) return;
    bool any = false;
    KisFilterConfigurationSP config = filter->defaultConfiguration(KisGlobalResourcesInterface::instance());
    if (hsvMode) {
        config->setProperty("type", 1); // HSV
        config->setProperty("colorize", false);
        for (IkCentredSlider *s : d->hsv) {
            config->setProperty(s->channel.property, s->value());
            any |= s->value() != 0;
        }
    } else {
        for (IkCentredSlider *s : d->balance) {
            config->setProperty(s->channel.property + QStringLiteral("_midtones"), s->value());
            any |= s->value() != 0;
        }
        config->setProperty("preserve_luminosity", true);
    }
    if (!any) {
        if (d->previewing) fm->cancelRunningStroke();
        d->previewing = false;
        return;
    }
    fm->apply(config);
    d->previewing = true;
}

void IkLayerAdjustPanel::commit()
{
    if (d->previewing) {
        if (KisFilterManager *fm = d->filterManager()) {
            if (fm->isStrokeRunning()) fm->finish();
        }
        d->previewing = false;
    }
    for (IkCentredSlider *s : d->hsv + d->balance) s->setValue(0);
}

void IkLayerAdjustPanel::reset()
{
    if (d->previewing) {
        if (KisFilterManager *fm = d->filterManager()) fm->cancelRunningStroke();
        d->previewing = false;
    }
    for (IkCentredSlider *s : d->hsv + d->balance) s->setValue(0);
}
