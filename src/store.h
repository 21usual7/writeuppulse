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

    // One form for the many links to the same page (http or https, www,
    // tracking parameters, fragment, trailing slash), used to key progress.
    static QString canonicalUrl(const QString &url);
    static QJsonObject normalize(const QJsonObject &output, const QString &date, QString *error);
    // With progress, the Markdown also says what the user read, did in a lab,
    // tagged, and checked off.
    static QString markdown(const QJsonObject &digest, const Progress *progress = nullptr);
    // One day, or the seven days ending on it, oldest first.
    QString exportMarkdown(const QString &date, bool week, const Progress *progress = nullptr) const;
    // Copies every day into the public site's data directory, plus an index,
    // the labs (labs.json), and an Atom feed of the newest writeups
    // (feed.xml). Each published writeup carries the catalog entries its own
    // text names ("terms": id, name, kind, text, url), for the site's glossary.
    bool publish(const QString &directory, QString *error) const;
    // The ATT&CK, CWE, CAPEC, and OWASP entries a writeup's title, why,
    // summary, and takeaways name, most named first, at most eight.
    static QJsonArray terms(const QJsonObject &item);
    // The Atom feed publish() writes.
    QByteArray feed() const;

private:
    QString m_directory;
};
