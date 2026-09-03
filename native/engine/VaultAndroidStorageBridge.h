#pragma once

#include "VaultAndroidIndexAdapter.h"

#include <QByteArray>
#include <QList>
#include <QString>

// JSON boundary between Android platform/provider discovery and the shared
// Vault index adapter. The Android platform layer owns MediaStore/SAF queries
// and persisted permissions; Vault owns validation and index reconciliation.
class VaultAndroidStorageBridge
{
public:
    struct DecodeResult {
        bool ok = false;
        QString error;
        QList<VaultAndroidIndexAdapter::SourceSnapshot> sources;
    };

    static DecodeResult decodeSnapshotJson(const QByteArray& payload);
};
