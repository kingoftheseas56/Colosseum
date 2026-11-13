#include "StreamTypes.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QTextStream>
#include <cstdlib>
using namespace colosseum::stream;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            qFatal("line %d: %s", __LINE__, #x);                                                   \
        }                                                                                          \
    } while (false)
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    auto s = stream_from_json({{"title", "Movie.2024.1080p"},
                               {"infoHash", "abc"},
                               {"size", 123.0},
                               {"seeders", 0},
                               {"behaviorHints", QJsonObject{{"filename", "file.mkv"}}}},
                              2, "Torrentio", 4);
    CHECK(s.title == "Movie.2024.1080p");
    CHECK(s.input_index == 2);
    CHECK(s.addon_name == "Torrentio");
    CHECK(s.addon_priority == 4);
    CHECK(s.seeders == 0);
    CHECK(s.size == 123);
    CHECK(s.behavior_hints.value("filename") == "file.mkv");
    auto n = stream_from_json({{"release", "Series.S01E01.720p"},
                               {"infoHash", "url:https://example.invalid/video.mp4"},
                               {"filename", "actual.mkv"},
                               {"size", "1.34 GB"},
                               {"seeders", -1},
                               {"addonPriority", 1},
                               {"addonName", "Comet"}});
    CHECK(n.title == "Series.S01E01.720p");
    CHECK(n.info_hash.isEmpty());
    CHECK(n.url == "https://example.invalid/video.mp4");
    CHECK(!n.seeders);
    CHECK(!n.size);
    CHECK(n.description.contains("1.34 GB"));
    CHECK(n.behavior_hints.value("filename") == "actual.mkv");
    CHECK(n.addon_priority == 1);
    CHECK(n.addon_name == "Comet");
    auto invalid = stream_from_json({{"seeders", 2.5}, {"size", -10}, {"addonPriority", -1}});
    CHECK(!invalid.seeders);
    CHECK(!invalid.size);
    CHECK(!invalid.addon_priority);
    QTextStream(stdout) << "input_test: 19 checks passed\n";
}
