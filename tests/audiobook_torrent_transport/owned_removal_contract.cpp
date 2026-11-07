#include "../../native/torrent/engine/TorrentEngine.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/torrent_info.hpp>
namespace lt=libtorrent;
class AudiobookOwnedRemovalTest:public QObject {
 Q_OBJECT
private slots:
 void actualPendingStateHandlesOrderingAndDuplicates() {
  TorrentEngine::PendingOwnedRemoval erase;erase.erase=true;
  QVERIFY(!erase.observe(true,false,0));QVERIFY(!erase.observe(true,false,0));
  QVERIFY(erase.observe(false,true,13));QCOMPARE(erase.error,13);
  QVERIFY(erase.observe(false,true,0));QCOMPARE(erase.error,13);
  TorrentEngine::PendingOwnedRemoval reversed;reversed.erase=true;
  QVERIFY(!reversed.observe(false,true,0));QVERIFY(reversed.observe(true,false,0));
  TorrentEngine::PendingOwnedRemoval keep;
  QVERIFY(!keep.observe(false,true,13));QVERIFY(!keep.observe(true,false,0));QCOMPARE(keep.error,0);
  QVERIFY(keep.observe(false,false,0,true));
  TorrentEngine::PendingOwnedRemoval keepReversed;
  QVERIFY(!keepReversed.observe(false,false,0,true));
  QVERIFY(keepReversed.observe(true,false,0));
 }
 void expiredSameHashHandlesRetainDistinctWeakIdentity() {
  QTemporaryDir root(QDir::tempPath()+"/audiobook-identity-XXXXXX");
  QVERIFY(root.isValid());
  QFile file(root.path()+"/file.mp3");QVERIFY(file.open(QIODevice::WriteOnly));file.write("audio");file.close();
  lt::file_storage storage;storage.add_file("file.mp3",5);lt::create_torrent create(storage,16384,lt::create_torrent::v1_only);lt::error_code ec;
  lt::set_piece_hashes(create,root.path().toStdString(),ec);QVERIFY(!ec);std::vector<char> bytes;lt::bencode(std::back_inserter(bytes),create.generate());
  auto metadata=std::make_shared<lt::torrent_info>(bytes.data(),int(bytes.size()));
  auto settings=[] {lt::settings_pack p;p.set_str(lt::settings_pack::listen_interfaces,"127.0.0.1:0");p.set_str(lt::settings_pack::outgoing_interfaces,"127.0.0.1");p.set_str(lt::settings_pack::dht_bootstrap_nodes,"");p.set_bool(lt::settings_pack::enable_dht,false);p.set_bool(lt::settings_pack::enable_lsd,false);p.set_bool(lt::settings_pack::enable_upnp,false);p.set_bool(lt::settings_pack::enable_natpmp,false);return p;};
  lt::torrent_handle first,second;
  {lt::session session(settings());lt::add_torrent_params p;p.ti=metadata;p.save_path=root.path().toStdString();first=session.add_torrent(p);}
  {lt::session session(settings());lt::add_torrent_params p;p.ti=metadata;p.save_path=root.path().toStdString();second=session.add_torrent(p);}
  QVERIFY(!first.is_valid());QVERIFY(!second.is_valid());QVERIFY(first!=second);
  std::map<lt::torrent_handle,TorrentEngine::PendingOwnedRemoval> pending;
  TorrentEngine::PendingOwnedRemoval old;old.token="old";old.erase=true;
  pending.emplace(first,old);QVERIFY(pending.find(second)==pending.end());
  const auto copy=first;QCOMPARE(pending.find(copy)->second.token,"old");
 }
};
QTEST_GUILESS_MAIN(AudiobookOwnedRemovalTest)
#include "owned_removal_contract.moc"
