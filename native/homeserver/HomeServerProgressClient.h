#pragma once
#include "HomeServerPlayback.h"
#include <QPointer>

struct HomeServerProgressState {
    qint64 positionMs = 0;
    qint64 durationMs = 0;
    qint64 updatedAt = 0;
    bool paused = false;
    bool played = false;
    bool writePlayed = false;
};

// One native provider event. Ordering, reconciliation and persistence belong to
// the existing service/progress owner, outside this protocol job.
class HomeServerProgressJob final : public QObject {
public:
    struct Result {
        QString error;
        bool expired = false;
        bool ok() const { return error.isEmpty(); }
    };
    using Callback = std::function<void(Result)>;
    explicit HomeServerProgressJob(HomeServerClient *client, QObject *parent = nullptr);
    ~HomeServerProgressJob() override;
    void start(QJsonObject connection, QJsonObject item,
               HomeServerPlaybackJob::Result playback, QString token,
               QString event, HomeServerProgressState state, Callback callback);
    void cancel();
private:
    void request(QString path, QByteArray method, QJsonValue body,
                 std::function<void(QJsonValue)> callback);
    void report();
    void synchronize();
    void watched();
    QString userDataPath() const;
    void finish(QString error = {}, bool expired = false);
    QPointer<HomeServerClient> m_client;
    Callback m_callback;
    QJsonObject m_connection, m_item;
    HomeServerPlaybackJob::Result m_playback;
    HomeServerProgressState m_state;
    QString m_token, m_event;
    int m_major = 0;
    bool m_started = false, m_finished = false;
};
