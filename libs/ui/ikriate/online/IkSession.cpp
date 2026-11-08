/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "IkSession.h"
#include "IkSessionMirror.h"
#include "IkOnlineCommon.h"
#include "IkDrawpileFiles.h"

extern "C" {
#include <dpmsg/acl.h>
#include <dpmsg/messages.h>
}

#include "cmake-config/config.h"
#include "libclient/canvas/acl.h"
#include "libclient/canvas/canvasmodel.h"
#include "libclient/canvas/paintengine.h"
#include "libclient/canvas/userlist.h"
#include "libclient/config/memoryconfig.h"
#include "libclient/document.h"
#include "libclient/drawdance/global.h"
#include "libclient/net/client.h"
#include "libclient/net/login.h"
#include "libclient/net/loginsessions.h"
#include "libclient/net/message.h"
#include "libclient/server/builtinserver.h"
#include "libclient/view/enums.h"
#include "libclient/utils/cursors.h"
#include "libclient/utils/icons.h"
#include "libshared/net/message.h"
#include "libshared/net/netutils.h"
#include "libshared/net/servercmd.h"
#include "libshared/util/whatismyip.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QVBoxLayout>

#include <KConfigGroup>
#include <KSharedConfig>
#include <klocalizedstring.h>

#include <KisDocument.h>
#include <KisMainWindow.h>
#include <KisPart.h>
#include <KisView.h>
#include <KoColor.h>
#include <KoColorSpaceRegistry.h>
#include <kis_config.h>

#include "../IkStyle.h"
#include "../IkWidgets.h"

namespace
{
const char kGroup[] = "IKriateOnline";
const int kTimeoutSecs = 30;
const int kUndoLimit = 30;

void initDrawdanceOnce()
{
    static bool done = false;
    if (done) return;
    done = true;
    IkDrawpileFiles::ensureEngine();
    // Drawpile's tool controller makes cursors and icons for its tools
    // (never shown in IKriate, but they must exist).
    utils::Cursors::init();
    utils::Icons::init();
}

QString defaultName()
{
    QString user = qEnvironmentVariable("USER", qEnvironmentVariable("USERNAME"));
    if (user.isEmpty() || user == QLatin1String("root") || user.startsWith(QLatin1Char('u'))) {
        user = QStringLiteral("Artist%1").arg(QRandomGenerator::global()->bounded(100, 999));
    }
    return user;
}
}

/// Small form in a popover: a message, optionally a text field, and buttons.
void ikAskInPopover(QWidget *window, const QString &title, const QString &text, const QString &fieldPlaceholder,
                    bool password, const QString &acceptText, std::function<void(const QString &)> onAccept,
                    std::function<void()> onCancel, const QString &initialText)
{
    if (!window) {
        if (onCancel) onCancel();
        return;
    }
    IkPopover *pop = new IkPopover(window);
    pop->setDimBackground(true);
    pop->setCloseButton(true);
    pop->setAccentOutline(true);
    pop->setModal(true);
    QWidget *w = new QWidget();
    QVBoxLayout *l = new QVBoxLayout(w);
    l->setContentsMargins(IkStyle::scaled(8), IkStyle::scaled(4), IkStyle::scaled(8), IkStyle::scaled(8));
    l->setSpacing(IkStyle::scaled(12));
    QLabel *t = new QLabel(title, w);
    t->setFont(IkStyle::font(1.5, QFont::Bold));
    l->addWidget(t);
    if (!text.isEmpty()) {
        QLabel *m = new QLabel(text, w);
        m->setWordWrap(true);
        m->setTextFormat(Qt::PlainText);
        m->setFont(IkStyle::font(1.05));
        m->setMaximumWidth(IkStyle::scaled(520));
        if (text.size() > 400) {
            QScrollArea *sa = new QScrollArea(w);
            sa->setWidget(m);
            sa->setWidgetResizable(true);
            sa->setMinimumHeight(IkStyle::scaled(220));
            IkStyle::makeTransparent(sa);
            l->addWidget(sa);
        } else {
            l->addWidget(m);
        }
    }
    QLineEdit *field = nullptr;
    if (!fieldPlaceholder.isNull()) {
        field = new QLineEdit(w);
        field->setPlaceholderText(fieldPlaceholder);
        field->setMinimumHeight(IkStyle::touchSize());
        field->setFont(IkStyle::font(1.15));
        if (password) field->setEchoMode(QLineEdit::Password);
        field->setText(initialText);
        field->selectAll();
        l->addWidget(field);
    }
    QHBoxLayout *buttons = new QHBoxLayout();
    buttons->addStretch(1);
    IkTextButton *ok = new IkTextButton(acceptText, IkTextButton::Filled, w);
    ok->setRadius(IkStyle::scaled(10));
    ok->setFontScale(1.15, QFont::Bold);
    ok->setMinimumHeight(IkStyle::touchSize());
    buttons->addWidget(ok);
    l->addLayout(buttons);
    pop->setContent(w);
    QPointer<IkPopover> guard = pop;
    auto accepted = std::make_shared<bool>(false);
    QObject::connect(ok, &QAbstractButton::clicked, pop, [guard, field, onAccept, accepted]() {
        *accepted = true;
        const QString value = field ? field->text() : QString();
        if (guard) guard->dismiss();
        if (onAccept) onAccept(value);
    });
    if (field) QObject::connect(field, &QLineEdit::returnPressed, ok, &QAbstractButton::click);
    QObject::connect(pop, &IkPopover::dismissed, pop, [accepted, onCancel]() {
        if (!*accepted && onCancel) onCancel();
    });
    pop->popup(nullptr, IkPopover::Center);
    if (field) field->setFocus();
}


