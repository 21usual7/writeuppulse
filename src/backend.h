#pragma once

#include <memory>

#include <QDateTime>
#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>
#include <QVariantList>

#include "archive.h"
#include "database.h"
#include "extractor.h"
#include "filepicker.h"
#include "progress.h"
#include "store.h"

// What the window shows: the saved days, the writeups of the current view
// (a day, the favorites, or a search across every day) after the filters, and
// the state of the last and current fetch. Fetches run outside the window,
// through the systemd user service, so closing the window never stops one.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList dates READ dates NOTIFY datesChanged)
    Q_PROPERTY(QString date READ date WRITE setDate NOTIFY dateChanged)
    Q_PROPERTY(QString view READ view WRITE setView NOTIFY filterChanged)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY filterChanged)
    Q_PROPERTY(QStringList categoryFilter READ categoryFilter WRITE setCategoryFilter NOTIFY filterChanged)
    Q_PROPERTY(QString levelFilter READ levelFilter WRITE setLevelFilter NOTIFY filterChanged)
    Q_PROPERTY(QString tagFilter READ tagFilter WRITE setTagFilter NOTIFY filterChanged)
    Q_PROPERTY(bool hideRead READ hideRead WRITE setHideRead NOTIFY filterChanged)
    Q_PROPERTY(QStringList shownCategories READ shownCategories NOTIFY digestChanged)
    Q_PROPERTY(int hiddenCount READ hiddenCount NOTIFY digestChanged)
    Q_PROPERTY(QVariantList items READ items NOTIFY digestChanged)
    Q_PROPERTY(bool running READ running NOTIFY statusChanged)
    Q_PROPERTY(QDateTime lastRun READ lastRun NOTIFY statusChanged)
    Q_PROPERTY(bool lastOk READ lastOk NOTIFY statusChanged)
    Q_PROPERTY(QString lastMessage READ lastMessage NOTIFY statusChanged)
    Q_PROPERTY(bool agentMissing READ agentMissing CONSTANT)
    Q_PROPERTY(bool timerAvailable READ timerAvailable NOTIFY timerChanged)
    Q_PROPERTY(bool timerEnabled READ timerEnabled NOTIFY timerChanged)
    Q_PROPERTY(QString problem READ problem NOTIFY problemChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(int progressRevision READ progressRevision NOTIFY progressChanged)
    Q_PROPERTY(int streak READ streak NOTIFY progressChanged)
    Q_PROPERTY(QVariantMap stats READ stats NOTIFY progressChanged)
    Q_PROPERTY(QStringList tags READ tags NOTIFY progressChanged)
    Q_PROPERTY(int favoriteCount READ favoriteCount NOTIFY progressChanged)
    Q_PROPERTY(QStringList levels READ levels CONSTANT)
    Q_PROPERTY(QStringList topics READ topics CONSTANT)
    Q_PROPERTY(QStringList searchCategories READ searchCategories WRITE setSearchCategories NOTIFY preferencesChanged)
    Q_PROPERTY(QStringList preferredCategories READ preferredCategories WRITE setPreferredCategories NOTIFY preferencesChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)
    Q_PROPERTY(bool saveTexts READ saveTexts WRITE setSaveTexts NOTIFY preferencesChanged)
    Q_PROPERTY(QVariantMap graph READ graph NOTIFY graphChanged)

public:
    explicit Backend(const QString &directory = {}, bool systemd = true, QObject *parent = nullptr);

    QStringList dates() const { return m_dates; }
    QString date() const { return m_date; }
    void setDate(const QString &date);
    QVariantList items() const { return m_items; }
    QString view() const { return m_view; }
    void setView(const QString &view);
    QString query() const { return m_query; }
    void setQuery(const QString &query);
    QStringList categoryFilter() const { return m_categoryFilter; }
    void setCategoryFilter(const QStringList &categories);
    QString levelFilter() const { return m_levelFilter; }
    void setLevelFilter(const QString &level);
    QString tagFilter() const { return m_tagFilter; }
    void setTagFilter(const QString &tag);
    bool hideRead() const { return m_hideRead; }
    void setHideRead(bool hide);
    QStringList shownCategories() const { return m_shownCategories; }
    int hiddenCount() const { return m_hiddenCount; }
    int favoriteCount() const { return m_progress.favorites().size(); }
    QStringList levels() const { return Store::levels(); }
    Q_INVOKABLE QString topicOf(const QString &category) const { return Store::topicOf(category); }
    QStringList topics() const { return Store::topics(); }
    Q_INVOKABLE QStringList categoriesOf(const QString &topic) const { return Store::categories(topic); }
    // Which directions the agent searches (empty means all) and which it
    // picks more often; kept in settings, where the fetch reads them.
    QStringList searchCategories() const { return m_searchCategories; }
    void setSearchCategories(const QStringList &categories);
    QStringList preferredCategories() const { return m_preferredCategories; }
    void setPreferredCategories(const QStringList &categories);
    QString notice() const { return m_notice; }
    // Whether the window downloads each writeup's page for search; on by
    // default, and only in a real session, never in tests.
    bool saveTexts() const { return m_saveTexts; }
    void setSaveTexts(bool save);

    // The picker answers asynchronously; the export is written when it does.
    void setFilePicker(FilePicker *picker);
    // Concepts are extracted with this local model from then on.
    void setLlm(LlmClient *llm);
    Q_INVOKABLE void exportMarkdown(bool week);
    bool running() const { return m_lockHeld || m_starting.isActive(); }
    QDateTime lastRun() const { return m_lastRun; }
    bool lastOk() const { return m_lastOk; }
    QString lastMessage() const { return m_lastMessage; }
    bool agentMissing() const { return m_agentMissing; }
    bool timerAvailable() const { return m_timerAvailable; }
    bool timerEnabled() const { return m_timerEnabled; }
    QString problem() const { return m_problem; }
    QString language() const { return m_language; }
    QString source() const { return m_source; }
    void setLanguage(const QString &language);
    void setSource(const QString &source);
    int progressRevision() const { return m_progressRevision; }
    int streak() const { return m_progress.streak(); }
    QVariantMap stats() const { return m_progress.stats(m_categories).toVariantMap(); }
    QStringList tags() const { return m_progress.tags(); }

    // QML reads a writeup's state through state(url) in bindings that also
    // read progressRevision, so they re-evaluate after every change.
    Q_INVOKABLE QVariantMap state(const QString &url) const { return m_progress.entry(url).toVariantMap(); }
    Q_INVOKABLE void setRead(const QVariantMap &item, bool read);
    Q_INVOKABLE void setLab(const QVariantMap &item, bool lab);
    Q_INVOKABLE void setFavorite(const QVariantMap &item, bool favorite);
    Q_INVOKABLE void setTags(const QString &url, const QStringList &tags);
    Q_INVOKABLE void setPracticeDone(const QString &url, int index, bool done);
    // result is "knew", "missed", or empty to take the answer back.
    Q_INVOKABLE void setCheck(const QVariantMap &item, const QString &question, const QString &result);

    // The concepts of the library and their relations, laid out: nodes with
    // x and y in [0, 1]. Worked out when first read after a change.
    QVariantMap graph();
    // One concept (see Database::conceptInfo) with its writeups as library items,
    // oldest first, and each piece of evidence with its writeup's title.
    Q_INVOKABLE QVariantMap conceptInfo(const QString &id) const;
    // Places nodes (from Database::graph) so related ones sit together:
    // a force layout from a fixed start, so the same graph looks the same.
    static void layout(QJsonArray &nodes, const QJsonArray &edges);

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setTimerEnabled(bool enabled);
    Q_INVOKABLE void olderDay() { step(1); }
    Q_INVOKABLE void newerDay() { step(-1); }

signals:
    void datesChanged();
    void digestChanged();
    void dateChanged();
    void filterChanged();
    void statusChanged();
    void timerChanged();
    void problemChanged();
    void languageChanged();
    void sourceChanged();
    void progressChanged();
    void preferencesChanged();
    void noticeChanged();
    void graphChanged();

private:
    void reload();
    void syncIfStale();
    void rebuild();
    void filtersChanged();
    void step(int delta);
    void setProblem(const QString &problem);
    bool systemctl(const QStringList &arguments, QString *output = nullptr);
    void readTimer();
    void progressSaved(bool ok);
    void saveExport(const QUrl &url);
    void setNotice(const QString &notice);
    void archive();
    void extract();
    QSet<QString> libraryUrls() const;
    void graphOutdated();

    Store m_store;
    Database m_database;
    Progress m_progress;
    Archive m_archive;
    std::unique_ptr<Extractor> m_extractor;
    bool m_saveTexts = true;
    QVariantMap m_graph;
    bool m_graphOutdated = true;
    int m_progressRevision = 0;
    int m_dataVersion = -1;
    // Canonical URL → the category its day has now, for the statistics.
    QHash<QString, QString> m_categories;
    bool m_systemd;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce, m_starting;
    QStringList m_dates;
    QString m_date;
    QVariantList m_library, m_items;
    QByteArray m_libraryStamp, m_statusStamp;
    QString m_view = "day";
    QString m_query, m_levelFilter, m_tagFilter;
    QStringList m_categoryFilter, m_shownCategories;
    bool m_hideRead = false;
    int m_hiddenCount = 0;
    QStringList m_searchCategories, m_preferredCategories;
    FilePicker *m_picker = nullptr;
    QString m_pendingExport;
    QString m_notice;
    QTimer m_noticeTimer;
    bool m_lockHeld = false;
    QDateTime m_lastRun;
    bool m_lastOk = true;
    QString m_lastMessage;
    bool m_agentMissing = false;
    bool m_timerAvailable = false;
    bool m_timerEnabled = false;
    QString m_problem;
    QString m_language;
    QString m_source;
    QTimer m_syncTimer;
};
