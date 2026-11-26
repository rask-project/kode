#include "Session.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

namespace kode
{

namespace
{

const QString foldersKey{QStringLiteral("session/folders")};
// The app-wide session of earlier versions.
const QString oldStateKey{QStringLiteral("session/state")};

QJsonObject folders(const QSettings &settings)
{
    return QJsonDocument::fromJson(settings.value(foldersKey).toByteArray()).object();
}

} // namespace

Session::Session(QObject *parent) : QObject(parent) {}

QVariantMap Session::folderState(const QUrl &folder)
{
    return folders(QSettings{}).value(folder.toString()).toObject().toVariantMap();
}

void Session::saveFolder(const QUrl &folder, const QVariantMap &state)
{
    QSettings settings;
    QJsonObject all{folders(settings)};
    all.insert(folder.toString(), QJsonObject::fromVariantMap(state));
    settings.setValue(foldersKey, QJsonDocument{all}.toJson(QJsonDocument::Compact));
    settings.remove(oldStateKey);
}

} // namespace kode
