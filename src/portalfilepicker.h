#pragma once

#include <QVariantMap>

#include "filepicker.h"

class QDBusPendingCallWatcher;

// The save dialog through xdg-desktop-portal, so it is the desktop's own.
// Adapted from Omacut's src/portalfilepicker.{h,cpp} (MIT, David Heinemeier
// Hansson), keeping only SaveFile.
class PortalFilePicker : public FilePicker {
    Q_OBJECT

public:
    explicit PortalFilePicker(QObject *parent = nullptr);

    void saveMarkdown(const QUrl &suggestedUrl) override;

private slots:
    void handleResponse(uint response, const QVariantMap &results);

private:
    bool connectToRequestPath(const QString &path);
    void clearPending();
    // Best-effort Close of the pending portal request, if there is one.
    void closePendingRequest();

    QString m_pendingPath;
    bool m_pending = false;
    // Bumped per request, so a late reply for a replaced request is ignored.
    int m_pendingGeneration = 0;
    QDBusPendingCallWatcher *m_pendingWatcher = nullptr;
};
