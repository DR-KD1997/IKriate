/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "IkOnlinePage.h"

#include "IkOnlineCommon.h"
#include "IkSession.h"

#include "libshared/listings/announcementapi.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QScrollArea>
#include <QScroller>
#include <QTimer>
#include <QVBoxLayout>

#include <klocalizedstring.h>

#include <KisDocument.h>
#include <KisMainWindow.h>
#include <KisView.h>
#include <kis_canvas2.h>
#include <kis_coordinates_converter.h>
#include <kis_image.h>
#include <KoCanvasBase.h>
#include "utils/KisRecentDocumentsModelWrapper.h"

#include "../IkMenus.h"
#include "../IkStyle.h"
#include "../IkWidgets.h"

namespace
{
QLabel *label(const QString &text, qreal scale, QWidget *parent, int weight = -1, bool muted = false)
{
    QLabel *l = new QLabel(text, parent);
    l->setFont(IkStyle::font(scale, weight));
    l->setWordWrap(true);
    if (muted) l->setForegroundRole(QPalette::PlaceholderText);
    return l;
}

IkTextButton *button(const QString &text, bool filled, QWidget *parent)
{
    IkTextButton *b = new IkTextButton(text, filled ? IkTextButton::Filled : IkTextButton::Plain, parent);
    b->setRadius(IkStyle::scaled(10));
    b->setFontScale(1.1, QFont::DemiBold);
    b->setMinimumHeight(IkStyle::touchSize());
    b->setMinimumWidth(IkStyle::scaled(92));
    return b;
}

IkPanel *card(QWidget *parent)
{
    IkPanel *p = new IkPanel(parent);
    p->setRole(IkStyle::Surface);
    p->setRadius(IkStyle::scaled(18));
    return p;
}

QString relativeTime(const QDateTime &when)
{
    if (!when.isValid()) return QString();
    const qint64 s = when.secsTo(QDateTime::currentDateTime());
    if (s < 3600) return i18n("%1 min ago", qMax<qint64>(1, s / 60));
    if (s < 86400) return i18n("%1 h ago", s / 3600);
    return QLocale().toString(when.date(), QLocale::ShortFormat);
}

/// Round coloured badge with an initial (people, chat).
class IkAvatar : public QWidget
{
public:
    IkAvatar(int userId, const QString &name, int size, QWidget *parent)
        : QWidget(parent), m_id(userId), m_name(name)
    {
        setFixedSize(size, size);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(IkSession::colorFor(m_id));
        p.drawEllipse(rect().adjusted(1, 1, -1, -1));
        p.setPen(QColor(27, 22, 34));
        p.setFont(IkStyle::font(height() / 28.0, QFont::Bold));
        p.drawText(rect(), Qt::AlignCenter, m_name.left(1).toUpper());
    }

private:
    int m_id;
    QString m_name;
};
}

// ================================================================ the page

struct IkOnlinePage::Private {
    QPointer<KisMainWindow> mw;
    QLineEdit *joinField {nullptr};
    QWidget *activeCard {nullptr};
    QLabel *activeTitle {nullptr};
    QLabel *activeInfo {nullptr};
    QWidget *joinCard {nullptr};
    QVBoxLayout *recentList {nullptr};
    QVBoxLayout *publicList {nullptr};
    QLabel *publicStatus {nullptr};
    IkTextButton *nameButton {nullptr};
    QElapsedTimer lastFetch;
    QPointer<sessionlisting::AnnouncementApiResponse> pending;
};

