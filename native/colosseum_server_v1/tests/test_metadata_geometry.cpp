#include "server1/policy/TorrentMetadata.h"

#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <initializer_list>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using server1::policy::TorrentMetadata;
using server1::policy::Value;
using server1::policy::VerificationPieceCoordinate;
using server1::policy::VirtualPieceCoordinate;
using server1::policy::VirtualPieceMap;
using server1::policy::WireBlockCoordinate;

using Bytes = std::vector<std::uint8_t>;

void require(bool condition, std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string(message));
}

void append(Bytes &output, std::string_view text)
{
    output.insert(output.end(), text.begin(), text.end());
}

Bytes bstring(std::string_view value)
{
    Bytes output;
    append(output, std::to_string(value.size()));
    output.push_back(':');
    append(output, value);
    return output;
}

Bytes bbytes(const Bytes &value)
{
    Bytes output;
    append(output, std::to_string(value.size()));
    output.push_back(':');
    output.insert(output.end(), value.begin(), value.end());
    return output;
}

Bytes bint(std::int64_t value)
{
    Bytes output;
    output.push_back('i');
    append(output, std::to_string(value));
    output.push_back('e');
    return output;
}

Bytes blist(std::initializer_list<Bytes> values)
{
    Bytes output { 'l' };
    for (const auto &value : values)
        output.insert(output.end(), value.begin(), value.end());
    output.push_back('e');
    return output;
}

Bytes bdict(std::initializer_list<std::pair<std::string_view, Bytes>> values)
{
    Bytes output { 'd' };
    for (const auto &[key, value] : values) {
        const auto encodedKey = bstring(key);
        output.insert(output.end(), encodedKey.begin(), encodedKey.end());
        output.insert(output.end(), value.begin(), value.end());
    }
    output.push_back('e');
    return output;
}

Bytes repeatedBytes(std::size_t count, std::uint8_t value)
{
    return Bytes(count, value);
}

std::string nativePath(std::initializer_list<std::string_view> parts)
{
    const char separator = std::filesystem::path::preferred_separator;
    std::string result;
    for (const auto part : parts) {
        if (!result.empty())
            result.push_back(separator);
        result.append(part);
    }
    return result;
}

Bytes oneFileTorrent(std::uint64_t length, std::uint64_t pieceLength)
{
    const Bytes pieces = repeatedBytes(60, 0x11);
    const Bytes info = bdict({
        {"pieces", bbytes(pieces)},
        {"piece length", bint(static_cast<std::int64_t>(pieceLength))},
        {"name", bstring("single")},
        {"length", bint(static_cast<std::int64_t>(length))},
    });
    return bdict({{"info", info}});
}

Bytes multiFileTorrent()
{
    const Bytes files = blist({
        bdict({
            {"length", bint(0)},
            {"path", blist({bstring("empty.txt")})},
        }),
        bdict({
            {"length", bint(600000)},
            {"path", blist({bstring("disc"), bstring("track.mkv")})},
        }),
        bdict({
            {"length", bint(900000)},
            {"path.utf-8", blist({bstring("日本.mkv")})},
            {"path", blist({bstring("fallback.mkv")})},
        }),
    });
    const Bytes info = bdict({
        {"files", files},
        {"piece length", bint(1048576)},
        {"name", bstring("album")},
        {"pieces", bbytes(repeatedBytes(60, 0x22))},
    });
    return bdict({{"info", info}});
}

Bytes differentialTorrent()
{
    const Bytes files = blist({
        bdict({
            {"length", bint(3)},
            {"path", blist({bstring("clip%20one.mkv")})},
        }),
    });
    const Bytes info = bdict({
        {"pieces", bbytes(repeatedBytes(20, 0x33))},
        {"name", bstring("fallback")},
        {"piece length", bint(524288)},
        {"name.utf-8", bstring("日本")},
        {"files", files},
    });
    const Bytes announceList = blist({
        blist({bstring("https://tracker.example/a"), bstring("https://tracker.example/a")}),
        blist({bstring("https://tracker.example/b")}),
    });
    return bdict({
        {"announce", bstring("https://tracker.example/fallback")},
        {"announce-list", announceList},
        {"private", bint(1)},
        {"url-list", blist({bstring("https://seed.example/file"), bstring("https://seed.example/file")})},
        {"info", info},
    });
}

