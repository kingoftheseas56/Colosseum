#include "Player2Types.h"

namespace Colosseum::Player2
{
void registerPlayer2MetaTypes()
{
    qRegisterMetaType<Player2State>("Colosseum::Player2::Player2State");
    qRegisterMetaType<NormalizationMode>("Colosseum::Player2::NormalizationMode");
    qRegisterMetaType<Player2ErrorCode>("Colosseum::Player2::Player2ErrorCode");
    qRegisterMetaType<ExternalSubtitleRequest>("Colosseum::Player2::ExternalSubtitleRequest");
    qRegisterMetaType<PlaybackRequest>("Colosseum::Player2::PlaybackRequest");
    qRegisterMetaType<Player2Error>("Colosseum::Player2::Player2Error");
    qRegisterMetaType<RequestHeaders>("Colosseum::Player2::RequestHeaders");
    qRegisterMetaType<ExternalSubtitleRequests>("Colosseum::Player2::ExternalSubtitleRequests");
}
}
