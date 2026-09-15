#include "server1/policy/EngineRegistry.h"

#include "server1/discovery/PeerSearch.h"
#include "server1/policy/CircularPieceStore.h"
#include "server1/policy/PieceStore.h"
#include "server1/policy/Scheduler.h"
#include "server1/policy/TorrentMetadata.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include <set>
#include <stdexcept>
#include <utility>

namespace server1::policy {
namespace {

std::atomic<std::uint64_t> nextTimerOwner{1};

class ReaderStore : public FileReaderSource {
public:
    ~ReaderStore() override = default;
    virtual void close() = 0;
};

class PersistentReaderStore final : public ReaderStore {
public:
    PersistentReaderStore(std::filesystem::path root,
                          const TorrentMetadata &metadata)
        : store_(std::move(root),
                 static_cast<std::size_t>(metadata.geometry().virtualPieceLength()),
                 static_cast<std::size_t>(metadata.length()),
                 static_cast<std::size_t>(metadata.pieceLength()),
                 makeFiles(metadata.files()),
                 metadata.pieces())
    {}

    bool hasPiece(std::size_t piece) const override
    {
        return store_.read(piece).has_value();
    }

    void readPiece(std::uint64_t token,
                   std::size_t piece,
                   Completion completion) override
    {
        std::string error;
        auto bytes = store_.read(piece, &error);
        completion(token, piece, bytes.value_or(ByteBuffer{}),
                   bytes ? std::string{} : std::move(error));
    }

    bool cancelRead(std::uint64_t) override { return false; }
    void close() override { store_.close(); }

private:
    static std::vector<StoreFile> makeFiles(const std::vector<TorrentFile> &files)
    {
        std::vector<StoreFile> result;
        result.reserve(files.size());
        for (const auto &file : files) {
            result.push_back({static_cast<std::size_t>(file.offset),
                              static_cast<std::size_t>(file.length)});
        }
        return result;
    }

    PersistentPieceStore store_;
};

class CircularReaderStore final : public ReaderStore {
public:
    CircularReaderStore(std::filesystem::path root,
                        std::size_t sizeBytes,
                        std::size_t pieceLength)
        : store_(std::move(root), CircularStoreMode::Memory,
                 std::max(sizeBytes, pieceLength), pieceLength)
    {}

    bool hasPiece(std::size_t piece) const override
    {
        return store_.read(piece, 0).has_value();
    }

    void readPiece(std::uint64_t token,
                   std::size_t piece,
                   Completion completion) override
    {
        auto bytes = store_.read(piece, 0);
        completion(token, piece, bytes.value_or(ByteBuffer{}),
                   bytes ? std::string{} : "piece is unavailable");
    }

    bool cancelRead(std::uint64_t) override { return false; }
    void close() override { store_.close(); }

private:
    mutable CircularPieceStore store_;
};

std::size_t optionSize(const Value &options,
                       std::string_view key,
                       std::size_t fallback)
{
    const auto *value = options.find(key);
    if (!value)
        return fallback;
    const auto checked = checkedSize(*value, std::numeric_limits<std::size_t>::max());
    return checked.value_or(fallback);
}

std::vector<std::string> configuredPeerSources(const Value &options)
{
    const auto *peerSearch = options.find("peerSearch");
    if (!peerSearch || !jsTruthy(*peerSearch) || peerSearch->kind() != Value::Kind::Object)
        return {};
    const auto *sources = peerSearch->find("sources");
    if (!sources || sources->kind() != Value::Kind::Array)
        return {};
    std::vector<std::string> result;
    for (const auto &source : sources->asArray())
        if (source.kind() == Value::Kind::String)
            result.push_back(source.asString());
    return result;
}

std::optional<std::size_t> configuredBound(const Value &options,
                                           std::string_view key)
{
    const auto *peerSearch = options.find("peerSearch");
    if (!peerSearch || peerSearch->kind() != Value::Kind::Object)
        return std::nullopt;
    const auto *value = peerSearch->find(key);
    if (!value)
        return std::nullopt;
    return checkedSize(*value, std::numeric_limits<std::size_t>::max());
}

std::string hashHex(const ports::V1InfoHash &hash)
{
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(40);
    for (const auto byte : hash) {
        result.push_back(digits[(byte >> 4U) & 0x0fU]);
        result.push_back(digits[byte & 0x0fU]);
    }
    return result;
}

} // namespace

struct TorrentEngine::Impl final {
    std::string sourceKey;
    ports::EngineGeneration generation = 0;
    Value options = Value::object({});
    std::filesystem::path cachePath;
    std::unique_ptr<ports::TorrentTransport> transport;
    std::optional<TorrentMetadata> metadata;
    std::unique_ptr<Scheduler> scheduler;
    std::unique_ptr<ReaderStore> store;
    std::unique_ptr<discovery::PeerSearch> peerSearch;
    std::vector<std::weak_ptr<FileReader>> readers;
    std::string sourceError;
    std::size_t resumeCount = 0;
    std::uint64_t timerOwner = nextTimerOwner.fetch_add(1);
    bool ready = false;
    bool failed = false;
    bool closed = false;
    bool circular = false;
};

TorrentEngine::TorrentEngine(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl))
{}

