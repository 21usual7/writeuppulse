#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

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
    static QString markdown(const QJsonObject &digest);

private:
    QString m_directory;
};
