#pragma once
// dHash + hamming for lanista's golden-image comparisons.
#include <QImage>
#include <QtGlobal>
namespace lanista {
quint64 dhash(const QImage& img);
int hamming(quint64 a, quint64 b);
}