TorrentEngine::~TorrentEngine()
{
    close();
}

std::shared_ptr<TorrentEngine>
EngineRegistry::makeEngine(std::string sourceKey,
                           ports::EngineGeneration generation,
                           Value options,
                           std::filesystem::path cachePath,
                           std::unique_ptr<ports::TorrentTransport> transport)
{
    if (!transport || !transport->configureAutonomy(discovery::AutonomyPolicy{}))
        throw std::runtime_error("torrent transport rejected external scheduler ownership");
    auto impl = std::make_unique<TorrentEngine::Impl>();
    impl->sourceKey = std::move(sourceKey);
    impl->generation = generation;
    impl->options = std::move(options);
    impl->cachePath = std::move(cachePath);
    impl->transport = std::move(transport);
    return std::shared_ptr<TorrentEngine>(new TorrentEngine(std::move(impl)));
}

std::string TorrentEngine::sourceKey() const { return impl_->sourceKey; }
ports::EngineGeneration TorrentEngine::generation() const noexcept { return impl_->generation; }
Value TorrentEngine::options() const { return impl_->options; }
bool TorrentEngine::ready() const noexcept { return impl_->ready; }
bool TorrentEngine::failed() const noexcept { return impl_->failed; }
bool TorrentEngine::closed() const noexcept { return impl_->closed; }
std::string TorrentEngine::sourceError() const { return impl_->sourceError; }
std::size_t TorrentEngine::resumeCount() const noexcept { return impl_->resumeCount; }
std::size_t TorrentEngine::fileCount() const noexcept
{
    return impl_->metadata ? impl_->metadata->files().size() : 0;
}
std::vector<TorrentFile> TorrentEngine::files() const
{
    return impl_->metadata ? impl_->metadata->files() : std::vector<TorrentFile>{};
}
std::size_t TorrentEngine::readerCount() const noexcept
{
    return static_cast<std::size_t>(std::count_if(
        impl_->readers.begin(), impl_->readers.end(),
        [](const auto &reader) { return !reader.expired(); }));
}
std::size_t TorrentEngine::selectionCount() const noexcept
{
    return impl_->scheduler ? impl_->scheduler->selections().size() : 0;
}
bool TorrentEngine::hasPersistentStore() const noexcept
{
    return impl_->store && !impl_->circular;
}
bool TorrentEngine::hasCircularStore() const noexcept
{
    return impl_->store && impl_->circular;
}
bool TorrentEngine::hasPeerSearch() const noexcept { return !!impl_->peerSearch; }
std::uint64_t TorrentEngine::timerOwner() const noexcept { return impl_->timerOwner; }
ports::TransportStatistics TorrentEngine::transportStatistics() const
{
    return impl_->transport ? impl_->transport->statistics()
                            : ports::TransportStatistics{};
}

std::shared_ptr<FileReader>
TorrentEngine::createReader(std::size_t fileIndex, FileReadOptions options)
{
    if (!impl_->ready || impl_->closed || !impl_->metadata || !impl_->scheduler || !impl_->store)
        throw std::logic_error("torrent engine is not ready");
    if (fileIndex >= impl_->metadata->files().size())
        throw std::out_of_range("torrent file index is outside metadata");
    if (options.generation == 0)
        options.generation = impl_->generation;
    auto reader = std::make_shared<FileReader>(
        *impl_->scheduler, *impl_->store, impl_->metadata->files()[fileIndex],
        static_cast<std::size_t>(impl_->metadata->geometry().virtualPieceLength()),
        std::move(options));
    impl_->readers.erase(std::remove_if(impl_->readers.begin(), impl_->readers.end(),
                                        [](const auto &item) { return item.expired(); }),
                         impl_->readers.end());
    impl_->readers.push_back(reader);
    return reader;
}

