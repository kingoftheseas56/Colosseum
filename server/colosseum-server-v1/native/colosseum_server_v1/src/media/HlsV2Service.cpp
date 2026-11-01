#include "server1/media/HlsV2Service.h"
#include "server1/media/HlsPlaylist.h"
#include "server1/media/KeyframeIndex.h"
#include "server1/media/Probe.h"
#include <QDateTime>
#include <QPointer>
#include <QRegularExpression>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <vector>
namespace server1::media {
namespace {
const QString dead = QStringLiteral("MasterConverter is destroyed");
TrackConversionResult failure(QString error) {
  return {{},
          error == dead ? QStringLiteral("ConverterDestroyedError")
                        : QStringLiteral("Error"),
          error};
}
bool truth(const QJsonValue &v) {
  return !(v.isUndefined() || v.isNull() || (v.isBool() && !v.toBool()) ||
           (v.isDouble() && v.toDouble() == 0) ||
           (v.isString() && v.toString().isEmpty()));
}
QString numberString(double n) {
  if (!std::isfinite(n))
    return {};
  if (n == 0)
    return "0";
  char buffer[128];
  auto end = std::to_chars(buffer, buffer + sizeof buffer, n);
  QString value = QString::fromLatin1(buffer, int(end.ptr - buffer));
  int at = value.indexOf('e');
  if (at < 0)
    return value;
  int exponent = value.mid(at + 1).toInt();
  QString digits = value.left(at);
  bool negative = digits.startsWith('-');
  if (negative)
    digits.remove(0, 1);
  digits.remove('.');
  if (exponent >= -6 && exponent < 21) {
    int position = exponent + 1;
    if (position <= 0)
      digits = "0." + QString(-position, '0') + digits;
    else if (position >= digits.size())
      digits += QString(position - digits.size(), '0');
    else
      digits.insert(position, '.');
    return (negative ? "-" : "") + digits;
  }
  QString mantissa = value.left(at);
  return mantissa + "e" + (exponent >= 0 ? "+" : "") +
         QString::number(exponent);
}
QString scalar(const QJsonValue &v) {
  if (v.isString())
    return v.toString();
  if (v.isBool())
    return v.toBool() ? "true" : "false";
  if (v.isDouble())
    return numberString(v.toDouble());
  return {};
}
QString enc(const QString &s) {
  return QString::fromLatin1(QUrl::toPercentEncoding(s, "!'()*-._~"));
}
void samplesFor(QJsonObject value, QString url, QVector<int> indexes,
                std::function<void(QJsonObject, QString)> callback,
                QObject *owner, bool strict) {
  const QString name = value["format"].toObject()["name"].toString();
  auto done = [value, callback, strict](SamplesResult s) mutable {
    value["samples"] = s.tracks;
    callback(value, strict ? s.error : QString());
  };
  if (name.contains("mp4"))
    retrieveMp4Samples(QUrl(url), indexes, done, owner);
  else if (name.contains("matroska"))
    retrieveMkvSamples(QUrl(url), indexes, done, owner);
  else {
    value["samples"] = QJsonObject{};
    callback(value, {});
  }
}
} // namespace
struct HlsV2Converter::Impl : std::enable_shared_from_this<Impl> {
  QJsonObject options, settings, probeValue;
  QString query, probeError, ffmpeg;
  int cpu = 1, trackLimit = 1;
  QPointer<Probe> probe;
  QPointer<ProcessDriver> driver;
  std::unique_ptr<QObject> life = std::make_unique<QObject>();
  std::function<qint64()> now;
  bool destroyed = false, probing = false;
  quint64 probeId = 0;
  using Ready = std::function<void(QJsonObject, QString)>;
  std::vector<Ready> waiters;
  struct Slot {
    QString key, error;
    qint64 touched = 0;
    std::shared_ptr<TrackConverter> value;
    bool canceled = false;
  };
  std::vector<std::shared_ptr<Slot>> trackSlots;
  void finishProbe(QJsonObject value, QString error) {
    if (destroyed)
      error = dead;
    probing = false;
    if (error.isEmpty())
      probeValue = value;
    else if (error != dead)
      probeError = error;
    auto callbacks = std::move(waiters);
    waiters.clear();
    for (auto &cb : callbacks)
      cb(value, destroyed ? dead : error);
  }
  void getProbe(Ready cb) {
    if (destroyed) {
      cb({}, dead);
      return;
    }
    if (!probeValue.isEmpty()) {
      cb(probeValue, {});
      return;
    }
    if (!probeError.isEmpty()) {
      cb({}, probeError);
      return;
    }
    waiters.push_back(cb);
    if (probing)
      return;
    probing = true;
    auto self = shared_from_this();
    probeId = probe->start(
        options["mediaURL"].toString(), [self](const ProbeResult &r) {
          if (self->destroyed)
            return;
          if (!r.ok()) {
            self->finishProbe({}, r.error["message"].toString());
            return;
          }
          auto value = r.value;
          QJsonArray streams;
          bool video = false;
          QVector<int> indexes;
          for (auto v : value["streams"].toArray()) {
            auto s = v.toObject();
            auto t = s["track"].toString();
            if (t == "video" && !video) {
              video = true;
              streams.append(s);
              indexes.append(s["index"].toInt());
            } else if (t == "audio") {
              streams.append(s);
              indexes.append(s["index"].toInt());
            } else if (t == "subtitle")
              streams.append(s);
          }
          value["streams"] = streams;
          if (!value["format"].toObject()["duration"].isDouble()) {
            self->finishProbe({}, "Live media is not supported");
            return;
          }
          if (indexes.isEmpty()) {
            self->finishProbe({}, "No video or audio streams found");
            return;
          }
          samplesFor(
              value, self->options["mediaURL"].toString(), indexes,
              [self](QJsonObject v, QString e) { self->finishProbe(v, e); },
              self->life.get(), false);
        });
  }
  void
  getTrack(QString track,
           std::function<void(std::shared_ptr<TrackConverter>, QString)> cb) {
    if (destroyed) {
      cb({}, dead);
      return;
    }
    std::shared_ptr<Slot> slot;
    for (auto s : trackSlots)
      if (s->key == track)
        slot = s;
    if (!slot) {
      QString type = track;
      type.remove(QRegularExpression("[0-9]"));
      std::vector<std::shared_ptr<Slot>> matching;
      for (auto s : trackSlots)
        if (s->key.startsWith(type))
          matching.push_back(s);
      std::stable_sort(matching.begin(), matching.end(),
                       [](auto a, auto b) { return a->touched > b->touched; });
      int start = trackLimit - 1;
      if (start < 0)
        start = std::max(0, int(matching.size()) + start);
      for (int i = start; i < int(matching.size()); ++i) {
        auto s = matching[i];
        s->canceled = true;
        trackSlots.erase(std::remove(trackSlots.begin(), trackSlots.end(), s),
                         trackSlots.end());
      }
      slot = std::make_shared<Slot>();
      slot->key = track;
      trackSlots.push_back(slot);
      for (int i = start; i < int(matching.size()); ++i)
        if (matching[i]->value)
          matching[i]->value->destroy();
    }
    slot->touched = now();
    if (slot->value) {
      cb(slot->value, {});
      return;
    }
    if (!slot->error.isEmpty()) {
      cb({}, slot->error);
      return;
    }
    auto self = shared_from_this();
    getProbe([self, slot, track, cb](QJsonObject p, QString error) {
      if (self->destroyed || slot->canceled) {
        cb({}, dead);
        return;
      }
      if (!error.isEmpty()) {
        slot->error = error;
        cb({}, error);
        return;
      }
      if (slot->value) {
        cb(slot->value, {});
        return;
      }
      QJsonObject stream;
      for (auto v : p["streams"].toArray()) {
        auto s = v.toObject();
        if (s["track"].toString() + scalar(s["id"]) == track) {
          stream = s;
          break;
        }
      }
      if (stream.isEmpty()) {
        slot->error = "Track not found";
        cb({}, slot->error);
        return;
      }
      auto input = self->options;
      input["ffmpeg"] = self->ffmpeg;
      input["stream"] = stream;
      input["format"] = p["format"];
      input["query"] = self->query;
      auto sm = p["samples"].toObject()[scalar(stream["index"])].toObject();
      input["samples"] =
          sm.contains("samples") ? sm["samples"] : QJsonValue(QJsonArray{});
      input["sampleDuration"] =
          sm.contains("sampleDuration") ? sm["sampleDuration"] : QJsonValue();
      try {
        slot->value = std::make_shared<TrackConverter>(
            input, *self->driver, self->settings, self->cpu);
      } catch (const std::exception &e) {
        slot->error = QString::fromUtf8(e.what());
        cb({}, slot->error);
        return;
      }
      cb(slot->value, {});
    });
  }
  void stop() {
    if (destroyed)
      return;
    destroyed = true;
    auto active = trackSlots;
    trackSlots.clear();
    for (auto s : active) {
      s->canceled = true;
      if (s->value)
        s->value->destroy();
    }
    if (probe && probing)
      probe->cancel(probeId);
    life.reset();
    finishProbe({}, dead);
  }
};
HlsV2Converter::HlsV2Converter(std::shared_ptr<Impl> p) : impl_(std::move(p)) {}
HlsV2Converter::~HlsV2Converter() { destroy(); }
void HlsV2Converter::destroy() {
  auto p = impl_;
  p->stop();
}
void HlsV2Converter::playlist(QString track, Callback cb) {
  auto p = impl_;
  if (track == "master") {
    p->getProbe([p, cb](QJsonObject v, QString e) {
      if (!e.isEmpty()) {
        cb(failure(e));
        return;
      }
      QJsonArray streams;
      QStringList ignored = {"dvb_subtitle", "dvd_subtitle",
                             "hdmv_pgs_subtitle", "xsub"};
      for (auto s : v["streams"].toArray())
        if (!ignored.contains(s.toObject()["codec"].toString()))
          streams.append(s);
      cb({HlsPlaylist::master(streams, p->query).toUtf8(), {}, {}});
    });
  } else
    p->getTrack(track, [cb](auto t, QString e) {
      if (!e.isEmpty())
        cb(failure(e));
      else
        t->playlist(cb);
    });
}
void HlsV2Converter::initSegment(QString track, Callback cb) {
  impl_->getTrack(track, [cb](auto t, QString e) {
    if (!e.isEmpty())
      cb(failure(e));
    else
      t->initSegment(cb);
  });
}
void HlsV2Converter::mediaSegment(QString track, int seq, Callback cb) {
  impl_->getTrack(track, [cb, seq](auto t, QString e) {
    if (!e.isEmpty())
      cb(failure(e));
    else
      t->mediaSegment(seq, cb);
  });
}
void HlsV2Converter::burnSubtitles(QJsonObject s, Callback cb) {
  impl_->getTrack("video0", [cb, s](auto t, QString e) {
    if (!e.isEmpty())
      cb(failure(e));
    else
      t->burnSubtitles(s, cb);
  });
}
void HlsV2Converter::status(std::function<void(QJsonObject, QString)> cb) {
  auto p = impl_;
  if (p->destroyed) {
    cb({}, dead);
    return;
  }
  struct State {
    QStringList keys;
    int i = 0;
    QJsonObject result, tracks;
    std::function<void(QJsonObject, QString)> cb;
  };
  auto state = std::make_shared<State>();
  state->cb = cb;
  state->result = {
      {"query", p->query},
      {"probe",
       QJsonObject{{"value", p->probeValue.isEmpty()
                                 ? QJsonValue()
                                 : QJsonValue(p->probeValue)},
                   {"error", p->probeError.isEmpty()
                                 ? QJsonValue()
                                 : QJsonValue("Error: " + p->probeError)}}}};
  for (auto s : p->trackSlots)
    state->keys.append(s->key);
  auto loop = std::make_shared<std::function<void()>>();
  std::weak_ptr<std::function<void()>> weak = loop;
  *loop = [p, state, weak] {
    auto keep = weak.lock();
    if (!keep)
      return;
    if (state->i == state->keys.size()) {
      state->result["converters"] = state->tracks;
      state->cb(state->result, {});
      return;
    }
    QString key = state->keys[state->i++];
    p->getTrack(key, [state, key, keep](auto track, QString error) {
      if (!error.isEmpty()) {
        state->tracks[key] =
            (error == dead ? "ConverterDestroyedError: " : "Error: ") + error;
        (*keep)();
      } else
        track->status([state, key, keep](QJsonObject value, QString e) {
          state->tracks[key] =
              e.isEmpty() ? QJsonValue(value)
                          : QJsonValue((e == dead ? "ConverterDestroyedError: "
                                                  : "Error: ") +
                                       e);
          (*keep)();
        });
    });
  };
  (*loop)();
}
struct HlsV2Service::Impl {
  bool shutting = false;
  QString ffmpeg;
  Probe probe;
  ProcessPort port;
  ProcessDriver driver;
  QTimer timer;
  int sources = 1, tracks = 1, cpu = QThread::idealThreadCount();
  QJsonObject settings{{"transcodeHorsepower", 0.75},
                       {"transcodeMaxBitRate", 0},
                       {"transcodeMaxWidth", 1920},
                       {"transcodeHardwareAccel", true},
                       {"transcodeProfile", QJsonValue()}};
  std::function<qint64()> now = [] {
    return QDateTime::currentMSecsSinceEpoch();
  };
  struct Entry {
    QString id;
    std::shared_ptr<HlsV2Converter> converter;
    qint64 touched;
  };
  std::vector<Entry> entries;
  Impl(QString f, QString p) : ffmpeg(f), probe(p), driver(port) {}
};
HlsV2Service::HlsV2Service(QString f, QString p, QObject *parent)
    : QObject(parent), impl_(std::make_unique<Impl>(f, p)) {
  connect(&impl_->timer, &QTimer::timeout, this, [this] { cleanup(); });
  impl_->timer.start(120000);
}
HlsV2Service::~HlsV2Service() {
  impl_->shutting = true;
  auto es = impl_->entries;
  impl_->entries.clear();
  for (auto &e : es)
    e.converter->destroy();
}
QString HlsV2Service::queryString(const QJsonObject &o, QString *error) {
  if (error)
    error->clear();
  auto fail = [&](QString e) {
    if (error)
      *error = e;
    return QString();
  };
  if (!o["mediaURL"].isString() || o["mediaURL"].toString().isEmpty())
    return fail("Invalid media url");
  if (o["maxAudioChannels"].isDouble() && o["maxAudioChannels"].toDouble() <= 0)
    return fail("Invalid audio channels");
  QStringList list;
  auto add = [&](QString k, QJsonValue v) {
    if (v.isArray()) {
      for (auto item : v.toArray())
        list << enc(k) + "=" + enc(scalar(item));
    } else
      list << enc(k) + "=" + enc(scalar(v));
  };
  add("mediaURL", o["mediaURL"]);
  if (truth(o["maxAudioChannels"]))
    add("maxAudioChannels", o["maxAudioChannels"]);
  if (truth(o["forceTranscoding"]))
    add("forceTranscoding", "1");
  for (auto k : {"videoCodecs", "audioCodecs"})
    if (o[k].isArray())
      add(k, o[k]);
  for (auto k : {"profile", "maxWidth"})
    if (truth(o[k]))
      add(k, o[k]);
  return list.join('&');
}
std::shared_ptr<HlsV2Converter>
HlsV2Service::acquire(QString id, QJsonObject options, QString *error) {
  if (impl_->shutting) {
    if (error)
      *error = dead;
    return {};
  }
  if (error)
    error->clear();
  for (auto &e : impl_->entries)
    if (e.id == id) {
      e.touched = impl_->now();
      return e.converter;
    }
  QString e;
  auto query = queryString(options, &e);
  if (!e.isEmpty()) {
    if (error)
      *error = e;
    return {};
  }
  auto p = std::make_shared<HlsV2Converter::Impl>();
  p->options = options;
  p->query = query;
  p->probe = &impl_->probe;
  p->driver = &impl_->driver;
  p->ffmpeg = impl_->ffmpeg;
  p->trackLimit = impl_->tracks;
  p->settings = impl_->settings;
  p->cpu = impl_->cpu;
  p->now = impl_->now;
  auto converter = std::shared_ptr<HlsV2Converter>(new HlsV2Converter(p));
  auto sorted = impl_->entries;
  std::stable_sort(sorted.begin(), sorted.end(),
                   [](auto &a, auto &b) { return a.touched > b.touched; });
  int start = impl_->sources - 1;
  if (start < 0)
    start = std::max(0, int(sorted.size()) + start);
  for (int i = start; i < int(sorted.size()); ++i) {
    auto old = sorted[i];
    impl_->entries.erase(
        std::remove_if(impl_->entries.begin(), impl_->entries.end(),
                       [&](auto &e) { return e.id == old.id; }),
        impl_->entries.end());
  }
  // Publish the complete registry mutation before cancellation callbacks can
  // reenter. JavaScript Promise continuations likewise see the completed turn.
  impl_->entries.push_back({id, converter, impl_->now()});
  for (int i = start; i < int(sorted.size()); ++i)
    sorted[i].converter->destroy();
  return converter;
}
void HlsV2Service::destroy(const QString &id) {
  for (auto &e : impl_->entries)
    if (e.id == id) {
      e.converter->destroy();
      return;
    }
}
bool HlsV2Service::contains(const QString &id) const {
  for (auto &e : impl_->entries)
    if (e.id == id)
      return true;
  return false;
}
void HlsV2Service::cleanup() {
  QPointer<HlsV2Service> guard(this);
  auto entries = impl_->entries;
  for (auto &e : entries)
    if (e.touched < impl_->now() - 120000) {
      impl_->entries.erase(
          std::remove_if(impl_->entries.begin(), impl_->entries.end(),
                         [&](auto &a) { return a.id == e.id; }),
          impl_->entries.end());
      e.converter->destroy();
      if (!guard)
        return;
    }
}
void HlsV2Service::setConcurrency(int s, int t) {
  impl_->sources = s;
  impl_->tracks = t;
  for (auto &e : impl_->entries)
    e.converter->impl_->trackLimit = t;
}
void HlsV2Service::setClock(std::function<qint64()> c) {
  impl_->now = std::move(c);
}
void HlsV2Service::setTranscodeSettings(QJsonObject s, int c) {
  impl_->settings = s;
  impl_->cpu = c;
}
void HlsV2Service::status(std::function<void(QJsonObject, QString)> cb) {
  struct State {
    std::vector<Impl::Entry> entries;
    size_t i = 0;
    QJsonObject out;
    std::function<void(QJsonObject, QString)> cb;
  };
  auto state = std::make_shared<State>();
  state->entries = impl_->entries;
  state->cb = cb;
  auto loop = std::make_shared<std::function<void()>>();
  std::weak_ptr<std::function<void()>> weak = loop;
  *loop = [state, weak] {
    auto keep = weak.lock();
    if (!keep)
      return;
    if (state->i == state->entries.size()) {
      state->cb(state->out, {});
      return;
    }
    auto e = state->entries[state->i++];
    e.converter->status([state, e, keep](QJsonObject v, QString error) {
      if (!error.isEmpty()) {
        state->cb({}, error);
        return;
      }
      state->out[e.id] = QJsonObject{
          {"status", v},
          {"touched", QDateTime::fromMSecsSinceEpoch(e.touched, Qt::UTC)
                          .toString(Qt::ISODateWithMs)}};
      (*keep)();
    });
  };
  (*loop)();
}
void HlsV2Service::probe(QString url, bool samples,
                         std::function<void(QJsonObject, QString)> cb) {
  QPointer<HlsV2Service> guard(this);
  impl_->probe.start(url, [guard, url, samples, cb](const ProbeResult &r) {
    if (!guard)
      return;
    if (!r.ok()) {
      cb({}, r.error["message"].toString());
      return;
    }
    if (samples)
      samplesFor(r.value, url, {}, cb, guard, true);
    else {
      auto v = r.value;
      v["samples"] = QJsonObject{};
      cb(v, {});
    }
  });
}
} // namespace server1::media
