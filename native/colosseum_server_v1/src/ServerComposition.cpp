#include "server1/ServerComposition.h"
#include "server1/discovery/QtDiscoveryNetwork.h"
#include "server1/http/HttpContract.h"
#include "server1/http/TorrentRoutes.h"
#include "server1/http/ExternalCors.h"
#include "server1/http/HlsV2Routes.h"
#include "server1/http/LegacyMediaRoutes.h"
#include "server1/media/HlsV2Service.h"
#include "server1/media/LegacyHlsService.h"
#include "server1/http/RootRoutes.h"
#include "server1/http/DownloadRoutes.h"
#include "server1/http/ArchiveRoutes.h"
#include "server1/downloads/DownloadManager.h"
#include "server1/settings/RuntimeSettings.h"
#include "server1/settings/MediaSettings.h"
#include "server1/settings/CachePolicy.h"
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QThread>
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
    QTimer cleanupTimer;
    std::unique_ptr<settings::SettingsStore> settings;
    std::unique_ptr<downloads::DownloadManager> downloads;
    std::unique_ptr<policy::EngineRegistry> registry;
    std::unique_ptr<media::HlsV2Service> hls;
    std::unique_ptr<media::LegacyHlsService> legacyHls;
    void *router=nullptr;
    void *server=nullptr;
    std::string failure;
    bool stopping=false;
    explicit Impl(policy::EngineRegistryConfig config,ServerMediaConfig mediaConfig,ServerRuntimeConfig runtimeConfig){
        auto appPath=QString::fromStdString(runtimeConfig.appPath);
        if(appPath.isEmpty())appPath=qEnvironmentVariable("APP_PATH");
        if(appPath.isEmpty() && !config.cacheRoot.empty())appPath=QString::fromStdString(config.cacheRoot.string());
        if(appPath.isEmpty())appPath=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/colosseum-server1";
        QDir().mkpath(appPath);
        auto settingsPath=runtimeConfig.settingsPathOverride;
        if(settingsPath.empty())settingsPath=qEnvironmentVariable("SETTINGS_PATH").toStdString();
        const bool disableCaching=!qgetenv("DISABLE_CACHING").isEmpty();
        settings=std::make_unique<settings::SettingsStore>(settings::SettingsStoreConfig{
            appPath.toStdString(),"4.21.0",settingsPath,false,disableCaching});
        downloads=std::make_unique<downloads::DownloadManager>(appPath,&lane);
        if(!config.defaultsProvider)config.defaultsProvider=[this](const std::string &key){
            return settings::runtimeEngineDefaults(*settings,key);
        };
        if(!config.cachePathProvider)config.cachePathProvider=[this](const std::string &key){
            const auto *root=settings->find("cacheRoot");
            return cache::cachePath(root && root->kind()==policy::Value::Kind::String
                ? root->asString() : settings->appPath(),key);
        };
        // M564's application policy overrides the generic EngineFS timeouts.
        config.streamTimeoutMs=20000;
        config.engineTimeoutMs=120000;
        const auto observer=config.onEvent;
        config.onEvent=[this,observer](const policy::EngineEvent &event){
            if(!stopping && registry){
                if(event.type==policy::EngineEventType::EngineInactive)
                    registry->remove(event.sourceKey);
                else if(event.type==policy::EngineEventType::EngineIdle)
                    (void)registry->setSwarmPaused(event.sourceKey,true);
                else if(event.type==policy::EngineEventType::Created)
                    QTimer::singleShot(0,&lane,[this]{if(!stopping)trimCache();});
            }
            if(observer)observer(event);
        };
        auto onError=[this](std::exception_ptr){failure="engine timer callback failed";};
        if(!config.repeat)config.repeat=policy::qtEngineRepeat(&lane,onError);
        if(!config.after)config.after=policy::qtEngineAfter(&lane,onError);
        if(!config.discoveryNetworkFactory)config.discoveryNetworkFactory=[] {return std::make_shared<discovery::QtDiscoveryNetwork>();};
        registry=std::make_unique<policy::EngineRegistry>(std::move(config));
        router=server1_http_router_create();
        {
            const auto directory=QCoreApplication::applicationDirPath();
            auto paths=[&](const std::string &specified,const char *environment,const QString &name){
                return QStringList{QString::fromStdString(specified),qEnvironmentVariable(environment),directory+"/"+name,directory+"\\"+name+".exe",directory+"\\bin\\"+name+".exe","/usr/lib/jellyfin-ffmpeg/"+name,"/usr/bin/"+name,"/usr/local/bin/"+name};
            };
            media::ExecutableLocator locator;
            const auto executables=locator.locateAll({paths(mediaConfig.ffmpeg,"FFMPEG_BIN","ffmpeg"),paths(mediaConfig.ffprobe,"FFPROBE_BIN","ffprobe"),paths({},"FFSPLIT_BIN","ffsplit")});
            if(mediaConfig.hlsV2Enabled && qgetenv("HLS_V2_DISABLED").isEmpty())
                hls=std::make_unique<media::HlsV2Service>(executables.ffmpeg,executables.ffprobe,&lane);
            legacyHls=std::make_unique<media::LegacyHlsService>(executables,&lane);
        }
        if(hls)settings::applyMediaSettings(*settings,*hls,QThread::idealThreadCount());
        auto localBase=[this]{return QString("http://127.0.0.1:%1").arg(server1_http_server_port(server));};
        cleanupTimer.setSingleShot(true);
        cleanupTimer.setInterval(10000);
        QObject::connect(&cleanupTimer,&QTimer::timeout,&lane,[this]{trimCache();});
        http::RootRoutesConfig rootRoutes;
        rootRoutes.settings=settings.get();
        rootRoutes.baseUrl=[localBase]{return localBase().toStdString();};
        rootRoutes.settingsChanged=[this]{
            cleanupTimer.start();
            if(hls)settings::applyMediaSettings(*settings,*hls,QThread::idealThreadCount());
        };
        rootRoutes.webUILocation=runtimeConfig.webUiLocation.empty()
            ? qEnvironmentVariable("WEBUI_LOCATION","https://app.strem.io/shell-v4.4/").toStdString()
            : runtimeConfig.webUiLocation;
        if(!http::installExternalCors(router) || (hls && !http::installHlsV2Routes(router,hls.get(),localBase)) || !http::installLegacyMediaRoutes(router,legacyHls.get(),localBase) || !http::installRootRoutes(router,std::move(rootRoutes)) || !network::registerProxyRoutes(router) || !media::installSubtitleRoutes(router,&lane)){server1_http_router_destroy(router);router=nullptr;throw std::runtime_error("server route installation failed");}
        const auto ipcKey=runtimeConfig.ipcDownloadKey.empty()
            ? qEnvironmentVariable("SERVER_IPC_KEY") : QString::fromStdString(runtimeConfig.ipcDownloadKey);
        http::mountDownloadRoutes(router,downloads.get(),ipcKey);
        http::mountArchiveRoutes(router);
        if(!http::installTorrentRoutes(router,*registry)){server1_http_router_destroy(router);router=nullptr;throw std::runtime_error("torrent route installation failed");}
        server=server1_http_server_create(router,1024*1024);
        QObject::connect(&poll,&QTimer::timeout,&lane,[this]{registry->poll();registry->dispatch();});
        poll.start(5);
    }
    ~Impl(){
        stopping=true;
        poll.stop();
        cleanupTimer.stop();
        server1_http_server_destroy(server);
        server1_http_router_destroy(router);
        hls.reset();
        legacyHls.reset();
        registry.reset();
    }
    void trimCache(){
        if(stopping || !registry)return;
        try{
            const auto *root=settings->find("cacheRoot");
            const auto *size=settings->find("cacheSize");
            const auto directory=cache::cachePath(root && root->kind()==policy::Value::Kind::String
                ? root->asString() : settings->appPath(),"");
            (void)cache::clearCache(directory,size?policy::jsNumber(*size):0,0,registry->list());
        }catch(const std::exception &error){failure=error.what();}
    }
};
ServerComposition::ServerComposition(policy::EngineRegistryConfig config,ServerMediaConfig mediaConfig,ServerRuntimeConfig runtimeConfig):impl_(std::make_unique<Impl>(std::move(config),std::move(mediaConfig),std::move(runtimeConfig))){}
ServerComposition::~ServerComposition()=default;
bool ServerComposition::listen(unsigned short port){return server1_http_server_listen(impl_->server,port)!=0;}
void ServerComposition::stop(){server1_http_server_stop(impl_->server);}
unsigned short ServerComposition::port()const{return server1_http_server_port(impl_->server);}
std::string ServerComposition::error()const{return impl_->failure.empty()?server1_http_server_error(impl_->server):impl_->failure;}
policy::EngineRegistry &ServerComposition::engines(){return *impl_->registry;}
}
