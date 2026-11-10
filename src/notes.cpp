#include "notes.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>

Notes::Notes(QObject *parent) : QObject(parent), m_network(new QNetworkAccessManager(this)) {
    api = QUrl(qEnvironmentVariableIsEmpty("WRITEUPPULSE_NOTION") ? QString("https://api.notion.com")
                                                                  : qEnvironmentVariable("WRITEUPPULSE_NOTION"));
}

static QString quoted(const QString &text) {
    return "\"" + QString(text).replace('\\', "\\\\").replace('"', "\\\"").replace('\n', ' ') + "\"";
}

// Obsidian tags can't hold spaces or most punctuation.
static QString tag(const QString &text) {
    static const QRegularExpression other("[^\\p{L}\\p{N}_/-]+");
    return text.toLower().replace(other, "-").remove(QRegularExpression("^-+|-+$"));
}

QString Notes::markdown(const QVariantMap &item, const QString &note, const QStringList &terms, const QStringList &tags) {
    QStringList tagList{"writeuppulse"};
    for (const auto &t : QStringList{item.value("category").toString()} + tags)
        if (!tag(t).isEmpty() && !tagList.contains(tag(t)))
            tagList << tag(t);
    QString text = "---\n";
    text += "title: " + quoted(item.value("title").toString()) + "\n";
    text += "url: " + quoted(item.value("url").toString()) + "\n";
    text += "date: " + item.value("date").toString() + "\n";
    if (!item.value("category").toString().isEmpty())
        text += "category: " + quoted(item.value("category").toString()) + "\n";
    if (!item.value("level").toString().isEmpty())
        text += "level: " + item.value("level").toString() + "\n";
    text += "tags: [" + tagList.join(", ") + "]\n---\n\n";
    QString body = "# " + item.value("title").toString() + "\n\n[" + QObject::tr("Original article") + "]("
                   + item.value("url").toString() + ")\n\n";
    if (!item.value("summary").toString().isEmpty())
        body += "## " + QObject::tr("Summary") + "\n\n" + item.value("summary").toString() + "\n\n";
    const QVariantList takeaways = item.value("takeaways").toList();
    if (!takeaways.isEmpty()) {
        body += "## " + QObject::tr("Takeaways") + "\n\n";
        for (const auto &takeaway : takeaways)
            body += "- " + takeaway.toString() + "\n";
        body += "\n";
    }
    if (!terms.isEmpty()) {
        QStringList links;
        for (const auto &term : terms)
            links << "[[" + QString(term).remove(QRegularExpression("[\\[\\]|#^]")) + "]]";
        body += "## " + QObject::tr("Terms") + "\n\n" + links.join(" · ") + "\n\n";
    }
    body += "## " + QObject::tr("My notes") + "\n\n" + note.trimmed() + "\n";
    return text + body;
}

QString Notes::fileName(const QVariantMap &item) {
    static const QRegularExpression unsafe("[\\\\/:*?\"<>|#^\\[\\]\\x00-\\x1f]+");
    QString title = item.value("title").toString().replace(unsafe, " ").simplified().left(80).trimmed();
    if (title.isEmpty())
        title = "Writeup";
    return item.value("date").toString() + " " + title + ".md";
}

// The app's mark in the file: a hash of what it wrote, so it can tell
// whether the reader has changed the file since.
static QByteArray mark(const QString &text) {
    return QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex().left(16);
}

bool Notes::writeToVault(const QString &folder, const QVariantMap &item, const QString &note, const QStringList &terms,
                         const QStringList &tags, QString *path, QString *error) {
    const QString directory = QDir(folder).filePath("WriteupPulse");
    const QString file = QDir(directory).filePath(fileName(item));
    if (path)
        *path = file;
    const QString markFile = QDir(directory).filePath(".writeuppulse");
    QSettings marks(markFile, QSettings::IniFormat);
    QFile existing(file);
    if (existing.open(QIODevice::ReadOnly)) {
        const QByteArray last = marks.value(QString(mark(file))).toByteArray();
        if (last.isEmpty() || mark(QString::fromUtf8(existing.readAll())) != last) {
            if (error)
                *error = QObject::tr("%1 was changed in your notes app, so it was left as it is.").arg(QDir(folder).relativeFilePath(file));
            return false;
        }
    }
    const QString text = markdown(item, note, terms, tags);
    QSaveFile out(file);
    if (!QDir().mkpath(directory) || !out.open(QIODevice::WriteOnly) || out.write(text.toUtf8()) < 0 || !out.commit()) {
        if (error)
            *error = QObject::tr("Could not write %1.").arg(file);
        return false;
    }
    marks.setValue(QString(mark(file)), mark(text));
    marks.sync();
    return true;
}

