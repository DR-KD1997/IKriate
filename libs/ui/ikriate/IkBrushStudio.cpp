/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "IkBrushStudio.h"

#include <QAction>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSplitter>
#include <QVBoxLayout>
#include <QtMath>

#include <klocalizedstring.h>

#include <KoCanvasResourceProvider.h>
#include <KoCanvasResourcesIds.h>
#include <brushengine/kis_paintop_preset.h>

#include "KisMainWindow.h"
#include "KisViewManager.h"
#include "kis_canvas_resource_provider.h"
#include "kis_config.h"
#include "kis_paintop_box.h"
#include "widgets/kis_preset_chooser.h"

#include "IkInterfaceManager.h"

namespace
{
// Size slider is logarithmic over 1..1000 px; 1000 steps.
const qreal kMaxSize = 1000.0;
int sizeToSlider(qreal px) { return qRound(qBound<qreal>(0.0, std::log(qMax<qreal>(1.0, px)) / std::log(kMaxSize), 1.0) * 1000); }
qreal sliderToSize(int v) { return qMax<qreal>(1.0, std::pow(kMaxSize, v / 1000.0)); }

struct Smoothing { const char *action; const char *label; };
const Smoothing kSmoothing[] = {
    {"set_no_brush_smoothing", I18N_NOOP("None")},
    {"set_simple_brush_smoothing", I18N_NOOP("Basic")},
    {"set_weighted_brush_smoothing", I18N_NOOP("Weighted")},
    {"set_stabilizer_brush_smoothing", I18N_NOOP("Stabiliser")},
};
}

IkBrushStudio::IkBrushStudio(IkInterfaceManager *manager, QWidget *parent)
    : QDialog(parent)
    , m_manager(manager)
{
    setWindowTitle(i18n("Brush Studio"));
    setObjectName("IkBrushStudio");

    QVBoxLayout *root = new QVBoxLayout(this);
    QSplitter *split = new QSplitter(Qt::Horizontal, this);
    root->addWidget(split, 1);

    // ---- left: tagged library with search ---------------------------------
    m_chooser = new KisPresetChooser(split);
    m_chooser->showTaggingBar(true);
    m_chooser->setViewModeToDetail();
    m_chooser->setMinimumWidth(320);
    connect(m_chooser, &KisPresetChooser::resourceSelected, this, &IkBrushStudio::slotPresetChosen);
    connect(m_chooser, &KisPresetChooser::resourceClicked, this, &IkBrushStudio::slotPresetChosen);
    split->addWidget(m_chooser);

    // ---- right: quick settings --------------------------------------------
    QWidget *settings = new QWidget(split);
    QVBoxLayout *sv = new QVBoxLayout(settings);
    sv->setContentsMargins(16, 4, 4, 4);

    m_presetName = new QLabel(settings);
    QFont titleFont = m_presetName->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.5);
    titleFont.setBold(true);
    m_presetName->setFont(titleFont);
    m_presetName->setWordWrap(true);
    sv->addWidget(m_presetName);
    m_engine = new QLabel(settings);
    m_engine->setForegroundRole(QPalette::PlaceholderText);
    sv->addWidget(m_engine);
    sv->addSpacing(12);

    QGridLayout *grid = new QGridLayout();
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(14);
    m_size = addSlider(grid, 0, i18n("Size"), &m_sizeValue);
    m_size->setRange(0, 1000);
    m_opacity = addSlider(grid, 1, i18n("Opacity"), &m_opacityValue);
    m_flow = addSlider(grid, 2, i18n("Flow"), &m_flowValue);

    QLabel *smoothLabel = new QLabel(i18n("Smoothing"), settings);
    m_smoothing = new QComboBox(settings);
    for (const Smoothing &s : kSmoothing) m_smoothing->addItem(i18n(s.label));
    m_smoothing->setCurrentIndex(qBound(0, KisConfig(true).lineSmoothingType(), 3));
    grid->addWidget(smoothLabel, 3, 0);
    grid->addWidget(m_smoothing, 3, 1, 1, 2);
    sv->addLayout(grid);
    sv->addStretch(1);

    QPushButton *fullEditor = new QPushButton(i18n("Open Full Brush Editor..."), settings);
    fullEditor->setToolTip(i18n("Every setting of the brush engine: pressure curves, tips, textures and more"));
    sv->addWidget(fullEditor);
    split->addWidget(settings);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    root->addWidget(buttons);

    // ---- wiring -------------------------------------------------------------
    connect(m_size, &QSlider::valueChanged, this, [this](int v) {
        m_sizeValue->setText(i18n("%1 px", QString::number(sliderToSize(v), 'f', sliderToSize(v) < 10 ? 1 : 0)));
        if (m_syncing) return;
        if (KisViewManager *vm = m_manager->viewManager()) vm->canvasResourceProvider()->setSize(sliderToSize(v));
    });
    connect(m_opacity, &QSlider::valueChanged, this, [this](int v) {
        m_opacityValue->setText(i18n("%1%", v));
        if (m_syncing) return;
        if (KisViewManager *vm = m_manager->viewManager()) vm->canvasResourceProvider()->setOpacity(v / 100.0);
    });
    connect(m_flow, &QSlider::valueChanged, this, [this](int v) {
        m_flowValue->setText(i18n("%1%", v));
        if (m_syncing) return;
        if (KisViewManager *vm = m_manager->viewManager()) vm->canvasResourceProvider()->setFlow(v / 100.0);
    });
    connect(m_smoothing, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        if (m_syncing || i < 0 || i > 3) return;
        m_manager->triggerAction(QString::fromLatin1(kSmoothing[i].action));
    });
    connect(fullEditor, &QPushButton::clicked, this, [this]() {
        close();
        m_manager->triggerAction(QStringLiteral("show_brush_editor"));
    });

    if (KisViewManager *vm = m_manager->viewManager()) {
        connect(vm->canvasResourceProvider()->resourceManager(), &KoCanvasResourceProvider::canvasResourceChanged,
                this, [this](int key, const QVariant &) {
                    if (!isVisible()) return;
                    if (key == KoCanvasResource::Size || key == KoCanvasResource::Opacity
                        || key == KoCanvasResource::Flow || key == KoCanvasResource::CurrentPaintOpPreset) {
                        slotSyncFromCanvas();
                    }
                });
    }
}

