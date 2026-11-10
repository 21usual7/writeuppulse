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
#include "notes.h"
#include "progress.h"
#include "references.h"
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
        QVERIFY(Fetcher(Store(dir.path()), "/bin/true").prompt("2026-11-09").contains("at least one Junior piece"));
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

        // The site also gets the labs and an Atom feed of the newest writeups.
        QVERIFY(QFile::exists(site.path() + "/labs.json"));
        QFile feed(site.path() + "/feed.xml");
        QVERIFY(feed.open(QIODevice::ReadOnly));
        const QByteArray atom = feed.readAll();
        QVERIFY(atom.contains("<feed xmlns=\"http://www.w3.org/2005/Atom\">"));
        QVERIFY(atom.contains("<title>New</title>"));
        QVERIFY(atom.contains("<link href=\"https://example.com/a\"/>"));
        QCOMPARE(atom.count("<entry>"), 2);
        QVERIFY(atom.contains("Z</updated>"));

        // Each writeup names its catalog terms for the glossary.
        const QJsonArray terms = Store::terms(QJsonObject{{"title", "A stored XSS"}, {"summary", "Then SSRF to the metadata service."}});
        QCOMPARE(terms.size(), 2);
        QVERIFY(terms[0].toObject().value("url").toString().startsWith("https://capec.mitre.org/")
                || terms[1].toObject().value("url").toString().startsWith("https://capec.mitre.org/"));
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

    void reviewSpacesTheQuestions()
    {
        QTemporaryDir dir;
        Database database(dir.path());
        Progress progress(database);
        auto writeup = [](const QString &name, const QString &date, int questions) {
            QVariantList list;
            for (int i = 0; i < questions; ++i)
                list << QVariantMap{{"question", QString("%1 %2?").arg(name).arg(i)}, {"answer", "Yes."}};
            return QVariantMap{{"url", "https://example.com/" + name}, {"date", date}, {"title", name}, {"questions", list}};
        };
        const QDate today = QDate::currentDate();
        const QString yesterday = today.addDays(-1).toString(Qt::ISODate);
        // Newest first, like the window's library.
        const QVariantList library{writeup("today", today.toString(Qt::ISODate), 2), writeup("old", yesterday, 4),
                                   writeup("read", yesterday, 3)};
        auto questions = [](const QVariantMap &review) {
            QStringList list;
            for (const auto &card : review.value("cards").toList())
                list << card.toMap().value("question").toString();
            return list;
        };

        // New questions: the read writeup's first, then earlier days', never today's unread ones, five a day.
        QVERIFY(progress.setRead("https://example.com/read", "Web", true, today.addDays(-1)));
        QCOMPARE(questions(progress.review(library, today)),
                 QStringList({"read 0?", "read 1?", "read 2?", "old 0?", "old 1?"}));
        QCOMPARE(progress.review(library, today, 2).value("cards").toList().size(), 2);

        // Answering one takes it out of today and counts against today's new ones.
        QVERIFY(progress.setCheck("https://example.com/read", "Web", "read 0?", "knew"));
        QVERIFY(progress.setCheck("https://example.com/read", "Web", "read 1?", "missed"));
        QVariantMap review = progress.review(library, today);
        QCOMPARE(questions(review), QStringList({"read 2?", "old 0?", "old 1?"}));
        QCOMPARE(review.value("reviewedToday").toInt(), 2);
        // A day of review keeps the streak going without reading anything new.
        QCOMPARE(progress.streak(), 2);
        QCOMPARE(review.value("nextDue").toString(), today.addDays(1).toString(Qt::ISODate));

        // Both come back tomorrow, in box 1.
        review = progress.review(library, today.addDays(1));
        QCOMPARE(questions(review).mid(0, 2), QStringList({"read 0?", "read 1?"}));
        QCOMPARE(review.value("cards").toList().first().toMap().value("box").toInt(), 1);

        // Known again, it waits three days in box 2; taking an answer back undoes it.
        QSqlQuery query(QSqlDatabase::database(QSqlDatabase::connectionNames().first()));
        QVERIFY(query.exec(QString("UPDATE events SET at = '%1T09:00:00'").arg(today.addDays(-1).toString(Qt::ISODate))));
        QVERIFY(progress.setCheck("https://example.com/read", "Web", "read 0?", "knew"));
        QVERIFY(!questions(progress.review(library, today.addDays(2))).contains("read 0?"));
        QVERIFY(questions(progress.review(library, today.addDays(3))).contains("read 0?"));
        QVERIFY(progress.setCheck("https://example.com/read", "Web", "read 0?", ""));
        QVERIFY(questions(progress.review(library, today)).contains("read 0?"));
    }

    void referencesFindCatalogEntries()
    {
        const References &references = References::instance();
        QVERIFY(!references.isEmpty());
        QCOMPARE(references.entry("CWE-79").value("kind").toString(), QString("weakness"));
        QVERIFY(references.entry("A01:2025").value("related").toArray().size() > 5);
        QVERIFY(references.notice().contains("MITRE"));

        auto ids = [&](const QStringList &texts) {
            QStringList list;
            for (const auto &hit : references.find(texts))
                list << hit.id;
            std::sort(list.begin(), list.end());
            return list;
        };
        // Names, other names, IDs, any case and hyphenation; the longest name wins.
        QCOMPARE(ids({"An XSS, then a use-after-free and CWE-918."}), QStringList({"CWE-416", "CWE-79", "CWE-918"}));
        QCOMPARE(ids({"A stored XSS."}), QStringList({"CAPEC-592"}));
        QCOMPARE(ids({"Blind SQL injection in the login form."}), QStringList({"CAPEC-7"}));
        QVERIFY(ids({"Server-side request forgery via the PDF renderer."}).contains("CWE-918"));
        // ATT&CK comes first when catalogs share a name.
        QCOMPARE(ids({"Kerberoasting service accounts"}), QStringList({"T1558.003"}));
        // Abbreviations only in their capitals; words that only look like one stay words.
        QVERIFY(ids({"xss is lowercase here, and it isn't an ISN probe"}).isEmpty());

        QList<QStringList> named;
        const auto hits = references.find({"Phishing led to XSS.", "Nothing.", "More XSS."}, &named);
        QCOMPARE(named.size(), 3);
        QVERIFY(named[1].isEmpty());
        const auto xss = std::find_if(hits.begin(), hits.end(), [](const auto &hit) { return hit.id == "CWE-79"; });
        QVERIFY(xss != hits.end());
        QCOMPARE(xss->count, 2);
        QCOMPARE(xss->first, 0);
        QCOMPARE(hits.first().id, QString("CWE-79"));
    }

    void backendRelatesTheReferencesOfTheLibrary()
    {
        QTemporaryDir dir;
        Store store(dir.path());
        QString error;
        auto digest = [](const QString &date, const QString &name, const QString &summary) {
            return QJsonObject{{"date", date}, {"items", QJsonArray{QJsonObject{
                {"title", name}, {"url", "https://example.com/" + name}, {"summary", summary}}}}};
        };
        QVERIFY(store.saveDigest(digest("2026-11-08", "a", "An XSS."), &error));
        QVERIFY(store.saveDigest(digest("2026-11-09", "b", "Another way in."), &error));
        {
            Database database(dir.path());
            QVERIFY(database.savePage("https://example.com/b", "ok", {"Intro.", "Phishing mail carried an XSS payload."}, {}));
        }
        Backend backend(dir.path(), false);
        backend.setGraphSource("references");
        const QVariantMap graph = backend.graph();
        QCOMPARE(graph.value("done").toInt(), 2);
        QStringList nodes;
        for (const auto &node : graph.value("nodes").toList())
            nodes << node.toMap().value("id").toString();
        QVERIFY(nodes.contains("CWE-79"));
        QVERIFY(nodes.contains("T1566"));
        QCOMPARE(graph.value("edges").toList().size(), 1);

        const QVariantList found = backend.references("https://example.com/b");
        QVERIFY(found.size() >= 2);
        QVERIFY(!found.first().toMap().value("text").toString().isEmpty());
        QVERIFY(found.first().toMap().value("link").toString().startsWith("https://"));

        const QVariantMap xss = backend.conceptInfo("CWE-79");
        QCOMPARE(xss.value("writeups").toList().size(), 2);
        QCOMPARE(xss.value("writeups").toList().first().toMap().value("title").toString(), QString("a"));
        QVERIFY(xss.value("aliases").toStringList().contains("XSS"));
        const QVariantMap phishing = xss.value("neighbours").toList().first().toMap();
        QCOMPARE(phishing.value("id").toString(), QString("T1566"));
        QCOMPARE(phishing.value("evidence").toList().first().toMap().value("passage").toInt(), 1);
        QVERIFY(phishing.value("evidence").toList().first().toMap().value("text").toString().startsWith("Phishing mail"));
        QVERIFY(!xss.value("catalog").toList().isEmpty());
        QVERIFY(!xss.value("link").toString().isEmpty());
    }

    void backendPutsTheReadersLevelFirst()
    {
        QTemporaryDir dir;
        Store store(dir.path());
        QString error;
        auto item = [](const QString &title, const QString &level) {
            return QJsonObject{{"title", title}, {"url", "https://example.com/" + title}, {"summary", "About " + title},
                               {"category", "Web"}, {"level", level},
                               {"questions", QJsonArray{QJsonObject{{"question", title + "?"}, {"answer", "Yes."}}}}};
        };
        QVERIFY(store.saveDigest({{"date", "2026-11-01"}, {"items", QJsonArray{item("deep", "Senior"), item("easy", "Junior")}}}, &error));
        Backend backend(dir.path(), false);
        auto titles = [&] {
            QStringList list;
            for (const auto &value : backend.items())
                list << value.toMap().value("title").toString();
            return list;
        };
        QCOMPARE(titles(), QStringList({"deep", "easy"}));
        backend.setMyLevel("Junior");
        QCOMPARE(titles(), QStringList({"easy", "deep"}));
        QCOMPARE(backend.review().value("cards").toList().first().toMap().value("question").toString(), QString("easy?"));
        // Labs for the direction, this level's first.
        const QVariantList labs = backend.labs("Web", "Junior");
        QCOMPARE(labs.size(), 3);
        for (const auto &lab : labs)
            QVERIFY(lab.toMap().value("levels").toStringList().contains("Junior"));
        QVERIFY(!labs.first().toMap().value("about").toMap().value("uk").toString().isEmpty());
        QVERIFY(backend.labs("Postmortems", "Junior").isEmpty());
        backend.setMyLevel("Nobody");
        QCOMPARE(backend.myLevel(), QString());
        QCOMPARE(titles(), QStringList({"deep", "easy"}));
    }

    void notesGoToTheDatabaseAndTheVault()
    {
        QTemporaryDir dir;
        {
            Database database(dir.path());
            QVERIFY(database.saveNote("https://example.com/a", "First thought."));
            QCOMPARE(database.note("https://example.com/a"), QString("First thought."));
            QVERIFY(database.setNotionPage("https://example.com/a", "page1"));
            // Emptied, the note goes, but the Notion page is remembered.
            QVERIFY(database.saveNote("https://example.com/a", ""));
            QVERIFY(database.note("https://example.com/a").isEmpty());
            QCOMPARE(database.notionPage("https://example.com/a"), QString("page1"));
        }

        const QVariantMap item{{"title", "Heap: a tour / part 1"}, {"url", "https://example.com/heap"}, {"date", "2026-11-09"},
                               {"category", "Pwn"}, {"level", "Senior"}, {"summary", "About heaps."},
                               {"takeaways", QVariantList{"Bins matter."}}};
        QCOMPARE(Notes::fileName(item), QString("2026-11-09 Heap a tour part 1.md"));
        const QString text = Notes::markdown(item, "Mine.", {"CWE-416 Use After Free"}, {"to study"});
        QVERIFY(text.startsWith("---\ntitle: \"Heap: a tour / part 1\"\n"));
        QVERIFY(text.contains("tags: [writeuppulse, pwn, to-study]"));
        QVERIFY(text.contains("[[CWE-416 Use After Free]]"));
        QVERIFY(text.contains("- Bins matter."));
        QVERIFY(text.trimmed().endsWith("Mine."));

        QTemporaryDir vault;
        QString path, error;
        QVERIFY(Notes::writeToVault(vault.path(), item, "One.", {}, {}, &path, &error));
        QCOMPARE(path, vault.path() + "/WriteupPulse/2026-11-09 Heap a tour part 1.md");
        QVERIFY(Notes::writeToVault(vault.path(), item, "Two.", {}, {}, &path, &error));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().contains("Two."));
        file.close();
        // Edited in Obsidian, the file is the reader's from then on.
        QVERIFY(file.open(QIODevice::Append));
        file.write("My own line.\n");
        file.close();
        QVERIFY(!Notes::writeToVault(vault.path(), item, "Three.", {}, {}, &path, &error));
        QVERIFY(error.contains("left as it is"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().contains("My own line."));

        QCOMPARE(Notes::notionId("https://www.notion.so/me/Learning-1a2b3c4d5e6f40718293a4b5c6d7e8f9?pvs=4"),
                 QString("1a2b3c4d5e6f40718293a4b5c6d7e8f9"));
        QCOMPARE(Notes::notionId("1a2b3c4d-5e6f-4071-8293-a4b5c6d7e8f9"), QString("1a2b3c4d5e6f40718293a4b5c6d7e8f9"));
        QVERIFY(Notes::notionId("not a page").isEmpty());
    }

    void notesSendAPageToNotion()
    {
        // A stand-in for api.notion.com that records requests.
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QByteArrayList requests;
        connect(&server, &QTcpServer::newConnection, this, [&] {
            QTcpSocket *socket = server.nextPendingConnection();
            auto request = std::make_shared<QByteArray>();
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket, request] {
                *request += socket->readAll();
                const qsizetype head = request->indexOf("\r\n\r\n");
                if (head < 0)
                    return;
                const QByteArray length = request->mid(request->toLower().indexOf("content-length:") + 15).split('\r').value(0).trimmed();
                if (request->size() - head - 4 < length.toInt())
                    return;
                requests << *request;
                const QByteArray body = request->startsWith("POST") ? QByteArray("{\"object\":\"page\",\"id\":\"new-page\"}") : QByteArray("{}");
                socket->write("HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: application/json\r\nContent-Length: "
                              + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
        Notes notes;
        notes.api = QUrl(QString("http://127.0.0.1:%1").arg(server.serverPort()));
        QSignalSpy sent(&notes, &Notes::sent);
        const QVariantMap item{{"title", "Heaps"}, {"url", "https://example.com/heap"}, {"summary", "One.\n\nTwo."},
                               {"takeaways", QVariantList{"Bins."}}};
        notes.sendToNotion(item, QString(2500, 'x'), {"CWE-416 Use After Free"}, "secret", "parentid", "oldpage");
        QTRY_COMPARE(sent.count(), 1);
        QCOMPARE(sent.first().at(1).toBool(), true);
        QCOMPARE(sent.first().at(2).toString(), QString("new-page"));
        QCOMPARE(requests.size(), 2);
        // The old page to the trash first, then the new one.
        QVERIFY(requests[0].startsWith("PATCH /v1/pages/oldpage "));
        QVERIFY(requests[0].contains("\"archived\":true"));
        QVERIFY(requests[1].startsWith("POST /v1/pages "));
        QVERIFY(requests[1].contains("Authorization: Bearer secret"));
        QVERIFY(requests[1].contains("Notion-Version: 2022-06-28"));
        const QJsonObject body = QJsonDocument::fromJson(requests[1].mid(requests[1].indexOf("\r\n\r\n") + 4)).object();
        QCOMPARE(body.value("parent").toObject().value("page_id").toString(), QString("parentid"));
        const QJsonArray children = body.value("children").toArray();
        QCOMPARE(children.first().toObject().value("type").toString(), QString("bookmark"));
        // Two summary paragraphs, and a long note in pieces of 2000 characters.
        const QJsonObject last = children.last().toObject();
        QCOMPARE(last.value("paragraph").toObject().value("rich_text").toArray().size(), 2);
    }

    void backendShowsTheGraphOfTheLibrary()
    {
        QTemporaryDir dir;
        Store store(dir.path());
        QString error;
        QVERIFY(store.saveDigest(sampleDigest("2026-11-08", "https://example.com/a"), &error));
        QVERIFY(store.saveDigest(sampleDigest("2026-11-09", "https://www.example.com/b/"), &error));
        auto concept_ = [](const QString &name, const QStringList &aliases = {}) {
            return QJsonObject{{"name", name}, {"kind", "vulnerability"}, {"aliases", QJsonArray::fromStringList(aliases)}};
        };
        {
            Database database(dir.path());
            const QStringList pages{"Intro.", "XSS rewrites the DOM."};
            QVERIFY(database.savePage("https://example.com/a", "ok", pages, {}));
            QVERIFY(database.saveConcepts("https://example.com/a", "ok", Extractor::validate(QJsonObject{{"concepts", QJsonArray{
                concept_("Cross-site scripting", {"XSS"}), concept_("DOM")}}}, pages, true), "page", "tiny"));
            QVERIFY(database.saveConcepts("https://example.com/b", "ok", Extractor::validate(QJsonObject{{"concepts", QJsonArray{
                concept_("XSS"), concept_("DOM")}}}, {"XSS and the DOM."}, false), "summary", "tiny"));
            // A writeup no longer in the library takes its concepts with it.
            QVERIFY(database.saveConcepts("https://example.com/gone", "ok", Extractor::validate(QJsonObject{{"concepts", QJsonArray{
                concept_("Clickjacking")}}}, {}, true), "page", "tiny"));
        }
        Backend backend(dir.path(), false);
        backend.setGraphSource("model");
        const QVariantMap graph = backend.graph();
        QCOMPARE(graph.value("total").toInt(), 2);
        QCOMPARE(graph.value("done").toInt(), 2);
        const QVariantList nodes = graph.value("nodes").toList();
        QCOMPARE(nodes.size(), 2);
        for (const auto &node : nodes) {
            QCOMPARE(node.toMap().value("urls").toList().size(), 2);
            QVERIFY(node.toMap().value("x").toDouble() >= 0 && node.toMap().value("x").toDouble() <= 1);
        }
        // Both writeups relate the pair once each.
        const QVariantList edges = graph.value("edges").toList();
        QCOMPARE(edges.size(), 1);
        QCOMPARE(edges.first().toMap().value("writeups").toInt(), 2);
        QCOMPARE(edges.first().toMap().value("weight").toInt(), 2);

        const QVariantMap xss = backend.conceptInfo("cross-site-scripting");
        QCOMPARE(xss.value("aliases").toStringList(), QStringList({"xss"}));
        QCOMPARE(xss.value("models").toStringList(), QStringList({"tiny"}));
        const QVariantList writeups = xss.value("writeups").toList();
        QCOMPARE(writeups.size(), 2);
        QCOMPARE(writeups.first().toMap().value("date").toString(), QString("2026-11-08"));
        const QVariantMap dom = xss.value("neighbours").toList().first().toMap();
        QCOMPARE(dom.value("id").toString(), QString("dom"));
        QCOMPARE(dom.value("weight").toInt(), 2);
        const QVariantList evidence = dom.value("evidence").toList();
        QCOMPARE(evidence.size(), 2);
        QCOMPARE(evidence.first().toMap().value("text").toString(), QString("XSS rewrites the DOM."));
        QCOMPARE(evidence.first().toMap().value("title").toString(), QString("A writeup"));
        QVERIFY(evidence.last().toMap().value("text").toString().isEmpty());
        QVERIFY(backend.conceptInfo("clickjacking").isEmpty());
    }

    void layoutKeepsRelatedConceptsTogether()
    {
        QJsonArray nodes, edges;
        for (int i = 0; i < 8; ++i)
            nodes.append(QJsonObject{{"id", QString::number(i)}});
        // Two groups of four, each related all round, nothing between them.
        for (int group : {0, 4})
            for (int i = group; i < group + 4; ++i)
                for (int j = i + 1; j < group + 4; ++j)
                    edges.append(QJsonObject{{"from", QString::number(i)}, {"to", QString::number(j)}, {"weight", 1}});
        QJsonArray first = nodes, second = nodes;
        Backend::layout(first, edges);
        Backend::layout(second, edges);
        QCOMPARE(first, second);
        auto distance = [&](int a, int b) {
            const QJsonObject p = first[a].toObject(), q = first[b].toObject();
            return std::hypot(p.value("x").toDouble() - q.value("x").toDouble(), p.value("y").toDouble() - q.value("y").toDouble());
        };
        double inside = 0, across = 0;
        for (int i = 0; i < 8; ++i)
            for (int j = i + 1; j < 8; ++j)
                (i / 4 == j / 4 ? inside : across) += distance(i, j);
        QVERIFY2(inside / 12 < across / 16 * 0.6, qPrintable(QString("%1 vs %2").arg(inside / 12).arg(across / 16)));
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
        extractor.spreadMs = 0;
        QTemporaryDir power;
        extractor.powerSupply = power.path();
        extractor.powerCheckMs = 50;
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

        // A batch is spread out: two writeups over 1.6 s start 0.8 s apart.
        extractor.spreadMs = 1600;
        auto other = [](const QString &name) {
            return QVariantMap{{"url", "https://example.com/" + name}, {"title", name}, {"summary", "About " + name + "."}};
        };
        extractor.enqueue({other("one"), other("two")});
        QTRY_COMPARE(extractions.size(), 4);
        release();
        QTRY_COMPARE(extracted.count(), 3);
        QTest::qWait(300);
        QCOMPARE(extractions.size(), 4);
        QTRY_COMPARE(extractions.size(), 5);
        release();
        QTRY_COMPARE(extracted.count(), 4);

        // On battery nothing starts, and a running extraction stops and waits.
        extractor.spreadMs = 0;
        auto supply = [&](const QString &name, const QByteArray &type, const QByteArray &online) {
            QDir(power.path()).mkpath(name);
            write(power.path() + "/" + name + "/type", type + "\n");
            write(power.path() + "/" + name + "/online", online + "\n");
        };
        supply("BAT0", "Battery", "0");
        supply("AC", "Mains", "1");
        extractor.enqueue({other("three")});
        QTRY_COMPARE(extractions.size(), 6);
        supply("AC", "Mains", "0");
        QTRY_VERIFY(!extractor.busy());
        QTest::qWait(200);
        QCOMPARE(extractions.size(), 6);
        QCOMPARE(extracted.count(), 4);
        supply("AC", "Mains", "1");
        QTRY_COMPARE(extractions.size(), 7);
        release();
        QTRY_COMPARE(extracted.count(), 5);
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
