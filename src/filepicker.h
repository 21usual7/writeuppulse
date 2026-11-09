#pragma once

#include <QObject>
#include <QUrl>

// Where the user saves an export. Abstract so tests can answer without a
// dialog; the app uses PortalFilePicker. Follows Omacut's src/filepicker.h
// (MIT, David Heinemeier Hansson).
class FilePicker : public QObject {
    Q_OBJECT

public:
    explicit FilePicker(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~FilePicker() = default;

    virtual void saveMarkdown(const QUrl &suggestedUrl) = 0;

signals:
    void saveSelected(const QUrl &url);
    void failed(const QString &message);
};
