#pragma once
#include "server1/media/KeyframeIndex.h"
namespace server1::media {
SamplesResult inspectLegacyMp4Moov(const QByteArray&,const QVector<int>& indexes={});
SamplesResult inspectLegacyMp4Samples(const QByteArray&,const QVector<int>& indexes={});
SamplesResult inspectLegacyMkvSamples(const QByteArray&,const QVector<int>& indexes={});
QObject* retrieveLegacyMp4Samples(const QUrl&,const QVector<int>&,std::function<void(SamplesResult)>,QObject* parent=nullptr);
QObject* retrieveLegacyMkvSamples(const QUrl&,const QVector<int>&,std::function<void(SamplesResult)>,QObject* parent=nullptr);
}
