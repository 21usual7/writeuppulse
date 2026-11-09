#include "database.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>

#include "store.h"

Database::Database(const QString &directory)
    : m_name(QString("writeuppulse-%1").arg(quintptr(this))), m_path(directory + "/library/library.db") {
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", m_name);
    db.setDatabaseName(m_path);
    // A second window or `export` may hold the file for a moment.
    db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=5000");
    if (!db.open())
        m_error = QString("Could not open %1: %2").arg(m_path, db.lastError().text());
    else
        migrate(directory);
}

Database::~Database() {
    {
        QSqlDatabase db = QSqlDatabase::database(m_name, false);
        db.close();
    }
    QSqlDatabase::removeDatabase(m_name);
}

QSqlDatabase Database::connection() const {
    return QSqlDatabase::database(m_name, false);
}

int Database::dataVersion() const {
    QSqlQuery query(connection());
    return query.exec("PRAGMA data_version") && query.next() ? query.value(0).toInt() : -1;
}

// Each step moves the file one version up inside one transaction, so a crash
// or a second window halfway through leaves the earlier version whole.
bool Database::migrate(const QString &directory) {
    QSqlQuery query(connection());
    auto run = [&](const QString &sql) {
        if (query.exec(sql))
            return true;
        m_error = QString("Could not set up %1: %2").arg(m_path, query.lastError().text());
        return false;
    };
    if (!run("BEGIN IMMEDIATE"))
        return false;
    const int current = query.exec("PRAGMA user_version") && query.next() ? query.value(0).toInt() : 0;
    bool ok = true;
    if (current > version) {
        m_error = QString("%1 was written by a newer WriteupPulse.").arg(m_path);
        ok = false;
    }
    if (ok && current < 1) {
        for (const char *sql : {
                 "CREATE TABLE progress (url TEXT PRIMARY KEY, category TEXT, read TEXT, lab TEXT,"
                 " favorite INTEGER NOT NULL DEFAULT 0, tags TEXT, done TEXT)",
                 "CREATE TABLE events (id INTEGER PRIMARY KEY, url TEXT NOT NULL, kind TEXT NOT NULL,"
                 " subject TEXT NOT NULL, result TEXT NOT NULL, at TEXT NOT NULL)",
                 "CREATE INDEX events_by_url ON events (url, kind)",
                 "CREATE TABLE days (date TEXT PRIMARY KEY, stamp TEXT NOT NULL)",
                 "CREATE VIRTUAL TABLE writeups USING fts5 (date UNINDEXED, url UNINDEXED, title, summary,"
                 " details, tokenize = 'unicode61 remove_diacritics 2')"})
            ok = ok && run(sql);
        ok = ok && importProgress(directory + "/progress.json");
    }
    ok = ok && (current == version || run(QString("PRAGMA user_version = %1").arg(version))) && run("COMMIT");
    if (!ok)
        QSqlQuery(connection()).exec("ROLLBACK");
    return ok;
}

// progress.json from earlier versions comes in once, with its URLs made
// canonical; the file itself is left alone as a backup.
bool Database::importProgress(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return true;
    const QJsonObject items = QJsonDocument::fromJson(file.readAll()).object().value("items").toObject();
    QJsonObject merged;
    for (auto it = items.begin(); it != items.end(); ++it) {
        const QString url = Store::canonicalUrl(it.key());
        QJsonObject entry = merged.value(url).toObject();
        const QJsonObject old = it.value().toObject();
        for (auto field = old.begin(); field != old.end(); ++field)
            if (!entry.contains(field.key()))
                entry.insert(field.key(), field.value());
        merged.insert(url, entry);
    }
    for (auto it = merged.begin(); it != merged.end(); ++it)
        if (!saveEntry(it.key(), it.value().toObject())) {
            m_error = QString("Could not import %1.").arg(path);
            return false;
        }
    return true;
}

QJsonObject Database::entries() const {
    QJsonObject items;
    QSqlQuery query(connection());
    query.exec("SELECT url, category, read, lab, favorite, tags, done FROM progress");
    while (query.next()) {
        QJsonObject entry;
        if (!query.value(1).isNull())
            entry.insert("category", query.value(1).toString());
        if (!query.value(2).isNull())
            entry.insert("read", query.value(2).toString());
        if (!query.value(3).isNull())
            entry.insert("lab", query.value(3).toString());
        if (query.value(4).toBool())
            entry.insert("favorite", true);
        for (const auto &[column, name] : {std::pair{5, "tags"}, std::pair{6, "done"}}) {
            const QJsonArray list = QJsonDocument::fromJson(query.value(column).toByteArray()).array();
            if (!list.isEmpty())
                entry.insert(name, list);
        }
        items.insert(query.value(0).toString(), entry);
    }
    // Answers in order, so the latest one for a question wins.
    query.exec("SELECT url, subject, result FROM events WHERE kind = 'check' ORDER BY id");
    while (query.next()) {
        const QString url = query.value(0).toString();
        QJsonObject entry = items.value(url).toObject();
        QJsonObject checks = entry.value("checks").toObject();
        if (query.value(2).toString().isEmpty())
            checks.remove(query.value(1).toString());
        else
            checks.insert(query.value(1).toString(), query.value(2).toString());
        if (checks.isEmpty())
            entry.remove("checks");
        else
            entry.insert("checks", checks);
        if (entry.isEmpty())
            items.remove(url);
        else
            items.insert(url, entry);
    }
    return items;
}

