#include "backend.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QProcess>
#include <QStandardPaths>

#include "fetcher.h"

Backend::Backend(const QString &directory, bool systemd, QObject *parent)
    : QObject(parent), m_store(directory), m_systemd(systemd) {
    QDir().mkpath(m_store.directory());
    m_agentMissing = Fetcher::findClaude().isEmpty();

    // A fetch writes its files and lock into the data directory, so watching
    // that one directory follows timer runs and manual ones alike.
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(150);
    connect(&m_debounce, &QTimer::timeout, this, &Backend::reload);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_debounce.start(); });
    m_watcher.addPath(m_store.directory());

    // Covers the moment between asking systemd for a fetch and the fetch
    // taking its lock, so the button doesn't flicker back.
    m_starting.setSingleShot(true);
    m_starting.setInterval(20000);
    connect(&m_starting, &QTimer::timeout, this, &Backend::statusChanged);

    if (m_systemd)
        readTimer();
    reload();
}

void Backend::setDate(const QString &date) {
    if (date == m_date || !m_dates.contains(date))
        return;
    m_date = date;
    m_digestStamp.clear();
    reload();
}

void Backend::step(int delta) {
    const int index = m_dates.indexOf(m_date) + delta;
    if (index >= 0 && index < m_dates.size())
        setDate(m_dates.at(index));
}

void Backend::setProblem(const QString &problem) {
    if (problem != m_problem) {
        m_problem = problem;
        emit problemChanged();
    }
}

static QByteArray stamp(const QString &path) {
    const QFileInfo info(path);
    return info.exists() ? QByteArray::number(info.lastModified().toMSecsSinceEpoch()) + ":" + QByteArray::number(info.size())
                         : QByteArray();
}

void Backend::reload() {
    // Deleting and recreating the directory drops the watch; re-arm it every time.
    if (!m_watcher.directories().contains(m_store.directory()))
        m_watcher.addPath(m_store.directory());

    const QStringList dates = m_store.dates();
    if (dates != m_dates) {
        // Stay on the day the user picked, but jump to a new day as it lands.
        const bool follow = m_date.isEmpty() || (!m_dates.isEmpty() && m_date == m_dates.first());
        m_dates = dates;
        emit datesChanged();
        if (follow || !m_dates.contains(m_date)) {
            m_date = m_dates.value(0);
            m_digestStamp.clear();
        }
    }

    const QByteArray digestStamp = m_date.toUtf8() + "@" + stamp(m_store.directory() + "/" + m_date + ".json");
    if (digestStamp != m_digestStamp) {
        m_digestStamp = digestStamp;
        m_items = m_date.isEmpty() ? QVariantList() : m_store.digest(m_date).value("items").toArray().toVariantList();
        emit digestChanged();
    }

    // QLockFile removes a lock left by a dead process, but only when it tries
    // to take it; reading the owner's pid doesn't touch the directory, which
    // would wake this watcher again.
    qint64 pid = 0;
    QString host, app;
    QLockFile probe(m_store.lockPath());
    const bool held = QFile::exists(m_store.lockPath()) && probe.getLockInfo(&pid, &host, &app)
                      && QFileInfo::exists(QString("/proc/%1").arg(pid));
    const QByteArray statusStamp = stamp(m_store.directory() + "/status.json");
    const bool statusMoved = statusStamp != m_statusStamp;
    if (held || statusMoved)
        m_starting.stop();
    if (held != m_lockHeld || statusMoved) {
        m_lockHeld = held;
        m_statusStamp = statusStamp;
        const QJsonObject status = m_store.status();
        m_lastRun = QDateTime::fromString(status.value("lastRun").toString(), Qt::ISODate);
        m_lastOk = status.value("ok").toBool(true);
        m_lastMessage = status.value("message").toString();
        emit statusChanged();
    }
}

bool Backend::systemctl(const QStringList &arguments, QString *output) {
    QProcess process;
    process.start("systemctl", QStringList{"--user"} + arguments);
    if (!process.waitForStarted(3000) || !process.waitForFinished(10000))
        return false;
    if (output)
        *output = QString::fromUtf8(process.readAllStandardOutput() + process.readAllStandardError()).trimmed();
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

void Backend::readTimer() {
    QString state;
    systemctl({"is-enabled", "writeuppulse.timer"}, &state);
    const bool available = !state.isEmpty() && state != "not-found" && !state.contains("No such file");
    const bool enabled = state == "enabled";
    if (available != m_timerAvailable || enabled != m_timerEnabled) {
        m_timerAvailable = available;
        m_timerEnabled = enabled;
        emit timerChanged();
    }
}

void Backend::refresh() {
    if (running())
        return;
    if (m_agentMissing) {
        setProblem(tr("Claude Code isn't installed, so there is no agent to fetch writeups. Install it and sign in with `claude`."));
        return;
    }
    setProblem({});

    // Through systemd when the package is installed, so the fetch outlives
    // the window and lands in the journal; straight from this binary otherwise.
    if (!(m_systemd && systemctl({"start", "--no-block", "writeuppulse.service"}))
        && !QProcess::startDetached(QCoreApplication::applicationFilePath(), {"fetch"})) {
        setProblem(tr("Could not start a fetch."));
        return;
    }
    m_starting.start();
    emit statusChanged();
}

void Backend::setTimerEnabled(bool enabled) {
    QString output;
    if (!systemctl({enabled ? "enable" : "disable", "--now", "writeuppulse.timer"}, &output))
        setProblem(tr("Could not change the daily timer: %1").arg(output));
    else
        setProblem({});
    readTimer();
}
