#include "cache/CachePolicy.h"
#include "core/ColosseumServer.h"
#include "core/HttpRouter.h"
#include "enginefs/EngineFsControlPlane.h"
#include "media/MediaPipeline.h"
#include "network_app/NetworkAppServices.h"
#include "remote_archive/RemoteArchive.h"
#include "remote_archive/RemoteServices.h"
#include "scheduler/FileStream.h"
#include "scheduler/SchedulerSpine.h"
#include "settings/ServerSettings.h"
#include "torrent/DiscoverySession.h"
#include "torrent/Storage.h"
#include "torrent/model/MetadataExchange.h"
#include "torrent/model/TorrentMetadata.h"
#include "torrent/model/VirtualPieceMap.h"
#include "torrent_http/StreamProgressTracker.h"
#include "torrent_http/TorrentHttpSurface.h"

int main()
{
    ColosseumServer::Media::TrackInfo track;
    track.id = 1;
    return track.id == 1 ? 0 : 1;
}
