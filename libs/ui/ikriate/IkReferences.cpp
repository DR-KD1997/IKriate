/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "IkReferences.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QStandardPaths>
#include <QVBoxLayout>

#include <KConfigGroup>
#include <KSharedConfig>
#include <KoFileDialog.h>
#include <klocalizedstring.h>

#include "KisDocument.h"
#include "KisMainWindow.h"
#include "KisReferenceImage.h"
#include "KisView.h"
#include "KisViewManager.h"
#include "flake/KisReferenceImagesLayer.h"
#include "kis_canvas2.h"
#include "kis_coordinates_converter.h"

#include "IkInterfaceManager.h"
#include "IkMenus.h"
#include "IkStyle.h"
#include "IkWidgets.h"

namespace
{
KConfigGroup refConfig()
{
    return KConfigGroup(KSharedConfig::openConfig(), "IKriateReferences");
}

/// A reference image floating over the interface.
class IkFloatingReference : public IkPanel
{
public:
    IkFloatingReference(const QString &path, QWidget *window)
        : IkPanel(window)
        , m_path(path)
    {
        setRole(IkStyle::Surface);
        setRadius(14);
        setShadow(true);
        setAttribute(Qt::WA_AcceptTouchEvents, false);
        QImageReader reader(path);
        reader.setAutoTransform(true);
        m_image = reader.read();
        QVBoxLayout *l = new QVBoxLayout(this);
        l->setContentsMargins(14, 8, 14, 18);
        l->setSpacing(4);
        QHBoxLayout *bar = new QHBoxLayout();
        bar->setSpacing(6);
        QLabel *grip = new QLabel(this);
        grip->setPixmap(IkStyle::pixmap(QStringLiteral("grip"), IkStyle::scaled(18), IkStyle::color(IkStyle::MutedText), devicePixelRatioF()));
        bar->addWidget(grip);
        QLabel *name = new QLabel(QFileInfo(path).completeBaseName(), this);
        name->setFont(IkStyle::font(0.9, QFont::DemiBold));
        name->setForegroundRole(QPalette::PlaceholderText);
        bar->addWidget(name, 1);
        IkIconButton *close = new IkIconButton(QStringLiteral("close"), i18n("Remove reference"), this);
        close->setEdge(IkStyle::scaled(30));
        QObject::connect(close, &QAbstractButton::clicked, this, [this]() {
            QStringList paths = refConfig().readEntry("Floating", QStringList());
            paths.removeAll(m_path);
            refConfig().writeEntry("Floating", paths);
            deleteLater();
        });
        bar->addWidget(close);
        l->addLayout(bar);
        l->addStretch(1);
        m_barHeight = IkStyle::scaled(38);

        const QSize saved = refConfig().readEntry(QStringLiteral("Size:") + path, QSize());
        QSize s = saved.isValid() ? saved : m_image.size().scaled(QSize(IkStyle::scaled(320), IkStyle::scaled(320)), Qt::KeepAspectRatio);
        s += QSize(28, m_barHeight + 26);
        resize(s.expandedTo(QSize(IkStyle::scaled(160), IkStyle::scaled(140))));
        const QPoint pos = refConfig().readEntry(QStringLiteral("Pos:") + path, QPoint(-1, -1));
        move(pos.x() >= 0 ? pos : QPoint(window->width() - width() - IkStyle::scaled(40), IkStyle::scaled(120)));
        setCursor(Qt::OpenHandCursor);
    }

    QString path() const { return m_path; }

protected:
    void paintEvent(QPaintEvent *e) override
    {
        IkPanel::paintEvent(e);
        if (m_image.isNull()) return;
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const QRect area = imageArea();
        QSize s = m_image.size().scaled(area.size(), Qt::KeepAspectRatio);
        QRect r(QPoint(), s);
        r.moveCenter(area.center());
        p.drawImage(r, m_image);
        // Resize handle, bottom right.
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(IkStyle::color(IkStyle::MutedText, palette()), 2, Qt::SolidLine, Qt::RoundCap));
        const QPointF c = QPointF(width() - 14, height() - 16);
        p.drawLine(c + QPointF(-12, 0), c + QPointF(0, -12));
        p.drawLine(c + QPointF(-6, 0), c + QPointF(0, -6));
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        m_press = e->globalPos();
        m_startGeometry = geometry();
        m_resizing = QRect(width() - 40, height() - 40, 40, 40).contains(e->pos());
        m_dragging = true;
        raise();
    }

    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (!m_dragging) return;
        const QPoint delta = e->globalPos() - m_press;
        QWidget *w = parentWidget();
        if (m_resizing) {
            const QSize s = (m_startGeometry.size() + QSize(delta.x(), delta.y()))
                                .expandedTo(QSize(IkStyle::scaled(140), IkStyle::scaled(120)))
                                .boundedTo(w->size());
            resize(s);
        } else {
            QPoint pos = m_startGeometry.topLeft() + delta;
            pos.setX(qBound(-width() / 2, pos.x(), w->width() - width() / 2));
            pos.setY(qBound(0, pos.y(), w->height() - m_barHeight));
            move(pos);
        }
    }

    void mouseReleaseEvent(QMouseEvent *) override
    {
        m_dragging = false;
        KConfigGroup g = refConfig();
        g.writeEntry(QStringLiteral("Pos:") + m_path, pos());
        g.writeEntry(QStringLiteral("Size:") + m_path, imageArea().size());
    }

private:
    QRect imageArea() const { return rect().adjusted(14, m_barHeight + 4, -14, -18); }

    QString m_path;
    QImage m_image;
    int m_barHeight {38};
    QPoint m_press;
    QRect m_startGeometry;
    bool m_dragging {false};
    bool m_resizing {false};
};

