#pragma once
#include "server1/archive/ArchiveSources.h"
namespace server1::archive {
struct FtpMetadata { qint64 size=-1; bool supportsSeek=false; QString path,lastModified,error; };
// Passive, bounded FTP reads on the Qt owner lane. Contiguous reads reuse one
// transfer with bounded socket buffering; noncontiguous reads reconnect.
// Cancellation/destruction suppress callbacks and close both lanes.
class FtpSource final : public QObject {
public:
 using Token=quint64;
 explicit FtpSource(QUrl url,QObject*parent=nullptr);
 ~FtpSource() override;
 Token inspect(std::function<void(FtpMetadata)> completion);
 Token read(qint64 offset,qint64 count,RemoteByteSource::Completion completion,bool useRest=true);
 void cancel(Token token);
 int pendingRequests() const;
private:
 struct State;std::shared_ptr<State> state_;
};
struct FtpRangePlan {int status=200;qint64 start=0,length=0;QString contentRange;bool acceptRanges=false;};
FtpRangePlan ftpRangePlan(qint64 size,bool supportsSeek,const QString&range);
}