// ------------------------------------------------------------------ private

struct IkSession::Private {
    IkSession *q;
    QPointer<KisMainWindow> mw;
    config::MemoryConfig *cfg {nullptr};
    Document *doc {nullptr};
    IkSessionMirror *mirror {nullptr};
    QPointer<server::BuiltinServer> builtin;
    QPointer<KisDocument> kdoc;
    QPointer<canvas::CanvasModel> hookedCanvas;
    State state {Idle};
    QString title;
    QString server;
    QString address;
    int lag {0};
    bool hosting {false};
    bool viewOpened {false};
    bool notifiedLeave {false};
    QVector<ChatLine> chat;
    QVector<Person> people;

    explicit Private(IkSession *s) : q(s) {}

    QWidget *window() const
    {
        if (mw) return mw;
        return QApplication::activeWindow();
    }

    void setState(State s)
    {
        if (state == s) return;
        state = s;
        Q_EMIT q->stateChanged();
    }

    void addChat(int userId, const QString &name, const QString &text)
    {
        chat.append({userId, name, text, QDateTime::currentDateTime()});
        if (chat.size() > 500) chat.remove(0, chat.size() - 500);
        Q_EMIT q->chatChanged();
    }

    void system(const QString &text) { addChat(0, QString(), text); }

    void ensureDocument()
    {
        if (doc) return;
        initDrawdanceOnce();
        cfg = new config::MemoryConfig(q);
        doc = new Document(int(view::CanvasImplementation::Software), cfg, q);
        mirror = new IkSessionMirror(doc, q);

        QObject::connect(doc, &Document::serverConnected, q, [this](const QUrl &url) {
            server = url.host();
            setState(Connecting);
        });
        QObject::connect(doc, &Document::serverLoggedIn, q, [this](bool join, const QString &) {
            Q_UNUSED(join);
            setState(Joined);
            if (!doc->sessionTitle().isEmpty()) title = doc->sessionTitle();
            Q_EMIT q->titleChanged();
            rememberRecent();
            system(i18n("Connected to %1.", server));
            openKritaDocument();
        });
        QObject::connect(doc, &Document::serverDisconnected, q,
                         [this](const QString &message, const QString &, bool local, bool) {
                             if (!local && !message.isEmpty()) Q_EMIT q->notice(message);
                             system(local ? i18n("You left the session.") : i18n("Disconnected: %1", message));
                             mirror->detach();
                             if (kdoc) kdoc->setModified(true); // the local copy stays, unsaved
                             if (builtin) builtin->stop();
                             hosting = false;
                             viewOpened = false;
                             people.clear();
                             Q_EMIT q->peopleChanged();
                             setState(Idle);
                         });
        QObject::connect(doc, &Document::sessionTitleChanged, q, [this](const QString &t) {
            if (t.isEmpty()) return;
            title = t;
            Q_EMIT q->titleChanged();
        });
        QObject::connect(doc, &Document::canvasChanged, q, [this](canvas::CanvasModel *cm) { hookCanvas(cm); });
        QObject::connect(doc->client(), &net::Client::lagMeasured, q, [this](qint64 ms) {
            lag = int(ms);
            Q_EMIT q->latencyChanged();
        });
        QObject::connect(doc->client(), &net::Client::youWereKicked, q, [this](const QString &by) {
            Q_EMIT q->notice(i18n("You were removed from the session by %1.", by));
        });
        QObject::connect(doc->client(), &net::Client::serverMessage, q, [this](const QString &message, int) {
            system(message);
        });
    }

