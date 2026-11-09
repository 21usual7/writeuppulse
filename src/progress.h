#pragma once

#include <QDate>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <functional>

// What the user did with each writeup, keyed by its URL so it survives a day
// being fetched again: read, worked through in a lab, starred with tags, and
// which practice ideas are done. Lives beside the digests as progress.json,
// which only the window writes.
class Progress {
public:
    explicit Progress(const QString &directory);

    void load();
    QJsonObject entry(const QString &url) const { return m_items.value(url).toObject(); }

    bool setRead(const QString &url, const QString &category, bool read, const QDate &day = QDate::currentDate());
    bool setLab(const QString &url, const QString &category, bool lab, const QDate &day = QDate::currentDate());
    bool setFavorite(const QString &url, const QString &category, bool favorite);
    bool setTags(const QString &url, const QStringList &tags);
    bool setPracticeDone(const QString &url, int index, bool done);

    // Days in a row with at least one writeup marked read, up to today; a day
    // not yet read today keeps yesterday's streak alive.
    int streak(const QDate &today = QDate::currentDate()) const;
    QJsonObject stats() const;
    QStringList favorites() const;
    QStringList tags() const;

private:
    bool update(const QString &url, const QString &category, const std::function<void(QJsonObject &)> &change);

    QString m_path;
    QJsonObject m_items;
};
