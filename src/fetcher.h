#pragma once

#include <QString>
#include <QStringList>

#include "store.h"

// Runs Claude Code headless to find and summarize the day's writeups. The
// agent only gets web search and fetch, no shell and no file access; it
// answers with JSON on stdout, and this class checks and saves it.
class Fetcher {
public:
    explicit Fetcher(const Store &store, const QString &claude = {});

    QString claude() const { return m_claude; }
    QStringList arguments(const QString &date) const;
    QString prompt(const QString &date) const;
    bool run(QString *error, int *count = nullptr);

    static QString findClaude();
    static QString language(const QString &omarchyLanguageFile = {});
    static void setLanguage(const QString &language);
    static QByteArray schema();

    int timeoutMs = 30 * 60 * 1000;

private:
    bool fetch(const QString &date, QJsonObject *digest, QString *error) const;

    Store m_store;
    QString m_claude;
};
