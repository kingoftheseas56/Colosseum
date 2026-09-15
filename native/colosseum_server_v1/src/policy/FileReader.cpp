#include "server1/policy/FileReader.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>

namespace server1::policy {

namespace {

std::size_t checkedCoordinate(std::int64_t base, std::size_t relative)
{
    if (base < 0) {
        throw std::invalid_argument("negative file coordinate");
    }
    const auto unsignedBase = static_cast<std::uint64_t>(base);
    if (relative > std::numeric_limits<std::uint64_t>::max() - unsignedBase
        || unsignedBase + relative > std::numeric_limits<std::size_t>::max()) {
        throw std::overflow_error("file coordinate overflow");
    }
    return static_cast<std::size_t>(unsignedBase + relative);
}

std::size_t saturatingAdd(std::size_t left, std::size_t right)
{
    return right > std::numeric_limits<std::size_t>::max() - left
        ? std::numeric_limits<std::size_t>::max()
        : left + right;
}

std::atomic<std::uint64_t> nextFileReadToken{1};

} // namespace

struct FileReader::State final {
    struct CompletedRead final {
        ByteBuffer bytes;
    };

    struct DetachedOwnership final {
        std::vector<std::uint64_t> tokens;
        bool selection = false;
    };

    Scheduler *scheduler = nullptr;
    FileReaderSource *source = nullptr;
    Refresh refresh;
    SelectionId selectionId = 0;
    std::uint64_t generation = 0;
    std::size_t pieceLength = 0;
    std::size_t globalStart = 0;
    std::size_t globalEnd = 0;
    std::size_t startPiece = 0;
    std::size_t endPiece = 0;
    std::size_t nextReadPiece = 0;
    std::size_t nextDeliverPiece = 0;
    std::size_t length = 0;
    std::size_t remaining = 0;
    std::size_t bufferPieces = 0;
    std::size_t criticalWidth = 0;
    std::size_t demandBytes = 0;
    std::size_t reservedBytes = 0;
    std::map<std::uint64_t, std::size_t> activeReads;
    std::map<std::size_t, CompletedRead> completedReads;
    std::set<std::size_t> lockedPieces;
    std::vector<ByteBuffer> ready;
    std::optional<std::string> error;
    std::optional<std::size_t> waitingPiece;
    bool selectionActive = false;
    bool pumping = false;
    bool eof = false;
    bool closed = false;
    bool destroyed = false;

    [[nodiscard]] DetachedOwnership detachOwnership()
    {
        DetachedOwnership detached;
        detached.tokens.reserve(activeReads.size());
        for (const auto &[token, piece] : activeReads) {
            static_cast<void>(piece);
            detached.tokens.push_back(token);
        }
        detached.selection = selectionActive;

        activeReads.clear();
        lockedPieces.clear();
        completedReads.clear();
        waitingPiece.reset();
        selectionActive = false;
        return detached;
    }

    void releaseDetached(DetachedOwnership detached)
    {
        for (const auto token : detached.tokens) {
            static_cast<void>(source->cancelRead(token));
        }
        if (detached.selection) {
            static_cast<void>(scheduler->deselect(selectionId));
        }
    }

