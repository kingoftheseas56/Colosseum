#include "TorrentPieceSource.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>

#include <chrono>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <libtorrent/file_storage.hpp>
#include <libtorrent/hasher.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_status.hpp>

namespace colosseum::server::integration {
namespace {

std::error_code sourceError(const std::errc error)
{
    return std::make_error_code(error);
}

constexpr std::int64_t MaxQint64 = (std::numeric_limits<std::int64_t>::max)();

struct PieceGeometry final {
    std::int64_t length = 0;
    int pieceLength = 0;
    int pieceCount = 0;
    std::shared_ptr<const lt::torrent_info> info;
};

std::optional<PieceGeometry> geometry(const lt::torrent_handle &torrent)
{
    if (!torrent.is_valid())
        return std::nullopt;
    auto info = torrent.torrent_file();
    if (!info || info->piece_length() <= 0 || info->num_pieces() <= 0
        || info->total_size() < 0)
        return std::nullopt;
    return PieceGeometry{info->total_size(), info->piece_length(),
                         info->num_pieces(), std::move(info)};
}

std::optional<std::int64_t> pieceSize(const PieceGeometry &shape,
                                      const std::size_t piece)
{
    if (piece >= static_cast<std::size_t>(shape.pieceCount))
        return std::nullopt;
    const auto offset = static_cast<std::int64_t>(piece)
        * static_cast<std::int64_t>(shape.pieceLength);
    if (offset < 0 || offset >= shape.length)
        return std::nullopt;
    return std::min<std::int64_t>(shape.pieceLength, shape.length - offset);
}

} // namespace

struct TorrentPieceSource::ReadState final
{
    ReadToken token = 0;
    Subscription subscription = 0;
    std::size_t piece = 0;
    ReadCallback callback;
    bool cancelled = false;
    bool finished = false;
    bool registered = false;
};

struct TorrentPieceSource::State final
{
    struct Waiter final {
        std::size_t piece = 0;
        Ready callback;
    };

    explicit State(lt::torrent_handle value,
                   std::shared_ptr<TorrentVerifiedPieceCache> cache,
                   const bool requireVisibility)
        : torrent(std::move(value)), verifiedCache(std::move(cache)),
          requireExplicitVisibility(requireVisibility)
    {
    }

