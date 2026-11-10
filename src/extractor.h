#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QVariantList>

class Database;
class LlmClient;
class QNetworkReply;

// Finds the concepts a writeup teaches with the local model (never Claude),
// relates the ones its passages name together, and keeps them in the
// Database with where they came from: the passage of the saved page, or the
// summary when there is no page, and the model's name. One writeup at a time, only while the model is
// ready, and out of the way of the reader's own questions to the tutor.
//
// The page is untrusted and the model is small, so the answer is held to a
// JSON schema and checked again here before anything is saved.
class Extractor : public QObject {
    Q_OBJECT

public:
    Extractor(Database &database, LlmClient &llm, QObject *parent = nullptr);

    // Queues the writeups (maps with url, title, summary, takeaways) the
    // database wants concepts for, in the given order.
    void enqueue(const QVariantList &items);
    bool busy() const { return m_reply != nullptr; }

    static QStringList kinds();
    static QJsonObject schema();
    // A concept's key: its name lowercased, words joined by dashes.
    static QString id(const QString &name);
    // Keeps named concepts of known kinds (general words out, at most 20)
    // and relates the ones named in the same passage (RELATED_TO, with the
    // first passage, or -1 when not numbered, and the number of passages).
    static QJsonObject validate(const QJsonObject &answer, const QStringList &passages, bool numbered);
    // The prompt, with the page's first passages or the summary; material is
    // set to every passage of the page (or the summary) and source to "page"
    // or "summary".
    QJsonArray messages(const QVariantMap &item, QString *source, QStringList *material) const;

    // While true, a writeup whose page the Archive hasn't tried yet waits for it.
    bool waitForPages = true;
    // How long the queue waits after the reader's last question.
    int pauseMs = 60 * 1000;

signals:
    // Concepts were saved or given up on; url is canonical.
    void extracted(const QString &url);

private:
    void next();
    void finish();

    Database &m_database;
    LlmClient &m_llm;
    QList<QVariantMap> m_queue;
    // Canonical URLs queued or running.
    QSet<QString> m_queued;
    QVariantMap m_current;
    QString m_source;
    QStringList m_material;
    QPointer<QNetworkReply> m_reply;
    QByteArray m_buffer;
    QString m_text, m_error;
    bool m_yielding = false;
    QTimer m_pause;
};
