#include "../../native/engine/AudiobookRecordingMatch.h"
#include <QtTest>

class RecordingMatchTests : public QObject {
    Q_OBJECT
private:
    const QVariantMap dune = {
        {"recordingKey","dune|frank herbert#as:dune-reading"},
        {"asin","B012345678"},
        {"narrators",QStringList{"Scott Brick","Orlagh Cassidy"}},
        {"language","en"},
        {"abridgement","unabridged"}
    };
    QVariantMap source() const {
        return {{"title","Dune"},{"author","Frank Herbert"},
                {"asin","B012345678"},{"narrators",QStringList{"Scott Brick"}},
                {"language","en"},{"workMatchCount",1}};
    }
private slots:
    void exactIdentifierAndCompatibleNarrator() {
        const auto result = AudiobookRecordingMatch::match(source(), QVariantList{dune});
        QCOMPARE(result.value("verdict").toString(),"exact");
        QCOMPARE(result.value("recordingKey").toString(),dune.value("recordingKey").toString());
    }
    void duneAsinNarratorConflictIsNotExact() {
        auto row=source();row["narrators"]=QStringList{"Simon Vance"};
        const auto result=AudiobookRecordingMatch::match(row,QVariantList{dune});
        QCOMPARE(result.value("verdict").toString(),"none");
        QCOMPARE(result.value("reason").toString(),"conflict");
        QVERIFY(result.value("recordingKey").toString().isEmpty());
    }
    void narratorOverlapAndUniqueWorkIsProbable() {
        auto row=source();row.remove("asin");
        auto result=AudiobookRecordingMatch::match(row,QVariantList{dune});
        QCOMPARE(result.value("verdict").toString(),"probable");
        QCOMPARE(result.value("recordingKey").toString(),dune.value("recordingKey").toString());
    }
    void ambiguousWorkDoesNotGuess() {
        auto row=source();row.remove("asin");row["workMatchCount"]=2;
        const auto result=AudiobookRecordingMatch::match(row,QVariantList{dune});
        QCOMPARE(result.value("verdict").toString(),"none");
        QCOMPARE(result.value("reason").toString(),"ambiguous");
    }
    void bundleCannotMatch() {
        auto row=source();row["bundle"]=true;
        const auto result=AudiobookRecordingMatch::match(row,QVariantList{dune});
        QCOMPARE(result.value("verdict").toString(),"none");
        QCOMPARE(result.value("reason").toString(),"bundle");
    }
    void languageMismatchIsConflict() {
        auto row=source();row["language"]="fr";
        const auto result=AudiobookRecordingMatch::match(row,QVariantList{dune});
        QCOMPARE(result.value("verdict").toString(),"none");
        QCOMPARE(result.value("reason").toString(),"conflict");
    }
    void multipleProbableNarrationsDoNotGuess() {
        auto row=source();row.remove("asin");
        auto another=dune;another["recordingKey"]="dune|frank herbert#as:another";
        const auto result=AudiobookRecordingMatch::match(row,QVariantList{dune,another});
        QCOMPARE(result.value("verdict").toString(),"none");
        QCOMPARE(result.value("reason").toString(),"ambiguous");
    }
    void namesEstablishUniqueWorkForProbable() {
        auto row=source();row.remove("asin");row.remove("workMatchCount");
        auto candidate=dune;candidate["workTitle"]="Dune";candidate["workAuthor"]="Frank Herbert";
        const auto result=AudiobookRecordingMatch::match(row,QVariantList{candidate});
        QCOMPARE(result.value("verdict").toString(),"probable");
    }
    void multipleNamedWorksRemainAmbiguous() {
        auto row=source();row.remove("asin");row.remove("workMatchCount");
        auto one=dune;one["workTitle"]="Dune";one["workAuthor"]="Frank Herbert";
        auto other=dune;other["recordingKey"]="different#src:123";
        other["workTitle"]="Dune Messiah";other["workAuthor"]="Frank Herbert";
        QCOMPARE(AudiobookRecordingMatch::match(row,QVariantList{one,other}).value("verdict").toString(),"none");
    }
    void conflictingIdentifiersCannotBecomeProbable() {
        auto row=source();row["asin"]="B000000001";
        QCOMPARE(AudiobookRecordingMatch::match(row,QVariantList{dune}).value("verdict").toString(),"none");
    }
    void exactIdentifierWorksDespiteUncertainWorkTitle() {
        auto row=source();row["workMatchCount"]=2;
        QCOMPARE(AudiobookRecordingMatch::match(row,QVariantList{dune}).value("verdict").toString(),"exact");
    }
    void identifierFromTitleAndMissingMetadata() {
        auto row=source();row.remove("asin");
        row["title"]="Dune (ASIN B012345678)";
        row.remove("narrators");row.remove("language");
        const auto result=AudiobookRecordingMatch::match(row,QVariantList{dune});
        QCOMPARE(result.value("verdict").toString(),"exact");
        row["title"]="Dune";
        QCOMPARE(AudiobookRecordingMatch::match(row,QVariantList{dune}).value("verdict").toString(),"none");
    }
};
QTEST_GUILESS_MAIN(RecordingMatchTests)
#include "recording_match_test.moc"
