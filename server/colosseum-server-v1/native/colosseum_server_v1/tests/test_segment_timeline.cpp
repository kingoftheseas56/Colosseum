#include "server1/media/HlsPlaylist.h"
#include "server1/media/Mp4Timeline.h"
#include "server1/media/SegmentLayout.h"
#include "server1/media/VttTimeline.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QtEndian>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace server1::media;
static void check(bool x, const char *m) {
  if (!x)
    throw std::runtime_error(m);
}

static QByteArray box(QByteArray type, QByteArray payload,
                      bool extended = false) {
  QByteArray b(extended ? 16 : 8, '\0');
  qToBigEndian<quint32>(extended ? 1 : b.size() + payload.size(), b.data());
  b.replace(4, 4, type);
  if (extended)
    qToBigEndian<quint64>(b.size() + payload.size(), b.data() + 8);
  return b + payload;
}
template <typename F> static void rejects(F f, const char *why) {
  bool thrown = false;
  try {
    f();
  } catch (const std::exception &) {
    thrown = true;
  }
  check(thrown, why);
}
static void boundaries() {
  for (bool wide : {false, true}) {
    auto first = box("ftyp", QByteArray("isom"), wide),
         target = box("moov", QByteArray("payload"), wide),
         rest = box("mdat", QByteArray("data"), wide);
    auto all = first + target + rest;
    for (qsizetype split = 0; split < first.size() + target.size(); ++split) {
      auto pending = all.left(split);
      check(!Mp4Timeline::takeUntil(pending, "moov"), "partial box emitted");
      pending += all.mid(split);
      auto out = Mp4Timeline::takeUntil(pending, "moov");
      check(out && *out == first + target && pending == rest,
            "split box output differs");
    }
    for (qsizetype split = 0; split < first.size() + target.size(); ++split) {
      auto pending = all.left(split);
      rejects([&] { Mp4Timeline::takeUntil(pending, "moov", true); },
              "truncated EOF accepted");
    }
  }
  QByteArray zero = QByteArray::fromHex("000000006d646174010203");
  check(!Mp4Timeline::takeUntil(zero, "mdat"),
        "open ended box prematurely emitted");
  check(Mp4Timeline::takeUntil(zero, "mdat", true).has_value(),
        "EOF box missing");
  for (auto hex : {"000000046d6f6f76", "000000016d6f6f76ffffffffffffffff",
                   "000000016d6f6f760000000000000008"}) {
    auto data = QByteArray::fromHex(hex);
    rejects([&] { Mp4Timeline::takeUntil(data, "moov", true); },
            "invalid size accepted");
  }
  rejects([] { Mp4Timeline::sidx(box("free", {})); }, "missing sidx accepted");
  rejects([] { Mp4Timeline::sidx(box("sidx", QByteArray(8, '\0'))); },
          "truncated sidx accepted");
  QByteArray tfhd(8, '\0');
  qToBigEndian<quint32>(1, tfhd.data() + 4);
  auto shortTfdt =
      box("moof",
          box("traf", box("tfhd", tfhd) + box("tfdt", QByteArray(8, '\0'))));
  rejects([&] { Mp4Timeline::updateBaseMediaDecodeTime(shortTfdt, 1); },
          "undersized tfdt accepted");
  check(shortTfdt[shortTfdt.size() - 8] == 1,
        "source version-before-error ordering differs");
  auto fragment =
      box("moof",
          box("traf", box("tfhd", tfhd) + box("tfdt", QByteArray(12, '\0'))));
  for (double n :
       {1.5, std::numeric_limits<double>::infinity(), 18446744073709551616.0})
    rejects([&] { Mp4Timeline::updateBaseMediaDecodeTime(fragment, n); },
            "invalid tfdt value accepted");
  auto noTrack = fragment;
  check(!Mp4Timeline::updateBaseMediaDecodeTime(noTrack, 5, 2) &&
            noTrack == fragment,
        "unmatched track mutated");
  QJsonArray cues{
      QJsonObject{{"startTime", 9.5}, {"endTime", 10}, {"text", "cross"}}};
  auto partitions = VttTimeline::partition(cues, 3);
  check(partitions[0] == cues && partitions[1] == cues &&
            partitions[2].isEmpty(),
        "inclusive VTT bucket differs");
  rejects([&] { VttTimeline::partition(cues, 1); },
          "cue out of bounds clamped");
  rejects(
      [] {
        SegmentLayout::predict({{"timescale", 1000}}, {{"duration", 1}}, 0,
                               1000);
      },
      "zero duration admitted");
  rejects(
      [] {
        SegmentLayout::predict({{"timescale", 1000}}, {{"duration", -1}}, 4,
                               1000);
      },
      "negative duration admitted");
  rejects(
      [] {
        SegmentLayout::predict({{"timescale", 0}, {"startTimeTs", 1}},
                               {{"duration", 1}}, 4, 1000);
      },
      "invalid stream timescale admitted");
  std::cout
      << "native split-box/overflow/truncation/VTT boundary checks passed\n";
}
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  try {
    boundaries();
    QFile file(argv[1]);
    check(file.open(QIODevice::ReadOnly), "goldens");
    int count = 0;
    for (auto v : QJsonDocument::fromJson(file.readAll()).array()) {
      auto r = v.toObject(), i = r["input"].toObject();
      QJsonValue actual;
      QString error;
      try {
        auto k = r["kind"].toString();
        if (k == "predict")
          actual = SegmentLayout::predict(
              i["stream"].toObject(), i["format"].toObject(),
              i["segmentDuration"].toDouble(), i["timescale"].toDouble());
        else if (k == "retrieve")
          actual = SegmentLayout::retrieve(i);
        else if (k == "master")
          actual = HlsPlaylist::master(i["streams"].toArray(),
                                       i["query"].toString());
        else if (k == "media")
          actual = HlsPlaylist::media(
              i["segments"].toArray(), i["timescale"].toDouble(),
              i["track"].toString(), i["query"].toString());
        else if (k == "format")
          actual = VttTimeline::format(i["cues"].toArray());
        else if (k == "parse")
          actual = VttTimeline::parse(i["text"].toString());
        else if (k == "tfdt") {
          auto bytes = QByteArray::fromHex(i["hex"].toString().toLatin1());
          Mp4Timeline::updateBaseMediaDecodeTime(bytes, i["value"].toDouble(),
                                                 i["trackId"].toInt());
          actual = QString::fromLatin1(bytes.toHex());
        } else if (k == "sidx") {
          auto t = Mp4Timeline::sidx(
              QByteArray::fromHex(i["hex"].toString().toLatin1()));
          actual = QJsonObject{
              {"earliestPresentationTime", double(t.earliestPresentationTime)},
              {"duration", double(t.duration)},
              {"timescale", double(t.timescale)}};
        } else if (k == "readUntil") {
          auto bytes = QByteArray::fromHex(i["hex"].toString().toLatin1());
          auto output = Mp4Timeline::takeUntil(
              bytes, i["type"].toString().toLatin1(), true);
          actual = QString::fromLatin1(output->toHex());
        } else if (k == "timescales") {
          QJsonObject o;
          auto scales = Mp4Timeline::timescales(
              QByteArray::fromHex(i["hex"].toString().toLatin1()));
          for (auto it = scales.begin(); it != scales.end(); ++it)
            o[QString::number(it.key())] = double(it.value());
          actual = o;
        }
      } catch (const std::exception &e) {
        error = QString::fromUtf8(e.what());
      }
      if ((r.contains("error") ? error != r["error"].toString()
                               : !error.isEmpty() || actual != r["expected"])) {
        std::cerr << "case " << count << " "
                  << QJsonDocument(r).toJson().constData() << " actual "
                  << QJsonDocument(QJsonArray{actual}).toJson().constData()
                  << " error " << error.toStdString() << '\n';
        return 1;
      }
      ++count;
    }
    std::cout << count << " oracle cases passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
