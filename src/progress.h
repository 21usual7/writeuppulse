#pragma once

#include <QDate>
#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <functional>

class Database;

// What the user did with each writeup, keyed by its canonical URL so it
// survives a day being fetched again and a link with a different tail: read,
// worked through in a lab, starred with tags, which practice ideas are done,
// and which self-check questions they knew. Kept in the Database, which only
// the window writes.
class Progress {
public:
    explicit Progress(Database &database);

    void load();
    QJsonObject entry(const QString &url) const;

    bool setRead(const QString &url, const QString &category, bool read, const QDate &day = QDate::currentDate());
    bool setLab(const QString &url, const QString &category, bool lab, const QDate &day = QDate::currentDate());
    bool setFavorite(const QString &url, const QString &category, bool favorite);
    bool setTags(const QString &url, const QStringList &tags);
    bool setPracticeDone(const QString &url, int index, bool done);
    // result is "knew", "missed", or empty to take the answer back.
    bool setCheck(const QString &url, const QString &category, const QString &question, const QString &result);

    // Days in a row with at least one writeup marked read, up to today; a day
    // not yet read today keeps yesterday's streak alive.
    int streak(const QDate &today = QDate::currentDate()) const;
    // categories (canonical URL → category) has each writeup's category as
    // its day has it now; the one saved with the progress is only a fallback.
    QJsonObject stats(const QHash<QString, QString> &categories = {}) const;
    QStringList favorites() const;
    QStringList tags() const;

private:
    bool update(const QString &url, const QString &category, const std::function<void(QJsonObject &)> &change);

    Database &m_database;
    QJsonObject m_items;
};
