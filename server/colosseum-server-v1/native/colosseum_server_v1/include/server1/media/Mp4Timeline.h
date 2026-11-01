#pragma once
#include <QByteArray>
#include <QJsonArray>
#include <QMap>
#include <optional>
namespace server1::media {
struct Mp4SegmentTiming {
  quint64 earliestPresentationTime = 0;
  quint64 duration = 0;
  quint32 timescale = 0;
};
class Mp4Timeline {
public:
  // Incremental readUntilBox (M468/920/921). Incomplete data returns nullopt;
  // finish=true makes incomplete/missing target an error. Consumes through
  // target.
  static std::optional<QByteArray>
  takeUntil(QByteArray &pending, const QByteArray &type, bool finish = false);
  static Mp4SegmentTiming sidx(const QByteArray &bytes);
  static QMap<quint32, quint32> timescales(const QByteArray &init);
  // Source M922: absent moof/matching traf/tfdt is a no-op; malformed boxes
  // fail.
  static bool updateBaseMediaDecodeTime(QByteArray &bytes, double value,
                                        quint32 trackId = 1);
};
} // namespace server1::media
