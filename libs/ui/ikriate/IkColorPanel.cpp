/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "IkColorPanel.h"

#include <QGridLayout>
#include <QLabel>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QTimer>
#include <QVBoxLayout>
#include <QVector4D>

#include <klocalizedstring.h>

#include <KisColorSelectorConfiguration.h>
#include <KisDlgInternalColorSelector.h>
#include <KoColorDisplayRendererInterface.h>
#include <KisVisualColorModel.h>
#include <KisVisualColorSelector.h>
#include <KoCanvasResourceProvider.h>
#include <KoCanvasResourcesIds.h>
#include <KoColorSpaceRegistry.h>

#include "KisViewManager.h"
#include "canvas/kis_canvas2.h"
#include "canvas/kis_display_color_converter.h"
#include "kis_canvas_resource_provider.h"
#include "kis_favorite_resource_manager.h"
#include "kis_paintop_box.h"

#include "IkInterfaceManager.h"
#include "IkStyle.h"
#include "IkWidgets.h"

namespace
{
/// Foreground colour in front of the background colour.
class IkFgBgChip : public QWidget
{
public:
    explicit IkFgBgChip(QWidget *parent) : QWidget(parent) { setCursor(Qt::PointingHandCursor); setFixedSize(IkStyle::scaled(44),IkStyle::scaled(44)); }
    QColor fg {Qt::black}, bg {Qt::white};
    std::function<void()> onClick;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QColor edge = IkStyle::color(IkStyle::Text, palette());
        p.setPen(QPen(IkStyle::withAlpha(edge, 120), 1));
        p.setBrush(bg);
        p.drawRoundedRect(QRectF(16, 16, 24, 24), 4, 4);
        p.setPen(QPen(IkStyle::withAlpha(edge, 200), 1.5));
        p.setBrush(fg);
        p.drawRoundedRect(QRectF(3, 3, 26, 26), 4, 4);
    }
    void mouseReleaseEvent(QMouseEvent *) override { if (onClick) onClick(); }
};
}

struct IkColorPanel::Private
{
    IkInterfaceManager *manager {nullptr};
    KisVisualColorModelSP model;
    KisVisualColorSelector *selector {nullptr};
    IkChannelSlider *sliders[3] {nullptr, nullptr, nullptr};
    QLineEdit *fields[3] {nullptr, nullptr, nullptr};
    QWidget *sliderBlock {nullptr};
    IkFgBgChip *chip {nullptr};
    QHBoxLayout *swatchRow {nullptr};
    QList<IkSwatch *> swatches;
    QPointer<KisViewManager> connectedVm;
    bool updating {false};
    QVector4D hsv;
};

