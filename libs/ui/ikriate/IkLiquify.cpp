/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "IkLiquify.h"

#include <QAbstractButton>
#include <QAction>
#include <cmath>
#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

#include <KConfigGroup>
#include <KSharedConfig>
#include <klocalizedstring.h>

#include "KisMainWindow.h"

#include "IkInterfaceManager.h"
#include "IkMenus.h"
#include "IkStyle.h"
#include "IkWidgets.h"

namespace
{
enum Mode { Push, Pinch, Bloat, SwirlCW, SwirlCCW, Restore, ModeCount };

struct ModeInfo {
    const char *icon;
    const char *button;   ///< Krita's liquify mode button
    bool reverse;
};
const ModeInfo kModes[ModeCount] = {
    {"liquify-push", "liquifyMove", false},
    {"liquify-pinch", "liquifyScale", true},
    {"liquify-bloat", "liquifyScale", false},
    {"liquify-cw", "liquifyRotate", true},   // Krita rotates counter-clockwise unless reversed
    {"liquify-ccw", "liquifyRotate", false},
    {"liquify-restore", "liquifyUndo", false},
};

QString modeName(int m)
{
    switch (m) {
    case Push: return i18nc("liquify mode", "Push");
    case Pinch: return i18nc("liquify mode", "Pinch");
    case Bloat: return i18nc("liquify mode", "Bloat");
    case SwirlCW: return i18nc("liquify mode", "Swirl Clockwise");
    case SwirlCCW: return i18nc("liquify mode", "Swirl Counterclockwise");
    default: return i18nc("liquify mode", "Restore");
    }
}

KConfigGroup config()
{
    return KConfigGroup(KSharedConfig::openConfig(), "IKriate");
}

/// The transform tool's options (wherever IKriate or Krita keeps them).
QWidget *transformOptions()
{
    for (QWidget *top : QApplication::topLevelWidgets()) {
        if (top->inherits("KisToolTransformConfigWidget")) return top;
        for (QWidget *w : top->findChildren<QWidget *>()) {
            if (w->inherits("KisToolTransformConfigWidget")) return w;
        }
    }
    return nullptr;
}

template<class T>
T *option(const char *name)
{
    QWidget *options = transformOptions();
    return options ? options->findChild<T *>(QString::fromLatin1(name)) : nullptr;
}

class IkLiquifyBar : public IkPanel
{
public:
    IkLiquifyBar(IkInterfaceManager *manager)
        : IkPanel(manager->mainWindow())
        , m_manager(manager)
    {
        setRole(IkStyle::Surface);
        setRadius(18);
        setShadow(true);
        QVBoxLayout *l = new QVBoxLayout(this);
        l->setContentsMargins(IkStyle::scaled(20), IkStyle::scaled(12), IkStyle::scaled(20), IkStyle::scaled(16));
        l->setSpacing(IkStyle::scaled(4));

        QHBoxLayout *top = new QHBoxLayout();
        top->setSpacing(IkStyle::scaled(12));
        IkIconButton *cancel = new IkIconButton(QStringLiteral("close"), i18n("Cancel liquify"), this);
        connect(cancel, &QAbstractButton::clicked, this, [this]() { finish(false); });
        top->addWidget(cancel);
        IkIconButton *more = new IkIconButton(QStringLiteral("sliders-horizontal"), i18n("Size, spacing and flow"), this);
        connect(more, &QAbstractButton::clicked, this, [this, more]() { showMore(more); });
        top->addWidget(more);
        m_strength = new IkSlider(this);
        m_strength->setToolTip(i18n("Strength"));
        m_strength->setMinimumWidth(IkStyle::scaled(260));
        connect(m_strength, &IkSlider::valueEdited, this, [](qreal v) {
            if (QDoubleSpinBox *amount = option<QDoubleSpinBox>("liquifyAmountSlider")) {
                amount->setValue(amount->minimum() + v * (amount->maximum() - amount->minimum()));
            }
        });
        top->addWidget(m_strength, 1);
        m_eye = new IkIconButton(QStringLiteral("eye"), i18n("Hide the rest of the interface"), this);
        connect(m_eye, &QAbstractButton::clicked, this, [this]() {
            m_manager->triggerAction(QStringLiteral("view_show_canvas_only"));
            QAction *a = m_manager->action(QStringLiteral("view_show_canvas_only"));
            m_eye->setActive(a && a->isChecked());
            raise();
        });
        top->addWidget(m_eye);
        IkIconButton *apply = new IkIconButton(QStringLiteral("check"), i18n("Apply"), this);
        connect(apply, &QAbstractButton::clicked, this, [this]() { finish(true); });
        top->addWidget(apply);
        l->addLayout(top);

        m_label = new QLabel(this);
        m_label->setAlignment(Qt::AlignCenter);
        m_label->setFont(IkStyle::font(1.5));
        m_label->setForegroundRole(QPalette::PlaceholderText);
        l->addWidget(m_label);

        QHBoxLayout *modes = new QHBoxLayout();
        modes->setSpacing(IkStyle::scaled(10));
        for (int m = 0; m < ModeCount; ++m) {
            IkIconButton *b = new IkIconButton(QString::fromLatin1(kModes[m].icon), modeName(m), this);
            b->setEdge(IkStyle::touchSize() + IkStyle::scaled(6));
            b->setIconPixelSize(IkStyle::scaled(28));
            connect(b, &QAbstractButton::clicked, this, [this, m]() { setMode(m); });
            modes->addStretch(1);
            modes->addWidget(b);
            m_modeButtons[m] = b;
        }
        modes->addStretch(1);
        l->addLayout(modes);
    }