QList<QPointer<IkFloatingReference>> &floating()
{
    static QList<QPointer<IkFloatingReference>> list;
    return list;
}

void showFloating(IkInterfaceManager *m, const QString &path)
{
    for (const auto &f : floating()) {
        if (f && f->path() == path) {
            f->show();
            f->raise();
            return;
        }
    }
    IkFloatingReference *f = new IkFloatingReference(path, m->mainWindow());
    floating() << f;
    f->show();
    f->raise();
}

KisReferenceImagesLayer *canvasLayer(IkInterfaceManager *m)
{
    KisView *view = m->mainWindow() ? m->mainWindow()->activeView() : nullptr;
    if (!view || !view->document()) return nullptr;
    return view->document()->referenceImagesLayer().data();
}
}

namespace IkReferences
{
Placement placement()
{
    return Placement(refConfig().readEntry("Placement", int(OnCanvas)));
}

void setPlacement(Placement p)
{
    refConfig().writeEntry("Placement", int(p));
}

int count(IkInterfaceManager *m)
{
    int n = 0;
    for (const auto &f : floating()) n += f ? 1 : 0;
    if (KisReferenceImagesLayer *layer = canvasLayer(m)) n += layer->shapes().size();
    return n;
}

void addImage(IkInterfaceManager *m)
{
    KisMainWindow *mw = m->mainWindow();
    KisViewManager *vm = m->viewManager();
    if (!mw || !vm) return;
    KoFileDialog dialog(mw, KoFileDialog::OpenFile, QStringLiteral("OpenReferenceImage"));
    dialog.setCaption(i18n("Select a Reference Image"));
    const QStringList locations = QStandardPaths::standardLocations(QStandardPaths::PicturesLocation);
    if (!locations.isEmpty()) dialog.setDefaultDir(locations.first());
    dialog.setImageFilters();
    const QString file = dialog.filename();
    if (file.isEmpty()) return;

    if (placement() == Floating) {
        QStringList paths = refConfig().readEntry("Floating", QStringList());
        if (!paths.contains(file)) paths << file;
        refConfig().writeEntry("Floating", paths);
        showFloating(m, file);
        return;
    }
    KisCanvas2 *canvas = vm->canvasBase();
    KisView *view = mw->activeView();
    if (!canvas || !view || !view->document()) return;
    KisReferenceImage *reference = KisReferenceImage::fromFile(file, *canvas->coordinatesConverter(), canvas->canvasWidget());
    if (!reference) return;
    KisDocument *doc = view->document();
    if (doc->referenceImagesLayer()) reference->setZIndex(doc->referenceImagesLayer()->shapes().size());
    canvas->addCommand(KisReferenceImagesLayer::addReferenceImages(doc, {reference}));
}

void buttonClicked(IkInterfaceManager *m, QWidget *anchor)
{
    if (count(m) == 0) {
        addImage(m);
        return;
    }
    KisMainWindow *mw = m->mainWindow();
    IkActionGrid *grid = new IkActionGrid(i18n("Reference Images"), nullptr, 4);
    QPointer<IkPopover> pop = IkMenus::create(mw);
    grid->onTriggered = [pop]() { if (pop) pop->dismiss(); };

    IkSegmented *where = new IkSegmented();
    where->addSegment(QStringLiteral("picture"), i18n("On the Canvas"));
    where->addSegment(QStringLiteral("pin"), i18n("Floating"));
    where->setCurrentIndex(int(placement()));
    QObject::connect(where, &IkSegmented::currentChanged, where, [](int i) { setPlacement(Placement(i)); });
    grid->addHeaderWidget(where);

    grid->addItem(QStringLiteral("plus"), i18n("Add"), [m]() { QMetaObject::invokeMethod(m, [m]() { addImage(m); }, Qt::QueuedConnection); });
    KisReferenceImagesLayer *layer = canvasLayer(m);
    const bool onCanvas = layer && !layer->shapes().isEmpty();
    grid->addItem(QStringLiteral("transform-basic"), i18n("Adjust"), [m]() { m->activateTool(QStringLiteral("ToolReferenceImages")); }, onCanvas);
    const bool shown = onCanvas ? layer->visible() : true;
    grid->addItem(shown ? QStringLiteral("eye-off") : QStringLiteral("eye"), shown ? i18n("Hide") : i18n("Show"), [m, shown]() {
        if (KisReferenceImagesLayer *l = canvasLayer(m)) {
            l->setVisible(!shown);
            l->setDirty();
        }
        for (const auto &f : floating()) {
            if (f) f->setVisible(!shown);
        }
    });
    grid->addItem(QStringLiteral("trash"), i18n("Clear"), [m]() {
        KisViewManager *vm = m->viewManager();
        KisView *view = m->mainWindow() ? m->mainWindow()->activeView() : nullptr;
        if (KisReferenceImagesLayer *l = canvasLayer(m)) {
            if (vm && vm->canvasBase() && view) vm->canvasBase()->addCommand(l->removeReferenceImages(view->document(), l->shapes()));
        }
        for (const auto &f : floating()) delete f.data();
        floating().clear();
        refConfig().writeEntry("Floating", QStringList());
    });
    pop->setContent(grid);
    pop->popup(anchor, IkPopover::Below);
}

void setFloatingVisible(IkInterfaceManager *m, bool visible)
{
    if (visible) {
        // Bring back the ones from last time.
        for (const QString &path : refConfig().readEntry("Floating", QStringList())) {
            if (QFileInfo::exists(path)) showFloating(m, path);
        }
    } else {
        for (const auto &f : floating()) {
            if (f) f->hide();
        }
    }
}

void raiseFloating()
{
    for (const auto &f : floating()) {
        if (f && f->isVisible()) f->raise();
    }
}
}