IkColorPanel::IkColorPanel(IkInterfaceManager *manager, QWidget *parent)
    : QWidget(parent)
    , d(new Private)
{
    d->manager = manager;
    QVBoxLayout *l = new QVBoxLayout(this);
    l->setContentsMargins(12, 8, 12, 8);
    l->setSpacing(10);

    // ---- chips + sliders --------------------------------------------------
    d->sliderBlock = new QWidget(this);
    QGridLayout *g = new QGridLayout(d->sliderBlock);
    g->setContentsMargins(0, 0, 0, 0);
    g->setHorizontalSpacing(8);
    g->setVerticalSpacing(2);
    d->chip = new IkFgBgChip(d->sliderBlock);
    d->chip->onClick = [this]() {
        if (d->manager) d->manager->triggerAction(QStringLiteral("toggle_fg_bg"));
    };
    g->addWidget(d->chip, 0, 0, 3, 1, Qt::AlignTop);
    const char *letters[3] = {"H", "S", "B"};
    const QString units[3] = {QStringLiteral("°"), QStringLiteral("%"), QStringLiteral("%")};
    for (int i = 0; i < 3; ++i) {
        QLabel *letter = new QLabel(QString::fromLatin1(letters[i]), d->sliderBlock);
        letter->setFont(IkStyle::font(0.8, QFont::Bold));
        letter->setForegroundRole(QPalette::WindowText);
        g->addWidget(letter, i, 1);
        d->sliders[i] = new IkChannelSlider(IkChannelSlider::Channel(i), d->sliderBlock);
        g->addWidget(d->sliders[i], i, 2);
        d->fields[i] = new QLineEdit(d->sliderBlock);
        d->fields[i]->setFixedWidth(IkStyle::scaled(46));
        d->fields[i]->setAlignment(Qt::AlignRight);
        d->fields[i]->setFont(IkStyle::font(0.85));
        d->fields[i]->setFrame(false);
        g->addWidget(d->fields[i], i, 3);
        QLabel *unit = new QLabel(units[i], d->sliderBlock);
        unit->setFont(IkStyle::font(0.8));
        g->addWidget(unit, i, 4);

        d->sliders[i]->onEdited = [this, i](qreal val) {
            QVector4D v = d->hsv;
            v[i] = float(val);
            if (d->model) d->model->slotSetChannelValues(v);
        };
        connect(d->fields[i], &QLineEdit::editingFinished, this, [this, i]() {
            bool ok = false;
            const double raw = d->fields[i]->text().toDouble(&ok);
            if (!ok) return;
            QVector4D v = d->hsv;
            v[i] = float(qBound(0.0, i == 0 ? raw / 360.0 : raw / 100.0, i == 0 ? 0.9999 : 1.0));
            if (d->model) d->model->slotSetChannelValues(v);
        });
    }
    l->addWidget(d->sliderBlock);

    // ---- ring + triangle ----------------------------------------------------
    d->model = KisVisualColorModelSP(new KisVisualColorModel());
    d->model->setRGBColorModel(KisVisualColorModel::HSV);
    d->selector = new KisVisualColorSelector(this, d->model);
    KisColorSelectorConfiguration cfg(KisColorSelectorConfiguration::Triangle, KisColorSelectorConfiguration::Ring,
                                      KisColorSelectorConfiguration::SV, KisColorSelectorConfiguration::H);
    d->selector->setConfiguration(&cfg);
    d->selector->setAcceptTabletEvents(true);
    d->selector->setMinimumSize(IkStyle::scaled(180),IkStyle::scaled(180));
    d->selector->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    d->selector->slotSetColorSpace(KoColorSpaceRegistry::instance()->rgb8());
    l->addWidget(d->selector, 1);

    connect(d->model.data(), &KisVisualColorModel::sigChannelValuesChanged, this,
            [this](const QVector4D &values, quint32) {
                d->hsv = values;
                for (int i = 0; i < 3; ++i) {
                    d->sliders[i]->h = values[0];
                    d->sliders[i]->s = values[1];
                    d->sliders[i]->v = values[2];
                    d->sliders[i]->update();
                    if (!d->fields[i]->hasFocus()) {
                        d->fields[i]->setText(QString::number(qRound(values[i] * (i == 0 ? 360.0 : 100.0))));
                    }
                }
            });
    connect(d->selector, &KisColorSelectorInterface::sigNewColor, this, [this](const KoColor &c) {
        if (!d->updating) setForeground(c);
    });

    // ---- recent colours -----------------------------------------------------
    QHBoxLayout *bottom = new QHBoxLayout();
    bottom->setSpacing(6);
    d->swatchRow = new QHBoxLayout();
    d->swatchRow->setSpacing(6);
    bottom->addLayout(d->swatchRow, 1);
    IkIconButton *more = new IkIconButton(QStringLiteral("plus"), i18n("More colour options"), this);
    more->setEdge(30);
    connect(more, &QAbstractButton::clicked, this, [this]() {
        KisViewManager *vm = d->manager ? d->manager->viewManager() : nullptr;
        if (!vm || !vm->canvasResourceProvider()) return;
        KisDlgInternalColorSelector::Config config;
        config.modal = true;
        KisDlgInternalColorSelector dlg(this, vm->canvasResourceProvider()->fgColor(), config, i18n("Colour"),
                                        vm->canvasBase() ? vm->canvasBase()->displayColorConverter()->displayRendererInterface()
                                                         : KoDumbColorDisplayRenderer::instance());
        if (dlg.exec() == QDialog::Accepted) setForeground(dlg.getCurrentColor());
    });
    bottom->addWidget(more, 0, Qt::AlignTop);
    l->addLayout(bottom);

    refresh();
}

IkColorPanel::~IkColorPanel()
{
    delete d;
}

QSize IkColorPanel::sizeHint() const
{
    return QSize(340, d->sliderBlock->isVisible() ? 440 : 330);
}

