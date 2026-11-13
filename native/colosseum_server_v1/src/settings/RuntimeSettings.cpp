#include "server1/settings/RuntimeSettings.h"
namespace server1::settings {
policy::Value runtimeEngineDefaults(const SettingsStore &settings,const std::string &hash,bool noCache) {
    using policy::Value;
    auto number=[&](const char *key,double fallback){const auto *v=settings.find(key);return v && policy::isPositiveInteger(*v)?*v:Value::number(fallback);};
    Value::Array sources;
    const char *trackers[]={
        "udp://zer0day.ch:1337/announce", "udp://tracker.publictracker.xyz:6969/announce", "udp://tracker.opentrackr.org:1337/announce", "udp://open.demonii.com:1337/announce", "udp://open.stealth.si:80/announce", "udp://tracker2.dler.org:80/announce", "udp://tracker.torrent.eu.org:451/announce", "udp://tracker.qu.ax:6969/announce", "udp://tracker.dler.org:6969/announce", "udp://tracker.auctor.tv:6969/announce", "udp://tracker-udp.gbitt.info:80/announce", "udp://torrentclub.online:54123/announce", "udp://torrentclub.online:1984/announce", "udp://t.overflow.biz:6969/announce", "udp://retracker01-msk-virt.corbina.net:80/announce", "udp://ipv4announce.sktorrent.eu:6969/announce", "udp://explodie.org:6969/announce", "udp://exodus.desync.com:6969/announce", "udp://bittorrent-tracker.e-n-c-r-y-p-t.net:1337/announce", "https://tracker.zhuqiy.com:443/announce"};
    for(const auto *tracker:trackers) sources.push_back(Value::string(std::string("tracker:")+tracker));
    sources.push_back(Value::string("dht:"+hash));
    auto minPeers=number("btMinPeersForStable",5);
    Value result=Value::object({
        {"peerSearch",Value::object({{"min",Value::number(40)},{"max",Value::number(150)},{"sources",Value::array(std::move(sources))}})},
        {"dht",Value::boolean(false)},{"tracker",Value::boolean(false)},
        {"connections",number("btMaxConnections",35)},
        {"handshakeTimeout",number("btHandshakeTimeout",20000)},
        {"timeout",number("btRequestTimeout",4000)}, {"virtual",Value::boolean(true)},
        {"swarmCap",Value::object({{"minPeers",minPeers},{"maxSpeed",number("btDownloadSpeedSoftLimit",1677721.6)}})},
        {"growler",Value::object({{"flood",Value::number(0)},{"pulse",number("btDownloadSpeedHardLimit",2621440)}})}});
    const auto *cache=settings.find("cacheSize");
    // Source uses loose equality: 0 == settings.cacheSize.
    if(noCache || (cache && !cache->isNull() && !cache->isMissing() && policy::jsNumber(*cache)==0)) result=policy::shallowExtend(result,Value::object({
        {"buffer",Value::number(15728640)},
        {"circularBuffer",Value::object({{"type",Value::string("memory")},{"size",Value::number(47185920)}})},
        {"swarmCap",Value::object({{"minPeers",minPeers},{"maxBuffer",Value::number(.75)}})}}));
    return result;
}
}
