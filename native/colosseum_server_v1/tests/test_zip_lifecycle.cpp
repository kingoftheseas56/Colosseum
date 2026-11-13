#include "server1/archive/ZipSource.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSet>
#include <QTimer>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace server1::archive;
namespace {
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void until(const std::function<bool()> &ready) {
    QElapsedTimer timer; timer.start();
    while (!ready() && timer.elapsed() < 1000) QCoreApplication::processEvents();
    check(ready(), "async completion timed out");
}
struct Source {
    QObject owner;
    QByteArray bytes = "0123456789";
    QSet<quint64> cancelled;
    quint64 sequence = 0;
    int reads = 0;
    ZipSource::Read read() {
        return [this](qint64 offset, qint64 count, RemoteByteSource::Completion completion) {
            ++reads;
            const auto token = ++sequence;
            QTimer::singleShot(0, &owner, [=]() {
                if (!cancelled.contains(token)) completion({bytes.mid(offset, count), {}});
            });
            return token;
        };
    }
    ZipSource::Cancel cancel() { return [this](quint64 token) { cancelled.insert(token); }; }
};
ZipEntry stored() { ZipEntry entry; entry.size = entry.compressedSize = 10; entry.dataOffset = 0; return entry; }
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        {
            Source source;
            ZipEntryReader reader(stored(), source.read(), source.cancel());
            bool first = false, second = false;
            reader.pull(3, [&](ZipChunk chunk) { check(chunk.bytes == "012" && chunk.error.isEmpty(), "first bounded pull"); first = true; });
            reader.pull(3, [&](ZipChunk chunk) { check(chunk.error == "A ZIP pull is already pending", "duplicate pull rejected"); second = true; });
            until([&] { return first && second; });
            check(source.reads == 1, "duplicate pull performed IO");
            bool next = false;
            reader.pull(7, [&](ZipChunk chunk) { check(chunk.bytes == "3456789" && chunk.eof, "stream offset preserved"); next = true; });
            until([&] { return next; });
        }
        for (qint64 count : {qint64(0), qint64(-1), qint64(1024 * 1024 + 1)}) {
            Source source;
            ZipEntryReader reader(stored(), source.read(), source.cancel());
            bool done = false;
            reader.pull(count, [&](ZipChunk chunk) { check(!chunk.error.isEmpty() && chunk.eof, "invalid pull size"); done = true; });
            until([&] { return done; });
            check(source.reads == 0, "invalid pull performed IO");
        }
        {
            Source source;
            ZipEntryReader reader(stored(), source.read(), source.cancel());
            reader.pull(3, [&](ZipChunk) { check(false, "cancelled callback delivered"); });
            reader.cancel();
            bool done = false;
            reader.pull(3, [&](ZipChunk chunk) { check(chunk.eof && chunk.bytes.isEmpty(), "cancelled reader EOF"); done = true; });
            until([&] { return done; });
            check(source.reads == 0, "cancelled deferred pull performed IO");
        }
        {
            Source source;
            auto reader = std::make_unique<ZipEntryReader>(stored(), source.read(), source.cancel());
            reader->pull(3, [&](ZipChunk) { check(false, "destroyed reader callback delivered"); });
            reader.reset();
            QCoreApplication::processEvents();
            check(source.reads == 0, "destroyed deferred pull performed IO");
        }
        for (int kind = 0; kind < 4; ++kind) {
            Source source;
            auto entry = stored();
            if (kind == 0) entry.flags = 1;
            if (kind == 1) entry.dataOffset = std::numeric_limits<qint64>::max();
            if (kind == 2) source.bytes = "short";
            if (kind == 3) entry.method = 8; // not a raw DEFLATE stream
            ZipEntryReader reader(entry, source.read(), source.cancel());
            bool done = false;
            reader.pull(10, [&](ZipChunk chunk) { check(!chunk.error.isEmpty() && chunk.eof, "invalid entry rejected"); done = true; });
            until([&] { return done; });
            if (kind < 2) check(source.reads == 0, "invalid entry performed IO");
        }
        {
            auto entry=stored();entry.dataOffset=std::numeric_limits<qint64>::max();
            check(zipRangePlan(entry,"GET","").status==500,"ZIP range overflow rejected");
        }
        {
            Source source;
            ZipSource directory(source.bytes.size(), source.read(), source.cancel());
            directory.open([&](ZipDirectory) { check(false, "cancelled metadata callback delivered"); });
            directory.cancel();
            QCoreApplication::processEvents();
            check(source.cancelled.size() == 1, "metadata read not cancelled");
        }
        std::cout << "PASS bounded ZIP pull, cancellation, teardown and invalid entry lifecycle\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << "FAIL " << error.what() << '\n'; return 1; }
}
