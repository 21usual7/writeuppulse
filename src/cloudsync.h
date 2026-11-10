#pragma once

#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>

class Database;

// Keeps progress, answers, and notes the same on the user's computers through
// a small service (cloud/worker.js on Cloudflare). One secret, the sync key,
// is made on the first computer and typed on the others. From it come the
// account the service files rows under, the id of each row, and the key that
// encrypts it (AES-256-GCM), so the service holds ciphertext and opaque ids,
// never a URL or a note. Rows are last-write-wins by the time of the change;
// answers are never changed, only added.
class CloudSync : public QObject {
    Q_OBJECT
public:
    explicit CloudSync(Database &database, QObject *parent = nullptr);

    static QUrl defaultServer();
    static QString newKey();
    static bool validKey(const QString &key);
    // HMAC-SHA256 of a label under the key's secret.
    static QByteArray derive(const QString &key, const QByteArray &label);
    static QByteArray seal(const QByteArray &key, const QByteArray &plain);
    static QByteArray open(const QByteArray &key, const QByteArray &sealed, bool *ok);

    QString key() const { return m_key; }
    // A new key starts from the beginning of the account's rows.
    void setKey(const QString &key);
    bool busy() const { return m_busy; }
    void sync();

    QUrl server;
    int batch = 400;
    int timeoutMs = 30 * 1000;

signals:
    // `received` counts other computers' rows taken in.
    void finished(bool ok, const QString &message, int received);

private:
    void round();
    void done(bool ok, const QString &message);

    Database &m_database;
    QNetworkAccessManager m_network;
    QString m_key;
    QByteArray m_auth, m_rowKey, m_cipherKey;
    qint64 m_cursor = 0;
    bool m_busy = false;
    int m_received = 0, m_rounds = 0;
};
