// poster_scoreboard_harness.cpp — proves Stage 0 classification + aggregation:
// every reply lands in exactly one bucket; webp-without-decoder is Undecodable;
// per-host rows aggregate; summaryText is empty until something is recorded.
#include "../native/net/PosterScoreboard.h"
#include <cstdio>

static int fails = 0;
#define CHECK(c,l) do{ if(!(c)){ ++fails; std::printf("FAIL: %s\n", l);} }while(0)

int main() {
    using B = PosterScoreboard::Bucket;

    // classify: pure, no sockets
    CHECK(PosterScoreboard::classify(200, "image/jpeg", false, false) == B::Arrived,
          "200 jpeg -> Arrived");
    CHECK(PosterScoreboard::classify(200, "image/webp", false, false) == B::Undecodable,
          "200 webp, no decoder -> Undecodable");
    CHECK(PosterScoreboard::classify(200, "IMAGE/WEBP; charset=binary", false, false) == B::Undecodable,
          "content-type case/params ignored");
    CHECK(PosterScoreboard::classify(200, "image/webp", false, true) == B::Arrived,
          "200 webp, decoder present -> Arrived");
    CHECK(PosterScoreboard::classify(404, "text/html", false, true) == B::NetworkFailed,
          "404 -> NetworkFailed");
    CHECK(PosterScoreboard::classify(200, "image/jpeg", true, true) == B::NetworkFailed,
          "transport error wins -> NetworkFailed");
    CHECK(PosterScoreboard::classify(0, "", false, true) == B::Arrived,
          "finished, no error, no status (wrapped reply) -> Arrived");

    // record + summary: per-host rows, bytes sum
    PosterScoreboard sb;
    sb.setWebpDecoderPresent(false);
    CHECK(sb.summaryText().isEmpty(), "empty scoreboard -> empty text");
    sb.record("images.metahub.space", 200, "image/jpeg", 1000, false);
    sb.record("images.metahub.space", 200, "image/webp", 2000, false);
    sb.record("images.metahub.space", 404, "", 0, false);
    sb.record("wsrv.nl", 200, "image/jpeg", 500, false);
    const QVariantMap s = sb.summary();
    const QVariantMap metahub = s.value("images.metahub.space").toMap();
    CHECK(metahub.value("arrived").toLongLong() == 1,      "metahub arrived == 1");
    CHECK(metahub.value("undecodable").toLongLong() == 1,  "metahub undecodable == 1");
    CHECK(metahub.value("failed").toLongLong() == 1,       "metahub failed == 1");
    CHECK(metahub.value("bytes").toLongLong() == 3000,     "metahub bytes == 3000");
    CHECK(s.value("wsrv.nl").toMap().value("arrived").toLongLong() == 1, "wsrv arrived == 1");
    CHECK(!sb.summaryText().isEmpty(), "recorded scoreboard -> non-empty text");

    std::printf(fails ? "FAILS: %d\n" : "poster_scoreboard_harness: ALL PASS\n", fails);
    return fails;
}
