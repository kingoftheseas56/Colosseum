#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QHash>
#include <QtCore/QList>
#include <QtCore/QMetaType>
#include <QtCore/QString>
#include <QtCore/QUrl>
#include <QtCore/QVariantMap>

namespace Colosseum::Player2
{
Q_NAMESPACE

enum class Player2State
{
    Idle,
    Opening,
    Buffering,
    Playing,
    Paused,
    Seeking,
    Ended,
    Recovering,
    Error,
};
Q_ENUM_NS(Player2State)

enum class NormalizationMode
{
    Smooth,
    Light,
    Full,
};
Q_ENUM_NS(NormalizationMode)

enum class Player2ErrorCode
{
    None,
    Cancelled,
    OpenFailed,
    UnsupportedHardware,
    DecodeFailed,
    NetworkFailed,
    DeviceLost,
    AudioDeviceLost,
    InvalidCommand,
};
Q_ENUM_NS(Player2ErrorCode)

struct ExternalSubtitleRequest
{
    Q_GADGET
    Q_PROPERTY(QUrl source MEMBER source)
    Q_PROPERTY(QString title MEMBER title)
    Q_PROPERTY(QString language MEMBER language)

public:
    QUrl source;
    QString title;
    QString language;
};

inline bool operator==(const ExternalSubtitleRequest &left, const ExternalSubtitleRequest &right)
{
    return left.source == right.source && left.title == right.title &&
           left.language == right.language;
}

inline bool operator!=(const ExternalSubtitleRequest &left, const ExternalSubtitleRequest &right)
{
    return !(left == right);
}

using RequestHeaders = QHash<QByteArray, QByteArray>;
using ExternalSubtitleRequests = QList<ExternalSubtitleRequest>;

struct PlaybackRequest
{
    Q_GADGET
    Q_PROPERTY(QUrl source MEMBER source)
    Q_PROPERTY(QString mediaId MEMBER mediaId)
    Q_PROPERTY(QString title MEMBER title)
    Q_PROPERTY(double resumeSeconds MEMBER resumeSeconds)
    Q_PROPERTY(RequestHeaders headers MEMBER headers)
    Q_PROPERTY(QVariantMap displayMetadata MEMBER displayMetadata)
    Q_PROPERTY(ExternalSubtitleRequests externalSubtitles MEMBER externalSubtitles)
    Q_PROPERTY(bool stream MEMBER stream)
    Q_PROPERTY(bool live MEMBER live)

public:
    QUrl source;
    QString mediaId;
    QString title;
    double resumeSeconds = 0.0;
    RequestHeaders headers;
    QVariantMap displayMetadata;
    ExternalSubtitleRequests externalSubtitles;
    bool stream = false;
    bool live = false;
};

struct Player2Error
{
    Q_GADGET
    Q_PROPERTY(Player2ErrorCode code MEMBER code)
    Q_PROPERTY(QString message MEMBER message)
    Q_PROPERTY(bool recoverable MEMBER recoverable)

public:
    Player2ErrorCode code = Player2ErrorCode::None;
    QString message;
    bool recoverable = false;
};

void registerPlayer2MetaTypes();
}

Q_DECLARE_METATYPE(Colosseum::Player2::Player2State)
Q_DECLARE_METATYPE(Colosseum::Player2::NormalizationMode)
Q_DECLARE_METATYPE(Colosseum::Player2::Player2ErrorCode)
Q_DECLARE_METATYPE(Colosseum::Player2::ExternalSubtitleRequest)
Q_DECLARE_METATYPE(Colosseum::Player2::PlaybackRequest)
Q_DECLARE_METATYPE(Colosseum::Player2::Player2Error)
