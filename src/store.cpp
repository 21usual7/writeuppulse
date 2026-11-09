#include "store.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

Store::Store(const QString &directory)
    : m_directory(!directory.isEmpty() ? directory
                  : !qEnvironmentVariableIsEmpty("WRITEUPPULSE_DATA") ? qEnvironmentVariable("WRITEUPPULSE_DATA")
                  : QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/writeuppulse") {}

static QJsonObject readJson(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

// Written beside the target and renamed over it, so a reader never sees half
// a file and a crash never leaves one behind.
static bool writeAtomically(const QString &path, const QByteArray &data) {
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
}

// Newest first.
QStringList Store::dates() const {
    QStringList dates;
    for (const auto &name : QDir(m_directory).entryList({"*.json"}, QDir::Files, QDir::Name | QDir::Reversed)) {
        const QString date = name.chopped(5);
        if (QDate::fromString(date, Qt::ISODate).isValid())
            dates << date;
    }
    return dates;
}

QJsonObject Store::digest(const QString &date) const {
    return readJson(m_directory + "/" + date + ".json");
}

bool Store::saveDigest(const QJsonObject &digest, QString *error) const {
    const QString base = m_directory + "/" + digest.value("date").toString();
    if (!QDir().mkpath(m_directory)
        || !writeAtomically(base + ".json", QJsonDocument(digest).toJson(QJsonDocument::Indented))
        || !writeAtomically(base + ".md", markdown(digest).toUtf8())) {
        if (error)
            *error = QString("Could not write to %1.").arg(m_directory);
        return false;
    }
    return true;
}

QJsonObject Store::status() const {
    return readJson(m_directory + "/status.json");
}

bool Store::saveStatus(const QJsonObject &status) const {
    return QDir().mkpath(m_directory)
        && writeAtomically(m_directory + "/status.json", QJsonDocument(status).toJson(QJsonDocument::Indented));
}

// Links already shown recently, so the agent doesn't pick them again.
QStringList Store::recentUrls(int days) const {
    QStringList urls;
    for (const auto &date : dates().mid(0, days))
        for (const auto &item : digest(date).value("items").toArray())
            urls << item.toObject().value("url").toString();
    return urls;
}

static QStringList strings(const QJsonValue &value) {
    QStringList list;
    for (const auto &entry : value.toArray())
        if (!entry.toString().trimmed().isEmpty())
            list << entry.toString().trimmed();
    return list;
}

// The agent's output is untrusted: keep only complete items with web links,
// so the window never shows an empty card or opens a non-web URL.
QJsonObject Store::normalize(const QJsonObject &output, const QString &date, QString *error) {
    QJsonArray items;
    for (const auto &value : output.value("items").toArray()) {
        const QJsonObject item = value.toObject();
        const QString title = item.value("title").toString().trimmed();
        const QString summary = item.value("summary").toString().trimmed();
        const QUrl url(item.value("url").toString().trimmed(), QUrl::StrictMode);
        if (title.isEmpty() || summary.isEmpty() || !url.isValid() || url.host().isEmpty()
            || (url.scheme() != "https" && url.scheme() != "http"))
            continue;
        items.append(QJsonObject{
            {"title", title},
            {"url", url.toString()},
            {"source", item.value("source").toString().trimmed()},
            {"popularity", item.value("popularity").toString().trimmed()},
            {"why", item.value("why").toString().trimmed()},
            {"summary", summary},
            {"takeaways", QJsonArray::fromStringList(strings(item.value("takeaways")))},
            {"tags", QJsonArray::fromStringList(strings(item.value("tags")))},
        });
        if (items.size() == 10)
            break;
    }
    if (items.isEmpty()) {
        if (error)
            *error = "The agent returned no usable writeups.";
        return {};
    }
    return {{"date", date}, {"generatedAt", QDateTime::currentDateTime().toString(Qt::ISODate)}, {"items", items}};
}

QString Store::markdown(const QJsonObject &digest) {
    QString md = QString("# WriteupPulse — %1\n").arg(digest.value("date").toString());
    int number = 0;
    for (const auto &value : digest.value("items").toArray()) {
        const QJsonObject item = value.toObject();
        QStringList meta{item.value("source").toString(), item.value("popularity").toString()};
        meta.removeAll(QString());
        md += QString("\n## %1. %2\n\n<%3>").arg(++number).arg(item.value("title").toString(), item.value("url").toString());
        if (!meta.isEmpty())
            md += " · " + meta.join(" · ");
        md += "\n\n";
        if (!item.value("why").toString().isEmpty())
            md += "*" + item.value("why").toString() + "*\n\n";
        md += item.value("summary").toString() + "\n";
        const QStringList takeaways = strings(item.value("takeaways"));
        if (!takeaways.isEmpty())
            md += "\n**Takeaways**\n\n- " + takeaways.join("\n- ") + "\n";
    }
    return md;
}
