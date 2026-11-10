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
#include <QUrlQuery>
#include <QXmlStreamWriter>

#include "progress.h"
#include "references.h"

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

QString Store::canonicalUrl(const QString &text) {
    QUrl url(text.trimmed());
    if (!url.isValid() || url.host().isEmpty())
        return text.trimmed();
    if (url.scheme() == "http")
        url.setScheme("https");
    if (url.port() == 80 || url.port() == 443)
        url.setPort(-1);
    if (url.host().startsWith("www."))
        url.setHost(url.host().mid(4));
    url.setFragment({});
    QUrlQuery query(url);
    for (const auto &[key, value] : query.queryItems())
        if (key.startsWith("utm_") || key == "fbclid" || key == "gclid")
            query.removeAllQueryItems(key);
    url.setQuery(query.isEmpty() ? QString() : query.query(QUrl::FullyEncoded));
    QString path = url.path(QUrl::FullyEncoded);
    while (path.endsWith('/'))
        path.chop(1);
    url.setPath(path, QUrl::TolerantMode);
    return url.toString(QUrl::FullyEncoded);
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

QString Store::markdown(const QJsonObject &digest, const Progress *progress) {
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
        const QJsonObject state = progress ? progress->entry(item.value("url").toString()) : QJsonObject();
        QStringList marks;
        if (state.contains("read"))
            marks << "✓ Read";
        if (state.contains("lab"))
            marks << "✓ Done in lab";
        if (state.value("favorite").toBool())
            marks << "★";
        for (const auto &tag : state.value("tags").toArray())
            marks << "#" + tag.toString();
        if (!marks.isEmpty())
            md += marks.join(" · ") + "\n\n";
        if (!item.value("why").toString().isEmpty())
            md += "*" + item.value("why").toString() + "*\n\n";
        md += item.value("summary").toString() + "\n";
        const QStringList takeaways = strings(item.value("takeaways"));
        if (!takeaways.isEmpty())
            md += "\n**Takeaways**\n\n- " + takeaways.join("\n- ") + "\n";
        const QJsonArray questions = item.value("questions").toArray();
        if (!questions.isEmpty()) {
            md += "\n**Self-check**\n\n";
            const QJsonObject checks = state.value("checks").toObject();
            for (const auto &entry : questions) {
                const QString question = entry.toObject().value("question").toString();
                const QString result = checks.value(question).toString();
                md += QString("- %1%2\n  *%3*\n").arg(question, result == "knew" ? " ✓" : result == "missed" ? " ✗" : "",
                                                       entry.toObject().value("answer").toString());
            }
        }
        const QStringList practice = strings(item.value("practice"));
        const QJsonArray done = state.value("done").toArray();
        if (!practice.isEmpty())
            md += "\n**Try it yourself**\n\n";
        for (int i = 0; i < practice.size(); ++i)
            md += QString("- [%1] %2\n").arg(done.contains(i) ? "x" : " ", practice.at(i));
    }
    return md;
}

QString Store::exportMarkdown(const QString &date, bool week, const Progress *progress) const {
    const QDate last = QDate::fromString(date, Qt::ISODate);
    QStringList days;
    for (const auto &day : dates()) {
        const QDate d = QDate::fromString(day, Qt::ISODate);
        if (d == last || (week && d < last && d > last.addDays(-7)))
            days.prepend(day);
    }
    QStringList parts;
    for (const auto &day : days)
        parts << markdown(digest(day), progress);
    return parts.join("\n---\n\n");
}

