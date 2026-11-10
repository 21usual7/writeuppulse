#include "backend.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QPointF>
#include <QtMath>
#include <QProcess>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

#include "fetcher.h"
#include "syncer.h"

static QByteArray stamp(const QString &path);

Backend::Backend(const QString &directory, bool systemd, QObject *parent)
    : QObject(parent), m_store(directory), m_database(m_store.directory()), m_progress(m_database), m_archive(m_database), m_systemd(systemd) {
    QDir().mkpath(m_store.directory());
    if (!m_database.error().isEmpty())
        setProblem(tr("Your progress can't be saved: %1").arg(m_database.error()));
    m_agentMissing = Fetcher::findClaude().isEmpty();
    m_language = Fetcher::language();
    m_source = Syncer::source();
    const QSettings settings("writeuppulse", "writeuppulse");
    m_hideRead = settings.value("hideRead", false).toBool();
    m_searchCategories = settings.value("categories").toStringList();
    m_preferredCategories = settings.value("preferred").toStringList();
    m_myLevel = Store::levels().contains(settings.value("myLevel").toString()) ? settings.value("myLevel").toString() : QString();
    m_saveTexts = settings.value("saveTexts", true).toBool();
    m_graphSource = settings.value("graphSource", "references").toString() == "model" ? "model" : "references";
    // A newly saved page can change what a search finds and which
    // references the writeup names.
    connect(&m_archive, &Archive::saved, this, [this](const QString &url) {
        if (!m_query.trimmed().isEmpty())
            rebuild();
        m_scans.remove(url);
        if (m_graphSource == "references")
            graphOutdated();
        extract();
    });
    m_noticeTimer.setSingleShot(true);
    m_noticeTimer.setInterval(6000);
    connect(&m_noticeTimer, &QTimer::timeout, this, [this] { setNotice({}); });

    // A fetch writes its files and lock into the data directory, so watching
    // that one directory follows timer runs and manual ones alike.
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(150);
    connect(&m_debounce, &QTimer::timeout, this, &Backend::reload);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_debounce.start(); });
    m_watcher.addPath(m_store.directory());
    // A commit from another window changes the database file.
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_debounce.start(); });

    // Covers the moment between asking systemd for a fetch and the fetch
    // taking its lock, so the button doesn't flicker back.
    m_starting.setSingleShot(true);
    m_starting.setInterval(20000);
    connect(&m_starting, &QTimer::timeout, this, &Backend::statusChanged);

    if (m_systemd)
        readTimer();
    reload();

    // Readers of the site's feed get new days while the window is open, not
    // only from the morning timer: the site updates whenever its agent runs.
    if (m_systemd) {
        m_syncTimer.setInterval(60 * 60 * 1000);
        connect(&m_syncTimer, &QTimer::timeout, this, &Backend::syncIfStale);
        m_syncTimer.start();
        syncIfStale();
    }
}

void Backend::syncIfStale() {
    if (m_source == "feed" && !running()
        && (!m_lastRun.isValid() || m_lastRun.secsTo(QDateTime::currentDateTime()) > 30 * 60))
        refresh();
}

void Backend::setSource(const QString &source) {
    if (source == m_source)
        return;
    Syncer::setSource(source);
    m_source = Syncer::source();
    emit sourceChanged();
    setProblem({});
    if (m_source == "feed")
        refresh();
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
        setProblem(tr("Could not save your progress to %1.").arg(m_database.path()));
    ++m_progressRevision;
    emit progressChanged();
    // Hiding read writeups and the favorites view both depend on progress.
    rebuild();
}

void Backend::setSearchCategories(const QStringList &categories) {
    // Every direction picked is the same as none: search them all.
    QStringList list;
    for (const auto &category : Store::categories())
        if (categories.contains(category))
            list << category;
    if (list.size() == Store::categories().size())
        list.clear();
    if (list != m_searchCategories) {
        m_searchCategories = list;
        QSettings("writeuppulse", "writeuppulse").setValue("categories", list);
        emit preferencesChanged();
    }
}

void Backend::setPreferredCategories(const QStringList &categories) {
    QStringList list;
    for (const auto &category : Store::categories())
        if (categories.contains(category))
            list << category;
    if (list != m_preferredCategories) {
        m_preferredCategories = list;
        QSettings("writeuppulse", "writeuppulse").setValue("preferred", list);
        emit preferencesChanged();
        rebuild();
    }
}

