#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>

class QNetworkReply;

// The one place that talks to the local Ollama: which model is pulled, and
// chats, streamed, optionally held to a JSON schema. The Tutor and the
// Extractor share it, so the reader's questions come first: a chat in the
// foreground tells background work to step aside.
class LlmClient : public QObject {
    Q_OBJECT

public:
    explicit LlmClient(const QUrl &host = {}, QObject *parent = nullptr);

    // "missing" (Ollama isn't running), "battery" (not running, and the laptop is on
    // battery, where this machine keeps it off), "noModel" (the model isn't pulled), "ready".
    QString status() const { return m_status; }
    QString model() const { return m_model; }
    void setModel(const QString &model);
    // No foreground chat is running.
    bool idle() const { return m_foreground == 0; }

    void check();
    // Streams /api/chat. With a format, the answer is JSON that follows that
    // schema; a background chat doesn't count against idle(); maxTokens > 0
    // ends the answer there.
    QNetworkReply *chat(const QJsonArray &messages, double temperature, const QJsonObject &format = {},
                        bool background = false, int timeoutMs = 120000, int maxTokens = 0);
    // Takes the complete lines out of a streamed answer and returns their
    // text; an error line sets error.
    static QString take(QByteArray &buffer, QString *error);
    // True with a charger plugged in, or on a machine without a battery.
    static bool onAcPower(const QString &directory = "/sys/class/power_supply");

signals:
    void statusChanged();
    void foregroundStarted();
    void idleChanged();

private:
    QUrl m_host;
    QString m_model;
    QString m_status = "missing";
    QNetworkAccessManager m_network;
    int m_foreground = 0;
};
