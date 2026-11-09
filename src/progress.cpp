#include "progress.h"

#include <QJsonArray>
#include <QSet>

#include "database.h"
#include "store.h"

Progress::Progress(Database &database) : m_database(database) {
    load();
}

void Progress::load() {
    m_items = m_database.entries();
}

QJsonObject Progress::entry(const QString &url) const {
    return m_items.value(Store::canonicalUrl(url)).toObject();
}

// Every change is written at once, so nothing the user marked is lost when
// the window closes.
bool Progress::update(const QString &url, const QString &category, const std::function<void(QJsonObject &)> &change) {
    const QString key = Store::canonicalUrl(url);
    if (key.isEmpty())
        return false;
    QJsonObject entry = m_items.value(key).toObject();
    if (!category.isEmpty())
        entry.insert("category", category);
    change(entry);
    // An entry with nothing left in it is dropped rather than kept empty.
    const bool empty = !entry.contains("read") && !entry.contains("lab") && !entry.value("favorite").toBool()
                       && entry.value("tags").toArray().isEmpty() && entry.value("done").toArray().isEmpty()
                       && entry.value("checks").toObject().isEmpty();
    if (empty)
        m_items.remove(key);
    else
        m_items.insert(key, entry);
    // The answers themselves are events in the database, written by setCheck.
    QJsonObject row = entry;
    row.remove("checks");
    return m_database.saveEntry(key, row);
}

bool Progress::setRead(const QString &url, const QString &category, bool read, const QDate &day) {
    return update(url, category, [&](QJsonObject &entry) {
        if (read)
            entry.insert("read", day.toString(Qt::ISODate));
        else
            entry.remove("read");
    });
}

bool Progress::setLab(const QString &url, const QString &category, bool lab, const QDate &day) {
    return update(url, category, [&](QJsonObject &entry) {
        if (lab)
            entry.insert("lab", day.toString(Qt::ISODate));
        else
            entry.remove("lab");
    });
}

bool Progress::setFavorite(const QString &url, const QString &category, bool favorite) {
    return update(url, category, [&](QJsonObject &entry) {
        if (favorite)
            entry.insert("favorite", true);
        else
            entry.remove("favorite");
    });
}

bool Progress::setTags(const QString &url, const QStringList &tags) {
    QStringList clean;
    for (const auto &tag : tags)
        if (!tag.trimmed().isEmpty() && !clean.contains(tag.trimmed(), Qt::CaseInsensitive))
            clean << tag.trimmed();
    return update(url, {}, [&](QJsonObject &entry) {
        if (clean.isEmpty())
            entry.remove("tags");
        else
            entry.insert("tags", QJsonArray::fromStringList(clean));
    });
}

bool Progress::setPracticeDone(const QString &url, int index, bool done) {
    return update(url, {}, [&](QJsonObject &entry) {
        QJsonArray list;
        for (const auto &value : entry.value("done").toArray())
            if (value.toInt() != index)
                list.append(value);
        if (done)
            list.append(index);
        if (list.isEmpty())
            entry.remove("done");
        else
            entry.insert("done", list);
    });
}

bool Progress::setCheck(const QString &url, const QString &category, const QString &question, const QString &result) {
    if (question.isEmpty() || (!result.isEmpty() && result != "knew" && result != "missed")
        || !m_database.addCheck(Store::canonicalUrl(url), question, result))
        return false;
    return update(url, category, [&](QJsonObject &entry) {
        QJsonObject checks = entry.value("checks").toObject();
        if (result.isEmpty())
            checks.remove(question);
        else
            checks.insert(question, result);
        if (checks.isEmpty())
            entry.remove("checks");
        else
            entry.insert("checks", checks);
    });
}

int Progress::streak(const QDate &today) const {
    QSet<QDate> days;
    for (const auto &value : m_items)
        if (value.toObject().contains("read"))
            days.insert(QDate::fromString(value.toObject().value("read").toString(), Qt::ISODate));
    QDate day = days.contains(today) ? today : today.addDays(-1);
    int count = 0;
    while (days.contains(day)) {
        ++count;
        day = day.addDays(-1);
    }
    return count;
}

QJsonObject Progress::stats(const QHash<QString, QString> &categories) const {
    int read = 0, lab = 0, favorites = 0, checked = 0, knew = 0;
    QJsonObject byCategory;
    for (auto it = m_items.begin(); it != m_items.end(); ++it) {
        const QJsonObject entry = it.value().toObject();
        if (entry.contains("read")) {
            ++read;
            const QString category = categories.value(it.key(), entry.value("category").toString("Other"));
            byCategory.insert(category, byCategory.value(category).toInt() + 1);
        }
        lab += entry.contains("lab");
        favorites += entry.value("favorite").toBool();
        for (const auto &result : entry.value("checks").toObject()) {
            ++checked;
            knew += result.toString() == "knew";
        }
    }
    return {{"read", read}, {"lab", lab}, {"favorites", favorites}, {"checked", checked}, {"knew", knew},
            {"byCategory", byCategory}};
}

QStringList Progress::favorites() const {
    QStringList urls;
    for (auto it = m_items.begin(); it != m_items.end(); ++it)
        if (it.value().toObject().value("favorite").toBool())
            urls << it.key();
    return urls;
}

QStringList Progress::tags() const {
    QStringList all;
    for (const auto &value : m_items)
        for (const auto &tag : value.toObject().value("tags").toArray())
            if (!all.contains(tag.toString(), Qt::CaseInsensitive))
                all << tag.toString();
    all.sort(Qt::CaseInsensitive);
    return all;
}