    lt::torrent_handle torrent;
    mutable std::mutex mutex;
    bool shuttingDown = false;
    Subscription nextSubscription = 1;
    ReadToken nextRead = 1;
    std::map<Subscription, Waiter> waiters;
    std::map<ReadToken, std::shared_ptr<ReadState>> reads;
    std::map<std::size_t, std::vector<std::byte>> providedPieces;
    std::set<std::size_t> visiblePieces;
    std::shared_ptr<TorrentVerifiedPieceCache> verifiedCache;
    bool requireExplicitVisibility = false;
};

TorrentPieceSource::TorrentPieceSource(
    lt::torrent_handle torrent,
    std::shared_ptr<TorrentVerifiedPieceCache> verifiedCache,
    const bool requireExplicitVisibility)
    : state_(std::make_shared<State>(
          std::move(torrent), verifiedCache ? std::move(verifiedCache)
                                             : std::make_shared<TorrentVerifiedPieceCache>(),
          requireExplicitVisibility))
{
}

TorrentPieceSource::~TorrentPieceSource()
{
    std::lock_guard lock(state_->mutex);
    state_->shuttingDown = true;
    state_->waiters.clear();
    for (auto &[token, read] : state_->reads) {
        (void)token;
        read->cancelled = true;
    }
    state_->reads.clear();
}

bool TorrentPieceSource::hasPiece(const std::shared_ptr<State> &state,
                                  const std::size_t piece)
{
    const auto shape = geometry(state->torrent);
    return shape && pieceSize(*shape, piece)
        && state->torrent.have_piece(lt::piece_index_t{
            static_cast<std::int32_t>(piece)});
}

bool TorrentPieceSource::hasPiece(const std::size_t piece) const
{
    return hasPiece(state_, piece);
}

void TorrentPieceSource::markPieceVisible(const std::size_t piece)
{
    if (!hasPiece(state_, piece))
        return;
    std::lock_guard lock(state_->mutex);
    if (!state_->shuttingDown)
        state_->visiblePieces.insert(piece);
}

void TorrentPieceSource::makeUrgent(const std::size_t piece)
{
    // W06's SchedulerTransportBridge is the sole owner of missing-piece
    // requests. Keeping this legacy hook side-effect free prevents a
    // FileStream read from silently handing selection back to libtorrent's
    // deadline picker.
    (void)piece;
}

TorrentPieceSource::Subscription TorrentPieceSource::waitForPiece(
    const std::size_t piece, Ready ready)
{
    if (!ready)
        return 0;
    const auto shape = geometry(state_->torrent);
    if (!shape || !pieceSize(*shape, piece)) {
        ready(sourceError(std::errc::invalid_argument));
        return 0;
    }

    Subscription token = 0;
    bool readyNow = false;
    std::error_code readyError;
    {
        std::lock_guard lock(state_->mutex);
        if (state_->shuttingDown) {
            readyNow = true;
            readyError = sourceError(std::errc::operation_canceled);
        } else if (state_->visiblePieces.contains(piece)
                   || (!state_->requireExplicitVisibility
                       && hasPiece(state_, piece))) {
            readyNow = true;
        } else {
            token = state_->nextSubscription++;
            state_->waiters.emplace(token,
                                    State::Waiter{piece, std::move(ready)});
        }
    }
    if (readyNow) {
        ready(readyError);
        return 0;
    }

    // Close the check/register race: a piece may have completed between the
    // initial check and waiter insertion. notifyPieceFinished() is idempotent
    // for the per-piece waiter set and runs callbacks outside the mutex.
    {
        std::lock_guard lock(state_->mutex);
        if (state_->visiblePieces.contains(piece)
            || (!state_->requireExplicitVisibility && hasPiece(state_, piece)))
            readyNow = true;
    }
    if (readyNow)
        notifyPieceFinished(piece);
    return token;
}

void TorrentPieceSource::cancelWait(const Subscription token)
{
    if (token == 0)
        return;
    std::lock_guard lock(state_->mutex);
    state_->waiters.erase(token);
}

void TorrentPieceSource::notifyPieceFinished(const std::size_t piece)
{
    if (!hasPiece(state_, piece))
        return;

    std::vector<Ready> callbacks;
    {
        std::lock_guard lock(state_->mutex);
        if (state_->shuttingDown)
            return;
        state_->visiblePieces.insert(piece);
        for (auto it = state_->waiters.begin(); it != state_->waiters.end();) {
            if (it->second.piece != piece) {
                ++it;
                continue;
            }
            callbacks.push_back(std::move(it->second.callback));
            it = state_->waiters.erase(it);
        }
    }
    for (auto &callback : callbacks)
        if (callback)
            callback({});
}

std::vector<std::byte> TorrentPieceSource::readBlock(
    const std::shared_ptr<State> &state,
    const scheduler::WireBlock &block,
    std::error_code &error)
{
    error.clear();
    const auto shape = geometry(state->torrent);
    if (!shape) {
        error = sourceError(std::errc::not_connected);
        return {};
    }
    const auto bytesInPiece = pieceSize(*shape, block.piece);
    if (!bytesInPiece || block.offset > static_cast<std::size_t>(*bytesInPiece)
        || block.length > static_cast<std::size_t>(*bytesInPiece)
                               - block.offset) {
        error = sourceError(std::errc::invalid_argument);
        return {};
    }

    if (!hasPiece(state, block.piece)) {
        error = sourceError(std::errc::operation_would_block);
        return {};
    }

    {
        std::lock_guard lock(state->mutex);
        const auto provided = state->providedPieces.find(block.piece);
        if (provided != state->providedPieces.end()) {
            const auto &bytes = provided->second;
            if (bytes.size() != static_cast<std::size_t>(*bytesInPiece)) {
                error = sourceError(std::errc::io_error);
                return {};
            }
            return std::vector<std::byte>(
                bytes.begin() + static_cast<std::ptrdiff_t>(block.offset),
                bytes.begin() + static_cast<std::ptrdiff_t>(block.offset + block.length));
        }
    }
    if (block.length == 0)
        return {};
    if (block.length > static_cast<std::size_t>(MaxQint64)) {
        error = sourceError(std::errc::value_too_large);
        return {};
    }

    const auto pieceOffset = static_cast<std::int64_t>(block.piece)
        * static_cast<std::int64_t>(shape->pieceLength);
    const auto wantedStart = pieceOffset + static_cast<std::int64_t>(block.offset);
    const auto wantedEnd = wantedStart + static_cast<std::int64_t>(block.length);
    std::vector<std::byte> result(block.length);
    std::size_t covered = 0;
    const auto storage = shape->info->files();
    const auto savePath = state->torrent.status().save_path;

    for (int index = 0; index < storage.num_files(); ++index) {
        const lt::file_index_t fileIndex{index};
        const auto fileStart = storage.file_offset(fileIndex);
        const auto fileLength = storage.file_size(fileIndex);
        const auto fileEnd = fileStart + fileLength;
        const auto overlapStart = std::max(wantedStart, fileStart);
        const auto overlapEnd = std::min(wantedEnd, fileEnd);
        if (overlapStart >= overlapEnd)
            continue;

        const auto count = overlapEnd - overlapStart;
        const auto destination = static_cast<std::size_t>(overlapStart - wantedStart);
        if (storage.pad_file_at(fileIndex)) {
            std::fill(result.begin() + static_cast<std::ptrdiff_t>(destination),
                      result.begin() + static_cast<std::ptrdiff_t>(destination + count),
                      std::byte{});
            covered += static_cast<std::size_t>(count);
            continue;
        }

        const QString path = QString::fromUtf8(
            storage.file_path(fileIndex, savePath).c_str());
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            error = sourceError(std::errc::io_error);
            return {};
        }
        if (!file.seek(overlapStart - fileStart)) {
            error = sourceError(std::errc::io_error);
            return {};
        }
        const QByteArray bytes = file.read(count);
        if (bytes.size() != count) {
            error = sourceError(std::errc::io_error);
            return {};
        }
        std::memcpy(result.data() + destination, bytes.constData(),
                    static_cast<std::size_t>(count));
        covered += static_cast<std::size_t>(count);
    }

