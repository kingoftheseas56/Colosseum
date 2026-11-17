// Offline AudiobookCatalog acceptance: no user profile, network, or installed app.
#include "engine/AudiobookCatalog.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QSet>
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
    std::cout << "PASS audiobook_catalogue_harness checks=" << checks << "\n";
    return 0;
}