    void sync()
    {
        setMode(config().readEntry("LiquifyMode", int(Push)));
        if (QDoubleSpinBox *amount = option<QDoubleSpinBox>("liquifyAmountSlider")) {
            const qreal range = amount->maximum() - amount->minimum();
            m_strength->setValue(range > 0 ? (amount->value() - amount->minimum()) / range : 0.5);
        }
        QAction *a = m_manager->action(QStringLiteral("view_show_canvas_only"));
        m_eye->setActive(a && a->isChecked());
    }

    void setMode(int m)
    {
        m = qBound(0, m, ModeCount - 1);
        config().writeEntry("LiquifyMode", m);
        for (int i = 0; i < ModeCount; ++i) m_modeButtons[i]->setActive(i == m);
        m_label->setText(modeName(m));
        if (QAbstractButton *b = option<QAbstractButton>(kModes[m].button)) {
            if (!b->isChecked()) b->click();
        }
        if (QCheckBox *reverse = option<QCheckBox>("liquifyReverseDirectionChk")) reverse->setChecked(kModes[m].reverse);
    }

    void place()
    {
        QWidget *w = parentWidget();
        const QRect area = m_manager->canvasArea();
        const QSize s = sizeHint().boundedTo(QSize(qMin(area.width() - 32, IkStyle::scaled(720)), w->height()));
        const QSize size(qMax(s.width(), qMin(area.width() - 32, IkStyle::scaled(560))), s.height());
        setGeometry(area.left() + (area.width() - size.width()) / 2, area.bottom() - size.height() - IkStyle::scaled(20), size.width(),
                    size.height());
        raise();
    }

private:
    void finish(bool apply)
    {
        if (QDialogButtonBox *box = option<QDialogButtonBox>("buttonBox")) {
            if (QAbstractButton *b = box->button(apply ? QDialogButtonBox::Apply : QDialogButtonBox::Reset)) b->click();
        }
        QAction *a = m_manager->action(QStringLiteral("view_show_canvas_only"));
        if (a && a->isChecked()) a->trigger();
        // Back to painting.
        m_manager->setPaintMode(IkInterfaceManager::Draw);
        m_manager->ensureBrushTool();
        IkLiquify::hide();
    }

    void showMore(QWidget *anchor)
    {
        QWidget *box = new QWidget();
        QVBoxLayout *bl = new QVBoxLayout(box);
        bl->setContentsMargins(IkStyle::scaled(12), IkStyle::scaled(6), IkStyle::scaled(12), IkStyle::scaled(6));
        bl->setSpacing(IkStyle::scaled(6));
        const struct { const char *name; QString label; } rows[] = {
            {"liquifySizeSlider", i18n("Size")},
            {"liquifySpacingSlider", i18n("Spacing")},
            {"liquifyFlowSlider", i18n("Flow")},
        };
        for (const auto &r : rows) {
            QDoubleSpinBox *spin = option<QDoubleSpinBox>(r.name);
            if (!spin) continue;
            QLabel *label = new QLabel(box);
            auto text = [label, spin, name = r.label]() { label->setText(QStringLiteral("%1   %2").arg(name, spin->text())); };
            text();
            bl->addWidget(label);
            IkSlider *slider = new IkSlider(box);
            const qreal range = spin->maximum() - spin->minimum();
            // Size spans a large range: map it on a square-root scale.
            const bool size = qstrcmp(r.name, "liquifySizeSlider") == 0;
            const qreal t = range > 0 ? (spin->value() - spin->minimum()) / range : 0;
            slider->setValue(size ? std::sqrt(t) : t);
            QPointer<QDoubleSpinBox> guard = spin;
            connect(slider, &IkSlider::valueEdited, box, [guard, size, text](qreal v) {
                if (!guard) return;
                const qreal t = size ? v * v : v;
                guard->setValue(guard->minimum() + t * (guard->maximum() - guard->minimum()));
                text();
            });
            bl->addWidget(slider);
        }
        IkPopover *pop = IkMenus::popup(box, anchor, IkPopover::Above, QSize(IkStyle::scaled(360), box->sizeHint().height() + 50));
        Q_UNUSED(pop);
    }

    IkInterfaceManager *m_manager;
    IkSlider *m_strength {nullptr};
    IkIconButton *m_eye {nullptr};
    QLabel *m_label {nullptr};
    IkIconButton *m_modeButtons[ModeCount] {};
};

QPointer<IkLiquifyBar> &bar()
{
    static QPointer<IkLiquifyBar> b;
    return b;
}
}

namespace IkLiquify
{
void show(IkInterfaceManager *manager)
{
    if (!bar() || bar()->parentWidget() != manager->mainWindow()) {
        delete bar().data();
        bar() = new IkLiquifyBar(manager);
    }
    bar()->show();
    bar()->sync();
    bar()->place();
}

void hide()
{
    if (bar()) bar()->hide();
}

bool isShown()
{
    return bar() && bar()->isVisible();
}

void raiseAndPlace()
{
    if (bar() && bar()->isVisible()) bar()->place();
}
}
