#pragma once
#include "server1/archive/ArchiveSources.h"
namespace server1::archive {
struct ZipEntry {
 QString name; quint16 flags=0,method=0; quint32 crc=0; qint64 compressedSize=0,size=0,localOffset=0,dataOffset=-1;
};
struct ZipDirectory { QList<ZipEntry> entries; QString error; };
struct ZipRangePlan {
 int status=200; qint64 start=0,end=-1,contentLength=0,sourceStart=0,sourceEnd=0;QString contentRange,error;bool head=false,acceptRanges=false;
};
ZipRangePlan zipRangePlan(const ZipEntry &entry,const QString &method,const QString &range);
// Async lazy metadata traversal, no archive/body buffering. read callbacks must
// run asynchronously on the owner Qt lane; cancellation suppresses completion.
class ZipSource : public QObject {
public:
 using Read = std::function<quint64(qint64,qint64,RemoteByteSource::Completion)>;
 using Cancel = std::function<void(quint64)>;
 using Completion = std::function<void(ZipDirectory)>;
 ZipSource(qint64 length, Read read, Cancel cancel, QObject *parent=nullptr);
 ~ZipSource() override;
 void open(Completion completion);
 void localEntry(ZipEntry entry,std::function<void(ZipEntry,QString)> completion);
 void cancel();
private:
 struct State;std::shared_ptr<State> state_;
};
struct ZipChunk { QByteArray bytes; QString error; bool eof=false; };
class ZipEntryReader : public QObject {
public:
 using Completion=std::function<void(ZipChunk)>;
 ZipEntryReader(ZipEntry entry, ZipSource::Read read, ZipSource::Cancel cancel, QObject *parent=nullptr);
 ~ZipEntryReader() override;
 // Exactly one outstanding pull; no source read before pull. Bounded output and
 // 64 KiB compressed input. Entry must have resolved local dataOffset.
 void pull(qint64 maximumBytes,Completion completion);
 void cancel();
private:
 struct State;std::shared_ptr<State> state_;
};
}
