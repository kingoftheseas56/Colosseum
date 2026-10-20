#pragma once
#include "server1/ports/ProcessPort.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <functional>
#include <memory>
namespace server1::media {
struct LegacyResponse {
 int status=200;
 QJsonObject headers;
 QByteArray body;
 QString program;
 QStringList arguments;
};
class LegacyOperation : public QObject {
public:
 ~LegacyOperation() override;
 qint64 read(char*,qint64);
 void cancel();
 qsizetype bufferedBytes() const;
 bool running() const;
 std::function<void()> wake;
 struct Impl;
 std::unique_ptr<Impl> impl;
 LegacyOperation();
};
class LegacyHlsService : public QObject {
public:
 using Callback=std::function<void(LegacyResponse)>;
 explicit LegacyHlsService(ExecutablePaths paths, QObject* parent=nullptr);
 ~LegacyHlsService() override;
 // Exact pure legacy middleware factory. input has url/search/params/query/headers.
 static LegacyResponse render(QString action,QJsonObject instance,QJsonObject input,QJsonArray subtitles={},bool hasSplitter=true);
 std::shared_ptr<LegacyOperation> request(QString action,QJsonObject input,Callback callback);
 void receiveSubtitles(QString id,QJsonArray subtitles);
private:
 struct Impl;
 std::unique_ptr<Impl> impl;
};
}
