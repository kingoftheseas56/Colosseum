#pragma once

#include "PieceSourceBlockTransport.h"
#include "scheduler/FileStream.h"

#include <cstddef>
#include <memory>

#include <libtorrent/torrent_handle.hpp>

namespace colosseum::server::integration {

// The verified-piece boundary used by W06/W07.  Libtorrent remains the
// authority for have_piece() and piece deadlines; this adapter only waits for
// those verified pieces and reads their exact bytes from libtorrent storage.
class TorrentPieceSource final : public IPieceSource,
                                 public scheduler::IFilePieceStore
{
public:
    explicit TorrentPieceSource(lt::torrent_handle torrent);
    ~TorrentPieceSource() override;

    TorrentPieceSource(const TorrentPieceSource &) = delete;
    TorrentPieceSource &operator=(const TorrentPieceSource &) = delete;

    bool hasPiece(std::size_t piece) const override;
    void makeUrgent(std::size_t piece) override;
    Subscription waitForPiece(std::size_t piece, Ready ready) override;
    void cancelWait(Subscription token) override;
    std::vector<std::byte> readBlock(const scheduler::WireBlock &block,
                                     std::error_code &error) const override;

    ReadToken readPiece(std::size_t piece, ReadCallback callback) override;
    void cancelRead(ReadToken token) override;

    // TorrentEngine's queued pieceFinished signal (or an equivalent alert
    // consumer) calls this after libtorrent has marked the piece verified.
    void notifyPieceFinished(std::size_t piece);

    // W06 hands over the assembled bytes after libtorrent reports the same
    // piece as verified. This closes the disk-writer visibility window where
    // have_piece() can become true before a synchronous QFile read observes
    // the newly flushed bytes.
    void provideVerifiedPiece(std::size_t piece, std::vector<std::byte> bytes);

private:
    struct State;
    struct ReadState;

    static bool hasPiece(const std::shared_ptr<State> &state,
                         std::size_t piece);
    static std::vector<std::byte> readBlock(
        const std::shared_ptr<State> &state,
        const scheduler::WireBlock &block,
        std::error_code &error);
    static std::vector<std::byte> readVerifiedPiece(
        const std::shared_ptr<State> &state,
        std::size_t piece,
        std::error_code &error);
    static void consumeProvidedPiece(const std::shared_ptr<State> &state,
                                     std::size_t piece);
    static void onReadReady(const std::shared_ptr<State> &state,
                            const std::shared_ptr<ReadState> &read,
                            std::error_code error);

    std::shared_ptr<State> state_;
};

} // namespace colosseum::server::integration
