#pragma once

#include <QHash>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

class QNetworkReply;

// A local model, through Ollama, that answers questions about the open
// writeup and reviews what the reader did for a practice idea. Claude still
// finds and summarizes; this only teaches, on the reader's machine, with no
// tools. Conversations live as long as the window.
class Tutor : public QObject {
    Q_OBJECT
    // "missing" (Ollama isn't running), "noModel" (the model isn't pulled), "ready".
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString model READ model WRITE setModel NOTIFY statusChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY messagesChanged)
    Q_PROPERTY(QVariantList messages READ messages NOTIFY messagesChanged)
    Q_PROPERTY(int practice READ practice NOTIFY messagesChanged)
    Q_PROPERTY(QString error READ error NOTIFY messagesChanged)

public:
    explicit Tutor(const QUrl &host = {}, QObject *parent = nullptr);

    QString status() const { return m_status; }
    QString model() const { return m_model; }
    void setModel(const QString &model);
    bool busy() const { return m_reply != nullptr; }
    QVariantList messages() const;
    int practice() const { return m_practice; }
    QString error() const { return m_error; }

    Q_INVOKABLE void check();
    // Switches to the conversation about this writeup, or about one of its
    // practice ideas when practice >= 0.
    Q_INVOKABLE void open(const QVariantMap &item, int practice = -1);
    Q_INVOKABLE void ask(const QString &text);
    Q_INVOKABLE void stop();

    QString systemPrompt() const;
    // The app teaches without code, so fenced blocks and a model's thinking are cut.
    static QString clean(const QString &text);

signals:
    void statusChanged();
    void messagesChanged();

private:
    QString key() const;
    void read();
    void finish();

    QUrl m_host;
    QString m_model;
    QString m_status = "missing";
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QString m_replyKey;
    QByteArray m_buffer;
    QVariantMap m_item;
    int m_practice = -1;
    QHash<QString, QJsonArray> m_conversations;
    QString m_error;
};
