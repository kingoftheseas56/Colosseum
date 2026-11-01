#include "server1/media/FfmpegArguments.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
#include <cmath>
namespace server1::media {
QJsonObject FfmpegArguments::profiles() {
  return QJsonDocument::fromJson(
             R"PROFILE({"mediacodec":{"audio":{"decoders":{},"encoders":{}},"video":{"inputArgs":[],"decoders":{"libx264":"h264_mediacodec","hevc":"hevc_mediacodec","mpeg2":"mpeg2_mediacodec","mpeg4":"mpeg4_mediacodec","vp8":"vp8_mediacodec","vp9":"vp9_mediacodec"},"encoders":{}}},"videotoolbox":{"audio":{"decoders":{},"encoders":{"aac":"aac_at"}},"video":{"inputArgs":["-init_hw_device","videotoolbox=vt","-hwaccel","videotoolbox"],"decoders":{},"encoders":{"libx264":"h264_videotoolbox","hevc":"hevc_videotoolbox"},"pixelFormat":"nv12","extraOutputArgs":["-b:v","{bitrate}","-maxrate","{bitrate}","-bufsize","{bufsize}"]}},"qsv-win":{"audio":{"decoders":{},"encoders":{}},"video":{"inputArgs":["-init_hw_device","d3d11va=dx11:,vendor=0x8086","-init_hw_device","qsv=qs@dx11","-filter_hw_device","qs","-hwaccel","d3d11va","-hwaccel_output_format","d3d11","-threads","3"],"decoders":{},"encoders":{"libx264":"h264_qsv","hevc":"hevc_qsv","vp9":"vp9_qsv","mpeg2video":"mpeg2_qsv"},"preset":"veryfast","pixelFormat":"nv12","scalePrefix":"hwmap=derive_device=qsv,","scale":"scale_qsv","resizeHeightAuto":"-1","scaleExtra":"","wrapSwFilters":["hwdownload","hwupload"],"hwFilters":["hwmap=derive_device=qsv","format=qsv"],"extraOutputArgs":["-look_ahead","0","-b:v","{bitrate}","-maxrate","{bitrate}","-bufsize","{bufsize}"]}},"qsv-linux":{"audio":{"decoders":{},"encoders":{}},"video":{"inputArgs":["-init_hw_device","vaapi=va:,driver=iHD,kernel_driver=i915","-init_hw_device","qsv=qs@va","-filter_hw_device","qs","-hwaccel","vaapi","-hwaccel_output_format","vaapi"],"decoders":{},"encoders":{"libx264":"h264_qsv","hevc":"hevc_qsv","vp9":"vp9_qsv","mpeg2video":"mpeg2_qsv"},"preset":"veryfast","pixelFormat":"nv12","scale":"scale_vaapi","scaleExtra":":mode=fast","wrapSwFilters":["hwdownload","hwupload_vaapi"],"hwFilters":["hwmap=derive_device=qsv","format=qsv"],"extraOutputArgs":["-look_ahead","0","-b:v","{bitrate}","-maxrate","{bitrate}","-bufsize","{bufsize}"]}},"amf":{"audio":{"decoders":{},"encoders":{}},"video":{"inputArgs":["-init_hw_device","d3d11va=dx11:,vendor=0x1002","-init_hw_device","opencl=ocl@dx11","-filter_hw_device","ocl","-hwaccel","d3d11va","-hwaccel_output_format","d3d11"],"decoders":{},"encoders":{"libx264":"h264_amf","hevc":"hevc_amf"},"scalePrefix":"hwmap=derive_device=opencl,","scale":"scale_opencl","scaleExtra":"","pixelFormat":"nv12","wrapSwFilters":["hwdownload","hwupload_vaapi"],"hwFilters":["hwmap=derive_device=d3d11va:reverse=1","format=d3d11"],"extraOutputArgs":["-quality","speed","-rc","cbr","-qmin","0","-qmax","32","-b:v","{bitrate}","-maxrate","{bitrate}","-bufsize","{bufsize}"]}},"nvenc-win":{"audio":{"decoders":{},"encoders":{}},"video":{"inputArgs":["-init_hw_device","cuda=cu:0","-filter_hw_device","cu","-hwaccel","cuda","-hwaccel_output_format","cuda","-threads","1"],"decoders":{},"encoders":{"libx264":"h264_nvenc","hevc":"hevc_nvenc"},"preset":"p1","tune":"ull","scale":"scale_cuda","scaleExtra":"","pixelFormat":"yuv420p","wrapSwFilters":["hwdownload","hwupload_cuda"],"extraOutputArgs":["-b:v","{bitrate}","-maxrate","{bitrate}","-bufsize","{bufsize}"],"noH264Level":true}},"nvenc-linux":{"audio":{"decoders":{},"encoders":{}},"video":{"inputArgs":["-init_hw_device","cuda=cu:0","-filter_hw_device","cu","-hwaccel","cuda","-hwaccel_output_format","cuda"],"decoders":{"hevc":"hevc_cuvid","libx264":"h264_cuvid","av1":"av1_cuvid","mpeg1":"mpeg1_cuvid","mpeg2":"mpeg2_cuvid","mpeg4":"mpeg4_cuvid","vc1":"vc1_cuvid","vp8":"vp8_cuvid","vp9":"vp9_cuvid"},"encoders":{"libx264":"h264_nvenc","hevc":"hevc_nvenc"},"preset":"p1","tune":"ull","scale":"scale_cuda","scaleExtra":"","pixelFormat":"yuv420p","wrapSwFilters":["hwdownload","hwupload_cuda"],"extraOutputArgs":["-b:v","{bitrate}","-maxrate","{bitrate}","-bufsize","{bufsize}"]}},"vaapi-renderD128":{"audio":{"decoders":{},"encoders":{}},"video":{"inputArgs":["-init_hw_device","vaapi=va:/dev/dri/renderD128","-filter_hw_device","va","-hwaccel","vaapi","-hwaccel_output_format","vaapi"],"decoders":{},"encoders":{"libx264":"h264_vaapi","hevc":"hevc_vaapi","mpeg2":"mpeg2_vaapi","vp8":"vp8_vaapi","vp9":"vp9_vaapi"},"pixelFormat":"nv12","scale":"scale_vaapi","scaleExtra":":mode=fast","wrapSwFilters":["hwdownload","hwupload_vaapi"],"extraOutputArgs":["-rc_mode","VBR","-b:v","{bitrate}","-maxrate","{bitrate}","-bufsize","{bufsize}"],"noH264Level":true}},"v4l2m2m":{"audio":{"decoders":{},"encoders":{}},"video":{"inputArgs":["-hwaccel","drm"],"decoders":{"hevc":"hevc"},"encoders":{"libx264":"h264_v4l2m2m","hevc":"hevc_v4l2m2m","mpeg2":"mpeg2_v4l2m2m","mpeg4":"mpeg4_v4l2m2m","vp8":"vp8_v4l2m2m","vp9":"vp9_v4l2m2m"},"scale":"scale_v4l2m2m","scaleExtra":"","pixelFormat":"nv12","wrapSwFilters":["hwdownload","hwupload"],"extraOutputArgs":["-b:v","{bitrate}","-maxrate","{bitrate}","-bufsize","{bufsize}"]}}})PROFILE")
      .object();
}
namespace {
QString str(const QJsonValue &v) {
  if (v.isString())
    return v.toString();
  if (v.isDouble())
    return QString::number(v.toDouble(), 'g', 16);
  if (v.isBool())
    return v.toBool() ? "true" : "false";
  return v.isNull() ? "null" : "undefined";
}
bool truth(const QJsonValue &v) {
  return !(v.isNull() || v.isUndefined() || (v.isBool() && !v.toBool()) ||
           (v.isDouble() && v.toDouble() == 0) ||
           (v.isString() && v.toString().isEmpty()));
}
double number(const QJsonValue &v) {
  if (v.isDouble())
    return v.toDouble();
  if (v.isNull())
    return 0;
  if (v.isBool())
    return v.toBool() ? 1 : 0;
  bool ok = false;
  auto n = v.toString().toDouble(&ok);
  return ok ? n : NAN;
}
QStringList strings(QJsonValue v) {
  QStringList r;
  for (auto x : v.toArray())
    r << str(x);
  return r;
}
double sourceBitrate(const QJsonObject &s, bool video) {
  for (auto key : {"streamBitRate", "bps"}) {
    auto n = number(s[key]);
    if (!std::isnan(n) && n)
      return key == QString("bps") ? std::floor(n) : n;
  }
  double b = video ? 3145728 : 0;
  if (truth(s["formatDuration"]) && truth(s["numberOfBytes"])) {
    auto n = std::floor(8 * number(s["numberOfBytes"]) /
                        number(s["formatDuration"]));
    if (!std::isnan(n) && n)
      b = n;
  } else if (video && truth(s["formatBitRate"]))
    b = std::floor(.8 * number(s["formatBitRate"]));
  return std::floor(b);
}
} // namespace
double FfmpegArguments::videoBitrate(const QJsonObject &s, const QString &i,
                                     const QString &o, double max) {
  double b = sourceBitrate(s, true);
  auto scale = [](QString c) {
    return QStringList{"h265", "hevc", "vp9", "av1"}.contains(c) ? .6 : 1.;
  };
  if (b) {
    double f = scale(o) / scale(i);
    if (b <= 500000)
      f = std::max(f, 4.);
    else if (b <= 1000000)
      f = std::max(f, 3.);
    else if (b <= 2000000)
      f = std::max(f, 2.5);
    else if (b <= 3000000)
      f = std::max(f, 2.);
    b *= f;
    if (max)
      b = std::min(b, max);
  }
  return std::min(b, 1073741823.5);
}
double FfmpegArguments::audioBitrate(const QJsonObject &s, const QString &c,
                                     double ch) {
  double b = sourceBitrate(s, false);
  if (c == "aac" && !b)
    b = 64000 * ch;
  if (!b)
    return 0;
  if (c.isEmpty())
    return std::min(384000., b);
  if (QStringList{"aac", "mp3", "opus", "vorbis", "ac3", "eac3"}.contains(c))
    return std::min(ch >= 6 ? 640000. : 384000., b);
  if (c == "flac" || c == "alac")
    return std::min(ch >= 6 ? 3584000. : 1536000., b);
  return 128000;
}
QJsonObject FfmpegArguments::conversionOptions(const QJsonObject &i,
                                               const QJsonObject &sub) {
  auto s = i["stream"].toObject();
  auto track = s["track"].toString();
  QJsonObject o;
  for (auto k : {"ffmpeg", "mediaURL", "format"})
    if (i.contains(k))
      o[k] = i[k];
  o["track"] = track + str(s["id"]);
  if (track != "video" && track != "audio") {
    o[track] = s;
    return o;
  }
  for (auto k : {"profile", "maxWidth"})
    if (i.contains(k))
      o[k] = i[k];
  bool key = false;
  for (auto v : i["samples"].toArray())
    key |= truth(v.toObject()["key"]);
  auto codecs = i[track == "video" ? "videoCodecs" : "audioCodecs"];
  auto accepted = codecs.isArray()   ? strings(codecs)
                  : track == "video" ? QStringList{"h264"}
                                     : QStringList{"aac", "mp3"};
  bool trans =
      truth(i["forceTranscoding"]) ||
      (!key && (!i["sampleDuration"].isDouble() || track == "video")) ||
      !accepted.contains(s["codec"].toString()) ||
      (track == "video"
           ? !sub.isEmpty()
           : i["maxAudioChannels"].isDouble() &&
                 number(s["channels"]) > i["maxAudioChannels"].toDouble());
  if (trans) {
    double rate = 48000;
    if (track == "audio" && s["sampleRate"].isDouble()) {
      double n = s["sampleRate"].toDouble();
      rate = 7350;
      for (double r : {7350., 8000., 11025., 12000., 16000., 22050., 24000.,
                       32000., 44100., 48000., 64000., 88200., 96000.})
        if (std::abs(r - n) < std::abs(rate - n))
          rate = r;
    }
    QJsonObject over{
        {"frameRate", track == "video" ? QJsonValue(24) : QJsonValue()},
        {"embeddedSubtitlesId",
         track == "video" && sub["id"].isString() ? sub["id"] : QJsonValue()},
        {"externalSubtitlesUrl",
         track == "video" && sub["url"].isString() ? sub["url"] : QJsonValue()},
        {"sampleRate", track == "audio" ? QJsonValue(rate) : QJsonValue()}};
    if (s.contains("channels"))
      over["channels"] = s["channels"];
    if (track == "audio" && i["maxAudioChannels"].isDouble() &&
        number(s["channels"]) > i["maxAudioChannels"].toDouble())
      over["channels"] = i["maxAudioChannels"];
    s["transcode"] = true;
    s["override"] = over;
    o["segmentDuration"] = track == "video" ? 4 : 196608 / rate;
  } else
    o["segmentDuration"] = track == "video" ? 6 : 4.096;
  o[track] = s;
  return o;
}
QStringList FfmpegArguments::build(const QJsonObject &o,
                                   const QJsonObject &settings, int cpu) {
  const auto v = o["video"].toObject(), a = o["audio"].toObject(),
             sub = o["subtitle"].toObject(), vo = v["override"].toObject(),
             ao = a["override"].toObject();
  bool vt = truth(v["transcode"]), at = truth(a["transcode"]);
  double ch = ao["channels"].isDouble() ? ao["channels"].toDouble()
              : truth(a["channels"])    ? number(a["channels"])
                                        : 2;
  double vb = vt ? videoBitrate(v, v["codec"].toString(), "libx264",
                                number(settings["transcodeMaxBitRate"]))
                 : 0,
         ab = at ? audioBitrate(a, "aac", ch) : 0;
  double threads = cpu ? cpu * number(settings["transcodeHorsepower"]) : 0;
  if (std::isnan(threads))
    threads = 0;
  QJsonObject c{{"inputArgs", QJsonArray{}},
                {"maxWidth", truth(o["maxWidth"])
                                 ? o["maxWidth"]
                                 : settings["transcodeMaxWidth"]},
                {"encoderH264", "libx264"},
                {"levelH264", "51"},
                {"encoderAAC", "aac"},
                {"videoPreset", "ultrafast"},
                {"pixelFormat", "yuv420p"},
                {"videoTune", "zerolatency"},
                {"resizeHeightAuto", "-2"},
                {"scaleExtra", ":flags=lanczos"},
                {"scalePrefix", ""},
                {"wrapSwFilters", false},
                {"scale", false},
                {"noH264Level", false},
                {"extraVideoOutputArgs", QJsonArray{}},
                {"hwFilters", QJsonArray{}},
                {"encodeThreads", threads},
                {"colorSpace", "bt709"}};
  auto p = o["profile"];
  if (!truth(p) && truth(settings["transcodeHardwareAccel"]) &&
      truth(settings["transcodeProfile"]))
    p = settings["transcodeProfile"];
  auto profile = profiles()[str(p)].toObject();
  auto pv = profile["video"].toObject(), pa = profile["audio"].toObject();
  if (!v.isEmpty() && !pv.isEmpty()) {
    auto inputs = c["inputArgs"].toArray();
    for (auto x : pv["inputArgs"].toArray())
      inputs.append(x);
    auto enc = pv["encoders"].toObject()[str(c["encoderH264"])];
    if (truth(enc))
      c["encoderH264"] = enc;
    QMap<QString, QString> map{{"preset", "videoPreset"},
                               {"tune", "videoTune"},
                               {"extraOutputArgs", "extraVideoOutputArgs"}};
    for (auto k : {"preset", "pixelFormat", "tune", "scale", "resizeHeightAuto",
                   "wrapSwFilters", "extraSwFilters", "scalePrefix",
                   "scaleExtra", "extraOutputArgs", "hwFilters", "colorSpace",
                   "levelH264", "encodeThreads", "noH264Level"}) {
      QString key = k;
      if ((QStringList{"pixelFormat", "scaleExtra", "encodeThreads"}.contains(
               key) &&
           pv.contains(key)) ||
          truth(pv[key]))
        c[map.value(key, key)] = pv[key];
    }
    auto dec = pv["decoders"].toObject()[v["codec"].toString()];
    if (vt && truth(dec)) {
      inputs.append("-c:v");
      inputs.append(dec);
    }
    c["inputArgs"] = inputs;
  }
  if (!a.isEmpty() && !pa.isEmpty()) {
    auto inputs = c["inputArgs"].toArray();
    for (auto x : pa["inputArgs"].toArray())
      inputs.append(x);
    auto enc = pa["encoders"].toObject()[str(c["encoderAAC"])];
    if (truth(enc))
      c["encoderAAC"] = enc;
    auto dec = pa["decoders"].toObject()[a["codec"].toString()];
    if (at && truth(dec)) {
      inputs.append("-c:a");
      inputs.append(dec);
    }
    c["inputArgs"] = inputs;
  }
  QStringList r;
  if (vt)
    r += strings(c["inputArgs"]);
  r << "-fflags" << "+genpts" << "-noaccurate_seek" << "-seek_timestamp" << "1"
    << "-copyts";
  if (o["format"].toObject()["name"].toString().contains("matroska"))
    r << "-seek2any" << "1";
  if (o["time"].isDouble())
    r << "-ss" << str(o["time"]);
  r << "-i" << str(o["mediaURL"]) << "-threads" << str(c["encodeThreads"]);
  if (o["time"].isDouble() && (!a.isEmpty() || vt))
    r << "-ss" << str(o["time"]) << "-output_ts_offset" << str(o["time"]);
  r << "-max_muxing_queue_size" << "2048" << "-ignore_unknown"
    << "-map_metadata" << "-1" << "-map_chapters" << "-1" << "-map" << "-0:d?"
    << "-map" << "-0:t?";
  if (!v.isEmpty()) {
    r << "-map" << "v:" + str(v["id"]);
    if (vt) {
      QStringList filters;
      auto escape = [](QString s) { return s.replace(":", "\\:"); };
      if (vo["embeddedSubtitlesId"].isString())
        filters << "subtitles='" + escape(str(o["mediaURL"])) +
                       "':stream_index=" + str(vo["embeddedSubtitlesId"]);
      else if (vo["externalSubtitlesUrl"].isString())
        filters << "subtitles='" + escape(str(vo["externalSubtitlesUrl"])) +
                       "'";
      if (truth(o["blank"]))
        filters << "tpad=stop=-1";
      filters += strings(c["extraSwFilters"]);
      bool resize = number(v["width"]) > number(c["maxWidth"]);
      if (!truth(c["scale"])) {
        if (resize)
          filters << str(c["scalePrefix"]) + "scale=" + str(c["maxWidth"]) +
                         ":" + str(c["resizeHeightAuto"]) +
                         str(c["scaleExtra"]);
        filters << "format=" + str(c["pixelFormat"]);
      }
      if (!filters.isEmpty() && c["wrapSwFilters"].isArray()) {
        auto w = strings(c["wrapSwFilters"]);
        if (w.size() > 0 && !w[0].isEmpty())
          filters.prepend(w[0]);
        if (w.size() > 1 && !w[1].isEmpty())
          filters.append(w[1]);
      }
      filters << "setparams=color_primaries=" + str(c["colorSpace"]) +
                     ":color_trc=" + str(c["colorSpace"]) +
                     ":colorspace=" + str(c["colorSpace"]);
      if (truth(c["scale"])) {
        if (resize)
          filters << str(c["scalePrefix"]) + str(c["scale"]) + "=" +
                         str(c["maxWidth"]) + ":" + str(c["resizeHeightAuto"]) +
                         str(c["scaleExtra"]) +
                         ":format=" + str(c["pixelFormat"]);
        else if (truth(c["pixelFormat"]))
          filters << str(c["scale"]) + "=format=" + str(c["pixelFormat"]);
      }
      filters += strings(c["hwFilters"]);
      if (!filters.isEmpty())
        r << "-vf" << filters.join(',');
      r << "-c:v" << str(c["encoderH264"]) << "-preset:v"
        << str(c["videoPreset"]) << "-profile:v" << "high" << "-tune:v"
        << str(c["videoTune"]);
      if (!truth(c["noH264Level"]))
        r << "-level" << str(c["levelH264"]);
      if (vo["frameRate"].isDouble()) {
        r << "-vsync" << "cfr" << "-r:v" << str(vo["frameRate"]);
        if (o["segmentDuration"].isDouble()) {
          auto g = QString::number(vo["frameRate"].toDouble() *
                                       o["segmentDuration"].toDouble(),
                                   'g', 16);
          r << "-sc_threshold" << "0" << "-g" << g << "-keyint_min" << g;
        }
      }
      for (auto x : strings(c["extraVideoOutputArgs"]))
        r << (x == "{bitrate}"   ? QString::number(vb, 'g', 16)
              : x == "{bufsize}" ? QString::number(vb * 2, 'g', 16)
                                 : x);
    } else
      r << "-c:v" << "copy" << "-force_key_frames:v" << "source";
  } else
    r << "-map" << "-0:v?";
  if (!a.isEmpty()) {
    r << "-map" << "a:" + str(a["id"]);
    if (at) {
      r << "-c:a" << str(c["encoderAAC"]) << "-filter:a" << "apad" << "-async"
        << "1";
      if (ao["channels"].isDouble())
        r << "-ac:a" << str(ao["channels"]) << "-ab"
          << QString::number(ab, 'g', 16);
      if (ao["sampleRate"].isDouble())
        r << "-ar:a" << str(ao["sampleRate"]);
    } else {
      r << "-c:a" << "copy";
      if (a["codec"] == "aac")
        r << "-bsf:a" << "aac_adtstoasc";
    }
  } else
    r << "-map" << "-0:a?";
  r << "-map" << (sub.isEmpty() ? "-0:s?" : "s:" + str(sub["id"]));
  if (o["segmentDuration"].isDouble() && (!a.isEmpty() || vt))
    r << "-frag_duration"
      << QString::number(
             std::floor(1000 * o["segmentDuration"].toDouble() * 1000), 'f', 0);
  if (o["sequenceNumber"].isDouble() && (!v.isEmpty() || !a.isEmpty()))
    r << "-fragment_index" << str(o["sequenceNumber"]);
  if (!v.isEmpty())
    r << "-movflags"
      << "frag_keyframe+empty_moov+default_base_moof+delay_moov+dash";
  else if (!a.isEmpty())
    r << "-movflags" << "empty_moov+default_base_moof+delay_moov+dash";
  if (!v.isEmpty() || !a.isEmpty())
    r << "-use_editlist" << "1" << "-f" << "mp4";
  else if (!sub.isEmpty())
    r << "-f" << "webvtt";
  return r;
}
} // namespace server1::media