void Backend::setSaveTexts(bool save) {
    if (save == m_saveTexts)
        return;
    m_saveTexts = save;
    QSettings("writeuppulse", "writeuppulse").setValue("saveTexts", save);
    emit preferencesChanged();
    if (save)
        archive();
    else
        m_archive.clear();
    extract();
}

void Backend::setLlm(LlmClient *llm) {
    m_extractor = std::make_unique<Extractor>(m_database, *llm);
    connect(m_extractor.get(), &Extractor::extracted, this, &Backend::graphOutdated);
    extract();
}

// After the page, when there will be one, so the concepts come from its text.
void Backend::extract() {
    if (!m_extractor)
        return;
    m_extractor->waitForPages = m_systemd && m_saveTexts;
    m_extractor->enqueue(m_library);
}

QSet<QString> Backend::libraryUrls() const {
    QSet<QString> urls;
    for (const auto &value : m_library)
        urls.insert(Store::canonicalUrl(value.toMap().value("url").toString()));
    return urls;
}

void Backend::graphOutdated() {
    m_graphOutdated = true;
    emit graphChanged();
}

void Backend::setMyLevel(const QString &level) {
    const QString value = Store::levels().contains(level) ? level : QString();
    if (value == m_myLevel)
        return;
    m_myLevel = value;
    QSettings("writeuppulse", "writeuppulse").setValue("myLevel", value);
    emit preferencesChanged();
    rebuild();
    emit progressChanged();
}

QVariantMap Backend::review() const {
    if (m_myLevel.isEmpty())
        return m_progress.review(m_library);
    QVariantList library = m_library;
    std::stable_sort(library.begin(), library.end(), [this](const QVariant &a, const QVariant &b) {
        return (a.toMap().value("level") == m_myLevel) > (b.toMap().value("level") == m_myLevel);
    });
    return m_progress.review(library);
}

void Backend::setGraphSource(const QString &source) {
    if (source == m_graphSource || (source != "model" && source != "references"))
        return;
    m_graphSource = source;
    QSettings("writeuppulse", "writeuppulse").setValue("graphSource", source);
    graphOutdated();
}

QVariantMap Backend::graph() {
    if (m_graphOutdated) {
        m_graphOutdated = false;
        QJsonObject graph = m_graphSource == "references" ? referenceGraph() : m_database.graph(libraryUrls());
        QJsonArray nodes = graph.value("nodes").toArray();
        layout(nodes, graph.value("edges").toArray());
        graph.insert("nodes", nodes);
        graph.insert("total", int(libraryUrls().size()));
        m_graph = graph.toVariantMap();
    }
    return m_graph;
}

QVariantMap Backend::conceptInfo(const QString &id) const {
    if (m_graphSource == "references")
        return referenceInfo(id);
    QJsonObject info = m_database.conceptInfo(id, libraryUrls());
    if (info.isEmpty())
        return {};
    QHash<QString, QVariantMap> items;
    for (const auto &value : m_library)
        items.insert(Store::canonicalUrl(value.toMap().value("url").toString()), value.toMap());
    QVariantList writeups;
    for (const auto &url : info.value("urls").toArray())
        writeups << items.value(url.toString());
    std::sort(writeups.begin(), writeups.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value("date").toString() < b.toMap().value("date").toString();
    });
    QVariantMap result = info.toVariantMap();
    result.insert("writeups", writeups);
    QVariantList neighbours;
    for (const auto &value : result.value("neighbours").toList()) {
        QVariantMap neighbour = value.toMap();
        QVariantList evidence;
        for (const auto &piece : neighbour.value("evidence").toList()) {
            QVariantMap map = piece.toMap();
            map.insert("title", items.value(map.value("url").toString()).value("title"));
            evidence << map;
        }
        neighbour.insert("evidence", evidence);
        neighbours << neighbour;
    }
    result.insert("neighbours", neighbours);
    return result;
}

