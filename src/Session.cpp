#include "Session.h"

#include <QJsonDocument>
#include <QSettings>

namespace tekst
{

namespace
{

const QString windowsKey{QStringLiteral("session/windows")};

} // namespace

Session::Session(QObject *parent) : QObject(parent) {}

QJsonArray Session::windows()
{
    return QJsonDocument::fromJson(QSettings{}.value(windowsKey).toByteArray()).array();
}

void Session::save(const QJsonArray &windows)
{
    QSettings{}.setValue(windowsKey, QJsonDocument{windows}.toJson(QJsonDocument::Compact));
}

} // namespace tekst
