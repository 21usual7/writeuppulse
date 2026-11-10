#pragma once

#include <QHash>
#include <QJsonArray>
#include <QSet>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>

class Store;

// The local SQLite file, <data>/library/library.db. It keeps what the user did
// (progress and self-check answers), which nothing can rebuild, a full-text
// index of the saved days, which reindexing can, and the text of the
// writeups' own pages, which the Archive downloads, and the concepts and
// relations the Extractor's local model found in them. It sits in a
// subdirectory so its journal doesn't wake the window's watcher of the data
// directory. Only the window and `export` open it; fetches and syncs never do.
class Database {
public:
    explicit Database(const QString &directory);
    ~Database();
    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    static constexpr int version = 4;

    QString path() const { return m_path; }
    // Empty when the file opened and is at the current version.
    QString error() const { return m_error; }
    // Changes when another connection, a second window for one, commits.
    int dataVersion() const;

    // Progress keyed by canonical URL, each entry with "checks" (question →
    // "knew" or "missed") from the latest self-check answers.
    QJsonObject entries() const;
    // An entry with nothing left in it is deleted.
    bool saveEntry(const QString &url, const QJsonObject &entry);
    // Every answer is kept; an empty result takes an earlier one back.
    bool addCheck(const QString &url, const QString &question, const QString &result);
    // Every self-check answer, oldest first: {url, question, result, at}.
    QJsonArray checks() const;

    // The reader's own note on a writeup (Markdown), and the Notion page it
    // was last sent to. An empty text deletes the note.
    QString note(const QString &url) const;
    bool saveNote(const QString &url, const QString &text);
    QString notionPage(const QString &url) const;
    bool setNotionPage(const QString &url, const QString &page);

    // Brings the index in line with the days on disk: changed days again,
    // removed days out.
    bool index(const Store &store, QString *error);
    // Writeups with a word starting with this one, as "<date>\t<url>" → BM25
    // score (lower is better).
    QHash<QString, double> match(const QString &word) const;

    // True while a page has no text saved yet and isn't given up on: a
    // failed download is tried again after six hours, three times in all.
    bool wantsPage(const QString &url) const;
    // status: "ok" with passages, or "failed", "skipped", "refused" with error.
    bool savePage(const QString &url, const QString &status, const QStringList &passages, const QString &error);
    QStringList passages(const QString &url) const;
    // "ok", "failed", "skipped", "refused", or empty when never tried.
    QString pageStatus(const QString &url) const;
    // Pages with a word starting with this one, canonical URL → best BM25.
    QHash<QString, double> matchPages(const QString &word) const;

    // True while a writeup has no concepts yet, or has them only from its
    // summary while its page text is saved now; a failure is tried again
    // after six hours, three times in all.
    bool wantsConcepts(const QString &url) const;
    // result is what Extractor::validate returns; status "ok", or "failed"
    // with error. source: "page" (edge passages index passages(url)) or
    // "summary" (passage -1). A concept whose name or alias another writeup
    // already gave joins that concept.
    bool saveConcepts(const QString &url, const QString &status, const QJsonObject &result, const QString &source,
                      const QString &model, const QString &error = {});
    // {"concepts": [{id, name, kind}], "edges": [{from, to, type, passage, weight, model}]}
    // for one writeup.
    QJsonObject knowledge(const QString &url) const;
    QString conceptsStatus(const QString &url) const;
    // The concepts of these writeups (canonical URLs, so a removed day's
    // concepts drop out), each with the writeups naming it, and their
    // relations summed over those writeups: {"nodes": [{id, name, kind, urls}],
    // "edges": [{from, to, weight, writeups}], "done", "failed"}, the last two
    // counting the writeups whose extraction finished or was given up on.
    QJsonObject graph(const QSet<QString> &urls) const;
    // One concept among these writeups: {id, name, kind, aliases, urls,
    // models, neighbours: [{id, name, kind, weight, evidence: [{url, passage,
    // weight, text}]}]}, strongest neighbour first, text being the passage
    // (empty for a summary). Empty when no writeup names it.
    QJsonObject conceptInfo(const QString &id, const QSet<QString> &urls) const;

private:
    QSqlDatabase connection() const;
    bool migrate(const QString &directory);
    bool importProgress(const QString &path);

    QString m_name;
    QString m_path;
    QString m_error;
};