    void terminalFailure(std::string reason)
    {
        if (closed || eof || error) {
            return;
        }
        if (reason.empty()) {
            reason = "file reader failed";
        }

        error = std::move(reason);
        closed = true;
        releaseDetached(detachOwnership());
    }
};

FileReader::FileReader(Scheduler &scheduler,
                       FileReaderSource &source,
                       TorrentFile file,
                       std::size_t pieceLength,
                       FileReadOptions options,
                       Refresh refresh)
    : state_(std::make_shared<State>())
{
    if (pieceLength == 0 || file.length <= 0) {
        throw std::invalid_argument("file reader requires positive geometry");
    }
    const auto fileLength = static_cast<std::uint64_t>(file.length);
    if (fileLength > std::numeric_limits<std::size_t>::max()) {
        throw std::overflow_error("file length exceeds native size");
    }
    const auto end = options.end.value_or(static_cast<std::size_t>(fileLength - 1));
    if (options.start > end || end >= fileLength) {
        throw std::out_of_range("file read range outside file");
    }

    state_->scheduler = &scheduler;
    state_->source = &source;
    state_->refresh = std::move(refresh);
    state_->generation = options.generation;
    state_->pieceLength = pieceLength;
    state_->globalStart = checkedCoordinate(file.offset, options.start);
    state_->globalEnd = checkedCoordinate(file.offset, end);
    state_->startPiece = state_->globalStart / pieceLength;
    state_->endPiece = state_->globalEnd / pieceLength;
    state_->nextReadPiece = state_->startPiece;
    state_->nextDeliverPiece = state_->startPiece;
    state_->length = end - options.start + 1;
    state_->remaining = state_->length;
    state_->bufferPieces = options.bufferBytes == 0 ? 0 : options.bufferBytes / pieceLength;
    state_->criticalWidth = std::min<std::size_t>((1024U * 1024U) / pieceLength, 4);

    const auto selectTo = state_->bufferPieces == 0
        ? state_->endPiece
        : std::min(state_->endPiece,
                   saturatingAdd(state_->startPiece, state_->bufferPieces));
    state_->selectionId = scheduler.select(state_->startPiece,
                                           state_->endPiece,
                                           options.priority,
                                           selectTo,
                                           state_->startPiece);
    state_->selectionActive = true;
}

FileReader::~FileReader()
{
    const auto state = state_;
    closeState(state, true);
}

void FileReader::request(std::size_t bytes)
{
    if (bytes == 0 || state_->closed || state_->eof || state_->error) {
        return;
    }
    state_->demandBytes = saturatingAdd(state_->demandBytes, bytes);
    pump(state_);
}

void FileReader::notifyPiece(std::size_t piece)
{
    if (state_->closed || !state_->waitingPiece || *state_->waitingPiece != piece
        || !state_->source->hasPiece(piece)) {
        return;
    }
    state_->waitingPiece.reset();
    pump(state_);
}

void FileReader::fail(std::string error)
{
    const auto state = state_;
    state->terminalFailure(std::move(error));
}

std::vector<ByteBuffer> FileReader::takeData()
{
    std::vector<ByteBuffer> result;
    result.swap(state_->ready);
    std::size_t delivered = 0;
    for (const auto &chunk : result) {
        delivered = saturatingAdd(delivered, chunk.size());
    }
    state_->reservedBytes -= std::min(state_->reservedBytes, delivered);
    state_->demandBytes -= std::min(state_->demandBytes, delivered);
    pump(state_);
    return result;
}

std::optional<std::string> FileReader::takeError()
{
    auto error = std::move(state_->error);
    state_->error.reset();
    return error;
}

void FileReader::close()
{
    const auto state = state_;
    closeState(state, false);
}

std::size_t FileReader::length() const noexcept { return state_->length; }
std::size_t FileReader::startPiece() const noexcept { return state_->startPiece; }
std::size_t FileReader::endPiece() const noexcept { return state_->endPiece; }
std::size_t FileReader::bufferPieces() const noexcept { return state_->bufferPieces; }
std::size_t FileReader::criticalWidth() const noexcept { return state_->criticalWidth; }
SelectionId FileReader::selectionId() const noexcept { return state_->selectionId; }
std::uint64_t FileReader::generation() const noexcept { return state_->generation; }
std::size_t FileReader::pendingReads() const noexcept { return state_->activeReads.size(); }
const std::set<std::size_t> &FileReader::lockedPieces() const noexcept { return state_->lockedPieces; }
bool FileReader::eof() const noexcept { return state_->eof; }
bool FileReader::closed() const noexcept { return state_->closed; }

bool FileReader::hasActiveSelection() const
{
    return state_->selectionActive && state_->scheduler->find(state_->selectionId).has_value();
}

void FileReader::pump(const std::shared_ptr<State> &state)
{
    if (state->pumping || state->closed || state->eof || state->error) {
        return;
    }
    state->pumping = true;
    while (!state->closed && !state->eof && !state->error
           && state->activeReads.size() < 2
           && state->nextReadPiece <= state->endPiece
           && state->reservedBytes < state->demandBytes) {
        const auto piece = state->nextReadPiece;
        if (!state->source->hasPiece(piece)) {
            if (!state->waitingPiece || *state->waitingPiece != piece) {
                state->waitingPiece = piece;
                state->scheduler->setCritical(piece, state->criticalWidth);
                if (state->refresh) {
                    state->refresh();
                }
            }
            break;
        }

        state->waitingPiece.reset();
        const auto pieceStart = piece * state->pieceLength;
        const auto ownedStart = std::max(state->globalStart, pieceStart);
        const auto pieceEnd = saturatingAdd(pieceStart, state->pieceLength - 1);
        const auto ownedEnd = std::min(state->globalEnd, pieceEnd);
        const auto ownedLength = ownedEnd - ownedStart + 1;
        const auto token = nextFileReadToken.fetch_add(1, std::memory_order_relaxed);
        state->activeReads.emplace(token, piece);
        state->lockedPieces.insert(piece);
        state->reservedBytes = saturatingAdd(state->reservedBytes, ownedLength);
        ++state->nextReadPiece;

        if (state->bufferPieces != 0 && state->nextReadPiece <= state->endPiece) {
            static_cast<void>(state->scheduler->updateReadWindow(
                state->selectionId,
                state->nextReadPiece,
                std::min(state->endPiece,
                         saturatingAdd(state->nextReadPiece, state->bufferPieces))));
        }

        std::weak_ptr<State> weakState = state;
        state->source->readPiece(
            token,
            piece,
            [weakState](std::uint64_t completedToken,
                        std::size_t completedPiece,
                        ByteBuffer bytes,
                        std::string error) {
                if (const auto locked = weakState.lock()) {
                    complete(locked,
                             completedToken,
                             completedPiece,
                             std::move(bytes),
                             std::move(error));
                }
            });
    }
    state->pumping = false;
}

void FileReader::complete(const std::shared_ptr<State> &state,
                          std::uint64_t requestToken,
                          std::size_t piece,
                          ByteBuffer bytes,
                          std::string error)
{
    const auto active = state->activeReads.find(requestToken);
    if (active == state->activeReads.end() || active->second != piece) {
        return;
    }
    state->activeReads.erase(active);
    state->lockedPieces.erase(piece);
    if (state->destroyed || state->closed) {
        return;
    }
    if (!error.empty()) {
        state->terminalFailure(std::move(error));
        return;
    }
    state->completedReads[piece] = {std::move(bytes)};

    while (true) {
        const auto completed = state->completedReads.find(state->nextDeliverPiece);
        if (completed == state->completedReads.end()) {
            break;
        }
        const auto pieceStart = state->nextDeliverPiece * state->pieceLength;
        const auto ownedStart = std::max(state->globalStart, pieceStart);
        const auto pieceEnd = saturatingAdd(pieceStart, state->pieceLength - 1);
        const auto ownedEnd = std::min(state->globalEnd, pieceEnd);
        const auto offset = ownedStart - pieceStart;
        const auto count = ownedEnd - ownedStart + 1;
        if (offset > completed->second.bytes.size()
            || count > completed->second.bytes.size() - offset) {
            state->terminalFailure("piece shorter than requested file span");
            return;
        }
        state->ready.emplace_back(completed->second.bytes.begin()
                                      + static_cast<std::ptrdiff_t>(offset),
                                  completed->second.bytes.begin()
                                      + static_cast<std::ptrdiff_t>(offset + count));
        state->remaining -= count;
        state->completedReads.erase(completed);
        ++state->nextDeliverPiece;
    }

    if (state->remaining == 0) {
        state->eof = true;
        if (state->selectionActive) {
            static_cast<void>(state->scheduler->deselect(state->selectionId));
            state->selectionActive = false;
        }
        return;
    }
    pump(state);
}

void FileReader::closeState(const std::shared_ptr<State> &state, bool destroyed)
{
    if (destroyed) {
        state->destroyed = true;
    }
    if (state->closed) {
        return;
    }
    state->closed = true;
    state->releaseDetached(state->detachOwnership());
}

} // namespace server1::policy
