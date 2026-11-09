#include "syncer.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSettings>

static const char *defaultFeed = "https://21usual7.github.io/writeuppulse/data/";

Syncer::Syncer(const Store &store, const QUrl &feed)
    : m_store(store),
      m_feed(!feed.isEmpty() ? feed
             : !qEnvironmentVariableIsEmpty("WRITEUPPULSE_FEED") ? QUrl(qEnvironmentVariable("WRITEUPPULSE_FEED"))
             : QUrl(defaultFeed)) {
    if (!m_feed.path().endsWith('/'))
        m_feed.setPath(m_feed.path() + '/');
}

QString Syncer::source() {
    return QSettings("writeuppulse", "writeuppulse").value("source").toString() == "claude" ? "claude" : "feed";
}

void Syncer::setSource(const QString &source) {
    QSettings("writeuppulse", "writeuppulse").setValue("source", source == "claude" ? "claude" : "feed");
}

// One file from the feed, cut off past a size no real day reaches.
bool Syncer::get(const QString &name, qint64 limit, QByteArray *data, QString *error) const {
    QNetworkAccessManager network;
    QNetworkRequest request(m_feed.resolved(QUrl(name)));
    request.setTransferTimeout(timeoutMs);
    request.setHeader(QNetworkRequest::UserAgentHeader, "WriteupPulse");
    QNetworkReply *reply = network.get(request);
    bool tooBig = false;
    QObject::connect(reply, &QNetworkReply::downloadProgress, reply, [&](qint64 received) {
        if (received > limit) {
            tooBig = true;
            reply->abort();
        }
    });
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    *data = reply->readAll();
    const bool ok = !tooBig && reply->error() == QNetworkReply::NoError && data->size() <= limit;
    if (!ok)
        *error = tooBig || data->size() > limit ? QString("%1 is too large.").arg(name)
                                                : QString("Could not download %1: %2").arg(name, reply->errorString());
    delete reply;
    return ok;
}

bool Syncer::run(QString *error, int *count) {
    QDir().mkpath(m_store.directory());
    QLockFile lock(m_store.lockPath());
    if (!lock.tryLock(0)) {
        *error = "Another fetch is already running.";
        return false;
    }

    int saved = 0;
    QByteArray data;
    bool ok = get("index.json", 4 << 20, &data, error);
    if (ok) {
        const QJsonArray index = QJsonDocument::fromJson(data).object().value("days").toArray();
        if (index.isEmpty() && !QJsonDocument::fromJson(data).isObject()) {
            *error = "The feed's index is not valid JSON.";
            ok = false;
        }
        for (qsizetype i = 0; ok && i < qMin<qsizetype>(index.size(), days); ++i) {
            const QJsonObject entry = index[i].toObject();
            // The date becomes a file name, so only a real ISO date will do.
            const QString date = entry.value("date").toString();
            if (QDate::fromString(date, Qt::ISODate).toString(Qt::ISODate) != date)
                continue;
            const QJsonObject local = m_store.digest(date);
            const QString published = entry.value("generatedAt").toString();
            if (!local.isEmpty() && (published.isEmpty() || local.value("generatedAt").toString() == published))
                continue;
            if (!get(date + ".json", 2 << 20, &data, error)) {
                ok = false;
                break;
            }
            const QJsonObject remote = QJsonDocument::fromJson(data).object();
            QJsonObject digest = Store::normalize(remote, date, nullptr);
            if (digest.isEmpty())
                continue;
            digest.insert("generatedAt", remote.value("generatedAt"));
            if (!m_store.saveDigest(digest, error)) {
                ok = false;
                break;
            }
            ++saved;
        }
    }
    if (count)
        *count = saved;
    m_store.saveStatus({{"lastRun", QDateTime::currentDateTime().toString(Qt::ISODate)},
                        {"ok", ok},
                        {"date", QDate::currentDate().toString(Qt::ISODate)},
                        {"count", saved},
                        {"message", ok ? QString() : *error}});
    return ok;
}
