/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "IkPractice.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QRandomGenerator>
#include <QTimer>
#include <QVBoxLayout>

#include <KConfigGroup>
#include <KSharedConfig>
#include <KoColor.h>
#include <KoColorSpaceRegistry.h>
#include <KoFileDialog.h>
#include <klocalizedstring.h>

#include "KisDocument.h"
#include "KisMainWindow.h"
#include "KisPart.h"
#include "kis_config.h"

#include "IkInterfaceManager.h"
#include "IkStyle.h"
#include "IkWidgets.h"

namespace
{
enum Activity { Timed, Memory, Values, Warmup };

KConfigGroup practiceConfig()
{
    return KConfigGroup(KSharedConfig::openConfig(), "IKriatePractice");
}

QString activityTitle(Activity a)
{
    switch (a) {
    case Timed: return i18n("Timed Drawing");
    case Memory: return i18n("Memory Drawing");
    case Values: return i18n("Value Study");
    case Warmup: return i18n("Warm-up");
    }
    return QString();
}

QString activityDescription(Activity a)
{
    switch (a) {
    case Timed:
        return i18n("Your reference images, one after another, each for a set time. Great for gesture and figure drawing.");
    case Memory:
        return i18n("Study an image for a few seconds, then draw it from memory. Reveal it afterwards to compare.");
    case Values:
        return i18n("Your reference in greyscale, to practise light, shadow and value grouping.");
    case Warmup:
        return i18n("Timed prompts to loosen up: straight lines, ellipses, curves and hatching.");
    }
    return QString();
}

QString activityIcon(Activity a)
{
    switch (a) {
    case Timed: return QStringLiteral("timer");
    case Memory: return QStringLiteral("eye-closed");
    case Values: return QStringLiteral("dark");
    case Warmup: return QStringLiteral("pencil");
    }
    return QString();
}

QStringList warmupPrompts()
{
    return {i18n("Straight lines: connect pairs of dots in one confident stroke."),
            i18n("Ellipses: draw ellipses of different sizes, going around twice."),
            i18n("Boxes in perspective: one-point and two-point."),
            i18n("Curves: long S-curves and C-curves from the shoulder."),
            i18n("Hatching: even parallel lines, then a gradient from dark to light."),
            i18n("Circles: fill an area with touching circles of the same size."),
            i18n("Spirals: from the centre outwards, keeping the gaps even."),
            i18n("Contours: trace the outline of an object near you without lifting the pen.")};
}

QString formatTime(int seconds)
{
    seconds = qMax(0, seconds);
    return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

struct Settings {
    Activity activity {Timed};
    QStringList images;
    int seconds {60};
    int count {10};       ///< 0 = all
    int studySeconds {15}; ///< memory drawing: time to look
    bool shuffle {true};
    bool layerPerDrawing {true};
};

// ======================================================================
/// Floating reference panel with countdown, shown on the canvas.

class IkPracticeSession;
QPointer<IkPracticeSession> s_active;

class IkReferenceView : public QWidget
{
public:
    explicit IkReferenceView(QWidget *parent) : QWidget(parent) { setMinimumSize(IkStyle::scaled(160), IkStyle::scaled(120)); }
    void setImage(const QImage &img) { m_image = img; update(); }
    void setText(const QString &text) { m_text = text; update(); }
    void setHidden(bool hidden) { m_hiddenImage = hidden; update(); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.setRenderHint(QPainter::Antialiasing);
        const QPalette pal = palette();
        QPainterPath clip;
        clip.addRoundedRect(QRectF(rect()), 10, 10);
        p.setClipPath(clip);
        p.fillRect(rect(), IkStyle::color(IkStyle::Raised, pal));
        if (!m_image.isNull() && !m_hiddenImage) {
            const QSize s = m_image.size().scaled(size(), Qt::KeepAspectRatio);
            const QRect r(QPoint((width() - s.width()) / 2, (height() - s.height()) / 2), s);
            p.drawImage(r, m_image);
        }
        if (!m_text.isEmpty() && (m_image.isNull() || m_hiddenImage)) {
            p.setPen(IkStyle::color(IkStyle::Text, pal));
            p.setFont(IkStyle::font(1.25));
            p.drawText(rect().adjusted(20, 20, -20, -20), Qt::AlignCenter | Qt::TextWordWrap, m_text);
        }
    }

private:
    QImage m_image;
    QString m_text;
    bool m_hiddenImage {false};
};

class IkPracticeSession : public IkPanel
{
public:
    IkPracticeSession(IkInterfaceManager *manager, const Settings &settings)
        : IkPanel(manager->mainWindow())
        , m_manager(manager)
        , m_settings(settings)
    {
        setRole(IkStyle::Surface);
        setShadow(true);
        setBorder(true);
        setAttribute(Qt::WA_DeleteOnClose);

        QVBoxLayout *l = new QVBoxLayout(this);
        l->setContentsMargins(16, 12, 16, 16);
        l->setSpacing(8);

        QHBoxLayout *head = new QHBoxLayout();
        head->setSpacing(6);
        m_title = new QLabel(activityTitle(settings.activity), this);
        m_title->setFont(IkStyle::font(1.05, QFont::DemiBold));
        head->addWidget(m_title, 1);
        m_clock = new QLabel(this);
        m_clock->setFont(IkStyle::font(1.4, QFont::Bold));
        head->addWidget(m_clock);
        l->addLayout(head);

        m_progress = new QWidget(this);
        m_progress->setFixedHeight(4);
        l->addWidget(m_progress);

        m_view = new IkReferenceView(this);
        l->addWidget(m_view, 1);

        QHBoxLayout *controls = new QHBoxLayout();
        controls->setSpacing(4);
        m_counter = new QLabel(this);
        m_counter->setForegroundRole(QPalette::PlaceholderText);
        controls->addWidget(m_counter, 1);
        m_reveal = new IkIconButton(QStringLiteral("eye"), i18n("Show the image"), this);
        m_reveal->setCheckable(true);
        m_reveal->setVisible(settings.activity == Memory);
        connect(m_reveal, &QAbstractButton::toggled, this, [this](bool on) { m_view->setHidden(!on && m_phase == Drawing); });
        controls->addWidget(m_reveal);
        m_pause = new IkIconButton(QStringLiteral("pause"), i18n("Pause"), this);
        connect(m_pause, &QAbstractButton::clicked, this, [this]() { setPaused(!m_paused); });
        controls->addWidget(m_pause);
        IkIconButton *skip = new IkIconButton(QStringLiteral("skip-forward"), i18n("Next"), this);
        connect(skip, &QAbstractButton::clicked, this, [this]() { next(); });
        controls->addWidget(skip);
        m_stop = new IkIconButton(QStringLiteral("stop"), i18n("End session"), this);
        connect(m_stop, &QAbstractButton::clicked, this, [this]() { finish(); });
        controls->addWidget(m_stop);
        l->addLayout(controls);

        m_progress->installEventFilter(this);

        // Images in play order.
        m_queue = settings.images;
        if (settings.shuffle) {
            for (int i = m_queue.size() - 1; i > 0; --i) m_queue.swapItemsAt(i, QRandomGenerator::global()->bounded(i + 1));
        }
        if (settings.activity == Warmup) {
            m_queue = warmupPrompts();
        }
        if (settings.count > 0 && m_queue.size() > settings.count) m_queue = m_queue.mid(0, settings.count);

        m_tick = new QTimer(this);
        m_tick->setInterval(200);
        connect(m_tick, &QTimer::timeout, this, [this]() { tick(); });

        const QWidget *win = manager->mainWindow();
        const int w = qBound(IkStyle::scaled(260), int(win->width() * 0.26), IkStyle::scaled(480));
        resize(w, int(w * 1.05));
        move(win->width() - width() - IkStyle::scaled(24), qMin(IkStyle::touchSize() * 2 + 24, win->height() / 5));
        show();
        raise();

        s_active = this;
        start(0);
    }

    ~IkPracticeSession() override
    {
        if (s_active == this) s_active = nullptr;
    }

    void finish()
    {
        m_tick->stop();
        m_phase = Done;
        const int done = qMin(m_index + 1, m_queue.size());
        m_view->setImage(QImage());
        m_view->setText(i18np("Session complete: %1 drawing in %2.", "Session complete: %1 drawings in %2.",
                              done, formatTime(int(m_total.elapsed() / 1000))));
        m_clock->clear();
        m_counter->clear();
        m_pause->hide();
        m_reveal->hide();
        m_stop->setIconName(QStringLiteral("close"));
        m_stop->setToolTip(i18n("Close"));
        disconnect(m_stop, nullptr, this, nullptr);
        connect(m_stop, &QAbstractButton::clicked, this, [this]() { close(); });
        m_progress->update();
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_progress && event->type() == QEvent::Paint) {
            QPainter p(m_progress);
            p.setRenderHint(QPainter::Antialiasing);
            const QRectF r = QRectF(m_progress->rect());
            p.setPen(Qt::NoPen);
            p.setBrush(IkStyle::color(IkStyle::Raised, palette()));
            p.drawRoundedRect(r, 2, 2);
            if (m_phase != Done && m_duration > 0) {
                const qreal f = qBound<qreal>(0.0, 1.0 - qreal(remainingMs()) / (m_duration * 1000.0), 1.0);
                p.setBrush(IkStyle::color(IkStyle::Accent, palette()));
                p.drawRoundedRect(QRectF(r.left(), r.top(), r.width() * f, r.height()), 2, 2);
            }
            return true;
        }
        return IkPanel::eventFilter(watched, event);
    }

    // The panel can be moved by dragging its frame or header.
    void mousePressEvent(QMouseEvent *e) override
    {
        m_dragOffset = e->pos();
        m_dragging = true;
        e->accept();
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (!m_dragging || !parentWidget()) return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        const QPoint p = mapToParent(e->position().toPoint()) - m_dragOffset;
#else
        const QPoint p = mapToParent(e->pos()) - m_dragOffset;
#endif
        const QRect bounds = parentWidget()->rect();
        move(qBound(bounds.left(), p.x(), bounds.right() - width()), qBound(bounds.top(), p.y(), bounds.bottom() - height()));
    }
    void mouseReleaseEvent(QMouseEvent *) override { m_dragging = false; }

private:
    enum Phase { Studying, Drawing, Done };

    int remainingMs() const
    {
        return int(m_duration * 1000 - m_elapsedBefore - (m_paused ? 0 : m_clockTimer.elapsed()));
    }

    void setPaused(bool paused)
    {
        if (m_phase == Done || paused == m_paused) return;
        if (paused) m_elapsedBefore += m_clockTimer.elapsed();
        else m_clockTimer.restart();
        m_paused = paused;
        m_pause->setIconName(paused ? QStringLiteral("play") : QStringLiteral("pause"));
        m_pause->setToolTip(paused ? i18n("Resume") : i18n("Pause"));
    }

    void start(int index)
    {
        if (index >= m_queue.size()) {
            finish();
            return;
        }
        if (index == 0) m_total.start();
        // One layer per drawing, so a session's drawings stay apart.
        if (index > 0 && m_settings.layerPerDrawing && m_settings.activity != Warmup) {
            m_manager->triggerAction(QStringLiteral("add_new_paint_layer"));
        }
        m_index = index;
        m_counter->setText(i18nc("practice progress", "%1 of %2", index + 1, m_queue.size()));
        m_reveal->setChecked(false);

        if (m_settings.activity == Warmup) {
            m_view->setImage(QImage());
            m_view->setText(m_queue.at(index));
            beginPhase(Drawing, m_settings.seconds);
        } else {
            QImageReader reader(m_queue.at(index));
            reader.setAutoTransform(true);
            QImage img = reader.read();
            if (img.isNull()) {
                // Unreadable file: skip it.
                QTimer::singleShot(0, this, [this, index]() { start(index + 1); });
                return;
            }
            const QSize max(1600, 1600);
            if (img.width() > max.width() || img.height() > max.height()) img = img.scaled(max, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            if (m_settings.activity == Values) img = img.convertToFormat(QImage::Format_Grayscale8);
            m_view->setImage(img);
            m_view->setText(i18n("Draw it from memory. Tap the eye to compare."));
            m_view->setHidden(false);
            if (m_settings.activity == Memory) beginPhase(Studying, m_settings.studySeconds);
            else beginPhase(Drawing, m_settings.seconds);
        }
        if (m_paused) setPaused(false);
        m_tick->start();
        tick();
    }

    void beginPhase(Phase phase, int seconds)
    {
        m_phase = phase;
        m_duration = seconds;
        m_elapsedBefore = 0;
        m_clockTimer.restart();
        if (m_settings.activity == Memory) {
            m_view->setHidden(phase == Drawing);
            m_reveal->setEnabled(phase == Drawing);
            m_title->setText(phase == Studying ? i18n("Memory Drawing: study") : i18n("Memory Drawing: draw"));
        }
    }

    void next()
    {
        if (m_phase == Studying) {
            beginPhase(Drawing, m_settings.seconds);
            return;
        }
        start(m_index + 1);
    }

    void tick()
    {
        if (m_phase == Done) return;
        const int ms = remainingMs();
        m_clock->setText(formatTime((ms + 999) / 1000));
        m_progress->update();
        if (ms <= 0 && !m_paused) next();
    }

    IkInterfaceManager *m_manager;
    Settings m_settings;
    QStringList m_queue;
    int m_index {0};
    Phase m_phase {Drawing};
    int m_duration {60};
    qint64 m_elapsedBefore {0};
    QElapsedTimer m_clockTimer;
    QElapsedTimer m_total;
    bool m_paused {false};
    QTimer *m_tick {nullptr};
    QLabel *m_title {nullptr};
    QLabel *m_clock {nullptr};
    QLabel *m_counter {nullptr};
    QWidget *m_progress {nullptr};
    IkReferenceView *m_view {nullptr};
    IkIconButton *m_pause {nullptr};
    IkIconButton *m_reveal {nullptr};
    IkIconButton *m_stop {nullptr};
    QPoint m_dragOffset;
    bool m_dragging {false};
};

// ======================================================================
/// Opens a new canvas with the user's default size, then runs @p settings.

void startSession(IkInterfaceManager *manager, const Settings &settings)
{
    KisMainWindow *mw = manager->mainWindow();
    if (!mw->activeView()) {
        KisConfig cfg(true);
        const KoColorSpace *cs = KoColorSpaceRegistry::instance()->colorSpace(cfg.defColorModel(), cfg.defaultColorDepth(),
                                                                               cfg.defColorProfile());
        if (!cs) cs = KoColorSpaceRegistry::instance()->rgb8();
        KisDocument *doc = KisPart::instance()->createDocument();
        doc->newImage(activityTitle(settings.activity), cfg.defImageWidth(), cfg.defImageHeight(), cs,
                      KoColor(Qt::white, cs), KisConfig::RASTER_LAYER, 1, QString(), cfg.defImageResolution());
        KisPart::instance()->startCustomDocument(doc);
    }
    QPointer<IkInterfaceManager> m = manager;
    // Let the canvas and its layout appear first.
    QTimer::singleShot(400, manager, [m, settings]() {
        if (!m) return;
        if (s_active) s_active->close();
        new IkPracticeSession(m, settings);
    });
}

// ======================================================================
/// Options for one activity: images, time per drawing, count, shuffle.

QWidget *sheetTitle(const QString &title, QWidget *parent, std::function<void()> onClose)
{
    QWidget *bar = new QWidget(parent);
    QHBoxLayout *l = new QHBoxLayout(bar);
    l->setContentsMargins(16, 6, 6, 6);
    l->addSpacing(36);
    QLabel *t = new QLabel(title, bar);
    t->setAlignment(Qt::AlignCenter);
    t->setFont(IkStyle::font(1.45));
    l->addWidget(t, 1);
    IkIconButton *close = new IkIconButton(QStringLiteral("close"), i18n("Close"), bar);
    QObject::connect(close, &QAbstractButton::clicked, bar, onClose);
    l->addWidget(close);
    return bar;
}

void showSetup(IkInterfaceManager *manager, Activity activity)
{
    KisMainWindow *mw = manager->mainWindow();
    IkPopover *pop = new IkPopover(mw);
    pop->setDimBackground(true);
    QObject::connect(pop, &IkPopover::dismissed, pop, &QObject::deleteLater);

    QWidget *sheet = new QWidget(pop);
    QVBoxLayout *l = new QVBoxLayout(sheet);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(0);
    l->addWidget(sheetTitle(activityTitle(activity), sheet, [pop]() { pop->dismiss(); }));
    l->addWidget(new IkDivider(sheet));

    QWidget *body = new QWidget(sheet);
    QVBoxLayout *b = new QVBoxLayout(body);
    b->setContentsMargins(18, 16, 18, 16);
    b->setSpacing(10);
    QLabel *desc = new QLabel(activityDescription(activity), body);
    desc->setWordWrap(true);
    desc->setForegroundRole(QPalette::PlaceholderText);
    b->addWidget(desc);

    KConfigGroup cfg = practiceConfig();
    auto settings = std::make_shared<Settings>();
    settings->activity = activity;
    settings->images = cfg.readEntry("Images", QStringList());
    settings->seconds = cfg.readEntry(activity == Values ? "ValueSeconds" : activity == Warmup ? "WarmupSeconds" : "Seconds",
                                      activity == Values ? 600 : activity == Warmup ? 120 : 60);
    settings->count = cfg.readEntry("Count", 10);
    settings->studySeconds = cfg.readEntry("StudySeconds", 15);
    settings->shuffle = cfg.readEntry("Shuffle", true);
    settings->layerPerDrawing = cfg.readEntry("LayerPerDrawing", true);

    const bool needsImages = activity != Warmup;
    IkChoiceRow *images = nullptr;
    auto updateImages = [settings](IkChoiceRow *row) {
        if (!row) return;
        row->setValueText(settings->images.isEmpty() ? i18n("Choose…")
                                                     : i18np("%1 image", "%1 images", settings->images.size()));
    };
    if (needsImages) {
        images = new IkChoiceRow(i18n("Reference images"), body);
        images->setBordered(true);
        images->setMinimumHeight(IkStyle::touchSize() + 8);
        updateImages(images);
        QObject::connect(images, &QAbstractButton::clicked, images, [settings, images, updateImages, mw]() {
            KoFileDialog dlg(mw, KoFileDialog::OpenFiles, QStringLiteral("IKriatePracticeImages"));
            dlg.setCaption(i18n("Choose Reference Images"));
            dlg.setMimeTypeFilters({QStringLiteral("image/png"), QStringLiteral("image/jpeg"), QStringLiteral("image/webp"),
                                    QStringLiteral("image/gif"), QStringLiteral("image/bmp"), QStringLiteral("image/tiff")});
            const QStringList files = dlg.filenames();
            if (files.isEmpty()) return;
            settings->images = files;
            updateImages(images);
        });
        b->addWidget(images);
    }

    // Time per drawing.
    const QList<int> times = activity == Values ? QList<int>{300, 600, 900, 1200, 1800}
                           : activity == Warmup ? QList<int>{60, 120, 180, 300}
                                                : QList<int>{30, 60, 120, 300, 600};
    IkChoiceRow *time = new IkChoiceRow(activity == Memory ? i18n("Time to draw") : activity == Warmup ? i18n("Time per prompt")
                                                                                                       : i18n("Time per drawing"),
                                        body);
    time->setBordered(true);
    time->setMinimumHeight(IkStyle::touchSize() + 8);
    QStringList timeNames;
    for (int t : times) timeNames << (t < 60 ? i18nc("seconds", "%1 s", t) : i18nc("minutes", "%1 min", t / 60));
    time->setChoices(timeNames, qMax(0, times.indexOf(settings->seconds)));
    settings->seconds = times.value(qMax(0, times.indexOf(settings->seconds)));
    QObject::connect(time, &IkChoiceRow::chosen, time, [settings, times](int i) { settings->seconds = times.value(i, 60); });
    b->addWidget(time);

    if (activity == Memory) {
        const QList<int> study = {5, 10, 15, 30, 60};
        IkChoiceRow *look = new IkChoiceRow(i18n("Time to study"), body);
        look->setBordered(true);
        look->setMinimumHeight(IkStyle::touchSize() + 8);
        QStringList names;
        for (int t : study) names << i18nc("seconds", "%1 s", t);
        look->setChoices(names, qMax(0, study.indexOf(settings->studySeconds)));
        settings->studySeconds = study.value(qMax(0, study.indexOf(settings->studySeconds)));
        QObject::connect(look, &IkChoiceRow::chosen, look, [settings, study](int i) { settings->studySeconds = study.value(i, 15); });
        b->addWidget(look);
    }

    if (needsImages) {
        const QList<int> counts = {5, 10, 20, 40, 0};
        IkChoiceRow *count = new IkChoiceRow(i18n("Number of drawings"), body);
        count->setBordered(true);
        count->setMinimumHeight(IkStyle::touchSize() + 8);
        count->setChoices({QStringLiteral("5"), QStringLiteral("10"), QStringLiteral("20"), QStringLiteral("40"), i18n("All")},
                          qMax(0, counts.indexOf(settings->count)));
        settings->count = counts.value(qMax(0, counts.indexOf(settings->count)));
        QObject::connect(count, &IkChoiceRow::chosen, count, [settings, counts](int i) { settings->count = counts.value(i, 10); });
        b->addWidget(count);

        auto toggleRow = [body, b](const QString &icon, const QString &text, bool on, std::function<void(bool)> set) {
            IkNavRow *row = new IkNavRow(icon, text, body);
            IkSwitch *sw = new IkSwitch(row);
            sw->setChecked(on);
            row->setTrailing(sw);
            QObject::connect(row, &QAbstractButton::clicked, sw, [sw]() { sw->toggle(); });
            QObject::connect(sw, &QAbstractButton::toggled, sw, set);
            b->addWidget(row);
        };
        toggleRow(QStringLiteral("shuffle"), i18n("Shuffle"), settings->shuffle, [settings](bool on) { settings->shuffle = on; });
        toggleRow(QStringLiteral("layers"), i18n("New layer for each drawing"), settings->layerPerDrawing,
                  [settings](bool on) { settings->layerPerDrawing = on; });
    }
    l->addWidget(body);
    l->addWidget(new IkDivider(sheet));

    IkTextButton *startButton = new IkTextButton(i18nc("start a practice session", "START"), IkTextButton::Plain, sheet);
    startButton->setFontScale(1.25, QFont::Bold);
    startButton->setMinimumHeight(IkStyle::touchSize() + 12);
    l->addWidget(startButton);
    QPointer<IkInterfaceManager> m = manager;
    QObject::connect(startButton, &QAbstractButton::clicked, pop, [m, pop, settings, images, needsImages]() {
        if (needsImages && settings->images.isEmpty()) {
            if (images) images->click();
            if (settings->images.isEmpty()) return;
        }
        KConfigGroup c = practiceConfig();
        c.writeEntry("Images", settings->images);
        c.writeEntry(settings->activity == Values ? "ValueSeconds" : settings->activity == Warmup ? "WarmupSeconds" : "Seconds",
                     settings->seconds);
        c.writeEntry("Count", settings->count);
        c.writeEntry("StudySeconds", settings->studySeconds);
        c.writeEntry("Shuffle", settings->shuffle);
        c.writeEntry("LayerPerDrawing", settings->layerPerDrawing);
        pop->dismiss();
        if (m) startSession(m, *settings);
    });

    pop->setContent(sheet);
    pop->popup(nullptr, IkPopover::Center, QSize(IkStyle::scaled(460), pop->sizeHint().height()));
}
}

namespace IkPractice
{
void showMenu(IkInterfaceManager *manager, QWidget *anchor)
{
    KisMainWindow *mw = manager->mainWindow();
    IkPopover *pop = new IkPopover(mw);
    QObject::connect(pop, &IkPopover::dismissed, pop, &QObject::deleteLater);
    QWidget *menu = new QWidget(pop);
    QVBoxLayout *l = new QVBoxLayout(menu);
    l->setContentsMargins(4, 4, 4, 4);
    l->setSpacing(2);
    QLabel *title = new QLabel(i18n("Practice"), menu);
    title->setFont(IkStyle::font(1.2, QFont::DemiBold));
    title->setContentsMargins(12, 6, 12, 6);
    l->addWidget(title);
    QPointer<IkInterfaceManager> m = manager;
    for (Activity a : {Timed, Memory, Values, Warmup}) {
        IkNavRow *row = new IkNavRow(activityIcon(a), activityTitle(a), menu);
        row->setToolTip(activityDescription(a));
        QObject::connect(row, &QAbstractButton::clicked, pop, [m, pop, a]() {
            pop->dismiss();
            if (m) showSetup(m, a);
        });
        l->addWidget(row);
    }
    pop->setContent(menu);
    pop->popup(anchor, IkPopover::Above, QSize(IkStyle::scaled(300), pop->sizeHint().height()));
}

void raiseActive()
{
    if (s_active) s_active->raise();
}

void stopActive()
{
    if (s_active) s_active->close();
}
}
