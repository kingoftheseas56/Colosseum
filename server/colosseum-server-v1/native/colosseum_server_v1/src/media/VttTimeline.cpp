#include "server1/media/VttTimeline.h"
#include "server1/media/HlsPlaylist.h"
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>
#include <cmath>
#include <stdexcept>
namespace server1::media {
namespace {
QString formatTime(double t) {
  auto pad = [](QString v, double n) { return n < 10 ? "0" + v : v; };
  double h = std::floor(t / 3600), m = std::fmod(std::floor(t / 60), 60),
         s = std::fmod(t, 60);
  QString seconds = HlsPlaylist::fixed(s, 3);
  return pad(QString::number(h, 'f', 0), h) + ":" +
         pad(QString::number(m, 'f', 0), m) + ":" +
         pad(seconds, seconds.toDouble());
}
qint32 jsInt(const QString &s) {
  double x = s.toDouble();
  if (!std::isfinite(x))
    return 0;
  double u = std::fmod(x, 4294967296.0);
  if (u < 0)
    u += 4294967296.0;
  return u >= 2147483648.0 ? qint32(u - 4294967296.0) : qint32(u);
}
double stamp(QString &s, const QString &original) {
  static const QRegularExpression re("^(\\d+):(\\d{2})(:\\d{2})?\\.(\\d{3})");
  auto m = re.match(s);
  if (!m.hasMatch())
    throw std::runtime_error(
        ("Malformed timestamp: " + original).toStdString());
  double h = 0, mins = 0, sec = 0;
  if (!m.captured(3).isEmpty()) {
    h = jsInt(m.captured(1));
    mins = jsInt(m.captured(2));
    sec = jsInt(m.captured(3).mid(1));
  } else if (m.captured(1).toDouble() > 59) {
    h = jsInt(m.captured(1));
    mins = jsInt(m.captured(2));
  } else {
    mins = jsInt(m.captured(1));
    sec = jsInt(m.captured(2));
  }
  s.remove(QRegularExpression("^[^\\sa-zA-Z-]+"));
  return h * 3600 + mins * 60 + sec + jsInt(m.captured(4)) / 1000.0;
}
} // namespace
QString VttTimeline::format(const QJsonArray &cues) {
  QStringList lines;
  for (auto x : cues) {
    auto c = x.toObject();
    lines << formatTime(c["startTime"].toDouble()) + " --> " +
                 formatTime(c["endTime"].toDouble()) + "\n" +
                 c["text"].toString() + "\n";
  }
  return "WEBVTT\n\n" + lines.join('\n');
}
QJsonArray VttTimeline::parse(QString text) {
  for (qsizetype i = 0; i < text.size(); ++i) {
    if (text[i].isHighSurrogate()) {
      if (i + 1 >= text.size() || !text[i + 1].isLowSurrogate())
        throw std::runtime_error("URI malformed");
      ++i;
    } else if (text[i].isLowSurrogate())
      throw std::runtime_error("URI malformed");
  }

  if (!text.startsWith("WEBVTT"))
    text.prepend("WEBVTT\n\n");
  // StringDecoder accepts UTF-16 text. collectNextLine normalizes CRLF and LF.
  QString buffer = text;
  enum State { Initial, Header, Id, Note, Cue, CueText, BadCue, BadWeb };
  State state = Initial;
  QJsonArray out;
  QJsonObject cue;
  QString firstError;
  auto error = [&](QString e) {
    if (firstError.isEmpty())
      firstError = e;
  };
  auto run = [&]() {
    QString line;
    bool again = false;
    while (!buffer.isEmpty()) {
      if (!buffer.contains('\n'))
        return;
      if (again) {
        again = false;
      } else {
        int pos = 0;
        while (pos < buffer.size() && buffer[pos] != '\r' &&
               buffer[pos] != '\n')
          ++pos;
        line = buffer.left(pos);
        if (pos < buffer.size() && buffer[pos] == '\r')
          ++pos;
        if (pos < buffer.size() && buffer[pos] == '\n')
          ++pos;
        buffer.remove(0, pos);
      }
      switch (state) {
      case Initial:
        if (!QRegularExpression("^WEBVTT([ \\t].*)?$").match(line).hasMatch()) {
          error("Malformed WebVTT signature.");
          state = BadWeb;
        } else
          state = Header;
        break;
      case Header: {
        auto fields = line.split(':');
        if (fields.size() == 2 && fields[0] == "Region") {
          for (const auto &token : fields[1].split(QRegularExpression("\\s"))) {
            auto pair = token.split('=');
            if (pair.size() == 2 && pair[0] == "id" && !pair[1].isEmpty())
              throw std::runtime_error(
                  "self.window.VTTRegion is not a constructor");
          }
        }
        if (line.isEmpty())
          state = Id;
        break;
      }
      case Note:
      case BadCue:
        if (line.isEmpty())
          state = Id;
        break;
      case Id:
        if (QRegularExpression("^NOTE($|[ \\t])").match(line).hasMatch()) {
          state = Note;
          break;
        }
        if (line.isEmpty())
          break;
        cue = QJsonObject{{"startTime", 0}, {"endTime", 0}, {"text", ""}};
        state = Cue;
        if (!line.contains("-->"))
          break;
        [[fallthrough]];
      case Cue: {
        QString s = line.trimmed();
        try {
          double start = stamp(s, line);
          s = s.trimmed();
          if (!s.startsWith("-->"))
            throw std::runtime_error(("Malformed time stamp (time stamps must "
                                      "be separated by '-->'): " +
                                      line)
                                         .toStdString());
          s.remove(0, 3);
          s = s.trimmed();
          double end = stamp(s, line);
          cue["startTime"] = start;
          cue["endTime"] = end;
          state = CueText;
        } catch (const std::exception &e) {
          error(QString::fromUtf8(e.what()));
          cue = {};
          state = BadCue;
        }
        break;
      }
      case CueText:
        if (line.isEmpty() || line.contains("-->")) {
          out.append(cue);
          cue = {};
          state = Id;
          again = !line.isEmpty();
        } else {
          QString value = cue["text"].toString();
          if (!value.isEmpty())
            value += '\n';
          cue["text"] = value + line;
        }
        break;
      case BadWeb:
        break;
      }
    }
  };
  run();
  if (!cue.isEmpty() || state == Header) {
    buffer += "\n\n";
    run();
  }
  if (state == Initial)
    error("Malformed WebVTT signature.");
  if (out.isEmpty() && !firstError.isEmpty())
    throw std::runtime_error(firstError.toStdString());
  return out;
}
QVector<QJsonArray> VttTimeline::partition(const QJsonArray &cues,
                                           int segmentCount) {
  if (segmentCount < 0)
    throw std::invalid_argument("Invalid subtitle segment count");
  QVector<QJsonArray> out(segmentCount);
  for (auto v : cues) {
    auto c = v.toObject();
    double first = std::floor(c["startTime"].toDouble() / 10),
           last = std::floor(c["endTime"].toDouble() / 10);
    if (!std::isfinite(first) || !std::isfinite(last))
      throw std::invalid_argument("Invalid subtitle timing");
    if (last < first)
      continue;
    if (first < 0 || last >= segmentCount)
      throw std::out_of_range("Subtitle cue sequence out of range");
    for (int i = int(first); i <= int(last); ++i)
      out[i].append(v);
  }
  return out;
}
} // namespace server1::media
