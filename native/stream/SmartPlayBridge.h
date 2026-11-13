#pragma once
#include <QObject>
#include <QVariantMap>

class SmartPlayBridge final : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    Q_INVOKABLE QVariantMap pick(const QVariantList &rows, const QString &quality,
                                const QVariantMap &context) const;
};
