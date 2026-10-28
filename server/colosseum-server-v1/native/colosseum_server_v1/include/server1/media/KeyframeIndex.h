#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QUrl>
#include <QVector>
#include <functional>

namespace server1::media {
// A valid container with no timing entries is available with empty samples.
// Failure is unavailable, so callers retain the source converter's best-effort fallback.
struct SamplesResult {
    QJsonObject tracks;
    QString error;
    bool available = false;
    quint64 bytesFetched = 0;
    QVector<quint64> sourceOffsets;
};
SamplesResult inspectMp4Moov(const QByteArray& moov, const QVector<int>& trackIndexes = {});
SamplesResult inspectMp4Samples(const QByteArray& data, const QVector<int>& trackIndexes = {});
SamplesResult inspectMkvSamples(const QByteArray& data, const QVector<int>& trackIndexes = {});
// Source module 918 grouping. Input matches the source factory argument object;
// output is {segments:[{key,segment}], predicted:bool}. Prediction is owned by M04.
QJsonObject bundledCodecSamples();
QJsonObject groupKeyframeSegments(const QJsonObject& options);
// File and HTTP(S) retrieval. Destroy the returned owner to cancel outstanding I/O
// and suppress completion. Offsets record stream opens, not bytes consumed.
QObject* retrieveMp4Samples(const QUrl& url, const QVector<int>& trackIndexes,
                            std::function<void(SamplesResult)> callback, QObject* parent = nullptr);
QObject* retrieveMkvSamples(const QUrl& url, const QVector<int>& trackIndexes,
                            std::function<void(SamplesResult)> callback, QObject* parent = nullptr);
}