IkOnlinePage::IkOnlinePage(KisMainWindow *mainWindow, QWidget *parent)
    : QWidget(parent)
    , d(new Private)
{
    d->mw = mainWindow;
    IkSession::instance()->setMainWindow(mainWindow);
    QVBoxLayout *l = new QVBoxLayout(this);
    l->setContentsMargins(40, 26, 40, 24);
    l->setSpacing(14);

    QWidget *top = new QWidget(this);
    QGridLayout *tl = new QGridLayout(top);
    tl->setContentsMargins(0, 0, 0, 0);
    IkSectionHeader *header = new IkSectionHeader(i18nc("home tab title", "ONLINE"), top);
    header->setLarge(true);
    tl->addWidget(header, 0, 0);
    d->nameButton = new IkTextButton(QString(), IkTextButton::Link, top);
    d->nameButton->setFontScale(1.05);
    tl->addWidget(d->nameButton, 0, 0, Qt::AlignRight | Qt::AlignTop);
    l->addWidget(top);
    connect(d->nameButton, &QAbstractButton::clicked, this, [this]() {
        ikAskInPopover(window(), i18n("Your name"), i18n("This is how others see you in sessions."), i18n("Name"), false,
                       i18n("Save"),
                       [this](const QString &name) {
                           IkSession::setDisplayName(name);
                           d->nameButton->setText(i18n("Shown to others as %1", IkSession::displayName()));
                       },
                       {}, IkSession::displayName());
    });

    QHBoxLayout *cols = new QHBoxLayout();
    cols->setSpacing(24);
    l->addLayout(cols, 1);
    QVBoxLayout *left = new QVBoxLayout();
    left->setSpacing(16);
    cols->addLayout(left, 0);

    // ---- in a session
    IkPanel *active = card(this);
    d->activeCard = active;
    {
        QVBoxLayout *al = new QVBoxLayout(active);
        al->setContentsMargins(22, 20, 22, 20);
        al->setSpacing(10);
        d->activeTitle = label(QString(), 1.35, active, QFont::Bold);
        d->activeInfo = label(QString(), 1.0, active, -1, true);
        al->addWidget(d->activeTitle);
        al->addWidget(d->activeInfo);
        QHBoxLayout *row = new QHBoxLayout();
        IkTextButton *back = button(i18n("Back to the Canvas"), true, active);
        IkTextButton *leave = button(i18n("Leave"), false, active);
        row->addWidget(back, 1);
        row->addWidget(leave);
        al->addLayout(row);
        connect(back, &QAbstractButton::clicked, IkSession::instance(), &IkSession::showCanvas);
        connect(leave, &QAbstractButton::clicked, IkSession::instance(), &IkSession::leave);
    }
    left->addWidget(active);

    // ---- join / host
    IkPanel *join = card(this);
    d->joinCard = join;
    {
        QVBoxLayout *jl = new QVBoxLayout(join);
        jl->setContentsMargins(22, 20, 22, 20);
        jl->setSpacing(10);
        jl->addWidget(label(i18n("Join a session"), 1.3, join, QFont::DemiBold));
        jl->addWidget(label(i18n("Paste an invite link, or a server address with the session after a slash."), 0.95, join, -1, true));
        QHBoxLayout *row = new QHBoxLayout();
        d->joinField = new QLineEdit(join);
        d->joinField->setPlaceholderText(QStringLiteral("drawpile://server/session"));
        d->joinField->setMinimumHeight(IkStyle::touchSize());
        d->joinField->setFont(IkStyle::font(1.1));
        IkTextButton *go = button(i18n("Join"), true, join);
        row->addWidget(d->joinField, 1);
        row->addWidget(go);
        jl->addLayout(row);
        QHBoxLayout *row2 = new QHBoxLayout();
        IkTextButton *paste = button(i18n("Paste Link"), false, join);
        IkTextButton *host = button(i18n("Host a Session"), false, join);
        row2->addWidget(paste, 1);
        row2->addWidget(host, 1);
        jl->addLayout(row2);
        connect(go, &QAbstractButton::clicked, this, [this]() { IkSession::instance()->join(d->joinField->text()); });
        connect(d->joinField, &QLineEdit::returnPressed, go, &QAbstractButton::click);
        connect(paste, &QAbstractButton::clicked, this, [this]() {
            d->joinField->setText(QApplication::clipboard()->text().trimmed());
        });
        connect(host, &QAbstractButton::clicked, this, [this]() { IkHostDialog::show(d->mw, nullptr); });
    }
    left->addWidget(join);

    // ---- recent
    IkPanel *recent = card(this);
    {
        QVBoxLayout *rl = new QVBoxLayout(recent);
        rl->setContentsMargins(22, 18, 22, 18);
        rl->setSpacing(10);
        rl->addWidget(label(i18n("Recent sessions"), 1.2, recent, QFont::DemiBold));
        d->recentList = new QVBoxLayout();
        d->recentList->setSpacing(8);
        rl->addLayout(d->recentList);
        rl->addStretch(1);
    }
    left->addWidget(recent, 1);
    for (QWidget *w : {static_cast<QWidget *>(active), static_cast<QWidget *>(join), static_cast<QWidget *>(recent)}) {
        w->setFixedWidth(IkStyle::scaled(440));
    }

    // ---- public sessions
    IkPanel *pub = card(this);
    {
        QVBoxLayout *pl = new QVBoxLayout(pub);
        pl->setContentsMargins(22, 18, 22, 18);
        pl->setSpacing(10);
        QHBoxLayout *head = new QHBoxLayout();
        head->addWidget(label(i18n("Public sessions"), 1.3, pub, QFont::DemiBold), 1);
        IkIconButton *refresh = new IkIconButton(QStringLiteral("refresh"), i18n("Refresh"), pub);
        refresh->setEdge(IkStyle::touchSize());
        head->addWidget(refresh);
        pl->addLayout(head);
        pl->addWidget(label(i18n("From Drawpile's public list. Anyone can join these; be kind."), 0.95, pub, -1, true));
        d->publicStatus = label(QString(), 1.0, pub, -1, true);
        pl->addWidget(d->publicStatus);
        QScrollArea *scroll = new QScrollArea(pub);
        scroll->setWidgetResizable(true);
        IkStyle::makeTransparent(scroll);
        QWidget *inner = new QWidget(scroll);
        d->publicList = new QVBoxLayout(inner);
        d->publicList->setContentsMargins(0, 0, 0, 0);
        d->publicList->setSpacing(8);
        d->publicList->addStretch(1);
        scroll->setWidget(inner);
        QScroller::grabGesture(scroll->viewport(), IkStyle::touchFirst() ? QScroller::LeftMouseButtonGesture : QScroller::TouchGesture);
        pl->addWidget(scroll, 1);
        connect(refresh, &QAbstractButton::clicked, this, [this]() {
            d->lastFetch.invalidate();
            activate();
        });
    }
    cols->addWidget(pub, 1);

    auto refreshState = [this]() {
        IkSession *s = IkSession::instance();
        const bool on = s->isActive();
        d->activeCard->setVisible(on);
        d->joinCard->setVisible(!on);
        if (on) {
            d->activeTitle->setText(s->title());
            d->activeInfo->setText(s->state() == IkSession::Connecting
                                       ? i18n("Connecting to %1…", s->serverName())
                                       : i18np("%2 · 1 person", "%2 · %1 people", s->people().size(), s->serverName()));
        }
        d->nameButton->setText(i18n("Shown to others as %1", IkSession::displayName()));
        // Recent sessions
        while (QLayoutItem *it = d->recentList->takeAt(0)) {
            delete it->widget();
            delete it;
        }
        const QVector<IkSession::RecentSession> recent = IkSession::recentSessions();
        if (recent.isEmpty()) {
            d->recentList->addWidget(label(i18n("Sessions you join or host appear here."), 1.0, this, -1, true));
        }
        for (const IkSession::RecentSession &r : recent) {
            QWidget *row = new QWidget(this);
            QHBoxLayout *rl = new QHBoxLayout(row);
            rl->setContentsMargins(0, 0, 0, 0);
            QVBoxLayout *text = new QVBoxLayout();
            text->setSpacing(0);
            text->addWidget(label(r.title.isEmpty() ? r.address : r.title, 1.05, row, QFont::DemiBold));
            text->addWidget(label((r.hosted ? i18n("You hosted · %1", relativeTime(r.when)) : relativeTime(r.when)), 0.9, row, -1, true));
            rl->addLayout(text, 1);
            if (!r.hosted) {
                IkTextButton *again = button(i18n("Rejoin"), false, row);
                const QString address = r.address;
                connect(again, &QAbstractButton::clicked, IkSession::instance(), [address]() { IkSession::instance()->join(address); });
                again->setEnabled(!on);
                rl->addWidget(again);
            }
            d->recentList->addWidget(row);
        }
    };
    connect(IkSession::instance(), &IkSession::stateChanged, this, refreshState);
    connect(IkSession::instance(), &IkSession::titleChanged, this, refreshState);
    connect(IkSession::instance(), &IkSession::peopleChanged, this, refreshState);
    connect(IkSession::instance(), &IkSession::notice, this, [this](const QString &message) {
        if (isVisible()) ikAskInPopover(window(), i18n("Online"), message, QString(), false, i18n("OK"), {}, {});
    });
    refreshState();
}

