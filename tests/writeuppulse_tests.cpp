#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>

#include "backend.h"
#include "database.h"
#include "fetcher.h"
#include "filepicker.h"
#include "progress.h"
#include "store.h"
#include "syncer.h"
#include "translator.h"
#include "tutor.h"

#include <QTcpServer>
#include <QTcpSocket>

// Answers the save dialog at once with a fixed file.
class FakePicker : public FilePicker {
    Q_OBJECT
public:
    QString answer;
    QUrl suggested;
    void saveMarkdown(const QUrl &url) override {
        suggested = url;
        emit saveSelected(QUrl::fromLocalFile(answer));
    }
};

class WriteupPulseTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Keeps QSettings out of the real ~/.config.
        QStandardPaths::setTestModeEnabled(true);
        QSettings("writeuppulse", "writeuppulse").clear();
    }

    void languagePickedInTheAppWins()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "uk\n");
        QCOMPARE(Fetcher::language(dir.path() + "/language"), QString("uk"));
        write(dir.path() + "/language", "da\n");
        QCOMPARE(Fetcher::language(dir.path() + "/language"), QString("en"));

        Fetcher::setLanguage("ru");
        QCOMPARE(Fetcher::language(dir.path() + "/language"), QString("ru"));
        Fetcher fetcher(Store(dir.path()), "/bin/true");
        QVERIFY(fetcher.prompt("2026-11-09").contains("every other text field in Russian"));
        QSettings("writeuppulse", "writeuppulse").clear();
    }

    void normalizeKeepsOnlyCompleteWebItems()
    {
        auto item = [](const QString &title, const QString &url, const QString &summary) {
            return QJsonObject{{"title", title}, {"url", url}, {"summary", summary}};
        };
        QJsonObject good = item("Good", "https://example.com/a", "One.\n\nTwo.");
        good.insert("takeaways", QJsonArray{"x", ""});
        const QJsonObject output{{"items", QJsonArray{good, item("No link", "javascript:void", "s"),
                                                      item("", "https://example.com/b", "s"),
                                                      item("No summary", "https://example.com/c", "  ")}}};
        QString error;
        const QJsonObject digest = Store::normalize(output, "2026-11-09", &error);
        const QJsonArray items = digest.value("items").toArray();
        QCOMPARE(items.size(), 1);
        QCOMPARE(items[0].toObject().value("title").toString(), QString("Good"));
        QCOMPARE(items[0].toObject().value("takeaways").toArray().size(), 1);
        QCOMPARE(digest.value("date").toString(), QString("2026-11-09"));

        QVERIFY(Store::normalize(QJsonObject{{"items", QJsonArray{}}}, "2026-11-09", &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }

    void normalizeKeepsLearningFieldsAndDropsCode()
    {
        const QJsonObject raw{
            {"title", "T"}, {"url", "https://example.com/t"}, {"summary", "Idea.\n```\nrm -rf /\n```\nMore."},
            {"category", "Pwn"}, {"level", "Wizard"},
            {"takeaways", QJsonArray{"one", "```bash\nexploit\n```"}},
            {"questions", QJsonArray{QJsonObject{{"question", "Why?"}, {"answer", "Because."}},
                                     QJsonObject{{"question", "No answer"}}}},
            {"practice", QJsonArray{"Set up a lab"}},
        };
        QString error;
        QJsonObject item = Store::normalize(QJsonObject{{"items", QJsonArray{raw}}}, "2026-11-09", &error)
                               .value("items").toArray().first().toObject();
        QVERIFY(!item.value("summary").toString().contains("rm -rf"));
        QCOMPARE(item.value("category").toString(), QString("Pwn"));
        QCOMPARE(item.value("level").toString(), QString());
        QCOMPARE(item.value("takeaways").toArray().size(), 1);
        QCOMPARE(item.value("questions").toArray().size(), 1);
        QCOMPARE(item.value("practice").toArray().first().toString(), QString("Set up a lab"));

        QJsonObject unknown = raw;
        unknown.insert("category", "Gardening");
        item = Store::normalize(QJsonObject{{"items", QJsonArray{unknown}}}, "2026-11-09", &error)
                   .value("items").toArray().first().toObject();
        QCOMPARE(item.value("category").toString(), QString("Other"));
        QCOMPARE(Store::topicOf("Pwn"), QString("Security"));
        QCOMPARE(Store::topicOf("LLM & Agents"), QString("AI / ML"));
    }

    void schemaAndPromptCarryTheDirections()
    {
        const QJsonObject schema = QJsonDocument::fromJson(Fetcher::schema()).object();
        const QJsonObject item = schema["properties"]["items"]["items"]["properties"].toObject();
        QCOMPARE(item["category"]["enum"].toArray().size(), Store::categories().size());
        QCOMPARE(item["takeaways"]["minItems"].toInt(), 5);
        QVERIFY(item.contains("questions") && item.contains("practice"));

        QTemporaryDir dir;
        QSettings settings("writeuppulse", "writeuppulse");
        settings.setValue("categories", QStringList{"Web", "Pwn", "Nonsense"});
        settings.setValue("preferred", QStringList{"Web"});
        settings.sync();
        const QString prompt = Fetcher(Store(dir.path()), "/bin/true").prompt("2026-11-09");
        QVERIFY(prompt.contains("in these directions: Web, Pwn."));
        QVERIFY(prompt.contains("especially wants: Web."));
        QVERIFY(prompt.contains("Never write code"));
        settings.clear();
    }

    void progressCountsStreaksAndStats()
    {
        QTemporaryDir dir;
        const QDate today(2026, 11, 9);
        {
            Database database(dir.path());
            Progress progress(database);
            QVERIFY(progress.setRead("https://a", "Web", true, today.addDays(-2)));
            QVERIFY(progress.setRead("https://b", "Pwn", true, today.addDays(-1)));
            QVERIFY(progress.setRead("https://c", "Web", true, today.addDays(-1)));
            QVERIFY(progress.setLab("https://b", "Pwn", true, today));
            QVERIFY(progress.setFavorite("https://d", "Crypto", true));
            QVERIFY(progress.setTags("https://d", {"study", " Study ", "", "later"}));
            QVERIFY(progress.setPracticeDone("https://a", 2, true));
        }

        // A fresh load sees everything written, and today not yet read keeps
        // yesterday's streak alive.
        Database database(dir.path());
        Progress progress(database);
        QCOMPARE(progress.streak(today), 2);
        QCOMPARE(progress.streak(today.addDays(1)), 0);
        progress.setRead("https://d", "Crypto", true, today);
        QCOMPARE(progress.streak(today), 3);

        const QJsonObject stats = progress.stats();
        QCOMPARE(stats.value("read").toInt(), 4);
        QCOMPARE(stats.value("lab").toInt(), 1);
        QCOMPARE(stats.value("favorites").toInt(), 1);
        QCOMPARE(stats.value("byCategory").toObject().value("Web").toInt(), 2);
        QCOMPARE(progress.tags(), QStringList({"later", "study"}));
        QCOMPARE(progress.favorites(), QStringList({"https://d"}));
        QCOMPARE(progress.entry("https://a").value("done").toArray().first().toInt(), 2);

        // Undoing everything drops the entry instead of keeping it empty.
        progress.setRead("https://a", {}, false);
        progress.setPracticeDone("https://a", 2, false);
        QVERIFY(progress.entry("https://a").isEmpty());
    }

    void backendMarksProgress()
    {
        QTemporaryDir dir;
        Store store(dir.path());
        QString error;
        QVERIFY(store.saveDigest(sampleDigest("2026-11-09", "https://example.com/x"), &error));
        Backend backend(dir.path(), false);
        QSignalSpy changed(&backend, &Backend::progressChanged);
        backend.setRead(backend.items().first().toMap(), true);
        QCOMPARE(changed.count(), 1);
        QVERIFY(backend.state("https://example.com/x").contains("read"));
        QCOMPARE(backend.streak(), 1);
        QCOMPARE(backend.stats().value("read").toInt(), 1);
    }

    void backendFiltersSearchesAndShowsFavorites()
    {
        QTemporaryDir dir;
        Store store(dir.path());
        QString error;
        auto item = [](const QString &title, const QString &category, const QString &level) {
            return QJsonObject{{"title", title}, {"url", "https://example.com/" + title}, {"summary", "About " + title},
                               {"category", category}, {"level", level}};
        };
        QVERIFY(store.saveDigest({{"date", "2026-11-08"}, {"items", QJsonArray{item("heap", "Pwn", "Senior")}}}, &error));
        QVERIFY(store.saveDigest({{"date", "2026-11-09"}, {"items", QJsonArray{
            item("xss", "Web", "Junior"), item("rag", "LLM & Agents", "Middle"),
            QJsonObject{{"title", "old"}, {"url", "https://example.com/old"}, {"summary", "No category"}}}}}, &error));

        Backend backend(dir.path(), false);
        QCOMPARE(backend.items().size(), 3);
        QCOMPARE(backend.shownCategories(), QStringList({"Web", "LLM & Agents", "Other"}));

        backend.setCategoryFilter({"Web", "Other"});
        QCOMPARE(backend.items().size(), 2);
        QCOMPARE(backend.hiddenCount(), 1);
        backend.setCategoryFilter({});
        backend.setLevelFilter("Middle");
        QCOMPARE(backend.items().first().toMap().value("title").toString(), QString("rag"));
        backend.setLevelFilter({});

        backend.setRead(backend.items().first().toMap(), true);
        backend.setHideRead(true);
        QCOMPARE(backend.items().size(), 2);
        backend.setHideRead(false);

        // Search spans every day, and each hit carries its date.
        backend.setQuery("HEAP");
        QCOMPARE(backend.items().size(), 1);
        QCOMPARE(backend.hiddenCount(), 0);
        QCOMPARE(backend.items().first().toMap().value("date").toString(), QString("2026-11-08"));
        backend.setQuery({});

        backend.setFavorite(backend.items().at(1).toMap(), true);
        backend.setTags("https://example.com/rag", {"study"});
        backend.setView("favorites");
        QCOMPARE(backend.items().size(), 1);
        QCOMPARE(backend.favoriteCount(), 1);
        backend.setTagFilter("other");
        QCOMPARE(backend.items().size(), 0);
        backend.setDate("2026-11-08");
        QCOMPARE(backend.view(), QString("day"));
        QCOMPARE(backend.items().size(), 1);
    }

    void exportCoversTheWeekWithProgress()
    {
        QTemporaryDir dir;
        Store store(dir.path());
        QString error;
        for (const QString date : {"2026-11-01", "2026-11-03", "2026-11-09"})
            QVERIFY(store.saveDigest(sampleDigest(date, "https://example.com/" + date), &error));
        Database database(dir.path());
        Progress progress(database);
        progress.setRead("https://example.com/2026-11-09", "Web", true);
        QVERIFY(progress.setCheck("https://example.com/2026-11-09", "Web", "Why?", "knew"));

        const QString week = store.exportMarkdown("2026-11-09", true, &progress);
        QVERIFY(!week.contains("2026-11-01"));
        QVERIFY(week.indexOf("— 2026-11-03") < week.indexOf("— 2026-11-09"));
        QVERIFY(week.contains("✓ Read"));
        QVERIFY(week.contains("- Why? ✓"));
        QVERIFY(!store.exportMarkdown("2026-11-09", false).contains("2026-11-03"));
        QVERIFY(store.exportMarkdown("2026-11-05", false).isEmpty());
    }

    void canonicalUrlFoldsTheFormsOfOneLink()
    {
        const QString plain = Store::canonicalUrl("https://example.com/post");
        QCOMPARE(plain, QString("https://example.com/post"));
        for (const QString url : {"http://www.example.com/post/", "https://EXAMPLE.com/post#part",
                                   "https://example.com:443/post?utm_source=hn&utm_medium=x", "https://example.com/post?fbclid=1"})
            QCOMPARE(Store::canonicalUrl(url), plain);
        QCOMPARE(Store::canonicalUrl("https://example.com/post?id=7&utm_campaign=a"), QString("https://example.com/post?id=7"));
        QCOMPARE(Store::canonicalUrl("not a url"), QString("not a url"));
    }

    void progressMovesFromJsonOnce()
    {
        QTemporaryDir dir;
        const QJsonObject old{{"items", QJsonObject{
            {"https://www.example.com/a/?utm_source=hn", QJsonObject{{"read", "2026-11-08"}, {"category", "Web"}}},
            {"https://example.com/a", QJsonObject{{"favorite", true}, {"tags", QJsonArray{"study"}}}},
            {"https://example.com/b", QJsonObject{{"done", QJsonArray{1}}}}}}};
        const QByteArray json = QJsonDocument(old).toJson();
        write(dir.path() + "/progress.json", json);
        {
            Database database(dir.path());
            QVERIFY2(database.error().isEmpty(), qPrintable(database.error()));
            Progress progress(database);
            // Two forms of one link become one entry with both sets of marks.
            const QJsonObject a = progress.entry("https://example.com/a");
            QCOMPARE(a.value("read").toString(), QString("2026-11-08"));
            QVERIFY(a.value("favorite").toBool());
            QCOMPARE(progress.entry("http://example.com/b/").value("done").toArray().first().toInt(), 1);
            QCOMPARE(progress.favorites(), QStringList({"https://example.com/a"}));
            progress.setRead("https://example.com/a", {}, false);
        }
        // The old file stays as it was and is never read again.
        QFile file(dir.path() + "/progress.json");
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), json);
        Database database(dir.path());
        Progress progress(database);
        QVERIFY(!progress.entry("https://example.com/a").contains("read"));
        QVERIFY(progress.entry("https://example.com/a").value("favorite").toBool());
    }

    void searchRanksThroughTheIndex()
    {
        QTemporaryDir dir;
        Store store(dir.path());
        QString error;
        auto item = [](const QString &title, const QString &summary) {
            return QJsonObject{{"title", title}, {"url", "https://example.com/" + title.left(4)}, {"summary", summary}};
        };
        QVERIFY(store.saveDigest({{"date", "2026-11-08"}, {"items", QJsonArray{
            item("Kernel heap grooming", "Shaping the allocator."), item("Notes", "A word about heap spraying.")}}}, &error));
        QVERIFY(store.saveDigest({{"date", "2026-11-09"}, {"items", QJsonArray{
            item("Уязвимость в ядре", "Разбор ошибки в драйвере.")}}}, &error));
        Backend backend(dir.path(), false);

        // A title hit comes before a summary hit, and words match as prefixes.
        backend.setQuery("hea");
        QCOMPARE(backend.items().size(), 2);
        QCOMPARE(backend.items().first().toMap().value("title").toString(), QString("Kernel heap grooming"));
        backend.setQuery("УЯЗВИМ драйв");
        QCOMPARE(backend.items().size(), 1);
        // What the user typed is only text, never FTS5 syntax.
        backend.setQuery("heap\" OR \"kernel");
        QCOMPARE(backend.items().size(), 0);
        backend.setQuery("NOT");
        QCOMPARE(backend.items().size(), 1);

        // The user's own tags count as text too.
        backend.setTags("https://example.com/Note", {"later"});
        backend.setQuery("heap later");
        QCOMPARE(backend.items().size(), 1);

        // A day written again is reindexed; a removed one leaves the index.
        QVERIFY(store.saveDigest({{"date", "2026-11-08"}, {"items", QJsonArray{item("Fresh title", "Nothing else.")}}}, &error));
        QVERIFY(QFile::remove(dir.path() + "/2026-11-09.json"));
        QTRY_COMPARE(backend.dates().size(), 1);
        backend.setQuery("fresh");
        QCOMPARE(backend.items().size(), 1);
        backend.setQuery("heap");
        QCOMPARE(backend.items().size(), 0);
        QVERIFY(Database(dir.path()).match("ядре").isEmpty());
    }

    void selfCheckAnswersAreKept()
    {
        QTemporaryDir dir;
        Store store(dir.path());
        QString error;
        QVERIFY(store.saveDigest(sampleDigest("2026-11-09", "https://example.com/x"), &error));
        {
            Backend backend(dir.path(), false);
            const QVariantMap item = backend.items().first().toMap();
            backend.setCheck(item, "Why?", "missed");
            backend.setCheck(item, "Why?", "knew");
            backend.setCheck(item, "How?", "missed");
            backend.setCheck(item, "How?", "");
            QCOMPARE(backend.stats().value("checked").toInt(), 1);
            QCOMPARE(backend.stats().value("knew").toInt(), 1);
        }
        // The latest answer for each question survives a restart.
        Database database(dir.path());
        Progress progress(database);
        const QJsonObject checks = progress.entry("https://example.com/x").value("checks").toObject();
        QCOMPARE(checks, QJsonObject({{"Why?", "knew"}}));
        QVERIFY(!progress.setCheck("https://example.com/x", {}, "Why?", "maybe"));
    }

    void statsUseTheCategoryOfTheDay()
    {
        QTemporaryDir dir;
        Store store(dir.path());
        QString error;
        {
            Database database(dir.path());
            Progress progress(database);
            progress.setRead("https://example.com/x", "Other", true);
        }
        QJsonObject digest = sampleDigest("2026-11-09", "https://example.com/x");
        QJsonArray items = digest.value("items").toArray();
        QJsonObject first = items.first().toObject();
        first.insert("category", "Web");
        items.replace(0, first);
        digest.insert("items", items);
        QVERIFY(store.saveDigest(digest, &error));
        Backend backend(dir.path(), false);
        QCOMPARE(backend.stats().value("byCategory").toMap(), QVariantMap({{"Web", 1}}));
    }

    void secondWindowSeesProgress()
    {
        QTemporaryDir dir;
        Store store(dir.path());
        QString error;
        QVERIFY(store.saveDigest(sampleDigest("2026-11-09", "https://example.com/x"), &error));
        Backend first(dir.path(), false), second(dir.path(), false);
        first.setFavorite(first.items().first().toMap(), true);
        QTRY_COMPARE(second.favoriteCount(), 1);
    }

    void backendExportsThroughThePicker()
    {
        QTemporaryDir dir;
        Store store(dir.path());
        QString error;
        QVERIFY(store.saveDigest(sampleDigest("2026-11-09", "https://example.com/x"), &error));
        Backend backend(dir.path(), false);
        FakePicker picker;
        picker.answer = dir.path() + "/out.md";
        backend.setFilePicker(&picker);
        backend.exportMarkdown(true);
        QVERIFY(picker.suggested.toLocalFile().endsWith("writeuppulse-week-2026-11-09.md"));
        QFile out(picker.answer);
        QVERIFY(out.open(QIODevice::ReadOnly));
        QVERIFY(out.readAll().startsWith("# WriteupPulse — 2026-11-09"));
        QVERIFY(backend.notice().contains("out.md"));
    }

    void preferencesReachTheAgentAndTheOrder()
    {
        QTemporaryDir dir;
        Store store(dir.path());
        QString error;
        auto item = [](const QString &title, const QString &category) {
            return QJsonObject{{"title", title}, {"url", "https://example.com/" + title}, {"summary", "s"}, {"category", category}};
        };
        QVERIFY(store.saveDigest({{"date", "2026-11-09"}, {"items", QJsonArray{item("a", "Web"), item("b", "Pwn")}}}, &error));
        Backend backend(dir.path(), false);
        backend.setPreferredCategories({"Pwn"});
        QCOMPARE(backend.items().first().toMap().value("title").toString(), QString("b"));

        backend.setSearchCategories(Store::categories());
        QVERIFY(backend.searchCategories().isEmpty());
        backend.setSearchCategories({"Pwn", "Web"});
        QCOMPARE(backend.searchCategories(), QStringList({"Web", "Pwn"}));
        QVERIFY(Fetcher(Store(dir.path()), "/bin/true").prompt("2026-11-09").contains("especially wants: Pwn."));
        backend.setSearchCategories({});
        backend.setPreferredCategories({});
    }

    void storeSavesJsonAndMarkdown()
    {
        QTemporaryDir dir;
        Store store(dir.path());
        QString error;
        QVERIFY(store.saveDigest(sampleDigest("2026-11-08", "https://example.com/old"), &error));
        QVERIFY(store.saveDigest(sampleDigest("2026-11-09", "https://example.com/new"), &error));
        QVERIFY(store.saveStatus({{"ok", true}}));

        QCOMPARE(store.dates(), QStringList({"2026-11-09", "2026-11-08"}));
        QCOMPARE(store.recentUrls(7), QStringList({"https://example.com/new", "https://example.com/old"}));
        QFile md(dir.path() + "/2026-11-09.md");
        QVERIFY(md.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(md.readAll());
        QVERIFY(text.startsWith("# WriteupPulse — 2026-11-09"));
        QVERIFY(text.contains("## 1. A writeup"));
        QVERIFY(text.contains("- First lesson"));
    }

    void publishLeavesPrivateFieldsHome()
    {
        QTemporaryDir data, site;
        const Store store(data.path());
        QJsonObject day = Store::normalize(QJsonObject{{"items", QJsonArray{QJsonObject{
            {"title", "New"}, {"url", "https://example.com/a"}, {"summary", "S"}, {"category", "Web"}}}}},
            "2026-11-09", nullptr);
        day.insert("costUsd", 0.5);
        QVERIFY(store.saveDigest(day, nullptr));
        write(data.path() + "/progress.json", "{}");
        QVERIFY(Store(site.path()).saveDigest(QJsonObject{{"date", "2026-11-01"}, {"items", QJsonArray{QJsonObject{
            {"title", "Old"}, {"url", "https://example.com/o"}, {"summary", "S"}}}}}, nullptr));

        QString error;
        QVERIFY(store.publish(site.path(), &error));
        const Store published(site.path());
        QCOMPARE(published.dates(), (QStringList{"2026-11-09", "2026-11-01"}));
        const QJsonObject newest = published.digest("2026-11-09");
        QVERIFY(!newest.contains("costUsd"));
        QCOMPARE(newest["items"][0]["topic"].toString(), QString("Security"));
        QVERIFY(!QFile::exists(site.path() + "/progress.json"));

        QFile index(site.path() + "/index.json");
        QVERIFY(index.open(QIODevice::ReadOnly));
        const QJsonArray days = QJsonDocument::fromJson(index.readAll())["days"].toArray();
        QCOMPARE(days.size(), 2);
        QCOMPARE(days[0]["items"][0]["title"].toString(), QString("New"));
        QCOMPARE(days[1]["items"][0]["topic"].toString(), QString("Other"));
    }

    void syncDownloadsOnlyNewCheckedDays()
    {
        QTemporaryDir author, site, reader;
        QJsonObject day = Store::normalize(QJsonObject{{"items", QJsonArray{QJsonObject{
            {"title", "Shared"}, {"url", "https://example.com/s"}, {"summary", "S"}, {"category", "Pwn"}}}}},
            "2026-11-09", nullptr);
        QVERIFY(Store(author.path()).saveDigest(day, nullptr));
        QVERIFY(Store(author.path()).publish(site.path(), nullptr));

        const Store store(reader.path());
        Syncer syncer(store, QUrl::fromLocalFile(site.path()));
        QString error;
        int count = -1;
        QVERIFY2(syncer.run(&error, &count), qPrintable(error));
        QCOMPARE(count, 1);
        QCOMPARE(store.digest("2026-11-09")["items"][0]["title"].toString(), QString("Shared"));
        QVERIFY(store.status().value("ok").toBool());

        QVERIFY(syncer.run(&error, &count));
        QCOMPARE(count, 0);

        // A republished day comes again; a date that isn't one never becomes a file.
        day.insert("generatedAt", "2026-11-09T20:00:00");
        QVERIFY(Store(author.path()).saveDigest(day, nullptr));
        QVERIFY(Store(author.path()).publish(site.path(), nullptr));
        QFile index(site.path() + "/index.json");
        QVERIFY(index.open(QIODevice::ReadOnly));
        QJsonArray days = QJsonDocument::fromJson(index.readAll())["days"].toArray();
        index.close();
        days.append(QJsonObject{{"date", "../evil"}});
        write(site.path() + "/index.json", QJsonDocument(QJsonObject{{"days", days}}).toJson());
        QVERIFY(syncer.run(&error, &count));
        QCOMPARE(count, 1);
        QVERIFY(!QFile::exists(reader.path() + "/../evil.json"));

        QVERIFY(!Syncer(store, QUrl::fromLocalFile(reader.path() + "/missing")).run(&error));
        QVERIFY(!store.status().value("ok").toBool());
    }

    void sourceDefaultsToTheSite()
    {
        QSettings("writeuppulse", "writeuppulse").clear();
        QCOMPARE(Syncer::source(), QString("feed"));
        Syncer::setSource("claude");
        QCOMPARE(Syncer::source(), QString("claude"));
        QSettings("writeuppulse", "writeuppulse").clear();
    }

    void tutorStreamsALocalAnswer()
    {
        // A stand-in for Ollama: lists one model and streams an answer in pieces.
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QByteArray lastRequest;
        connect(&server, &QTcpServer::newConnection, this, [&] {
            QTcpSocket *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                lastRequest += socket->readAll();
                const qsizetype head = lastRequest.indexOf("\r\n\r\n");
                if (head < 0)
                    return;
                const QByteArray length = lastRequest.mid(lastRequest.toLower().indexOf("content-length:") + 15).split('\r').value(0).trimmed();
                if (lastRequest.startsWith("POST") && lastRequest.size() - head - 4 < length.toInt())
                    return;
                QByteArray body;
                if (lastRequest.startsWith("GET"))
                    body = QJsonDocument(QJsonObject{{"models", QJsonArray{QJsonObject{{"name", "tiny:latest"}}}}}).toJson(QJsonDocument::Compact);
                else
                    for (const char *piece : {"<think>hm</think>A buffer ", "overflow writes past ", "the end.```x```"})
                        body += QJsonDocument(QJsonObject{{"message", QJsonObject{{"content", piece}}}}).toJson(QJsonDocument::Compact) + "\n";
                socket->write("HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });

        QSettings("writeuppulse", "writeuppulse").setValue("tutorModel", "tiny");
        Tutor tutor(QUrl(QString("http://127.0.0.1:%1").arg(server.serverPort())));
        QTRY_COMPARE(tutor.status(), QString("ready"));

        tutor.open(QVariantMap{{"url", "https://example.com/a"}, {"title", "Overflow"}, {"summary", "About buffers."},
                               {"practice", QStringList{"Watch a crash in a VM", "Read the patch"}}}, 1);
        QVERIFY(tutor.systemPrompt().contains("About buffers."));
        QVERIFY(tutor.systemPrompt().contains("practice idea 2: \"Read the patch\""));
        QVERIFY(tutor.systemPrompt().contains("never write code"));

        lastRequest.clear();
        tutor.ask("What is it?");
        QVERIFY(tutor.busy());
        QTRY_VERIFY(!tutor.busy());
        QVERIFY2(tutor.error().isEmpty(), qPrintable(tutor.error()));
        const QVariantList messages = tutor.messages();
        QCOMPARE(messages.size(), 2);
        QCOMPARE(messages[1].toMap()["text"].toString(), QString("A buffer overflow writes past the end."));
        QVERIFY(lastRequest.contains("\"model\":\"tiny\""));

        // Each practice idea and the whole writeup keep their own conversation.
        tutor.open(QVariantMap{{"url", "https://example.com/a"}, {"title", "Overflow"}}, -1);
        QCOMPARE(tutor.messages().size(), 0);
        QSettings("writeuppulse", "writeuppulse").clear();
    }

    void tutorKnowsWhenItRunsOnBattery()
    {
        QTemporaryDir dir;
        auto supply = [&](const QString &name, const QByteArray &type, const QByteArray &online) {
            QDir().mkpath(dir.path() + "/" + name);
            write(dir.path() + "/" + name + "/type", type + "\n");
            if (!online.isEmpty())
                write(dir.path() + "/" + name + "/online", online + "\n");
        };
        QVERIFY(Tutor::onAcPower(dir.path()));
        supply("BAT0", "Battery", "");
        supply("ACAD", "Mains", "0");
        supply("ucsi-source-psy-1", "USB", "0");
        QVERIFY(!Tutor::onAcPower(dir.path()));
        supply("ucsi-source-psy-1", "USB", "1");
        QVERIFY(Tutor::onAcPower(dir.path()));
    }

    void tutorReportsAMissingOllama()
    {
        QTcpServer closed;
        QVERIFY(closed.listen(QHostAddress::LocalHost));
        const quint16 port = closed.serverPort();
        closed.close();
        Tutor tutor(QUrl(QString("http://127.0.0.1:%1").arg(port)));
        QTRY_COMPARE(tutor.status(), QString(Tutor::onAcPower() ? "missing" : "battery"));
        tutor.open(QVariantMap{{"url", "https://example.com/a"}, {"title", "T"}});
        tutor.ask("Hello?");
        QTRY_VERIFY(!tutor.busy());
        QVERIFY(!tutor.error().isEmpty());
        QCOMPARE(tutor.messages().size(), 0);
    }

    void fetcherSavesTheAgentsAnswer()
    {
        QTemporaryDir dir;
        const QString claude = fakeClaude(dir.path(), QJsonObject{
            {"type", "result"}, {"is_error", false}, {"total_cost_usd", 0.5},
            {"structured_output", QJsonObject{{"items", sampleDigest("", "https://example.com/t").value("items")}}}});
        Store store(dir.path() + "/data");
        Fetcher fetcher(store, claude);
        QString error;
        int count = 0;
        QVERIFY2(fetcher.run(&error, &count), qPrintable(error));
        QCOMPARE(count, 1);
        QCOMPARE(store.dates().size(), 1);
        QCOMPARE(store.status().value("ok").toBool(), true);
        QVERIFY(fetcher.arguments("2026-11-09").contains("WebSearch,WebFetch"));
        QVERIFY(!fetcher.arguments("2026-11-09").contains("Bash"));
    }

    void fetcherReportsAgentErrors()
    {
        QTemporaryDir dir;
        const QString claude = fakeClaude(dir.path(), QJsonObject{{"type", "result"}, {"is_error", true}, {"result", "Not logged in"}});
        Store store(dir.path() + "/data");
        Fetcher fetcher(store, claude);
        QString error;
        QVERIFY(!fetcher.run(&error));
        QVERIFY(error.contains("Not logged in"));
        QCOMPARE(store.dates().size(), 0);
        QCOMPARE(store.status().value("ok").toBool(true), false);
        QVERIFY(store.status().value("message").toString().contains("Not logged in"));
    }

    void backendFollowsNewDays()
    {
        QTemporaryDir dir;
        Store store(dir.path());
        QString error;
        QVERIFY(store.saveDigest(sampleDigest("2026-11-08", "https://example.com/old"), &error));

        Backend backend(dir.path(), false);
        QCOMPARE(backend.date(), QString("2026-11-08"));
        QCOMPARE(backend.items().size(), 1);
        QVERIFY(!backend.running());

        QVERIFY(store.saveDigest(sampleDigest("2026-11-09", "https://example.com/new"), &error));
        QTRY_COMPARE(backend.date(), QString("2026-11-09"));
        QCOMPARE(backend.dates().size(), 2);
        backend.olderDay();
        QCOMPARE(backend.date(), QString("2026-11-08"));
        QCOMPARE(backend.items().first().toMap().value("url").toString(), QString("https://example.com/old"));
    }

    void translatorFallsBackToEnglish()
    {
        QTemporaryDir dir;
        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("en"));
        QVERIFY(translator.translate("Main", "Save").isNull());
    }

    void translatorFollowsTheLanguageFile()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "pt-BR\n");
        write(dir.path() + "/pt.json", R"({"Save": "Guardar", "Open": "Abrir"})");
        write(dir.path() + "/pt-BR.json", R"({"Save": "Salvar"})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.language(), QString("pt-BR"));
        QCOMPARE(translator.translate("Main", "Save"), QString("Salvar"));
        QCOMPARE(translator.translate("Main", "Open"), QString("Abrir"));
        QVERIFY(translator.translate("Main", "Quit").isNull());
    }

    void translatorPicksPluralForms()
    {
        QTemporaryDir dir;
        write(dir.path() + "/language", "ru\n");
        write(dir.path() + "/en.json", R"({"%n clips": {"one": "%n clip", "other": "%n clips"}})");
        write(dir.path() + "/ru.json", R"({"%n clips": {"one": "%n клип", "few": "%n клипа", "many": "%n клипов", "other": "%n клипа"}})");

        Translator translator(dir.path() + "/language", dir.path());
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 21), QString("%n клип"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 22), QString("%n клипа"));
        QCOMPARE(translator.translate("Main", "%n clips", nullptr, 25), QString("%n клипов"));
        QCOMPARE(Translator::pluralCategory("ar", 11), QString("many"));
        QCOMPARE(Translator::pluralCategory("ja", 1), QString("other"));
    }

private:
    static QJsonObject sampleDigest(const QString &date, const QString &url)
    {
        return {{"date", date}, {"items", QJsonArray{QJsonObject{
            {"title", "A writeup"}, {"url", url}, {"source", "example.com"}, {"popularity", "HN · 100 points"},
            {"why", "Because."}, {"summary", "Summary."}, {"takeaways", QJsonArray{"First lesson"}}, {"tags", QJsonArray{"security"}},
            {"questions", QJsonArray{QJsonObject{{"question", "Why?"}, {"answer", "Because."}}}}}}}};
    }

    static QString fakeClaude(const QString &directory, const QJsonObject &answer)
    {
        const QString answerPath = directory + "/answer.json", command = directory + "/claude";
        write(answerPath, QJsonDocument(answer).toJson());
        write(command, "#!/bin/sh\ncat '" + answerPath.toUtf8() + "'\n");
        QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        return command;
    }

    static void write(const QString &path, const QByteArray &contents)
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(contents);
    }
};

QTEST_MAIN(WriteupPulseTests)
#include "writeuppulse_tests.moc"