    void hookCanvas(canvas::CanvasModel *cm)
    {
        // Called both when the document makes a canvas and when hosting starts:
        // hook each canvas once, or every message shows up twice.
        if (!cm || cm == hookedCanvas) return;
        hookedCanvas = cm;
        QObject::connect(cm, &canvas::CanvasModel::chatMessageReceived, q,
                         [this, cm](int sender, int, uint8_t, uint8_t, const QString &message) {
                             addChat(sender, cm->userlist()->getUsername(sender), message);
                         });
        QObject::connect(cm, &canvas::CanvasModel::userJoined, q, [this](int, const QString &name) {
            system(i18n("%1 joined.", name));
            refreshPeople();
        });
        QObject::connect(cm, &canvas::CanvasModel::userLeft, q, [this](int id, const QString &name) {
            system(i18n("%1 left.", name));
            Q_EMIT q->cursorMoved(id, -1, -1);
            refreshPeople();
        });
        QAbstractItemModel *users = cm->userlist();
        QObject::connect(users, &QAbstractItemModel::modelReset, q, [this]() { refreshPeople(); });
        QObject::connect(users, &QAbstractItemModel::dataChanged, q, [this]() { refreshPeople(); });
        QObject::connect(users, &QAbstractItemModel::rowsInserted, q, [this]() { refreshPeople(); });
        QObject::connect(users, &QAbstractItemModel::rowsRemoved, q, [this]() { refreshPeople(); });
        QObject::connect(cm->paintEngine(), &canvas::PaintEngine::cursorMoved, q,
                         [this](unsigned int, int userId, int, int x, int y) {
                             if (doc && doc->canvas() && userId != doc->canvas()->localUserId()) {
                                 Q_EMIT q->cursorMoved(userId, x, y);
                             }
                         });
        QObject::connect(cm, &canvas::CanvasModel::laserTrail, q, [this](int userId, int, const QColor &color) {
            Q_UNUSED(color);
            Q_UNUSED(userId);
        });
        refreshPeople();
    }

    void refreshPeople()
    {
        people.clear();
        if (doc && doc->canvas()) {
            for (const canvas::User &u : doc->canvas()->userlist()->users()) {
                if (!u.isOnline) continue;
                Person p;
                p.id = u.id;
                p.name = u.name;
                p.local = u.isLocal;
                p.op = u.isOperator;
                p.trusted = u.isTrusted;
                p.locked = u.isLocked;
                p.muted = u.isMuted;
                p.mod = u.isMod;
                people << p;
            }
        }
        Q_EMIT q->peopleChanged();
    }

    void openKritaDocument()
    {
        if (kdoc) {
            mirror->attach(kdoc);
            return;
        }
        // Waits for the shared canvas to have a size, so the document opens
        // at that size (and the view fits it).
        canvas::CanvasModel *cm = doc ? doc->canvas() : nullptr;
        const drawdance::CanvasState cs = cm ? cm->paintEngine()->historyCanvasState() : drawdance::CanvasState::null();
        if (cs.isNull() || cs.width() <= 0 || cs.height() <= 0) {
            if (qEnvironmentVariableIsSet("IKRIATE_DEBUG_ONLINE")) {
                qDebug("IKriate online: waiting for canvas (canvas %p, null %d, state %d)", (void *)cm, int(cs.isNull()), int(state));
            }
            if (state == Joined) QTimer::singleShot(100, q, [this]() { openKritaDocument(); });
            return;
        }
        const KoColorSpace *rgb = KoColorSpaceRegistry::instance()->rgb8();
        KisDocument *d = KisPart::instance()->createDocument();
        d->newImage(title.isEmpty() ? i18n("Online session") : title, cs.width(), cs.height(), rgb,
                    KoColor(Qt::transparent, rgb), KisConfig::CANVAS_COLOR, 1, QString(), 1.0);
        kdoc = d;
        mirror->attach(d);
        viewOpened = true;
        KisPart::instance()->startCustomDocument(d);
    }

    void rememberRecent()
    {
        KConfigGroup g(KSharedConfig::openConfig(), kGroup);
        QJsonArray list = QJsonDocument::fromJson(g.readEntry("Recent", QByteArray("[]"))).array();
        const QString link = q->inviteLink();
        QJsonArray out;
        QJsonObject now;
        now[QStringLiteral("title")] = title;
        now[QStringLiteral("address")] = link;
        now[QStringLiteral("when")] = QDateTime::currentDateTime().toString(Qt::ISODate);
        now[QStringLiteral("hosted")] = hosting;
        out.append(now);
        for (const QJsonValue &v : list) {
            if (v.toObject().value(QStringLiteral("address")).toString() != link && out.size() < 8) out.append(v);
        }
        g.writeEntry("Recent", QJsonDocument(out).toJson(QJsonDocument::Compact));
    }

