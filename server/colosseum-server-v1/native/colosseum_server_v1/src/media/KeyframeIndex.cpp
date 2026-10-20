#include "server1/media/KeyframeIndex.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <cmath>
#include <limits>

namespace server1::media {
namespace {
double number(const QJsonValue& value)
{
    if (value.isNull()) return 0;
    if (value.isDouble()) return value.toDouble();
    if (value.isBool()) return value.toBool() ? 1 : 0;
    if (value.isString()) {
        const auto text = value.toString().trimmed();
        if (text.isEmpty()) return 0;
        bool ok = false; const auto n = text.toDouble(&ok);
        if (ok) return n;
    }
    return std::numeric_limits<double>::quiet_NaN();
}
}

QJsonObject groupKeyframeSegments(const QJsonObject& options)
{
    const auto stream = options["stream"].toObject();
    const double coefficient = number(options["timescale"]) / number(stream["timescale"]);
    const double target = std::ceil(number(options["segmentDuration"]) * number(options["timescale"]));
    const double sampleDuration = number(options["sampleDuration"]) * coefficient;
    const bool video = stream["track"] == "video";
    const bool audio = stream["track"] == "audio";
    QJsonArray keys;
    for (const auto& value : options["samples"].toArray())
        if (value.toObject()["key"].toBool()) keys.append(value);
    if (keys.isEmpty()) {
        const double perSegment = std::ceil(target / sampleDuration);
        return {{"segments", QJsonArray{}}, {"predicted", true},
                {"segmentDuration", perSegment * number(options["sampleDuration"]) / number(stream["timescale"])}};
    }
    QVector<QJsonObject> roots;
    double previousDuration = 0;
    for (qsizetype i = 0; i < keys.size(); ++i) {
        const double pts = keys[i].toObject()["pts"].toDouble();
        const double duration = i + 1 < keys.size()
            ? (keys[i + 1].toObject()["pts"].toDouble() - pts) * coefficient : sampleDuration;
        QJsonObject segment{{"sequenceNumber", double(i + 1)}, {"seekTime", pts * coefficient},
                            {"duration", duration}, {"buffer", QJsonValue()},
                            {"cues", QJsonArray{}}, {"extraSegments", QJsonArray{}}};
        const bool extra = video && (duration < 20 * sampleDuration || (i > 0 && previousDuration < 20 * sampleDuration));
        previousDuration = duration;
        if (!roots.isEmpty()) {
            auto& root = roots.last();
            auto extras = root["extraSegments"].toArray();
            double total = root["duration"].toDouble();
            for (auto v : extras) total += v.toObject()["duration"].toDouble();
            if (total < target || extra) {
                if (audio) segment["sequenceNumber"] = QJsonValue();
                extras.append(segment);
                root["extraSegments"] = extras;
                continue;
            }
        }
        if (audio) segment["sequenceNumber"] = roots.size() + 1;
        roots.append(segment);
    }
    QJsonArray segments;
    for (qsizetype i = 0; i < roots.size(); ++i)
        segments.append(QJsonObject{{"key", double(i + 1)}, {"segment", roots[i]}});
    return {{"segments", segments}, {"predicted", false}};
}
// Exact module 934 payloads; no transcoding or replacement fixtures.
QJsonObject bundledCodecSamples()
{
    static const auto samples = QJsonDocument::fromJson(QByteArrayLiteral(R"samples({"aac-6chan":{"container":"wav","mime":"audio/x-wav","data":"UklGRuAAAABXQVZFZm10IC4AAAD+/wYARKwAAIGmAAAAEhAAGwAQAD8AAAD/AAAAAAAQAIAAAKoAOJtxEjBW5QAAZmFjdAQAAAAAFAAATElTVBoAAABJTkZPSVNGVA4AAABMYXZmNTkuMTYuMTAwAGRhdGFwAAAA3gIATGF2YzU5LjE4LjEwMAACMEACEQBGCMBGIAjBGBhGAAHAARggAQiAIwRgIxAEYIwMIwAA4AEYIAEIgCMEYCMQBGCMDCMAAOABGCABCIAjBGAjEARgjAwjAADgARggAQiAIwRgIxAEYIwMIwAA4A=="},"ac3-2chan":{"container":"wav","mime":"audio/x-wav","data":"UklGRqYQAABXQVZFZm10IBIAAAAAIAIARKwAAMBdAAAADxAAAABmYWN0BAAAAAAeAABMSVNUGgAAAElORk9JU0ZUDgAAAExhdmY1OS4xNi4xMDAAZGF0YVIQAAALd6F6VEBD4Qb1dfCAghAQQVx8+fPnz58+fPnz58+fPnz58+fPnz58+fPv+dXz58+fPnz58+fPnz58+fPnz58+fPnz58+fPnz58+fPnz58+fPnz58+fPnz4/51fPnz58+fPnz58+fPnz58+fPnz58+fPnz58+fPnz58+fPnz58+fPnz58+fPjL/EECBIAAAAAA8bbbbbbbbx48eO7u7u7gAAAAAAAAAO7u7u7u7vHjxttt8+a1rWta1rWta1rWta1rWta0AAAABtttttttvHjx47u7u7uAAAAAAAAAA7u7u7u7u8ePG223z5rWta1rWta1rWta1rWta1rTAAAAAAAB422222223jx48d3d3d3AAAAAAAAAAd3d3d3d3ePHjbbb581rWta1rWta1rWta1rWta1oAAAADbbbbbbbePHjx3d3d3cAAAAAAAAAB3d3d3d3d48eNttvnzWta1rWta1rWta1rWta1rWmAAAAAAADxtttttttvHjx47u7u7uAAAAAAAAAA7u7u7u7u8ePG223z5rWta1rWta1rWta1rWta1rQAAAAG22222228ePHju7u7u4AAAAAAAAADu7u7u7u7x48bbbfPmta1rWta1rWta1rWta1rWtMAAAAAAAHjbbbbbbbePHjx3d3d3cAAAAAAAAAB3d3d3d3d48eNttvnzWta1rWta1rWta1rWta1rWgAAAANttttttt48ePHd3d3dwAAAAAAAAAHd3d3d3d3jx4222+fNa1rWta1rWta1rWta1rWtaYAAAAAAAPG22222228ePHju7u7u4AAAAAAAAADu7u7u7u7x48bbbfPmta1rWta1rWta1rWta1rWtAAAAAbbbbbbbbx48eO7u7u7gAAAAAAAAAO7u7u7u7vHjxttt8+a1rWta1rWta1rWta1rWta0wAAAAAAAeNttttttt48ePHd3d3dwAAAAAAAAAHd3d3d3d3jx4222+fNa1rWta1rWta1rWta1rWtaAAAAA22222223jx48d3d3d3AAAAAAAAAAd3d3d3d3ePHjbbb581rWta1rWta1rWta1rWta1oAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA3sgLdydwVUBD4Qb1dfCAghAQQVx8+fPnz58+fPnz58+fPnz58+fPnz58+fPv+dXz58+fPnz58+fPnz58+fPnz58+fPnz58+fPnz58+fPnz58+fPnz58+fPnz4/51fPnz58+fPnz58+fPnz58+fPnz58+fPnz58+fPnz58+fPnz58+fPnz58+fPjL/EkSJIAAAAAA8bbbbbbePHjx3d3d3AAAAAAAAAAAAAAAAADu7u7x48bbbbbbfPmta1rWta1rWta1rWta1rQAAAAHjbbbbbbx48eO7u7u4AAAAAAAAAAAAAAAAAd3d3ePHjbbbbbb581rWta1rWta1rWta1rWtaYAAAAAAAPG222223jx48d3d3dwAAAAAAAAAAAAAAAAA7u7u8ePG222223z5rWta1rWta1rWta1rWta0AAAAB42222228ePHju7u7uAAAAAAAAAAAAAAAAAHd3d3jx4222222+fNa1rWta1rWta1rWta1rWmAAAAAAADxtttttt48ePHd3d3cAAAAAAAAAAAAAAAAAO7u7vHjxtttttt8+a1rWta1rWta1rWta1rWtAAAAAeNtttttvHjx47u7u7gAAAAAAAAAAAAAAAAB3d3d48eNtttttvnzWta1rWta1rWta1rWta1pgAAAAAAA8bbbbbbePHjx3d3d3AAAAAAAAAAAAAAAAADu7u7x48bbbbbbfPmta1rWta1rWta1rWta1rQAAAAHjbbbbbbx48eO7u7u4AAAAAAAAAAAAAAAAAd3d3ePHjbbbbbb581rWta1rWta1rWta1rWtaYAAAAAAAPG222223jx48d3d3dwAAAAAAAAAAAAAAAAA7u7u8ePG222223z5rWta1rWta1rWta1rWta0AAAAB42222228ePHju7u7uAAAAAAAAAAAAAAAAAHd3d3jx4222222+fNa1rWta1rWta1rWta1rWmAAAAAAADxtttttt48ePHd3d3cAAAAAAAAAAAAAAAAAO7u7vHjxtttttt8+a1rWta1rWta1rWta1rWtAAAAAeNtttttvHjx47u7u7gAAAAAAAAAAAAAAAAB3d3d48eNtttttvnzWta1rWta1rWta1rWta1oAyugt3J3BVQEPhBvV18ICCEBBBXHz58+fPnz58+fPnz58+fPnz58+fPnz58+/51fPnz58+fPnz58+fPnz58+fPnz58+fPnz58+fPnz58+fPnz58+fPnz58+fPj/nV8+fPnz58+fPnz58+fPnz58+fPnz58+fPnz58+fPnz58+fPnz58+fPnz58+Mv8SRIkgAAAAADxtttttt48ePHd3d3cAAAAAAAAAAAAAAAAAO7u7vHjxtttttt8+a1rWta1rWta1rWta1rWtAAAAAeNtttttvHjx47u7u7gAAAAAAAAAAAAAAAAB3d3d48eNtttttvnzWta1rWta1rWta1rWta1pgAAAAAAA8bbbbbbePHjx3d3d3AAAAAAAAAAAAAAAAADu7u7x48bbbbbbfPmta1rWta1rWta1rWta1rQAAAAHjbbbbbbx48eO7u7u4AAAAAAAAAAAAAAAAAd3d3ePHjbbbbbb581rWta1rWta1rWta1rWtaYAAAAAAAPG222223jx48d3d3dwAAAAAAAAAAAAAAAAA7u7u8ePG222223z5rWta1rWta1rWta1rWta0AAAAB42222228ePHju7u7uAAAAAAAAAAAAAAAAAHd3d3jx4222222+fNa1rWta1rWta1rWta1rWmAAAAAAADxtttttt48ePHd3d3cAAAAAAAAAAAAAAAAAO7u7vHjxtttttt8+a1rWta1rWta1rWta1rWtAAAAAeNtttttvHjx47u7u7gAAAAAAAAAAAAAAAAB3d3d48eNtttttvnzWta1rWta1rWta1rWta1pgAAAAAAA8bbbbbbePHjx3d3d3AAAAAAAAAAAAAAAAADu7u7x48bbbbbbfPmta1rWta1rWta1rWta1rQAAAAHjbbbbbbx48eO7u7u4AAAAAAAAAAAAAAAAAd3d3ePHjbbbbbb581rWta1rWta1rWta1rWtaYAAAAAAAPG222223jx48d3d3dwAAAAAAAAAAAAAAAAA7u7u8ePG222223z5rWta1rWta1rWta1rWta0AAAAB42222228ePHju7u7uAAAAAAAAAAAAAAAAAHd3d3jx4222222+fNa1rWta1rWta1rWta1rWgDK6C3cncFVAQ+EG9XXwgIIQEEFcfPnz58+fPnz58+fPnz58+fPnz58+fPnz7/nV8+fPnz58+fPnz58+fPnz58+fPnz58+fPnz58+fPnz58+fPnz58+fPnz58+P+dXz58+fPnz58+fPnz58+fPnz58+fPnz58+fPnz58+fPnz58+fPnz58+fPnz4y/xJEiSAAAAAAPG222223jx48d3d3dwAAAAAAAAAAAAAAAAA7u7u8ePG222223z5rWta1rWta1rWta1rWta0AAAAB42222228ePHju7u7uAAAAAAAAAAAAAAAAAHd3d3jx4222222+fNa1rWta1rWta1rWta1rWmAAAAAAADxtttttt48ePHd3d3cAAAAAAAAAAAAAAAAAO7u7vHjxtttttt8+a1rWta1rWta1rWta1rWtAAAAAeNtttttvHjx47u7u7gAAAAAAAAAAAAAAAAB3d3d48eNtttttvnzWta1rWta1rWta1rWta1pgAAAAAAA8bbbbbbePHjx3d3d3AAAAAAAAAAAAAAAAADu7u7x48bbbbbbfPmta1rWta1rWta1rWta1rQAAAAHjbbbbbbx48eO7u7u4AAAAAAAAAAAAAAAAAd3d3ePHjbbbbbb581rWta1rWta1rWta1rWtaYAAAAAAAPG222223jx48d3d3dwAAAAAAAAAAAAAAAAA7u7u8ePG222223z5rWta1rWta1rWta1rWta0AAAAB42222228ePHju7u7uAAAAAAAAAAAAAAAAAHd3d3jx4222222+fNa1rWta1rWta1rWta1rWmAAAAAAADxtttttt48ePHd3d3cAAAAAAAAAAAAAAAAAO7u7vHjxtttttt8+a1rWta1rWta1rWta1rWtAAAAAeNtttttvHjx47u7u7gAAAAAAAAAAAAAAAAB3d3d48eNtttttvnzWta1rWta1rWta1rWta1pgAAAAAAA8bbbbbbePHjx3d3d3AAAAAAAAAAAAAAAAADu7u7x48bbbbbbfPmta1rWta1rWta1rWta1rQAAAAHjbbbbbbx48eO7u7u4AAAAAAAAAAAAAAAAAd3d3ePHjbbbbbb581rWta1rWta1rWta1rWtaAMroLdydwVUBD4Qb1dfCAghAQQVx8+fPnz58+fPnz58+fPnz58+fPnz58+fPv+dXz58+fPnz58+fPnz58+fPnz58+fPnz58+fPnz58+fPnz58+fPnz58+fPnz4/51fPnz58+fPnz58+fPnz58+fPnz58+fPnz58+fPnz58+fPnz58+fPnz58+fPjL/EkSJIAAAAAA8bbbbbbePHjx3d3d3AAAAAAAAAAAAAAAAADu7u7x48bbbbbbfPmta1rWta1rWta1rWta1rQAAAAHjbbbbbbx48eO7u7u4AAAAAAAAAAAAAAAAAd3d3ePHjbbbbbb581rWta1rWta1rWta1rWtaYAAAAAAAPG222223jx48d3d3dwAAAAAAAAAAAAAAAAA7u7u8ePG222223z5rWta1rWta1rWta1rWta0AAAAB42222228ePHju7u7uAAAAAAAAAAAAAAAAAHd3d3jx4222222+fNa1rWta1rWta1rWta1rWmAAAAAAADxtttttt48ePHd3d3cAAAAAAAAAAAAAAAAAO7u7vHjxtttttt8+a1rWta1rWta1rWta1rWtAAAAAeNtttttvHjx47u7u7gAAAAAAAAAAAAAAAAB3d3d48eNtttttvnzWta1rWta1rWta1rWta1pgAAAAAAA8bbbbbbePHjx3d3d3AAAAAAAAAAAAAAAAADu7u7x48bbbbbbfPmta1rWta1rWta1rWta1rQAAAAHjbbbbbbx48eO7u7u4AAAAAAAAAAAAAAAAAd3d3ePHjbbbbbb581rWta1rWta1rWta1rWtaYAAAAAAAPG222223jx48d3d3dwAAAAAAAAAAAAAAAAA7u7u8ePG222223z5rWta1rWta1rWta1rWta0AAAAB42222228ePHju7u7uAAAAAAAAAAAAAAAAAHd3d3jx4222222+fNa1rWta1rWta1rWta1rWmAAAAAAADxtttttt48ePHd3d3cAAAAAAAAAAAAAAAAAO7u7vHjxtttttt8+a1rWta1rWta1rWta1rWtAAAAAeNtttttvHjx47u7u7gAAAAAAAAAAAAAAAAB3d3d48eNtttttvnzWta1rWta1rWta1rWta1oAyug=="},"hevc":{"container":"mkv","mime":"video/x-matroska","data":"GkXfo6NChoEBQveBAULygQRC84EIQoKIbWF0cm9za2FCh4EEQoWBAhhTgGcBAAAAAAAPPxFNm3TAv4QoiMc2TbuLU6uEFUmpZlOsgaFNu4tTq4QWVK5rU6yB8U27jFOrhBJUw2dTrIIK1027jFOrhBxTu2tTrIIPI+wBAAAAAAAAUwAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAFUmpZsu/hKWsdsYq17GDD0JATYCNTGF2ZjU5LjE2LjEwMFdBjUxhdmY1OS4xNi4xMDBzpJCbCXiosnZ5+kwi7p4jwfjnRImIQGkAAAAAAAAWVK5rSeC/hOMyVqauAQAAAAAACdHXgQFzxYhOuit5hFVH5pyBACK1nIN1bmSIgQCGkFZfTVBFR0gvSVNPL0hFVkODgQEj44OEAmJaAOABAAAAAAAAC7CCB4C6ggPAmoECY6JJgQEBYAAAAJAAAAAAAHjwAPz9+PgAAA8EIAABABhAAQwB//8BYAAAAwCQAAADAAADAHiVmAkhAAEAK0IBAQFgAAADAJAAAAMAAAMAeKADwIA8FllZpJMrwFoCAAADAAIAAAMAMhAiAAEAB0QBwXK0YkAnAAEJDE4BBf///////////wcsot4JtRdH27tVpP5/wvxOeDI2NSAoYnVpbGQgMjAzKSAtIDMuNSsyMC0xNzgzOWNjMGQ6W01hYyBPUyBYXVtjbGFuZyAxMS4wLjBdWzY0IGJpdF0gOGJpdCsxMGJpdCsxMmJpdCAtIEguMjY1L0hFVkMgY29kZWMgLSBDb3B5cmlnaHQgMjAxMy0yMDE4IChjKSBNdWx0aWNvcmV3YXJlLCBJbmMgLSBodHRwOi8veDI2NS5vcmcgLSBvcHRpb25zOiBjcHVpZD0xMTExMDM5IGZyYW1lLXRocmVhZHM9MyB3cHAgbm8tcG1vZGUgbm8tcG1lIG5vLXBzbnIgbm8tc3NpbSBsb2ctbGV2ZWw9MiBiaXRkZXB0aD04IGlucHV0LWNzcD0xIGZwcz0yNS8xIGlucHV0LXJlcz0xOTIweDk2MCBpbnRlcmxhY2U9MCB0b3RhbC1mcmFtZXM9MCBsZXZlbC1pZGM9MCBoaWdoLXRpZXI9MSB1aGQtYmQ9MCByZWY9MyBuby1hbGxvdy1ub24tY29uZm9ybWFuY2Ugbm8tcmVwZWF0LWhlYWRlcnMgYW5uZXhiIG5vLWF1ZCBuby1lb2Igbm8tZW9zIG5vLWhyZCBpbmZvIGhhc2g9MCBuby10ZW1wb3JhbC1sYXllcnMgb3Blbi1nb3AgbWluLWtleWludD0yNSBrZXlpbnQ9MjUwIGdvcC1sb29rYWhlYWQ9MCBiZnJhbWVzPTQgYi1hZGFwdD0yIGItcHlyYW1pZCBiZnJhbWUtYmlhcz0wIHJjLWxvb2thaGVhZD0yMCBsb29rYWhlYWQtc2xpY2VzPTYgc2NlbmVjdXQ9NDAgbm8taGlzdC1zY2VuZWN1dCByYWRsPTAgbm8tc3BsaWNlIG5vLWludHJhLXJlZnJlc2ggY3R1PTY0IG1pbi1jdS1zaXplPTggbm8tcmVjdCBuby1hbXAgbWF4LXR1LXNpemU9MzIgdHUtaW50ZXItZGVwdGg9MSB0dS1pbnRyYS1kZXB0aD0xIGxpbWl0LXR1PTAgcmRvcS1sZXZlbD0wIGR5bmFtaWMtcmQ9MC4wMCBuby1zc2ltLXJkIHNpZ25oaWRlIG5vLXRza2lwIG5yLWludHJhPTAgbnItaW50ZXI9MCBuby1jb25zdHJhaW5lZC1pbnRyYSBzdHJvbmctaW50cmEtc21vb3RoaW5nIG1heC1tZXJnZT0zIGxpbWl0LXJlZnM9MSBuby1saW1pdC1tb2RlcyBtZT0xIHN1Ym1lPTIgbWVyYW5nZT01NyB0ZW1wb3JhbC1tdnAgbm8tZnJhbWUtZHVwIG5vLWhtZSB3ZWlnaHRwIG5vLXdlaWdodGIgbm8tYW5hbHl6ZS1zcmMtcGljcyBkZWJsb2NrPTA6MCBzYW8gbm8tc2FvLW5vbi1kZWJsb2NrIHJkPTMgc2VsZWN0aXZlLXNhbz00IGVhcmx5LXNraXAgcnNraXAgbm8tZmFzdC1pbnRyYSBuby10c2tpcC1mYXN0IG5vLWN1LWxvc3NsZXNzIGItaW50cmEgbm8tc3BsaXRyZC1za2lwIHJkcGVuYWx0eT0wIHBzeS1yZD0yLjAwIHBzeS1yZG9xPTAuMDAgbm8tcmQtcmVmaW5lIG5vLWxvc3NsZXNzIGNicXBvZmZzPTAgY3JxcG9mZnM9MCByYz1jcmYgY3JmPTI4LjAgcWNvbXA9MC42MCBxcHN0ZXA9NCBzdGF0cy13cml0ZT0wIHN0YXRzLXJlYWQ9MCBpcHJhdGlvPTEuNDAgcGJyYXRpbz0xLjMwIGFxLW1vZGU9MiBhcS1zdHJlbmd0aD0xLjAwIGN1dHJlZSB6b25lLWNvdW50PTAgbm8tc3RyaWN0LWNiciBxZy1zaXplPTMyIG5vLXJjLWdyYWluIHFwbWF4PTY5IHFwbWluPTAgbm8tY29uc3QtdmJ2IHNhcj0xIG92ZXJzY2FuPTAgdmlkZW9mb3JtYXQ9NSByYW5nZT0wIGNvbG9ycHJpbT0yIHRyYW5zZmVyPTIgY29sb3JtYXRyaXg9MiBjaHJvbWFsb2M9MCBkaXNwbGF5LXdpbmRvdz0wIGNsbD0wLDAgbWluLWx1bWE9MCBtYXgtbHVtYT0yNTUgbG9nMi1tYXgtcG9jLWxzYj04IHZ1aS10aW1pbmctaW5mbyB2dWktaHJkLWluZm8gc2xpY2VzPTEgbm8tb3B0LXFwLXBwcyBuby1vcHQtcmVmLWxpc3QtbGVuZ3RoLXBwcyBuby1tdWx0aS1wYXNzLW9wdC1ycHMgc2NlbmVjdXQtYmlhcz0wLjA1IGhpc3QtdGhyZXNob2xkPTAuMDMgbm8tb3B0LWN1LWRlbHRhLXFwIG5vLWFxLW1vdGlvbiBuby1oZHIxMCBuby1oZHIxMC1vcHQgbm8tZGhkcjEwLW9wdCBuby1pZHItcmVjb3Zlcnktc2VpIGFuYWx5c2lzLXJldXNlLWxldmVsPTAgYW5hbHlzaXMtc2F2ZS1yZXVzZS1sZXZlbD0wIGFuYWx5c2lzLWxvYWQtcmV1c2UtbGV2ZWw9MCBzY2FsZS1mYWN0b3I9MCByZWZpbmUtaW50cmE9MCByZWZpbmUtaW50ZXI9MCByZWZpbmUtbXY9MSByZWZpbmUtY3R1LWRpc3RvcnRpb249MCBuby1saW1pdC1zYW8gY3R1LWluZm89MCBuby1sb3dwYXNzLWRjdCByZWZpbmUtYW5hbHlzaXMtdHlwZT0wIGNvcHktcGljPTEgbWF4LWF1c2l6ZS1mYWN0b3I9MS4wIG5vLWR5bmFtaWMtcmVmaW5lIG5vLXNpbmdsZS1zZWkgbm8taGV2Yy1hcSBuby1zdnQgbm8tZmllbGQgcXAtYWRhcHRhdGlvbi1yYW5nZT0xLjAwIHNjZW5lY3V0LWF3YXJlLXFwPTBjb25mb3JtYW5jZS13aW5kb3ctb2Zmc2V0cyByaWdodD0wIGJvdHRvbT0wIGRlY29kZXItbWF4LXJhdGU9MCBuby12YnYtbGl2ZS1tdWx0aS1wYXNzgBJUw2dAoL+E/o9i5XNzAQAAAAAAACdjwIBnyAEAAAAAAAAaRaOHRU5DT0RFUkSHjUxhdmY1OS4xNi4xMDBzcwEAAAAAAABfY8CLY8WITroreYRVR+ZnyAEAAAAAAAAiRaOHRU5DT0RFUkSHlUxhdmM1OS4xOC4xMDAgbGlieDI2NWfIokWjiERVUkFUSU9ORIeUMDA6MDA6MDAuMjAwMDAwMDAwAAAfQ7Z1Q6C/hO1dHxTngQCjQeuBAACAAAAB4ygBrx0ebCF995111xxtttuA98+hP/7yvn/Lfh/bKVrr6zWG148ECGSYwAAAAwAAAwAAAwAAAwAAAwAAAwAAAwABuagXlgNMThqAAAADAAADAAADAAADAAADAAADAAADAAADAZeTC5NAAAADAAADAAADAAADAAADAAADAAADAAADAAAOaIcAAAMAAAMAAAMAAAMAAAMAAAMAAAMAAAMAAAMAAG3AAAADAAADAAADAAADAAADAAADAAADAAADAAADAAA7YAAAAwAAAwAAAwAAAwAAAwAAAwAAAwAAAwAAAwAEXAAAAwAAAwAAAwAAAwAAAwAAAwAAAwAAAwAAAwAi4AAAAwAAAwAAAwAAAwAAAwAAAwAAAwAAAwAAAwCegAAAAwAAAwAAAwAAAwAAAwAAAwAAAwAAAwAAAwGRAAADAAADAAADAAADAAADAAADAAADAAADAAADAtoAAAMAAAMAAAMAAAMAAAMAAAMAAAMAAAMAAAVMAAADAAADAAADAAADAAADAAADAAADAAADAAAIWAAAAwAAAwAAAwAAAwAAAwAAAwAAAwAAAwAADvgAAAMAAAMAAAMAAAMAAAMAAAMAAAMAAAMAABawAAADAAADAAADAAADAAADAAADAAADAAADAAAgIKPxgQCgAAAAAGkCAdAhSeEMY6PI0szKqqiGZ/mIlQAA6YDzkhioAAADAC3g00qAAABbwMMAAAMAEJAAAAMAANiAAAADAAW0AAADABPQAAADAEZAAAADAMeAAAADAY0AAAMD4gAABuwAAA0IAAAVkAAAIqCj6IEAUAAAAABgAgHgRJV4YSDyJDKqqiGZmcCLgAAr4JMr8QAAAwAXsGEAAAMAHxBlAAADAoYAAAMAEtAAAAMAYcAAAAMA8YAAAAMB4QAAAwO2AAAHFAAAChgAAA8IAAAVUAAAHRAAACVgo+SBACgAAAAAXAAB4CT9fosKR5EdURVRDMzOiYAAS0CUN8OAAAAOuGWAAAAaEGmAAAJ6XYAAD9gAAAMAQcAAAAMAvIAAAAMBowAAAwNGAAAF7AAACCgAAAv4AAAR8AAAGLAAAB9Qo+SBAHgAAAAAXAAB4Ga1/UYUDyI6oiqiGZmciYAAS0CUN8OAAAAOuGWAAAAaEGmAAAJ6XYAAD9gAAAMAQcAAAAMAvIAAAAMBowAAAwNGAAAF7AAACCgAAAv4AAAR8AAAGLAAAB9QHFO7a5e/hBPLdeu7j7OBALeK94EB8YILffCBCQ=="}})samples")).object();
    return samples;
}
}
