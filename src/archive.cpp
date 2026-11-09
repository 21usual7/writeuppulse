#include "archive.h"

#include <QHostInfo>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QStringDecoder>

#include "database.h"
#include "store.h"

Archive::Archive(Database &database, QObject *parent) : QObject(parent), m_database(database) {}

void Archive::enqueue(const QStringList &urls) {
    for (const auto &url : urls)
        if (!m_queue.contains(url) && url != m_current && m_database.wantsPage(Store::canonicalUrl(url)))
            m_queue << url;
    if (m_current.isEmpty())
        next();
}

// Loopback, private, link-local, unique-local, carrier-grade NAT,
// multicast, and unspecified addresses are all refused; an IPv4 address
// written as IPv6 is judged as IPv4.
bool Archive::isPublic(const QHostAddress &address) {
    bool mapped = false;
    const quint32 v4 = address.toIPv4Address(&mapped);
    if (mapped && address.protocol() == QAbstractSocket::IPv6Protocol)
        return isPublic(QHostAddress(v4));
    if (address.protocol() == QAbstractSocket::IPv4Protocol && (v4 >> 22) == (100u << 2 | 1u))
        return false;  // 100.64.0.0/10
    return address.isGlobal() && !address.isPrivateUse() && !address.isUniqueLocalUnicast()
           && !address.isMulticast() && !address.isLinkLocal() && !address.isLoopback();
}

void Archive::next() {
    m_current.clear();
    if (m_queue.isEmpty())
        return;
    m_current = m_queue.takeFirst();
    get(QUrl(m_current), 0);
}

void Archive::get(const QUrl &url, int redirects) {
    if (!url.isValid() || (url.scheme() != "https" && url.scheme() != "http") || url.host().isEmpty())
        return finish("refused", {}, "Not a web link.");
    // Resolved here and checked before any byte is sent. The request
    // resolves the name again, so a name that changes its answer in
    // between could still slip through; there is no cookie or login to
    // send, and every redirect is checked the same way.
    QHostInfo::lookupHost(url.host(), this, [this, url, redirects](const QHostInfo &host) {
        if (host.error() != QHostInfo::NoError || host.addresses().isEmpty())
            return finish("failed", {}, host.errorString());
        for (const auto &address : host.addresses())
            if (!allowPrivate && !isPublic(address))
                return finish("refused", {}, QString("%1 is not a public address.").arg(address.toString()));

        QNetworkRequest request(url);
        request.setTransferTimeout(timeoutMs);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        request.setHeader(QNetworkRequest::UserAgentHeader, "WriteupPulse");
        request.setRawHeader("Accept", "text/html, application/xhtml+xml, text/plain;q=0.8");
        QNetworkReply *reply = m_network.get(request);
        connect(reply, &QNetworkReply::downloadProgress, reply, [this, reply](qint64 received) {
            if (received > limit)
                reply->abort();
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply, url, redirects] {
            reply->deleteLater();
            const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            // Relative locations are common; the typed header drops them.
            const QUrl location(QString::fromUtf8(reply->rawHeader("Location")).trimmed());
            if (code >= 300 && code < 400 && !location.isEmpty()) {
                if (redirects >= 5)
                    return finish("failed", {}, "Too many redirects.");
                return get(url.resolved(location), redirects + 1);
            }
            const QByteArray data = reply->readAll();
            if (reply->error() == QNetworkReply::OperationCanceledError || data.size() > limit)
                return finish("failed", {}, "The page is too large.");
            if (reply->error() != QNetworkReply::NoError)
                return finish("failed", {}, reply->errorString());
            const QString type = reply->header(QNetworkRequest::ContentTypeHeader).toString().toLower();
            QString page;
            if (type.startsWith("text/html") || type.startsWith("application/xhtml"))
                page = text(data);
            else if (type.startsWith("text/plain"))
                page = QString::fromUtf8(data).trimmed();
            else
                return finish("skipped", {}, QString("Not a web page (%1).").arg(type));
            if (page.isEmpty())
                return finish("skipped", {}, "No text on the page.");
            finish("ok", page);
        });
    });
}