const Backend::Scan &Backend::scan(const QVariantMap &item) const {
    const QString url = Store::canonicalUrl(item.value("url").toString());
    auto it = m_scans.find(url);
    if (it == m_scans.end()) {
        Scan scan;
        QStringList own{item.value("title").toString(), item.value("summary").toString()};
        for (const auto &takeaway : item.value("takeaways").toList())
            own << takeaway.toString();
        scan.texts << own.join('\n') << m_database.passages(url);
        scan.hits = References::instance().find(scan.texts, &scan.named);
        it = m_scans.insert(url, scan);
    }
    return *it;
}

QVariantList Backend::references(const QString &url) const {
    const QString key = Store::canonicalUrl(url);
    QVariantList list;
    for (const auto &value : m_library)
        if (Store::canonicalUrl(value.toMap().value("url").toString()) == key) {
            for (const auto &hit : scan(value.toMap()).hits) {
                const QJsonObject entry = References::instance().entry(hit.id);
                list << QVariantMap{{"id", hit.id}, {"kind", entry.value("kind").toString()},
                                    {"name", entry.value("name").toString()}, {"text", entry.value("text").toString()},
                                    {"link", entry.value("url").toString()}, {"count", hit.count}};
            }
            break;
        }
    return list;
}

// Nodes are the entries the library names; two are related when a passage
// names both (a passage naming more than six is an overview and relates
// nothing, as with the model's concepts) or when a catalog relates them.
QJsonObject Backend::referenceGraph() const {
    const References &references = References::instance();
    QMap<QString, QStringList> named;
    QMap<QPair<QString, QString>, QPair<int, QSet<QString>>> pairs;
    QSet<QString> urls;
    for (const auto &value : m_library) {
        const QString url = Store::canonicalUrl(value.toMap().value("url").toString());
        if (urls.contains(url))
            continue;
        urls.insert(url);
        const Scan &found = scan(value.toMap());
        for (const auto &hit : found.hits)
            named[hit.id] << url;
        for (const auto &ids : found.named) {
            if (ids.size() > 6)
                continue;
            for (int i = 0; i < ids.size(); ++i)
                for (int j = i + 1; j < ids.size(); ++j) {
                    auto &pair = pairs[ids[i] < ids[j] ? qMakePair(ids[i], ids[j]) : qMakePair(ids[j], ids[i])];
                    ++pair.first;
                    pair.second.insert(url);
                }
        }
    }
    QJsonArray nodes, edges;
    for (auto it = named.begin(); it != named.end(); ++it) {
        const QJsonObject entry = references.entry(it.key());
        nodes.append(QJsonObject{{"id", it.key()}, {"name", entry.value("name").toString()},
                                 {"kind", entry.value("kind").toString()}, {"urls", QJsonArray::fromStringList(it.value())}});
        for (const auto &related : entry.value("related").toArray()) {
            const QString other = related.toString();
            const auto key = it.key() < other ? qMakePair(it.key(), other) : qMakePair(other, it.key());
            if (named.contains(other) && !pairs.contains(key))
                pairs.insert(key, {1, {}});
        }
    }
    for (auto it = pairs.begin(); it != pairs.end(); ++it)
        edges.append(QJsonObject{{"from", it.key().first}, {"to", it.key().second}, {"weight", it.value().first},
                                 {"writeups", int(it.value().second.size())}});
    return {{"nodes", nodes}, {"edges", edges}, {"done", int(urls.size())}, {"failed", 0}};
}

