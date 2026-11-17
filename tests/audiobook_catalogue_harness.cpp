// Offline AudiobookCatalog acceptance: no user profile, network, or installed app.
#include "engine/AudiobookCatalog.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <iostream>
#include <cstdlib>

namespace {
int checks=0;
void require(bool ok,const char* message) {
    ++checks;
    if (!ok) { std::cerr << "FAIL " << message << "\n"; std::exit(1); }
}
QVariantMap locate(const QVariantList& rows, const QString& key) {
    for (const QVariant& item:rows) {
        const auto r=item.toMap();
        if (r.value("id").toString()==key) return r;
    }
    return {};
}
} // namespace
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    require(argc==2 && QFileInfo::exists(QString::fromLocal8Bit(argv[1])),"fixture exists");
    AudiobookCatalog cat(QString::fromLocal8Bit(argv[1]));
    require(cat.ready(),"fixture SQLite opened");
    require(cat.available(),"local catalogue available");
    require(!cat.remoteEnabled(),"local mode has no remote");
    const auto rhythm=locate(cat.search("Rhythm",20),"rhythm-of-war");
    require(rhythm.value("title").toString()=="Rhythm of War","FTS title search");
    const auto work=cat.work("rhythm-of-war");
    const auto recordings=work.value("recordings").toList();
    require(recordings.size()==2,"both Rhythm of War recordings retained");
    require(!work.value("authors").toList().isEmpty(),"work authors present");
    QSet<QString> narrations;
    for(const QVariant& item:recordings) {
        const auto recording=item.toMap();
        narrations.insert(recording.value("id").toString());
        require(!recording.value("narrators").toList().isEmpty(),"recording narrators");
    }
    require(narrations==QSet<QString>{"full-cast-2020","kate-reading-2020"},"recording identity");
    const auto dune=cat.work("dune");
    const auto relations=dune.value("series").toList();
    require(relations.size()>=2,"Dune multiple series");
    bool sequence=false;
    for(const QVariant& item:relations) {
        const auto r=item.toMap();
        if(r.value("name").toString()=="The Dune Sequence"){
            sequence=r.value("numberText").toString()=="12" && r.value("positionSort").toDouble()==12;
        }
    }
    require(sequence,"Dune #12 in The Dune Sequence preserved");
    const auto latest=cat.latest(0);
    require(!latest.isEmpty(),"latest page has recordings");
    const auto author=work.value("authors").toList().at(0).toMap().value("id").toString();
    require(!locate(cat.byPerson(author,0),"rhythm-of-war").isEmpty(),"byPerson author lookup");
    const auto asin=cat.work("rhythm-of-war").value("recordings").toList().at(0).toMap().value("asins").toList();
    if(!asin.isEmpty()) {
        const auto key=asin.at(0).toMap().value("asin").toString();
        require(cat.recordingByAsin(key).value("workId")=="rhythm-of-war","ASIN lookup");
    }
    require(cat.work("not-a-real-work").isEmpty(),"unknown work empty");
    require(cat.series("not-a-real-series").isEmpty(),"unknown series empty");
    require(cat.recordingByIsbn("not-a-real-isbn").isEmpty(),"unknown ISBN empty");
    require(cat.search("\"\"\"",20).isEmpty(),"unsafe search returns empty");
    const auto shelves=cat.shelves();
    require(shelves.size()==5,"five static Discover shelves");
    require(shelves.at(0).toMap().value("key")=="new-releases","new releases first");
    require(shelves.at(1).toMap().value("key")=="dramatized","dramatized second");
    const QStringList narratorIds{"scott-brick","simon-vance","robin-miles"};
    const QStringList narratorNames{"Scott Brick","Simon Vance","Robin Miles"};
    auto evidence=QSqlDatabase::addDatabase("QSQLITE","narrator-evidence");
    evidence.setDatabaseName(QString::fromLocal8Bit(argv[1]));
    evidence.setConnectOptions("QSQLITE_OPEN_READONLY");
    require(evidence.open(),"open narrator evidence read-only");
    for(int i=0;i<3;++i) {
        const auto key="narrator:"+narratorIds.at(i);
        require(shelves.at(i+2).toMap().value("key")==key,"editorial narrator order");
        require(shelves.at(i+2).toMap().value("title")=="Narrated by "+narratorNames.at(i),
                "editorial narrator name");
        int count=0;
        bool more=true;
        for(int page=0;more;++page) {
            const auto result=cat.shelf(key,page);
            more=result.value("hasMore").toBool();
            const auto items=result.value("items").toList();
            for(const auto& value:items) {
                const auto item=value.toMap();
                QSqlQuery query(evidence);
                query.prepare("SELECT 1 FROM recordings r WHERE r.work_id=? AND r.id=? "
                              "AND r.language='en' AND (r.librivox_id IS NULL OR r.librivox_id='') "
                              "AND EXISTS(SELECT 1 FROM recording_narrators n WHERE n.work_id=r.work_id "
                              "AND n.recording_id=r.id AND n.person_id=?)");
                query.addBindValue(item.value("workId"));
                query.addBindValue(item.value("recordingId"));
                query.addBindValue(narratorIds.at(i));
                require(query.exec() && query.next(),"narrator shelf is commercial English with matching credit");
                ++count;
            }
        }
        QSqlQuery query(evidence);
        query.prepare("SELECT english_recordings FROM narrator_shelves WHERE person_id=?");
        query.addBindValue(narratorIds.at(i));
        require(query.exec() && query.next() && count>0 && count==query.value(0).toInt(),
                "narrator shelf count matches bake");
    }
    evidence.close();
    const auto newPage=cat.shelf("new-releases",0);
    const auto first=newPage.value("items").toList();
    require(first.size()==20,"Discover pages contain twenty items");
    require(newPage.value("hasMore").toBool(),"new releases have a second page");
    require(cat.shelf("new-releases",1).value("page").toInt()==1,"next page index");
    for(const auto& v:first){
        const auto item=v.toMap();
        require(!item.value("workId").toString().isEmpty(),"work identity on shelf");
        require(!item.value("recordingId").toString().isEmpty(),"recording identity on shelf");
        require(item.contains("dramatized"),"evidence flag on shelf");
        require(item.value("authors").canConvert<QVariantList>(),"authors list on shelf");
        require(item.value("narrators").canConvert<QVariantList>(),"narrator list on shelf");
    }
    require(cat.shelf("dramatized",0).value("items").toList().size()>0,"dramatization shelf populated");
    require(cat.shelf("unknown",0).value("items").toList().isEmpty(),"unknown shelf empty");
    const auto mosaic=cat.genres();
    require(!mosaic.isEmpty(),"genre mosaic populated");
    for(const auto& v:mosaic)require(v.toMap().value("covers").toList().size()==4,
                                    "four covers per genre");
    const auto genreName=mosaic.at(0).toMap().value("genre").toString();
    require(!cat.genre(genreName,0).value("items").toList().isEmpty(),"genre page populated");
    require(cat.genre("not-a-genre",0).value("items").toList().isEmpty(),
            "unknown genre empty");
    std::cout << "PASS audiobook_catalogue_harness checks=" << checks << "\n";
    return 0;
}
