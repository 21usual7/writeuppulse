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
    if (ok && current < 2)
        for (const char *sql : {
                 "CREATE TABLE pages (url TEXT PRIMARY KEY, status TEXT NOT NULL, words INTEGER NOT NULL DEFAULT 0,"
                 " error TEXT, attempts INTEGER NOT NULL DEFAULT 0, at TEXT NOT NULL)",
                 "CREATE VIRTUAL TABLE passages USING fts5 (url UNINDEXED, n UNINDEXED, text,"
                 " tokenize = 'unicode61 remove_diacritics 2')"})
            ok = ok && run(sql);
    if (ok && current < 3)
        for (const char *sql : {
                 "CREATE TABLE concepts (id TEXT PRIMARY KEY, name TEXT NOT NULL, kind TEXT NOT NULL)",
                 "CREATE TABLE aliases (alias TEXT PRIMARY KEY, concept TEXT NOT NULL)",
                 "CREATE TABLE mentions (url TEXT NOT NULL, concept TEXT NOT NULL, PRIMARY KEY (url, concept))",
                 "CREATE INDEX mentions_by_concept ON mentions (concept)",
                 "CREATE TABLE edges (url TEXT NOT NULL, source TEXT NOT NULL, target TEXT NOT NULL, type TEXT NOT NULL,"
                 " passage INTEGER NOT NULL, weight INTEGER NOT NULL DEFAULT 1, model TEXT NOT NULL)",
                 "CREATE INDEX edges_by_url ON edges (url)",
                 "CREATE INDEX edges_by_source ON edges (source)",
                 "CREATE INDEX edges_by_target ON edges (target)",
                 "CREATE TABLE extractions (url TEXT PRIMARY KEY, status TEXT NOT NULL, source TEXT, model TEXT,"
                 " error TEXT, attempts INTEGER NOT NULL DEFAULT 0, at TEXT NOT NULL)"})
            ok = ok && run(sql);
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

bool Database::wantsPage(const QString &url) const {
    QSqlQuery query(connection());
    query.prepare("SELECT status, attempts, at FROM pages WHERE url = ?");
    query.addBindValue(url);
    if (!query.exec() || !query.next())
        return true;
    return query.value(0).toString() == "failed" && query.value(1).toInt() < 3
           && QDateTime::fromString(query.value(2).toString(), Qt::ISODate).secsTo(QDateTime::currentDateTime()) > 6 * 3600;
}

bool Database::savePage(const QString &url, const QString &status, const QStringList &passages, const QString &error) {
    QSqlDatabase db = connection();
    QSqlQuery query(db);
    bool ok = db.transaction();
    query.prepare("SELECT attempts FROM pages WHERE url = ?");
    query.addBindValue(url);
    const int attempts = status == "failed" && query.exec() && query.next() ? query.value(0).toInt() + 1 : status == "failed";
    query.prepare("DELETE FROM passages WHERE url = ?");
    query.addBindValue(url);
    ok = ok && query.exec();
    int words = 0;
    for (int i = 0; i < passages.size(); ++i) {
        words += passages.at(i).split(' ', Qt::SkipEmptyParts).size();
        query.prepare("INSERT INTO passages (url, n, text) VALUES (?, ?, ?)");
        query.addBindValue(url);
        query.addBindValue(i);
        query.addBindValue(passages.at(i));
        ok = ok && query.exec();
    }
    query.prepare("INSERT OR REPLACE INTO pages (url, status, words, error, attempts, at) VALUES (?, ?, ?, ?, ?, ?)");
    for (const QVariant &value : {QVariant(url), QVariant(status), QVariant(words), QVariant(error), QVariant(attempts),
                                  QVariant(QDateTime::currentDateTime().toString(Qt::ISODate))})
        query.addBindValue(value);
    ok = ok && query.exec();
    if (ok && db.commit())
        return true;
    db.rollback();
    return false;
}

QStringList Database::passages(const QString &url) const {
    QStringList list;
    QSqlQuery query(connection());
    query.prepare("SELECT text FROM passages WHERE url = ? ORDER BY n");
    query.addBindValue(url);
    if (query.exec())
        while (query.next())
            list << query.value(0).toString();
    return list;
}

QString Database::pageStatus(const QString &url) const {
    QSqlQuery query(connection());
    query.prepare("SELECT status FROM pages WHERE url = ?");
    query.addBindValue(url);
    return query.exec() && query.next() ? query.value(0).toString() : QString();
}

QHash<QString, double> Database::matchPages(const QString &word) const {
    QHash<QString, double> hits;
    QSqlQuery query(connection());
    // bm25() can't be used inside an aggregate, so the best passage per page
    // is picked here.
    query.prepare("SELECT url, bm25(passages) FROM passages WHERE passages MATCH ?");
    query.addBindValue("\"" + QString(word).replace('"', "\"\"") + "\"*");
    if (query.exec())
        while (query.next()) {
            const QString url = query.value(0).toString();
            const double score = query.value(1).toDouble();
            if (!hits.contains(url) || score < hits.value(url))
                hits.insert(url, score);
        }
    return hits;
}

