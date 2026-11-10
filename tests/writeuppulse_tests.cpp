#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>

#include "archive.h"
#include "backend.h"
#include "database.h"
#include "extractor.h"
#include "fetcher.h"
#include "filepicker.h"
#include "llmclient.h"
#include "progress.h"
#include "store.h"
#include "syncer.h"
#include "translator.h"
#include "tutor.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <memory>

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

    void archiveAsksOnlyPublicAddresses()
    {
        for (const char *address : {"127.0.0.1", "10.1.2.3", "172.16.0.1", "192.168.1.1", "169.254.1.1", "100.64.0.1",
                                    "0.0.0.0", "224.0.0.1", "::1", "fe80::1", "fd00::1", "::ffff:127.0.0.1", "::ffff:10.0.0.1"})
            QVERIFY2(!Archive::isPublic(QHostAddress(address)), address);
        for (const char *address : {"93.184.215.14", "1.1.1.1", "2606:4700:4700::1111", "::ffff:1.1.1.1"})
            QVERIFY2(Archive::isPublic(QHostAddress(address)), address);
    }

    void archiveKeepsTheReadableText()
    {
        const QByteArray html =
            "<html><head><meta charset=\"utf-8\"><title>T</title><style>body{}</style>"
            "<script>alert('x')</script></head><body><nav>Home | About</nav>"
            "<aside>Popular posts</aside><article><h1>Heap grooming</h1>"
            "<p>Freed chunks &amp; bins&nbsp;matter &#8212; a lot.</p><!-- hidden -->"
            "<pre>int main() { return 0; }</pre><p>Привет, <b>мир</b> &#x41;</p></article>"
            "<footer>(c) site</footer></body></html>";
        const QString text = Archive::text(html);
        QCOMPARE(text, QString("Heap grooming\nFreed chunks & bins matter — a lot.\nПривет, мир A"));

        QString long_;
        for (int i = 0; i < 30; ++i)
            long_ += QString("Paragraph %1 ").arg(i).repeated(10) + "\n";
        const QStringList passages = Archive::passages(long_);
        QVERIFY(passages.size() > 1);
        for (const auto &passage : passages)
            QVERIFY(passage.size() <= 1200);
        QCOMPARE(passages.join('\n').split('\n').size(), 30);
    }

    void archiveSavesPagesAndGivesUpOnTheRest()
    {
        // A stand-in web server: a redirect, a page, a PDF, and a page too big to keep.
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        int connections = 0;
        connect(&server, &QTcpServer::newConnection, this, [&] {
            ++connections;
            QTcpSocket *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [socket] {
                const QByteArray request = socket->readAll();
                const QByteArray path = request.split(' ').value(1);
                QByteArray head = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n", body;
                if (path == "/old")
                    head = "HTTP/1.1 301 Moved\r\nLocation: /page\r\n";
                else if (path == "/page")
                    body = "<html><body><main><p>Zebra stripes explain the allocator.</p></main></body></html>";
                else if (path == "/paper.pdf")
                    head = "HTTP/1.1 200 OK\r\nContent-Type: application/pdf\r\n", body = "%PDF-1.7";
                else if (path == "/big")
                    body = QByteArray(20000, 'x');
                else
                    head = "HTTP/1.1 404 Not Found\r\n";
                socket->write(head + "Content-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
        const QString base = QString("http://127.0.0.1:%1").arg(server.serverPort());

        QTemporaryDir dir;
        Database database(dir.path());
        Archive archive(database);
        archive.allowPrivate = true;
        // Pages are kept by canonical URL, which makes this one https.
        auto status = [&](const QString &path) { return database.pageStatus(Store::canonicalUrl(base + path)); };
        archive.limit = 8000;
        QSignalSpy saved(&archive, &Archive::saved);
        archive.enqueue({base + "/old", base + "/paper.pdf", base + "/big", base + "/gone", "ftp://example.com/x"});
        QTRY_COMPARE(saved.count(), 5);

        QCOMPARE(status("/old"), QString("ok"));
        QCOMPARE(database.passages(Store::canonicalUrl(base + "/old")), QStringList({"Zebra stripes explain the allocator."}));
        QCOMPARE(status("/paper.pdf"), QString("skipped"));
        QCOMPARE(status("/big"), QString("failed"));
        QCOMPARE(status("/gone"), QString("failed"));
        QCOMPARE(database.pageStatus("ftp://example.com/x"), QString("refused"));
        QVERIFY(database.matchPages("zebr").contains(Store::canonicalUrl(base + "/old")));

        // Nothing is asked twice: saved pages are done, failures wait six hours.
        const int asked = connections;
        archive.enqueue({base + "/old", base + "/big"});
        QTest::qWait(200);
        QCOMPARE(connections, asked);

        // This machine is never asked outside tests.
        Archive strict(database);
        QSignalSpy refused(&strict, &Archive::saved);
        strict.enqueue({base + "/page"});
        QTRY_COMPARE(refused.count(), 1);
        QCOMPARE(status("/page"), QString("refused"));
        QCOMPARE(connections, asked);
    }

    void searchCoversSavedPages()
    {
        QTemporaryDir dir;
        Store store(dir.path());
        QString error;
        auto item = [](const QString &title, const QString &summary) {
            return QJsonObject{{"title", title}, {"url", "https://example.com/" + title}, {"summary", summary}};
        };
        QVERIFY(store.saveDigest({{"date", "2026-11-09"}, {"items", QJsonArray{
            item("a", "Nothing here."), item("b", "About the zebra allocator.")}}}, &error));
        {
            Database database(dir.path());
            QVERIFY(database.savePage("https://example.com/a", "ok", {"A long text that mentions a zebra once."}, {}));
        }
        Backend backend(dir.path(), false);
        backend.setQuery("zebra");
        QCOMPARE(backend.items().size(), 2);
        // A summary hit comes before a hit only in the page's text.
        QCOMPARE(backend.items().first().toMap().value("title").toString(), QString("b"));
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
        LlmClient llm(QUrl(QString("http://127.0.0.1:%1").arg(server.serverPort())));
        Tutor tutor(llm);
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
        QVERIFY(LlmClient::onAcPower(dir.path()));
        supply("BAT0", "Battery", "");
        supply("ACAD", "Mains", "0");
        supply("ucsi-source-psy-1", "USB", "0");
        QVERIFY(!LlmClient::onAcPower(dir.path()));
        supply("ucsi-source-psy-1", "USB", "1");
        QVERIFY(LlmClient::onAcPower(dir.path()));
    }

    void tutorReportsAMissingOllama()
    {
        QTcpServer closed;
        QVERIFY(closed.listen(QHostAddress::LocalHost));
        const quint16 port = closed.serverPort();
        closed.close();
        LlmClient llm(QUrl(QString("http://127.0.0.1:%1").arg(port)));
        Tutor tutor(llm);
        QTRY_COMPARE(tutor.status(), QString(LlmClient::onAcPower() ? "missing" : "battery"));
        tutor.open(QVariantMap{{"url", "https://example.com/a"}, {"title", "T"}});
        tutor.ask("Hello?");
        QTRY_VERIFY(!tutor.busy());
        QVERIFY(!tutor.error().isEmpty());
        QCOMPARE(tutor.messages().size(), 0);
    }

    void extractorValidatesTheModelsAnswer()
    {
        QCOMPARE(Extractor::id("  Cross-site Scripting (XSS) "), QString("cross-site-scripting-xss"));
        QCOMPARE(Extractor::id("C++"), QString("c++"));
        auto concept_ = [](const QString &name, const QString &kind, const QStringList &aliases = {}) {
            return QJsonObject{{"name", name}, {"kind", kind}, {"aliases", QJsonArray::fromStringList(aliases)}};
        };
        const QJsonObject answer{{"concepts", QJsonArray{
            concept_("Cross-site scripting", "vulnerability", {"XSS", "cross site scripting", "Exploit"}),
            concept_("`DOM`", "magic"), concept_("cross-site scripting", "concept"), concept_("Proof of Concept", "technique"),
            concept_("", "tool"), concept_(QString(90, 'a'), "tool"), concept_("Content Security Policy", "technology", {"CSP"})}}};
        const QStringList passages{"An XSS payload changes the DOM.", "CSP stops most cross-site scripting.",
                                   "The DOM, again, with XSS.", "Nothing here mentions a domain."};
        const QJsonObject valid = Extractor::validate(answer, passages, true);
        const QJsonArray concepts = valid.value("concepts").toArray();
        QCOMPARE(concepts.size(), 3);
        QCOMPARE(concepts[0].toObject().value("id").toString(), QString("cross-site-scripting"));
        QCOMPARE(concepts[0].toObject().value("aliases").toArray(), QJsonArray{"xss"});
        QCOMPARE(concepts[1].toObject().value("name").toString(), QString("DOM"));
        QCOMPARE(concepts[1].toObject().value("kind").toString(), QString("concept"));
        // Concepts named in one passage, by name or alias, whole words only; the strongest pair first.
        const QJsonArray edges = valid.value("edges").toArray();
        QCOMPARE(edges.size(), 2);
        QCOMPARE(edges[0].toObject().value("from").toString(), QString("cross-site-scripting"));
        QCOMPARE(edges[0].toObject().value("to").toString(), QString("dom"));
        QCOMPARE(edges[0].toObject().value("weight").toInt(), 2);
        QCOMPARE(edges[0].toObject().value("passage").toInt(), 0);
        QCOMPARE(edges[1].toObject().value("from").toString(), QString("content-security-policy"));
        QCOMPARE(edges[1].toObject().value("passage").toInt(), 1);
        QCOMPARE(Extractor::validate(answer, passages, false).value("edges").toArray().first().toObject().value("passage").toInt(), -1);

        // No concept is tied to everything.
        QJsonArray many{concept_("Hub", "concept")};
        QString text = "hub";
        QStringList pairs;
        for (int i = 0; i < 10; ++i) {
            many.append(concept_(QString("Leaf%1").arg(i), "concept"));
            text += QString(" leaf%1").arg(i);
            pairs << QString("hub leaf%1").arg(i);
        }
        int hub = 0;
        for (const auto &edge : Extractor::validate({{"concepts", many}}, pairs, true).value("edges").toArray())
            hub += edge.toObject().value("from").toString() == "hub" || edge.toObject().value("to").toString() == "hub";
        QCOMPARE(hub, 6);
        // ... and an overview naming more than six relates nothing.
        QVERIFY(Extractor::validate({{"concepts", many}}, {text.section(' ', 0, 7)}, true).value("edges").toArray().isEmpty());
    }

    void conceptsJoinAcrossWriteups()
    {
        QTemporaryDir dir;
        Database database(dir.path());
        QVERIFY(database.error().isEmpty());
        auto concept_ = [](const QString &name, const QStringList &aliases = {}) {
            return QJsonObject{{"name", name}, {"kind", "vulnerability"}, {"aliases", QJsonArray::fromStringList(aliases)}};
        };
        auto result = [](const QJsonArray &concepts, const QStringList &passages = {}) {
            return Extractor::validate(QJsonObject{{"concepts", concepts}}, passages, true);
        };
        const QString a = "https://example.com/a", b = "https://example.com/b";
        QVERIFY(database.wantsConcepts(a));
        QVERIFY(database.saveConcepts(a, "ok", result({concept_("Cross-site scripting", {"XSS"}), concept_("DOM")},
            {"Intro.", "Nothing.", "XSS rewrites the DOM."}), "page", "tiny"));
        QVERIFY(!database.wantsConcepts(a));
        // Another writeup's "XSS" is the same concept.
        QVERIFY(database.saveConcepts(b, "ok", result({concept_("XSS"), concept_("Content Security Policy")}), "summary", "tiny"));
        const QJsonArray concepts = database.knowledge(b).value("concepts").toArray();
        QCOMPARE(concepts.size(), 2);
        QCOMPARE(concepts[1].toObject().value("id").toString(), QString("cross-site-scripting"));
        const QJsonObject edge = database.knowledge(a).value("edges").toArray().first().toObject();
        QCOMPARE(edge.value("from").toString(), QString("cross-site-scripting"));
        QCOMPARE(edge.value("type").toString(), QString("RELATED_TO"));
        QCOMPARE(edge.value("passage").toInt(), 2);
        QCOMPARE(edge.value("weight").toInt(), 1);
        QCOMPARE(edge.value("model").toString(), QString("tiny"));

        // Summary concepts are done again once the page is saved; a failure then keeps them.
        QVERIFY(!database.wantsConcepts(b));
        QVERIFY(database.savePage(b, "ok", {"Text."}, {}));
        QVERIFY(database.wantsConcepts(b));
        QVERIFY(database.saveConcepts(b, "failed", {}, "page", "tiny", "Timed out."));
        QVERIFY(!database.wantsConcepts(b));
        QCOMPARE(database.conceptsStatus(b), QString("ok"));
        QCOMPARE(database.knowledge(b).value("concepts").toArray().size(), 2);

        // A new run replaces the old one, and concepts nothing mentions go.
        QVERIFY(database.saveConcepts(b, "ok", result({concept_("Clickjacking")}), "page", "tiny"));
        QCOMPARE(database.knowledge(b).value("concepts").toArray().size(), 1);
        QSqlQuery query(QSqlDatabase::database(QSqlDatabase::connectionNames().first()));
        QVERIFY(query.exec("SELECT count(*) FROM concepts") && query.next());
        QCOMPARE(query.value(0).toInt(), 3);

        QVERIFY(database.saveConcepts("https://example.com/c", "failed", {}, "summary", "tiny", "No JSON."));
        QVERIFY(!database.wantsConcepts("https://example.com/c"));
    }

    void extractorWaitsForTheReader()
    {
        // A stand-in for Ollama that holds extraction requests until released.
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QList<QPointer<QTcpSocket>> held;
        QByteArrayList extractions;
        const QJsonObject answer{
            {"concepts", QJsonArray{QJsonObject{{"name", "Heap spraying"}, {"kind", "technique"}, {"aliases", QJsonArray{"spraying"}}},
                                    QJsonObject{{"name", "Heap"}, {"kind", "concept"}, {"aliases", QJsonArray{}}}}}};
        const QByteArray json = QJsonDocument(answer).toJson(QJsonDocument::Compact);
        auto stream = [](const QList<QByteArray> &pieces) {
            QByteArray body;
            for (const auto &piece : pieces)
                body += QJsonDocument(QJsonObject{{"message", QJsonObject{{"content", QString::fromUtf8(piece)}}}}).toJson(QJsonDocument::Compact) + "\n";
            return "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
        };
        connect(&server, &QTcpServer::newConnection, this, [&] {
            QTcpSocket *socket = server.nextPendingConnection();
            auto request = std::make_shared<QByteArray>();
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket, request] {
                *request += socket->readAll();
                const qsizetype head = request->indexOf("\r\n\r\n");
                if (head < 0)
                    return;
                const QByteArray length = request->mid(request->toLower().indexOf("content-length:") + 15).split('\r').value(0).trimmed();
                if (request->startsWith("POST") && request->size() - head - 4 < length.toInt())
                    return;
                if (request->startsWith("GET")) {
                    const QByteArray body = QJsonDocument(QJsonObject{{"models", QJsonArray{QJsonObject{{"name", "tiny:latest"}}}}}).toJson(QJsonDocument::Compact);
                    socket->write("HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                    socket->disconnectFromHost();
                } else if (request->contains("\"format\"")) {
                    extractions << request->mid(head + 4);
                    held << socket;
                } else {
                    socket->write(stream({"Heap spraying fills memory."}));
                    socket->disconnectFromHost();
                }
            });
        });
        auto release = [&] {
            for (const auto &socket : held)
                if (socket && socket->state() == QAbstractSocket::ConnectedState) {
                    socket->write(stream({json.left(20), json.mid(20)}));
                    socket->disconnectFromHost();
                }
            held.clear();
        };

        QSettings("writeuppulse", "writeuppulse").setValue("tutorModel", "tiny");
        QTemporaryDir dir;
        Database database(dir.path());
        LlmClient llm(QUrl(QString("http://127.0.0.1:%1").arg(server.serverPort())));
        Extractor extractor(database, llm);
        extractor.pauseMs = 50;
        QSignalSpy extracted(&extractor, &Extractor::extracted);
        const QVariantMap item{{"url", "https://example.com/heap"}, {"title", "Heaps"}, {"summary", "About heaps."}};
        const QVariantMap waiting{{"url", "https://example.com/later"}, {"title", "Later"}, {"summary", "Not yet."}};
        QVERIFY(database.savePage("https://example.com/heap", "ok", {"Heaps grow.", "Spraying needs a heap."}, {}));

        // Nothing is asked before the model is ready, or before a page the Archive will still get.
        extractor.enqueue({item, waiting});
        QVERIFY(!extractor.busy());
        QTRY_COMPARE(llm.status(), QString("ready"));
        QTRY_COMPARE(extractions.size(), 1);
        QVERIFY(extractions[0].contains("Heaps grow.\\n\\nSpraying needs a heap."));
        QVERIFY(extractions[0].contains("\"stream\":true"));
        // A model that repeats itself can't go on for long.
        QVERIFY(extractions[0].contains("\"maxItems\":20"));
        QVERIFY(extractions[0].contains("\"num_predict\":1500"));

        // A question to the tutor stops the extraction, which starts over after the answer.
        Tutor tutor(llm);
        tutor.open(item);
        tutor.ask("What is spraying?");
        QVERIFY(!extractor.busy());
        QTRY_VERIFY(!tutor.busy());
        QCOMPARE(tutor.messages().size(), 2);
        QCOMPARE(extracted.count(), 0);
        QTRY_COMPARE(extractions.size(), 2);
        release();
        QTRY_COMPARE(extracted.count(), 1);

        const QJsonObject knowledge = database.knowledge("https://example.com/heap");
        QCOMPARE(knowledge.value("concepts").toArray().size(), 2);
        const QJsonObject edge = knowledge.value("edges").toArray().first().toObject();
        QCOMPARE(edge.value("from").toString(), QString("heap"));
        QCOMPARE(edge.value("to").toString(), QString("heap-spraying"));
        QCOMPARE(edge.value("passage").toInt(), 1);
        QCOMPARE(edge.value("model").toString(), QString("tiny"));
        QCOMPARE(database.conceptsStatus("https://example.com/later"), QString());

        // Without page downloads, the summary is the material.
        extractor.waitForPages = false;
        extractor.enqueue({item, waiting});
        QTRY_COMPARE(extractions.size(), 3);
        QVERIFY(extractions[2].contains("Not yet."));
        release();
        QTRY_COMPARE(extracted.count(), 2);
        QVERIFY(!database.wantsConcepts("https://example.com/later"));
        QSettings("writeuppulse", "writeuppulse").clear();
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