IkOnlinePage::~IkOnlinePage()
{
    delete d;
}

void IkOnlinePage::paintEvent(QPaintEvent *)
{
}

void IkOnlinePage::activate()
{
    if (d->lastFetch.isValid() && d->lastFetch.elapsed() < 60000) return;
    if (d->pending) return;
    d->lastFetch.start();
    d->publicStatus->setText(i18n("Loading…"));
    d->publicStatus->show();
    sessionlisting::AnnouncementApiResponse *r = sessionlisting::getSessionList(IkSession::publicListingUrl());
    d->pending = r;
    connect(r, &sessionlisting::AnnouncementApiResponse::finished, this,
            [this, r](const QVariant &result, const QString &, const QString &error) {
                r->deleteLater();
                while (d->publicList->count() > 1) {
                    QLayoutItem *it = d->publicList->takeAt(0);
                    delete it->widget();
                    delete it;
                }
                if (!error.isEmpty()) {
                    d->publicStatus->setText(i18n("The public list could not be loaded: %1", error));
                    return;
                }
                const QList<sessionlisting::Session> sessions = result.value<QList<sessionlisting::Session>>();
                int shown = 0;
                for (const sessionlisting::Session &s : sessions) {
                    // Sessions marked as not suitable for minors are not listed.
                    if (s.nsfm || s.closed) continue;
                    QWidget *row = new IkPanel(this);
                    static_cast<IkPanel *>(row)->setRole(IkStyle::Raised);
                    static_cast<IkPanel *>(row)->setRadius(IkStyle::scaled(14));
                    QHBoxLayout *rl = new QHBoxLayout(row);
                    rl->setContentsMargins(14, 10, 10, 10);
                    QVBoxLayout *text = new QVBoxLayout();
                    text->setSpacing(1);
                    text->addWidget(label(s.title.isEmpty() ? i18n("Untitled session") : s.title, 1.05, row, QFont::DemiBold));
                    QString meta = i18nc("session owner, people", "%1 · %2", s.owner,
                                         s.maxUsers > 0 ? QStringLiteral("%1/%2").arg(s.users).arg(s.maxUsers) : QString::number(s.users));
                    if (s.password) meta += i18n(" · password");
                    text->addWidget(label(meta, 0.9, row, -1, true));
                    rl->addLayout(text, 1);
                    IkTextButton *go = button(i18n("Join"), false, row);
                    QUrl url;
                    url.setScheme(QStringLiteral("drawpile"));
                    url.setHost(s.host);
                    if (s.port > 0 && s.port != 27750) url.setPort(s.port);
                    url.setPath(QLatin1Char('/') + s.id);
                    const QString link = url.toString();
                    connect(go, &QAbstractButton::clicked, IkSession::instance(), [link]() { IkSession::instance()->join(link); });
                    rl->addWidget(go);
                    d->publicList->insertWidget(d->publicList->count() - 1, row);
                    ++shown;
                }
                d->publicStatus->setText(shown ? QString() : i18n("No public sessions right now."));
                d->publicStatus->setVisible(!shown);
            });
}

