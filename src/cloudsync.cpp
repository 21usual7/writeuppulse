#include "cloudsync.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageAuthenticationCode>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSettings>

#include <openssl/evp.h>

#include "database.h"

namespace {
const QByteArray prefix = "wp1-";
constexpr qint64 replyLimit = 16 * 1024 * 1024;

QByteArray secretOf(const QString &key) {
    return QByteArray::fromBase64(key.mid(prefix.size()).toLatin1(),
                                  QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}
}

CloudSync::CloudSync(Database &database, QObject *parent)
    : QObject(parent), server(defaultServer()), m_database(database) {
    const QSettings settings("writeuppulse", "writeuppulse");
    const QString key = settings.value("cloudKey").toString();
    if (validKey(key)) {
        setKey(key);
        m_cursor = settings.value("cloudCursor", 0).toLongLong();
    }
}

QUrl CloudSync::defaultServer() {
    return QUrl(qEnvironmentVariable("WRITEUPPULSE_CLOUD", "https://writeuppulse-sync.21usual7.workers.dev/"));
}

QString CloudSync::newKey() {
    QByteArray secret(32, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(secret.data()), secret.size() / 4);
    return QString::fromLatin1(prefix + secret.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

bool CloudSync::validKey(const QString &key) {
    static const QRegularExpression pattern("^wp1-[A-Za-z0-9_-]{43}$");
    return pattern.match(key).hasMatch();
}

QByteArray CloudSync::derive(const QString &key, const QByteArray &label) {
    return QMessageAuthenticationCode::hash(label, secretOf(key), QCryptographicHash::Sha256);
}

// nonce (12) | ciphertext | tag (16)
QByteArray CloudSync::seal(const QByteArray &key, const QByteArray &plain) {
    QByteArray nonce(12, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(nonce.data()), 3);
    QByteArray out(plain.size(), Qt::Uninitialized), tag(16, Qt::Uninitialized);
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    int length = 0, extra = 0;
    const bool ok = ctx
        && EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, reinterpret_cast<const uchar *>(key.constData()),
                              reinterpret_cast<const uchar *>(nonce.constData()))
        && EVP_EncryptUpdate(ctx, reinterpret_cast<uchar *>(out.data()), &length,
                             reinterpret_cast<const uchar *>(plain.constData()), plain.size())
        && EVP_EncryptFinal_ex(ctx, reinterpret_cast<uchar *>(out.data()) + length, &extra)
        && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag.data());
    EVP_CIPHER_CTX_free(ctx);
    return ok ? nonce + out.left(length + extra) + tag : QByteArray();
}

QByteArray CloudSync::open(const QByteArray &key, const QByteArray &sealed, bool *ok) {
    *ok = false;
    if (sealed.size() < 28)
        return {};
    const QByteArray nonce = sealed.left(12), body = sealed.mid(12, sealed.size() - 28);
    QByteArray tag = sealed.right(16), out(body.size(), Qt::Uninitialized);
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    int length = 0, extra = 0;
    *ok = ctx
        && EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, reinterpret_cast<const uchar *>(key.constData()),
                              reinterpret_cast<const uchar *>(nonce.constData()))
        && EVP_DecryptUpdate(ctx, reinterpret_cast<uchar *>(out.data()), &length,
                             reinterpret_cast<const uchar *>(body.constData()), body.size())
        && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, tag.data())
        && EVP_DecryptFinal_ex(ctx, reinterpret_cast<uchar *>(out.data()) + length, &extra) > 0;
    EVP_CIPHER_CTX_free(ctx);
    return *ok ? out.left(length + extra) : QByteArray();
}

void CloudSync::setKey(const QString &key) {
    QSettings settings("writeuppulse", "writeuppulse");
    if (key != settings.value("cloudKey").toString()) {
        settings.setValue("cloudCursor", 0);
        m_cursor = 0;
    }
    if (key.isEmpty())
        settings.remove("cloudKey");
    else
        settings.setValue("cloudKey", key);
    m_key = key;
    m_auth = key.isEmpty() ? QByteArray() : derive(key, "writeuppulse auth").toHex();
    m_rowKey = key.isEmpty() ? QByteArray() : derive(key, "writeuppulse row");
    m_cipherKey = key.isEmpty() ? QByteArray() : derive(key, "writeuppulse cipher");
}

void CloudSync::sync() {
    if (m_busy || m_key.isEmpty())
        return;
    m_busy = true;
    m_received = 0;
    m_rounds = 0;
    round();
}

// One request: up to `batch` of our changes up, everything newer than the
// cursor down (the service sends at most 1000 rows and says if there's more).
void CloudSync::round() {
    const QJsonArray pending = m_database.outgoing(batch);
    QJsonArray rows;
    for (const auto &value : pending) {
        const QJsonObject row = value.toObject();
        const QByteArray plain = QJsonDocument(QJsonObject{{"kind", row.value("kind")}, {"key", row.value("key")},
                                                           {"value", row.value("value")}}).toJson(QJsonDocument::Compact);
        const QByteArray id = QMessageAuthenticationCode::hash(row.value("kind").toString().toUtf8() + '\n'
                                                                   + row.value("key").toString().toUtf8(),
                                                               m_rowKey, QCryptographicHash::Sha256).toHex();
        rows.append(QJsonObject{{"id", QString::fromLatin1(id)},
                                {"data", QString::fromLatin1(seal(m_cipherKey, plain).toBase64())},
                                {"updated", row.value("changed")}});
    }
    QNetworkRequest request(server.resolved(QUrl("sync")));
    request.setTransferTimeout(timeoutMs);
    request.setHeader(QNetworkRequest::UserAgentHeader, "WriteupPulse");
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", "Bearer " + m_auth);
    QNetworkReply *reply = m_network.post(request, QJsonDocument(QJsonObject{{"since", m_cursor}, {"rows", rows}}).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64) {
        if (received > replyLimit)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, pending] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError || status != 200) {
            const QString message = QJsonDocument::fromJson(body).object().value("error").toString();
            done(false, message.isEmpty() ? reply->errorString() : message);
            return;
        }
        const QJsonObject answer = QJsonDocument::fromJson(body).object();
        m_database.markSent(pending);
        for (const auto &value : answer.value("rows").toArray()) {
            const QJsonObject row = value.toObject();
            m_cursor = qMax(m_cursor, row.value("seq").toInteger());
            bool ok = false;
            const QJsonObject plain = QJsonDocument::fromJson(
                open(m_cipherKey, QByteArray::fromBase64(row.value("data").toString().toLatin1()), &ok)).object();
            const QString kind = plain.value("kind").toString(), key = plain.value("key").toString();
            if (!ok || !QStringList{"progress", "note", "event"}.contains(kind) || key.isEmpty())
                continue;
            if (m_database.applyRemote(kind, key, row.value("updated").toInteger(), plain.value("value").toObject()))
                ++m_received;
        }
        QSettings("writeuppulse", "writeuppulse").setValue("cloudCursor", m_cursor);
        if ((answer.value("more").toBool() || pending.size() == batch) && ++m_rounds < 50)
            round();
        else
            done(true, {});
    });
}

void CloudSync::done(bool ok, const QString &message) {
    m_busy = false;
    emit finished(ok, message, m_received);
}
