#include "server1/policy/QtEngineTimer.h"
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
        std::cout<<"Qt one-shot lifecycle: 10 cases PASS\\n";return 0;
    }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
}