    if (covered != block.length) {
        error = sourceError(std::errc::io_error);
        return {};
    }
    return result;
}

void TorrentPieceSource::consumeProvidedPiece(
    const std::shared_ptr<State> &state, const std::size_t piece)
{
    std::lock_guard lock(state->mutex);
    state->providedPieces.erase(piece);
}

std::optional<std::vector<std::byte>> TorrentPieceSource::readSharedPiece(
    const std::shared_ptr<State> &state, const std::size_t piece)
{
    std::lock_guard lock(state->verifiedCache->mutex);
    const auto found = state->verifiedCache->pieces.find(piece);
    if (found == state->verifiedCache->pieces.end())
        return std::nullopt;
    return found->second;
}

void TorrentPieceSource::consumeSharedPiece(
    const std::shared_ptr<State> &state, const std::size_t piece)
{
    std::lock_guard lock(state->verifiedCache->mutex);
    state->verifiedCache->pieces.erase(piece);
}

bool TorrentPieceSource::persistPiece(
    const std::shared_ptr<State> &state, const std::size_t piece,
    const std::vector<std::byte> &bytes)
{
    const auto shape = geometry(state->torrent);
    const auto bytesInPiece = shape ? pieceSize(*shape, piece) : std::nullopt;
    if (!shape || !bytesInPiece
        || bytes.size() != static_cast<std::size_t>(*bytesInPiece))
        return false;

    const auto pieceOffset = static_cast<std::int64_t>(piece)
        * static_cast<std::int64_t>(shape->pieceLength);
    const auto wantedEnd = pieceOffset + static_cast<std::int64_t>(*bytesInPiece);
    const auto storage = shape->info->files();
    const auto savePath = state->torrent.status().save_path;
    for (int index = 0; index < storage.num_files(); ++index) {
        const lt::file_index_t fileIndex{index};
        const auto fileStart = storage.file_offset(fileIndex);
        const auto fileEnd = fileStart + storage.file_size(fileIndex);
        const auto overlapStart = std::max(pieceOffset, fileStart);
        const auto overlapEnd = std::min(wantedEnd, fileEnd);
        if (overlapStart >= overlapEnd)
            continue;

        const auto count = overlapEnd - overlapStart;
        const auto sourceOffset = static_cast<std::size_t>(
            overlapStart - pieceOffset);
        if (storage.pad_file_at(fileIndex))
            continue;

        const QString path = QString::fromUtf8(
            storage.file_path(fileIndex, savePath).c_str());
        if (!QDir().mkpath(QFileInfo(path).absolutePath()))
            return false;
        QFile file(path);
        if (!file.open(QIODevice::ReadWrite)
            || !file.seek(overlapStart - fileStart))
            return false;
        const auto written = file.write(
            reinterpret_cast<const char *>(bytes.data() + sourceOffset), count);
        if (written != count)
            return false;
    }
    return true;
}