// The site gets each day re-checked like fresh agent output, so the cost and
// anything else private stay home, and the topic comes along for the colours.
// The index covers every day already in the directory, so publishing from a
// machine with fewer saved days never drops older ones from the site.
bool Store::publish(const QString &directory, QString *error) const {
    if (!QDir().mkpath(directory)) {
        if (error)
            *error = QString("Could not create %1.").arg(directory);
        return false;
    }
    for (const auto &date : dates()) {
        const QJsonObject saved = digest(date);
        QJsonObject day = normalize(saved, date, nullptr);
        if (day.isEmpty())
            continue;
        QJsonArray items;
        for (const auto &value : day.value("items").toArray()) {
            QJsonObject item = value.toObject();
            item.insert("topic", topicOf(item.value("category").toString()));
            item.insert("terms", terms(item));
            items.append(item);
        }
        day.insert("items", items);
        day.insert("generatedAt", saved.value("generatedAt"));
        if (!writeAtomically(directory + "/" + date + ".json", QJsonDocument(day).toJson(QJsonDocument::Indented))) {
            if (error)
                *error = QString("Could not write to %1.").arg(directory);
            return false;
        }
    }
    const Store site(directory);
    QJsonArray days;
    for (const auto &date : site.dates()) {
        const QJsonObject published = site.digest(date);
        QJsonArray items;
        for (const auto &value : published.value("items").toArray()) {
            const QJsonObject item = value.toObject();
            items.append(QJsonObject{
                {"title", item.value("title")},
                {"source", item.value("source")},
                {"category", item.value("category")},
                {"topic", topicOf(item.value("category").toString())},
                {"level", item.value("level")},
                {"tags", item.value("tags")},
            });
        }
        days.append(QJsonObject{{"date", date}, {"generatedAt", published.value("generatedAt")}, {"items", items}});
    }
    QFile labs(":/labs/labs.json");
    if (!writeAtomically(directory + "/index.json", QJsonDocument(QJsonObject{{"days", days}}).toJson(QJsonDocument::Compact))
        || !labs.open(QIODevice::ReadOnly) || !writeAtomically(directory + "/labs.json", labs.readAll())
        || !writeAtomically(directory + "/feed.xml", site.feed())) {
        if (error)
            *error = QString("Could not write to %1.").arg(directory);
        return false;
    }
    return true;
}

QJsonArray Store::terms(const QJsonObject &item) {
    QStringList texts{item.value("title").toString(), item.value("why").toString(), item.value("summary").toString()};
    for (const auto &takeaway : item.value("takeaways").toArray())
        texts << takeaway.toString();
    QJsonArray list;
    for (const auto &hit : References::instance().find({texts.join('\n')})) {
        const QJsonObject entry = References::instance().entry(hit.id);
        list.append(QJsonObject{{"id", hit.id}, {"name", entry.value("name")}, {"kind", entry.value("kind")},
                                {"text", entry.value("text")}, {"url", entry.value("url")}});
        if (list.size() == 8)
            break;
    }
    return list;
}

// Atom, newest first, the last 30 writeups; each entry links to the original
// article and carries the summary as text.
QByteArray Store::feed() const {
    // Atom wants a time zone; the days keep local time.
    auto atomTime = [](const QString &local, const QString &fallback) {
        const QDateTime time = QDateTime::fromString(local, Qt::ISODate);
        return (time.isValid() ? time : QDateTime::fromString(fallback, Qt::ISODate)).toUTC().toString(Qt::ISODate);
    };
    QByteArray xml;
    QXmlStreamWriter out(&xml);
    out.setAutoFormatting(true);
    out.writeStartDocument();
    out.writeStartElement("feed");
    out.writeDefaultNamespace("http://www.w3.org/2005/Atom");
    out.writeTextElement("title", "WriteupPulse");
    out.writeTextElement("subtitle", "Five technical writeups a day, security first, picked and summarized by an agent.");
    out.writeTextElement("id", "urn:writeuppulse:feed");
    const QStringList days = dates();
    out.writeTextElement("updated", atomTime(days.isEmpty() ? QString() : digest(days.first()).value("generatedAt").toString(),
                                             QDateTime::currentDateTime().toString(Qt::ISODate)));
    int count = 0;
    for (const auto &date : days) {
        const QJsonObject day = digest(date);
        const QString updated = atomTime(day.value("generatedAt").toString(), date + "T08:00:00");
        const QJsonArray items = day.value("items").toArray();
        for (int i = 0; i < items.size() && count < 30; ++i, ++count) {
            const QJsonObject item = items.at(i).toObject();
            out.writeStartElement("entry");
            out.writeTextElement("id", QString("urn:writeuppulse:%1:%2").arg(date).arg(i + 1));
            out.writeTextElement("title", item.value("title").toString());
            out.writeTextElement("updated", updated);
            out.writeEmptyElement("link");
            out.writeAttribute("href", item.value("url").toString());
            out.writeEmptyElement("category");
            out.writeAttribute("term", item.value("category").toString("Other"));
            out.writeStartElement("summary");
            out.writeAttribute("type", "text");
            out.writeCharacters(item.value("summary").toString());
            out.writeEndElement();
            out.writeEndElement();
        }
    }
    out.writeEndElement();
    out.writeEndDocument();
    return xml;
}
