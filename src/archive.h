#pragma once

#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QObject>
#include <QStringList>
#include <QUrl>

class Database;

// Saves the text of each writeup's own page in the Database, cut into
// passages, for full-text search now and for citing passages later. Claude
// is never asked for it: the window downloads the page itself, one at a
// time, once per page (a failed one is tried again up to three times).
//
// Links come from the agent or the site's feed, so they are untrusted: only
// http(s), only hosts that resolve to public addresses (checked again at
// every redirect, which is followed by hand), a size cap, a timeout, and no
// scripts run. Code blocks are left out, like everywhere else in the app.
class Archive : public QObject {
    Q_OBJECT

public:
    explicit Archive(Database &database, QObject *parent = nullptr);

    // Queues the pages the database doesn't have yet, in the given order.
    void enqueue(const QStringList &urls);
    // Drops what is queued; a download already running still finishes.
    void clear() { m_queue.clear(); }

    // The readable text of an HTML page: the article or main element when
    // the page marks one, without scripts, navigation, or code blocks.
    static QString text(const QByteArray &html);
    // About 1000 characters each, along paragraph breaks.
    static QStringList passages(const QString &text);
    static bool isPublic(const QHostAddress &address);

    // Tests serve pages from this machine.
    bool allowPrivate = false;
    // Some personal sites take 20 s to answer.
    int timeoutMs = 45 * 1000;
    qint64 limit = 4 << 20;

signals:
    // A page was saved or given up on; url is canonical.
    void saved(const QString &url);

private:
    void next();
    void get(const QUrl &url, int redirects);
    void finish(const QString &status, const QString &text = {}, const QString &error = {});

    Database &m_database;
    QNetworkAccessManager m_network;
    QStringList m_queue;
    QString m_current;
};
