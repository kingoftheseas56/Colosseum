#include "server1/ServerComposition.h"
#include "server1/discovery/QtDiscoveryNetwork.h"
#include "server1/http/HttpContract.h"
#include "server1/http/TorrentRoutes.h"
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
    void *router=nullptr;
    void *server=nullptr;
    std::string failure;
    explicit Impl(policy::EngineRegistryConfig config){
        auto onError=[this](std::exception_ptr){failure="engine timer callback failed";};
        if(!config.repeat)config.repeat=policy::qtEngineRepeat(&lane,onError);
        if(!config.after)config.after=policy::qtEngineAfter(&lane,onError);
        if(!config.discoveryNetworkFactory)config.discoveryNetworkFactory=[] {return std::make_shared<discovery::QtDiscoveryNetwork>();};
        registry=std::make_unique<policy::EngineRegistry>(std::move(config));
        router=server1_http_router_create();
        if(!network::registerProxyRoutes(router) || !media::installSubtitleRoutes(router,&lane) || !http::installTorrentRoutes(router,*registry)){server1_http_router_destroy(router);router=nullptr;throw std::runtime_error("torrent route installation failed");}
        server=server1_http_server_create(router,1024*1024);
        QObject::connect(&poll,&QTimer::timeout,&lane,[this]{registry->poll();registry->dispatch();});
        poll.start(5);
    }
    ~Impl(){
        poll.stop();
        server1_http_server_destroy(server);
        server1_http_router_destroy(router);
        registry.reset();
    }
};
ServerComposition::ServerComposition(policy::EngineRegistryConfig config):impl_(std::make_unique<Impl>(std::move(config))){}
ServerComposition::~ServerComposition()=default;
bool ServerComposition::listen(unsigned short port){return server1_http_server_listen(impl_->server,port)!=0;}
void ServerComposition::stop(){server1_http_server_stop(impl_->server);}
unsigned short ServerComposition::port()const{return server1_http_server_port(impl_->server);}
std::string ServerComposition::error()const{return impl_->failure.empty()?server1_http_server_error(impl_->server):impl_->failure;}
policy::EngineRegistry &ServerComposition::engines(){return *impl_->registry;}
}
