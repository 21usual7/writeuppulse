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
#include "fetcher.h"
#include "progress.h"
#include "store.h"
#include "theme.h"
#include "translator.h"

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
            Progress progress(dir.path());
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
        Progress progress(dir.path());
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

    void themeFallsBack()
    {
        QTemporaryDir dir;
        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#FFD60A"));
        QCOMPARE(theme.accentForeground(), QString("black"));
    }

    void themeReadsAccent()
    {
        QTemporaryDir dir;
        QDir(dir.path()).mkpath("theme");
        QFile colors(dir.path() + "/theme/colors.toml");
        QVERIFY(colors.open(QIODevice::WriteOnly));
        colors.write("accent = \"#112233\"\n");
        colors.close();

        Theme theme(dir.path());
        QCOMPARE(theme.accent(), QString("#112233"));
        QCOMPARE(theme.accentForeground(), QString("white"));
    }

    void themeFollowsHyprlandRounding()
    {
        QTemporaryDir dir;
        const QString response = dir.filePath("response.json"), command = dir.filePath("hyprctl");
        auto write = [](const QString &path, const QByteArray &data) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
        };
        QVERIFY(write(command, "#!/bin/sh\ncat '" + response.toUtf8() + "'\n"));
        QVERIFY(QFile::setPermissions(command, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QVERIFY(write(response, "{\"int\":0}"));

        Theme theme(dir.path(), nullptr, command);
        QCOMPARE(theme.radius(), 0);
        QVERIFY(write(response, "{\"int\":6}"));
        QTRY_COMPARE(theme.radius(), 6);
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
            {"why", "Because."}, {"summary", "Summary."}, {"takeaways", QJsonArray{"First lesson"}}, {"tags", QJsonArray{"security"}}}}}};
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
