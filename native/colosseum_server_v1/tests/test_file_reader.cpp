#include "server1/policy/FileReader.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using server1::policy::ByteBuffer;
using server1::policy::FileReadOptions;
using server1::policy::FileReader;
using server1::policy::FileReaderSource;
using server1::policy::Scheduler;
using server1::policy::TorrentFile;

[[noreturn]] void fail(const std::string &message)
{
    std::cerr << message << '\n';
    std::exit(1);
}

void expect(bool condition, const std::string &message)
{
    if (!condition) {
        fail(message);
    }
}

ByteBuffer bytes(std::initializer_list<std::uint8_t> values)
{
    return ByteBuffer(values);
}

ByteBuffer flatten(const std::vector<ByteBuffer> &chunks)
{
    ByteBuffer result;
    for (const auto &chunk : chunks) {
        result.insert(result.end(), chunk.begin(), chunk.end());
    }
    return result;
}

class ControlledPieceSource final : public FileReaderSource {
public:
    struct Pending final {
        std::uint64_t token = 0;
        std::size_t piece = 0;
        Completion completion;
        bool canceled = false;
    };

    bool hasPiece(std::size_t piece) const override
    {
        return available.count(piece) != 0;
    }

    void readPiece(std::uint64_t token, std::size_t piece, Completion completion) override
    {
        pending.push_back({token, piece, std::move(completion), false});
        ++reads;
    }

    bool cancelRead(std::uint64_t token) override
    {
        const auto it = std::find_if(pending.begin(), pending.end(),
                                     [token](const Pending &entry) { return entry.token == token; });
        if (it == pending.end() || it->canceled) {
            return false;
        }
        ++cancelCalls[token];
        it->canceled = true;
        canceled.insert(token);
        if (completeDuringCancel) {
            auto completion = std::move(it->completion);
            const auto piece = it->piece;
            pending.erase(it);
            completion(token, piece, pieces[piece], {});
        }
        return true;
    }

    void complete(std::size_t piece, std::string error = {})
    {
        const auto it = std::find_if(pending.begin(), pending.end(),
                                     [piece](const Pending &entry) {
                                         return entry.piece == piece && !entry.canceled;
                                     });
        if (it == pending.end()) {
            fail("no live pending read for piece " + std::to_string(piece));
        }
        auto completion = std::move(it->completion);
        const auto token = it->token;
        pending.erase(it);
        completion(token, piece, pieces[piece], std::move(error));
    }

    void completeCanceled(std::uint64_t token)
    {
        const auto it = std::find_if(pending.begin(), pending.end(),
                                     [token](const Pending &entry) { return entry.token == token; });
        if (it == pending.end()) {
            fail("no canceled read for token");
        }
        auto completion = std::move(it->completion);
        const auto piece = it->piece;
        pending.erase(it);
        completion(token, piece, pieces[piece], {});
    }

    std::map<std::size_t, ByteBuffer> pieces;
    std::set<std::size_t> available;
    std::vector<Pending> pending;
    std::set<std::uint64_t> canceled;
    std::map<std::uint64_t, std::size_t> cancelCalls;
    std::size_t reads = 0;
    bool completeDuringCancel = false;
};

FileReadOptions options(std::size_t start,
                        std::optional<std::size_t> end,
                        std::size_t bufferBytes = 0,
                        std::uint64_t generation = 1)
{
    FileReadOptions result;
    result.start = start;
    result.end = end;
    result.bufferBytes = bufferBytes;
    result.generation = generation;
    return result;
}

