#include "server1/policy/QtEngineTimer.h"
#include "server1/policy/EngineSpeedometerClock.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QObject>
#include <QThread>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace server1::policy;
void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
void spin(int ms){QElapsedTimer timer;timer.start();while(timer.elapsed()<ms){QCoreApplication::processEvents(QEventLoop::AllEvents,5);QThread::msleep(1);}}
struct ManualTimer final : EngineTimer {
    bool live=true;
    void cancel() noexcept override {live=false;}
    bool active() const noexcept override{return live;}
    std::uint64_t id() const noexcept override{return 1;}
};
struct CopyGuard {
    std::shared_ptr<bool> armed;
    int *calls;
    CopyGuard(std::shared_ptr<bool> a,int *c):armed(std::move(a)),calls(c){}
    CopyGuard(const CopyGuard &other):armed(other.armed),calls(other.calls){
        if(*armed)throw std::runtime_error("unexpected callback copy");
    }
    void operator()(){++*calls;}
};
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);
    try {
        auto owner=std::make_unique<QObject>();
        std::vector<std::string> errors;
        auto onError=[&](std::exception_ptr error){try{std::rethrow_exception(error);}catch(const std::exception&e){errors.push_back(e.what());}};
        auto after=qtEngineAfter(owner.get(),onError);
        int calls=0;const auto lane=QThread::currentThread();
        auto timer=after(5,[&]{require(QThread::currentThread()==lane,"timer escaped app lane");++calls;});
        require(timer && timer->active() && calls==0,"one-shot timer was not scheduled asynchronously");
        spin(30);require(calls==1 && !timer->active(),"one-shot did not fire exactly once");
        auto canceled=after(5,[&]{++calls;});canceled->cancel();spin(20);
        require(calls==1 && !canceled->active(),"canceled timer fired");
        auto abandoned=after(5,[&]{++calls;});abandoned.reset();spin(20);
        require(calls==1,"destroyed handle fired");
        auto deadOwner=after(5,[&]{++calls;});owner.reset();spin(20);
        require(calls==1 && !deadOwner->active(),"destroyed context delivered");
        require(!after(5,[]{}),"dead context accepted timer");
        QObject live;
        auto liveAfter=qtEngineAfter(&live,onError);
        std::shared_ptr<EngineTimer> selfDeleting;
        selfDeleting=liveAfter(0,[&]{++calls;selfDeleting.reset();});
        require(calls==1,"zero delay delivered inline");spin(20);
        require(calls==2 && !selfDeleting,"self-deleting callback failed");
        std::unique_ptr<EngineCounter> counter;
        int positive=0,zero=0;
        counter=std::make_unique<EngineCounter>(EngineCounterConfig{
            [](const CounterEvent&e){return e.hash;},[&](const auto&){++positive;},
            [&](const auto&){++zero;counter.reset();},[]{return 5;},liveAfter});
        counter->increment({"a",0});counter->decrement({"a",0});spin(30);
        require(positive==1 && zero==1 && !counter,"counter did not run with real app-lane timer");
        auto throwing=liveAfter(0,[]{throw std::runtime_error("timer callback failure");});
        spin(20);require(errors==std::vector<std::string>{"timer callback failure"},"timer callback error was not reported");
        require(!throwing->active(),"throwing callback timer remained active");
        auto foreign=liveAfter(100,[&]{++calls;});
        std::thread destroyer([&]{foreign.reset();});destroyer.join();spin(120);
        require(calls==2,"foreign-thread handle destruction delivered callback");
        counter=std::make_unique<EngineCounter>(EngineCounterConfig{
            [](const CounterEvent&e){return e.hash;},[](const auto&){},
            [&](const auto&){++zero;},[]{return 100;},liveAfter});
        counter->increment({"b",0});counter->decrement({"b",0});
        std::thread counterDestroyer([&]{counter.reset();});counterDestroyer.join();spin(120);
        require(zero==1,"foreign-thread counter destruction delivered callback");
        auto repeat=qtEngineRepeat(&live,onError);
        int repeats=0;
        auto repeating=repeat(5,[&]{require(QThread::currentThread()==lane,"repeat escaped app lane");++repeats;});
        require(repeating && repeats==0,"repeat delivered inline");
        spin(35);require(repeats>=2 && repeating->active(),"repeat did not remain live");
        repeating->cancel();const auto stopped=repeats;spin(20);
        require(repeats==stopped && !repeating->active(),"canceled repeat delivered");
        bool zeroRejected=false;try{repeat(0,[]{});}catch(const std::invalid_argument&){zeroRejected=true;}
        require(zeroRejected,"zero repeating interval accepted");
        auto repeatOwner=std::make_unique<QObject>();
        auto ownedRepeat=qtEngineRepeat(repeatOwner.get(),onError)(5,[&]{++repeats;});
        repeatOwner.reset();spin(20);require(repeats==stopped && !ownedRepeat->active(),"dead repeat owner delivered");
        std::shared_ptr<EngineTimer> selfRepeat;
        int selfCalls=0;selfRepeat=repeat(5,[&]{++selfCalls;selfRepeat.reset();});
        spin(25);require(selfCalls==1 && !selfRepeat,"repeat self destruction failed");
        auto failingRepeat=repeat(5,[]{throw std::runtime_error("repeat failure");});
        spin(25);require(!failingRepeat->active() && errors.size()==2 && errors.back()=="repeat failure","repeat failure was not terminal");
        auto foreignRepeat=repeat(5,[&]{++repeats;});
        std::thread foreignRelease([&]{foreignRepeat.reset();});foreignRelease.join();spin(20);
        require(repeats==stopped,"foreign repeat destruction delivered");
        int delayed=0;auto coalesced=repeat(10,[&]{++delayed;});
        QThread::msleep(75);require(delayed==0,"timer ran without app loop");
        QCoreApplication::processEvents();coalesced->cancel();
        require(delayed==1,"overdue repeating timer performed elapsed-time catchup");
        std::vector<int> mutableCalls;
        auto mutableRepeat=repeat(5,[n=0,&mutableCalls]() mutable {mutableCalls.push_back(++n);});
        spin(30);mutableRepeat->cancel();
        require(mutableCalls.size()>=2 && mutableCalls[0]==1 && mutableCalls[1]==2,"repeat discarded callable-local state");
        auto armed=std::make_shared<bool>(false);int guardedCalls=0;
        auto guardedRepeat=repeat(5,CopyGuard(armed,&guardedCalls));*armed=true;
        spin(25);guardedRepeat->cancel();require(guardedCalls>=2,"repeat copied armed callback");
        auto sourceClock=engineSpeedometerClock(repeat);
        auto secondReader=sourceClock;
        require(sourceClock()==1 && secondReader()==1,"source clock initial/shared tick incorrect");
        QThread::msleep(550);require(sourceClock()==1,"source clock synthesized wall-time ticks");
        QCoreApplication::processEvents();require(sourceClock()==2 && secondReader()==2,"source clock missed coalesced delivery");
        std::uint16_t foreignTick=0;std::thread reader([&]{foreignTick=sourceClock();});reader.join();
        require(foreignTick==2,"source clock foreign read failed");
        sourceClock={};require(secondReader()==2,"clock reader copy lost shared lifetime");
        std::thread lastRelease([&]{secondReader={};});lastRelease.join();spin(10);
        bool missingRejected=false;try{engineSpeedometerClock({});}catch(const std::invalid_argument&){missingRejected=true;}
        require(missingRejected,"missing clock repeat accepted");
        bool refusedRejected=false;try{engineSpeedometerClock([](std::uint64_t,EngineContinuation){return std::shared_ptr<EngineTimer>{};});}catch(const std::runtime_error&){refusedRejected=true;}
        require(refusedRejected,"refused clock timer accepted");
        EngineContinuation deliver;auto manual=std::make_shared<ManualTimer>();
        auto wrapping=engineSpeedometerClock([&](std::uint64_t interval,EngineContinuation callback){require(interval==250,"source interval changed");deliver=std::move(callback);return manual;});
        for(int n=0;n<65535;++n)deliver();require(wrapping()==0,"source clock modulo wrap failed");
        deliver();require(wrapping()==1,"source clock post-wrap failed");
        wrapping={};require(!manual->active(),"last clock reader did not cancel");
        deliver();require(!manual->active(),"late clock callback revived dead owner");
        std::cout<<"Qt timer lifecycle: 29 cases PASS\\n";return 0;
    }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
}
