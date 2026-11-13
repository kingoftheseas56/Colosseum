#include "server1/local/LocalAddon.h"
#include "server1/http/HttpContract.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>
#include <stdexcept>

using server1::local::LocalAddon;
namespace {
void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
void until(const std::function<bool()> &done){
    QElapsedTimer elapsed;elapsed.start();
    while(!done()&&elapsed.elapsed()<15000)QCoreApplication::processEvents();
    check(done(),"event-loop timeout");
}
void turns(int count=20){
    QTimer timer;int ticks=0;
    QObject::connect(&timer,&QTimer::timeout,[&]{++ticks;});
    timer.start(0);until([&]{return ticks>=count;});
}
void touch(const QString &path){QFile file(path);check(file.open(QIODevice::WriteOnly),"create synthetic file");}
QJsonObject entry(const QString &path,bool withFiles=false){
    return {{"itemId","local:"+path},{"name",path},{"files",withFiles?QJsonArray{QJsonObject{{"path",path},{"name",path}}}:QJsonArray{}}};
}
QStringList previousDiscovery(const QStringList &roots){
    QStringList files;
    std::function<void(const QString &,int)> visit=[&](const QString &path,int depth){
        if(depth>=7)return;
        for(const auto &file:QDir(path).entryInfoList(QDir::Files|QDir::Dirs|QDir::NoDotAndDotDot|QDir::NoSymLinks,QDir::NoSort)){
            const auto name=file.fileName();
            if(name.startsWith('.')||QStringList{"node_modules","bin","src","build","dist"}.contains(name))continue;
            if(file.isDir())visit(file.absoluteFilePath(),depth+1);
            else if(file.isFile()&&QStringList{"torrent","mp4","mkv","avi"}.contains(file.suffix().toLower()))files.append(file.absoluteFilePath());
        }
    };
    for(const auto &root:roots){QFileInfo file(root);if(!file.isSymLink()&&file.isDir())visit(file.absoluteFilePath(),0);}
    return files;
}
struct HttpProbe {
    void *router=server1_http_router_create();
    void *server=nullptr;
    QTcpSocket socket;
    bool done=false;
    QByteArray wire;
    HttpProbe(){
        check(server1_http_router_add_static(router,0,0,"GET","/heartbeat",200,"discovery-alive"),"register heartbeat");
        server=server1_http_server_create(router,1024);
        check(server1_http_server_listen(server,0),"listen on synthetic loopback server");
        QObject::connect(&socket,&QTcpSocket::connected,[this]{socket.write("GET /heartbeat HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");});
        QObject::connect(&socket,&QTcpSocket::readyRead,[this]{wire+=socket.readAll();});
        QObject::connect(&socket,&QTcpSocket::disconnected,[this]{wire+=socket.readAll();done=true;});
    }
    void request(){socket.connectToHost("127.0.0.1",server1_http_server_port(server));}
    ~HttpProbe(){socket.abort();server1_http_server_destroy(server);QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);server1_http_router_destroy(router);}
};
void responsiveness(const QString &wide,const QString &late,const QStringList &prefixRoots={}){
    // The marker is created after several event-loop turns, in a later root.
    // A synchronous discovery finishes before this and can never index it.
    // This tests real interleaving, not only a machine-dependent duration.
    const auto marker=late+"/heartbeat-marker.mp4";
    QFile::remove(marker);
    HttpProbe http;
    int ticks=0,finished=0,firstIndexTick=0;
    bool httpBeforeIndex=false;
    QStringList indexed;
    LocalAddon::Config config;config.roots=prefixRoots;
    if(!wide.isEmpty())config.roots.append(wide);
    config.roots.append(late);
    config.indexFile=[&](const QString &path){
        if(indexed.isEmpty()){firstIndexTick=ticks;httpBeforeIndex=http.done;}
        indexed.append(path);return QJsonObject{};
    };
    LocalAddon addon(config);
    QTimer heartbeat;
    QObject::connect(&heartbeat,&QTimer::timeout,[&]{
        ++ticks;
        if(ticks==3)http.request();
        if(ticks==4)touch(marker);
    });
    heartbeat.start(0);
    addon.startIndexing([&]{++finished;});
    until([&]{return finished&&ticks>=4&&http.done;});
    heartbeat.stop();
    check(indexed.contains(marker),"discovery did not yield before reaching the later-root heartbeat marker");
    check(firstIndexTick>=4&&httpBeforeIndex,"loopback HTTP must finish while discovery is still pending");
    check(http.wire.contains("200 OK")&&http.wire.contains("discovery-alive"),"HTTP heartbeat response");
    turns();check(finished==1,"discovery completion exactly once");
    std::cout<<"PASS discovery heartbeat before indexing, ticks="<<firstIndexTick<<'\n';
    QFile::remove(marker);
}
void orderAndPolicy(const QString &base){
    const auto root=base+"/policy";check(QDir().mkpath(root+"/branch/child"),"policy tree");
    for(const auto &name:{"z.mp4","A.MKV","b.AvI","c.TORRENT","ignored.txt",".hidden.mp4"})touch(root+'/'+name);
    touch(root+"/branch/child/nested.mp4");touch(root+"/branch/middle.mp4");
    for(const auto &name:{u"\u00e9clair.mp4",u"\u00c5ngstr\u00f6m.MKV",u"\u03a9mega.avi",u"Stra\u00dfe.torrent",u"\u65e5\u672c.mp4"})touch(root+'/'+QString::fromUtf16(name));
    for(const auto &name:{"node_modules","bin","src","build","dist",".hidden"}){
        check(QDir().mkpath(root+'/'+name),"excluded tree");touch(root+'/'+name+"/excluded.mp4");
    }
    auto deep=root;
    QStringList eligibleDepths;
    for(int depth=1;depth<=8;++depth){
        touch(deep+"/depth"+QString::number(depth)+".mp4");
        if(depth<=7)eligibleDepths.append(deep+"/depth"+QString::number(depth)+".mp4");
        deep+="/level";check(QDir().mkpath(deep),"deep tree");
    }
    const auto outside=base+"/outside";check(QDir().mkpath(outside),"outside tree");touch(outside+"/not-authorized.mp4");
    const auto rootLink=base+"/root-link";
#ifdef Q_OS_UNIX
    check(QFile::link(outside,root+"/linked-directory"),"directory symlink fixture");
    check(QFile::link(outside+"/not-authorized.mp4",root+"/linked-file.mp4"),"file symlink fixture");
    check(QFile::link(root,root+"/cycle"),"cycle symlink fixture");
    check(QFile::link(outside,rootLink),"root symlink fixture");
#endif
    QStringList roots{root,base+"/missing",root+"/z.mp4",rootLink,root};
    const auto expected=previousDiscovery(roots);
    QStringList actual;int finished=0;
    LocalAddon::Config config;config.roots=roots;
    config.indexFile=[&](const QString &path){actual.append(path);return QJsonObject{};};
    LocalAddon addon(config);addon.startIndexing([&]{++finished;});until([&]{return finished;});
    check(actual==expected,"root and depth-first filename order must match previous discovery");
    for(const auto &path:eligibleDepths)check(actual.contains(path),"files through depth seven eligible");
    for(const auto &path:actual){
        check(!path.contains("depth8")&&!path.contains("excluded")&&!path.contains("linked-")&&!path.contains("not-authorized")&&!path.contains(".hidden"),"excluded depth, directories, hidden files, and symlinks");
    }
    turns();check(finished==1,"policy scan completion once");
    LocalAddon empty({});int emptyFinished=0;empty.startIndexing([&]{++emptyFinished;});
    check(emptyFinished==0,"empty discovery completion is deferred");until([&]{return emptyFinished;});
    turns();check(emptyFinished==1,"empty discovery completion once");
}
void foldedNameTies(const QString &base){
    const auto root=base+"/folded";check(QDir().mkpath(root),"folded-name root");
    for(int i=0;i<48;++i)touch(root+'/'+QString::number(i)+".mp4");
    // All pairs are distinct on a case-sensitive filesystem. On a case-insensitive
    // filesystem the duplicate path names naturally refer to a single entry.
    for(const auto &name:{u"Case.mp4",u"case.MP4",u"\u00c9cho.mp4",u"\u00e9CHO.MP4",u"\u03a9.mp4",u"\u03c9.MP4"})
        touch(root+'/'+QString::fromUtf16(name));
    check(QDir().mkpath(root+"/A-directory")&&QDir().mkpath(root+"/z-directory"),"interleaved directories");
    touch(root+"/A-directory/inside.mp4");touch(root+"/z-directory/inside.mp4");
#ifdef Q_OS_UNIX
    check(QDir().mkpath(root+"/MIXED.mp4"),"equal-fold directory");
    touch(root+"/MIXED.mp4/inside.mp4");touch(root+"/mixed.MP4");
#endif
    const QStringList roots{root,root};const auto expected=previousDiscovery(roots);
    QStringList actual;int finished=0;
    LocalAddon::Config config;config.roots=roots;
    config.indexFile=[&](const QString &path){actual.append(path);return QJsonObject{};};
    LocalAddon addon(config);addon.startIndexing([&]{++finished;});until([&]{return finished;});
    auto expectedMembers=expected,actualMembers=actual;expectedMembers.sort();actualMembers.sort();
    check(actualMembers==expectedMembers,"equal-fold discovery preserves every member and duplicate root");
    check(actual.size()==expected.size(),"equal-fold discovery count");
    const auto group=[&](const QString &path){return QDir(root).relativeFilePath(path).section('/',0,0);};
    for(qsizetype i=0;i<actual.size();++i)
        check(group(actual[i]).compare(group(expected[i]),Qt::CaseInsensitive)==0,
              "mixed-case and Unicode filename groups follow the original Qt comparator");
    std::cout<<"PASS Qt case-insensitive ordering; equal-fold tie permutation "<<(actual==expected?"unchanged":"differs (Qt ties are unspecified)")<<'\n';
}
void cancellation(const QString &wide,const QString &late){
    LocalAddon::Config config;config.roots={wide,late};
    int indexed=0,finished=0;
    config.indexFile=[&](const QString &){++indexed;return QJsonObject{};};
    LocalAddon addon(config);
    addon.startIndexing([&]{++finished;});addon.cancelIndexing();turns();
    check(indexed==0&&finished==0,"immediate cancellation suppresses work and callback");
    int ticks=0;
    QTimer heartbeat;
    QObject::connect(&heartbeat,&QTimer::timeout,[&]{if(++ticks==4)addon.cancelIndexing();});
    heartbeat.start(0);addon.startIndexing([&]{++finished;});until([&]{return ticks>=20;});heartbeat.stop();
    check(indexed==0&&finished==0,"mid-discovery cancellation suppresses work and callback");
    ticks=0;int restarted=0;
    QObject::disconnect(&heartbeat,nullptr,nullptr,nullptr);
    const auto marker=late+"/restart.mp4";
    QObject::connect(&heartbeat,&QTimer::timeout,[&]{if(++ticks==4){touch(marker);addon.startIndexing([&]{++restarted;});}});
    heartbeat.start(0);addon.startIndexing([&]{++finished;});until([&]{return restarted;});heartbeat.stop();turns();
    check(finished==0&&restarted==1&&indexed==1,"restart replaces pending discovery without stale callbacks");
    QFile::remove(marker);indexed=0;ticks=0;
    QPointer<LocalAddon> owner=new LocalAddon(config);
    QObject::disconnect(&heartbeat,nullptr,nullptr,nullptr);
    QObject::connect(&heartbeat,&QTimer::timeout,[&]{if(++ticks==4)delete owner.data();});
    heartbeat.start(0);owner->startIndexing([&]{++finished;});until([&]{return ticks>=20;});heartbeat.stop();
    check(!owner&&indexed==0&&finished==0,"owner teardown during discovery suppresses work and callback");
}
void callbacks(const QString &root){
    touch(root+"/single.mp4");
    int finished=0,indexed=0;
    std::function<void(QJsonObject)> pending;
    LocalAddon::Config config;config.roots={root};
    config.indexFileAsync=[&](const QString &,auto callback){++indexed;pending=std::move(callback);};
    LocalAddon addon(config);addon.startIndexing([&]{++finished;});until([&]{return bool(pending);});
    auto stale=pending;pending={};addon.cancelIndexing();
    stale(entry("stale"));stale(entry("duplicate"));turns();
    check(addon.size()==0&&finished==0,"cancelled asynchronous completion ignored");
    addon.startIndexing([&]{++finished;});until([&]{return bool(pending);});
    auto current=pending;current(entry("current"));current(entry("duplicate"));
    until([&]{return finished;});turns();
    check(indexed==2&&addon.size()==1&&finished==1,"duplicate provider callbacks complete only once");

    for(int mode=0;mode<5;++mode){
        QPointer<LocalAddon> owner;
        int completed=0,calls=0;
        LocalAddon::Config reentrant;reentrant.roots={root};
        if(mode==0)reentrant.indexFile=[&](const QString &path){++calls;delete owner.data();return entry(path);};
        else if(mode==1)reentrant.indexFileAsync=[&](const QString &path,auto cb){++calls;delete owner.data();cb(entry(path));cb({});};
        else{
            reentrant.indexFile=[&](const QString &path){++calls;return entry(path,true);};
            if(mode==2)reentrant.enrichMeta=[&](const QJsonObject &){delete owner.data();return QJsonObject{{"name","unused"}};};
            if(mode==3)reentrant.enrichMetaAsync=[&](const QJsonObject &,auto cb){delete owner.data();cb({{"name","unused"}});};
        }
        owner=new LocalAddon(reentrant);
        owner->startIndexing([&]{++completed;delete owner.data();});
        until([&]{return !owner;});turns();
        check(calls==1&&completed==(mode==4?1:0),"reentrant index, enrichment, and finish teardown");
    }
    // A provider can supersede the generation before reporting its old result.
    QPointer<LocalAddon> restartedOwner;int calls=0,oldFinished=0,newFinished=0;
    LocalAddon::Config restart;restart.roots={root};
    restart.indexFileAsync=[&](const QString &path,auto cb){
        if(++calls==1){restartedOwner->startIndexing([&]{++newFinished;});cb(entry("stale"));}
        else{cb(entry(path));cb(entry("duplicate"));}
    };
    restartedOwner=new LocalAddon(restart);restartedOwner->startIndexing([&]{++oldFinished;});
    until([&]{return newFinished;});turns();
    check(calls==2&&oldFinished==0&&newFinished==1&&restartedOwner->size()==1,"reentrant provider restart discards stale generation");
    delete restartedOwner.data();
}
void reentrantMetaProviders(){
    for(int mode=0;mode<6;++mode){
        QPointer<LocalAddon> owner;int completed=0;bool valid=true,empty=false;
        const std::vector<int> payload(64,123); // Nontrivial callable-owned storage.
        LocalAddon::Config config;
        config.lookupTorrent=[&,payload](const QString &,auto cb){
            if(mode==0)delete owner.data();
            cb({{"infoHash","synthetic"}});
            valid=valid&&payload.back()==123;
        };
        config.indexTorrentAsync=[&,payload](const QJsonObject &,auto cb){
            if(mode==1)delete owner.data();
            cb(entry("synthetic",true));
            valid=valid&&payload.back()==123;
        };
        if(mode==2)config.enrichLookupMetaAsync=[&,payload](const QJsonObject &,auto cb){delete owner.data();cb({});valid=valid&&payload.back()==123;};
        if(mode==3)config.enrichMetaAsync=[&,payload](const QJsonObject &,auto cb){delete owner.data();cb({});valid=valid&&payload.back()==123;};
        if(mode==4)config.enrichMeta=[&,payload](const QJsonObject &){delete owner.data();valid=valid&&payload.back()==123;return QJsonObject{};};
        owner=new LocalAddon(std::move(config));
        owner->resolveMeta("bt:synthetic",0,[&](QJsonObject value){++completed;empty=value.isEmpty();if(mode==5)delete owner.data();});
        turns();
        check(!owner&&valid&&completed==1&&empty==(mode!=5),"metadata provider or result callback may delete owner exactly once");
    }
}
void mutableProviders(const QString &root){
    for(int i=0;i<3;++i)touch(root+'/'+QString::number(i)+".mp4");
    for(bool asynchronous:{false,true}){
        QList<int> indexed,enriched;int finished=0;
        LocalAddon::Config config;config.roots={root};
        if(asynchronous){
            config.indexFileAsync=[count=0,&indexed](const QString &path,auto cb)mutable{indexed.append(++count);cb(entry(path,true));};
            config.enrichMetaAsync=[count=0,&enriched](const QJsonObject &,auto cb)mutable{enriched.append(++count);cb({});};
        }else{
            config.indexFile=[count=0,&indexed](const QString &path)mutable{indexed.append(++count);return entry(path,true);};
            config.enrichMeta=[count=0,&enriched](const QJsonObject &)mutable{enriched.append(++count);return QJsonObject{};};
        }
        LocalAddon addon(std::move(config));addon.startIndexing([&]{++finished;});until([&]{return finished;});
        check(indexed==QList<int>{1,2,3}&&enriched==QList<int>{1,2,3},"original mutable provider state survives invocations");
    }
}
void acceptedEntryLimit(const QString &root,const QString &late){
    // More than 10,000 candidates must remain discoverable: failures do not
    // consume the 10,000 accepted-entry limit, including when a DB is preloaded.
    for(int i=0;i<10003;++i)touch(root+'/'+QString::number(i)+".mp4");
    responsiveness(root,late);
    int indexed=0,finished=0;
    LocalAddon::Config config;config.roots={root};
    config.indexFile=[&](const QString &path){return ++indexed<=10000?QJsonObject{}:entry(path);};
    LocalAddon addon(config);
    for(int i=0;i<9999;++i)check(addon.saveEntry("preloaded:"+QString::number(i),entry("stored")),"preload fixture");
    addon.startIndexing([&]{++finished;});until([&]{return finished;});
    check(indexed==10001&&addon.size()==10000,"accepted-entry limit must not cap discovered candidates");
    finished=0;addon.startIndexing([&]{++finished;});until([&]{return finished;});
    check(indexed==10001&&addon.size()==10000,"full storage does not invoke index provider");
}
}
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);
    try{
        QTemporaryDir temp;check(temp.isValid(),"temporary synthetic tree");
        const auto ignored=temp.path()+"/ignored",late=temp.path()+"/late";
        check(QDir().mkpath(ignored)&&QDir().mkpath(late),"wide fixture roots");
        for(int i=0;i<8192;++i)touch(ignored+'/'+QString::number(i)+".txt");
        if(argc>1&&QByteArray(argv[1])=="--cancel-only"){cancellation(ignored,late);return 0;}
        responsiveness(ignored,late);
        if(argc>1&&QByteArray(argv[1])=="--responsiveness-only")return 0;
        const auto hidden=temp.path()+"/hidden",empty=temp.path()+"/empty";
        check(QDir().mkpath(hidden)&&QDir().mkpath(empty),"hidden and empty roots");
        for(int i=0;i<8192;++i){touch(hidden+"/."+QString::number(i)+".mp4");check(QDir().mkpath(empty+'/'+QString::number(i)),"empty child fixture");}
        responsiveness(hidden,late);responsiveness(empty,late);
        QStringList invalidRoots;for(int i=0;i<8192;++i)invalidRoots.append(temp.path()+"/absent/"+QString::number(i));
        responsiveness({},late,invalidRoots);
        cancellation(ignored,late);orderAndPolicy(temp.path());foldedNameTies(temp.path());
        const auto single=temp.path()+"/single",many=temp.path()+"/many";
        check(QDir().mkpath(single)&&QDir().mkpath(many),"index fixture roots");
        callbacks(single);
        const auto stateful=temp.path()+"/stateful";check(QDir().mkpath(stateful),"mutable provider root");
        mutableProviders(stateful);reentrantMetaProviders();acceptedEntryLimit(many,late);
        std::cout<<"PASS local discovery scheduling, order, policy, cancellation, restart, reentrant teardown, and entry limits\n";
        return 0;
    }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