void caseK0801()
{
    ControlledPieceSource source;
    source.pieces = {{0, bytes({0, 1, 2, 3})}, {1, bytes({4, 5, 6, 7})}};
    source.available = {0, 1};

    Scheduler oneByteScheduler(2);
    FileReader oneByte(oneByteScheduler, source, TorrentFile{"a", "a", 6, 1}, 4,
                       options(0, 0));
    expect(oneByte.length() == 1 && oneByte.startPiece() == 0 && oneByte.endPiece() == 0,
           "K08-01 inclusive end=0 is a one-byte range");
    oneByte.request(1);
    source.complete(0);
    expect(flatten(oneByte.takeData()) == bytes({1}) && oneByte.eof(),
           "K08-01 first byte is clipped from the file offset and reaches EOF");

    Scheduler crossingScheduler(2);
    FileReader crossing(crossingScheduler, source, TorrentFile{"a", "a", 6, 1}, 4,
                        options(0, 5));
    crossing.request(6);
    expect(source.pending.size() == 2, "K08-01 two-read queue admits two pieces");
    source.complete(1);
    expect(crossing.takeData().empty(), "K08-01 out-of-order disk completion cannot reorder bytes");
    source.complete(0);
    expect(flatten(crossing.takeData()) == bytes({1, 2, 3, 4, 5, 6}) && crossing.eof(),
           "K08-01 a range crossing pieces is clipped and delivered in order");

    Scheduler exactScheduler(2);
    FileReader exact(exactScheduler, source, TorrentFile{"b", "b", 4, 4}, 4,
                     options(0, 3));
    expect(exact.startPiece() == 1 && exact.endPiece() == 1,
           "K08-01 exact file boundary maps to one verification piece");

    Scheduler sharedScheduler(1);
    FileReader left(sharedScheduler, source, TorrentFile{"left", "left", 2, 0}, 4,
                    options(0, 1, 0, 4));
    FileReader right(sharedScheduler, source, TorrentFile{"right", "right", 2, 2}, 4,
                     options(0, 1, 0, 5));
    left.request(2);
    right.request(2);
    source.complete(0);
    source.complete(0);
    expect(flatten(left.takeData()) == bytes({0, 1})
               && flatten(right.takeData()) == bytes({2, 3}),
           "K08-01 two files sharing one verification piece receive only owned bytes");

    bool rejected = false;
    try {
        FileReader invalid(sharedScheduler, source, TorrentFile{"x", "x", 1, 0}, 4,
                           options(1, std::nullopt));
    } catch (const std::out_of_range &) {
        rejected = true;
    }
    expect(rejected, "K08-01 start at EOF is rejected");
}

void caseK0802()
{
    ControlledPieceSource source;
    source.pieces = {{0, bytes({0, 1, 2, 3})}, {1, bytes({4, 5, 6, 7})}};
    source.available = {0, 1};
    Scheduler scheduler(2);

    FileReader slow(scheduler, source, TorrentFile{"slow", "slow", 8, 0}, 4,
                    options(0, 7, 8, 10));
    slow.request(1);
    expect(source.pending.size() == 1 && source.pending[0].piece == 0,
           "K08-02 slow consumer demand schedules one piece, not the full window");
    source.complete(0);
    expect(source.pending.empty(), "K08-02 unread buffered data applies backpressure");
    expect(flatten(slow.takeData()) == bytes({0, 1, 2, 3}),
           "K08-02 consumer drains the demanded piece");

    FileReader peer(scheduler, source, TorrentFile{"peer", "peer", 4, 4}, 4,
                    options(0, 3, 0, 11));
    peer.request(4);
    expect(slow.lockedPieces().empty() && peer.lockedPieces() == std::set<std::size_t>{1},
           "K08-02 simultaneous readers keep independent piece locks");
    source.complete(1);
    expect(flatten(peer.takeData()) == bytes({4, 5, 6, 7}),
           "K08-02 second reader receives its own bytes");

    std::uint64_t staleToken = 0;
    {
        FileReader oldRead(scheduler, source, TorrentFile{"seek", "seek", 4, 0}, 4,
                           options(0, 3, 0, 20));
        oldRead.request(4);
        staleToken = source.pending.back().token;
        oldRead.close();
        expect(source.canceled.count(staleToken) == 1,
               "K08-02 replacement cancels the old generation read");
    }
    FileReader replacement(scheduler, source, TorrentFile{"seek", "seek", 4, 0}, 4,
                           options(0, 3, 0, 21));
    replacement.request(4);
    source.completeCanceled(staleToken);
    expect(replacement.takeData().empty(),
           "K08-02 a late old-generation completion cannot reach the replacement");
    source.complete(0);
    expect(flatten(replacement.takeData()) == bytes({0, 1, 2, 3}),
           "K08-02 replacement receives only its own completion");

    ControlledPieceSource waitingSource;
    waitingSource.pieces = {{0, bytes({9, 9, 9, 9})}};
    Scheduler waitingScheduler(1);
    std::size_t refreshes = 0;
    FileReader waiting(waitingScheduler, waitingSource, TorrentFile{"wait", "wait", 4, 0}, 4,
                       options(0, 3, 0, 30), [&refreshes] { ++refreshes; });
    waiting.request(4);
    expect(waitingSource.pending.empty() && refreshes == 1 && waitingScheduler.isCritical(0),
           "K08-02 missing piece waits, marks critical, and refreshes once");
    waiting.notifyPiece(0);
    expect(waitingSource.pending.empty() && refreshes == 1,
           "K08-02 false notification does not spin refresh");
    waitingSource.available.insert(0);
    waiting.notifyPiece(0);
    expect(waitingSource.pending.size() == 1,
           "K08-02 availability notification resumes the waiting read");
    waitingSource.complete(0);
    expect(waiting.eof() && !waiting.hasActiveSelection(),
           "K08-02 EOF deselects the reader");

    ControlledPieceSource destroyedSource;
    destroyedSource.pieces = {{0, bytes({1, 1, 1, 1})}};
    destroyedSource.available = {0};
    Scheduler destroyedScheduler(1);
    std::uint64_t destroyedToken = 0;
    {
        auto destroyed = std::make_unique<FileReader>(
            destroyedScheduler, destroyedSource, TorrentFile{"gone", "gone", 4, 0}, 4,
            options(0, 3, 0, 40));
        destroyed->request(4);
        destroyedToken = destroyedSource.pending[0].token;
    }
    expect(destroyedSource.canceled.count(destroyedToken) == 1,
           "K08-02 destruction cancels owned disk work");
    destroyedSource.completeCanceled(destroyedToken);
    expect(destroyedScheduler.selections().empty(),
           "K08-02 late completion after destruction is inert");
}

