#pragma once
#include <QJsonObject>
#include <QString>
namespace server1::local {
// Native behavior port of pinned M304. Empty input is handled safely rather than
// reproducing the JS TypeError. Options: strict, fromInside, fileLength, hints.
QJsonObject parseLocalFilename(QString path, QJsonObject options = {});
}