QSlider *IkBrushStudio::addSlider(QGridLayout *grid, int row, const QString &label, QLabel **valueLabel)
{
    QLabel *l = new QLabel(label, this);
    QSlider *s = new QSlider(Qt::Horizontal, this);
    s->setRange(0, 100);
    s->setMinimumHeight(32);
    s->setAccessibleName(label);
    *valueLabel = new QLabel(this);
    (*valueLabel)->setMinimumWidth(64);
    (*valueLabel)->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    grid->addWidget(l, row, 0);
    grid->addWidget(s, row, 1);
    grid->addWidget(*valueLabel, row, 2);
    grid->setColumnStretch(1, 1);
    return s;
}

void IkBrushStudio::showStudio()
{
    if (QWidget *p = parentWidget()) {
        const QSize target(qMax(760, p->width() * 4 / 5), qMax(520, p->height() * 4 / 5));
        resize(target.boundedTo(p->size()));
        move(p->mapToGlobal(QPoint((p->width() - width()) / 2, (p->height() - height()) / 2)));
    }
    slotSyncFromCanvas();
    show();
    raise();
    activateWindow();
}

void IkBrushStudio::slotPresetChosen(KoResourceSP resource)
{
    KisViewManager *vm = m_manager->viewManager();
    if (!vm || !vm->paintOpBox() || !resource) return;
    vm->paintOpBox()->resourceSelected(resource);
    // Choosing a brush also means "I want to paint": switch to the brush tool.
    m_manager->triggerAction(QStringLiteral("KritaShape/KisToolBrush"));
    slotSyncFromCanvas();
}

void IkBrushStudio::slotSyncFromCanvas()
{
    KisViewManager *vm = m_manager->viewManager();
    if (!vm || !vm->canvasResourceProvider()) return;
    KisCanvasResourceProvider *rp = vm->canvasResourceProvider();

    m_syncing = true;
    if (KisPaintOpPresetSP preset = rp->currentPreset()) {
        m_presetName->setText(preset->name());
        m_engine->setText(i18n("Engine: %1", preset->paintOp().name()));
        m_chooser->setCurrentResource(preset);
    } else {
        m_presetName->setText(i18n("No brush selected"));
        m_engine->clear();
    }
    m_size->setValue(sizeToSlider(rp->size()));
    m_opacity->setValue(qRound(rp->opacity() * 100));
    m_flow->setValue(qRound(rp->flow() * 100));
    m_syncing = false;

    // Value labels follow the slider even when the value did not change.
    m_sizeValue->setText(i18n("%1 px", QString::number(rp->size(), 'f', rp->size() < 10 ? 1 : 0)));
    m_opacityValue->setText(i18n("%1%", m_opacity->value()));
    m_flowValue->setText(i18n("%1%", m_flow->value()));
}
