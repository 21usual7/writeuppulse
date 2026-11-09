// Commands that need no display, for the systemd timer and for agents.
#include "cli.h"

#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTextStream>

#include "fetcher.h"
#include "progress.h"
#include "store.h"

static const char *usage = R"(Usage: writeuppulse [command]

  open                 Open the window (the default)
  fetch [--json]       Run the agent now and save today's digest
  list [--json]        List the saved days
  show [DATE] [--json] Print a day's digest as Markdown (latest by default)
  export [DATE] [--week] [--output FILE] [--json]
                       Export a day, or the week ending on it, as Markdown
                       with your progress (latest day by default; stdout
                       unless --output)
  publish DIR [--json] Write every day, without private fields, and an
                       index to DIR for the public site
  skill install        Install the WriteupPulse skill for Claude Code
)";

static int fail(const QString &message) {
    QTextStream(stderr) << message << Qt::endl;
    return 1;
}

static void print(const QJsonObject &object) {
    QTextStream(stdout) << QJsonDocument(object).toJson(QJsonDocument::Compact) << Qt::endl;
}

int runCli(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    app.setApplicationName("writeuppulse");
    QStringList args = app.arguments().mid(1);
    const bool json = args.removeAll("--json") > 0;
    const QString command = args.value(0);
    Store store;

    if (command == "fetch") {
        Fetcher fetcher(store);
        QString error;
        int count = 0;
        if (!json)
            QTextStream(stderr) << "Searching and summarizing with Claude, this takes a few minutes…" << Qt::endl;
        if (!fetcher.run(&error, &count))
            return fail(error);
        const QString date = QDate::currentDate().toString(Qt::ISODate);
        if (json)
            print({{"ok", true}, {"date", date}, {"count", count}, {"path", store.directory() + "/" + date + ".md"}});
        else
            QTextStream(stdout) << QString("Saved %1 writeups to %2/%3.md").arg(count).arg(store.directory(), date) << Qt::endl;
        return 0;
    }

    if (command == "list") {
        const QStringList dates = store.dates();
        if (json)
            print({{"directory", store.directory()}, {"dates", QJsonArray::fromStringList(dates)}});
        else
            for (const auto &date : dates)
                QTextStream(stdout) << date << Qt::endl;
        return 0;
    }

    if (command == "show") {
        const QString date = args.value(1, store.dates().value(0));
        const QJsonObject digest = store.digest(date);
        if (digest.isEmpty())
            return fail(date.isEmpty() ? "No digests yet. Run `writeuppulse fetch`." : "No digest for " + date + ".");
        if (json)
            print(digest);
        else
            QTextStream(stdout) << Store::markdown(digest);
        return 0;
    }

    if (command == "export") {
        const bool week = args.removeAll("--week") > 0;
        QString output;
        const int at = args.indexOf("--output");
        if (at >= 0) {
            output = args.value(at + 1);
            args.remove(at, qMin(2, args.size() - at));
            if (output.isEmpty())
                return fail("--output needs a file name.");
        }
        const QString date = args.value(1, store.dates().value(0));
        const Progress progress(store.directory());
        const QString markdown = store.exportMarkdown(date, week, &progress);
        if (markdown.isEmpty())
            return fail(date.isEmpty() ? "No digests yet. Run `writeuppulse fetch`." : "No digest for " + date + ".");
        if (output.isEmpty()) {
            QTextStream(stdout) << markdown;
            return 0;
        }
        QSaveFile file(output);
        const QByteArray data = markdown.toUtf8();
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
            return fail("Could not write " + output);
        if (json)
            print({{"ok", true}, {"path", QFileInfo(output).absoluteFilePath()}});
        else
            QTextStream(stdout) << "Saved " << output << Qt::endl;
        return 0;
    }

    if (command == "publish") {
        const QString directory = args.value(1);
        if (directory.isEmpty())
            return fail("publish needs a directory.");
        QString error;
        if (!store.publish(directory, &error))
            return fail(error);
        const qsizetype days = Store(directory).dates().size();
        if (json)
            print({{"ok", true}, {"path", QFileInfo(directory).absoluteFilePath()}, {"days", days}});
        else
            QTextStream(stdout) << QString("Published %1 days to %2").arg(days).arg(directory) << Qt::endl;
        return 0;
    }

    if (command == "skill" && args.value(1) == "install") {
        const QString directory = QDir::homePath() + "/.claude/skills/writeuppulse";
        QFile skill(":/skill/SKILL.md");
        QSaveFile target(directory + "/SKILL.md");
        if (!QDir().mkpath(directory) || !skill.open(QIODevice::ReadOnly) || !target.open(QIODevice::WriteOnly)
            || target.write(skill.readAll()) < 0 || !target.commit())
            return fail("Could not write " + directory + "/SKILL.md");
        if (json)
            print({{"ok", true}, {"path", directory + "/SKILL.md"}});
        else
            QTextStream(stdout) << "Installed " << directory << "/SKILL.md" << Qt::endl;
        return 0;
    }

    if (command == "help" || command == "--help" || command == "-h") {
        QTextStream(stdout) << usage;
        return 0;
    }
    QTextStream(stderr) << usage;
    return 1;
}