std::string slashPath(std::string value)
{
    for (char &character : value) {
        if (character == '\\')
            character = '/';
    }
    return value;
}

std::string hexBytes(const Bytes &bytes)
{
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const auto byte : bytes)
        output << std::setw(2) << static_cast<unsigned>(byte);
    return output.str();
}

void traceGeometry(std::string_view label, const VirtualPieceMap &geometry)
{
    std::cout << label << ".meta=total:" << geometry.totalLength()
              << ",verificationLength:" << geometry.verificationPieceLength()
              << ",virtualLength:" << geometry.virtualPieceLength()
              << ",virtualized:" << (geometry.isVirtualized() ? "true" : "false") << '\n';
    for (const auto &piece : geometry.verificationPieces()) {
        std::cout << label << ".verification[" << piece.piece.value << "]=" << piece.offset
                  << ',' << piece.length << '\n';
    }
    for (const auto &piece : geometry.virtualPieces()) {
        std::cout << label << ".virtual[" << piece.piece.value << "]=" << piece.offset
                  << ',' << piece.length << ",verification:"
                  << piece.verificationIndex.value << ',' << piece.verificationOffset << '\n';
    }
    std::cout << label << ".wire-count=" << geometry.wireBlocks().size() << '\n';
    if (!geometry.wireBlocks().empty()) {
        const auto &last = geometry.wireBlocks().back();
        std::cout << label << ".wire-last=" << last.virtualPiece.value << ',' << last.offset
                  << ',' << last.length << '\n';
    }
}

Bytes privateTorrent(std::int64_t value)
{
    const Bytes info = bdict({
        {"private", bint(value)},
        {"name", bstring("private")},
        {"piece length", bint(16384)},
        {"pieces", bbytes(repeatedBytes(20, 0x66))},
        {"length", bint(1)},
    });
    return bdict({{"info", info}});
}

void traceCases()
{
    std::string error;
    const auto single = TorrentMetadata::parse(oneFileTorrent(700000, 262144), &error);
    const auto multi = TorrentMetadata::parse(multiFileTorrent(), &error);
    require(single.has_value() && multi.has_value(), "trace fixture rejected");
    std::cout << "K01-01 single=name:" << single->name() << ",length:" << single->length()
              << ",pieceLength:" << single->pieceLength()
              << ",lastPieceLength:" << single->lastPieceLength() << '\n';
    traceGeometry("K01-01 single.geometry", single->geometry());
    std::cout << "K01-01 multi=files:" << multi->files().size() << ",length:" << multi->length()
              << ",lastPieceLength:" << multi->lastPieceLength() << '\n';
    for (std::size_t index = 0; index < multi->files().size(); ++index) {
        const auto &file = multi->files()[index];
        std::cout << "K01-01 multi.file[" << index << "]=" << slashPath(file.path) << ','
                  << file.length << ',' << file.offset << '\n';
    }
    traceGeometry("K01-01 multi.geometry", multi->geometry());

    traceGeometry("K01-02 oneMiB", VirtualPieceMap::create(4 * 1024 * 1024, 1024 * 1024));
    traceGeometry("K01-02 fourMiB", VirtualPieceMap::create(5 * 1024 * 1024, 4 * 1024 * 1024));
    traceGeometry("K01-02 768KiB", VirtualPieceMap::create(1536 * 1024, 768 * 1024));

    const auto parsed = TorrentMetadata::parse(differentialTorrent(), &error);
    const auto privateZero = TorrentMetadata::parse(privateTorrent(0), &error);
    const auto privateOne = TorrentMetadata::parse(privateTorrent(1), &error);
    require(parsed.has_value() && privateZero.has_value() && privateOne.has_value(),
            "trace differential fixture rejected");
    std::cout << "K01-03 name=" << parsed->name() << '\n';
    std::cout << "K01-03 path=" << slashPath(parsed->files().front().path) << '\n';
    std::cout << "K01-03 announce=";
    for (std::size_t index = 0; index < parsed->announce().size(); ++index)
        std::cout << (index == 0 ? "" : "|") << parsed->announce()[index];
    std::cout << '\n';
    std::cout << "K01-03 url-list=";
    for (std::size_t index = 0; index < parsed->urlList().size(); ++index)
        std::cout << (index == 0 ? "" : "|") << parsed->urlList()[index];
    std::cout << '\n';
    std::cout << "K01-03 private.absent="
              << (parsed->privateValue().has_value() ? "present" : "absent")
              << ",isPrivate:" << (parsed->isPrivate() ? "true" : "false") << '\n';
    std::cout << "K01-03 private.zero="
              << (privateZero->privateValue().has_value() ? "present" : "absent")
              << ',' << (privateZero->privateValue().value_or(false) ? "true" : "false")
              << ",isPrivate:" << (privateZero->isPrivate() ? "true" : "false") << '\n';
    std::cout << "K01-03 private.one="
              << (privateOne->privateValue().has_value() ? "present" : "absent")
              << ',' << (privateOne->privateValue().value_or(false) ? "true" : "false")
              << ",isPrivate:" << (privateOne->isPrivate() ? "true" : "false") << '\n';
    std::cout << "K01-03 info-hash=" << parsed->infoHash() << '\n';
    std::cout << "K01-03 info-buffer-hex=" << hexBytes(parsed->infoBuffer()) << '\n';
}

