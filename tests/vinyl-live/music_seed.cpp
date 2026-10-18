// Real deterministic audio + loopback torrent seeder/tracker for runtime QA.
#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/torrent_status.hpp>
#include <libtorrent/alert.hpp>
#include <libtorrent/alert_types.hpp>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

namespace lt = libtorrent;
bool writeTone(const QString& path, double frequency)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    constexpr int rate = 44100, frames = rate * 20, bytes = frames * 4;
    QDataStream out(&file); out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData("RIFF",4); out << quint32(bytes+36);
    out.writeRawData("WAVEfmt ",8); out << quint32(16) << quint16(1) << quint16(2)
        << quint32(rate) << quint32(rate*4) << quint16(4) << quint16(16);
    out.writeRawData("data",4); out << quint32(bytes);
    for (int i=0;i<frames;++i) {
        const qint16 sample = qint16(4000 * std::sin(6.283185307179586 * frequency * i / rate));
        out << sample << sample;
    }
    return out.status() == QDataStream::Ok;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc,argv);
    if (app.arguments().size()!=2) { std::fprintf(stderr,"usage: music_seed <workDir>\n"); return 1; }
    const QString root=QDir(app.arguments().at(1)).absolutePath();
    const QString album=QStringLiteral("Tankorent Test Artist/Tankorent Test Album/");
    if (!QDir().mkpath(root+QLatin1Char('/')+album)) return 1;
    lt::file_storage storage;
    const QStringList names{QStringLiteral("01 First Tone.wav"),QStringLiteral("02 Second Tone.wav")};
    for (int i=0;i<names.size();++i) {
        const QString relative=album+names.at(i), path=root+QLatin1Char('/')+relative;
        if (!writeTone(path, i==0 ? 440 : 660)) return 1;
        storage.add_file(relative.toStdString(), QFileInfo(path).size());
    }
    // A real compact HTTP tracker avoids dependence on x.pe support in the
    // receiving runtime. Only advertise this loopback seeder, never public peers.
    QTcpServer tracker;
    if (!tracker.listen(QHostAddress::LocalHost,49022)) return 1;
    QObject::connect(&tracker,&QTcpServer::newConnection,&app,[&] {
        while (auto* socket=tracker.nextPendingConnection()) {
            QObject::connect(socket,&QTcpSocket::readyRead,socket,[socket] {
                QByteArray request=socket->property("request").toByteArray()+socket->readAll();
                socket->setProperty("request",request);
                if (!request.contains("\r\n\r\n")) return;
                std::fprintf(stderr,"TRACKER %s\n",request.left(request.indexOf("\r\n")).constData()); std::fflush(stderr);
                const bool seedAnnounce=request.left(request.indexOf("\r\n")).contains("&port=49021&");
                QByteArray body(seedAnnounce ? "d8:intervali5e5:peers0:" : "d8:intervali5e5:peers6:");
                const unsigned char peer[]={127,0,0,1,0xbf,0x7d}; // 49021
                if (!seedAnnounce) body.append(reinterpret_cast<const char*>(peer),6);
                body.append('e');
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\nContent-Length: "+QByteArray::number(body.size())+"\r\n\r\n"+body);
                socket->disconnectFromHost();
            });
            QObject::connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
        }
    });
    lt::create_torrent creator(storage,16*1024);
    creator.add_tracker("http://127.0.0.1:49022/announce");
    lt::set_piece_hashes(creator,root.toStdString());
    std::vector<char> encoded; lt::bencode(std::back_inserter(encoded),creator.generate());
    QFile torrent(root+QStringLiteral("/music.torrent"));
    if (!torrent.open(QIODevice::WriteOnly) || torrent.write(encoded.data(),qint64(encoded.size())) != qint64(encoded.size())) return 1;
    torrent.close();
    auto info=std::make_shared<lt::torrent_info>(encoded.data(),int(encoded.size()));
    lt::settings_pack settings;
    settings.set_str(lt::settings_pack::listen_interfaces,"127.0.0.1:49021");
    settings.set_bool(lt::settings_pack::enable_dht,false);
    settings.set_bool(lt::settings_pack::enable_upnp,false);
    settings.set_bool(lt::settings_pack::enable_natpmp,false);
    settings.set_bool(lt::settings_pack::allow_multiple_connections_per_ip,true);
    settings.set_int(lt::settings_pack::alert_mask, int(lt::alert_category::all));
    lt::session session(settings);
    lt::add_torrent_params params; params.ti=info; params.save_path=root.toStdString();
    params.flags |= lt::torrent_flags::seed_mode;
    params.flags &= ~lt::torrent_flags::paused;
    params.flags &= ~lt::torrent_flags::auto_managed;
    lt::error_code error; const auto handle=session.add_torrent(std::move(params),error);
    if (error) { std::fprintf(stderr,"%s\n",error.message().c_str()); return 1; }
    std::printf("READY %s&x.pe=127.0.0.1:49021\nFILE_INDEX 0\nPATH %s\n",lt::make_magnet_uri(*info).c_str(),(album+names.first()).toUtf8().constData());
    std::fflush(stdout);
    QTimer diagnostics;
    QObject::connect(&diagnostics,&QTimer::timeout,&app,[&] {
        std::vector<lt::alert*> alerts; session.pop_alerts(&alerts);
        for (const auto* alert : alerts) std::fprintf(stderr,"ALERT %s\n",alert->message().c_str());
        const auto status=handle.status();
        std::fprintf(stderr,"STATUS state=%d paused=%d seeding=%d peers=%d uploaded=%lld error=%s\n",int(status.state),bool(status.flags & lt::torrent_flags::paused),status.is_seeding,status.num_peers,static_cast<long long>(status.total_upload),status.errc.message().c_str());
        std::fflush(stderr);
    });
    diagnostics.start(2000);
    QTimer::singleShot(10*60*1000,&app,&QCoreApplication::quit);
    return app.exec();
}
