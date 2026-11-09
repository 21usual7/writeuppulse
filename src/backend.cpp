#include "backend.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

#include "fetcher.h"

static QByteArray stamp(const QString &path);

Backend::Backend(const QString &directory, bool systemd, QObject *parent)
    : QObject(parent), m_store(directory), m_progress(m_store.directory()), m_systemd(systemd) {
    QDir().mkpath(m_store.directory());
    m_agentMissing = Fetcher::findClaude().isEmpty();
    m_language = Fetcher::language();
    const QSettings settings("writeuppulse", "writeuppulse");
    m_deepMode = settings.value("deepMode", false).toBool();
    m_hideRead = settings.value("hideRead", false).toBool();

    // A fetch writes its files and lock into the data directory, so watching
    // that one directory follows timer runs and manual ones alike.
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(150);
    connect(&m_debounce, &QTimer::timeout, this, &Backend::reload);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_debounce.start(); });
    m_watcher.addPath(m_store.directory());

    // Covers the moment between asking systemd for a fetch and the fetch
    // taking its lock, so the button doesn't flicker back.
    m_starting.setSingleShot(true);
    m_starting.setInterval(20000);
    connect(&m_starting, &QTimer::timeout, this, &Backend::statusChanged);

    if (m_systemd)
        readTimer();
    reload();
}

void Backend::setLanguage(const QString &language) {
    if (language == m_language)
        return;
    Fetcher::setLanguage(language);
    m_language = language;
    emit languageChanged();
}

void Backend::progressSaved(bool ok) {
    if (!ok)
        setProblem(tr("Could not save your progress to %1.").arg(m_store.directory()));
    m_progressStamp = stamp(m_store.directory() + "/progress.json");
    ++m_progressRevision;
    emit progressChanged();
    // Hiding read writeups and the favorites view both depend on progress.
    rebuild();
}

void Backend::filtersChanged() {
    emit filterChanged();
    rebuild();
}

void Backend::setView(const QString &view) {
    if (view == m_view)
        return;
    m_view = view;
    m_query.clear();
    m_tagFilter.clear();
    filtersChanged();
}

void Backend::setQuery(const QString &query) {
    if (query != m_query) {
        m_query = query;
        filtersChanged();
    }
}

void Backend::setCategoryFilter(const QStringList &categories) {
    if (categories != m_categoryFilter) {
        m_categoryFilter = categories;
        filtersChanged();
    }
}

void Backend::setLevelFilter(const QString &level) {
    if (level != m_levelFilter) {
        m_levelFilter = level;
        filtersChanged();
    }
}

void Backend::setTagFilter(const QString &tag) {
    if (tag != m_tagFilter) {
        m_tagFilter = tag;
        filtersChanged();
    }
}

void Backend::setHideRead(bool hide) {
    if (hide != m_hideRead) {
        m_hideRead = hide;
        QSettings("writeuppulse", "writeuppulse").setValue("hideRead", hide);
        filtersChanged();
    }
}

void Backend::setDeepMode(bool deep) {
    if (deep != m_deepMode) {
        m_deepMode = deep;
        QSettings("writeuppulse", "writeuppulse").setValue("deepMode", deep);
        emit filterChanged();
    }
}

