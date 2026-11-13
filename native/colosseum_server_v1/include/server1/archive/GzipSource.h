#pragma once
#include "server1/archive/ZipSource.h"
namespace server1::archive {
// M563 pipes the multipart body through node zlib Gunzip; M1304 consumes the
// resulting TAR stream forward. No compressed/decompressed whole-body cache.
class GzipReader : public QObject {
public:
 using Completion=std::function<void(ZipChunk)>;
 GzipReader(qint64 length,ZipSource::Read read,ZipSource::Cancel cancel,QObject *parent=nullptr);
 ~GzipReader() override;
 void pull(qint64 maximum,Completion completion);
 // Forward adapter for TarSource streaming=true. Skips are decompressed in
 // bounded chunks, retaining only the requested interval (at most 1 MiB).
 quint64 readForward(qint64 offset,qint64 count,RemoteByteSource::Completion completion);
 void cancel(quint64 token);
 void cancel();
private:struct State;std::shared_ptr<State> state_;
};
}