    void connectLogin(net::LoginHandler *login)
    {
        QPointer<net::LoginHandler> lh = login;
        auto cancel = [lh]() {
            if (lh) lh->cancelLogin();
        };
        QObject::connect(login, &net::LoginHandler::ruleAcceptanceNeeded, q, [this, lh, cancel](const QString &rules) {
            ikAskInPopover(window(), i18n("Server rules"), rules, QString(), false, i18n("Accept"),
                         [lh](const QString &) {
                             if (lh) lh->acceptRules();
                         },
                         cancel);
        });
        QObject::connect(login, &net::LoginHandler::loginMethodChoiceNeeded, q,
                         [this, lh, cancel](const QVector<net::LoginHandler::LoginMethod> &methods, const QUrl &,
                                            const QUrl &, const QString &) {
                             if (!lh) return;
                             if (methods.contains(net::LoginHandler::LoginMethod::Guest)) {
                                 lh->selectIdentity(displayName(), QString(), net::LoginHandler::LoginMethod::Guest);
                             } else if (methods.contains(net::LoginHandler::LoginMethod::Auth)) {
                                 askPassword(lh, i18n("This server needs an account. Password for %1:", displayName()),
                                             net::LoginHandler::LoginMethod::Auth);
                             } else if (methods.contains(net::LoginHandler::LoginMethod::ExtAuth)) {
                                 askPassword(lh, i18n("This server needs a drawpile.net account. Password for %1:", displayName()),
                                             net::LoginHandler::LoginMethod::ExtAuth);
                             } else {
                                 cancel();
                             }
                         });
        QObject::connect(login, &net::LoginHandler::usernameNeeded, q, [lh](bool) {
            if (lh) lh->selectIdentity(displayName(), QString(), net::LoginHandler::LoginMethod::Guest);
        });
        QObject::connect(login, &net::LoginHandler::loginNeeded, q,
                         [this, lh](const QString &, const QString &prompt, const QString &, net::LoginHandler::LoginMethod intent) {
                             askPassword(lh, prompt.isEmpty() ? i18n("Password for %1:", displayName()) : prompt, intent);
                         });
        QObject::connect(login, &net::LoginHandler::extAuthNeeded, q,
                         [this, lh, cancel](const QString &user, const QString &, const QUrl &, const QString &host,
                                            net::LoginHandler::LoginMethod) {
                             ikAskInPopover(window(), i18n("Sign in to %1", host),
                                          i18n("Password for %1:", user.isEmpty() ? displayName() : user), QStringLiteral(""), true,
                                          i18n("Sign In"),
                                          [lh, user](const QString &pw) {
                                              if (lh) lh->requestExtAuth(user.isEmpty() ? displayName() : user, pw);
                                          },
                                          cancel);
                         });
        QObject::connect(login, &net::LoginHandler::badLoginPassword, q,
                         [this, lh](net::LoginHandler::LoginMethod intent, const QString &, const QString &) {
                             askPassword(lh, i18n("That password was not accepted. Try again:"), intent);
                         });
        QObject::connect(login, &net::LoginHandler::sessionChoiceNeeded, q, [this, lh, cancel](net::LoginSessionModel *m) {
            chooseSession(lh, m, cancel);
        });
        QObject::connect(login, &net::LoginHandler::sessionConfirmationNeeded, q, [lh](const QString &, bool, bool) {
            if (lh) lh->confirmJoinSelectedSession();
        });
        QObject::connect(login, &net::LoginHandler::sessionPasswordNeeded, q, [this, lh, cancel]() {
            ikAskInPopover(window(), i18n("Session password"), i18n("This session is protected by a password."),
                         QStringLiteral(""), true, i18n("Join"),
                         [lh](const QString &pw) {
                             if (lh) lh->sendSessionPassword(pw);
                         },
                         cancel);
        });
        QObject::connect(login, &net::LoginHandler::badSessionPassword, q, [this, lh, cancel]() {
            ikAskInPopover(window(), i18n("Session password"), i18n("That password was not accepted. Try again."),
                         QStringLiteral(""), true, i18n("Join"),
                         [lh](const QString &pw) {
                             if (lh) lh->sendSessionPassword(pw);
                         },
                         cancel);
        });
        QObject::connect(login, &net::LoginHandler::certificateCheckNeeded, q,
                         [this, lh, cancel](const QSslCertificate &, const QSslCertificate &) {
                             ikAskInPopover(window(), i18n("Server certificate changed"),
                                          i18n("The server's security certificate is not the one seen before. Continue only "
                                               "if you trust this server."),
                                          QString(), false, i18n("Continue"),
                                          [lh](const QString &) {
                                              if (lh) lh->acceptServerCertificate();
                                          },
                                          cancel);
                         });
    }