void requireGeometryProperties(const VirtualPieceMap &geometry)
{
    std::uint64_t verificationTotal = 0;
    for (std::size_t index = 0; index < geometry.verificationPieces().size(); ++index) {
        const auto &piece = geometry.verificationPieces()[index];
        require(piece.piece.value == index, "verification indices are contiguous");
        require(piece.offset <= geometry.totalLength()
                    && piece.length <= geometry.totalLength() - piece.offset,
                "verification piece stays within total length");
        verificationTotal += piece.length;
    }
    require(verificationTotal == geometry.totalLength(),
            "verification pieces cover the total length exactly");

    std::vector<std::uint64_t> wireTotals(geometry.virtualPieces().size(), 0);
    std::uint64_t virtualTotal = 0;
    for (std::size_t index = 0; index < geometry.virtualPieces().size(); ++index) {
        const auto &piece = geometry.virtualPieces()[index];
        require(piece.piece.value == index, "virtual indices are contiguous");
        require(piece.verificationIndex.value < geometry.verificationPieces().size(),
                "virtual piece points to a verification piece");
        const auto &verification = geometry.verificationPieces()[piece.verificationIndex.value];
        require(piece.verificationOffset <= verification.length
                    && piece.length <= verification.length - piece.verificationOffset,
                "virtual piece stays within its verification piece");
        require(piece.offset <= geometry.totalLength()
                    && piece.length <= geometry.totalLength() - piece.offset,
                "virtual piece stays within total length");
        require(verification.offset + piece.verificationOffset == piece.offset,
                "virtual to verification mapping round-trips");
        virtualTotal += piece.length;
    }
    require(virtualTotal == geometry.totalLength(),
            "virtual pieces cover the total length exactly");

    for (const auto &block : geometry.wireBlocks()) {
        require(block.virtualPiece.value < geometry.virtualPieces().size(),
                "wire block points to a virtual piece");
        const auto &piece = geometry.virtualPieces()[block.virtualPiece.value];
        require(block.offset <= piece.length && block.length <= piece.length - block.offset,
                "wire block stays within its virtual piece");
        wireTotals[block.virtualPiece.value] += block.length;
    }
    for (std::size_t index = 0; index < geometry.virtualPieces().size(); ++index)
        require(wireTotals[index] == geometry.virtualPieces()[index].length,
                "wire blocks cover each virtual piece exactly");
}

void generatedGeometryCorpus()
{
    const std::vector<std::uint64_t> totals {
        0, 1, 16383, 16384, 16385, 524287, 524288, 524289, 786432, 1048575,
        1048576, 1048577, 1500000, 4194303, 4194304, 4194305, 5242880,
    };
    const std::vector<std::uint64_t> realLengths {
        16384, 524287, 524288, 524289, 768 * 1024, 1024 * 1024, 4 * 1024 * 1024,
    };
    for (const auto total : totals) {
        for (const auto realLength : realLengths)
            requireGeometryProperties(VirtualPieceMap::create(total, realLength));
    }
}

