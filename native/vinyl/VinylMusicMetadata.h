#pragma once
#include <QVariantMap>
#include <QString>
#include <atomic>

// Read-only media inspection, called on the import worker. No audio device,
// user scripts, network helper, identity registry or persistent metadata store.
namespace VinylMusicMetadata {
QVariantMap read(const QString &path, const std::atomic_bool *cancel = nullptr);
}