    void askPassword(QPointer<net::LoginHandler> lh, const QString &prompt, net::LoginHandler::LoginMethod method)
    {
        ikAskInPopover(window(), i18n("Sign in"), prompt, QStringLiteral(""), true, i18n("Sign In"),
                     [lh, method](const QString &pw) {
                         if (lh) lh->selectIdentity(displayName(), pw, method);
                     },
                     [lh]() {
                         if (lh) lh->cancelLogin();
                     });
    }

    void chooseSession(QPointer<net::LoginHandler> lh, net::LoginSessionModel *m, std::function<void()> cancel)
    {
        QWidget *win = window();
        if (!win || !lh) return;
        IkPopover *pop = new IkPopover(win);
        pop->setDimBackground(true);
        pop->setCloseButton(true);
        pop->setAccentOutline(true);
        pop->setModal(true);
        QWidget *w = new QWidget();
        QVBoxLayout *l = new QVBoxLayout(w);
        l->setSpacing(IkStyle::scaled(10));
        QLabel *t = new QLabel(i18n("Choose a session"), w);
        t->setFont(IkStyle::font(1.5, QFont::Bold));
        l->addWidget(t);
        QListWidget *list = new QListWidget(w);
        list->setMinimumSize(IkStyle::scaled(440), IkStyle::scaled(300));
        list->setFont(IkStyle::font(1.15));
        IkStyle::makeTransparent(list);
        // The server sends its sessions after asking for a choice: follow
        // the model as they arrive.
        QPointer<QListWidget> listGuard = list;
        auto fill = [listGuard, m]() {
            if (!listGuard) return;
            listGuard->clear();
            for (int r = 0; r < m->rowCount(); ++r) {
                const QModelIndex idx = m->index(r, 0);
                QListWidgetItem *it = new QListWidgetItem(
                    i18nc("session title, number of people", "%1 · %2 people",
                          idx.data(net::LoginSessionModel::TitleRole).toString(),
                          idx.data(net::LoginSessionModel::UserCountRole).toInt()),
                    listGuard);
                it->setData(Qt::UserRole, r);
                it->setSizeHint(QSize(0, IkStyle::touchSize()));
                if (!idx.data(net::LoginSessionModel::JoinableRole).toBool()) it->setFlags(Qt::NoItemFlags);
            }
        };
        fill();
        QObject::connect(m, &QAbstractItemModel::rowsInserted, list, fill);
        QObject::connect(m, &QAbstractItemModel::rowsRemoved, list, fill);
        QObject::connect(m, &QAbstractItemModel::dataChanged, list, fill);
        QObject::connect(m, &QAbstractItemModel::modelReset, list, fill);
        l->addWidget(list);
        pop->setContent(w);
        QPointer<IkPopover> guard = pop;
        auto chosen = std::make_shared<bool>(false);
        QObject::connect(list, &QListWidget::itemClicked, pop, [guard, lh, m, chosen](QListWidgetItem *it) {
            const QModelIndex idx = m->index(it->data(Qt::UserRole).toInt(), 0);
            *chosen = true;
            if (guard) guard->dismiss();
            if (!lh) return;
            lh->prepareJoinSelectedSession(idx.data(net::LoginSessionModel::IdAliasRole).toString(),
                                           idx.data(net::LoginSessionModel::NeedPasswordRole).toBool(),
                                           idx.data(net::LoginSessionModel::CompatibilityModeRole).toBool(),
                                           idx.data(net::LoginSessionModel::MinorIncompatibilityRole).toBool(),
                                           idx.data(net::LoginSessionModel::TitleRole).toString(),
                                           idx.data(net::LoginSessionModel::NsfmRole).toBool(), false);
        });
        QObject::connect(pop, &IkPopover::dismissed, pop, [chosen, cancel]() {
            if (!*chosen) cancel();
        });
        // A link with the session in it joins on its own once the list has
        // arrived: close the list then (and if the login ends otherwise).
        auto done = [guard, chosen]() {
            *chosen = true;
            if (guard) guard->dismiss();
        };
        QObject::connect(lh.data(), &net::LoginHandler::sessionConfirmationNeeded, pop, done);
        QObject::connect(lh.data(), &QObject::destroyed, pop, done);
        QObject::connect(q, &IkSession::stateChanged, pop, [this, done]() {
            if (state != Connecting) done();
        });
        pop->popup(nullptr, IkPopover::Center);
    }

