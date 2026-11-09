#include "progress.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>

Progress::Progress(const QString &directory) : m_path(directory + "/progress.json") {
    load();
}

void Progress::load() {
    QFile file(m_path);
    m_items = file.open(QIODevice::ReadOnly)
        ? QJsonDocument::fromJson(file.readAll()).object().value("items").toObject() : QJsonObject();
}

// Every change is written at once, atomically, so nothing the user marked is
// lost when the window closes.
bool Progress::update(const QString &url, const QString &category, const std::function<void(QJsonObject &)> &change) {
    if (url.isEmpty())
        return false;
    QJsonObject entry = m_items.value(url).toObject();
    if (!category.isEmpty())
        entry.insert("category", category);
    change(entry);
    // An entry with nothing left in it is dropped rather than kept empty.
    const bool empty = !entry.contains("read") && !entry.contains("lab") && !entry.value("favorite").toBool()
                       && entry.value("tags").toArray().isEmpty() && entry.value("done").toArray().isEmpty();
    if (empty)
        m_items.remove(url);
    else
        m_items.insert(url, entry);

    QDir().mkpath(QFileInfo(m_path).absolutePath());
    const QByteArray data = QJsonDocument(QJsonObject{{"items", m_items}}).toJson(QJsonDocument::Indented);
    QSaveFile file(m_path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
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

QJsonObject Progress::stats() const {
    int read = 0, lab = 0, favorites = 0;
    QJsonObject byCategory;
    for (const auto &value : m_items) {
        const QJsonObject entry = value.toObject();
        if (entry.contains("read")) {
            ++read;
            const QString category = entry.value("category").toString("Other");
            byCategory.insert(category, byCategory.value(category).toInt() + 1);
        }
        lab += entry.contains("lab");
        favorites += entry.value("favorite").toBool();
    }
    return {{"read", read}, {"lab", lab}, {"favorites", favorites}, {"byCategory", byCategory}};
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
