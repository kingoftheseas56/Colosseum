#include "engine/SourceContentStore.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QDateTime>
#include <QDir>
#include <cstdio>

static QByteArray bytes(const QJsonObject& value) { return QJsonDocument(value).toJson(QJsonDocument::Compact); }
static QString digest(const QByteArray& value) { return QString::fromLatin1(QCryptographicHash::hash(value,QCryptographicHash::Sha256).toHex()); }
static void write(const QString& path, const QJsonObject& value) { QFile file(path); if(!file.open(QIODevice::WriteOnly) || file.write(bytes(value))<0) qFatal("fixture write failed"); }
static QJsonObject bundle(const QString& title) {
    const auto revision=digest(title.toUtf8());
    const auto data = QJsonObject{{"title",title},{"universes",QJsonArray{QJsonObject{}}},{"archive",QJsonArray{}},
        {"shelfOverlays",QJsonObject{}},{"universe",QJsonObject{{"id","test"},{"title",title},{"sections",QJsonArray{QJsonObject{}}}}},
        {"story",QJsonObject{{"arcs",QJsonArray{QJsonObject{}}}}},{"atlas",QJsonObject{}},{"artByArc",QJsonObject{}},
        {"hubs",QJsonArray{QJsonObject{}}},{"destinations",QJsonArray{QJsonObject{}}},{"skywalkerDestination",QJsonObject{}}};
    const auto payload=bytes(QJsonObject{{"schemaVersion",1},{"revision",revision},{"data",data}});
    const QString assetHash(64,'a');
    QJsonObject descriptions, texts;
    for(const auto& name : QStringList{"universe-test","universes-roster","universe-one-piece","universe-dcau","universe-star-wars",
        "tribute-dcau","tribute-star-wars","tribute-one-piece-east-blue","tribute-one-piece-paradise","tribute-one-piece-new-world","tribute-one-piece-final-sea"}) {
        descriptions.insert(name,QJsonObject{{"revision",revision},{"sha256",digest(payload)}});
        texts.insert(name,QString::fromUtf8(payload));
    }
    return {{"manifest",QJsonObject{{"schemaVersion",1},{"revision",revision},
        {"documents",descriptions},
        {"assets",QJsonObject{{"assets/universes/test.png",QJsonObject{{"path","/assets/content/"+assetHash+".png"},{"sha256",assetHash},{"bytes",100}}}}}}},
        {"documents",texts}};
}
static QJsonObject cache(const QJsonObject& result) {
    const auto id=digest(bytes(QJsonObject{{"method","bundle"},{"args",QJsonArray{}}}));
    return {{"apiVersion",1},{"dataset","content"},{"revision",QString(64,'b')},{"endpoint","https://content.example"},
        {"entries",QJsonObject{{id,QJsonObject{{"method","bundle"},{"revision",QString(64,'b')},
            {"fetchedAt",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},{"result",result}}}}}};
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    QTemporaryDir temporary;
    if(!temporary.isValid()) return 1;
    const auto starter=temporary.filePath("starter.json"), cached=temporary.filePath("cache.json");
    int checks=0;
    auto verify=[&](bool condition,const char* description){++checks;if(!condition)qFatal("FAIL: %s",description);};
    write(starter,bundle("Starter"));
    SourceContentStore first;
    first.configure(nullptr,QUrl("https://content.example"),{},starter);
    verify(first.document("universe-test").value("title").toString()=="Starter","small starter available offline");
    verify(first.document("unknown").isEmpty(),"unknown document empty");
    verify(first.assetUrl("../assets/universes/test.png")=="https://content.example/assets/content/"+QString(64,'a')+".png","hashed URL resolution");
    verify(first.assetUrl("../assets/../secret.png").isEmpty(),"asset traversal rejected");
    verify(first.assetUrl("https://images.example/image.png")=="https://images.example/image.png","external existing HTTPS art preserved");
    write(cached,cache(bundle("Published")));
    SourceContentStore restored;
    restored.configure(nullptr,QUrl("https://content.example"),cached,starter);
    verify(restored.document("universe-test").value("title").toString()=="Published","persisted API generation wins over starter");
    verify(restored.lastError().isEmpty(),"valid cache healthy");
    auto corrupt=bundle("Corrupted");
    auto docs=corrupt.value("documents").toObject(); docs.insert("universe-test","{}"); corrupt.insert("documents",docs);
    write(cached,cache(corrupt));
    SourceContentStore protectedStore;
    protectedStore.configure(nullptr,QUrl("https://content.example"),cached,starter);
    verify(protectedStore.document("universe-test").value("title").toString()=="Starter","bad checksum cannot replace good generation");
    verify(protectedStore.document("universe-test").value("title").toString() != "Corrupted","invalid disk candidate rejected by transport validator");
    auto inconsistent=bundle("First");
    auto manifest=inconsistent.value("manifest").toObject();
    auto descriptions=manifest.value("documents").toObject();descriptions.insert("universe-missing",descriptions.value("universe-test"));
    manifest.insert("documents",descriptions);inconsistent.insert("manifest",manifest);
    write(cached,cache(inconsistent));
    SourceContentStore atomic;
    atomic.configure(nullptr,QUrl("https://content.example"),cached,starter);
    verify(atomic.document("universe-test").value("title").toString()=="Starter","incomplete bundle rejected atomically");
    verify(atomic.document("universe-missing").isEmpty(),"partial document not installed");
    auto missing=bundle("MissingRequired");
    auto missingManifest=missing.value("manifest").toObject();
    auto missingDescriptions=missingManifest.value("documents").toObject();
    auto missingTexts=missing.value("documents").toObject();
    missingDescriptions.remove("tribute-dcau"); missingTexts.remove("tribute-dcau");
    missingManifest.insert("documents",missingDescriptions);missing.insert("manifest",missingManifest);missing.insert("documents",missingTexts);
    write(cached,cache(missing));
    SourceContentStore required;
    required.configure(nullptr,QUrl("https://content.example"),cached,starter);
    verify(required.document("universe-test").value("title").toString()=="Starter","self-consistent bundle missing required document rejected");
    auto invalidShape=bundle("WrongShape");
    auto shapeTexts=invalidShape.value("documents").toObject();
    auto envelope=QJsonDocument::fromJson(shapeTexts.value("universes-roster").toString().toUtf8()).object();
    auto shapeData=envelope.value("data").toObject();shapeData.insert("universes","not-an-array");envelope.insert("data",shapeData);
    const auto changedBytes=bytes(envelope);shapeTexts.insert("universes-roster",QString::fromUtf8(changedBytes));invalidShape.insert("documents",shapeTexts);
    auto shapeManifest=invalidShape.value("manifest").toObject();auto shapeDescriptions=shapeManifest.value("documents").toObject();
    auto changedDescription=shapeDescriptions.value("universes-roster").toObject();changedDescription.insert("sha256",digest(changedBytes));
    shapeDescriptions.insert("universes-roster",changedDescription);shapeManifest.insert("documents",shapeDescriptions);invalidShape.insert("manifest",shapeManifest);
    write(cached,cache(invalidShape));
    SourceContentStore schema;
    schema.configure(nullptr,QUrl("https://content.example"),cached,starter);
    verify(schema.document("universe-test").value("title").toString()=="Starter","invalid essential shape rejected even with valid recalculated checksum");
    const auto originalDirectory=QDir::currentPath();
    verify(QDir().mkpath(temporary.filePath("assets/universes")),"local asset fixture directory");
    QFile localArt(temporary.filePath("assets/universes/test.png"));verify(localArt.open(QIODevice::WriteOnly),"local fixture file");localArt.write("image");localArt.close();
    verify(QDir::setCurrent(temporary.path()),"local fixture resource root");
    SourceContentStore local;
    local.configure(nullptr,QUrl(),{},starter);
    verify(local.assetUrl("../assets/universes/test.png")==QUrl::fromLocalFile(localArt.fileName()).toString(),"non-API source app retains safe local artwork");
    verify(local.assetUrl("../assets/../starter.json").isEmpty(),"local fallback rejects traversal");
    verify(first.assetUrl("../assets/universes/unmapped.png").isEmpty(),"API app never falls back to undeclared local art");
    verify(QDir::setCurrent(originalDirectory),"restore resource root");
    std::printf("SourceContentStore: %d checks PASS\n",checks);
    return 0;
}
