#include <QCoreApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QTimer>
#include <iostream>
#include "server1/ServerComposition.h"
#include "server1/policy/TorrentMetadata.h"

int main(int argc,char **argv){
    QCoreApplication app(argc,argv);
    QCommandLineParser args;args.addHelpOption();
    args.addOptions({{"serve","Run the production HTTP composition."},{"port","Listener port (0 selects an ephemeral port).","port","11470"},{"cache","Torrent cache root.","path"},{"torrent","Preload a metainfo file for the standalone driver.","path"},{"peer","Connect a source peer for the preloaded torrent.","address:port"},{"fixed-interface","Disable OS interface-change monitoring for controlled fixtures."},{"quit-after-ms","Graceful test-host lifetime limit.","milliseconds"}});
    args.process(app);
    server1::policy::EngineRegistryConfig config;config.cacheRoot=args.value("cache").toStdString();
    if(args.isSet("fixed-interface"))config.transportFactory=[](const server1::ports::TorrentOpenRequest &request){auto fixed=request;fixed.monitorNetworkChanges=false;return server1::ports::openTorrentTransport(fixed);};
    try{
        server1::ServerComposition server(std::move(config));
        bool valid=false;const auto port=args.isSet("serve")?args.value("port").toUShort(&valid):0;
        if(args.isSet("serve") && !valid){std::cerr<<"invalid port\n";return 2;}
        if(!server.listen(port)){std::cerr<<server.error()<<'\n';return 1;}
        if(!args.isSet("serve")){std::cout<<"server1 composition initialized; listening=YES\n";server.stop();return 0;}
        if(args.isSet("torrent")){
            QFile input(args.value("torrent"));if(!input.open(QIODevice::ReadOnly)){std::cerr<<"cannot open metainfo\n";return 2;}
            const auto raw=input.readAll();std::vector<std::uint8_t> bytes(raw.begin(),raw.end());std::string error;
            auto meta=server1::policy::TorrentMetadata::parse(bytes,&error);if(!meta){std::cerr<<error<<'\n';return 2;}
            const auto peer=args.value("peer");
            server.engines().create({meta->infoHash(),server1::policy::Value::object({{"torrent",server1::policy::Value::bytes(std::move(bytes))}})},[peer](server1::policy::EngineCreateResult result){
                if(result.outcome!=server1::policy::EngineCreateOutcome::Ready){std::cerr<<result.error<<'\n';return;}
                if(!peer.isEmpty()){auto colon=peer.lastIndexOf(':');bool valid=false;auto port=peer.mid(colon+1).toUShort(&valid);if(colon>0 && valid)static_cast<void>(result.engine->connectSourcePeer(1,peer.left(colon).toStdString(),port));}
                std::cout<<"READY "<<result.engine->sourceKey()<<std::endl;
            });
        }
        std::cout<<"LISTEN "<<server.port()<<std::endl;
        if(args.isSet("quit-after-ms")){bool valid=false;const auto ms=args.value("quit-after-ms").toInt(&valid);if(!valid || ms<1)return 2;QTimer::singleShot(ms,&app,&QCoreApplication::quit);}
        const auto exit=app.exec();server.stop();return exit;
    }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