// ============================================================ host dialog

void IkHostDialog::show(KisMainWindow *mw, KisDocument *source)
{
    if (!mw) return;
    IkSession::instance()->setMainWindow(mw);
    IkPopover *pop = new IkPopover(mw);
    pop->setDimBackground(true);
    pop->setCloseButton(true);
    pop->setAccentOutline(true);
    QWidget *w = new QWidget();
    w->setMinimumWidth(IkStyle::scaled(560));
    QVBoxLayout *l = new QVBoxLayout(w);
    l->setContentsMargins(IkStyle::scaled(8), 0, IkStyle::scaled(8), IkStyle::scaled(8));
    l->setSpacing(IkStyle::scaled(12));
    l->addWidget(label(i18n("Host a session"), 1.6, w, QFont::Bold));

    // Artwork
    QComboBox *art = nullptr;
    QString sourceName;
    if (source) {
        sourceName = source->path().isEmpty() ? i18n("This artwork") : QFileInfo(source->path()).completeBaseName();
        l->addWidget(label(i18n("Sharing a copy of “%1”; the file itself is not changed.", sourceName), 1.0, w, -1, true));
    } else {
        l->addWidget(label(i18n("Artwork"), 0.95, w, -1, true));
        art = new QComboBox(w);
        art->setMinimumHeight(IkStyle::touchSize());
        art->setFont(IkStyle::font(1.1));
        art->addItem(i18n("A new blank canvas"), QString());
        QAbstractItemModel *recent = &KisRecentDocumentsModelWrapper::instance()->model();
        for (int r = 0; r < recent->rowCount(); ++r) {
            const QUrl url = recent->index(r, 0).data(Qt::UserRole + 1).toUrl();
            if (url.isLocalFile() && QFileInfo::exists(url.toLocalFile())) {
                art->addItem(QFileInfo(url.toLocalFile()).completeBaseName(), url.toLocalFile());
            }
        }
        l->addWidget(art);
    }

    l->addWidget(label(i18n("Session title"), 0.95, w, -1, true));
    QLineEdit *title = new QLineEdit(w);
    title->setMinimumHeight(IkStyle::touchSize());
    title->setFont(IkStyle::font(1.1));
    title->setText(source ? sourceName : i18n("%1's session", IkSession::displayName()));
    l->addWidget(title);

    l->addWidget(label(i18n("Where it runs"), 0.95, w, -1, true));
    IkSegmented *where = new IkSegmented(w);
    where->addSegment(QStringLiteral("system"), i18n("This Device"));
    where->addSegment(QStringLiteral("globe"), i18n("Public Server"));
    where->addSegment(QStringLiteral("server"), i18n("Own Server"));
    l->addWidget(where);
    QLabel *whereHint = label(QString(), 0.92, w, -1, true);
    l->addWidget(whereHint);
    QLineEdit *address = new QLineEdit(w);
    address->setPlaceholderText(i18n("Server address, e.g. example.com:27750"));
    address->setMinimumHeight(IkStyle::touchSize());
    l->addWidget(address);

    QHBoxLayout *row = new QHBoxLayout();
    QVBoxLayout *listCol = new QVBoxLayout();
    listCol->addWidget(label(i18n("Who can find it"), 0.95, w, -1, true));
    IkSegmented *listed = new IkSegmented(w);
    listed->addSegment(QStringLiteral("link"), i18n("Invite Only"));
    listed->addSegment(QStringLiteral("users"), i18n("Public List"));
    listCol->addWidget(listed);
    row->addLayout(listCol, 1);
    QVBoxLayout *pwCol = new QVBoxLayout();
    pwCol->addWidget(label(i18n("Password (optional)"), 0.95, w, -1, true));
    QLineEdit *password = new QLineEdit(w);
    password->setMinimumHeight(IkStyle::touchSize());
    password->setEchoMode(QLineEdit::Password);
    pwCol->addWidget(password);
    row->addLayout(pwCol, 1);
    l->addLayout(row);

    l->addWidget(label(i18n("The shared copy is 8-bit RGBA with paint layers and groups: filter, vector and other layers are "
                            "flattened or left out."),
                       0.9, w, -1, true));
    QHBoxLayout *buttons = new QHBoxLayout();
    buttons->addStretch(1);
    IkTextButton *start = button(i18n("Start Session"), true, w);
    start->setMinimumWidth(IkStyle::scaled(200));
    buttons->addWidget(start);
    l->addLayout(buttons);

    auto sync = [=]() {
        const int i = where->currentIndex();
        address->setVisible(i == 2);
        listed->setEnabled(i != 0);
        if (i == 0) listed->setCurrentIndex(0);
        whereHint->setText(i == 0 ? i18n("People on the same network join with the invite link. The session ends when you leave.")
                           : i == 1 ? i18n("Runs on pub.drawpile.net: anyone with the link can join from anywhere, also in Drawpile.")
                                    : i18n("A Drawpile server you or your group run."));
        listed->update();
    };
    QObject::connect(where, &IkSegmented::currentChanged, w, sync);
    sync();

    QPointer<IkPopover> guard = pop;
    QPointer<KisDocument> src = source;
    QObject::connect(start, &QAbstractButton::clicked, w, [=]() {
        IkSession::HostOptions o;
        o.title = title->text().trimmed().isEmpty() ? i18n("Untitled session") : title->text().trimmed();
        o.where = IkSession::HostOptions::Where(where->currentIndex());
        o.address = address->text().trimmed();
        o.password = password->text();
        o.listed = listed->currentIndex() == 1;
        if (o.where == IkSession::HostOptions::OwnServer && o.address.isEmpty()) {
            address->setFocus();
            return;
        }
        const QString path = art ? art->currentData().toString() : QString();
        if (guard) guard->dismiss();
        IkSession *s = IkSession::instance();
        if (src) s->hostDocument(src, o);
        else if (!path.isEmpty()) s->hostFile(path, o);
        else s->hostBlank(QSize(2048, 1536), Qt::white, o);
    });
    pop->setContent(w);
    pop->popup(nullptr, IkPopover::Center);
}