void caseK0803()
{
    constexpr std::size_t pieceLength = 512 * 1024;
    constexpr std::size_t bufferBytes = 15 * 1024 * 1024;
    ControlledPieceSource source;
    Scheduler scheduler(64);
    FileReader reader(scheduler, source,
                      TorrentFile{"video", "video", static_cast<std::int64_t>(pieceLength * 64), 0},
                      pieceLength, options(0, pieceLength * 64 - 1, bufferBytes, 50));
    expect(reader.bufferPieces() == 30, "K08-03 15 MiB window floors to 30 pieces");
    expect(reader.criticalWidth() == 2, "K08-03 1 MiB critical span floors to two pieces");
    const auto selection = scheduler.find(reader.selectionId());
    expect(selection && selection->readFrom == 0 && selection->selectTo == 30,
           "K08-03 scheduler window starts at the read head and uses the source upper bound");
    reader.request(pieceLength);
    expect(scheduler.isCritical(0) && scheduler.isCritical(1),
           "K08-03 missing read marks the two-piece critical span");
}

void caseK08F2()
{
    // Break caught: empty external failures must terminalize with the exact fallback,
    // and a later failure must not replace the first terminal reason.
    {
        ControlledPieceSource source;
        Scheduler scheduler(1);
        FileReader reader(scheduler, source, TorrentFile{"fallback", "fallback", 4, 0}, 4,
                          options(0, 3, 0, 60));
        reader.fail({});
        reader.fail("later failure");
        expect(reader.closed() && !reader.eof() && !reader.hasActiveSelection(),
               "K08-F2 empty failure closes and detaches the active reader");
        expect(reader.takeError() == std::optional<std::string>{"file reader failed"}
                   && !reader.takeError(),
               "K08-F2 empty failure uses the exact once-readable fallback");
    }

    // Break caught: EOF and explicit close are terminal boundaries which failure
    // injection cannot rewrite into errors.
    {
        ControlledPieceSource eofSource;
        eofSource.pieces = {{0, bytes({1, 2, 3, 4})}};
        eofSource.available = {0};
        Scheduler eofScheduler(1);
        FileReader eofReader(eofScheduler, eofSource, TorrentFile{"eof", "eof", 4, 0}, 4,
                             options(0, 3, 0, 61));
        eofReader.request(4);
        eofSource.complete(0);
        eofReader.fail("after eof");
        expect(eofReader.eof() && !eofReader.takeError(),
               "K08-F2 failure after EOF is inert");

        ControlledPieceSource closedSource;
        Scheduler closedScheduler(1);
        FileReader closedReader(closedScheduler, closedSource,
                                TorrentFile{"closed", "closed", 4, 0}, 4,
                                options(0, 3, 0, 62));
        closedReader.close();
        closedReader.fail("after close");
        expect(closedReader.closed() && !closedReader.takeError(),
               "K08-F2 failure after explicit close is inert");
    }

    // Break caught: external failure preserves bytes already ready, cancels each
    // still-active token once, and makes late completions inert.
    {
        ControlledPieceSource source;
        source.pieces = {{0, bytes({0, 1, 2, 3})}, {1, bytes({4, 5, 6, 7})}};
        source.available = {0, 1};
        Scheduler scheduler(2);
        FileReader reader(scheduler, source, TorrentFile{"ready", "ready", 8, 0}, 4,
                          options(0, 7, 0, 63));
        reader.request(8);
        const auto secondToken = source.pending[1].token;
        source.complete(0);
        reader.fail("transport failed");
        reader.fail("replacement");
        expect(flatten(reader.takeData()) == bytes({0, 1, 2, 3}),
               "K08-F2 failure preserves the already-ready takeData queue");
        expect(source.cancelCalls[secondToken] == 1 && reader.lockedPieces().empty(),
               "K08-F2 failure cancels each detached active token exactly once");
        source.completeCanceled(secondToken);
        expect(reader.takeData().empty()
                   && reader.takeError() == std::optional<std::string>{"transport failed"},
               "K08-F2 late completion is inert and the first reason wins");
    }

    // Break caught: out-of-order completions and waiting-piece state must not
    // survive failure and later become consumer-visible work.
    {
        ControlledPieceSource source;
        source.pieces = {{0, bytes({0, 1, 2, 3})}, {1, bytes({4, 5, 6, 7})}};
        source.available = {0, 1};
        Scheduler scheduler(2);
        FileReader reader(scheduler, source, TorrentFile{"ordered", "ordered", 8, 0}, 4,
                          options(0, 7, 0, 64));
        reader.request(8);
        const auto firstToken = source.pending[0].token;
        source.complete(1);
        reader.fail("stop ordered delivery");
        source.completeCanceled(firstToken);
        expect(reader.takeData().empty() && reader.pendingReads() == 0,
               "K08-F2 failure discards out-of-order not-ready completion state");

        ControlledPieceSource waitingSource;
        Scheduler waitingScheduler(1);
        std::size_t refreshes = 0;
        FileReader waiting(waitingScheduler, waitingSource,
                           TorrentFile{"waiting", "waiting", 4, 0}, 4,
                           options(0, 3, 0, 65), [&refreshes] { ++refreshes; });
        waiting.request(4);
        waiting.fail("stop waiting");
        waitingSource.available.insert(0);
        waiting.notifyPiece(0);
        expect(refreshes == 1 && waitingSource.pending.empty(),
               "K08-F2 failure detaches waiting-piece state");
    }

    // Break caught: a source which completes synchronously from cancelRead must
    // not mutate the detached active-read ledger or deliver bytes reentrantly.
    {
        ControlledPieceSource source;
        source.pieces = {{0, bytes({8, 8, 8, 8})}};
        source.available = {0};
        source.completeDuringCancel = true;
        Scheduler scheduler(1);
        FileReader reader(scheduler, source, TorrentFile{"reentrant", "reentrant", 4, 0}, 4,
                          options(0, 3, 0, 66));
        reader.request(4);
        const auto token = source.pending[0].token;
        reader.fail("reentrant cancel");
        expect(source.cancelCalls[token] == 1 && reader.pendingReads() == 0
                   && reader.takeData().empty(),
               "K08-F2 synchronous cancel completion is inert after ownership detaches");
    }

    // Break caught: source-read errors use the same terminal transition, while a
    // failed reader cannot cancel or deselect a sibling sharing both dependencies.
    {
        ControlledPieceSource source;
        source.pieces = {{0, bytes({1, 1, 1, 1})}, {1, bytes({2, 2, 2, 2})}};
        source.available = {0, 1};
        Scheduler scheduler(2);
        FileReader left(scheduler, source, TorrentFile{"left-fail", "left-fail", 4, 0}, 4,
                        options(0, 3, 0, 67));
        FileReader right(scheduler, source, TorrentFile{"right-live", "right-live", 4, 4}, 4,
                         options(0, 3, 0, 68));
        left.request(4);
        right.request(4);
        const auto leftToken = source.pending[0].token;
        const auto rightToken = source.pending[1].token;
        expect(leftToken != rightToken,
               "K08-F2 sibling readers sharing a source require distinct live tokens");
        left.fail("left only");
        expect(source.cancelCalls[leftToken] == 1 && source.cancelCalls[rightToken] == 0
                   && right.hasActiveSelection(),
               "K08-F2 external failure cannot cancel or deselect a sibling reader");
        source.complete(1);
        expect(flatten(right.takeData()) == bytes({2, 2, 2, 2}) && right.eof(),
               "K08-F2 sibling reader continues to ordered EOF");

        ControlledPieceSource errorSource;
        errorSource.pieces = {{0, bytes({3, 3, 3, 3})}, {1, bytes({4, 4, 4, 4})}};
        errorSource.available = {0, 1};
        Scheduler errorScheduler(2);
        FileReader sourceError(errorScheduler, errorSource,
                               TorrentFile{"source-error", "source-error", 8, 0}, 4,
                               options(0, 7, 0, 69));
        sourceError.request(8);
        const auto survivingToken = errorSource.pending[1].token;
        errorSource.complete(0, "disk failed");
        expect(sourceError.closed() && errorSource.cancelCalls[survivingToken] == 1
                   && sourceError.takeError() == std::optional<std::string>{"disk failed"},
               "K08-F2 source-read error uses the shared terminal transition");
        errorSource.completeCanceled(survivingToken);
        expect(sourceError.takeData().empty(),
               "K08-F2 source-error cancellation completion stays inert");
    }
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 2) {
        fail("expected one case id");
    }
    const std::string id = argv[1];
    if (id == "K08-01") {
        caseK0801();
    } else if (id == "K08-02") {
        caseK0802();
    } else if (id == "K08-03") {
        caseK0803();
    } else if (id == "K08-F2") {
        caseK08F2();
    } else {
        fail("unknown case id");
    }
    std::cout << id << " PASS\n";
    return 0;
}
