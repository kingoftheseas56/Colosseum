#include "../../native/engine/AudiobookTorrentTransport.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <libtorrent/session.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/torrent_info.hpp>
namespace lt=libtorrent;
static lt::settings_pack isolated() {
 lt::settings_pack p;
 p.set_int(lt::settings_pack::alert_mask,lt::alert_category::status | lt::alert_category::storage | lt::alert_category::error);
 p.set_str(lt::settings_pack::listen_interfaces,"127.0.0.1:0");
 p.set_str(lt::settings_pack::outgoing_interfaces,"127.0.0.1");
 p.set_str(lt::settings_pack::dht_bootstrap_nodes,"");
 p.set_bool(lt::settings_pack::enable_dht,false);p.set_bool(lt::settings_pack::enable_lsd,false);
 p.set_bool(lt::settings_pack::enable_upnp,false);p.set_bool(lt::settings_pack::enable_natpmp,false);
 return p;
}
class LoopbackEngine final:public IAudiobookTorrentEngine {
public:
 lt::session session{isolated()};lt::torrent_handle handle;std::shared_ptr<lt::torrent_info> metadata;
 struct Removal { QString token;bool erase=false,removed=false,deletion=false;int error=0;bool flushed=false; };
 std::map<lt::torrent_handle,Removal> removals;QTimer alerts;bool keepRemoved=false,keepFlushed=false;
 LoopbackEngine(){connect(&alerts,&QTimer::timeout,this,[this]{drain();});alerts.start(5);}
 QString expected,path;int port=0;QVector<int> selected;bool retired=false;
 bool isRunning()const override{return true;} void start()override{}
 bool hasTorrent(const QString& h)const override{return h==expected && handle.is_valid() && !retired;}
 QString addMagnet(const QString& magnet,const QString& p,bool only)override {
  if(!only || !magnet.contains(expected))return {};
  path=p;lt::add_torrent_params params;params.ti=metadata;params.save_path=p.toStdString();
  params.flags|=lt::torrent_flags::upload_mode;params.flags&=~lt::torrent_flags::paused;params.flags&=~lt::torrent_flags::auto_managed;
  handle=session.add_torrent(params);retired=false;return expected;
 }
 QString actualSavePath(const QString&)const override{return QString::fromStdString(handle.status().save_path);}
 QJsonArray files(const QString&)const override {
  QJsonArray out;const auto& fs=metadata->files();
  for(int i=0;i<fs.num_files();++i)out.append(QJsonObject{{"index",i},{"name",QString::fromStdString(fs.file_path(lt::file_index_t(i)))},{"size",double(fs.file_size(lt::file_index_t(i)))}});
  return out;
 }
 QJsonArray verifiedBytes(const QString&)const override {
  std::vector<std::int64_t> progress;handle.file_progress(progress,lt::torrent_handle::piece_granularity);
  QJsonArray out;for(auto n:progress)out.append(double(n));return out;
 }
 void setPriorities(const QString&,const QVector<int>& priorities)override {
  selected=priorities;std::vector<lt::download_priority_t> values;for(int p:priorities)values.push_back(lt::download_priority_t(p));handle.prioritize_files(values);
 }
 void startPayload(const QString&)override {
  handle.unset_flags(lt::torrent_flags::upload_mode);handle.resume();
  handle.connect_peer(lt::tcp::endpoint(lt::make_address("127.0.0.1"),std::uint16_t(port)));
 }
 bool requestRemoval(const QString& token,const QString& hash,const QString& expectedPath,bool erase)override {
  if(hash!=expected || actualSavePath(hash)!=expectedPath || retired)return false;
  removals.emplace(handle,Removal{token,erase});session.remove_torrent(handle,erase?lt::session::delete_files:lt::remove_flags_t{});retired=true;return true;
 }
 void drain(){
  std::vector<lt::alert*> batch;session.pop_alerts(&batch);
  for(auto* alert:batch){lt::torrent_handle identity;bool removed=false,deletion=false,flushed=false;int error=0;
   if(auto* a=lt::alert_cast<lt::torrent_removed_alert>(alert)){identity=a->handle;removed=true;}
   else if(auto* a=lt::alert_cast<lt::cache_flushed_alert>(alert)){identity=a->handle;flushed=true;}
   else if(auto* a=lt::alert_cast<lt::torrent_deleted_alert>(alert)){identity=a->handle;deletion=true;}
   else if(auto* a=lt::alert_cast<lt::torrent_delete_failed_alert>(alert)){identity=a->handle;deletion=true;error=a->error.value();}
   else continue;
   auto it=removals.find(identity);if(it==removals.end())continue;
   auto& pending=it->second;pending.removed|=removed;pending.flushed|=flushed;if(!pending.erase){keepRemoved|=removed;keepFlushed|=flushed;}if(pending.erase && deletion && !pending.deletion){pending.deletion=true;pending.error=error;}
   if(pending.removed && (pending.erase ? pending.deletion : pending.flushed)){const auto result=pending;removals.erase(it);emit removalSettled(result.token,result.error==0,result.error);}
  }
 }
};
class FixtureTest:public QObject {
 Q_OBJECT
private slots:
 void nestedAudioUsesRealVerifiedTorrentBytes() {
  QTemporaryDir dir(QDir::tempPath()+"/audiobook-loopback-XXXXXX");QVERIFY(dir.isValid());
  const QString seed=dir.path()+"/seed";QDir().mkpath(seed+"/Book");
  const QList<QPair<QString,QByteArray>> files={{"Book/cover.jpg",QByteArray(100,'C')},{"Book/Chapter 10.mp3",QByteArray(32768,'A')},{"Book/Chapter 2.mp3",QByteArray(49152,'B')}};
  for(const auto& item:files){QFile f(seed+'/'+item.first);QVERIFY(f.open(QIODevice::WriteOnly));QCOMPARE(f.write(item.second),qint64(item.second.size()));}
  // Deterministic offline bencoded v1 metadata generated from exact file bytes/order above.
  const QByteArray encoded=QByteArray::fromHex(
      "64343a696e666f64353a66696c65736c64363a6c656e6774686931303065343a706174686c393a636f7665722e6a70676565"
      "64363a6c656e67746869333237363865343a706174686c31343a436861707465722031302e6d7033656564363a6c656e6774"
      "6869343931353265343a706174686c31333a4368617074657220322e6d7033656565343a6e616d65343a426f6f6b31323a70"
      "69656365206c656e67746869313633383465363a7069656365733132303a26aa87faecc07ecfe6f8c57c1c5bfda7a6897b96"
      "7db67baceeecb3a420bf37a7beca4a45185f8f3c3516e27fc424b8bf29bf5ac07157eab6515020ea79996fbf590aad9da7ce"
      "2fe0480823331913b6f079996fbf590aad9da7ce2fe0480823331913b6f03f6394fbd002f322914183cff2939868a6264afe"
      "6565"
  );
  auto ti=std::make_shared<lt::torrent_info>(encoded.data(),int(encoded.size()));
  lt::session seeder(isolated());lt::add_torrent_params params;params.ti=ti;params.save_path=seed.toStdString();params.flags|=lt::torrent_flags::seed_mode;params.flags&=~lt::torrent_flags::paused;params.flags&=~lt::torrent_flags::auto_managed;seeder.add_torrent(params);
  LoopbackEngine engine;engine.metadata=ti;const auto hash=ti->info_hashes().v1.to_string();engine.expected=QString::fromLatin1(QByteArray(hash.data(),int(hash.size())).toHex());
  QTRY_VERIFY_WITH_TIMEOUT(seeder.listen_port()>0,2000);engine.port=seeder.listen_port();
  AudiobookTorrentTransport transport(&engine,{10000,10000,20});QSignalSpy done(&transport,&AudiobookTorrentTransport::completed),failed(&transport,&AudiobookTorrentTransport::failed);
  transport.start(1,engine.expected,dir.path()+"/download");
  QTRY_VERIFY_WITH_TIMEOUT(done.size()+failed.size()>0,15000);QCOMPARE(failed.size(),0);QCOMPARE(done.size(),1);
  QVERIFY(engine.keepRemoved);QVERIFY(engine.keepFlushed);
  const auto actual=done[0][2].toStringList();QCOMPARE(actual.size(),2);QVERIFY(actual[0].endsWith("Chapter 2.mp3"));QVERIFY(actual[1].endsWith("Chapter 10.mp3"));
  for(int i=0;i<actual.size();++i){QFile f(actual[i]);QVERIFY(f.open(QIODevice::ReadOnly));QCOMPARE(f.readAll(),files[i==0?2:1].second);}
  QCOMPARE(done[0][3].toLongLong(),qint64(81920));
  // Re-admit the owned complete torrent into a new attempt and confirm actual
  // erase completion from BOTH libtorrent removal and delete-I/O outcomes.
  const QString erasePath=dir.path()+"/erase";QDir().mkpath(erasePath+"/Book");
  for(const auto& item:files){QFile f(erasePath+'/'+item.first);QVERIFY(f.open(QIODevice::WriteOnly));f.write(item.second);}
  engine.addMagnet("magnet:?xt=urn:btih:"+engine.expected,erasePath,true);
  QSignalSpy settled(&engine,&IAudiobookTorrentEngine::removalSettled);
  QVERIFY(engine.requestRemoval("owned-delete",engine.expected,QFileInfo(erasePath).canonicalFilePath(),true));
  QTRY_COMPARE_WITH_TIMEOUT(settled.size(),1,5000);QVERIFY(settled[0][1].toBool());
  for(const auto& item:files)QVERIFY(!QFileInfo::exists(erasePath+'/'+item.first));
  const auto manifest=engine.files(engine.expected);for(int i=0;i<manifest.size();++i)QCOMPARE(engine.selected[i],manifest[i].toObject()["name"].toString().endsWith(".mp3")?4:0);
 }
};
QTEST_GUILESS_MAIN(FixtureTest)
#include "loopback_fixture.moc"
