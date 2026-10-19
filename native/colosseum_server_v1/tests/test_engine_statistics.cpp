#include "server1/policy/EngineStatistics.h"
#include "server1/policy/EngineRegistry.h"
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <thread>
#include <mutex>
#include <algorithm>
using namespace server1::policy;
using namespace server1::ports;
namespace {
void check(bool yes,const char *message) {if(!yes)throw std::runtime_error(message);}
EngineStatisticsSnapshot fixture() {
    EngineStatisticsSnapshot s;
    s.infoHash=std::string(40,'a');s.name="bundle";
    s.files=std::vector<TorrentFile>{{"bundle/a","a",3,0},{"bundle/b","b",3,3},{"bundle/empty","empty",0,6}};
    s.pieceLength=4;s.availablePieces={0,1};
    s.peers=3;s.unchoked=2;s.queued=7;s.unique=11;s.connectionTries=13;
    s.swarmConnections=4;s.swarmSize=Value::number(55);s.swarmPaused=true;
    s.downloaded=12345;s.uploaded=6789;s.downloadSpeed=23.5;
    s.selections=Value::array({Value::object({{"from",Value::number(0)},{"to",Value::number(1)},
        {"offset",Value::number(0)},{"priority",Value::number(1)}})});
    s.wires=Value::array({Value::object({{"requests",Value::number(3)},{"address",Value::string("127.0.0.1:1")},
        {"amInterested",Value::boolean(true)},{"isSeeder",Value::boolean(false)},
        {"downSpeed",Value::number(7)},{"upSpeed",Value::number(9)}}),
        Value::object({{"requests",Value::number(0)},{"address",Value::null()},
        {"amInterested",Value::boolean(false)},{"isSeeder",Value::boolean(false)},
        {"downSpeed",Value::number(0)},{"upSpeed",Value::number(0)}})});
    return s;
}
struct State { std::mutex mutex; TransportStatistics stats; std::vector<TorrentAction> actions; std::vector<TorrentObservation> observations; };
class Transport final:public TorrentTransport {
    std::shared_ptr<State> state_;
public:
    explicit Transport(std::shared_ptr<State> s):state_(std::move(s)){}
    bool submit(const TorrentAction &action) override{std::lock_guard<std::mutex> lock(state_->mutex);state_->actions.push_back(action);return true;}
    std::vector<TorrentObservation> poll()override{std::lock_guard<std::mutex> lock(state_->mutex);auto result=std::move(state_->observations);state_->observations.clear();return result;}
    TransportStatistics statistics()const override{std::lock_guard<std::mutex> lock(state_->mutex);return state_->stats;}
    void close()override{}
protected:bool applyAutonomySuppression()override{return true;}
};
class Timer final : public EngineTimer {
public:
    explicit Timer(std::shared_ptr<int> cancels):cancels_(std::move(cancels)){}
    ~Timer() override {cancel();}
    void cancel() noexcept override{if(active_){active_=false;++*cancels_;}}
    bool active()const noexcept override{return active_;}
    std::uint64_t id()const noexcept override{return 75;}
private:std::shared_ptr<int> cancels_;bool active_=true;
};
void lifecycleTest() {
    auto state=std::make_shared<State>();std::uint64_t now=0;
    EngineRegistryConfig config;config.monotonicClock=[&]{return now;};
    config.speedometerTick=[] {return std::uint16_t{0};};
    config.transportFactory=[state](const auto&){return std::make_unique<Transport>(state);};
    EngineRegistry registry(config);const std::string hash(40,'c');
    registry.create({hash,Value::object({{"peerSearch",Value::boolean(false)},{"connections",Value::number(1)}})});
    for(int i=0;i<100;++i){registry.dispatch();registry.poll();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    auto engine=registry.get(hash);check(bool(engine),"lifecycle engine missing");
    auto settle=[&]{for(int i=0;i<10;++i){registry.dispatch();registry.poll();std::this_thread::sleep_for(std::chrono::milliseconds(1));}};
    auto event=[&](PeerHandle peer,std::uint64_t identity,PeerLifecycleKind kind,EngineGeneration generation=0){
        {std::lock_guard<std::mutex> lock(state->mutex);state->observations.emplace_back(PeerLifecycleObservation{generation?generation:engine->generation(),peer,identity,kind});}settle();
    };
    auto expectCounts=[&](int queued,int unique,int tries,int connections){
        auto value=registry.statistics(hash);
        check(value.find("queued")->asNumber()==queued,"source lifecycle queue mismatch");
        check(value.find("unique")->asNumber()==unique,"source retained endpoint mismatch");
        check(value.find("connectionTries")->asNumber()==tries,"source admission attempts mismatch");
        check(value.find("swarmConnections")->asNumber()==connections,"source active connection mismatch");
    };
    check(engine->connectSourcePeer(1,"127.0.0.1",101),"first source admission");
    check(engine->connectSourcePeer(2,"127.0.0.1",102),"second source admission");
    check(engine->connectSourcePeer(3,"127.0.0.1",103),"third source admission");settle();
    expectCounts(2,3,1,1);
    event(1,11,PeerLifecycleKind::Started);event(1,11,PeerLifecycleKind::Connected);event(1,11,PeerLifecycleKind::Ready);
    event(1,11,PeerLifecycleKind::Disconnected);expectCounts(1,3,2,1);
    check(!engine->connectSourcePeer(4,"127.0.0.1",101),"retry-retained endpoint duplicate admitted");
    event(2,22,PeerLifecycleKind::Started);event(2,22,PeerLifecycleKind::Connected);
    now=4000;settle();expectCounts(2,3,2,1);
    event(2,22,PeerLifecycleKind::Disconnected);expectCounts(1,2,3,1);
    check(engine->connectSourcePeer(4,"127.0.0.1",102),"failed unhandshaken endpoint could not be rediscovered");
    expectCounts(2,3,3,1);
    event(3,33,PeerLifecycleKind::Started);
    event(3,33,PeerLifecycleKind::Disconnected,engine->generation()+1);expectCounts(2,3,3,1);
    now=6999;settle();
    auto disconnectCount=[&]{std::lock_guard<std::mutex> lock(state->mutex);return std::count_if(state->actions.begin(),state->actions.end(),[](const auto&a){return std::holds_alternative<DisconnectAction>(a);});};
    check(disconnectCount()==0,"connect timeout fired before 3000ms");
    now=7000;settle();check(disconnectCount()==1,"source timeout did not submit native disconnect");
    event(3,999,PeerLifecycleKind::Disconnected);expectCounts(2,3,3,1);
    event(3,33,PeerLifecycleKind::Disconnected);expectCounts(1,2,4,1);
    {std::lock_guard<std::mutex> lock(state->mutex);const auto &action=*std::find_if(state->actions.begin(),state->actions.end(),[](const auto&a){return std::holds_alternative<DisconnectAction>(a);});check(std::get<DisconnectAction>(action).connectionIdentity==33,"timeout not identity-bound");}
    registry.remove(hash);registry.dispatch();now=100000;settle();
    check(registry.statistics(hash).isNull(),"removed source lifecycle survived teardown");
}
void clockTest() {
    auto cancels=std::make_shared<int>(0);std::vector<std::uint64_t> intervals;
    EngineRegistryConfig config;
    config.repeat=[&](std::uint64_t ms,EngineContinuation){
        intervals.push_back(ms);return std::make_shared<Timer>(cancels);
    };
    {EngineRegistry registry(config);check(intervals==std::vector<std::uint64_t>{250},
        "registry must own one shared quarter-second M416 clock");}
    check(*cancels==1,"shared clock must retire with registry");
}
void liveTest() {
    auto state=std::make_shared<State>();
    state->stats.connectedPeers=2;state->stats.unchokedPeers=1;
    state->stats.wirePayloadBytesReceived=120;state->stats.downloadedBytes=50;
    state->stats.uploadedBytes=31;state->stats.downloadBytesPerSecond=900;
    state->stats.connectionAttempts=6;state->stats.queuedConnections=3;state->stats.knownPeerEndpoints=8;
    PeerTransferStatistics wire;wire.peer=4;wire.connectionIdentity=8;wire.attachmentOrder=1;
    wire.downloadedBytes=100;wire.uploadedBytes=25;wire.address="127.0.0.1:1234";
    wire.peerChoking=false;wire.amInterested=true;wire.requests=2;
    state->stats.peerTransfers={wire};
    std::uint16_t tick=1;
    EngineRegistryConfig c;c.speedometerTick=[&]{return tick;};
    c.transportFactory=[state](const auto&){return std::make_unique<Transport>(state);};
    EngineRegistry registry(c);
    check(registry.statistics(std::string(40,'b')).isNull(),"unknown engine must be null");
    const std::string hash(40,'a');registry.create({hash,Value::object({{"peerSearch",Value::boolean(false)}})});
    bool observed=false;
    for(int n=0;n<500;++n){
        registry.dispatch();registry.poll();
        const auto result=registry.statistics(hash);
        if(!result.isNull() && result.find("downloaded")->asNumber()==120){observed=true;break;}
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    check(observed,"live snapshot did not read transport counts");
    auto result=registry.statistics(hash);
    check(result.find("downloaded")->asNumber()==120,"owned delivery incorrectly substituted for all wire payload");
    check(result.find("uploaded")->asNumber()==31,"native upload count missing");
    check(result.find("downloadSpeed")->asNumber()==120,"source speedometer must not use native 900 rate");
    check(result.find("uploadSpeed")->asNumber()==120,"M172 uploadSpeed oddity lost");
    check(result.find("connectionTries")->asNumber()==0,"native retry counter must not masquerade as source admissions");
    check(result.find("queued")->asNumber()==0,"native deferred queue must not masquerade as source queue");
    check(result.find("name")->isNull()&&result.find("files")->isNull(),"pre-metadata nulls lost");
    check(jsonStringify(result).find("peerSearchRunning")==std::string::npos,"absent peer search must be omitted");
    const auto &w=result.find("wires")->asArray().at(0);
    check(w.find("downSpeed")->asNumber()==100 && w.find("upSpeed")->asNumber()==25,"wire source meter totals wrong");
    check(w.find("isSeeder")->isMissing(),"isSeeder absent before ontorrent");
    check(registry.statistics(hash,Value::string("bad")).find("wires")->isNull(),"present invalid index still nulls wires");
    tick=30;result=registry.statistics(hash);
    check(result.find("downloadSpeed")->asNumber()==0,"idle source meter must decay");
    check(!registry.prewarmStream(hash,0),"cannot prewarm unready metadata");
    registry.remove(hash);registry.dispatch();
    check(registry.statistics(hash).isNull(),"removed engine must be null");
}
}
int main(int argc,char **argv){try{
    const auto s=fixture();
    if(argc>1 && std::string(argv[1])=="--goldens") {
        std::vector<Value> indices{Value::missing(),Value::number(0),Value::number(1),Value::number(2),
            Value::number(3),Value::number(-1),Value::string("0"),Value::string("1"),Value::string("01"),
            Value::string("bad"),Value::null(),Value::boolean(false),Value::string("")};
        for(const auto &idx:indices)std::cout<<jsonStringify(projectEngineStatistics(s,idx))<<'\n';
        return 0;
    }
    const auto plain=projectEngineStatistics(s);
    check(plain.find("wires")->kind()==Value::Kind::Array,"unindexed wires array");
    check(!plain.find("streamProgress"),"unindexed stream fields absent");
    const auto cross=projectEngineStatistics(s,Value::string("1"));
    check(cross.find("wires")->isNull(),"indexed wires null");
    check(cross.find("streamProgress")->asNumber()==2,"cross-boundary progress >1 source oddity");
    check(!projectEngineStatistics(s,Value::string("01")).find("streamLen"),"array key 01 must not coerce");
    check(!projectEngineStatistics(s,Value::null()).find("streamLen"),"array key null must not coerce");
    check(jsonStringify(projectEngineStatistics(s,Value::number(2))).find("\"streamProgress\":null")!=std::string::npos,"zero length nonfinite progress must serialize null");
    clockTest();liveTest();lifecycleTest();std::cout<<"K12 statistics projection and live snapshot passed\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