// ========================================================= canvas overlay

namespace
{
/// Draws the other participants' cursors on the canvas widget.
class IkCursorLayer : public QWidget
{
public:
    explicit IkCursorLayer(QWidget *canvasWidget) : QWidget(canvasWidget)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setFocusPolicy(Qt::NoFocus);
        m_timer.setInterval(50);
        connect(&m_timer, &QTimer::timeout, this, [this]() {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            for (auto it = m_cursors.begin(); it != m_cursors.end();) {
                if (now - it->when > 8000) it = m_cursors.erase(it);
                else ++it;
            }
            if (m_cursors.isEmpty()) m_timer.stop();
            update();
        });
    }
    QPointer<KisView> view;

    void move(int userId, int x, int y)
    {
        if (x < 0) {
            m_cursors.remove(userId);
        } else {
            m_cursors[userId] = {QPointF(x, y), QDateTime::currentMSecsSinceEpoch()};
            if (!m_timer.isActive()) m_timer.start();
        }
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (!view || !view->canvasBase()) return;
        const KisCoordinatesConverter *conv = view->canvasBase()->coordinatesConverter();
        if (!conv) return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        for (auto it = m_cursors.constBegin(); it != m_cursors.constEnd(); ++it) {
            const QPointF pos = conv->imageToWidget(it->pos);
            const qreal fade = qBound(0.0, 1.0 - (now - it->when - 5000) / 3000.0, 1.0);
            p.setOpacity(fade);
            const QColor c = IkSession::colorFor(it.key());
            QPainterPath arrow;
            arrow.moveTo(pos);
            arrow.lineTo(pos + QPointF(6, 18));
            arrow.lineTo(pos + QPointF(9, 10));
            arrow.lineTo(pos + QPointF(17, 7));
            arrow.closeSubpath();
            p.setPen(QPen(Qt::white, 1.5));
            p.setBrush(c);
            p.drawPath(arrow);
            const QString name = IkSession::instance()->nameOf(it.key());
            const QFont f = IkStyle::font(0.85, QFont::Bold);
            p.setFont(f);
            const QFontMetrics fm(f);
            const QRectF tag(pos + QPointF(14, 14), QSizeF(fm.horizontalAdvance(name) + 14, fm.height() + 6));
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(tag, 7, 7);
            p.setPen(QColor(27, 22, 34));
            p.drawText(tag, Qt::AlignCenter, name);
        }
    }

private:
    struct Cursor {
        QPointF pos;
        qint64 when;
    };
    QHash<int, Cursor> m_cursors;
    QTimer m_timer;
};
}

struct IkSessionOverlay::Private {
    QPointer<KisMainWindow> mw;
    QPointer<IkCursorLayer> cursors;
    QRectF chipRect, peopleRect, chatRect;
    int unread {0};
    bool chatOpen {false};
    QPointer<IkPopover> chatPopover;
    QPointer<QListWidget> chatList;
};

