#pragma once

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

class QNetworkAccessManager;
class QNetworkReply;

// Takes the reader's notes out of the app: as Markdown files into a folder
// (an Obsidian vault, or any notes app that reads Markdown), and as pages in
// Notion through its API. Nothing leaves the machine unless the reader set
// one of them up and saves or sends.
class Notes : public QObject {
    Q_OBJECT

public:
    explicit Notes(QObject *parent = nullptr);

    // The writeup as a note: YAML front matter (title, url, date, category,
    // level, tags), the summary, takeaways, the catalog terms as [[wiki
    // links]], and the reader's own text under "My notes".
    static QString markdown(const QVariantMap &item, const QString &note, const QStringList &terms, const QStringList &tags);
    // <folder>/WriteupPulse/<date> <title>.md. A file there that was changed
    // since the app last wrote it is the reader's and is left alone: false,
    // with the reason in error.
    static bool writeToVault(const QString &folder, const QVariantMap &item, const QString &note, const QStringList &terms,
                             const QStringList &tags, QString *path, QString *error);
    static QString fileName(const QVariantMap &item);

    // A Notion page ID from an ID or a page link, or empty.
    static QString notionId(const QString &text);
    // Creates a page under parent with the same content; the old page, when
    // there is one, is moved to the trash first. Ends with sent().
    void sendToNotion(const QVariantMap &item, const QString &note, const QStringList &terms, const QString &token,
                      const QString &parent, const QString &oldPage);
    bool busy() const { return m_reply != nullptr; }
    // WRITEUPPULSE_NOTION, for tests; https://api.notion.com otherwise.
    QUrl api;

signals:
    // page is the new page's ID when ok, message the error otherwise.
    void sent(const QString &url, bool ok, const QString &page, const QString &message);

private:
    void create();

    QNetworkAccessManager *m_network;
    QPointer<QNetworkReply> m_reply;
    QVariantMap m_item;
    QByteArray m_body;
    QString m_token;
};