void Archive::finish(const QString &status, const QString &text, const QString &error) {
    const QString url = Store::canonicalUrl(m_current);
    m_database.savePage(url, status, passages(text), error);
    emit saved(url);
    next();
}

static QString decodeEntities(const QString &text) {
    static const QHash<QString, QString> named{
        {"amp", "&"}, {"lt", "<"}, {"gt", ">"}, {"quot", "\""}, {"apos", "'"}, {"nbsp", " "},
        {"mdash", "—"}, {"ndash", "–"}, {"hellip", "…"}, {"rsquo", "’"}, {"lsquo", "‘"},
        {"rdquo", "”"}, {"ldquo", "“"}, {"laquo", "«"}, {"raquo", "»"}, {"copy", "©"}};
    static const QRegularExpression entity("&(#[0-9]{1,7}|#[xX][0-9a-fA-F]{1,6}|[a-zA-Z]{2,8});");
    QString out;
    qsizetype last = 0;
    for (auto it = entity.globalMatch(text); it.hasNext();) {
        const auto match = it.next();
        const QString name = match.captured(1);
        QString value = named.value(name, match.captured(0));
        if (name.startsWith('#')) {
            bool ok = false;
            const uint code = name.startsWith("#x", Qt::CaseInsensitive) ? name.mid(2).toUInt(&ok, 16) : name.mid(1).toUInt(&ok);
            value = ok && code > 0 && code <= 0x10FFFF && (code < 0xD800 || code > 0xDFFF)
                    ? QString::fromUcs4(reinterpret_cast<const char32_t *>(&code), 1) : QString();
        }
        out += text.mid(last, match.capturedStart() - last) + value;
        last = match.capturedEnd();
    }
    return out + text.mid(last);
}

QString Archive::text(const QByteArray &data) {
    QStringDecoder decoder = QStringDecoder::decoderForHtml(data);
    QString html = decoder.isValid() ? decoder.decode(data) : QString::fromUtf8(data);

    const auto options = QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption;
    static const QRegularExpression comments("<!--.*?-->", options);
    static const QRegularExpression noise(
        "<(script|style|noscript|template|svg|iframe|pre|nav|header|footer|aside|form)\\b[^>]*>.*?</\\1\\s*>", options);
    html.remove(comments).remove(noise);
    // The article itself when the page marks it, without the sidebars.
    static const QRegularExpression main("<main\\b[^>]*>(.*)</main\\s*>", options);
    static const QRegularExpression article("<article\\b[^>]*>(.*)</article\\s*>", options);
    if (const auto found = main.match(html); found.hasMatch())
        html = found.captured(1);
    else if (const auto found = article.match(html); found.hasMatch())
        html = found.captured(1);

    static const QRegularExpression blocks(
        "</?(p|div|h[1-6]|li|ul|ol|br|tr|table|section|blockquote|dd|dt|figcaption|article|main)\\b[^>]*>", options);
    static const QRegularExpression tags("<[^>]*>", options);
    html.replace(blocks, "\n").remove(tags);
    html = decodeEntities(html);

    QStringList lines;
    static const QRegularExpression spaces("[ \\t\\x{00a0}\\r\\f\\v]+");
    for (QString line : html.split('\n')) {
        line = line.replace(spaces, " ").trimmed();
        if (!line.isEmpty())
            lines << line;
    }
    return lines.join('\n').left(200000);
}

QStringList Archive::passages(const QString &text) {
    QStringList list;
    QString passage;
    for (const auto &line : text.split('\n', Qt::SkipEmptyParts)) {
        if (!passage.isEmpty() && passage.size() + line.size() > 1000) {
            list << passage;
            passage.clear();
        }
        passage += (passage.isEmpty() ? "" : "\n") + line.left(2000);
    }
    if (!passage.isEmpty())
        list << passage;
    return list;
}
