#pragma once

#include <QObject>
#include <QUrl>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

namespace kode
{

// What each folder's window had open when it last closed (its documents,
// geometry and splits), reopened with the folder. Kept in QSettings
// (`session/folders`, JSON by folder URL).
class Session : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
public:
    explicit Session(QObject *parent = nullptr);

    // Empty when the folder was never open.
    Q_INVOKABLE [[nodiscard]] static QVariantMap folderState(const QUrl &folder);
    Q_INVOKABLE static void saveFolder(const QUrl &folder, const QVariantMap &state);
};

} // namespace kode
