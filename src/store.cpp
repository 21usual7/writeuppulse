#include "store.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
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

static const QList<QStringList> topicTable{
    {"Security", "Web", "Pwn", "Reverse", "Crypto", "Cloud Security", "Red Team", "Blue Team", "Malware", "OSINT"},
    {"AI / ML", "LLM & Agents", "ML Engineering"},
    {"Engineering", "Systems & Performance", "Backend & Databases", "DevOps & Infra", "Postmortems"},
    {"Other", "Other"},
};

QStringList Store::topics() {
    QStringList list;
    for (const auto &row : topicTable)
        list << row.first();
    return list;
}

QStringList Store::categories(const QString &topic) {
    QStringList list;
    for (const auto &row : topicTable)
        if (topic.isEmpty() || row.first() == topic)
            list << row.mid(1);
    return list;
}

QString Store::topicOf(const QString &category) {
    for (const auto &row : topicTable)
        if (row.indexOf(category, 1) > 0)
            return row.first();
    return "Other";
}

QStringList Store::levels() {
    return {"Junior", "Middle", "Senior"};
}

// The window shows agent text as plain text and the app teaches without
// code, so a fenced block the agent slipped in anyway is cut out.
static QString prose(const QJsonValue &value) {
    static const QRegularExpression fence("```.*?(```|$)", QRegularExpression::DotMatchesEverythingOption);
    return value.toString().remove(fence).trimmed();
}

static QStringList strings(const QJsonValue &value) {
    QStringList list;
    for (const auto &entry : value.toArray())
        if (!prose(entry).isEmpty())
            list << prose(entry);
    return list;
}

// The agent's output is untrusted: keep only complete items with web links,
// so the window never shows an empty card or opens a non-web URL.
QJsonObject Store::normalize(const QJsonObject &output, const QString &date, QString *error) {
    QJsonArray items;
    for (const auto &value : output.value("items").toArray()) {
        const QJsonObject item = value.toObject();
        const QString title = prose(item.value("title"));
        const QString summary = prose(item.value("summary"));
        const QUrl url(item.value("url").toString().trimmed(), QUrl::StrictMode);
        if (title.isEmpty() || summary.isEmpty() || !url.isValid() || url.host().isEmpty()
            || (url.scheme() != "https" && url.scheme() != "http"))
            continue;
        QJsonArray questions;
        for (const auto &entry : item.value("questions").toArray()) {
            const QString question = prose(entry.toObject().value("question"));
            const QString answer = prose(entry.toObject().value("answer"));
            if (!question.isEmpty() && !answer.isEmpty())
                questions.append(QJsonObject{{"question", question}, {"answer", answer}});
        }
        const QString category = item.value("category").toString();
        const QString level = item.value("level").toString();
        items.append(QJsonObject{
            {"title", title},
            {"url", url.toString()},
            {"source", prose(item.value("source"))},
            {"popularity", prose(item.value("popularity"))},
            {"category", categories().contains(category) ? category : "Other"},
            {"level", levels().contains(level) ? level : QString()},
            {"why", prose(item.value("why"))},
            {"summary", summary},
            {"takeaways", QJsonArray::fromStringList(strings(item.value("takeaways")))},
            {"questions", questions},
            {"practice", QJsonArray::fromStringList(strings(item.value("practice")))},
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
        QStringList meta{item.value("source").toString(), item.value("popularity").toString(),
                         item.value("category").toString(), item.value("level").toString()};
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
        const QJsonArray questions = item.value("questions").toArray();
        if (!questions.isEmpty()) {
            md += "\n**Self-check**\n\n";
            for (const auto &entry : questions)
                md += QString("- %1\n  *%2*\n").arg(entry.toObject().value("question").toString(),
                                                     entry.toObject().value("answer").toString());
        }
        const QStringList practice = strings(item.value("practice"));
        if (!practice.isEmpty())
            md += "\n**Try it yourself**\n\n- [ ] " + practice.join("\n- [ ] ") + "\n";
    }
    return md;
}
