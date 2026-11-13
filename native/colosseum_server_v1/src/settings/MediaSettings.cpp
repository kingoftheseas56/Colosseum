#include "server1/settings/MediaSettings.h"
#include "server1/media/HlsV2Service.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <limits>
namespace server1::settings {
namespace {
int concurrency(const policy::Value *v) {
    // Source performs Array.slice(setting - 1). Preserve that coercion for
    // values supplied by POST, which deliberately does not revalidate settings.
    double start=policy::jsNumber(v?*v:policy::Value::missing())-1;
    if(std::isnan(start))return 1;
    if(start>=std::numeric_limits<int>::max()-1)return std::numeric_limits<int>::max();
    if(start<=std::numeric_limits<int>::min()+1)return std::numeric_limits<int>::min()+2;
    return static_cast<int>(std::trunc(start))+1;
}
}
void applyMediaSettings(const SettingsStore &store,media::HlsV2Service &service,int cpuCores) {
    const auto json=QJsonDocument::fromJson(QByteArray::fromStdString(policy::jsonStringify(store.value()))).object();
    service.setTranscodeSettings(json,std::max(0,cpuCores));
    service.setConcurrency(concurrency(store.find("transcodeConcurrency")),concurrency(store.find("transcodeTrackConcurrency")));
}
}
