#pragma once
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <functional>
#include <memory>
namespace server1::youtube {
struct Result { QJsonObject format; QString error; };
// Native implementation of the pinned M936/M470/M473 playback-format path.
// Fetch injection supplies ordinary HTTP response bytes, never resolved formats.
class YouTubeResolver : public QObject {
public:
    struct Page { QByteArray body; QString error; int status=200; };
    using PageCallback=std::function<void(Page)>;
    using Fetch=std::function<void(const QUrl &,PageCallback)>;
    using Callback=std::function<void(Result)>;
    explicit YouTubeResolver(QObject *parent=nullptr,Fetch fetch={});
    ~YouTubeResolver() override;
    void resolve(const QString &id,Callback callback);
    static QStringList extractActions(const QString &script);
    static QString decipher(const QStringList &actions,const QString &signature);
    static QJsonObject addFormatMeta(QJsonObject format);
    static Result chooseFormat(QJsonArray formats);
private:
    struct State;
    std::shared_ptr<State> state_;
};
}
