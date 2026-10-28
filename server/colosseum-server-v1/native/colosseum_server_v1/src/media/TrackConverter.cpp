#include "server1/media/TrackConverter.h"
#include "server1/media/FfmpegArguments.h"
#include "server1/media/HlsPlaylist.h"
#include "server1/media/Mp4Timeline.h"
#include "server1/media/SegmentLayout.h"
#include "server1/media/VttTimeline.h"
#include <QJsonArray>
#include <QPointer>
#include <QTimer>
#include <map>
#include <optional>
#include <utility>
namespace server1::media {
namespace {
TrackConversionResult failure(QString name, QString error) {
  return {{}, std::move(name), std::move(error)};
}
TrackConversionResult canceled() {
  return failure("SegmentCanceledError", "Segment is canceled");
}
TrackConversionResult destroyedResult() {
  return failure("ConverterDestroyedError", "MediaConverter is destroyed");
}
} // namespace
struct TrackConverter::Impl : std::enable_shared_from_this<Impl> {
  QJsonObject input, options, subtitles;
  TrackConversionBackend backend;
  QString track;
  bool dead = false, initializing = false, initialized = false,
       processInit = false, ended = false, retried = false, preroll = false;
  QString initError, processError;
  int exitCode = -1, sequence = 0, target = 0, readSequence = 0, part = 0;
  quint64 generation = 0, process = 0;
  double timescale = 0, partDuration = 0;
  QByteArray initBuffer, pending, partBuffer;
  QJsonArray layout;
  QVector<QJsonArray> cueGroups;
  std::map<int, QByteArray> cache;
  std::vector<Callback> initWaiters, waiters;
  bool av() const { return track == "audio" || track == "video"; }
  void stop() {
    ++generation;
    auto old = std::exchange(process, 0);
    sequence = 0;
    pending.clear();
    if (old && backend.cancel)
      backend.cancel(old);
  }
  void finishWaiters(TrackConversionResult result) {
    if (process && backend.pause)
      backend.pause(process);
    auto list = std::move(waiters);
    waiters.clear();
    for (auto &cb : list)
      cb(result);
  }
  void failInit(QString error) {
    initializing = false;
    initialized = false;
    initError = error;
    initBuffer.clear();
    layout = {};
    options = {};
    stop();
    auto list = std::move(initWaiters);
    initWaiters.clear();
    for (auto &cb : list)
      cb(failure("Error", error));
  }
  void ensure(Callback cb) {
    if (dead) {
      cb(destroyedResult());
      return;
    }
    if (!initError.isEmpty()) {
      cb(failure("Error", initError));
      return;
    }
    if (initialized) {
      cb({});
      return;
    }
    initWaiters.push_back(std::move(cb));
    if (initializing)
      return;
    initializing = true;
    options = FfmpegArguments::conversionOptions(input, subtitles);
    if (backend.prepareOptions)
      backend.prepareOptions(options);
    spawn(options, true);
  }
  void ready() {
    try {
      auto stream = input["stream"].toObject();
      if (av()) {
        timescale = Mp4Timeline::timescales(initBuffer).value(1);
        if (track == "audio" &&
            options["audio"].toObject()["transcode"].toBool() &&
            options["audio"]
                    .toObject()["override"]
                    .toObject()["sampleRate"]
                    .toDouble() != timescale)
          throw std::runtime_error(
              "Output audio timescale does not match the sample rate");
        if (options[track].toObject()["transcode"].toBool())
          layout = SegmentLayout::predict(stream, input["format"].toObject(),
                                          options["segmentDuration"].toDouble(),
                                          timescale);
        else {
          auto opts = input;
          opts["timescale"] = timescale;
          opts["segmentDuration"] = options["segmentDuration"];
          layout = SegmentLayout::retrieve(opts);
        }
      } else {
        timescale = stream["timescale"].toDouble();
        layout = SegmentLayout::predict(stream, input["format"].toObject(), 10,
                                        timescale);
      }
      if (process && backend.pause)
        backend.pause(process);
      cueGroups.resize(layout.size());
      initialized = true;
      initializing = false;
      auto list = std::move(initWaiters);
      initWaiters.clear();
      for (auto &cb : list)
        cb({});
    } catch (const std::exception &e) {
      failInit(QString::fromUtf8(e.what()));
    }
  }
  void spawn(QJsonObject opts, bool init) {
    const auto gen = ++generation;
    pending.clear();
    ended = false;
    processError.clear();
    exitCode = -1;
    processInit = av();
    auto weak = weak_from_this();
    if (!backend.start) {
      if (init)
        failInit("Conversion process backend is unavailable");
      else
        finishWaiters(
            failure("Error", "Conversion process backend is unavailable"));
      return;
    }
    process = backend.start(
        opts,
        [weak, gen, init](QByteArray data) {
          if (auto s = weak.lock(); s && !s->dead && s->generation == gen) {
            s->pending += data;
            s->pump(init);
          }
        },
        [weak, gen, init](int code, QString error) {
          if (auto s = weak.lock(); s && !s->dead && s->generation == gen) {
            s->ended = true;
            s->exitCode = code;
            s->processError = error;
            s->pump(init);
          }
        });
    if (!av() && init)
      ready();
  }
  void seek(bool blank = false) {
    auto seekOptions = options;
    int seekSeq = target > 1 ? target - 1 : target;
    seekOptions["sequenceNumber"] =
        layout[seekSeq - 1].toObject()["sequenceNumber"];
    seekOptions["time"] =
        layout[seekSeq - 1].toObject()["seekTime"].toDouble() / timescale;
    if (blank)
      seekOptions["blank"] = true;
    sequence = target;
    readSequence = seekSeq;
    preroll = seekSeq != target;
    part = 0;
    partDuration = 0;
    partBuffer.clear();
    spawn(seekOptions, false);
  }
  void request(int n, Callback cb) {
    if (dead) {
      cb(destroyedResult());
      return;
    }
    if (n < 1 || n > layout.size()) {
      cb(failure("Error", "Sequence number out of range"));
      return;
    }
    auto found = cache.find(n);
    if (found != cache.end()) {
      cb({found->second, {}, {}});
      return;
    }
    if (!waiters.empty() && target == n) {
      waiters.push_back(std::move(cb));
      return;
    }
    if (!waiters.empty()) {
      auto old = std::move(waiters);
      waiters.clear();
      stop();
      for (auto &x : old)
        x(canceled());
      if (dead) {
        cb(destroyedResult());
        return;
      }
    }
    target = n;
    waiters.push_back(std::move(cb));
    retried = false;
    if (n != sequence) {
      stop();
      cache.clear();
      cueGroups.fill({});
      seek();
    } else {
      if (av())
        cache.clear();
      readSequence = n;
      preroll = false;
      part = 0;
      partDuration = 0;
      partBuffer.clear();
      pump(false);
      if (!waiters.empty() && process && backend.resume)
        backend.resume(process);
    }
  }
  void readFailure(QString error) {
    if (preroll) {
      preroll = false;
      readSequence = target;
      cache.clear();
      part = 0;
      partDuration = 0;
      partBuffer.clear();
      pump(false);
      return;
    }
    if (!ended)
      return;
    if (av() && exitCode == 0 && !retried) {
      retried = true;
      cache.clear();
      stop();
      seek(true);
      return;
    }
    stop();
    finishWaiters(
        failure("Error", error.isEmpty() ? "Unexpected end of stream" : error));
  }
  void pump(bool init) {
    if (dead)
      return;
    try {
      if (processInit) {
        auto data = Mp4Timeline::takeUntil(pending, "moov", ended);
        if (!data)
          return;
        processInit = false;
        if (init) {
          initBuffer = *data;
          ready();
          return;
        }
      }
      if (init || waiters.empty())
        return;
      if (!av()) {
        pumpSubtitles();
        return;
      }
      while (!waiters.empty()) {
        const auto segment = layout[readSequence - 1].toObject();
        const auto extras = segment["extraSegments"].toArray();
        const double expected =
            part == 0 ? segment["duration"].toDouble()
                      : extras[part - 1].toObject()["duration"].toDouble();
        auto data = Mp4Timeline::takeUntil(pending, "mdat", ended);
        if (!data)
          return;
        auto timing = Mp4Timeline::sidx(*data);
        Mp4Timeline::updateBaseMediaDecodeTime(*data,
                                               timing.earliestPresentationTime);
        partBuffer += *data;
        partDuration += timing.duration;
        if (track == "video" && partDuration < expected)
          continue;
        if (track == "video" && part < extras.size()) {
          ++part;
          partDuration = 0;
          continue;
        }
        if (preroll) {
          preroll = false;
          readSequence = target;
          cache.clear();
          partBuffer.clear();
          partDuration = 0;
          part = 0;
          continue;
        }
        cache[target] = partBuffer;
        sequence = target + 1;
        if (sequence > layout.size())
          stop();
        auto result = TrackConversionResult{partBuffer, {}, {}};
        partBuffer.clear();
        finishWaiters(result);
        return;
      }
    } catch (const std::exception &e) {
      if (init && initializing)
        failInit(!processError.isEmpty() ? processError
                 : ended                 ? QStringLiteral("stream ended")
                                         : QString::fromUtf8(e.what()));
      else if (processInit) {
        stop();
        finishWaiters(failure("Error", ended ? QStringLiteral("stream ended")
                                             : QString::fromUtf8(e.what())));
      } else
        readFailure(ended
                        ? QStringLiteral("stream ended")
                        : (processError.isEmpty() ? QString::fromUtf8(e.what())
                                                  : processError));
    }
  }
  void pumpSubtitles() {
    if (readSequence == layout.size() && !ended)
      return;
    if (pending.isEmpty() && !ended)
      return;
    if (ended && exitCode != 0 && pending.isEmpty()) {
      readFailure(processError);
      return;
    }
    auto text = std::exchange(pending, QByteArray{});
    const auto groups = VttTimeline::partition(
        VttTimeline::parse(QString::fromUtf8(text)), layout.size());
    for (int n = 0; n < groups.size(); ++n)
      for (auto cue : groups[n])
        cueGroups[n].append(cue);
    int last = 0;
    if ((ended && exitCode == 0) || readSequence == layout.size())
      last = layout.size();
    else
      for (int n = cueGroups.size() - 1; n >= 0; --n)
        if (!cueGroups[n].isEmpty()) {
          last = n;
          break;
        }
    for (int n = readSequence; n <= last; ++n)
      if (!cache.count(n))
        cache[n] = VttTimeline::format(cueGroups[n - 1]).toUtf8();
    if (!cache.count(readSequence))
      return;
    if (preroll) {
      preroll = false;
      readSequence = target;
      if (!cache.count(target))
        return;
    }
    auto data = cache[target];
    sequence = cache.empty() ? 0 : cache.rbegin()->first + 1;
    if (sequence > layout.size())
      stop();
    finishWaiters({data, {}, {}});
  }
  void destroy() {
    if (dead)
      return;
    dead = true;
    stop();
    initializing = false;
    initialized = false;
    options = {};
    layout = {};
    initBuffer.clear();
    initError.clear();
    cache.clear();
    auto list = std::move(initWaiters);
    initWaiters.clear();
    for (auto &cb : list)
      cb(destroyedResult());
    finishWaiters(canceled());
  }
};
TrackConverter::TrackConverter(QJsonObject input,
                               TrackConversionBackend backend, QObject *parent)
    : QObject(parent), impl_(std::make_shared<Impl>()) {
  impl_->input = std::move(input);
  impl_->track = impl_->input["stream"].toObject()["track"].toString();
  impl_->backend = std::move(backend);
}
TrackConverter::TrackConverter(QJsonObject input, ProcessDriver &driver,
                               QJsonObject settings, int cpu, QObject *parent)
    : TrackConverter(
          std::move(input),
          TrackConversionBackend{
              [&driver, settings, cpu](const QJsonObject &o, auto output,
                                       auto complete) {
                if (!qgetenv("HLSV2_REMOTE").isEmpty()) {
                  QTimer::singleShot(0, [complete] {
                    complete(-1, "Remote streaming conversion requires a "
                                 "remote stream backend");
                  });
                  return quint64(0);
                }
                DriverSpec spec;
                spec.process.backpressuredOutput = true;
                spec.process.program = o["ffmpeg"].toString();
                spec.process.arguments =
                    FfmpegArguments::build(o, settings, cpu);
                spec.process.arguments << "pipe:1";
                ProcessCallbacks cb;
                cb.onStdout = std::move(output);
                cb.onFinished = [complete](const ProcessResult &r) {
                  complete(r.exitCode, r.errorString);
                };
                return driver.start(spec, std::move(cb));
              },
              [&driver](quint64 id) { (void)driver.cancel(id); },
              [&driver](quint64 id) { (void)driver.pauseOutput(id); },
              [&driver](quint64 id) { (void)driver.resumeOutput(id); },
              [settings, cpu](QJsonObject &o) {
                if (o.value("profile").toString().isEmpty() &&
                    settings["transcodeHardwareAccel"].toBool() &&
                    !settings["transcodeProfile"].toString().isEmpty())
                  o["profile"] = settings["transcodeProfile"];
                auto args = FfmpegArguments::build(o, settings, cpu);
                args << "pipe:1";
                o["args"] = QJsonArray::fromStringList(args);
              }},
          parent) {}
TrackConverter::~TrackConverter() { impl_->destroy(); }
void TrackConverter::playlist(Callback cb) {
  auto s = impl_;
  s->ensure([s, cb](const auto &r) {
    if (!r.ok()) {
      cb(r);
      return;
    }
    cb({HlsPlaylist::media(s->layout, s->timescale,
                           s->options["track"].toString(),
                           s->input["query"].toString())
            .toUtf8(),
        {},
        {}});
  });
}
void TrackConverter::initSegment(Callback cb) {
  auto s = impl_;
  if (!s->av()) {
    cb(s->dead ? destroyedResult()
               : failure("Error",
                         "init segment is available only for A/V streams"));
    return;
  }
  s->ensure([s, cb](const auto &r) {
    cb(r.ok() ? TrackConversionResult{s->initBuffer, {}, {}} : r);
  });
}
void TrackConverter::mediaSegment(int n, Callback cb) {
  auto s = impl_;
  s->ensure([s, n, cb](const auto &r) {
    if (!r.ok())
      cb(r);
    else
      s->request(n, cb);
  });
}
void TrackConverter::burnSubtitles(QJsonObject sub, Callback cb) {
  auto s = impl_;
  if (s->dead) {
    cb(destroyedResult());
    return;
  }
  if (s->initializing) {
    QPointer<TrackConverter> self(this);
    s->initWaiters.push_back([self, sub, cb](const auto &) {
      if (self)
        self->burnSubtitles(sub, cb);
      else
        cb(destroyedResult());
    });
    return;
  }
  if (s->track != "video" ||
      (!s->options.isEmpty() &&
       !s->options["video"].toObject()["transcode"].toBool())) {
    cb(failure(
        "Error",
        "Burning subtitles only allowed for the transcoded video track"));
    return;
  }
  s->stop();
  s->finishWaiters(canceled());
  s->initError.clear();
  s->initialized = false;
  s->initBuffer.clear();
  s->layout = {};
  s->cache.clear();
  s->subtitles = std::move(sub);
  s->ensure(std::move(cb));
}
QJsonObject TrackConverter::status() const {
  return {
      {"convertOptions",
       impl_->options.isEmpty() ? QJsonValue() : QJsonValue(impl_->options)},
      {"sequenceNumber",
       impl_->sequence ? QJsonValue(impl_->sequence) : QJsonValue()},
      {"initError", impl_->initError.isEmpty() ? QJsonValue()
                                               : QJsonValue(impl_->initError)},
      {"init", !impl_->initBuffer.isNull()}};
}
void TrackConverter::status(std::function<void(QJsonObject, QString)> cb) {
  auto s = impl_;
  QPointer<TrackConverter> self(this);
  s->ensure([s, self, cb](const auto &) {
    if (s->dead || !self) {
      cb({}, "MediaConverter is destroyed");
      return;
    }
    cb(self->status(), {});
  });
}
void TrackConverter::destroy() { impl_->destroy(); }
} // namespace server1::media