IkSessionOverlay::IkSessionOverlay(KisMainWindow *mainWindow)
    : QWidget(mainWindow)
    , d(new Private)
{
    d->mw = mainWindow;
    setObjectName(QStringLiteral("IkSessionOverlay"));
    setAttribute(Qt::WA_TranslucentBackground);
    hide();
    IkSession *s = IkSession::instance();
    s->setMainWindow(mainWindow);
    auto repaint = [this]() {
        update();
        if (d->mw) relayout(QRect(), true);
    };
    connect(s, &IkSession::stateChanged, this, repaint);
    connect(s, &IkSession::titleChanged, this, [this]() { update(); });
    connect(s, &IkSession::peopleChanged, this, [this]() { update(); });
    connect(s, &IkSession::latencyChanged, this, [this]() { update(); });
    connect(s, &IkSession::chatChanged, this, [this]() {
        if (d->chatPopover && d->chatPopover->isOpen()) {
            if (d->chatList) {
                d->chatList->clear();
                for (const IkSession::ChatLine &c : IkSession::instance()->chat()) {
                    QListWidgetItem *it = new QListWidgetItem(c.userId ? QStringLiteral("%1: %2").arg(c.name, c.text) : c.text, d->chatList);
                    if (c.userId) it->setForeground(IkSession::colorFor(c.userId).lighter(110));
                    else it->setForeground(palette().color(QPalette::PlaceholderText));
                }
                d->chatList->scrollToBottom();
            }
        } else if (!IkSession::instance()->chat().isEmpty() && IkSession::instance()->chat().last().userId
                   && IkSession::instance()->chat().last().userId != IkSession::instance()->localUserId()) {
            ++d->unread;
            update();
        }
    });
    connect(s, &IkSession::cursorMoved, this, [this](int userId, int x, int y) {
        if (d->cursors) d->cursors->move(userId, x, y);
    });
    connect(s, &IkSession::notice, this, [this](const QString &message) {
        if (isVisible()) ikAskInPopover(window(), i18n("Online"), message, QString(), false, i18n("OK"), {}, {});
    });
}

IkSessionOverlay::~IkSessionOverlay()
{
    delete d;
}

void IkSessionOverlay::relayout(const QRect &canvasArea, bool canvasShown)
{
    static QRect lastArea;
    if (canvasArea.isValid()) lastArea = canvasArea;
    IkSession *s = IkSession::instance();
    KisView *view = d->mw ? d->mw->activeView() : nullptr;
    const bool onSession = s->isActive() && view && view->document() == s->document() && s->document();
    const bool visibleNow = canvasShown && onSession;
    if (!visibleNow) {
        hide();
        if (d->cursors) d->cursors->hide();
        return;
    }
    // Cursor layer on the canvas widget.
    QWidget *cw = view->canvasBase() ? view->canvasBase()->canvasWidget() : nullptr;
    if (cw && (!d->cursors || d->cursors->parentWidget() != cw)) {
        delete d->cursors;
        d->cursors = new IkCursorLayer(cw);
    }
    if (d->cursors) {
        d->cursors->view = view;
        d->cursors->setGeometry(cw->rect());
        d->cursors->show();
        d->cursors->raise();
    }
    // The pill: bottom centre of the canvas area.
    const int h = IkStyle::scaled(IkStyle::touchFirst() ? 58 : 50);
    const QFontMetrics fm(IkStyle::font(1.05, QFont::DemiBold));
    const int titleW = qMin(fm.horizontalAdvance(s->title()), IkStyle::scaled(260));
    const int w = IkStyle::scaled(70) + titleW + IkStyle::scaled(70) + IkStyle::scaled(150) + h;
    const QRect area = lastArea.isValid() ? lastArea : d->mw->rect();
    setGeometry(area.center().x() - w / 2, area.bottom() - h - IkStyle::scaled(18), w, h);
    show();
    raise();
    update();
}

