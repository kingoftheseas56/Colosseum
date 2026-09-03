#include "FileStream.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace colosseum::server::scheduler {

FileStream::FileStream(SchedulerSpine &scheduler,
                       IFilePieceStore &store,
                       const FileSpan file,
                       const std::size_t pieceLength,
                       FileStreamOptions options)
    : scheduler_(scheduler),
      store_(store),
      file_(file),
      pieceLength_(pieceLength)
{
    if (pieceLength_ == 0 || file_.length == 0) {
        throw std::invalid_argument("FileStream requires non-zero file and piece lengths");
    }
    const auto end = options.end.value_or(file_.length - 1);
    if (options.start > end || end >= file_.length) {
        throw std::out_of_range("FileStream byte range is outside the file");
    }

    const auto absoluteOffset = options.start + file_.offset;
    length_ = end - options.start + 1;
    remaining_ = length_;
    startPiece_ = absoluteOffset / pieceLength_;
    endPiece_ = (end + file_.offset) / pieceLength_;
    nextPiece_ = startPiece_;
    emitPiece_ = startPiece_;
    initialOffset_ = absoluteOffset - startPiece_ * pieceLength_;

    criticalWidth_ = std::min<std::size_t>(
        1024 * 1024 / pieceLength_, 4);
    if (options.bufferBytes) {
        const auto pieces = *options.bufferBytes / pieceLength_;
        if (pieces > 0) {
            bufferPieces_ = pieces;
        }
    }

    const bool priority = options.priority.value_or(true);
    selection_ = scheduler_.select(startPiece_, endPiece_, priority,
                                   notifyCallback());
    if (bufferPieces_) {
        selection_->selectTo = std::min(endPiece_, nextPiece_ + *bufferPieces_);
        selection_->readFrom = nextPiece_;
    }
}

FileStream::~FileStream()
{
    destroy();
}

std::function<void()> FileStream::notifyCallback()
{
    const std::weak_ptr<int> weak = lifetime_;
    return [weak, this]() {
        if (!weak.expired()) {
            notify();
        }
    };
}

void FileStream::start()
{
    if (started_ || destroyed_ || ended_) {
        return;
    }
    started_ = true;
    pump();
}

void FileStream::notify()
{
    if (destroyed_ || ended_ || !started_) {
        return;
    }
    waiting_ = false;
    pump();
}

void FileStream::pump()
{
    if (pumping_ || destroyed_ || ended_ || remaining_ == 0) {
        return;
    }
    pumping_ = true;
    while (!destroyed_ && !ended_ && remaining_ > 0
           && inFlight_ < MaxReadsInFlight && nextPiece_ <= endPiece_) {
        if (!scheduler_.isPieceAvailable(nextPiece_)) {
            if (!scheduler_.isSelected(nextPiece_)) {
                recoverySelections_.push_back(
                    scheduler_.select(nextPiece_, endPiece_, true, notifyCallback()));
            }
            waiting_ = true;
            scheduler_.critical(nextPiece_, criticalWidth_);
            scheduler_.refresh();
            break;
        }

        waiting_ = false;
        const auto piece = nextPiece_++;
        scheduleRead(piece);
        updateMovingWindow();
    }
    pumping_ = false;
}

void FileStream::scheduleRead(const std::size_t piece)
{
    scheduler_.lockPiece(piece);
    inFlightPieces_.push_back(piece);
    ++inFlight_;
    const std::weak_ptr<int> weak = lifetime_;
    const auto readToken = store_.readPiece(piece, [weak, this, piece](std::error_code error,
                                                                       std::vector<std::byte> data) {
        if (weak.expired()) {
            return;
        }
        for (auto it = readTokens_.begin(); it != readTokens_.end();) {
            if (it->second == piece) {
                it = readTokens_.erase(it);
            } else {
                ++it;
            }
        }
        scheduler_.unlockPiece(piece);
        const auto found = std::find(inFlightPieces_.begin(),
                                     inFlightPieces_.end(), piece);
        if (found != inFlightPieces_.end()) {
            inFlightPieces_.erase(found);
        }
        if (inFlight_ > 0) {
            --inFlight_;
        }
        if (error) {
            fail(error);
            return;
        }
        pendingPieces_[piece] = std::move(data);
        drainOrdered();
        if (!destroyed_ && !ended_) {
            pump();
        }
    });
    if (readToken != 0 && !destroyed_ &&
        std::find(inFlightPieces_.begin(), inFlightPieces_.end(), piece)
            != inFlightPieces_.end()) {
        readTokens_[readToken] = piece;
    }
}

void FileStream::updateMovingWindow()
{
    if (!selection_ || !bufferPieces_) {
        return;
    }
    selection_->readFrom = nextPiece_;
    selection_->selectTo = std::min(endPiece_, nextPiece_ + *bufferPieces_);
}

void FileStream::drainOrdered()
{
    while (!destroyed_ && !ended_ && remaining_ > 0) {
        const auto found = pendingPieces_.find(emitPiece_);
        if (found == pendingPieces_.end()) {
            break;
        }

        auto &piece = found->second;
        const auto offset = emitPiece_ == startPiece_ ? initialOffset_ : 0;
        if (offset > piece.size()) {
            fail(std::make_error_code(std::errc::io_error));
            return;
        }
        const auto available = piece.size() - offset;
        if (available == 0) {
            fail(std::make_error_code(std::errc::io_error));
            return;
        }
        const auto count = std::min(remaining_, available);
        std::vector<std::byte> chunk(
            piece.begin() + static_cast<std::ptrdiff_t>(offset),
            piece.begin() + static_cast<std::ptrdiff_t>(offset + count));
        pendingPieces_.erase(found);
        remaining_ -= count;
        ++emitPiece_;
        updateMovingWindow();
        if (chunkObserver_ && !chunk.empty()) {
            chunkObserver_(chunk);
        }
        if (remaining_ == 0) {
            finish();
            return;
        }
    }
}
void FileStream::fail(const std::error_code error)
{
    if (destroyed_ || ended_) {
        return;
    }
    if (errorObserver_) {
        errorObserver_(error);
    }
    destroy();
}

void FileStream::finish()
{
    if (destroyed_ || ended_) {
        return;
    }
    ended_ = true;
    waiting_ = false;
    scheduler_.deselect(selection_);
    for (const auto &selection : recoverySelections_) {
        scheduler_.deselect(selection);
    }
    recoverySelections_.clear();
    if (endObserver_) {
        endObserver_();
    }
}
void FileStream::destroy()
{
    if (destroyed_) {
        return;
    }
    destroyed_ = true;
    waiting_ = false;
    lifetime_.reset();
    for (const auto &[token, piece] : readTokens_) {
        (void)piece;
        store_.cancelRead(token);
    }
    readTokens_.clear();
    for (const auto piece : inFlightPieces_) {
        scheduler_.unlockPiece(piece);
    }
    inFlightPieces_.clear();
    inFlight_ = 0;
    pendingPieces_.clear();
    scheduler_.deselect(selection_);
    for (const auto &selection : recoverySelections_) {
        scheduler_.deselect(selection);
    }
    recoverySelections_.clear();
}

} // namespace colosseum::server::scheduler