    /// Hosts @p cs, or (null) the canvas the document already has.
    void startHost(const drawdance::CanvasState &cs, const HostOptions &options, bool useCurrent = false)
    {
        if (cs.isNull() && !useCurrent) {
            Q_EMIT q->notice(i18n("This artwork could not be prepared for sharing."));
            return;
        }
        ensureDocument();
        if (doc->client()->isConnected()) {
            Q_EMIT q->notice(i18n("Leave the current session before hosting a new one."));
            return;
        }
        if (!useCurrent) doc->loadState(cs, QString(), DP_SAVE_IMAGE_UNKNOWN, false, false, QString(), 0);
        canvas::CanvasModel *cm = doc->canvas();
        if (!cm) return;
        hookCanvas(cm);
        kdoc = nullptr;

        const bool remote = options.where != HostOptions::ThisDevice;
        QUrl url;
        if (remote) {
            const QString a = options.where == HostOptions::PublicServer ? QStringLiteral("pub.drawpile.net") : options.address;
            url = QUrl(net::addSchemeToUserSuppliedAddress(a), QUrl::TolerantMode);
            if (!url.isValid() || url.host().isEmpty()) {
                Q_EMIT q->notice(i18n("\"%1\" is not a server address.", a));
                return;
            }
        } else {
            server::BuiltinServer *srv = new server::BuiltinServer(cm->paintEngine(), q);
            QString error;
            if (!srv->start(quint16(cmake_config::proto::port()), kTimeoutSecs, 0, &error)) {
                Q_EMIT q->notice(i18n("Could not start hosting on this device: %1", error));
                delete srv;
                return;
            }
            builtin = srv;
            cm->paintEngine()->setServer(srv);
            url.setScheme(QStringLiteral("drawpile"));
            url.setHost(WhatIsMyIp::guessLocalAddress());
            if (srv->port() != cmake_config::proto::port()) url.setPort(srv->port());
        }

        net::LoginHostParams *params = new net::LoginHostParams;
        params->remote = remote;
        params->nsfm = options.nsfm;
        params->keepChat = false;
        params->deputies = false;
        params->userId = cm->localUserId();
        params->title = options.title;
        params->password = options.password;
        if (options.listed && remote) params->announcementUrls << publicListingUrl().toString();
        if (remote) {
            params->initialState = cm->generateSnapshot(true, DP_ACL_STATE_RESET_IMAGE_SESSION_RESET_FLAGS, kUndoLimit);
        } else {
            QVector<uint8_t> features;
            for (int f = 0; f < DP_FEATURE_COUNT; ++f) {
                features.append(uint8_t(DP_feature_access_tier_default(f, int(DP_ACCESS_TIER_OPERATOR))));
            }
            QVector<int32_t> limits;
            for (int i = 0; i < DP_FEATURE_LIMIT_COUNT; ++i) {
                for (int j = 0; j < DP_ACCESS_TIER_COUNT; ++j) limits.append(DP_feature_limit_default(i, j, -1));
            }
            params->initialState = {net::makeFeatureAccessLevelsMessage(0, features), net::makeFeatureLimitsMessage(0, limits),
                                    net::makeUndoDepthMessage(0, kUndoLimit)};
        }
        hosting = true;
        title = options.title;
        net::LoginHandler *login = new net::LoginHandler(QSharedPointer<const net::LoginHostParams>(params), QString(), url, 0,
                                                         QStringList(), QJsonObject(), q);
        connectLogin(login);
        setState(Connecting);
        doc->connectToServer(kTimeoutSecs, 0, net::defaultConnectStrategy(), login, !remote);
    }
};

// ----------------------------------------------------------------- session

IkSession *IkSession::instance()
{
    static QPointer<IkSession> s;
    if (!s) s = new IkSession(qApp);
    return s;
}

IkSession::IkSession(QObject *parent)
    : QObject(parent)
    , d(new Private(this))
{
}

IkSession::~IkSession()
{
    delete d;
}

void IkSession::setMainWindow(KisMainWindow *mainWindow)
{
    d->mw = mainWindow;
}

IkSession::State IkSession::state() const
{
    return d->state;
}

QString IkSession::title() const
{
    return d->title.isEmpty() ? i18n("Online session") : d->title;
}

QString IkSession::serverName() const
{
    return d->server;
}

QString IkSession::inviteLink() const
{
    if (!d->doc || !d->doc->client()->isConnected()) return QString();
    QUrl url = d->doc->client()->sessionUrl(false);
    url.setQuery(QString());
    if (d->builtin && (url.host().isEmpty() || url.host() == QLatin1String("127.0.0.1") || url.host() == QLatin1String("localhost"))) {
        url.setHost(WhatIsMyIp::guessLocalAddress());
    }
    return url.toString();
}

