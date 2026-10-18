#include "server1/policy/EngineCounters.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>
#include <thread>
#include <map>
#include <stdexcept>
using namespace server1::policy;
struct Clock;
struct Timer final : EngineTimer {
    Clock *clock;
    std::uint64_t token;
    std::uint64_t due;
    EngineContinuation callback;
    bool enabled=true;
    Timer(Clock *c, std::uint64_t t, std::uint64_t d, EngineContinuation cb)
        : clock(c), token(t), due(d), callback(std::move(cb)) {}
    void cancel() noexcept override;
    bool active() const noexcept override {return enabled;}
    std::uint64_t id() const noexcept override {return token;}
};
struct Clock {
    std::uint64_t now=0, serial=0;
    QJsonArray trace;
    std::map<std::uint64_t,std::shared_ptr<Timer>> timers;
    std::shared_ptr<EngineTimer> after(std::uint64_t delay, EngineContinuation cb) {
        const auto token=++serial;
        auto timer=std::make_shared<Timer>(this,token,now+delay,std::move(cb));
        timers[token]=timer;
        trace.append(QJsonArray{"set",qint64(token),qint64(timer->due)});
        return timer;
    }
    void advance(std::uint64_t delay) {
        const auto target=now+delay;
        int guard=0;
        for (;;) {
            auto next=timers.end();
            for(auto it=timers.begin();it!=timers.end();++it)
                if(it->second->enabled && it->second->due<=target &&
                   (next==timers.end() || it->second->due<next->second->due)) next=it;
            if(next==timers.end())break;
            if(++guard>=100)throw std::runtime_error("timer runaway");
            auto timer=next->second;timers.erase(next);timer->enabled=false;
            now=timer->due;trace.append(QJsonArray{"fire",qint64(timer->token)});
            timer->callback();
        }
        now=target;
    }
};
void Timer::cancel() noexcept {
    enabled=false;clock->timers.erase(token);
    clock->trace.append(QJsonArray{"clear",qint64(token)});
}
QJsonArray run(const QJsonObject &c) {
    Clock clock;
    bool reentered=false, thrown=false;
    std::uint64_t delay=10;
    std::unique_ptr<EngineCounter> counter;
    std::function<void(const QJsonArray &)> act;
    act=[&](const QJsonArray &op) {
        const auto kind=op[0].toString();
        if(kind=="wait") clock.advance(op[1].toInteger());
        else if(kind=="delay")delay=op[1].toInteger();
        else {
            CounterEvent event{op[1].toString("a").toStdString(),{}};
            if(op.size()>2 && !op[2].isNull())event.index=op[2].toInteger();
            if(kind=="open")counter->increment(event);
            else if(kind=="close")counter->decrement(event);
            else throw std::runtime_error("unknown operation");
        }
    };
    auto notify=[&](const char *kind,const CounterEvent &event) {
        clock.trace.append(QJsonArray{kind,qint64(clock.now),QString::fromStdString(event.hash),
            event.index?QJsonValue(qint64(*event.index)):QJsonValue(QJsonValue::Null)});
        if(!thrown && c["throw"].toString()==kind){thrown=true;throw std::runtime_error("callback throw");}
        const auto reenter=c["reenter"].toObject();
        if(!reentered && reenter["when"].toString()==kind) {
            reentered=true;
            for(const auto op:reenter["ops"].toArray())act(op.toArray());
        }
    };
    EngineCounterConfig config;
    config.key=[&](const CounterEvent &e){return c["hashOnly"].toBool()?e.hash:
        e.hash+":"+(e.index?std::to_string(*e.index):"undefined");};
    config.onPositive=[&](const CounterEvent &e){notify("positive",e);};
    config.onZero=[&](const CounterEvent &e){notify("zero",e);};
    config.timeoutMs=[&]{return delay;};
    config.after=[&](auto d,auto cb){return clock.after(d,std::move(cb));};
    counter=std::make_unique<EngineCounter>(std::move(config));
    for(const auto op:c["ops"].toArray()) {
        try {act(op.toArray());}
        catch(const std::exception &e){clock.trace.append(QJsonArray{"error",e.what()});}
    }
    // Snapshot precedes native-only teardown, which has no oracle counterpart.
    return clock.trace;
}
void require(bool ok,const char *message) {if(!ok)throw std::runtime_error(message);}
int ownership() {
    int passed=0;
    for(int which=0;which<10;++which) {
        Clock clock;
        int positive=0, zero=0;
        bool didReenter=false;
        std::unique_ptr<EngineCounter> counter;
        EngineCounterConfig config;
        config.key=[&](const CounterEvent &e){if(which==8)counter.reset();return e.hash;};
        config.onPositive=[&](const CounterEvent &){++positive;if(which==1)counter.reset();};
        config.onZero=[&](const CounterEvent &e){
            ++zero;
            if(which==2)counter.reset();
            if(which==3 && !didReenter) {
                didReenter=true;counter->increment(e);counter->decrement(e);
            }
        };
        config.timeoutMs=[&]{if(which==9)counter.reset();return 10;};
        bool reject=which==7;
        config.after=[&](auto delay,auto cb)->std::shared_ptr<EngineTimer>{
            if(which==6)cb();
            if(reject){reject=false;return {};}
            return clock.after(delay,std::move(cb));
        };
        counter=std::make_unique<EngineCounter>(std::move(config));
        const CounterEvent event{"a",0};
        counter->increment(event);
        if(which==8) {require(!counter && positive==0,"key deletion must stop callbacks");++passed;continue;}
        if(which==9) {counter->decrement(event);require(!counter && clock.timers.empty(),"timeout deletion must stop scheduling");++passed;continue;}
        if(which==1) {require(!counter && positive==1,"delete during positive");++passed;continue;}
        if(which==5) {
            bool rejected=false;
            std::thread wrong([&]{try {counter->increment(event);}catch(const std::logic_error &){rejected=true;}});
            wrong.join();require(rejected && positive==1,"foreign lane rejected");++passed;continue;
        }
        if(which==6) {
            bool rejected=false;
            try {counter->decrement(event);}catch(const std::logic_error &){rejected=true;}
            require(rejected && zero==0,"inline scheduling must reject before callback");++passed;continue;
        }
        if(which==7) {
            bool rejected=false;
            try {counter->decrement(event);}catch(const std::runtime_error &){rejected=true;}
            require(rejected,"null scheduler must reject");
            counter->increment(event);counter->decrement(event);clock.advance(10);
            require(zero==1,"scheduler failure recovery");++passed;continue;
        }
        counter->decrement(event);
        auto queued=clock.timers.begin()->second->callback;
        if(which==0) {
            counter.reset();queued();clock.advance(20);
            require(zero==0 && clock.timers.empty(),"destruction cancels queued callback");
        }else if(which==2) {
            clock.advance(10);queued();require(zero==1 && !counter,"delete during zero");
        }else if(which==3) {
            clock.advance(10);require(clock.timers.size()==1,"reentrant timer retained");
            auto ghost=clock.timers.begin()->second->callback;
            counter.reset();ghost();clock.advance(20);
            require(zero==1 && clock.timers.empty(),"destruction cancels orphaned timer");
        }else if(which==4) {
            counter->increment(event);queued();clock.advance(20);
            require(zero==0,"canceled timer late callback ignored");
        }
        ++passed;
    }
    std::cout<<"native ownership: "<<passed<<" passed, 0 failed\n";
    return 0;
}
int main(int argc,char **argv) {
    try {
        if(argc==2 && std::string(argv[1])=="--ownership")return ownership();
        if(argc!=2)throw std::runtime_error("expected cases file");
        QFile file(QString::fromLocal8Bit(argv[1]));
        if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("cannot open cases");
        QJsonParseError error;
        const auto document=QJsonDocument::fromJson(file.readAll(),&error);
        if(error.error!=QJsonParseError::NoError || !document.isArray())throw std::runtime_error("invalid cases");
        QJsonArray output;
        for(const auto value:document.array()) {
            const auto c=value.toObject();
            output.append(QJsonObject{{"name",c["name"]},{"trace",run(c)}});
        }
        std::cout<<QJsonDocument(output).toJson(QJsonDocument::Compact).constData()<<'\n';
    }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
