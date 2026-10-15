#pragma once

#include "VaultIndex.h"

#include <QList>
#include <QString>

class VaultIdentity;

// Vault-side boundary for Android MediaStore/SAF discovery. The Android platform
// layer owns permissions and provider queries; this class consumes their snapshot
// and publishes it into the existing VaultIndex without filesystem recursion.
class VaultAndroidIndexAdapter
{
public:
    struct MediaEntry {
        QString uri;          // provider-openable content:// URI
        QString displayName;  // provider DISPLAY_NAME
        QString relativePath; // parent path relative to this source root
        QString mimeType;
        qint64 sizeBytes = 0;
        qint64 modifiedMs = 0;
        qint64 durationMs = -1; // MediaStore duration; -1 when not supplied
    };

    struct SourceSnapshot {
        QString rootUri;
        bool available = true; // false means permission/provider unavailable, not empty
        QList<MediaEntry> entries;
    };

    struct ApplyResult {
        bool ok = false;
        bool sourceAway = false;
        int indexedCount = 0;
        int removedCount = 0;
    };

    VaultAndroidIndexAdapter(VaultIndex* index, VaultIdentity* identity);

    // Reconciles one Android source atomically. An unavailable source keeps the
    // last index rows and marks them away; an available empty source removes them.
    ApplyResult applySnapshot(const SourceSnapshot& snapshot);

    // Pure mapping seam for provider fixtures and future JNI/provider adapters.
    VaultIndex::FileRow rowForEntry(const QString& rootUri,
                                    const MediaEntry& entry) const;

private:
    static QString cleanRelativePath(const QString& path);
    static QString mediaKind(const MediaEntry& entry);

    VaultIndex* m_index = nullptr;
    VaultIdentity* m_identity = nullptr;
};
