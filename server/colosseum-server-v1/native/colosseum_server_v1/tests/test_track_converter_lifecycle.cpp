#include "server1/media/TrackConverter.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <QtEndian>
#include <iostream>
#include <map>
using namespace server1::media;
void check(bool b, const char *m) {
  if (!b) {
    std::cerr << m << '\n';
    std::exit(1);
  }
}
QByteArray box(QByteArray type, QByteArray payload) {
  QByteArray b(4, 0);
  qToBigEndian<quint32>(payload.size() + 8, b.data());
  return b + type + payload;
}
QByteArray init() {
  QByteArray tk(16, 0), md(16, 0);
  qToBigEndian<quint32>(1, tk.data() + 12);
  qToBigEndian<quint32>(1000, md.data() + 12);
  return box("ftyp", {}) +
         box("moov",
             box("trak", box("tkhd", tk) + box("mdia", box("mdhd", md))));
}
QByteArray media(int time = 0) {
  QByteArray sx(36, 0);
  qToBigEndian<quint32>(1, sx.data() + 4);
  qToBigEndian<quint32>(1000, sx.data() + 8);
  qToBigEndian<quint32>(time, sx.data() + 12);
  qToBigEndian<quint16>(1, sx.data() + 22);
  qToBigEndian<quint32>(4000, sx.data() + 28);
  return box("sidx", sx) + box("mdat", "fixture");
}
struct Fake {
  struct Child {
    QJsonObject options;
    std::function<void(QByteArray)> data;
    std::function<void(int, QString)> exit;
    bool canceled = false;
  };
  std::map<int, Child> children;
  int last = 0;
  TrackConversionBackend backend() {
    return {[this](auto opts, auto data, auto exit) {
              children[++last] = {opts, data, exit, false};
              return quint64(last);
            },
            [this](auto id) { children[id].canceled = true; }};
  }
};
QJsonObject input() {
  return {{"ffmpeg", "ffmpeg"},
          {"mediaURL", "fixture:test"},
          {"forceTranscoding", true},
          {"stream", QJsonObject{{"track", "video"},
                                 {"id", 0},
                                 {"codec", "h264"},
                                 {"timescale", 1000}}},
          {"format", QJsonObject{{"duration", 12}, {"name", "mp4"}}},
          {"samples", QJsonArray{}}};
}
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  QJsonObject differential;
  {
    Fake f;
    TrackConverter c(input(), f.backend());
    int initCalls = 0;
    c.initSegment([&](auto r) {
      check(r.ok(), "init success");
      ++initCalls;
    });
    c.playlist([&](auto r) {
      check(r.ok(), "playlist init lock");
      ++initCalls;
    });
    check(f.last == 1, "single init process");
    f.children[1].data(init());
    check(initCalls == 2, "both init waiters");
    int completed = 0;
    c.mediaSegment(1, [&](auto r) {
      check(r.ok(), "segment 1 success");
      ++completed;
    });
    check(f.children[1].canceled && f.last == 2,
          "first request displaces initial process");
    f.children[2].data(init() + media());
    check(completed == 1, "segment finished");
    differential["initialSpawnCount"] = f.last;
    c.mediaSegment(1, [&](auto r) {
      check(r.ok(), "cache success");
      ++completed;
    });
    check(f.last == 2 && completed == 2, "cached no spawn");
    differential["cacheReuses"] = f.last == 2;
    c.mediaSegment(2, [&](auto r) {
      check(r.ok(), "sequential success");
      ++completed;
    });
    check(f.last == 2, "reuse current process");
    differential["sequentialReuses"] = f.last == 2;
    f.children[2].data(media(4000));
    check(completed == 3, "sequential callback");
    int cancelCount = 0;
    c.mediaSegment(3, [&](auto r) {
      check(r.errorName == "SegmentCanceledError",
            "displaced canceled not failure");
      ++cancelCount;
      differential["displacedError"] = r.errorName;
    });
    c.mediaSegment(1, [&](auto r) {
      check(r.ok(), "seek success");
      ++completed;
    });
    check(cancelCount == 1 && f.last == 3, "seek displacement");
    f.children[2].data(media(8000));
    check(completed == 3, "late old output ignored");
    f.children[3].data(init() + media());
    check(completed == 4, "new seek output accepted");
  }
  {
    Fake f;
    TrackConverter c(input(), f.backend());
    int count = 0;
    c.initSegment([](auto r) { check(r.ok(), "retry init"); });
    f.children[1].data(init());
    c.mediaSegment(1, [&](auto r) {
      check(r.ok(), "normal EOF retry");
      ++count;
    });
    f.children[2].data(init());
    f.children[2].exit(0, {});
    check(f.last == 3 && f.children[3].options["blank"].toBool(),
          "one blank retry");
    f.children[3].data(init() + media());
    check(count == 1, "one retry callback");
    differential["retryBlank"] = f.children[3].options["blank"].toBool();
    differential["retrySpawnCount"] = f.last;
  }
  {
    Fake f;
    TrackConverter c(input(), f.backend());
    c.initSegment([](auto) {});
    f.children[1].data(init());
    int count = 0;
    c.mediaSegment(1, [&](auto r) {
      check(!r.ok(), "failed retry errors");
      differential["failedRetryMessage"] = r.error;
      ++count;
    });
    f.children[2].data(init());
    f.children[2].exit(0, {});
    f.children[3].data(init());
    f.children[3].exit(0, {});
    check(f.last == 3 && count == 1, "failed retry no third attempt");
    differential["failedRetrySpawnCount"] = f.last;
  }
  {
    Fake f;
    TrackConverter c(input(), f.backend());
    int count = 0;
    c.initSegment([&](auto r) {
      check(r.errorName == "ConverterDestroyedError", "destroy init");
      ++count;
    });
    c.playlist([&](auto r) {
      check(r.errorName == "ConverterDestroyedError", "destroy waiting init");
      ++count;
    });
    c.destroy();
    f.children[1].data(init());
    f.children[1].exit(0, {});
    check(count == 2, "no double init completion");
  }
  {
    Fake f;
    TrackConverter c(input(), f.backend());
    int errors = 0;
    c.initSegment([&](auto r) {
      check(!r.ok(), "init failure");
      ++errors;
    });
    f.children[1].exit(7, "fixture failure");
    c.initSegment([&](auto r) {
      check(r.error == "fixture failure", "sticky init error");
      ++errors;
    });
    check(errors == 2 && f.last == 1, "failed init cached");
    c.burnSubtitles(QJsonObject{{"id", "0"}},
                    [&](auto r) { check(r.ok(), "burn retries failed init"); });
    f.children[2].data(init());
    int count = 0;
    c.mediaSegment(1, [&](auto r) {
      check(r.errorName == "SegmentCanceledError", "destroy active conversion");
      differential["destroyActiveError"] = r.errorName;
      ++count;
    });
    c.mediaSegment(1, [&](auto r) {
      check(r.errorName == "SegmentCanceledError",
            "destroy waiting conversion");
      ++count;
    });
    c.destroy();
    f.children[3].data(init() + media());
    check(count == 2, "no late double completion");
  }
  if (argc > 1) {
    ProcessPort port;
    ProcessDriver driver(port);
    auto in = input();
    in["mediaURL"] = QString::fromUtf8(argv[1]);
    in["ffmpeg"] = "/usr/bin/ffmpeg";
    if (QString::fromUtf8(argv[1]).endsWith(".wav")) {
      in["stream"] = QJsonObject{{"track", "audio"},     {"id", 0},
                                 {"codec", "pcm_s16le"}, {"timescale", 48000},
                                 {"sampleRate", 48000},  {"channels", 1}};
      in["format"] = QJsonObject{{"name", "wav"}, {"duration", 12}};
    }

    TrackConverter c(
        in, driver,
        QJsonObject{{"transcodeHorsepower", .5}, {"transcodeMaxWidth", 1920}},
        2);
    QEventLoop loop;
    bool done = false;
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    c.mediaSegment(1, [&](auto r) {
      if (!r.ok())
        std::cerr << r.error.toStdString() << '\n';
      check(r.ok() && !r.data.isEmpty(), "real FFmpeg first segment");
      QFile out("/tmp/m05-real-output.m4s");
      out.open(QIODevice::WriteOnly);
      c.initSegment([&](auto initResult) { out.write(initResult.data); });
      out.write(r.data);
      done = true;
      loop.quit();
    });
    if (!done)
      loop.exec();
    check(done, "real FFmpeg timeout");
    c.destroy();
    std::cout << "Real FFmpeg media segment PASS\n";
  }
  std::cout
      << QJsonDocument(differential).toJson(QJsonDocument::Compact).constData()
      << "\n";
  std::cout << "Track converter lifecycle PASS\n";
}
