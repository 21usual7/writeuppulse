#pragma once

#include <QDateTime>
#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>
#include <QVariantList>

#include "store.h"

// What the window shows: the saved days, the selected day's writeups, and
// the state of the last and current fetch. Fetches run outside the window,
// through the systemd user service, so closing the window never stops one.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList dates READ dates NOTIFY datesChanged)
    Q_PROPERTY(QString date READ date WRITE setDate NOTIFY digestChanged)
    Q_PROPERTY(QVariantList items READ items NOTIFY digestChanged)
    Q_PROPERTY(bool running READ running NOTIFY statusChanged)
    Q_PROPERTY(QDateTime lastRun READ lastRun NOTIFY statusChanged)
    Q_PROPERTY(bool lastOk READ lastOk NOTIFY statusChanged)
    Q_PROPERTY(QString lastMessage READ lastMessage NOTIFY statusChanged)
    Q_PROPERTY(bool agentMissing READ agentMissing CONSTANT)
    Q_PROPERTY(bool timerAvailable READ timerAvailable NOTIFY timerChanged)
    Q_PROPERTY(bool timerEnabled READ timerEnabled NOTIFY timerChanged)
    Q_PROPERTY(QString problem READ problem NOTIFY problemChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)

public:
    explicit Backend(const QString &directory = {}, bool systemd = true, QObject *parent = nullptr);

    QStringList dates() const { return m_dates; }
    QString date() const { return m_date; }
    void setDate(const QString &date);
    QVariantList items() const { return m_items; }
    bool running() const { return m_lockHeld || m_starting.isActive(); }
    QDateTime lastRun() const { return m_lastRun; }
    bool lastOk() const { return m_lastOk; }
    QString lastMessage() const { return m_lastMessage; }
    bool agentMissing() const { return m_agentMissing; }
    bool timerAvailable() const { return m_timerAvailable; }
    bool timerEnabled() const { return m_timerEnabled; }
    QString problem() const { return m_problem; }
    QString language() const { return m_language; }
    void setLanguage(const QString &language);

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setTimerEnabled(bool enabled);
    Q_INVOKABLE void olderDay() { step(1); }
    Q_INVOKABLE void newerDay() { step(-1); }

signals:
    void datesChanged();
    void digestChanged();
    void statusChanged();
    void timerChanged();
    void problemChanged();
    void languageChanged();

private:
    void reload();
    void step(int delta);
    void setProblem(const QString &problem);
    bool systemctl(const QStringList &arguments, QString *output = nullptr);
    void readTimer();

    Store m_store;
    bool m_systemd;
    QFileSystemWatcher m_watcher;
    QTimer m_debounce, m_starting;
    QStringList m_dates;
    QString m_date;
    QVariantList m_items;
    QByteArray m_digestStamp, m_statusStamp;
    bool m_lockHeld = false;
    QDateTime m_lastRun;
    bool m_lastOk = true;
    QString m_lastMessage;
    bool m_agentMissing = false;
    bool m_timerAvailable = false;
    bool m_timerEnabled = false;
    QString m_problem;
    QString m_language;
};
