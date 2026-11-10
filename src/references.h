#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

// MITRE ATT&CK (Enterprise techniques), CWE, CAPEC, and the OWASP Top Ten
// 2025, as bin/references writes them into the resources: what each entry is,
// how the catalogs relate them, and which of them a text names. Nothing is
// downloaded at run time and no model is involved, so this works on every
// install at once.
class References {
public:
    // Read once from :/references/references.json.
    static const References &instance();
    explicit References(const QJsonObject &data);

    bool isEmpty() const { return m_entries.isEmpty(); }
    // {id, kind (technique, weakness, pattern, owasp), name, text, url, terms,
    // related, ...}; empty when unknown.
    QJsonObject entry(const QString &id) const { return m_entries.value(id); }
    QString notice() const { return m_notice; }

    struct Hit {
        QString id;
        // The first text naming it, and how many texts do.
        int first = -1;
        int count = 0;
    };
    // The entries the texts name, by one of their terms or their ID, most
    // named first. A term is matched as whole words, the longest first, so
    // "SQL injection" isn't also "injection"; an abbreviation like XSS or
    // AiTM only in its own capitals.
    // named, when given, gets the entries each text names.
    QList<Hit> find(const QStringList &texts, QList<QStringList> *named = nullptr) const;

private:
    QHash<QString, QJsonObject> m_entries;
    // Lowercased words joined by spaces → entry; abbreviations kept as written.
    QHash<QString, QString> m_terms, m_exact;
    int m_longest = 1;
    QString m_notice;
};