QVariantMap Backend::referenceInfo(const QString &id) const {
    const References &references = References::instance();
    const QJsonObject entry = references.entry(id);
    if (entry.isEmpty())
        return {};
    QVariantList writeups;
    QHash<QString, QVariantMap> neighbours;
    QSet<QString> urls, inLibrary;
    for (const auto &value : m_library) {
        const QVariantMap item = value.toMap();
        const QString url = Store::canonicalUrl(item.value("url").toString());
        if (urls.contains(url))
            continue;
        urls.insert(url);
        const Scan &found = scan(item);
        for (const auto &hit : found.hits)
            inLibrary.insert(hit.id);
        bool names = false;
        for (int t = 0; t < found.named.size(); ++t) {
            const QStringList &ids = found.named.at(t);
            if (!ids.contains(id))
                continue;
            names = true;
            if (ids.size() > 6)
                continue;
            for (const auto &other : ids) {
                if (other == id)
                    continue;
                QVariantMap &neighbour = neighbours[other];
                if (neighbour.isEmpty()) {
                    const QJsonObject e = references.entry(other);
                    neighbour = {{"id", other}, {"name", e.value("name").toString()}, {"kind", e.value("kind").toString()},
                                 {"weight", 0}, {"evidence", QVariantList()}};
                }
                neighbour["weight"] = neighbour.value("weight").toInt() + 1;
                QVariantList evidence = neighbour.value("evidence").toList();
                evidence << QVariantMap{{"url", url}, {"title", item.value("title")}, {"passage", t - 1},
                                        {"text", t > 0 ? found.texts.at(t) : QString()}};
                neighbour["evidence"] = evidence;
            }
        }
        if (names)
            writeups << item;
    }
    std::sort(writeups.begin(), writeups.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value("date").toString() < b.toMap().value("date").toString();
    });
    QVariantList sorted = QVariantList(neighbours.begin(), neighbours.end());
    std::sort(sorted.begin(), sorted.end(), [](const QVariant &a, const QVariant &b) {
        const int wa = a.toMap().value("weight").toInt(), wb = b.toMap().value("weight").toInt();
        return wa != wb ? wa > wb : a.toMap().value("name").toString() < b.toMap().value("name").toString();
    });
    QVariantList catalog;
    for (const auto &related : entry.value("related").toArray()) {
        const QJsonObject e = references.entry(related.toString());
        catalog << QVariantMap{{"id", related.toString()}, {"name", e.value("name").toString()},
                               {"kind", e.value("kind").toString()}, {"inLibrary", inLibrary.contains(related.toString())}};
    }
    // Entries that relate to this one from their side (a technique's
    // sub-techniques, the patterns that name a weakness) count too, when the
    // library names them.
    for (const auto &other : inLibrary) {
        const QJsonObject e = references.entry(other);
        if (other != id && e.value("related").toArray().contains(id)
            && std::none_of(catalog.begin(), catalog.end(), [&](const QVariant &c) { return c.toMap().value("id") == other; }))
            catalog << QVariantMap{{"id", other}, {"name", e.value("name").toString()}, {"kind", e.value("kind").toString()},
                                   {"inLibrary", true}};
    }
    QStringList aliases;
    for (const auto &term : entry.value("terms").toArray())
        if (term.toString() != entry.value("name").toString() && term.toString() != id)
            aliases << term.toString();
    return {{"id", id}, {"name", entry.value("name").toString()}, {"kind", entry.value("kind").toString()},
            {"title", entry.value("title").toString()}, {"text", entry.value("text").toString()}, {"link", entry.value("url").toString()}, {"aliases", aliases},
            {"writeups", writeups}, {"neighbours", sorted}, {"catalog", catalog}};
}

// Fruchterman–Reingold with a pull towards the middle, so writeups that share
// nothing don't drift off the edge; then scaled to fill [0, 1].
void Backend::layout(QJsonArray &nodes, const QJsonArray &edges) {
    const int n = nodes.size();
    if (n == 0)
        return;
    QHash<QString, int> index;
    for (int i = 0; i < n; ++i)
        index.insert(nodes[i].toObject().value("id").toString(), i);
    QList<QPointF> at(n), push(n);
    // A sunflower spiral: spread out, and the same for the same nodes.
    for (int i = 0; i < n; ++i) {
        const double r = qSqrt(i + 0.5) / qSqrt(n), angle = i * 2.39996;
        at[i] = {r * qCos(angle), r * qSin(angle)};
    }
    const double k = 1.0 / qSqrt(n);
    double heat = 0.1;
    for (int round = 0; round < 300; ++round, heat *= 0.985) {
        for (int i = 0; i < n; ++i)
            push[i] = -at[i] * qSqrt(QPointF::dotProduct(at[i], at[i])) * 2.0;
        for (int i = 0; i < n; ++i)
            for (int j = i + 1; j < n; ++j) {
                QPointF d = at[i] - at[j];
                const double length = qMax(1e-4, qSqrt(QPointF::dotProduct(d, d)));
                d *= k * k / (length * length);
                push[i] += d;
                push[j] -= d;
            }
        for (const auto &value : edges) {
            const QJsonObject edge = value.toObject();
            const int a = index.value(edge.value("from").toString(), -1), b = index.value(edge.value("to").toString(), -1);
            if (a < 0 || b < 0)
                continue;
            QPointF d = at[a] - at[b];
            const double length = qSqrt(QPointF::dotProduct(d, d));
            d *= length / k * qMin(3.0, 1.0 + qLn(edge.value("weight").toInt(1)));
            push[a] -= d;
            push[b] += d;
        }
        for (int i = 0; i < n; ++i) {
            const double length = qSqrt(QPointF::dotProduct(push[i], push[i]));
            if (length > 0)
                at[i] += push[i] / length * qMin(length, heat);
        }
    }
    double left = at[0].x(), right = left, top = at[0].y(), bottom = top;
    for (const auto &p : at) {
        left = qMin(left, p.x()), right = qMax(right, p.x());
        top = qMin(top, p.y()), bottom = qMax(bottom, p.y());
    }
    const double scale = qMax(1e-6, qMax(right - left, bottom - top));
    for (int i = 0; i < n; ++i) {
        QJsonObject node = nodes[i].toObject();
        node.insert("x", (at[i].x() - left) / scale + (1 - (right - left) / scale) / 2);
        node.insert("y", (at[i].y() - top) / scale + (1 - (bottom - top) / scale) / 2);
        nodes.replace(i, node);
    }
}

