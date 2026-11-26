#pragma once

#include <QObject>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

namespace kode
{

// What was open when the app last quit (the documents, and each window's
// geometry and splits), reopened at launch. Kept in QSettings
// (`session/state`, JSON).
class Session : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
public:
    explicit Session(QObject *parent = nullptr);

    Q_INVOKABLE [[nodiscard]] static QVariantMap state();
    Q_INVOKABLE static void save(const QVariantMap &state);
};

} // namespace kode