// The writeups the window shows: the source the view picks (a day, the
// favorites, or every day when searching), narrowed by the filters. Each
// item carries its date, and old digests get the fields they lack.
void Backend::rebuild() {
    const QStringList favorites = m_progress.favorites();
    const bool searching = !m_query.trimmed().isEmpty();
    QVariantList source;
    for (const auto &value : m_library) {
        const QVariantMap item = value.toMap();
        if (searching || (m_view == "favorites" ? favorites.contains(item.value("url").toString())
                                                : m_view == "day" && item.value("date") == m_date))
            source << item;
    }

    QStringList shown;
    for (const auto &category : Store::categories())
        for (const auto &value : source)
            if (value.toMap().value("category") == category && !shown.contains(category))
                shown << category;

    // A search hit that a filter hides counts as hidden; a miss doesn't.
    const QStringList words = m_query.toLower().split(' ', Qt::SkipEmptyParts);
    QVariantList items;
    int hidden = 0;
    for (const auto &value : source) {
        const QVariantMap item = value.toMap();
        const QJsonObject state = m_progress.entry(item.value("url").toString());
        const QStringList userTags = state.value("tags").toVariant().toStringList();
        if (!words.isEmpty()) {
            const QString haystack = QStringList{item.value("title").toString(), item.value("summary").toString(),
                item.value("why").toString(), item.value("source").toString(), item.value("category").toString(),
                item.value("takeaways").toStringList().join(' '), item.value("tags").toStringList().join(' '),
                userTags.join(' ')}.join(' ').toLower();
            if (!std::all_of(words.begin(), words.end(), [&](const QString &word) { return haystack.contains(word); }))
                continue;
        }
        if ((!m_categoryFilter.isEmpty() && !m_categoryFilter.contains(item.value("category").toString()))
            || (!m_levelFilter.isEmpty() && item.value("level") != m_levelFilter)
            || (m_hideRead && state.contains("read"))
            || (!m_tagFilter.isEmpty() && !userTags.contains(m_tagFilter, Qt::CaseInsensitive))) {
            ++hidden;
            continue;
        }
        items << item;
    }

    m_hiddenCount = hidden;
    if (items != m_items || shown != m_shownCategories) {
        m_items = items;
        m_shownCategories = shown;
        emit digestChanged();
    }
}

void Backend::setRead(const QVariantMap &item, bool read) {
    progressSaved(m_progress.setRead(item.value("url").toString(), item.value("category").toString(), read));
}

void Backend::setLab(const QVariantMap &item, bool lab) {
    progressSaved(m_progress.setLab(item.value("url").toString(), item.value("category").toString(), lab));
}

void Backend::setFavorite(const QVariantMap &item, bool favorite) {
    progressSaved(m_progress.setFavorite(item.value("url").toString(), item.value("category").toString(), favorite));
}

void Backend::setTags(const QString &url, const QStringList &tags) {
    progressSaved(m_progress.setTags(url, tags));
}

void Backend::setPracticeDone(const QString &url, int index, bool done) {
    progressSaved(m_progress.setPracticeDone(url, index, done));
}

void Backend::setDate(const QString &date) {
    if (!m_dates.contains(date) || (date == m_date && m_view == "day" && m_query.isEmpty()))
        return;
    m_date = date;
    emit dateChanged();
    m_view = "day";
    m_query.clear();
    m_tagFilter.clear();
    filtersChanged();
}

void Backend::step(int delta) {
    const int index = m_dates.indexOf(m_date) + delta;
    if (index >= 0 && index < m_dates.size())
        setDate(m_dates.at(index));
}

void Backend::setProblem(const QString &problem) {
    if (problem != m_problem) {
        m_problem = problem;
        emit problemChanged();
    }
}

static QByteArray stamp(const QString &path) {
    const QFileInfo info(path);
    return info.exists() ? QByteArray::number(info.lastModified().toMSecsSinceEpoch()) + ":" + QByteArray::number(info.size())
                         : QByteArray();
}