void IkSessionOverlay::paintEvent(QPaintEvent *)
{
    IkSession *s = IkSession::instance();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette pal = palette();
    const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
    IkStyle::drawShadow(p, r, r.height() / 2, 8, 60);
    p.setPen(QPen(IkStyle::color(IkStyle::Accent, pal), 2));
    p.setBrush(IkStyle::color(IkStyle::Surface, pal));
    p.drawRoundedRect(r, r.height() / 2, r.height() / 2);

    const qreal pad = r.height() * 0.22;
    qreal x = r.left() + pad;
    // LIVE badge
    const bool live = s->state() == IkSession::Joined;
    QFont bf = IkStyle::font(0.8, QFont::Black);
    p.setFont(bf);
    const QString badge = live ? i18nc("online session badge", "LIVE") : i18nc("online session badge", "…");
    const QRectF b(x, r.center().y() - r.height() * 0.22, QFontMetrics(bf).horizontalAdvance(badge) + 18, r.height() * 0.44);
    p.setPen(Qt::NoPen);
    p.setBrush(live ? IkStyle::color(IkStyle::Accent, pal) : IkStyle::color(IkStyle::Raised, pal));
    p.drawRoundedRect(b, 6, 6);
    p.setPen(live ? IkStyle::color(IkStyle::AccentText, pal) : IkStyle::color(IkStyle::Text, pal));
    p.drawText(b, Qt::AlignCenter, badge);
    x = b.right() + 10;
    // Title and latency
    const QFont tf = IkStyle::font(1.05, QFont::DemiBold);
    const QFont sf = IkStyle::font(0.82);
    const QFontMetrics tfm(tf);
    const int titleW = qMin(tfm.horizontalAdvance(s->title()), IkStyle::scaled(260));
    p.setFont(tf);
    p.setPen(IkStyle::color(IkStyle::Text, pal));
    p.drawText(QRectF(x, r.top() + 4, titleW + 2, r.height() * 0.55), Qt::AlignLeft | Qt::AlignBottom,
               tfm.elidedText(s->title(), Qt::ElideRight, titleW));
    p.setFont(sf);
    p.setPen(IkStyle::color(IkStyle::MutedText, pal));
    p.drawText(QRectF(x, r.top() + r.height() * 0.55, titleW + 60, r.height() * 0.4), Qt::AlignLeft | Qt::AlignTop,
               live ? i18n("%1 ms", s->latencyMs()) : i18n("connecting"));
    d->chipRect = QRectF(r.left(), r.top(), x + titleW - r.left() + 10, r.height());
    x += titleW + 18;
    // People
    const QVector<IkSession::Person> people = s->people();
    const qreal av = r.height() * 0.62;
    const qreal startX = x;
    int shown = 0;
    for (const IkSession::Person &pp : people) {
        if (shown == 4) break;
        const QRectF a(x, r.center().y() - av / 2, av, av);
        p.setPen(QPen(IkStyle::color(IkStyle::Surface, pal), 2));
        p.setBrush(IkSession::colorFor(pp.id));
        p.drawEllipse(a);
        p.setPen(QColor(27, 22, 34));
        p.setFont(IkStyle::font(0.85, QFont::Bold));
        p.drawText(a, Qt::AlignCenter, pp.name.left(1).toUpper());
        x += av * 0.7;
        ++shown;
    }
    x += av * 0.3 + 4;
    p.setFont(IkStyle::font(0.95, QFont::DemiBold));
    p.setPen(IkStyle::color(IkStyle::Text, pal));
    const QString count = people.size() > shown ? QStringLiteral("+%1").arg(people.size() - shown) : QString::number(people.size());
    p.drawText(QRectF(x, r.top(), 34, r.height()), Qt::AlignVCenter | Qt::AlignLeft, count);
    x += 34;
    d->peopleRect = QRectF(startX - 4, r.top(), x - startX + 4, r.height());
    // Chat
    const qreal cs = r.height() * 0.5;
    const QRectF chat(r.right() - pad - cs, r.center().y() - cs / 2, cs, cs);
    const qreal dpr = devicePixelRatioF();
    p.drawPixmap(chat, IkStyle::pixmap(QStringLiteral("chat"), int(cs), IkStyle::color(IkStyle::Text, pal), dpr),
                 QRectF(0, 0, cs * dpr, cs * dpr));
    if (d->unread > 0) {
        const QRectF dot(chat.right() - 6, chat.top() - 6, 16, 16);
        p.setPen(Qt::NoPen);
        p.setBrush(IkStyle::color(IkStyle::Accent, pal));
        p.drawEllipse(dot);
        p.setPen(IkStyle::color(IkStyle::AccentText, pal));
        p.setFont(IkStyle::font(0.65, QFont::Black));
        p.drawText(dot, Qt::AlignCenter, QString::number(qMin(d->unread, 9)));
    }
    d->chatRect = chat.adjusted(-pad, -pad, pad, pad);
}

namespace
{
void showPeople(KisMainWindow *mw, QWidget *anchor)
{
    IkSession *s = IkSession::instance();
    IkPopover *pop = IkMenus::create(mw);
    pop->setAccentOutline(true);
    pop->setCloseButton(true);
    QPointer<IkPopover> guard = pop;
    QWidget *w = new QWidget();
    w->setMinimumWidth(IkStyle::scaled(420));
    QVBoxLayout *l = new QVBoxLayout(w);
    l->setSpacing(IkStyle::scaled(10));
    l->addWidget(label(i18np("People · 1", "People · %1", s->people().size()), 1.4, w, QFont::Bold));

    // Invite
    const QString link = s->inviteLink();
    if (!link.isEmpty()) {
        IkPanel *inv = new IkPanel(w);
        inv->setRole(IkStyle::Raised);
        inv->setRadius(IkStyle::scaled(14));
        QVBoxLayout *il = new QVBoxLayout(inv);
        il->setContentsMargins(14, 12, 14, 12);
        il->addWidget(label(i18n("Invite people"), 1.05, inv, QFont::DemiBold));
        QLabel *lk = label(link, 0.9, inv, -1, true);
        lk->setTextInteractionFlags(Qt::TextSelectableByMouse);
        il->addWidget(lk);
        il->addWidget(label(i18n("Works in IKriate and in Drawpile."), 0.85, inv, -1, true));
        IkTextButton *copy = button(i18n("Copy Link"), true, inv);
        QObject::connect(copy, &QAbstractButton::clicked, inv, [link, copy]() {
            QApplication::clipboard()->setText(link);
            copy->setText(i18n("Copied"));
        });
        il->addWidget(copy);
        l->addWidget(inv);
    }

    const bool op = s->isOperator();
    for (const IkSession::Person &p : s->people()) {
        QWidget *row = new QWidget(w);
        QHBoxLayout *rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 2, 0, 2);
        rl->addWidget(new IkAvatar(p.id, p.name, IkStyle::scaled(40), row));
        QVBoxLayout *text = new QVBoxLayout();
        text->setSpacing(0);
        text->addWidget(label(p.local ? i18n("%1 (you)", p.name) : p.name, 1.05, row, QFont::DemiBold));
        QStringList roles;
        if (p.op) roles << i18n("Operator");
        if (p.trusted) roles << i18n("Trusted");
        if (p.mod) roles << i18n("Moderator");
        if (p.locked) roles << i18n("Watching only");
        if (p.muted) roles << i18n("Muted");
        text->addWidget(label(roles.isEmpty() ? i18n("Drawing") : roles.join(QStringLiteral(" · ")), 0.88, row, -1, true));
        rl->addLayout(text, 1);
        if (op && !p.local) {
            IkIconButton *more = new IkIconButton(QStringLiteral("more"), i18n("Options for %1", p.name), row);
            more->setEdge(IkStyle::touchSize());
            const IkSession::Person person = p;
            QPointer<KisMainWindow> mwp = mw;
            QObject::connect(more, &QAbstractButton::clicked, row, [mwp, person, more, guard]() {
                IkActionGrid *grid = new IkActionGrid(person.name);
                QPointer<IkPopover> sub = IkMenus::create(mwp);
                grid->onTriggered = [sub, guard]() {
                    if (sub) sub->dismiss();
                    if (guard) guard->dismiss();
                };
                IkSession *s = IkSession::instance();
                grid->addItem(QStringLiteral("crown"), person.op ? i18n("Remove Operator") : i18n("Make Operator"),
                              [s, person]() { s->setOperator(person.id, !person.op); });
                grid->addItem(QStringLiteral("shield"), person.trusted ? i18n("Untrust") : i18n("Trust"),
                              [s, person]() { s->setTrusted(person.id, !person.trusted); });
                grid->addItem(QStringLiteral("lock"), person.locked ? i18n("Allow Drawing") : i18n("Watch Only"),
                              [s, person]() { s->setLocked(person.id, !person.locked); });
                grid->addItem(QStringLiteral("log-out"), i18n("Remove"), [s, person]() { s->kick(person.id, false); });
                sub->setContent(grid);
                sub->popup(more, IkPopover::Left);
            });
            rl->addWidget(more);
        }
        l->addWidget(row);
    }
    IkTextButton *leave = button(i18n("Leave Session"), false, w);
    QObject::connect(leave, &QAbstractButton::clicked, w, [guard]() {
        if (guard) guard->dismiss();
        IkSession::instance()->leave();
    });
    l->addWidget(leave);
    pop->setContent(w);
    pop->popup(anchor, IkPopover::Above);
}
}

