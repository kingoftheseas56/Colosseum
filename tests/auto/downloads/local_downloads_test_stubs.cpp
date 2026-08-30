#include "engine/BookDownloader.h"
#include "engine/ComicDownloader.h"
#include "engine/MangaDownloader.h"
#include "engine/MangaTankobanService.h"
#include "player/downloadstore.h"

QVariantList MangaDownloader::downloadedChapters() const { return {}; }
QVariantList MangaDownloader::activeChapterJobs() const { return {}; }
void MangaDownloader::downloadChapter(const QString&, const QString&, const QString&, const QString&) {}
QVariantList MangaDownloader::localPages(const QString&) const { return {}; }
QVariantMap MangaDownloader::statusOf(const QString&) const { return {}; }
void MangaDownloader::cancelDownload(const QString&) {}
bool MangaDownloader::isDownloaded(const QString&) const { return false; }
QVariantMap MangaDownloader::deleteChapter(const QString&) { return {}; }
void MangaDownloader::fetchThumb(const QString&, const QString&) {}
void MangaDownloader::indexSelfTest() {}

void BookDownloader::downloadBook(const QString&, const QString&, const QString&, double, const QString&) {}
QString BookDownloader::localBook(const QString&) const { return {}; }
bool BookDownloader::isDownloaded(const QString&) const { return false; }
QVariantMap BookDownloader::statusOf(const QString&) const { return {}; }
QVariantList BookDownloader::downloadedBooks() const { return {}; }
QVariantList BookDownloader::activeBookJobs() const { return {}; }
void BookDownloader::cancelDownload(const QString&) {}
QVariantMap BookDownloader::deleteBook(const QString&) { return {}; }

void ComicDownloader::downloadIssue(const QString&, const QString&, const QString&,
                                    const QString&, const QString&, double) {}
void ComicDownloader::ingestLocalArchive(const QString&, const QString&, const QString&,
                                          const QString&, const QString&) {}
void ComicDownloader::downloadIssueTorrent(const QString&, const QString&, const QString&,
                                            const QString&, const QString&) {}
void ComicDownloader::searchTorrentSources(const QString&, const QString&, const QString&,
                                           const QString&, const QString&, const QString&) {}
void ComicDownloader::searchTorrentSourcesQuery(const QString&, const QString&) {}
void ComicDownloader::cancelTorrentSourceSearch(const QString&) {}
void ComicDownloader::downloadTorrentSource(const QString&, const QString&, const QString&,
                                            const QString&, const QString&, const QString&,
                                            const QString&) {}
void ComicDownloader::chooseTorrentArchive(const QString&, int) {}
void ComicDownloader::downloadTorrentEdition(const QString&, const QString&, const QString&,
                                             const QString&, const QString&, const QString&,
                                             const QString&, const QString&, const QString&) {}
void ComicDownloader::chooseTorrentFiles(const QString&, const QVariantList&) {}
void ComicDownloader::confirmCombinedArchive(const QString&) {}
QVariantList ComicDownloader::localPages(const QString&) const { return {}; }
bool ComicDownloader::isDownloaded(const QString&) const { return false; }
QVariantMap ComicDownloader::statusOf(const QString&) const { return {}; }
QVariantList ComicDownloader::downloadedIssues() const { return {}; }
QVariantList ComicDownloader::activeIssueJobs() const { return {}; }
void ComicDownloader::cancelDownload(const QString&) {}
QVariantMap ComicDownloader::deleteIssue(const QString&) { return {}; }
QVariantMap ComicDownloader::packVolumes(const QString&) const { return {}; }
void ComicDownloader::selfTest(const QString&) {}
void ComicDownloader::runPackSelfTest(const QString&) {}

DownloadStore::DownloadStore(QObject* parent) : QObject(parent) {}
DownloadStore::~DownloadStore() = default;
QVariantMap DownloadStore::status() const { return {}; }
void DownloadStore::startDownload(const QVariantMap&) {}
void DownloadStore::cancelDownload() {}
void DownloadStore::revealDownload() {}
void DownloadStore::resetDownload() {}
void DownloadStore::enqueue(const QVariantMap&) {}
void DownloadStore::enqueueBatch(const QVariantList&) {}
bool DownloadStore::hasVideo(const QString&) const { return false; }
void DownloadStore::feedUrl(const QString&, const QString&) {}
void DownloadStore::feedSource(const QString&, const QString&, const QVariantMap&) {}
void DownloadStore::failJob(const QString&, const QString&) {}
QVariantList DownloadStore::downloadedVideos() const { return {}; }
QVariantList DownloadStore::jobs() const { return {}; }
void DownloadStore::retryJob(const QString&) {}
void DownloadStore::pauseJob(const QString&) {}
void DownloadStore::resumeJob(const QString&) {}
void DownloadStore::cancelJob(const QString&) {}
QVariantMap DownloadStore::removeVideo(const QString&) { return {}; }
void DownloadStore::selfTest(const QString&) {}

void MangaTankobanService::prepareSeries(QVariantMap, QVariantList, QVariantList) {}
QVariantList MangaTankobanService::volumesForSeries(QString) const { return {}; }
bool MangaTankobanService::modeEnabled(QString) const { return false; }
void MangaTankobanService::setModeEnabled(QString, bool) {}
void MangaTankobanService::searchSources(QString) {}
void MangaTankobanService::searchSeriesSources(QString, QString) {}
void MangaTankobanService::downloadNyaa(QString, QString) {}
void MangaTankobanService::downloadNyaaBatch(QStringList, QString) {}
void MangaTankobanService::compileWeebCentral(QString) {}
QVariantMap MangaTankobanService::statusOf(QString) const { return {}; }
QVariantList MangaTankobanService::localPages(QString) const { return {}; }
QVariantList MangaTankobanService::downloadedVolumes() const { return {}; }
QVariantList MangaTankobanService::activeVolumeJobs() const { return {}; }
void MangaTankobanService::cancel(QString) {}
QVariantMap MangaTankobanService::remove(QString) { return {}; }
void MangaTankobanService::runDownloadSelfTest(const QString&) {}