void caseK01_01()
{
    std::string error;
    const auto single = TorrentMetadata::parse(oneFileTorrent(700000, 262144), &error);
    require(single.has_value(), error.empty() ? "one-file torrent rejected" : error);
    require(single->name() == "single", "one-file name");
    require(single->length() == 700000, "one-file total length");
    require(single->pieceLength() == 262144, "one-file real piece length");
    require(single->lastPieceLength() == 175712, "one-file final short verification piece");
    require(single->files().size() == 1, "one-file count");
    require(single->files()[0].path == "single", "one-file path");
    require(single->files()[0].name == "single", "one-file basename");
    require(single->files()[0].offset == 0 && single->files()[0].length == 700000,
            "one-file offset and length");

    const auto &singleGeometry = single->geometry();
    require(!singleGeometry.isVirtualized(), "sub-512 KiB real pieces are not virtualized");
    require(singleGeometry.verificationPieces().size() == 3, "one-file verification count");
    require(singleGeometry.virtualPieces().size() == 3, "one-file virtual count");
    require(singleGeometry.wireBlocks().back().length == 11872,
            "final short wire block retains its remainder");

    error.clear();
    const auto multi = TorrentMetadata::parse(multiFileTorrent(), &error);
    require(multi.has_value(), error.empty() ? "multi-file torrent rejected" : error);
    require(multi->files().size() == 3, "multi-file order is retained");
    require(multi->files()[0].offset == 0 && multi->files()[0].length == 0,
            "zero-length file keeps its zero offset");
    require(multi->files()[1].offset == 0 && multi->files()[1].length == 600000,
            "second file offset and length");
    require(multi->files()[2].offset == 600000 && multi->files()[2].length == 900000,
            "third file offset and length");
    require(multi->files()[2].path == nativePath({"album", "日本.mkv"}),
            "UTF-8 path alternate is preferred");

    const auto &multiGeometry = multi->geometry();
    require(multiGeometry.isVirtualized(), "1 MiB real pieces are virtualized");
    require(multiGeometry.virtualPieces().size() == 3, "multi-file virtual piece count");
    require(multiGeometry.virtualPieces()[1].verificationIndex.value == 0
                && multiGeometry.virtualPieces()[1].verificationOffset == 524288,
            "file data crosses a real-to-virtual boundary without changing domains");
    require(multiGeometry.virtualPieces().back().length == 451424,
            "multi-file final short virtual piece");
    require(multiGeometry.wireBlocks().back().length == 451424 % 16384,
            "multi-file final short block");

    std::cout << "K01-01 PASS\n";
}

void caseK01_02()
{
    static_assert(!std::is_same<VerificationPieceCoordinate, VirtualPieceCoordinate>::value,
                  "verification and virtual coordinates must be distinct types");
    static_assert(!std::is_same<VirtualPieceCoordinate, WireBlockCoordinate>::value,
                  "virtual and wire coordinates must be distinct types");

    const auto oneMiB = VirtualPieceMap::create(4 * 1024 * 1024, 1024 * 1024);
    require(oneMiB.isVirtualized(), "1 MiB real pieces split");
    require(oneMiB.virtualPieceLength() == 524288, "1 MiB virtual length");
    require(oneMiB.verificationPieces().size() == 4, "1 MiB real-piece count");
    require(oneMiB.virtualPieces().size() == 8, "1 MiB virtual-piece count");
    require(oneMiB.virtualPieces()[0].verificationIndex.value == 0
                && oneMiB.virtualPieces()[1].verificationIndex.value == 0
                && oneMiB.virtualPieces()[1].verificationOffset == 524288,
            "1 MiB virtual pieces map to one real verification piece");

    const auto fourMiB = VirtualPieceMap::create(5 * 1024 * 1024, 4 * 1024 * 1024);
    require(fourMiB.isVirtualized(), "4 MiB real pieces split");
    require(fourMiB.virtualPieces().size() == 10, "4 MiB virtual count includes final real piece");
    require(fourMiB.virtualPieces()[8].verificationIndex.value == 1
                && fourMiB.virtualPieces()[8].verificationOffset == 0,
            "4 MiB final real piece starts a new verification domain");

    const auto sevenSixtyEightKiB = VirtualPieceMap::create(1536 * 1024, 768 * 1024);
    require(!sevenSixtyEightKiB.isVirtualized(), "768 KiB real pieces remain unchanged");
    require(sevenSixtyEightKiB.virtualPieceLength() == 768 * 1024,
            "unchanged real piece length is exposed as virtual length");
    require(sevenSixtyEightKiB.virtualPieces().size() == 2,
            "unchanged real-piece count is retained");
    generatedGeometryCorpus();

    std::cout << "K01-02 PASS\n";
}

