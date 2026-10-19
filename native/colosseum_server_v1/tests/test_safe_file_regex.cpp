#include "server1/http/SafeFileRegex.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>
int main(){
    std::string input;while(std::getline(std::cin,input)){
        const auto value=QJsonDocument::fromJson(QByteArray::fromStdString(input)).array();
        if(value.at(0).isArray()){
            QStringList names,patterns;
            for(const auto &name:value.at(0).toArray())names.push_back(name.toString());
            for(const auto &pattern:value.at(1).toArray())patterns.push_back(pattern.toString());
            QJsonObject output;
            try{const auto selected=server1::http::selectFilePattern(names,patterns);output.insert("selected",selected?QJsonValue(static_cast<int>(*selected)):QJsonValue());}
            catch(const std::exception &){output.insert("invalid",true);}
            std::cout<<QJsonDocument(output).toJson(QJsonDocument::Compact).constData()<<std::endl;continue;
        }
        const auto result=server1::http::matchFilePattern(value.at(0).toString(),value.at(1).toString());
        QJsonObject output{{"valid",result.valid}};
        output.insert("matches",result.matches?QJsonValue(*result.matches):QJsonValue());
        std::cout<<QJsonDocument(output).toJson(QJsonDocument::Compact).constData()<<std::endl;
    }
}
