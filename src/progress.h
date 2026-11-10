#pragma once

#include <QDate>
#include <QHash>
#include <QJsonObject>
#include <QVariantMap>
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

    // Days in a row with at least one writeup marked read or a self-check
    // answered, up to today; a day not yet done keeps yesterday's streak alive.
    int streak(const QDate &today = QDate::currentDate()) const;
    // categories (canonical URL → category) has each writeup's category as
    // its day has it now; the one saved with the progress is only a fallback.
    QJsonObject stats(const QHash<QString, QString> &categories = {}) const;
    QStringList favorites() const;

    // Today's review, Leitner style: every self-check question answered
    // before is in a box, 1 to 5; "knew" moves it up, "missed" back to 1, and
    // it comes back 1, 3, 7, 16, or 35 days after the last answer by box.
    // Questions never answered join, newPerDay a day, from writeups marked
    // read first, then from earlier days, newest first. library holds the
    // window's items (url, date, title, questions...). Returns {cards: [item
    // fields + question, answer, box, fresh], reviewedToday, nextDue}.
    QVariantMap review(const QVariantList &library, const QDate &today = QDate::currentDate(), int newPerDay = 5) const;
    QStringList tags() const;

private:
    bool update(const QString &url, const QString &category, const std::function<void(QJsonObject &)> &change);

    Database &m_database;
    QJsonObject m_items;
};