void caseK01_03()
{
    std::string error;
    const auto parsed = TorrentMetadata::parse(differentialTorrent(), &error);
    require(parsed.has_value(), error.empty() ? "differential torrent rejected" : error);
    require(parsed->name() == "日本", "name.utf-8 wins over name");
    require(parsed->files()[0].path == nativePath({"日本", "clip%20one.mkv"}),
            "path remains percent-encoded because the oracle does not decode it");
    require(parsed->announce().size() == 3, "announce-list is flattened");
    require(parsed->announce()[0] == "https://tracker.example/a"
                && parsed->announce()[1] == "https://tracker.example/a"
                && parsed->announce()[2] == "https://tracker.example/b",
            "duplicate tracker entries retain parser order");
    require(parsed->urlList().size() == 2 && parsed->urlList()[0] == parsed->urlList()[1],
            "duplicate url-list entries retain parser order");
    // M303 27769: void 0 !== torrent.info.private && (result.private = !!torrent.info.private).
    require(!parsed->privateValue().has_value(), "absent info.private remains absent");
    require(!parsed->isPrivate(), "root private is ignored when info.private is absent");

    const auto privateTorrent = [](std::int64_t value) {
        const Bytes info = bdict({
            {"private", bint(value)},
            {"name", bstring("private")},
            {"piece length", bint(16384)},
            {"pieces", bbytes(repeatedBytes(20, 0x66))},
            {"length", bint(1)},
        });
        return bdict({{"info", info}});
    };
    const auto privateZero = TorrentMetadata::parse(privateTorrent(0), &error);
    require(privateZero.has_value() && privateZero->privateValue().has_value()
                && !*privateZero->privateValue(),
            "info.private=0 is present and coerces to false");
    require(privateZero.has_value() && !privateZero->isPrivate(),
            "info.private=0 reports false");
    const auto privateOne = TorrentMetadata::parse(privateTorrent(1), &error);
    require(privateOne.has_value() && privateOne->privateValue().has_value()
                && *privateOne->privateValue(),
            "info.private=1 is present and coerces to true");
    require(privateOne.has_value() && privateOne->isPrivate(),
            "info.private=1 reports true");
    require(parsed->infoBuffer() != differentialTorrent(),
            "info hash input is re-encoded from the decoded info object");
    require(parsed->infoHash() == "5e569fc6271ca36489701b1da88b6916200fbdb1",
            "info hash is SHA-1 of canonical bencode info bytes");

    const Bytes missingInfo = bdict({});
    error.clear();
    require(!TorrentMetadata::parse(missingInfo, &error), "missing info is rejected");
    require(error.find("info") != std::string::npos, "missing-info error names the field");

    const Bytes missingName = bdict({
        {"info", bdict({
            {"piece length", bint(16384)},
            {"pieces", bbytes(repeatedBytes(20, 0x44))},
        })},
    });
    error.clear();
    require(!TorrentMetadata::parse(missingName, &error), "missing name is rejected");
    require(error.find("name") != std::string::npos, "missing-name error names the field");

    const Bytes malformedLength = bdict({
        {"info", bdict({
            {"name", bstring("bad")},
            {"piece length", bint(16384)},
            {"pieces", bbytes(repeatedBytes(20, 0x55))},
            {"files", blist({bdict({
                {"length", bstring("not-a-number")},
                {"path", blist({bstring("bad.mkv")})},
            })})},
        })},
    });
    error.clear();
    require(!TorrentMetadata::parse(malformedLength, &error),
            "malformed numeric file length is rejected");
    require(error.find("length") != std::string::npos,
            "malformed-length error names the field");

    std::cout << "K01-03 PASS\n";
}

} // namespace

int main(int argc, char **argv)
{
    try {
        const std::string requested = argc > 1 ? argv[1] : "all";
        if (requested == "--trace") {
            traceCases();
            return 0;
        }
        if (requested == "all" || requested == "K01-01")
            caseK01_01();
        if (requested == "all" || requested == "K01-02")
            caseK01_02();
        if (requested == "all" || requested == "K01-03")
            caseK01_03();
        if (requested != "all" && requested != "K01-01" && requested != "K01-02"
            && requested != "K01-03")
            throw std::runtime_error("unknown K01 case");
    } catch (const std::exception &error) {
        std::cerr << "K01 FAIL: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