void Backend::reload() {
    // Deleting and recreating the directory drops the watch; re-arm it every time.
    if (!m_watcher.directories().contains(m_store.directory()))
        m_watcher.addPath(m_store.directory());

    const QStringList dates = m_store.dates();
    if (dates != m_dates) {
        // Stay on the day the user picked, but jump to a new day as it lands.
        const bool follow = m_date.isEmpty() || (!m_dates.isEmpty() && m_date == m_dates.first());
        m_dates = dates;
        emit datesChanged();
        if (follow || !m_dates.contains(m_date)) {
            m_date = m_dates.value(0);
            emit dateChanged();
        }
    }

    // Every day is loaded at once: search and favorites span them all, and a
    // year of digests is still only a few megabytes.
    QByteArray libraryStamp;
    for (const auto &date : m_dates)
        libraryStamp += date.toUtf8() + "@" + stamp(m_store.directory() + "/" + date + ".json") + ";";
    bool rebuildNeeded = false;
    if (libraryStamp != m_libraryStamp) {
        m_libraryStamp = libraryStamp;
        m_library.clear();
        for (const auto &date : m_dates)
            for (const auto &value : m_store.digest(date).value("items").toArray()) {
                QVariantMap item = value.toObject().toVariantMap();
                item.insert("date", date);
                if (!Store::categories().contains(item.value("category").toString()))
                    item.insert("category", "Other");
                m_library << item;
            }
        rebuildNeeded = true;
    }

    // Another window may have marked something; pick it up.
    const QByteArray progressStamp = stamp(m_store.directory() + "/progress.json");
    if (progressStamp != m_progressStamp) {
        m_progressStamp = progressStamp;
        m_progress.load();
        ++m_progressRevision;
        emit progressChanged();
        rebuildNeeded = true;
    }
    if (rebuildNeeded)
        rebuild();

    // QLockFile removes a lock left by a dead process, but only when it tries
    // to take it; reading the owner's pid doesn't touch the directory, which
    // would wake this watcher again.
    qint64 pid = 0;
    QString host, app;
    QLockFile probe(m_store.lockPath());
    const bool held = QFile::exists(m_store.lockPath()) && probe.getLockInfo(&pid, &host, &app)
                      && QFileInfo::exists(QString("/proc/%1").arg(pid));
    const QByteArray statusStamp = stamp(m_store.directory() + "/status.json");
    const bool statusMoved = statusStamp != m_statusStamp;
    if (held || statusMoved)
        m_starting.stop();
    if (held != m_lockHeld || statusMoved) {
        m_lockHeld = held;
        m_statusStamp = statusStamp;
        const QJsonObject status = m_store.status();
        m_lastRun = QDateTime::fromString(status.value("lastRun").toString(), Qt::ISODate);
        m_lastOk = status.value("ok").toBool(true);
        m_lastMessage = status.value("message").toString();
        emit statusChanged();
    }
}

bool Backend::systemctl(const QStringList &arguments, QString *output) {
    QProcess process;
    process.start("systemctl", QStringList{"--user"} + arguments);
    if (!process.waitForStarted(3000) || !process.waitForFinished(10000))
        return false;
    if (output)
        *output = QString::fromUtf8(process.readAllStandardOutput() + process.readAllStandardError()).trimmed();
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

void Backend::readTimer() {
    QString state;
    systemctl({"is-enabled", "writeuppulse.timer"}, &state);
    const bool available = !state.isEmpty() && state != "not-found" && !state.contains("No such file");
    const bool enabled = state == "enabled";
    if (available != m_timerAvailable || enabled != m_timerEnabled) {
        m_timerAvailable = available;
        m_timerEnabled = enabled;
        emit timerChanged();
    }
}

void Backend::refresh() {
    if (running())
        return;
    if (m_agentMissing) {
        setProblem(tr("Claude Code isn't installed, so there is no agent to fetch writeups. Install it and sign in with `claude`."));
        return;
    }
    setProblem({});

    // Through systemd when the package is installed, so the fetch outlives
    // the window and lands in the journal; straight from this binary otherwise.
    if (!(m_systemd && systemctl({"start", "--no-block", "writeuppulse.service"}))
        && !QProcess::startDetached(QCoreApplication::applicationFilePath(), {"fetch"})) {
        setProblem(tr("Could not start a fetch."));
        return;
    }
    m_starting.start();
    emit statusChanged();
}

void Backend::setTimerEnabled(bool enabled) {
    QString output;
    if (!systemctl({enabled ? "enable" : "disable", "--now", "writeuppulse.timer"}, &output))
        setProblem(tr("Could not change the daily timer: %1").arg(output));
    else
        setProblem({});
    readTimer();
}
