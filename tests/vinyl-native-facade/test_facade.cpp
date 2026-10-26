#include <QtTest>
#include <QQuickItem>
#include <QJsonDocument>
#include <QJsonObject>
#include "vinyl/VinylBackendSession.h"
#include "VinylSessionPersistence.h"
#include "FeedbackTastePersistence.h"

class DeferredSource : public QObject {
    Q_OBJECT
public:
    int serial=0;
    Q_INVOKABLE int request(const QString &,const QString &,const QString &) { return ++serial; }
    Q_INVOKABLE void cancel(const QString &) {}
signals:
    void completed(const QString &,int,const QVariant &,const QString &);
};
class PersonalPorts : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString vinylFeedbackSnapshot MEMBER feedback)
public:
    QString feedback;
    QVariantMap saved;
    Q_INVOKABLE bool setVinylFeedbackSnapshot(const QString &value) { feedback=value;return true; }
    Q_INVOKABLE bool add(const QString &world,const QVariantMap &entry) { if(world!="Vinyl")return false;saved=entry;return true; }
};

class FacadeTest : public QObject {
    Q_OBJECT
    QVariantMap row(const QString &id) {
        QVariantMap track{{"id",id},{"title",id},{"seconds",30}};
        QVariantMap edition{{"id","edition"},{"albumId","album"},{"name","Album"},
            {"discs",QVariantList{QVariant::fromValue(QVariantList{track})}}};
        return {{"trackId",id},{"title",id},{"artist","Artist"},{"seconds",30},
            {"albumId","album"},{"editionId","edition"},{"discIndex",0},{"trackIndex",0},
            {"track",track},{"edition",edition}};
    }
private slots:
    void unrelatedStateDoesNotNotifyPaused() {
        DeferredSource source;VinylBackendSession session;
        session.setProfileScope("profile-a");session.setSource(&source);
        QVERIFY(session.replaceRows({row("vault:a"),row("vault:b")},-1,false));
        const auto property=session.metaObject()->property(session.metaObject()->indexOfProperty("paused"));
        QSignalSpy pauses(&session,property.notifySignal());QVERIFY(pauses.isValid());
        session.cycleRepeat();session.setShuffle(true);session.moveQueue(0,1);
        QCOMPARE(pauses.count(),0);
        session.playQueue(0);QCOMPARE(pauses.count(),1);
        session.setPaused(true);QCOMPARE(pauses.count(),2);
        session.setPaused(true);QCOMPARE(pauses.count(),2);
    }
    void localQueueHasNoInventedMbid() {
        VinylBackendSession session;
        QVERIFY(!session.replaceRows({row("vault:a")},-1,false));
        session.setProfileScope("profile-a");
        QVERIFY(session.replaceRows({row("vault:a"),row("vault:a"),row("vault:b")},-1,false));
        QCOMPARE(session.rows().size(),3);
        QVERIFY(session.rows()[0].toMap()["queueId"]!=session.rows()[1].toMap()["queueId"]);
        const auto checkpoint=session.checkpoint();
        QVERIFY(!checkpoint.value("backendState").toString().contains("recordingMbid\":\""));
        QVERIFY(checkpoint.value("backendState").toString().contains("vault:a"));
    }
    void nativeQueueCommandsPreserveOccurrenceAndColdRestore() {
        DeferredSource source;
        VinylBackendSession session; session.setProfileScope("profile-a");session.setSource(&source);
        QVERIFY(session.replaceRows({row("vault:a"),row("vault:b"),row("vault:a")},0,false));
        const auto id=session.rows()[0].toMap()["queueId"];
        session.moveQueue(0,2); QCOMPARE(session.activeIndex(),2);
        QCOMPARE(session.rows()[2].toMap()["queueId"],id);
        session.cycleRepeat();session.cycleRepeat();QCOMPARE(session.repeatMode(),QString("one"));
        session.setShuffle(true);session.setShuffle(false);
        const auto checkpoint=session.checkpoint();
        VinylBackendSession restored;restored.setProfileScope("profile-a");
        QVERIFY(restored.restoreCheckpoint(checkpoint));
        QCOMPARE(restored.rows(),session.rows());QVERIFY(restored.paused());QVERIFY(!restored.player());
        QCOMPARE(restored.repeatMode(),QString("one"));
        VinylBackendSession foreign;foreign.setProfileScope("profile-b");
        QVERIFY(!foreign.restoreCheckpoint(checkpoint));QVERIFY(foreign.rows().isEmpty());
    }
    void cancellationAndShutdownFenceOldResolution() {
        DeferredSource source;
        VinylBackendSession session;session.setProfileScope("profile-a");session.setSource(&source);
        QVERIFY(session.replaceRows({row("vault:a")},0,false));
        QTRY_VERIFY(source.serial>0);const auto old=source.serial;
        session.clearProfile();
        emit source.completed("play",old,QVariantMap{{"trackId","vault:a"},{"url","file:///unused.wav"}},{});
        QVERIFY(session.rows().isEmpty());QVERIFY(!session.player());
        session.shutdown();QVERIFY(!session.replaceRows({row("vault:a")},0,false));
    }
    void malformedReplacementIsAtomic() {
        VinylBackendSession session;session.setProfileScope("profile-a");
        QVERIFY(session.replaceRows({row("vault:a")},-1,false));const auto before=session.rows();
        QVERIFY(!session.replaceRows({row("")},0,false));QCOMPARE(session.rows(),before);
        auto state=session.checkpoint();auto rows=state["rows"].toList();
        auto forged=rows[0].toMap();forged["trackId"]="vault:other";rows[0]=forged;state["rows"]=rows;
        QVERIFY(!session.restoreCheckpoint(state));QCOMPARE(session.rows(),before);
    }
    void pauseDuringResolutionUpdatesNativeIntent() {
        DeferredSource source;
        VinylBackendSession session;session.setProfileScope("profile-a");session.setSource(&source);
        QVERIFY(session.replaceRows({row("vault:a")},0,false));session.setPaused(true);
        auto profile=vinyl::ProfileScopeId::parse("profile-a");
        auto captured=session.checkpoint();
        auto restored=vinyl::VinylSessionPersistence::restore(*profile.value,captured["backendState"].toString().toUtf8());
        QVERIFY(restored.ok());QVERIFY(restored.value->coordinatorIntent.pendingInput);
        QVERIFY(!restored.value->coordinatorIntent.pendingInput->playWhenReady);
        QCOMPARE(captured["editionTable"].toList().size(),1);
    }
    void existingPersonalPortsRetainLocalCatalogueAndRejectForeignFeedback() {
        PersonalPorts ports;VinylBackendSession session;session.setProfileScope("profile-a");
        QVERIFY(session.setProperty("preferences",QVariant::fromValue<QObject *>(&ports)));
        QVERIFY(session.setProperty("collectionStore",QVariant::fromValue<QObject *>(&ports)));
        const QVariantMap album{{"id","local-album"},{"origin","local"}};
        QVERIFY(session.setSavedAlbum({{"id","local-album"},{"payload",QVariantMap{{"album",album}}}},false));
        QCOMPARE(ports.saved["payload"].toMap()["album"].toMap(),album);
        QCOMPARE(ports.saved["payload"].toMap()["saved"].toBool(),false);
        auto profile=vinyl::ProfileScopeId::parse("profile-a");
        vinyl::TasteProfileState taste(*profile.value);
        auto snapshot=vinyl::FeedbackTastePersistence::capture(taste);QVERIFY(snapshot.ok());
        QVERIFY(session.storeFeedbackSnapshot(QString::fromUtf8(*snapshot.value)));
        QVERIFY(session.feedbackState()["ok"].toBool());
        session.setProfileScope("profile-b");QVERIFY(!session.feedbackState()["ok"].toBool());
        QVERIFY(!session.storeFeedbackSnapshot(QString::fromUtf8(*snapshot.value)));
        QCOMPARE(ports.feedback,QString::fromUtf8(*snapshot.value));
    }
    void providerNativeArtistFeedbackPersistsAndRejectsInvalidSnapshotsAtomically() {
        const QString uuid = QStringLiteral("11111111-1111-4111-8111-111111111111");
        const auto provider = vinyl::ProviderId::parse(QStringLiteral("catalogue.provider"));
        const auto providerItemId = vinyl::ProviderItemId::parse(uuid);
        const auto artistMbid = vinyl::ArtistMbid::parse(uuid);
        QVERIFY(provider.ok());
        QVERIFY(providerItemId.ok());
        QVERIFY(artistMbid.ok());

        const vinyl::ProviderItemRef providerItem(*provider.value, *providerItemId.value);
        const auto providerArtist = vinyl::RecommendationEntityRef::fromProvider(
            vinyl::CanonicalEntityKind::Artist, providerItem);
        const auto canonicalArtist = vinyl::RecommendationEntityRef::fromCanonical(
            vinyl::CanonicalEntityRef::from(*artistMbid.value));
        QVERIFY(providerArtist != canonicalArtist);

        auto profile = vinyl::ProfileScopeId::parse(QStringLiteral("profile-a"));
        QVERIFY(profile.ok());
        vinyl::TasteProfileState taste(*profile.value);
        QVERIFY(taste.applyExplicitFeedback(providerArtist,
                    vinyl::ExplicitTasteFeedback::Positive, 1, 1000).ok);
        QVERIFY(taste.applyExplicitFeedback(canonicalArtist,
                    vinyl::ExplicitTasteFeedback::Negative, 1, 1000).ok);
        QCOMPARE(taste.size(), 2);

        const auto snapshot = vinyl::FeedbackTastePersistence::capture(taste);
        QVERIFY(snapshot.ok());
        const auto snapshotDocument = QJsonDocument::fromJson(*snapshot.value);
        QVERIFY(snapshotDocument.isObject());
        QCOMPARE(snapshotDocument.object().value(QStringLiteral("version")).toInt(), 3);

        PersonalPorts ports;
        VinylBackendSession session;
        session.setProfileScope(QStringLiteral("profile-a"));
        QVERIFY(session.setProperty("preferences", QVariant::fromValue<QObject *>(&ports)));
        const QString snapshotText = QString::fromUtf8(*snapshot.value);
        QVERIFY(session.storeFeedbackSnapshot(snapshotText));
        QCOMPARE(ports.feedback, snapshotText);
        QVERIFY(session.feedbackState().value(QStringLiteral("ok")).toBool());

        const auto restored = vinyl::FeedbackTastePersistence::restore(
            *profile.value, ports.feedback.toUtf8());
        QVERIFY(restored.ok());
        QCOMPARE(restored.value->size(), 2);
        const auto *restoredProviderArtist = restored.value->find(providerArtist);
        const auto *restoredCanonicalArtist = restored.value->find(canonicalArtist);
        QVERIFY(restoredProviderArtist != nullptr);
        QVERIFY(restoredCanonicalArtist != nullptr);
        QVERIFY(restoredProviderArtist != restoredCanonicalArtist);
        QVERIFY(restoredProviderArtist->explicitFeedback().feedback()
                == vinyl::ExplicitTasteFeedback::Positive);
        QVERIFY(restoredCanonicalArtist->explicitFeedback().feedback()
                == vinyl::ExplicitTasteFeedback::Negative);

        const QString savedBytes = ports.feedback;
        QJsonObject unknownVersionObject = snapshotDocument.object();
        unknownVersionObject.insert(QStringLiteral("version"), 4);
        const QString unknownVersionSnapshot = QString::fromUtf8(
            QJsonDocument(unknownVersionObject).toJson(QJsonDocument::Compact));
        QVERIFY(!session.storeFeedbackSnapshot(unknownVersionSnapshot));
        QCOMPARE(ports.feedback, savedBytes);

        auto foreignProfile = vinyl::ProfileScopeId::parse(QStringLiteral("profile-b"));
        QVERIFY(foreignProfile.ok());
        vinyl::TasteProfileState foreignTaste(*foreignProfile.value);
        QVERIFY(foreignTaste.applyExplicitFeedback(providerArtist,
                    vinyl::ExplicitTasteFeedback::Positive, 1, 1000).ok);
        const auto foreignSnapshot = vinyl::FeedbackTastePersistence::capture(foreignTaste);
        QVERIFY(foreignSnapshot.ok());
        QVERIFY(!session.storeFeedbackSnapshot(QString::fromUtf8(*foreignSnapshot.value)));
        QCOMPARE(ports.feedback, savedBytes);
    }
    void replacingAnUnresolvedOccurrenceRetiresCoordinatorIntent() {
        DeferredSource source;VinylBackendSession session;
        session.setProfileScope("profile-a");session.setSource(&source);
        QVERIFY(session.replaceRows({row("vault:a"),row("vault:b"),row("vault:c")},0,false));
        QTRY_VERIFY(source.serial>0);const auto old=source.serial;
        session.playQueue(1);QCOMPARE(session.sourceState(),QString("finding"));QCOMPARE(session.error(),QString());
        QTRY_VERIFY(source.serial>old);const auto replaced=source.serial;
        session.removeQueue(1);QCOMPARE(session.sourceState(),QString("finding"));QCOMPARE(session.error(),QString());
        QCOMPARE(session.rows()[session.activeIndex()].toMap()["trackId"].toString(),QString("vault:c"));
        QTRY_VERIFY(source.serial>replaced);
    }
};
QTEST_GUILESS_MAIN(FacadeTest)
#include "test_facade.moc"
