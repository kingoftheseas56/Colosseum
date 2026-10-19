#include "server1/media/FfmpegArguments.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <iostream>
using namespace server1::media;
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  QFile f(argv[1]);
  if (!f.open(QIODevice::ReadOnly))
    return 2;
  auto rows = QJsonDocument::fromJson(f.readAll()).array();
  int n = 0;
  for (auto x : rows) {
    auto r = x.toObject();
    QJsonValue actual;
    if (r["kind"] == "decision")
      actual = FfmpegArguments::conversionOptions(r["input"].toObject(),
                                                  r["subtitles"].toObject());
    else if (r["kind"] == "bitrate")
      actual = QJsonObject{
          {"video", FfmpegArguments::videoBitrate(r["input"].toObject(),
                                                  r["codec"].toString(),
                                                  "libx264", 8000000)},
          {"audio",
           FfmpegArguments::audioBitrate(r["input"].toObject(), "aac", 6)}};
    else
      actual = QJsonArray::fromStringList(FfmpegArguments::build(
          r["input"].toObject(), r["settings"].toObject(),
          r["cpuCores"].toInt()));
    if (actual != r["expected"]) {
      std::cerr << "Mismatch " << n << '\n'
                << QJsonDocument(QJsonArray{actual, r["expected"]})
                       .toJson()
                       .constData();
      return 1;
    }
    ++n;
  }
  std::cout << n << " exact oracle cases PASS\n";
}
