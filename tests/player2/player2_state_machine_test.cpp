#include "player2/core/Player2StateMachine.h"
#include "player2/core/Player2Types.h"
#include "player2/host/Player2HostServices.h"

#include <QtCore/QMetaType>
#include <QtTest/QTest>

#include <array>
#include <cstdio>
#include <type_traits>

using namespace Colosseum::Player2;

namespace
{
struct Edge
{
    Player2State from;
    Player2State to;
};

constexpr std::array<Player2State, 9> AllStates{
    Player2State::Idle,
    Player2State::Opening,
    Player2State::Buffering,
    Player2State::Playing,
    Player2State::Paused,
    Player2State::Seeking,
    Player2State::Ended,
    Player2State::Recovering,
    Player2State::Error,
};

constexpr std::array<Edge, 50> LegalChangedEdges{{
    {Player2State::Idle, Player2State::Opening},

    {Player2State::Opening, Player2State::Buffering},
    {Player2State::Opening, Player2State::Playing},
    {Player2State::Opening, Player2State::Paused},
    {Player2State::Opening, Player2State::Ended},
    {Player2State::Opening, Player2State::Recovering},
    {Player2State::Opening, Player2State::Error},
    {Player2State::Opening, Player2State::Idle},

    {Player2State::Buffering, Player2State::Playing},
    {Player2State::Buffering, Player2State::Paused},
    {Player2State::Buffering, Player2State::Seeking},
    {Player2State::Buffering, Player2State::Ended},
    {Player2State::Buffering, Player2State::Recovering},
    {Player2State::Buffering, Player2State::Error},
    {Player2State::Buffering, Player2State::Idle},

    {Player2State::Playing, Player2State::Paused},
    {Player2State::Playing, Player2State::Buffering},
    {Player2State::Playing, Player2State::Seeking},
    {Player2State::Playing, Player2State::Ended},
    {Player2State::Playing, Player2State::Recovering},
    {Player2State::Playing, Player2State::Error},
    {Player2State::Playing, Player2State::Idle},

    {Player2State::Paused, Player2State::Playing},
    {Player2State::Paused, Player2State::Buffering},
    {Player2State::Paused, Player2State::Seeking},
    {Player2State::Paused, Player2State::Ended},
    {Player2State::Paused, Player2State::Recovering},
    {Player2State::Paused, Player2State::Error},
    {Player2State::Paused, Player2State::Idle},

    {Player2State::Seeking, Player2State::Playing},
    {Player2State::Seeking, Player2State::Paused},
    {Player2State::Seeking, Player2State::Buffering},
    {Player2State::Seeking, Player2State::Ended},
    {Player2State::Seeking, Player2State::Recovering},
    {Player2State::Seeking, Player2State::Error},
    {Player2State::Seeking, Player2State::Idle},

    {Player2State::Ended, Player2State::Opening},
    {Player2State::Ended, Player2State::Seeking},
    {Player2State::Ended, Player2State::Idle},

    {Player2State::Recovering, Player2State::Opening},
    {Player2State::Recovering, Player2State::Buffering},
    {Player2State::Recovering, Player2State::Playing},
    {Player2State::Recovering, Player2State::Paused},
    {Player2State::Recovering, Player2State::Seeking},
    {Player2State::Recovering, Player2State::Ended},
    {Player2State::Recovering, Player2State::Error},
    {Player2State::Recovering, Player2State::Idle},

    {Player2State::Error, Player2State::Opening},
    {Player2State::Error, Player2State::Recovering},
    {Player2State::Error, Player2State::Idle},
}};

bool isExpectedLegal(Player2State from, Player2State to)
{
    if (from == to)
        return true;
    for (const Edge &edge : LegalChangedEdges) {
        if (edge.from == from && edge.to == to)
            return true;
    }
    return false;
}
}

