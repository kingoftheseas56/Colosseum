#include "AccountRuntime.h"

#include "../engine/LocalDownloads.h"

void AccountRuntime::setDownloadSource(LocalDownloads *downloads) {
    if (m_downloadSource == downloads)
        return;

    m_downloadSource = downloads;
    if (!m_downloadSource)
        return;

    m_downloadSource->setDownloadIntentStore(&m_downloadIntentStore);
    m_downloadIntentStore.setLocalRecordProvider(
        [downloads]() {
            return downloads ? downloads->portableDownloadIntents()
                              : QVariantList();
        });
    connect(
        m_downloadSource,
        &LocalDownloads::changed,
        this,
        [this]() {
            QString ignored;
            m_downloadIntentStore.refreshFromLocal(&ignored);
        });
    connect(
        &m_downloadIntentStore,
        &DownloadIntentStore::changed,
        m_downloadSource,
        &LocalDownloads::changed);
}