bool Database::wantsConcepts(const QString &url) const {
    QSqlQuery query(connection());
    query.prepare("SELECT status, source, attempts, at FROM extractions WHERE url = ?");
    query.addBindValue(url);
    if (!query.exec() || !query.next())
        return true;
    const int attempts = query.value(2).toInt();
    const bool retry = attempts < 3
        && QDateTime::fromString(query.value(3).toString(), Qt::ISODate).secsTo(QDateTime::currentDateTime()) > 6 * 3600;
    if (query.value(0).toString() == "ok")
        return query.value(1).toString() == "summary" && pageStatus(url) == "ok" && (attempts == 0 || retry);
    return retry;
}

bool Database::saveConcepts(const QString &url, const QString &status, const QJsonObject &result, const QString &source,
                            const QString &model, const QString &error) {
    QSqlDatabase db = connection();
    QSqlQuery query(db);
    bool ok = db.transaction();
    auto exec = [&](const QString &sql, const QVariantList &values) {
        query.prepare(sql);
        for (const auto &value : values)
            query.addBindValue(value);
        ok = ok && query.exec();
    };
    query.prepare("SELECT status, attempts FROM extractions WHERE url = ?");
    query.addBindValue(url);
    const bool known = query.exec() && query.next();
    const QString now = QDateTime::currentDateTime().toString(Qt::ISODate);
    // A failure after a good run keeps the good concepts and only counts.
    if (status != "ok" && known && query.value(0).toString() == "ok") {
        exec("UPDATE extractions SET attempts = attempts + 1, error = ?, at = ? WHERE url = ?", {error, now, url});
        return ok && db.commit();
    }
    const int attempts = status == "ok" ? 0 : known ? query.value(1).toInt() + 1 : 1;
    if (status == "ok") {
        exec("DELETE FROM mentions WHERE url = ?", {url});
        exec("DELETE FROM edges WHERE url = ?", {url});
        // Each name and alias leads to the concept that first had it.
        QHash<QString, QString> resolved;
        for (const auto &value : result.value("concepts").toArray()) {
            const QJsonObject node = value.toObject();
            const QString id = node.value("id").toString();
            QStringList names{id};
            for (const auto &alias : node.value("aliases").toArray())
                names << alias.toString();
            QString target = id;
            for (const auto &name : names) {
                query.prepare("SELECT concept FROM aliases WHERE alias = ?");
                query.addBindValue(name);
                if (query.exec() && query.next()) {
                    target = query.value(0).toString();
                    break;
                }
            }
            resolved.insert(id, target);
            exec("INSERT OR IGNORE INTO concepts (id, name, kind) VALUES (?, ?, ?)",
                 {target, node.value("name").toString(), node.value("kind").toString()});
            for (const auto &name : names)
                exec("INSERT OR IGNORE INTO aliases (alias, concept) VALUES (?, ?)", {name, target});
            exec("INSERT OR IGNORE INTO mentions (url, concept) VALUES (?, ?)", {url, target});
        }
        for (const auto &value : result.value("edges").toArray()) {
            const QJsonObject edge = value.toObject();
            const QString from = resolved.value(edge.value("from").toString()), to = resolved.value(edge.value("to").toString());
            if (!from.isEmpty() && !to.isEmpty() && from != to)
                exec("INSERT INTO edges (url, source, target, type, passage, weight, model) VALUES (?, ?, ?, ?, ?, ?, ?)",
                     {url, from, to, edge.value("type").toString(), edge.value("passage").toInt(-1),
                      edge.value("weight").toInt(1), model});
        }
        exec("DELETE FROM concepts WHERE id NOT IN (SELECT concept FROM mentions)", {});
        exec("DELETE FROM aliases WHERE concept NOT IN (SELECT id FROM concepts)", {});
    }
    exec("INSERT OR REPLACE INTO extractions (url, status, source, model, error, attempts, at) VALUES (?, ?, ?, ?, ?, ?, ?)",
         {url, status, source, model, error, attempts, now});
    if (ok && db.commit())
        return true;
    db.rollback();
    return false;
}

QJsonObject Database::knowledge(const QString &url) const {
    QJsonArray concepts, edges;
    QSqlQuery query(connection());
    query.prepare("SELECT c.id, c.name, c.kind FROM mentions m JOIN concepts c ON c.id = m.concept WHERE m.url = ? ORDER BY c.name");
    query.addBindValue(url);
    if (query.exec())
        while (query.next())
            concepts.append(QJsonObject{{"id", query.value(0).toString()}, {"name", query.value(1).toString()},
                                        {"kind", query.value(2).toString()}});
    query.prepare("SELECT source, target, type, passage, weight, model FROM edges WHERE url = ? ORDER BY rowid");
    query.addBindValue(url);
    if (query.exec())
        while (query.next())
            edges.append(QJsonObject{{"from", query.value(0).toString()}, {"to", query.value(1).toString()},
                                     {"type", query.value(2).toString()}, {"passage", query.value(3).toInt()},
                                     {"weight", query.value(4).toInt()}, {"model", query.value(5).toString()}});
    return {{"concepts", concepts}, {"edges", edges}};
}

QString Database::conceptsStatus(const QString &url) const {
    QSqlQuery query(connection());
    query.prepare("SELECT status FROM extractions WHERE url = ?");
    query.addBindValue(url);
    return query.exec() && query.next() ? query.value(0).toString() : QString();
}
