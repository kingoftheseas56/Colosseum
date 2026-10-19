#include "server1/ServerComposition.h"
#include "server1/discovery/QtDiscoveryNetwork.h"
#include "server1/http/HttpContract.h"
#include "server1/http/TorrentRoutes.h"
#include "server1/http/ExternalCors.h"
#include "server1/http/HlsV2Routes.h"
#include "server1/media/HlsV2Service.h"
#include <QCoreApplication>
#include <QDir>
#include "server1/network/ProxyService.h"
#include "server1/media/SubtitleRoutes.h"
#include "server1/policy/QtEngineTimer.h"
#include <QObject>
#include <QTimer>
#include <stdexcept>

namespace server1 {
struct ServerComposition::Impl final {
    QObject lane;
    QTimer poll;
    std::unique_ptr<policy::EngineRegistry> registry;
    std::unique_ptr<media::HlsV2Service> hls;
    void *router=nullptr;
    void *server=nullptr;
    std::string failure;
    explicit Impl(policy::EngineRegistryConfig config,ServerMediaConfig mediaConfig){
        auto onError=[this](std::exception_ptr){failure="engine timer callback failed";};
        if(!config.repeat)config.repeat=policy::qtEngineRepeat(&lane,onError);
        if(!config.after)config.after=policy::qtEngineAfter(&lane,onError);
        if(!config.discoveryNetworkFactory)config.discoveryNetworkFactory=[] {return std::make_shared<discovery::QtDiscoveryNetwork>();};
        registry=std::make_unique<policy::EngineRegistry>(std::move(config));
        router=server1_http_router_create();
        if(mediaConfig.hlsV2Enabled && qgetenv("HLS_V2_DISABLED").isEmpty()){
            const auto directory=QCoreApplication::applicationDirPath();
            auto paths=[&](const std::string &specified,const char *environment,const QString &name){
                return QStringList{QString::fromStdString(specified),qEnvironmentVariable(environment),directory+"/"+name,directory+"\\"+name+".exe",directory+"\\bin\\"+name+".exe","/usr/lib/jellyfin-ffmpeg/"+name,"/usr/bin/"+name,"/usr/local/bin/"+name};
            };
            media::ExecutableLocator locator;
            const auto executables=locator.locateAll({paths(mediaConfig.ffmpeg,"FFMPEG_BIN","ffmpeg"),paths(mediaConfig.ffprobe,"FFPROBE_BIN","ffprobe"),{}});
            hls=std::make_unique<media::HlsV2Service>(executables.ffmpeg,executables.ffprobe,&lane);
        }
        if(!http::installExternalCors(router) || (hls && !http::installHlsV2Routes(router,hls.get(),[this]{return QString("http://127.0.0.1:%1").arg(server1_http_server_port(server));})) || !network::registerProxyRoutes(router) || !media::installSubtitleRoutes(router,&lane) || !http::installTorrentRoutes(router,*registry)){server1_http_router_destroy(router);router=nullptr;throw std::runtime_error("torrent route installation failed");}
        server=server1_http_server_create(router,1024*1024);
        QObject::connect(&poll,&QTimer::timeout,&lane,[this]{registry->poll();registry->dispatch();});
        poll.start(5);
    }
    ~Impl(){
        poll.stop();
        server1_http_server_destroy(server);
        server1_http_router_destroy(router);
        hls.reset();
        registry.reset();
    }
};
ServerComposition::ServerComposition(policy::EngineRegistryConfig config,ServerMediaConfig mediaConfig):impl_(std::make_unique<Impl>(std::move(config),std::move(mediaConfig))){}
ServerComposition::~ServerComposition()=default;
bool ServerComposition::listen(unsigned short port){return server1_http_server_listen(impl_->server,port)!=0;}
void ServerComposition::stop(){server1_http_server_stop(impl_->server);}
unsigned short ServerComposition::port()const{return server1_http_server_port(impl_->server);}
std::string ServerComposition::error()const{return impl_->failure.empty()?server1_http_server_error(impl_->server):impl_->failure;}
policy::EngineRegistry &ServerComposition::engines(){return *impl_->registry;}
}