std::vector<std::byte> TorrentPieceSource::readVerifiedPiece(
    const std::shared_ptr<State> &state, const std::size_t piece,
    std::error_code &error)
{
    constexpr int MaxVisibilityRetries = 50;
    constexpr auto VisibilityRetry = std::chrono::milliseconds(2);
    bool usedProvidedPiece = false;
    {
        std::lock_guard lock(state->mutex);
        usedProvidedPiece = state->providedPieces.contains(piece);
    }
    for (int attempt = 0; attempt < MaxVisibilityRetries; ++attempt) {
        const auto shape = geometry(state->torrent);
        const auto bytesInPiece = shape ? pieceSize(*shape, piece) : std::nullopt;
        if (!bytesInPiece) {
            error = sourceError(std::errc::invalid_argument);
            return {};
        }

        auto bytes = readBlock(state,
                               scheduler::WireBlock{
                                   piece, 0, static_cast<std::size_t>(*bytesInPiece)},
                               error);
        if (error) {
            if (error != sourceError(std::errc::io_error)
                && error != sourceError(std::errc::operation_would_block))
                return {};
            if (attempt + 1 >= MaxVisibilityRetries) {
                if (const auto shared = readSharedPiece(state, piece)) {
                    error.clear();
                    return *shared;
                }
                return {};
            }
            std::this_thread::sleep_for(VisibilityRetry);
            continue;
        }

        // A libtorrent have_piece() transition and its disk write can be
        // observed a few milliseconds apart. Hash the complete piece before
        // handing QFile bytes to W07 so a preallocated/partially flushed file
        // can never become a successful media response.
        // For v2-only metadata there is no v1 SHA-1 piece hash. libtorrent's
        // have_piece() state is the verification authority in that case.
        if (shape && shape->info && !shape->info->v1())
        {
            if (!usedProvidedPiece)
                consumeSharedPiece(state, piece);
            return bytes;
        }
        if (shape && shape->info
            && bytes.size() <= static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
            const auto actual = lt::hasher(
                reinterpret_cast<const char *>(bytes.data()),
                static_cast<int>(bytes.size())).final();
            const auto wanted = shape->info->hash_for_piece(
                lt::piece_index_t{static_cast<std::int32_t>(piece)});
            if (actual == wanted) {
                if (!usedProvidedPiece)
                    consumeSharedPiece(state, piece);
                return bytes;
            }
        }

        if (attempt + 1 < MaxVisibilityRetries)
            std::this_thread::sleep_for(VisibilityRetry);
    }
    if (const auto shared = readSharedPiece(state, piece)) {
        error.clear();
        return *shared;
    }
    error = sourceError(std::errc::io_error);
    return {};
}

std::vector<std::byte> TorrentPieceSource::readBlock(
    const scheduler::WireBlock &block, std::error_code &error) const
{
    error.clear();
    const auto shape = geometry(state_->torrent);
    const auto bytesInPiece = shape ? pieceSize(*shape, block.piece) : std::nullopt;
    if (!bytesInPiece || block.offset > static_cast<std::size_t>(*bytesInPiece)
        || block.length > static_cast<std::size_t>(*bytesInPiece) - block.offset) {
        error = sourceError(std::errc::invalid_argument);
        return {};
    }
    if (!hasPiece(state_, block.piece)) {
        error = sourceError(std::errc::operation_would_block);
        return {};
    }

    auto bytes = readVerifiedPiece(state_, block.piece, error);
    consumeProvidedPiece(state_, block.piece);
    if (error)
        return {};
    return std::vector<std::byte>(
        bytes.begin() + static_cast<std::ptrdiff_t>(block.offset),
        bytes.begin() + static_cast<std::ptrdiff_t>(block.offset + block.length));
}

