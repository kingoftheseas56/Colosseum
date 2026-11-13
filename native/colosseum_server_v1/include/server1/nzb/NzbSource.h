#pragma once
#include "server1/archive/ArchiveSources.h"
namespace server1::nzb {
struct Segment { QString group, article, subject; qint64 bytes=0; };
using File = QList<Segment>;
QList<File> parse(const QByteArray &xml, QString *error=nullptr);
struct Server { QString host,user,password; quint16 port=119; int connections=1; bool tls=false; };
std::optional<Server> parseServer(const QString &url);
struct Decoded { QString filename,error; QByteArray bytes; bool crcMismatch=false; };
// M1115 treats CRC mismatch as diagnostic rather than discarding decoded bytes.
Decoded decodeYenc(const QByteArray &body);
// Shared article subscribers and bounded reusable sockets on the Qt owner lane.
// No TLS implementation; cancel detaches a subscriber, then aborts if it was last.
class ArticleClient final : public QObject {
public:
    using Token=quint64;
    explicit ArticleClient(QObject *parent=nullptr);
    ~ArticleClient() override;
    Token get(const Server &server,const Segment &segment,std::function<void(Decoded)> done);
    void cancel(Token token);
    void setTimeout(int milliseconds);
    void cancelAll();
private:
    struct State;std::shared_ptr<State> state_;
};
// M1088 first/last segment geometry, bounded reads and ordered backbone fallback.
class NzbSource final : public QObject {
public:
    using Token=quint64;
    explicit NzbSource(QObject *parent=nullptr,std::shared_ptr<ArticleClient> client={});
    ~NzbSource() override;
    void open(QList<Server> servers,File file,std::function<void(QString)> done);
    qint64 size() const;
    QString name() const;
    Token read(qint64 offset,qint64 count,archive::RemoteByteSource::Completion done);
    void cancel(Token token);
private:
    struct State;std::shared_ptr<State> state_;
};
}
