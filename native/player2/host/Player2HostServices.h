#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>

namespace Colosseum::Player2
{
class Player2HostServices : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;
    ~Player2HostServices() override = default;

    virtual void requestAdjacentEpisode(const QString &mediaId, int direction) = 0;
    virtual void requestAlternateSources(const QString &mediaId) = 0;
    virtual void reportProgress(const QString &mediaId, double position, double duration) = 0;
};
}