bool TorrentEngine::connectSourcePeer(ports::PeerHandle peer,
                                      std::string address,
                                      std::uint16_t port)
{
    return !impl_->closed && impl_->transport
        && impl_->transport->submit(ports::ConnectAction{
            impl_->generation, peer, std::move(address), port});
}

void TorrentEngine::resume(Value options)
{
    if (impl_->closed)
        return;
    impl_->options = std::move(options);
    ++impl_->resumeCount;
}

std::vector<ports::TorrentObservation> TorrentEngine::pollTransport()
{
    return impl_->closed || !impl_->transport
        ? std::vector<ports::TorrentObservation>{}
        : impl_->transport->poll();
}

bool TorrentEngine::acceptMetadata(const ports::MetadataReadyObservation &metadata,
                                   std::string *error)
{
    if (impl_->closed || impl_->ready || impl_->failed
        || metadata.generation != impl_->generation
        || hashHex(metadata.infoHash) != impl_->sourceKey) {
        return false;
    }

    ByteBuffer torrent{'d', '4', ':', 'i', 'n', 'f', 'o'};
    torrent.insert(torrent.end(), metadata.infoSection.begin(), metadata.infoSection.end());
    torrent.push_back('e');
    std::string parseError;
    auto parsed = TorrentMetadata::parse(torrent, &parseError);
    if (!parsed || parsed->infoHash() != impl_->sourceKey) {
        impl_->failed = true;
        impl_->sourceError = parsed ? "metadata info hash mismatch" : std::move(parseError);
        if (error)
            *error = impl_->sourceError;
        return false;
    }

    try {
        impl_->metadata = std::move(*parsed);
        const auto pieceLength = static_cast<std::size_t>(impl_->metadata->geometry().virtualPieceLength());
        impl_->scheduler = std::make_unique<Scheduler>(impl_->metadata->geometry().virtualPieces().size());
        const auto *circularOption = impl_->options.find("circularBuffer");
        impl_->circular = circularOption && jsTruthy(*circularOption);
        if (impl_->circular) {
            const auto *bufferOption = impl_->options.find("buffer");
            if (!bufferOption || !jsTruthy(*bufferOption))
                throw std::runtime_error("circularBuffer can only be used with buffer");
            impl_->store = std::make_unique<CircularReaderStore>(
                impl_->cachePath, optionSize(impl_->options, "buffer", pieceLength * 4U), pieceLength);
        } else {
            impl_->store = std::make_unique<PersistentReaderStore>(impl_->cachePath, *impl_->metadata);
        }

        const auto *peerSearchOption = impl_->options.find("peerSearch");
        if (peerSearchOption && jsTruthy(*peerSearchOption)) {
            auto sources = discovery::PeerSearch::selectSources(
                metadata.trackers, configuredPeerSources(impl_->options), impl_->sourceKey);
            impl_->peerSearch = std::make_unique<discovery::PeerSearch>(
                std::move(sources), configuredBound(impl_->options, "min"),
                configuredBound(impl_->options, "max"), 0);
        }
        impl_->ready = true;
        return true;
    } catch (const std::exception &exception) {
        impl_->failed = true;
        impl_->sourceError = exception.what();
        if (error)
            *error = impl_->sourceError;
        return false;
    }
}

void TorrentEngine::acceptSourceFailure(const ports::SourceFailureObservation &failure)
{
    if (impl_->closed || impl_->ready || impl_->failed
        || failure.generation != impl_->generation
        || hashHex(failure.infoHash) != impl_->sourceKey) {
        return;
    }
    impl_->failed = true;
    impl_->sourceError = failure.error.empty() ? "torrent source failed" : failure.error;
}

void TorrentEngine::close()
{
    if (!impl_ || impl_->closed)
        return;
    impl_->closed = true;
    for (auto &weakReader : impl_->readers)
        if (auto reader = weakReader.lock())
            reader->close();
    impl_->readers.clear();
    if (impl_->transport)
        impl_->transport->close();
    if (impl_->peerSearch)
        impl_->peerSearch->close();
    if (impl_->store)
        impl_->store->close();
    impl_->peerSearch.reset();
    impl_->store.reset();
    impl_->scheduler.reset();
    impl_->transport.reset();
}

} // namespace server1::policy