class Player2StateMachineTest final : public QObject
{
    Q_OBJECT

private slots:
    void publicTypesHaveStableDefaultsAndNamedMetaTypes()
    {
        registerPlayer2MetaTypes();

        QVERIFY(QMetaType::fromName("Colosseum::Player2::Player2State").isValid());
        QVERIFY(QMetaType::fromName("Colosseum::Player2::NormalizationMode").isValid());
        QVERIFY(QMetaType::fromName("Colosseum::Player2::Player2ErrorCode").isValid());
        QVERIFY(QMetaType::fromName("Colosseum::Player2::ExternalSubtitleRequest").isValid());
        QVERIFY(QMetaType::fromName("Colosseum::Player2::PlaybackRequest").isValid());
        QVERIFY(QMetaType::fromName("Colosseum::Player2::Player2Error").isValid());

        PlaybackRequest request;
        QCOMPARE(request.resumeSeconds, 0.0);
        QVERIFY(request.headers.isEmpty());
        QVERIFY(request.displayMetadata.isEmpty());
        QVERIFY(request.externalSubtitles.isEmpty());
        QVERIFY(!request.stream);
        QVERIFY(!request.live);

        ExternalSubtitleRequest firstSubtitle;
        firstSubtitle.source = QUrl(QStringLiteral("file:///first.ass"));
        firstSubtitle.title = QStringLiteral("English");
        firstSubtitle.language = QStringLiteral("en");
        ExternalSubtitleRequest matchingSubtitle = firstSubtitle;
        QVERIFY(firstSubtitle == matchingSubtitle);
        matchingSubtitle.language = QStringLiteral("fr");
        QVERIFY(firstSubtitle != matchingSubtitle);

        Player2Error error;
        QCOMPARE(error.code, Player2ErrorCode::None);
        QVERIFY(error.message.isEmpty());
        QVERIFY(!error.recoverable);
    }

    void hostServicesIsAnAbstractApplicationBoundary()
    {
        QVERIFY(std::is_abstract_v<Player2HostServices>);
        QVERIFY(std::has_virtual_destructor_v<Player2HostServices>);
        QVERIFY((std::is_base_of_v<QObject, Player2HostServices>));
    }

    void classifiesEveryStatePairAndPreservesStateOnRejection()
    {
        int legalCount = 0;
        int rejectedCount = 0;

        for (const Player2State from : AllStates) {
            for (const Player2State to : AllStates) {
                Player2StateMachine machine(from);
                const StateTransitionResult result = machine.transitionTo(to);
                const bool expectedLegal = isExpectedLegal(from, to);

                QCOMPARE(result.accepted, expectedLegal);
                QCOMPARE(result.previous, from);

                if (expectedLegal) {
                    ++legalCount;
                    QCOMPARE(result.changed, from != to);
                    QCOMPARE(result.current, to);
                    QCOMPARE(machine.state(), to);
                    QVERIFY(!result.error.has_value());
                } else {
                    ++rejectedCount;
                    QVERIFY(!result.changed);
                    QCOMPARE(result.current, from);
                    QCOMPARE(machine.state(), from);
                    QVERIFY(result.error.has_value());
                    QCOMPARE(result.error->code, Player2ErrorCode::InvalidCommand);
                    QVERIFY(!result.error->message.isEmpty());
                    QVERIFY(!result.error->recoverable);
                }
            }
        }

        QCOMPARE(legalCount, static_cast<int>(LegalChangedEdges.size() + AllStates.size()));
        QCOMPARE(rejectedCount, static_cast<int>(AllStates.size() * AllStates.size()) - legalCount);
    }

    void requiredPlanExamplesRemainExplicitlyCovered()
    {
        const auto accepted = [](Player2State from, Player2State to) {
            Player2StateMachine machine(from);
            return machine.transitionTo(to).accepted;
        };

        QVERIFY(accepted(Player2State::Idle, Player2State::Opening));
        QVERIFY(accepted(Player2State::Playing, Player2State::Seeking));
        QVERIFY(accepted(Player2State::Seeking, Player2State::Playing));
        QVERIFY(accepted(Player2State::Opening, Player2State::Error));
        QVERIFY(!accepted(Player2State::Idle, Player2State::Playing));
    }
};

int main(int argc, char *argv[])
{
    Player2StateMachineTest test;
    const int result = QTest::qExec(&test, argc, argv);
    if (result == 0) {
        std::fputs("player2_state_machine_test: PASS\n", stdout);
        std::fflush(stdout);
    }
    return result;
}

#include "player2_state_machine_test.moc"