void IkColorPanel::setShowSliders(bool show)
{
    d->sliderBlock->setVisible(show);
}

void IkColorPanel::connectToCanvas()
{
    KisViewManager *vm = d->manager ? d->manager->viewManager() : nullptr;
    if (!vm || d->connectedVm == vm || !vm->canvasResourceProvider()) return;
    d->connectedVm = vm;
    connect(vm->canvasResourceProvider(), &KisCanvasResourceProvider::sigFGColorChanged, this,
            [this](const KoColor &c) { updateFromColor(c); });
    connect(vm->canvasResourceProvider(), &KisCanvasResourceProvider::sigBGColorChanged, this, [this](const KoColor &c) {
        d->chip->bg = c.toQColor();
        d->chip->update();
    });
    if (vm->paintOpBox() && vm->paintOpBox()->favoriteResourcesManager()) {
        connect(vm->paintOpBox()->favoriteResourcesManager(), &KisFavoriteResourceManager::setSelectedColor, this,
                [this](int) { rebuildSwatches(); });
        connect(vm->canvasResourceProvider(), &KisCanvasResourceProvider::sigFGColorUsed, this,
                [this](const KoColor &) { QTimer::singleShot(0, this, [this]() { rebuildSwatches(); }); });
    }
}

void IkColorPanel::refresh()
{
    connectToCanvas();
    KisViewManager *vm = d->manager ? d->manager->viewManager() : nullptr;
    if (vm && vm->canvasResourceProvider()) {
        updateFromColor(vm->canvasResourceProvider()->fgColor());
        d->chip->bg = vm->canvasResourceProvider()->bgColor().toQColor();
        if (vm->canvasBase()) {
            d->selector->setDisplayRenderer(vm->canvasBase()->displayColorConverter()->displayRendererInterface());
        }
    }
    rebuildSwatches();
}

void IkColorPanel::setForeground(const KoColor &color)
{
    KisViewManager *vm = d->manager ? d->manager->viewManager() : nullptr;
    if (!vm || !vm->canvasResourceProvider()) return;
    d->updating = true;
    vm->canvasResourceProvider()->setFGColor(color);
    d->chip->fg = color.toQColor();
    d->chip->update();
    d->updating = false;
}

void IkColorPanel::updateFromColor(const KoColor &color)
{
    if (d->updating) return;
    d->updating = true;
    d->selector->slotSetColor(color);
    d->chip->fg = color.toQColor();
    d->chip->update();
    d->updating = false;
}

void IkColorPanel::rebuildSwatches()
{
    KisViewManager *vm = d->manager ? d->manager->viewManager() : nullptr;
    KisFavoriteResourceManager *fav = (vm && vm->paintOpBox()) ? vm->paintOpBox()->favoriteResourcesManager() : nullptr;
    QList<QColor> colors;
    const int recent = fav ? qMin(6, fav->recentColorsTotal()) : 0;
    for (int i = 0; i < recent; ++i) colors << fav->recentColorAt(i).toQColor();
    // Start with a friendly default palette until there is history.
    const QList<QColor> defaults = {QColor(0xC7, 0x5B, 0x3B), QColor(0xEE, 0xB0, 0x5E), QColor(0x5F, 0x80, 0x6B),
                                    QColor(0x33, 0x4A, 0x3E), QColor(0xF2, 0xE6, 0xD3), QColor(0x22, 0x22, 0x22)};
    for (int i = colors.size(); i < 6; ++i) colors << defaults[i];

    while (d->swatches.size() < colors.size()) {
        IkSwatch *s = new IkSwatch(this);
        s->setShape(true);
        s->setDiameter(30);
        s->setMinimumSize(IkStyle::scaled(40),IkStyle::scaled(40));
        s->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(s, &QAbstractButton::clicked, this, [this, s]() {
            setForeground(KoColor(s->color(), KoColorSpaceRegistry::instance()->rgb8()));
            updateFromColor(KoColor(s->color(), KoColorSpaceRegistry::instance()->rgb8()));
        });
        d->swatchRow->addWidget(s);
        d->swatches << s;
    }
    for (int i = 0; i < d->swatches.size(); ++i) d->swatches[i]->setColor(colors.value(i));
}