bool Database::saveEntry(const QString &url, const QJsonObject &entry) {
    QSqlQuery query(connection());
    const QJsonArray tags = entry.value("tags").toArray(), done = entry.value("done").toArray();
    if (!entry.contains("read") && !entry.contains("lab") && !entry.value("favorite").toBool() && tags.isEmpty()
        && done.isEmpty()) {
        query.prepare("DELETE FROM progress WHERE url = ?");
        query.addBindValue(url);
        return query.exec();
    }
    auto text = [&](const char *field) { return entry.contains(field) ? entry.value(field).toVariant() : QVariant(); };
    auto list = [](const QJsonArray &values) {
        return values.isEmpty() ? QVariant() : QVariant(QJsonDocument(values).toJson(QJsonDocument::Compact));
    };
    query.prepare("INSERT OR REPLACE INTO progress (url, category, read, lab, favorite, tags, done)"
                  " VALUES (?, ?, ?, ?, ?, ?, ?)");
    for (const QVariant &value : {QVariant(url), text("category"), text("read"), text("lab"),
                                  QVariant(int(entry.value("favorite").toBool())), list(tags), list(done)})
        query.addBindValue(value);
    return query.exec();
}

bool Database::addCheck(const QString &url, const QString &question, const QString &result) {
    QSqlQuery query(connection());
    query.prepare("INSERT INTO events (url, kind, subject, result, at) VALUES (?, 'check', ?, ?, ?)");
    for (const QString &value : {url, question, result, QDateTime::currentDateTime().toString(Qt::ISODate)})
        query.addBindValue(value);
    return query.exec();
}

static QString stamp(const QString &path) {
    const QFileInfo info(path);
    return QString("%1:%2").arg(info.lastModified().toMSecsSinceEpoch()).arg(info.size());
}

bool Database::index(const Store &store, QString *error) {
    QSqlDatabase db = connection();
    QSqlQuery query(db);
    QHash<QString, QString> indexed;
    query.exec("SELECT date, stamp FROM days");
    while (query.next())
        indexed.insert(query.value(0).toString(), query.value(1).toString());

    QStringList changed;
    const QStringList dates = store.dates();
    for (const auto &date : dates)
        if (indexed.value(date) != stamp(store.directory() + "/" + date + ".json"))
            changed << date;
    QStringList removed;
    for (auto it = indexed.begin(); it != indexed.end(); ++it)
        if (!dates.contains(it.key()))
            removed << it.key();
    if (changed.isEmpty() && removed.isEmpty())
        return true;

    bool ok = db.transaction();
    for (const auto &date : changed + removed) {
        for (const char *sql : {"DELETE FROM writeups WHERE date = ?", "DELETE FROM days WHERE date = ?"}) {
            query.prepare(sql);
            query.addBindValue(date);
            ok = ok && query.exec();
        }
    }
    for (const auto &date : changed) {
        for (const auto &value : store.digest(date).value("items").toArray()) {
            const QJsonObject item = value.toObject();
            QStringList details{item.value("why").toString(), item.value("source").toString(),
                                item.value("category").toString()};
            for (const char *field : {"takeaways", "tags"})
                for (const auto &entry : item.value(field).toArray())
                    details << entry.toString();
            query.prepare("INSERT INTO writeups (date, url, title, summary, details) VALUES (?, ?, ?, ?, ?)");
            for (const QString &text : {date, item.value("url").toString(), item.value("title").toString(),
                                        item.value("summary").toString(), details.join('\n')})
                query.addBindValue(text);
            ok = ok && query.exec();
        }
        query.prepare("INSERT INTO days (date, stamp) VALUES (?, ?)");
        query.addBindValue(date);
        query.addBindValue(stamp(store.directory() + "/" + date + ".json"));
        ok = ok && query.exec();
    }
    if (ok && db.commit())
        return true;
    if (error)
        *error = QString("Could not index the saved days in %1: %2").arg(m_path, query.lastError().text());
    db.rollback();
    return false;
}

// Each word is one quoted prefix, so punctuation and FTS5 operators in what
// the user typed are only text; titles weigh most, then the summary.
QHash<QString, double> Database::match(const QString &word) const {
    QHash<QString, double> hits;
    QSqlQuery query(connection());
    query.prepare("SELECT date, url, bm25(writeups, 0, 0, 10, 4, 1) FROM writeups WHERE writeups MATCH ?");
    query.addBindValue("\"" + QString(word).replace('"', "\"\"") + "\"*");
    if (query.exec())
        while (query.next())
            hits.insert(query.value(0).toString() + "\t" + query.value(1).toString(), query.value(2).toDouble());
    return hits;
}
