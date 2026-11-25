#include "Session.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

namespace tekst
{

namespace
{

const QString stateKey{QStringLiteral("session/state")};

} // namespace

Session::Session(QObject *parent) : QObject(parent) {}

QVariantMap Session::state()
{
    return QJsonDocument::fromJson(QSettings{}.value(stateKey).toByteArray()).object().toVariantMap();
}

void Session::save(const QVariantMap &state)
{
    QSettings{}.setValue(stateKey, QJsonDocument{QJsonObject::fromVariantMap(state)}.toJson(QJsonDocument::Compact));
}

} // namespace tekst
