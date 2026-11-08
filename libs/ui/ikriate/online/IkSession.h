/*
 * SPDX-FileCopyrightText: 2026 IKriate contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef IKSESSION_H
#define IKSESSION_H

#include <QColor>
#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QSize>
#include <QUrl>
#include <QVector>

#include "kritaui_export.h"

class KisDocument;
class KisMainWindow;

/**
 * An online drawing session, compatible with Drawpile's servers and clients.
 *
 * IKriate runs Drawpile's own engine (drawdance) and networking code for the
 * shared canvas and mirrors it into an ordinary Krita document (see
 * IkSessionMirror), so every IKriate tool works on it. Joining, hosting
 * (on this device with Drawpile's built-in server, or on a server), people,
 * chat and moderation go through this class; the interface for it is in
 * IkOnlinePage (home screen) and IkSessionUi (over the canvas).
 *
 * There is one session at a time.
 */
class KRITAUI_EXPORT IkSession : public QObject
{
    Q_OBJECT
public:
    enum State { Idle, Connecting, Joined };

    struct Person {
        int id {0};
        QString name;
        bool local {false};
        bool op {false};
        bool trusted {false};
        bool locked {false};
        bool muted {false};
        bool mod {false};
    };

    struct ChatLine {
        int userId {0}; ///< 0 for messages from the server or IKriate
        QString name;
        QString text;
        QDateTime when;
    };

    struct HostOptions {
        enum Where { ThisDevice, PublicServer, OwnServer };
        QString title;
        Where where {ThisDevice};
        QString address;     ///< OwnServer: host[:port]
        QString password;
        bool listed {false}; ///< announced on the public session list
        bool nsfm {false};
    };

    struct RecentSession {
        QString title;
        QString address;
        QDateTime when;
        bool hosted {false};
    };

    static IkSession *instance();

    State state() const;
    bool isActive() const { return state() != Idle; }
    QString title() const;
    QString serverName() const;
    /// Link others can join with (drawpile://host:port/session).
    QString inviteLink() const;
    int latencyMs() const;
    /// The Krita document showing the shared canvas (null until it has one).
    KisDocument *document() const;

    QVector<Person> people() const;
    int localUserId() const;
    bool isOperator() const;
    QString nameOf(int userId) const;
    /// A colour for each participant (cursor, avatar, chat).
    static QColor colorFor(int userId);
    const QVector<ChatLine> &chat() const;

    /// The name shown to others (Settings › Online, asked the first time).
    static QString displayName();
    static void setDisplayName(const QString &name);
    static QVector<RecentSession> recentSessions();
    /// Drawpile's public session list.
    static QUrl publicListingUrl();

    void setMainWindow(KisMainWindow *mainWindow);

public Q_SLOTS:
    /// @p address: a drawpile:// link, or host[:port][/session].
    void join(const QString &address);
    /// Shares @p source (a copy of it: the original file is not touched).
    void hostDocument(KisDocument *source, const HostOptions &options);
    /// Shares the artwork at @p path (an IKriate or Krita file).
    void hostFile(const QString &path, const HostOptions &options);
    /// Shares a new blank canvas.
    void hostBlank(const QSize &size, const QColor &background, const HostOptions &options);
    void leave();
    /// Opens the shared canvas again (after going back to the gallery).
    void showCanvas();
    void sendChat(const QString &text);
    /// Moderation (operators only).
    void kick(int userId, bool ban = false);
    void setOperator(int userId, bool op);
    void setTrusted(int userId, bool trusted);
    void setLocked(int userId, bool locked);
    /// Points at a spot of the canvas for the others (image coordinates).
    void pointAt(const QPointF &imagePos);

Q_SIGNALS:
    void stateChanged();
    void titleChanged();
    void peopleChanged();
    void chatChanged();
    void latencyChanged();
    /// Something went wrong or the server said something (shown as a notice).
    void notice(const QString &message);
    /// Another participant's cursor (image coordinates); x < 0 hides it.
    void cursorMoved(int userId, int x, int y);
    void laser(int userId, int x, int y, const QColor &color);

private:
    explicit IkSession(QObject *parent = nullptr);
    ~IkSession() override;
    struct Private;
    Private *const d;
    friend struct Private;
};

#endif // IKSESSION_H
