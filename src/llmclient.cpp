#include "llmclient.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QSettings>

LlmClient::LlmClient(const QUrl &host, QObject *parent)
    : QObject(parent),
      m_host(!host.isEmpty() ? host
             : !qEnvironmentVariableIsEmpty("WRITEUPPULSE_OLLAMA") ? QUrl(qEnvironmentVariable("WRITEUPPULSE_OLLAMA"))
             : QUrl("http://127.0.0.1:11434")),
      m_model(QSettings("writeuppulse", "writeuppulse").value("tutorModel", "qwen3:8b").toString()) {
    check();
}

void LlmClient::setModel(const QString &model) {
    const QString name = model.trimmed();
    if (name.isEmpty() || name == m_model)
        return;
    m_model = name;
    QSettings("writeuppulse", "writeuppulse").setValue("tutorModel", name);
    emit statusChanged();
    check();
}

// Asks Ollama which models are pulled; "qwen3" also matches "qwen3:latest".
void LlmClient::check() {
    QNetworkRequest request(m_host.resolved(QUrl("/api/tags")));
    request.setTransferTimeout(3000);
    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        QString status = onAcPower() ? "missing" : "battery";
        if (reply->error() == QNetworkReply::NoError) {
            status = "noModel";
            const QString wanted = m_model.contains(':') ? m_model : m_model + ":latest";
            for (const auto &model : QJsonDocument::fromJson(reply->readAll())["models"].toArray())
                if (model.toObject().value("name").toString() == wanted || model.toObject().value("model").toString() == wanted)
                    status = "ready";
        }
        if (status != m_status) {
            m_status = status;
            emit statusChanged();
        }
    });
}

QNetworkReply *LlmClient::chat(const QJsonArray &messages, double temperature, const QJsonObject &format,
                               bool background, int timeoutMs, int maxTokens) {
    QNetworkRequest request(m_host.resolved(QUrl("/api/chat")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(timeoutMs);
    // One context size for every chat, so Ollama doesn't reload the model
    // when the tutor and the extractor take turns.
    QJsonObject options{{"temperature", temperature}, {"num_ctx", 8192}};
    if (maxTokens > 0)
        options.insert("num_predict", maxTokens);
    QJsonObject body{{"model", m_model}, {"messages", messages}, {"stream", true}, {"think", false}, {"options", options}};
    if (!format.isEmpty())
        body.insert("format", format);
    QNetworkReply *reply = m_network.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    if (!background) {
        ++m_foreground;
        emit foregroundStarted();
        connect(reply, &QNetworkReply::finished, this, [this] {
            --m_foreground;
            emit idleChanged();
        });
    }
    return reply;
}

// Ollama streams one JSON object per line, each with the next piece of text.
QString LlmClient::take(QByteArray &buffer, QString *error) {
    QString text;
    qsizetype end;
    while ((end = buffer.indexOf('\n')) >= 0) {
        const QJsonObject chunk = QJsonDocument::fromJson(buffer.left(end)).object();
        buffer.remove(0, end + 1);
        if (chunk.contains("error") && error)
            *error = chunk.value("error").toString();
        text += chunk.value("message").toObject().value("content").toString();
    }
    return text;
}

static QByteArray readLine(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readLine().trimmed() : QByteArray();
}

bool LlmClient::onAcPower(const QString &directory) {
    bool battery = false;
    for (const auto &name : QDir(directory).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QByteArray type = readLine(directory + "/" + name + "/type");
        if ((type == "Mains" || type == "USB") && readLine(directory + "/" + name + "/online") == "1")
            return true;
        battery = battery || type == "Battery";
    }
    return !battery;
}