void IkSessionOverlayShowChat(KisMainWindow *mw, QWidget *anchor, QPointer<IkPopover> &popOut, QPointer<QListWidget> &listOut)
{
    IkPopover *pop = IkMenus::create(mw);
    pop->setAccentOutline(true);
    pop->setCloseButton(true);
    pop->setModal(false);
    QWidget *w = new QWidget();
    w->setMinimumSize(IkStyle::scaled(380), IkStyle::scaled(420));
    QVBoxLayout *l = new QVBoxLayout(w);
    l->setSpacing(IkStyle::scaled(8));
    l->addWidget(label(i18n("Chat"), 1.4, w, QFont::Bold));
    QListWidget *list = new QListWidget(w);
    list->setWordWrap(true);
    list->setFont(IkStyle::font(1.05));
    list->setSelectionMode(QAbstractItemView::NoSelection);
    IkStyle::makeTransparent(list);
    for (const IkSession::ChatLine &c : IkSession::instance()->chat()) {
        QListWidgetItem *it = new QListWidgetItem(c.userId ? QStringLiteral("%1: %2").arg(c.name, c.text) : c.text, list);
        if (c.userId) it->setForeground(IkSession::colorFor(c.userId).lighter(110));
        else it->setForeground(w->palette().color(QPalette::PlaceholderText));
    }
    list->scrollToBottom();
    l->addWidget(list, 1);
    QHBoxLayout *row = new QHBoxLayout();
    QLineEdit *field = new QLineEdit(w);
    field->setPlaceholderText(i18n("Message everyone…"));
    field->setMinimumHeight(IkStyle::touchSize());
    field->setFont(IkStyle::font(1.05));
    IkIconButton *send = new IkIconButton(QStringLiteral("send"), i18n("Send"), w);
    send->setVariant(IkIconButton::Accent);
    send->setEdge(IkStyle::touchSize());
    row->addWidget(field, 1);
    row->addWidget(send);
    l->addLayout(row);
    QObject::connect(send, &QAbstractButton::clicked, field, [field]() {
        IkSession::instance()->sendChat(field->text());
        field->clear();
    });
    QObject::connect(field, &QLineEdit::returnPressed, send, &QAbstractButton::click);
    pop->setContent(w);
    pop->popup(anchor, IkPopover::Above);
    field->setFocus();
    popOut = pop;
    listOut = list;
}

void IkSessionOverlay::mousePressEvent(QMouseEvent *event)
{
    event->accept();
}

void IkSessionOverlay::mouseReleaseEvent(QMouseEvent *event)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QPointF pos = event->position();
#else
    const QPointF pos = event->localPos();
#endif
    if (d->chatRect.contains(pos)) {
        d->unread = 0;
        update();
        IkSessionOverlayShowChat(d->mw, this, d->chatPopover, d->chatList);
    } else if (d->peopleRect.contains(pos) || d->chipRect.contains(pos)) {
        showPeople(d->mw, this);
    }
}
