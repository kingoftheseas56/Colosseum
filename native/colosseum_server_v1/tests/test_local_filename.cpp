#include "server1/local/LocalFilename.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <iostream>
int main(int argc,char**argv){QCoreApplication app(argc,argv);if(argc!=2)return 2;QFile f(QString::fromLocal8Bit(argv[1]));if(!f.open(QIODevice::ReadOnly))return 2;int count=0;for(auto v:QJsonDocument::fromJson(f.readAll()).array()){auto row=v.toObject();if(row.contains("error"))continue;auto actual=server1::local::parseLocalFilename(row["path"].toString(),row["options"].toObject());if(actual!=row["expected"].toObject()){std::cerr<<QJsonDocument(row).toJson().constData()<<"Actual: "<<QJsonDocument(actual).toJson().constData();return 1;}++count;}std::cout<<"PASS "<<count<<" pinned filename comparisons\n";return 0;}
