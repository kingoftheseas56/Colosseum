#include "TorrentPieceSource.h"

#include <QFile>
#include <QString>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <system_error>
#include <utility>
#include <vector>

#include <libtorrent/file_storage.hpp>
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

    explicit State(lt::torrent_handle value)
        : torrent(std::move(value))
    {
    }

    lt::torrent_handle torrent;
    mutable std::mutex mutex;
    bool shuttingDown = false;
    Subscription nextSubscription = 1;
    ReadToken nextRead = 1;
    std::map<Subscription, Waiter> waiters;
    std::map<ReadToken, std::shared_ptr<ReadState>> reads;
};

TorrentPieceSource::TorrentPieceSource(lt::torrent_handle torrent)
    : state_(std::make_shared<State>(std::move(torrent)))
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

void TorrentPieceSource::makeUrgent(const std::size_t piece)
{
    const auto shape = geometry(state_->torrent);
    if (!shape || !pieceSize(*shape, piece))
        return;
    state_->torrent.set_piece_deadline(
        lt::piece_index_t{static_cast<std::int32_t>(piece)}, 0);
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
        } else if (hasPiece(state_, piece)) {
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
    if (hasPiece(state_, piece))
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
        if (!file.open(QIODevice::ReadOnly)
            || !file.seek(overlapStart - fileStart)) {
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

std::vector<std::byte> TorrentPieceSource::readBlock(
    const scheduler::WireBlock &block, std::error_code &error) const
{
    return readBlock(state_, block, error);
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
    auto bytes = readBlock(state,
                           scheduler::WireBlock{read->piece, 0,
                                                static_cast<std::size_t>(
                                                    *bytesInPiece)},
                           readError);
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
    if (hasPiece(state_, piece)) {
        std::error_code error;
        auto bytes = readBlock(state_, scheduler::WireBlock{
            piece, 0, static_cast<std::size_t>(*bytesInPiece)}, error);
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

} // namespace colosseum::server::integration
