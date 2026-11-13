#pragma once
#include "server1/archive/ZipSource.h"
namespace server1::archive {
QJsonArray orderArchiveStoredUrls(SourceFamily family,const QJsonArray &urls);
struct ArchiveStoredChunk { qint64 offset=0,size=0; };
struct ArchiveStoredEntry { QString name; qint64 length=0; QList<ArchiveStoredChunk> chunks; };
// M1124/M1288 stored-only entry parser. Physical volumes must already be in
// source bundle order. Metadata is bounded; entry data is never cached.
class RarSevenZipSource final : public QObject {
public:
 using Predicate=std::function<bool(const QString&,int)>;
 using Completion=std::function<void(ArchiveStoredEntry,QString)>;
 RarSevenZipSource(SourceFamily family,qint64 length,ZipSource::Read read,ZipSource::Cancel cancel,QObject *parent=nullptr);
 ~RarSevenZipSource() override;
 void setVolumeLengths(QList<qint64> lengths);
 void find(Predicate predicate,Completion completion);
 void cancel();
private:struct State;std::shared_ptr<State> state_;
};
class ArchiveStoredReader final : public QObject {
public:
 using Completion=ZipEntryReader::Completion;
 ArchiveStoredReader(ArchiveStoredEntry entry,ZipSource::Read read,ZipSource::Cancel cancel,QObject *parent=nullptr,qint64 start=0,qint64 end=-1);
 ~ArchiveStoredReader() override;
 void pull(qint64 maximum,Completion completion);
 void cancel();
private:struct State;std::shared_ptr<State> state_;
};
}