// Newest first, so today's writeups are searchable before older ones.
void Backend::archive() {
    if (!m_systemd || !m_saveTexts)
        return;
    QStringList urls;
    for (const auto &value : m_library)
        urls << value.toMap().value("url").toString();
    m_archive.enqueue(urls);
}

void Backend::setNotice(const QString &notice) {
    if (notice != m_notice) {
        m_notice = notice;
        emit noticeChanged();
    }
    if (!notice.isEmpty())
        m_noticeTimer.start();
}

void Backend::setFilePicker(FilePicker *picker) {
    m_picker = picker;
    connect(picker, &FilePicker::saveSelected, this, &Backend::saveExport);
    connect(picker, &FilePicker::failed, this, &Backend::setProblem);
}

void Backend::exportMarkdown(bool week) {
    const QString date = m_date.isEmpty() ? QDate::currentDate().toString(Qt::ISODate) : m_date;
    m_pendingExport = m_store.exportMarkdown(date, week, &m_progress);
    if (m_pendingExport.isEmpty()) {
        setProblem(tr("There is nothing to export yet."));
        return;
    }
    if (!m_picker) {
        setProblem(tr("The save dialog is not available."));
        return;
    }
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString folder = QDir(documents).exists() ? documents : QDir::homePath();
    m_picker->saveMarkdown(QUrl::fromLocalFile(QString("%1/writeuppulse-%2%3.md").arg(folder, week ? "week-" : "", date)));
}