int IkSession::latencyMs() const
{
    return d->lag;
}

KisDocument *IkSession::document() const
{
    return d->kdoc;
}

QVector<IkSession::Person> IkSession::people() const
{
    return d->people;
}

int IkSession::localUserId() const
{
    return (d->doc && d->doc->canvas()) ? d->doc->canvas()->localUserId() : 0;
}

bool IkSession::isOperator() const
{
    return d->doc && d->doc->canvas() && d->doc->canvas()->aclState()->amOperator();
}

QString IkSession::nameOf(int userId) const
{
    return (d->doc && d->doc->canvas()) ? d->doc->canvas()->userlist()->getUsername(userId) : QString();
}

QColor IkSession::colorFor(int userId)
{
    static const QColor palette[] = {QColor(242, 165, 65), QColor(231, 127, 179), QColor(90, 169, 255), QColor(255, 90, 110),
                                     QColor(155, 181, 154), QColor(46, 232, 159), QColor(199, 125, 219), QColor(250, 204, 21)};
    return palette[qAbs(userId) % 8];
}

const QVector<IkSession::ChatLine> &IkSession::chat() const
{
    return d->chat;
}

QString IkSession::displayName()
{
    KConfigGroup g(KSharedConfig::openConfig(), kGroup);
    QString name = g.readEntry("Name", QString());
    if (name.trimmed().isEmpty()) {
        name = defaultName();
        g.writeEntry("Name", name);
    }
    return name;
}

void IkSession::setDisplayName(const QString &name)
{
    if (name.trimmed().isEmpty()) return;
    KConfigGroup(KSharedConfig::openConfig(), kGroup).writeEntry("Name", name.trimmed());
}

QVector<IkSession::RecentSession> IkSession::recentSessions()
{
    QVector<RecentSession> out;
    KConfigGroup g(KSharedConfig::openConfig(), kGroup);
    const QJsonArray list = QJsonDocument::fromJson(g.readEntry("Recent", QByteArray("[]"))).array();
    for (const QJsonValue &v : list) {
        const QJsonObject o = v.toObject();
        RecentSession r;
        r.title = o.value(QStringLiteral("title")).toString();
        r.address = o.value(QStringLiteral("address")).toString();
        r.when = QDateTime::fromString(o.value(QStringLiteral("when")).toString(), Qt::ISODate);
        r.hosted = o.value(QStringLiteral("hosted")).toBool();
        if (!r.address.isEmpty()) out << r;
    }
    return out;
}

QUrl IkSession::publicListingUrl()
{
    return QUrl(QStringLiteral("https://pub.drawpile.net/listing/"));
}

void IkSession::join(const QString &address)
{
    const QString a = address.trimmed();
    if (a.isEmpty()) return;
    d->ensureDocument();
    if (d->doc->client()->isConnected()) {
        Q_EMIT notice(i18n("Leave the current session before joining another one."));
        return;
    }
    // host[:port][/session]: Drawpile's own helper keeps only host and port,
    // so the session after the slash would be lost.
    const QUrl url(a.contains(QLatin1String("://")) ? a : QStringLiteral("drawpile://") + a, QUrl::TolerantMode);
    if (!url.isValid() || url.host().isEmpty()) {
        Q_EMIT notice(i18n("\"%1\" is not a session link or server address.", a));
        return;
    }
    d->hosting = false;
    d->kdoc = nullptr;
    d->chat.clear();
    Q_EMIT chatChanged();
    net::LoginHandler *login = new net::LoginHandler(QSharedPointer<const net::LoginHostParams>(nullptr),
                                                     net::extractAutoJoinIdFromUrl(url), url, 0, QStringList(),
                                                     QJsonObject(), this);
    d->connectLogin(login);
    d->server = url.host();
    d->setState(Connecting);
    d->doc->connectToServer(kTimeoutSecs, 0, net::defaultConnectStrategy(), login, false);
}

void IkSession::hostDocument(KisDocument *source, const HostOptions &options)
{
    if (!source) return;
    // Krita writes OpenRaster, which Drawpile reads: layers, groups, blend
    // modes and opacity carry over.
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("share.ora"));
    if (!source->exportDocumentSync(path, "image/openraster")) {
        Q_EMIT notice(i18n("This artwork could not be prepared for sharing."));
        return;
    }
    d->chat.clear();
    Q_EMIT chatChanged();
    d->startHost(drawdance::CanvasState::load(path), options);
}

