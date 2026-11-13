#pragma once
#include "server1/archive/ZipSource.h"
namespace server1::archive {
struct TarEntry {
 QString name,type,linkname; qint64 size=0,offset=0,dataOffset=0;
 // Streaming M1304 reports a different dataOffset from its actual stream cursor.
 qint64 streamOffset=0; QJsonObject pax; bool streaming=false;
};
class TarSource : public QObject {
public:
 using Read=ZipSource::Read; using Cancel=ZipSource::Cancel;
 using Predicate=std::function<bool(const TarEntry&)>;
 using Completion=std::function<void(TarEntry,QString)>;
 // streaming=true accepts a forward-only decompressed source (length=-1).
 TarSource(qint64 length,Read read,Cancel cancel,QObject *parent=nullptr,bool streaming=false);
 ~TarSource() override;
 void find(Predicate predicate,Completion completion);
 void cancel();
private: struct State;std::shared_ptr<State> state_;
};
class TarEntryReader : public QObject {
public:
 using Completion=std::function<void(ZipChunk)>;
 TarEntryReader(TarEntry entry,ZipSource::Read read,ZipSource::Cancel cancel,
                QObject *parent=nullptr,qint64 start=0,qint64 end=-1);
 ~TarEntryReader() override;
 void pull(qint64 maximum,Completion completion);
 void cancel();
private:struct State;std::shared_ptr<State> state_;
};
}