QString Notes::notionId(const QString &text) {
    static const QRegularExpression id("([0-9a-fA-F]{8})-?([0-9a-fA-F]{4})-?([0-9a-fA-F]{4})-?([0-9a-fA-F]{4})-?([0-9a-fA-F]{12})(?![0-9a-fA-F])");
    QRegularExpressionMatch match;
    // A page link ends with the ID: take the last one in it.
    for (auto it = id.globalMatch(text); it.hasNext();)
        match = it.next();
    return match.hasMatch() ? (match.captured(1) + match.captured(2) + match.captured(3) + match.captured(4) + match.captured(5)).toLower()
                            : QString();
}

// Notion takes at most 2000 characters in one piece of text.
static QJsonArray richText(const QString &text) {
    QJsonArray pieces;
    for (int i = 0; i < text.size(); i += 2000)
        pieces.append(QJsonObject{{"type", "text"}, {"text", QJsonObject{{"content", text.mid(i, 2000)}}}});
    return pieces;
}

static QJsonObject block(const QString &type, const QString &text) {
    return {{"object", "block"}, {"type", type}, {type, QJsonObject{{"rich_text", richText(text)}}}};
}

void Notes::sendToNotion(const QVariantMap &item, const QString &note, const QStringList &terms, const QString &token,
                         const QString &parent, const QString &oldPage) {
    if (m_reply)
        return;
    m_item = item;
    m_token = token;
    QJsonArray children;
    const QString url = item.value("url").toString();
    if (url.startsWith("https://") || url.startsWith("http://"))
        children.append(QJsonObject{{"object", "block"}, {"type", "bookmark"}, {"bookmark", QJsonObject{{"url", url}}}});
    auto paragraphs = [&](const QString &text) {
        for (const auto &paragraph : text.split(QRegularExpression("\\n\\s*\\n"), Qt::SkipEmptyParts))
            children.append(block("paragraph", paragraph.trimmed()));
    };
    if (!item.value("summary").toString().isEmpty()) {
        children.append(block("heading_2", tr("Summary")));
        paragraphs(item.value("summary").toString());
    }
    const QVariantList takeaways = item.value("takeaways").toList();
    if (!takeaways.isEmpty()) {
        children.append(block("heading_2", tr("Takeaways")));
        for (const auto &takeaway : takeaways)
            children.append(block("bulleted_list_item", takeaway.toString()));
    }
    if (!terms.isEmpty()) {
        children.append(block("heading_2", tr("Terms")));
        children.append(block("paragraph", terms.join(" · ")));
    }
    children.append(block("heading_2", tr("My notes")));
    paragraphs(note);
    // A page takes at most 100 blocks in one request.
    while (children.size() > 100)
        children.removeLast();
    m_body = QJsonDocument(QJsonObject{
        {"parent", QJsonObject{{"page_id", parent}}},
        {"properties", QJsonObject{{"title", QJsonObject{{"title", richText(item.value("title").toString().left(2000))}}}}},
        {"children", children}}).toJson(QJsonDocument::Compact);

    if (oldPage.isEmpty()) {
        create();
        return;
    }
    // The old page goes to the trash; a page already gone is fine.
    QNetworkRequest request(api.resolved(QUrl("/v1/pages/" + oldPage)));
    request.setRawHeader("Authorization", "Bearer " + token.toUtf8());
    request.setRawHeader("Notion-Version", "2022-06-28");
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(30000);
    m_reply = m_network->sendCustomRequest(request, "PATCH", QJsonDocument(QJsonObject{{"archived", true}}).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 401) {
            emit sent(m_item.value("url").toString(), false, {}, tr("Notion didn't accept the token."));
            return;
        }
        create();
    });
}

void Notes::create() {
    QNetworkRequest request(api.resolved(QUrl("/v1/pages")));
    request.setRawHeader("Authorization", "Bearer " + m_token.toUtf8());
    request.setRawHeader("Notion-Version", "2022-06-28");
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(30000);
    m_reply = m_network->post(request, m_body);
    connect(m_reply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        reply->deleteLater();
        const QJsonObject answer = QJsonDocument::fromJson(reply->readAll().left(1 << 20)).object();
        const QString url = m_item.value("url").toString();
        if (reply->error() == QNetworkReply::NoError && !answer.value("id").toString().isEmpty())
            emit sent(url, true, answer.value("id").toString(), {});
        else
            emit sent(url, false, {}, answer.value("message").toString(reply->errorString()));
    });
}
