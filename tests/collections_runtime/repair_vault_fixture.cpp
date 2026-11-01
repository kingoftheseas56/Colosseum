// Repairs only the exact owned AVI fixture after an earlier seed omitted its root.
#include "../../native/devtools/CollectionsTestProfileController.h"
#include "../../native/engine/VaultConfig.h"
#include "../../native/engine/VaultIdentity.h"
#include "../../native/engine/VaultIndex.h"
#include "../../native/engine/VaultLocation.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTextStream>

static QByteArray bytes(const QString &p) { QFile f(p); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }
static QJsonObject rows(const QString &path) {
    const QString connection = QStringLiteral("collections_vault_fixture_snapshot");
    QJsonObject result;
    {
        auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
        db.setDatabaseName(path); db.setConnectOptions("QSQLITE_OPEN_READONLY");
        if (db.open()) { QSqlQuery q(db); if(q.exec("SELECT * FROM files ORDER BY id")) {
            const auto columns = q.record();
            while(q.next()) { QJsonObject r; for(int i=0;i<columns.count();++i) r.insert(columns.fieldName(i),QJsonValue::fromVariant(q.value(i)));
                result.insert(r.value("id").toString(),r); }
        } db.close(); }
    }
    QSqlDatabase::removeDatabase(connection); return result;
}
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv); QCommandLineParser parser; parser.addHelpOption();
    parser.addOption({"root","Existing isolated Collections fixture root","path"}); parser.process(app);
    const QString root=QDir::fromNativeSeparators(parser.value("root"));
    auto fail=[](const QString &why){QTextStream(stderr)<<why<<'\n';return 2;};
    CollectionsTestProfileController guard(root,{},{});
    if(!guard.enabled())return fail("Unmarked, untagged or linked root refused");
    QLockFile lock(root+"/.collections-vault-seeding.lock"); if(!lock.tryLock(0))return fail("Seed already active");
    const QString group=root+"/collections-fixture-media/video/arrival-ui-fixture";
    const QString video=group+"/synthetic-color.avi";
    const QString folder=root+"/collections-fixture-media/video";
    const QString vault=root+"/vault", database=vault+"/index-v1.sqlite";
    const QFileInfo info(video); const QByteArray media=bytes(video);
    if(!info.isFile()||info.isSymLink()||info.canonicalFilePath()!=video||media.size()>16*1024*1024
       ||media.left(4)!="RIFF"||media.mid(8,4)!="AVI ")return fail("Exact synthetic AVI unavailable");
    const QByteArray identityBytes=bytes(vault+"/identity.json"),configBytes=bytes(vault+"/config.json");
    const auto idDoc=QJsonDocument::fromJson(identityBytes); const auto cfg=QJsonDocument::fromJson(configBytes);
    if(!idDoc.isObject()||idDoc.object().value("version").toInt()!=1||!cfg.isObject()
       ||cfg.object().value("version").toInt()!=1||!cfg.object().value("roots").isArray())return fail("Malformed existing stores preserved");
    const QString id=VaultIdentity::computeId(video,info.size(),info.lastModified().toMSecsSinceEpoch());
    bool known=false; for(const auto v:idDoc.object().value("entries").toArray()) {
        auto r=v.toObject(); if(r.value("id").toString()==id) known=r.value("path").toString()==video
            &&r.value("size").toInteger()==info.size()&&r.value("mtimeMs").toInteger()==info.lastModified().toMSecsSinceEpoch();
    }
    if(!known)return fail("Only the previously registered exact fixture identity may be repaired");
    const auto before=rows(database); if(before.contains(id))return fail("No overwrite: fixture row is already present");
    for(auto it=before.constBegin();it!=before.constEnd();++it) {
        auto r=it.value().toObject(); if(!r.value("path").toString().startsWith(root+"/",Qt::CaseInsensitive))
            return fail("An index row is outside the disposable fixture");
    }
    VaultConfig config(vault); if(config.recoveredFromBackup())return fail("Do not replace recovery data");
    const auto previousRoots=config.roots();
    for(auto r:previousRoots)if(!r.toMap().value("path").toString().startsWith(root+"/",Qt::CaseInsensitive))return fail("External root refused");
    config.addRoot(folder,1793232000000LL); config.confirmRoot(folder);
    VaultConfig reloaded(vault); if(!reloaded.isRootConfirmed(folder))return fail("Root was not persisted");
    for(auto r:previousRoots)if(!reloaded.roots().contains(r))return fail("Prior root metadata changed");
    VaultIndex index(database);if(!index.isOpen())return fail("Index unavailable");
    VaultIndex::FileRow r; r.id=id; r.rootPath=VaultLocation::normalize(folder);r.subtreePath=group;r.groupKey=group;
    r.groupTitle="Arrival (2016) · synthetic video fixture";r.kind="video";r.path=video;
    r.displayTitle="Synthetic AVI for Collections UI verification";r.realName=info.fileName();r.size=info.size();r.mtimeMs=info.lastModified().toMSecsSinceEpoch();r.format="AVI";
    r.identitySource="IMDB";r.identityId="imdb:tt2543164";r.identityTitle=r.groupTitle;r.identityWorld="Theatre";r.identityYear=2016;r.identityState="adopted";
    r.identitySynopsis="Synthetic fixture only. This file is not the film Arrival.";
    if(!guard.enabled()||!index.upsert(r))return fail("Native fixture upsert failed");
    auto after=rows(database);if(after.take(id).toObject().value("identityId").toString()!=r.identityId||after!=before
        ||bytes(vault+"/identity.json")!=identityBytes)return fail("Repair preservation verification failed");
    QTextStream(stdout)<<QJsonDocument(QJsonObject{{"ok",true},{"identityPreserved",true},{"existingRowsPreserved",before.size()},
        {"registeredRoot",folder},{"fixtureId",id},{"fixtureOnly",true}}).toJson(QJsonDocument::Compact)<<'\n';return 0;
}