void IkSession::hostFile(const QString &path, const HostOptions &options)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    static const QStringList direct = {QStringLiteral("ora"), QStringLiteral("psd"), QStringLiteral("png"), QStringLiteral("jpg"),
                                       QStringLiteral("jpeg"), QStringLiteral("webp"), QStringLiteral("dppr"),
                                       QStringLiteral("dpcs"), QStringLiteral("qoi")};
    if (direct.contains(suffix)) {
        d->chat.clear();
        Q_EMIT chatChanged();
        d->startHost(drawdance::CanvasState::load(path), options);
        return;
    }
    QScopedPointer<KisDocument> tmp(KisPart::instance()->createDocument());
    if (!tmp->openPath(path, KisDocument::DontAddToRecent)) {
        Q_EMIT notice(i18n("\"%1\" could not be opened.", QFileInfo(path).fileName()));
        return;
    }
    tmp->image()->waitForDone();
    hostDocument(tmp.data(), options);
}

void IkSession::hostBlank(const QSize &size, const QColor &background, const HostOptions &options)
{
    d->ensureDocument();
    if (d->doc->client()->isConnected()) {
        Q_EMIT notice(i18n("Leave the current session before hosting a new one."));
        return;
    }
    d->chat.clear();
    Q_EMIT chatChanged();
    if (!d->doc->loadBlank(size, background, i18n("Layer 1"), i18n("Track 1"), false)) {
        Q_EMIT notice(i18n("Could not create the canvas."));
        return;
    }
    // The canvas engine builds the blank canvas on its own thread: host it
    // once it has its size.
    auto tries = std::make_shared<int>(0);
    auto wait = std::make_shared<std::function<void()>>();
    *wait = [this, options, tries, wait]() {
        canvas::CanvasModel *cm = d->doc ? d->doc->canvas() : nullptr;
        const drawdance::CanvasState cs = cm ? cm->paintEngine()->historyCanvasState() : drawdance::CanvasState::null();
        if (!cs.isNull() && cs.width() > 0) {
            d->startHost(drawdance::CanvasState::null(), options, true);
        } else if (++*tries < 100) {
            QTimer::singleShot(50, this, *wait);
        } else {
            Q_EMIT notice(i18n("Could not create the canvas."));
        }
    };
    (*wait)();
}

void IkSession::leave()
{
    if (d->doc && d->doc->client()->isConnected()) d->doc->client()->disconnectFromServer();
}

void IkSession::showCanvas()
{
    if (d->state != Joined) return;
    if (d->kdoc) {
        if (d->mw) {
            for (QPointer<KisView> v : KisPart::instance()->views()) {
                if (v && v->document() == d->kdoc) {
                    d->mw->showView(v);
                    return;
                }
            }
            d->mw->addViewAndNotifyLoadingCompleted(d->kdoc);
        }
        return;
    }
    d->openKritaDocument();
}

void IkSession::sendChat(const QString &text)
{
    const QString t = text.trimmed();
    if (t.isEmpty() || !d->doc || !d->doc->client()->isLoggedIn()) return;
    const uint8_t ctx = uint8_t(localUserId());
    d->doc->client()->sendMessage(
        net::makeChatMessage(ctx, d->doc->isSessionPreserveChat() ? 0 : DP_MSG_CHAT_TFLAGS_BYPASS, 0, t));
}

void IkSession::kick(int userId, bool ban)
{
    if (d->doc && d->doc->client()->isLoggedIn()) d->doc->client()->sendMessage(net::ServerCommand::makeKick(userId, ban));
}

void IkSession::setOperator(int userId, bool op)
{
    if (!d->doc || !d->doc->canvas()) return;
    d->doc->client()->sendMessage(d->doc->canvas()->userlist()->getOpUserCommand(localUserId(), userId, op));
}

void IkSession::setTrusted(int userId, bool trusted)
{
    if (!d->doc || !d->doc->canvas()) return;
    d->doc->client()->sendMessage(d->doc->canvas()->userlist()->getTrustUserCommand(localUserId(), userId, trusted));
}

void IkSession::setLocked(int userId, bool locked)
{
    if (!d->doc || !d->doc->canvas()) return;
    d->doc->client()->sendMessage(d->doc->canvas()->userlist()->getLockUserCommand(localUserId(), userId, locked));
}

void IkSession::pointAt(const QPointF &imagePos)
{
    if (!d->doc || !d->doc->client()->isLoggedIn()) return;
    d->doc->client()->sendMessage(
        net::makeMovePointerMessage(uint8_t(localUserId()), qRound(imagePos.x() * 4), qRound(imagePos.y() * 4)));
}
