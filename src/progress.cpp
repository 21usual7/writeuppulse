#include "progress.h"

#include <QDateTime>
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
    // A day spent on review counts as much as one spent reading.
    for (const auto &value : m_database.checks())
        if (!value.toObject().value("result").toString().isEmpty())
            days.insert(QDateTime::fromString(value.toObject().value("at").toString(), Qt::ISODate).date());
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

QVariantMap Progress::review(const QVariantList &library, const QDate &today, int newPerDay) const {
    static const int intervals[] = {0, 1, 3, 7, 16, 35};
    struct State { int box = 0; QDate last, first; };
    // Each answer pushes a state; taking it back pops one.
    QHash<QString, QList<State>> history;
    QSet<QString> answeredToday;
    for (const auto &value : m_database.checks()) {
        const QJsonObject check = value.toObject();
        const QString key = check.value("url").toString() + "\n" + check.value("question").toString();
        QList<State> &states = history[key];
        const QString result = check.value("result").toString();
        const QDate at = QDateTime::fromString(check.value("at").toString(), Qt::ISODate).date();
        if (result.isEmpty()) {
            if (!states.isEmpty())
                states.removeLast();
            continue;
        }
        State state = states.isEmpty() ? State() : states.last();
        state.box = result == "knew" ? qMin(5, state.box + 1) : 1;
        state.last = at;
        if (!state.first.isValid())
            state.first = at;
        states << state;
        if (at == today)
            answeredToday.insert(key);
    }

    QList<QPair<qint64, QVariantMap>> due;
    QList<QVariantMap> fresh, freshRead;
    int introduced = 0;
    QDate nextDue;
    QSet<QString> seen;
    for (const auto &value : library) {
        const QVariantMap item = value.toMap();
        const QString url = Store::canonicalUrl(item.value("url").toString());
        const bool read = entry(url).contains("read");
        for (const auto &q : item.value("questions").toList()) {
            const QString question = q.toMap().value("question").toString();
            const QString key = url + "\n" + question;
            if (question.isEmpty() || q.toMap().value("answer").toString().isEmpty() || seen.contains(key))
                continue;
            seen.insert(key);
            QVariantMap card = item;
            card.insert("question", question);
            card.insert("answer", q.toMap().value("answer"));
            const QList<State> states = history.value(key);
            if (states.isEmpty()) {
                card.insert("box", 0);
                card.insert("fresh", true);
                if (read)
                    freshRead << card;
                else if (item.value("date").toString() < today.toString(Qt::ISODate))
                    fresh << card;
                continue;
            }
            const State state = states.last();
            introduced += state.first == today;
            const QDate when = state.last.addDays(intervals[state.box]);
            card.insert("box", state.box);
            card.insert("fresh", false);
            if (when <= today)
                due << qMakePair(when.daysTo(today), card);
            else if (!nextDue.isValid() || when < nextDue)
                nextDue = when;
        }
    }
    std::stable_sort(due.begin(), due.end(), [](const auto &a, const auto &b) {
        return a.first != b.first ? a.first > b.first : a.second.value("box").toInt() < b.second.value("box").toInt();
    });
    // The library is newest first already.
    QVariantList cards;
    for (const auto &pair : due)
        cards << pair.second;
    for (const auto &card : freshRead + fresh)
        if (introduced++ < newPerDay)
            cards << card;
    return {{"cards", cards}, {"reviewedToday", int(answeredToday.size())},
            {"nextDue", nextDue.isValid() ? nextDue.toString(Qt::ISODate) : QString()}};
}
