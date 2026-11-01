#include "server1/policy/EngineSpeedometer.h"
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
using server1::policy::EngineSpeedometer;

int main(int argc,char **argv)
{
    try {
        if(argc==2 && std::string(argv[1])=="--ownership"){
            bool rejected=false;
            try{EngineSpeedometer invalid({});}catch(const std::invalid_argument&){rejected=true;}
            if(!rejected)throw std::runtime_error("empty clock accepted");
            unsigned calls=0;rejected=false;
            try{EngineSpeedometer invalid([&]{++calls;return std::uint16_t{1};},16384);}catch(const std::invalid_argument&){rejected=true;}
            if(!rejected || calls)throw std::runtime_error("invalid window invoked clock");
            bool fail=false;
            EngineSpeedometer meter([&]{if(fail)throw std::runtime_error("clock failed");return std::uint16_t{1};});
            if(meter.sample(100)!=100)throw std::runtime_error("initial rate mismatch");
            fail=true;rejected=false;
            try{meter.sample(50);}catch(const std::runtime_error&){rejected=true;}
            fail=false;
            if(!rejected || meter.sample()!=100)throw std::runtime_error("clock failure partially mutated meter");
            std::cout<<"M416 native clock/window/exception ownership 3/3 PASS\n";return 0;
        }
        if(argc!=2)throw std::runtime_error("expected cases JSON");
        QFile file(QString::fromLocal8Bit(argv[1]));
        if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("cannot read speedometer cases");
        QJsonParseError error;
        const auto document=QJsonDocument::fromJson(file.readAll(),&error);
        if(error.error!=QJsonParseError::NoError || !document.isArray())throw std::runtime_error("invalid speedometer cases JSON");
        const auto cases=document.array();
        QJsonArray output;
        for(const auto &value:cases){
            const auto c=value.toObject();
            auto tick=static_cast<std::uint16_t>(c.value("startTick").toInt(1));
            std::map<QString,std::unique_ptr<EngineSpeedometer>> meters;
            meters.emplace("a",std::make_unique<EngineSpeedometer>([&]{return tick;},c.value("seconds").toInt(5)));
            QJsonArray readings;
            for(const auto &operation:c.value("steps").toArray()){
                const auto op=operation.toArray();
                const auto kind=op[0].toString();
                if(kind=="tick")tick=static_cast<std::uint16_t>(tick+op[1].toInt());
                else if(kind=="new")meters.insert_or_assign(op[1].toString(),std::make_unique<EngineSpeedometer>([&]{return tick;},op[2].toInt(5)));
                else if(kind=="read"){
                    double delta=op.size()>2?op[2].toDouble():0;
                    if(op.size()>2 && op[2].isString()){
                        const auto text=op[2].toString();
                        if(text=="NaN")delta=std::numeric_limits<double>::quiet_NaN();
                        else if(text=="Infinity")delta=std::numeric_limits<double>::infinity();
                        else if(text=="-Infinity")delta=-std::numeric_limits<double>::infinity();
                        else throw std::runtime_error("unsupported delta string");
                    }
                    const auto result=meters.at(op[1].toString())->sample(delta);
                    readings.append(std::isfinite(result)?QJsonValue(result):QJsonValue(
                        std::isnan(result)?QStringLiteral("NaN"):result>0?QStringLiteral("Infinity"):QStringLiteral("-Infinity")));
                }else throw std::runtime_error("unknown speedometer operation");
            }
            output.append(QJsonObject{{"name",c.value("name")},{"readings",readings}});
        }
        std::cout<<QJsonDocument(output).toJson(QJsonDocument::Compact).constData()<<'\n';
    }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
