#include "references.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

// Words as bin/references reads them: apostrophes dropped ("isn't" is one
// word), anything else that isn't a letter or digit splits.
static QStringList words(const QString &text) {
    static const QRegularExpression apostrophe("['’]"), split("[^\\w]+");
    return QString(text).remove(apostrophe).split(split, Qt::SkipEmptyParts);
}

// XSS, AiTM: capitals after the first letter.
static bool abbreviation(const QString &word) {
    for (int i = 1; i < word.size(); ++i)
        if (word.at(i).isUpper())
            return true;
    return false;
}

const References &References::instance() {
    static const References references([] {
        QFile file(":/references/references.json");
        return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
    }());
    return references;
}

References::References(const QJsonObject &data) : m_notice(data.value("notice").toString()) {
    for (const auto &value : data.value("entries").toArray()) {
        const QJsonObject entry = value.toObject();
        const QString id = entry.value("id").toString();
        m_entries.insert(id, entry);
        for (const auto &term : entry.value("terms").toArray()) {
            const QStringList list = words(term.toString());
            if (list.isEmpty())
                continue;
            if (list.size() == 1 && abbreviation(list.first()))
                m_exact.insert(list.first(), id);
            else
                m_terms.insert(list.join(' ').toLower(), id);
            m_longest = qMax(m_longest, int(list.size()));
        }
    }
}

QList<References::Hit> References::find(const QStringList &texts, QList<QStringList> *perText) const {
    QHash<QString, Hit> hits;
    for (int t = 0; t < texts.size(); ++t) {
        const QStringList original = words(texts.at(t));
        QStringList lower;
        for (const auto &word : original)
            lower << word.toLower();
        QSet<QString> named;
        for (int i = 0; i < lower.size();) {
            int taken = 1;
            QString id;
            for (int n = qMin(m_longest, int(lower.size()) - i); n >= 1 && id.isEmpty(); --n) {
                id = n == 1 && m_exact.contains(original.at(i)) ? m_exact.value(original.at(i))
                                                                 : m_terms.value(lower.mid(i, n).join(' '));
                taken = n;
            }
            if (!id.isEmpty())
                named.insert(id);
            i += id.isEmpty() ? 1 : taken;
        }
        if (perText)
            *perText << QStringList(named.begin(), named.end());
        for (const auto &id : named) {
            Hit &hit = hits[id];
            hit.id = id;
            if (hit.first < 0)
                hit.first = t;
            ++hit.count;
        }
    }
    QList<Hit> list = hits.values();
    std::sort(list.begin(), list.end(), [](const Hit &a, const Hit &b) {
        return a.count != b.count ? a.count > b.count : a.id < b.id;
    });
    return list;
}
