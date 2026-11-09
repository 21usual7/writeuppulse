#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

class Progress;

// The digests on disk: one <date>.json and <date>.md per day, plus
// status.json for the last run, all in ~/.local/share/writeuppulse.
class Store {
public:
    explicit Store(const QString &directory = {});

    QString directory() const { return m_directory; }
    QString lockPath() const { return m_directory + "/fetch.lock"; }

    QStringList dates() const;
    QJsonObject digest(const QString &date) const;
    bool saveDigest(const QJsonObject &digest, QString *error) const;
    QJsonObject status() const;
    bool saveStatus(const QJsonObject &status) const;
    QStringList recentUrls(int days) const;

    // The fixed directions the agent sorts writeups into, grouped by topic,
    // so filters and preferences never meet a category they don't know.
    static QStringList topics();
    static QStringList categories(const QString &topic = {});
    static QString topicOf(const QString &category);
    static QStringList levels();

    static QJsonObject normalize(const QJsonObject &output, const QString &date, QString *error);
    // With progress, the Markdown also says what the user read, did in a lab,
    // tagged, and checked off.
    static QString markdown(const QJsonObject &digest, const Progress *progress = nullptr);
    // One day, or the seven days ending on it, oldest first.
    QString exportMarkdown(const QString &date, bool week, const Progress *progress = nullptr) const;
    // Copies every day into the public site's data directory, plus an index.
    bool publish(const QString &directory, QString *error) const;

private:
    QString m_directory;
};
