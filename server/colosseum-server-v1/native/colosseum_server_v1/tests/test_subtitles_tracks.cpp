#include "server1/media/SubtitleFetch.h"
#include "server1/media/SubtitleHash.h"
#include "server1/media/TrackExtraction.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <iostream>
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  if (argc == 3) {
    QFile f(argv[2]);
    if (!f.open(QIODevice::ReadOnly))
      return 2;
    auto cases = QJsonDocument::fromJson(f.readAll()).array();
    for (auto c : cases) {
      auto o = c.toObject();
      QJsonValue actual;
      if (o["kind"] == "srt")
        actual = server1::media::parseSrt(o["input"].toString());
      else if (o["kind"] == "decode") {
        QString err;
        auto text = server1::media::decodeSubtitle(
            QByteArray::fromBase64(o["input"].toString().toLatin1()),
            o["charset"].toString(), &err);
        actual = QJsonObject{{"text", text}, {"error", err}};
      } else if (o["kind"] == "hash")
        actual = server1::media::subtitleHashBytes(
            QByteArray::fromBase64(o["input"].toString().toLatin1()));
      else {
        auto r = server1::media::extractTrackBytes(
            QByteArray::fromBase64(o["input"].toString().toLatin1()));
        actual = r.tracks;
      }
      if (actual != o["expected"]) {
        std::cerr << QJsonDocument(o).toJson().constData() << "actual "
                  << QJsonDocument(QJsonArray{actual}).toJson().constData();
        return 1;
      }
    }
    return 0;
  }
  auto s = server1::media::parseSrt(
      "1\r\n00:00:01,000 --> 00:00:02,300\r\na & b\r\n\r\n");
  if (s.size() != 1 || s[0].toObject()["endTime"].toInt() != 2300)
    return 1;
  if (server1::media::subtitleHashBytes(QByteArray(65536, '\0')) !=
      "0000000000010000")
    return 2;
  auto bad = server1::media::extractTrackBytes("garbage");
  if (bad.error.isEmpty())
    return 3;
  std::cout << "M02 unit checks passed\n";
}