void Backend::saveExport(const QUrl &url) {
    const QByteArray data = m_pendingExport.toUtf8();
    QSaveFile file(url.toLocalFile());
    if (m_pendingExport.isEmpty() || !file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        setProblem(tr("Could not save %1.").arg(url.toLocalFile()));
        return;
    }
    setProblem({});
    setNotice(tr("Saved %1").arg(QFileInfo(url.toLocalFile()).fileName()));
    m_pendingExport.clear();
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

// The writeups the window shows: the source the view picks (a day, the
// favorites, or every day when searching), narrowed by the filters. Each
// item carries its date, and old digests get the fields they lack.
void Backend::rebuild() {
    const QStringList favorites = m_progress.favorites();
    const bool searching = !m_query.trimmed().isEmpty();
    QVariantList source;
    for (const auto &value : m_library) {
        const QVariantMap item = value.toMap();
        if (searching || (m_view == "favorites" ? favorites.contains(Store::canonicalUrl(item.value("url").toString()))
                                                : m_view == "day" && item.value("date") == m_date))
            source << item;
    }

    QStringList shown;
    for (const auto &category : Store::categories())
        for (const auto &value : source)
            if (value.toMap().value("category") == category && !shown.contains(category))
                shown << category;

    // Every word has to start a word of the writeup (through the full-text
    // index) or be part of one of the user's tags; the best matches come
    // first. A search hit that a filter hides counts as hidden; a miss doesn't.
    const QStringList words = m_query.toLower().split(' ', Qt::SkipEmptyParts);
    QList<QHash<QString, double>> hits, pageHits;
    for (const auto &word : words) {
        hits << m_database.match(word);
        pageHits << m_database.matchPages(word);
    }
    QHash<QString, double> scores;
    QVariantList items;
    int hidden = 0;
    for (const auto &value : source) {
        const QVariantMap item = value.toMap();
        const QJsonObject state = m_progress.entry(item.value("url").toString());
        const QStringList userTags = state.value("tags").toVariant().toStringList();
        if (!words.isEmpty()) {
            const QString key = item.value("date").toString() + "\t" + item.value("url").toString();
            const QString page = Store::canonicalUrl(item.value("url").toString());
            const QString tagText = userTags.join(' ').toLower();
            double score = 0;
            bool all = true;
            for (int i = 0; all && i < words.size(); ++i) {
                if (hits.at(i).contains(key))
                    score += hits.at(i).value(key);
                // The page's own text counts for less than the summary.
                else if (pageHits.at(i).contains(page))
                    score += pageHits.at(i).value(page) / 2;
                else
                    all = tagText.contains(words.at(i));
            }
            if (!all)
                continue;
            scores.insert(key, score);
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

    // BM25 scores are negative, lower is better; ties keep the newest first.
    if (!words.isEmpty())
        std::stable_sort(items.begin(), items.end(), [&scores](const QVariant &a, const QVariant &b) {
            auto key = [](const QVariant &item) {
                return item.toMap().value("date").toString() + "\t" + item.toMap().value("url").toString();
            };
            return scores.value(key(a)) < scores.value(key(b));
        });

    // A day lists the directions the user prefers first, otherwise in the
    // agent's order.
    // A day starts with the writeups at the reader's level, then the preferred directions.
    auto rank = [this](const QVariant &item) {
        return 2 * (!m_myLevel.isEmpty() && item.toMap().value("level").toString() == m_myLevel)
               + m_preferredCategories.contains(item.toMap().value("category").toString());
    };
    if (m_view == "day" && words.isEmpty())
        std::stable_sort(items.begin(), items.end(), [&](const QVariant &a, const QVariant &b) { return rank(a) > rank(b); });

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

void Backend::setCheck(const QVariantMap &item, const QString &question, const QString &result) {
    progressSaved(m_progress.setCheck(item.value("url").toString(), item.value("category").toString(), question, result));
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
    if (!m_watcher.files().contains(m_database.path()) && QFileInfo::exists(m_database.path()))
        m_watcher.addPath(m_database.path());

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
        m_categories.clear();
        for (const auto &date : m_dates)
            for (const auto &value : m_store.digest(date).value("items").toArray()) {
                QVariantMap item = value.toObject().toVariantMap();
                item.insert("date", date);
                if (!Store::categories().contains(item.value("category").toString()))
                    item.insert("category", "Other");
                m_categories.insert(Store::canonicalUrl(item.value("url").toString()), item.value("category").toString());
                m_library << item;
            }
        QString error;
        if (!m_database.index(m_store, &error))
            setProblem(error);
        archive();
        extract();
        m_scans.clear();
        graphOutdated();
        rebuildNeeded = true;
        // The statistics read the categories of the days.
        emit progressChanged();
    }

    // Another window may have marked something; pick it up.
    const int dataVersion = m_database.dataVersion();
    if (dataVersion != m_dataVersion) {
        m_dataVersion = dataVersion;
        m_progress.load();
        // Another window's model may have found concepts.
        graphOutdated();
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
    if (m_source == "claude" && m_agentMissing) {
        setProblem(tr("Claude Code isn't installed, so there is no agent to fetch writeups. Install it and sign in with `claude`."));
        return;
    }
    setProblem({});

    // Through systemd when the package is installed, so the fetch outlives
    // the window and lands in the journal; straight from this binary otherwise.
    if (!(m_systemd && systemctl({"start", "--no-block", "writeuppulse.service"}))
        && !QProcess::startDetached(QCoreApplication::applicationFilePath(), {"update"})) {
        setProblem(tr("Could not start an update."));
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
