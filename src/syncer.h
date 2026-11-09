#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>

#include "store.h"

// Downloads the writeups published on the WriteupPulse site, for readers who
// don't run an agent of their own. What comes from the network is checked
// like the agent's own output before it is saved.
class Syncer {
public:
    explicit Syncer(const Store &store, const QUrl &feed = {});

    QUrl feed() const { return m_feed; }
    bool run(QString *error, int *count = nullptr);

    // "feed" (the default) downloads from the site; "claude" runs the agent.
    static QString source();
    static void setSource(const QString &source);

    int days = 30;
    int timeoutMs = 30 * 1000;

private:
    bool get(const QString &name, qint64 limit, QByteArray *data, QString *error) const;

    Store m_store;
    QUrl m_feed;
};
