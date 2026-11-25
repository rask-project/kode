#pragma once

#include <QJsonArray>
#include <QObject>
#include <QtQmlIntegration/qqmlintegration.h>

namespace tekst
{

// The windows open when the app last quit (their geometry, tabs and files),
// reopened at launch. Kept in QSettings (`session/windows`, JSON).
class Session : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
public:
    explicit Session(QObject *parent = nullptr);

    Q_INVOKABLE [[nodiscard]] static QJsonArray windows();
    Q_INVOKABLE static void save(const QJsonArray &windows);
};

} // namespace tekst
