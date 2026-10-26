#pragma once
#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <memory>
namespace server1::downloads {
// M1284: constructing the manager restores downloads even when HTTP routes are disabled.
class DownloadManager final: public QObject {
public:
 explicit DownloadManager(const QString& appPath, QObject* parent=nullptr);
 ~DownloadManager() override;
 QJsonArray all() const;
 QJsonObject get(const QString& id) const;
 QJsonObject add(const QString& url);
 QString pause(const QString& id);
 QString resume(const QString& id);
 QString remove(const QString& id);
 QString filePath(const QString& id) const;
private:
 struct Impl; std::unique_ptr<Impl> d;
};
}