void TorrentPieceSource::onReadReady(const std::shared_ptr<State> &state,
                                     const std::shared_ptr<ReadState> &read,
                                     std::error_code error)
{
    ReadCallback callback;
    {
        std::lock_guard lock(state->mutex);
        if (state->shuttingDown || read->cancelled || read->finished)
            return;
        read->finished = true;
        if (read->registered)
            state->reads.erase(read->token);
        callback = std::move(read->callback);
    }
    if (!callback)
        return;
    if (error) {
        callback(error, {});
        return;
    }
    const auto shape = geometry(state->torrent);
    const auto bytesInPiece = shape ? pieceSize(*shape, read->piece)
                                    : std::nullopt;
    if (!bytesInPiece) {
        callback(sourceError(std::errc::invalid_argument), {});
        return;
    }
    std::error_code readError;
    auto bytes = readVerifiedPiece(state, read->piece, readError);
    consumeProvidedPiece(state, read->piece);
    if (readError)
        callback(readError, {});
    else
        callback({}, std::move(bytes));
}

TorrentPieceSource::ReadToken TorrentPieceSource::readPiece(
    const std::size_t piece, ReadCallback callback)
{
    if (!callback)
        return 0;
    const auto shape = geometry(state_->torrent);
    const auto bytesInPiece = shape ? pieceSize(*shape, piece) : std::nullopt;
    if (!bytesInPiece) {
        callback(sourceError(std::errc::invalid_argument), {});
        return 0;
    }
    bool visible = false;
    {
        std::lock_guard lock(state_->mutex);
        visible = state_->visiblePieces.contains(piece)
            || (!state_->requireExplicitVisibility && hasPiece(state_, piece));
    }
    if (visible) {
        std::error_code error;
        auto bytes = readVerifiedPiece(state_, piece, error);
        consumeProvidedPiece(state_, piece);
        callback(error, std::move(bytes));
        return 0;
    }

    makeUrgent(piece);
    auto read = std::make_shared<ReadState>();
    {
        std::lock_guard lock(state_->mutex);
        if (state_->shuttingDown) {
            callback(sourceError(std::errc::operation_canceled), {});
            return 0;
        }
        read->token = state_->nextRead++;
        read->piece = piece;
        read->callback = std::move(callback);
        state_->reads.emplace(read->token, read);
    }

    const auto subscription = waitForPiece(piece,
        [state = state_, read](std::error_code error) {
            onReadReady(state, read, error);
        });

    bool cancelSubscription = false;
    ReadToken result = read->token;
    {
        std::lock_guard lock(state_->mutex);
        read->subscription = subscription;
        if (read->finished || read->cancelled || state_->shuttingDown) {
            state_->reads.erase(read->token);
            cancelSubscription = subscription != 0;
            result = 0;
        } else {
            read->registered = true;
        }
    }
    if (cancelSubscription)
        cancelWait(subscription);
    return result;
}

void TorrentPieceSource::cancelRead(const ReadToken token)
{
    if (token == 0)
        return;
    Subscription subscription = 0;
    {
        std::lock_guard lock(state_->mutex);
        const auto found = state_->reads.find(token);
        if (found == state_->reads.end())
            return;
        found->second->cancelled = true;
        subscription = found->second->subscription;
        state_->reads.erase(found);
    }
    cancelWait(subscription);
}

void TorrentPieceSource::provideVerifiedPiece(
    const std::size_t piece, std::vector<std::byte> bytes)
{
    const auto shape = geometry(state_->torrent);
    const auto bytesInPiece = shape ? pieceSize(*shape, piece) : std::nullopt;
    if (!bytesInPiece || bytes.size() != static_cast<std::size_t>(*bytesInPiece)) {
        return;
    }
    persistPiece(state_, piece, bytes);

    std::lock_guard lock(state_->mutex);
    if (!state_->shuttingDown) {
        state_->providedPieces[piece] = std::move(bytes);
        std::lock_guard cacheLock(state_->verifiedCache->mutex);
        state_->verifiedCache->pieces[piece] = state_->providedPieces[piece];
    }
}

} // namespace colosseum::server::integration
