#include "server1/policy/PieceStore.h"

#include <QCryptographicHash>

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace server1::policy {

namespace {

std::string sha1(const ByteBuffer &bytes)
{
    const QByteArray input(reinterpret_cast<const char *>(bytes.data()),
                           static_cast<qsizetype>(bytes.size()));
    return QCryptographicHash::hash(input, QCryptographicHash::Sha1).toHex().toStdString();
}

} // namespace

PersistentPieceStore::PersistentPieceStore(std::filesystem::path root,
                                           std::size_t pieceLength,
                                           std::size_t totalLength,
                                           std::size_t verificationLength,
                                           std::vector<StoreFile> files,
                                           std::vector<std::string> verificationHashes)
    : root_(std::move(root))
    , pieceLength_(pieceLength)
    , totalLength_(totalLength)
    , verificationLength_(verificationLength)
    , files_(std::move(files))
    , verificationHashes_(std::move(verificationHashes))
    , staged_(pieceCount())
    , assembled_(pieceCount(), false)
    , verified_(pieceCount(), false)
    , committed_(pieceCount(), false)
{
    if (pieceLength_ == 0 || verificationLength_ == 0
        || verificationLength_ % pieceLength_ != 0)
        throw std::invalid_argument("invalid piece-store geometry");
}

void PersistentPieceStore::setDestination(std::size_t fileIndex, std::filesystem::path path)
{
    if (fileIndex >= files_.size())
        throw std::out_of_range("store file index");
    destinations_[fileIndex] = std::move(path);
}

std::filesystem::path PersistentPieceStore::destination(std::size_t fileIndex) const
{
    if (fileIndex >= files_.size())
        throw std::out_of_range("store file index");
    const auto found = destinations_.find(fileIndex);
    return found == destinations_.end() ? root_ / std::to_string(fileIndex) : found->second;
}

void PersistentPieceStore::stage(std::size_t piece, ByteBuffer bytes)
{
    if (closed_ || piece >= pieceCount() || bytes.size() != pieceSize(piece))
        throw std::invalid_argument("invalid staged piece");
    staged_[piece] = std::move(bytes);
    assembled_[piece] = true;
    committed_[piece] = false;
}

std::optional<ByteBuffer> PersistentPieceStore::read(std::size_t piece, std::string *error) const
{
    if (piece >= pieceCount()) {
        if (error)
            *error = "piece index out of range";
        return std::nullopt;
    }
    if (staged_[piece].has_value())
        return staged_[piece];

    const auto length = pieceSize(piece);
    ByteBuffer result(length, 0);
    bool touched = false;
    const auto byteStart = piece * pieceLength_;
    const auto byteEnd = byteStart + length;
    for (std::size_t fileIndex = 0; fileIndex < files_.size(); ++fileIndex) {
        const auto &file = files_[fileIndex];
        const auto start = std::max(byteStart, file.offset);
        const auto end = std::min(byteEnd, file.offset + file.length);
        if (start >= end)
            continue;
        const auto path = destination(fileIndex);
        if (!std::filesystem::exists(path)) {
            if (error)
                *error = "File does not exist: " + path.string();
            return std::nullopt;
        }
        std::ifstream input(path, std::ios::binary);
        input.seekg(static_cast<std::streamoff>(start - file.offset));
        input.read(reinterpret_cast<char *>(result.data() + (start - byteStart)),
                   static_cast<std::streamsize>(end - start));
        if (!input) {
            if (error)
                *error = "partial read: " + path.string();
            return std::nullopt;
        }
        touched = true;
    }
    if (!touched) {
        if (error)
            *error = "piece has no destination";
        return std::nullopt;
    }
    return result;
}

VerifyResult PersistentPieceStore::verify(std::size_t piece)
{
    if (piece >= pieceCount())
        throw std::out_of_range("verify piece index");
    const auto ratio = verificationLength_ / pieceLength_;
    const auto real = piece / ratio;
    const auto start = real * ratio;
    const auto end = std::min(pieceCount(), (real + 1) * ratio);
    for (std::size_t index = start; index < end; ++index) {
        if (!assembled_[index] || !staged_[index].has_value())
            return {false, false, start, end};
    }
    ByteBuffer joined;
    for (std::size_t index = start; index < end; ++index)
        joined.insert(joined.end(), staged_[index]->begin(), staged_[index]->end());
    const bool success = real < verificationHashes_.size()
        && sha1(joined) == verificationHashes_[real];
    if (!success) {
        resetRange(start, end);
        return {true, false, start, end};
    }
    for (std::size_t index = start; index < end; ++index)
        verified_[index] = true;
    return {true, true, start, end};
}

CommitResult PersistentPieceStore::commit(std::size_t start, std::size_t endExclusive)
{
    if (paused_) {
        pending_.push_back({start, endExclusive});
        return {CommitState::Queued, 0, false, {}};
    }
    return commitNow(start, endExclusive);
}

void PersistentPieceStore::pauseWrites() noexcept { paused_ = true; }

void PersistentPieceStore::resumeWrites()
{
    paused_ = false;
    const auto pending = std::move(pending_);
    pending_.clear();
    for (const auto &entry : pending)
        (void)commitNow(entry.start, entry.endExclusive);
    if (closeQueued_) {
        ledger_.push_back("close");
        closeQueued_ = false;
        closed_ = true;
    }
}

void PersistentPieceStore::close()
{
    if (paused_ || !pending_.empty()) {
        closeQueued_ = true;
        return;
    }
    ledger_.push_back("close");
    closed_ = true;
}

void PersistentPieceStore::failNextWrite(std::string error)
{
    nextWriteError_ = std::move(error);
}

bool PersistentPieceStore::isAssembled(std::size_t piece) const
{
    return piece < assembled_.size() && assembled_[piece];
}
bool PersistentPieceStore::isVerified(std::size_t piece) const
{
    return piece < verified_.size() && verified_[piece];
}
bool PersistentPieceStore::isCommitted(std::size_t piece) const
{
    return piece < committed_.size() && committed_[piece];
}
bool PersistentPieceStore::closeQueued() const noexcept { return closeQueued_; }
bool PersistentPieceStore::closed() const noexcept { return closed_; }
const std::vector<std::string> &PersistentPieceStore::ledger() const noexcept { return ledger_; }

CommitResult PersistentPieceStore::commitNow(std::size_t start, std::size_t endExclusive)
{
    if (start >= endExclusive || endExclusive > pieceCount())
        return {CommitState::Error, 0, false, "invalid commit range"};
    CommitResult result{CommitState::Committed, 1, false, {}};
    for (std::size_t piece = start; piece < endExclusive; ++piece) {
        ++result.callbackCount;
        if (!verified_[piece])
            return {CommitState::Error, result.callbackCount, false,
                    "piece is not hash verified"};
        std::string error;
        if (!writePiece(piece, &error))
            return {CommitState::Error, result.callbackCount, false, error};
        committed_[piece] = true;
        staged_[piece].reset();
        ledger_.push_back("commit:" + std::to_string(piece));
    }
    return result;
}

bool PersistentPieceStore::writePiece(std::size_t piece, std::string *error)
{
    if (nextWriteError_.has_value()) {
        if (error)
            *error = *nextWriteError_;
        nextWriteError_.reset();
        return false;
    }
    if (!staged_[piece].has_value()) {
        if (error)
            *error = "piece has no staged bytes";
        return false;
    }
    const auto &buffer = *staged_[piece];
    const auto byteStart = piece * pieceLength_;
    const auto byteEnd = byteStart + buffer.size();
    for (std::size_t fileIndex = 0; fileIndex < files_.size(); ++fileIndex) {
        const auto &file = files_[fileIndex];
        const auto start = std::max(byteStart, file.offset);
        const auto end = std::min(byteEnd, file.offset + file.length);
        if (start >= end)
            continue;
        const auto path = destination(fileIndex);
        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path());
        if (!std::filesystem::exists(path)) {
            std::ofstream create(path, std::ios::binary);
        }
        std::fstream output(path, std::ios::binary | std::ios::in | std::ios::out);
        output.seekp(static_cast<std::streamoff>(start - file.offset));
        output.write(reinterpret_cast<const char *>(buffer.data() + (start - byteStart)),
                     static_cast<std::streamsize>(end - start));
        if (!output) {
            if (error)
                *error = "disk write failed: " + path.string();
            return false;
        }
    }
    return true;
}

std::size_t PersistentPieceStore::pieceCount() const noexcept
{
    return totalLength_ == 0 ? 0 : (totalLength_ + pieceLength_ - 1) / pieceLength_;
}

std::size_t PersistentPieceStore::pieceSize(std::size_t piece) const
{
    if (piece >= pieceCount())
        throw std::out_of_range("piece size index");
    return std::min(pieceLength_, totalLength_ - piece * pieceLength_);
}

void PersistentPieceStore::resetRange(std::size_t start, std::size_t endExclusive)
{
    for (std::size_t piece = start; piece < endExclusive; ++piece) {
        staged_[piece].reset();
        assembled_[piece] = false;
        verified_[piece] = false;
        committed_[piece] = false;
    }
}

} // namespace server1::policy
