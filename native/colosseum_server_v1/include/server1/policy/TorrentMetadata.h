#pragma once

#include "server1/policy/Value.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace server1::policy {

using ByteBuffer = std::vector<std::uint8_t>;

struct TorrentFile final {
    std::string path;
    std::string name;
    std::uint64_t length = 0;
    std::uint64_t offset = 0;
};

struct VerificationPieceIndex final {
    std::size_t value = 0;
};

struct VirtualPieceIndex final {
    std::size_t value = 0;
};

struct VerificationPieceCoordinate final {
    VerificationPieceIndex piece;
    std::uint64_t offset = 0;
    std::uint64_t length = 0;
};

struct VirtualPieceCoordinate final {
    VirtualPieceIndex piece;
    VerificationPieceIndex verificationIndex;
    std::uint64_t offset = 0;
    std::uint64_t verificationOffset = 0;
    std::uint64_t length = 0;
};

struct WireBlockCoordinate final {
    VirtualPieceIndex virtualPiece;
    std::uint64_t offset = 0;
    std::uint64_t length = 0;
};

class VirtualPieceMap final {
public:
    static constexpr std::uint64_t kVirtualPieceLength = 524288;
    static constexpr std::uint64_t kDefaultWireBlockLength = 16384;

    static VirtualPieceMap create(std::uint64_t totalLength,
                                  std::uint64_t verificationPieceLength,
                                  std::uint64_t wireBlockLength = kDefaultWireBlockLength);

    [[nodiscard]] bool isVirtualized() const noexcept;
    [[nodiscard]] std::uint64_t totalLength() const noexcept;
    [[nodiscard]] std::uint64_t verificationPieceLength() const noexcept;
    [[nodiscard]] std::uint64_t virtualPieceLength() const noexcept;
    [[nodiscard]] std::uint64_t wireBlockLength() const noexcept;

    [[nodiscard]] const std::vector<VerificationPieceCoordinate> &verificationPieces() const noexcept;
    [[nodiscard]] const std::vector<VirtualPieceCoordinate> &virtualPieces() const noexcept;
    [[nodiscard]] const std::vector<WireBlockCoordinate> &wireBlocks() const noexcept;

private:
    VirtualPieceMap(std::uint64_t totalLength,
                    std::uint64_t verificationPieceLength,
                    std::uint64_t virtualPieceLength,
                    std::uint64_t wireBlockLength,
                    bool virtualized,
                    std::vector<VerificationPieceCoordinate> verificationPieces,
                    std::vector<VirtualPieceCoordinate> virtualPieces,
                    std::vector<WireBlockCoordinate> wireBlocks);

    std::uint64_t totalLength_;
    std::uint64_t verificationPieceLength_;
    std::uint64_t virtualPieceLength_;
    std::uint64_t wireBlockLength_;
    bool virtualized_;
    std::vector<VerificationPieceCoordinate> verificationPieces_;
    std::vector<VirtualPieceCoordinate> virtualPieces_;
    std::vector<WireBlockCoordinate> wireBlocks_;
};

using PieceGeometry = VirtualPieceMap;

class TorrentMetadata final {
public:
    [[nodiscard]] static std::optional<TorrentMetadata> parse(const ByteBuffer &torrent,
                                                               std::string *error = nullptr);
    [[nodiscard]] static std::optional<TorrentMetadata> decode(const ByteBuffer &torrent,
                                                                std::string *error = nullptr)
    {
        return parse(torrent, error);
    }

    [[nodiscard]] const Value &info() const noexcept;
    [[nodiscard]] const ByteBuffer &infoBuffer() const noexcept;
    [[nodiscard]] const ByteBuffer &infoHashBytes() const noexcept;
    [[nodiscard]] const std::string &infoHash() const noexcept;
    [[nodiscard]] const std::string &name() const noexcept;
    [[nodiscard]] const std::vector<TorrentFile> &files() const noexcept;
    [[nodiscard]] const std::vector<std::string> &pieces() const noexcept;
    [[nodiscard]] const std::vector<std::string> &announce() const noexcept;
    [[nodiscard]] const std::vector<std::string> &urlList() const noexcept;
    [[nodiscard]] std::uint64_t length() const noexcept;
    [[nodiscard]] std::uint64_t pieceLength() const noexcept;
    [[nodiscard]] std::uint64_t lastPieceLength() const noexcept;
    [[nodiscard]] bool isPrivate() const noexcept;
    [[nodiscard]] const std::optional<bool> &privateValue() const noexcept;
    [[nodiscard]] const std::optional<std::int64_t> &creationDate() const noexcept;
    [[nodiscard]] const std::optional<std::string> &createdBy() const noexcept;
    [[nodiscard]] const std::optional<std::string> &comment() const noexcept;
    [[nodiscard]] const VirtualPieceMap &geometry() const noexcept;

private:
    TorrentMetadata(Value info,
                    ByteBuffer infoBuffer,
                    ByteBuffer infoHashBytes,
                    std::string infoHash,
                    std::string name,
                    std::vector<TorrentFile> files,
                    std::vector<std::string> pieces,
                    std::vector<std::string> announce,
                    std::vector<std::string> urlList,
                    std::uint64_t length,
                    std::uint64_t pieceLength,
                    std::uint64_t lastPieceLength,
                    std::optional<bool> privateValue,
                    std::optional<std::int64_t> creationDate,
                    std::optional<std::string> createdBy,
                    std::optional<std::string> comment,
                    VirtualPieceMap geometry);

    Value info_;
    ByteBuffer infoBuffer_;
    ByteBuffer infoHashBytes_;
    std::string infoHash_;
    std::string name_;
    std::vector<TorrentFile> files_;
    std::vector<std::string> pieces_;
    std::vector<std::string> announce_;
    std::vector<std::string> urlList_;
    std::uint64_t length_;
    std::uint64_t pieceLength_;
    std::uint64_t lastPieceLength_;
    std::optional<bool> private_;
    std::optional<std::int64_t> creationDate_;
    std::optional<std::string> createdBy_;
    std::optional<std::string> comment_;
    VirtualPieceMap geometry_;
};

} // namespace server1::policy
