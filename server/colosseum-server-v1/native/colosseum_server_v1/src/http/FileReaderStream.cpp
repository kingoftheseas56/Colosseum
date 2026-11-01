#include "server1/http/FileReaderStream.h"
#include "server1/http/HttpContract.h"

#include <algorithm>
#include <cstring>
#include <deque>

namespace server1::http {
namespace {
struct ReaderStream final {
    std::shared_ptr<void> dependencies;
    std::shared_ptr<policy::FileReader> reader;
    std::deque<policy::ByteBuffer> chunks;
    std::size_t offset = 0;
    bool demandOutstanding = false;
    bool failed = false;
    void *wake = nullptr;

    ~ReaderStream()
    {
        reader->setReadable({});
        reader->close();
        reader.reset();
        server1_http_stream_wake_destroy(wake);
    }

    void collect()
    {
        auto ready = reader->takeData();
        if (!ready.empty()) demandOutstanding = false;
        for (auto &chunk : ready)
            if (!chunk.empty()) chunks.push_back(std::move(chunk));
        if (reader->takeError()) failed = true;
    }

    std::ptrdiff_t read(void *buffer, std::size_t capacity)
    {
        if (capacity == 0) return SERVER1_HTTP_WOULD_BLOCK;
        collect();
        if (chunks.empty() && !failed && !reader->eof() && !reader->closed()
            && !demandOutstanding) {
            // A blocked socket may ask again before data arrives. Never amplify
            // the reader's demand on those retries.
            demandOutstanding = true;
            reader->request(capacity);
            collect();
        }
        if (!chunks.empty()) {
            auto &chunk = chunks.front();
            const auto count = std::min(capacity, chunk.size() - offset);
            std::memcpy(buffer, chunk.data() + offset, count);
            offset += count;
            if (offset == chunk.size()) { chunks.pop_front(); offset = 0; }
            return static_cast<std::ptrdiff_t>(count);
        }
        if (failed || (reader->closed() && !reader->eof())) return -1;
        return reader->eof() ? 0 : SERVER1_HTTP_WOULD_BLOCK;
    }
};
}

bool attachFileReader(void *response, std::shared_ptr<policy::FileReader> reader,
                      std::shared_ptr<void> dependencies)
{
    if (!response || !reader || !dependencies) return false;
    auto stream = std::make_unique<ReaderStream>();
    stream->dependencies = std::move(dependencies);
    stream->reader = std::move(reader);
    if (!server1_http_response_set_async_stream(response,
            +[](void *context, void *buffer, std::size_t capacity) -> std::ptrdiff_t {
                return static_cast<ReaderStream *>(context)->read(buffer, capacity);
            }, +[](void *context) { delete static_cast<ReaderStream *>(context); },
            stream.get(), stream->reader->length())) return false;
    auto *owned = stream.release();
    owned->wake = server1_http_response_stream_wake_handle(response);
    owned->reader->setReadable([wake = owned->wake] {
        server1_http_stream_wake_notify(wake);
    });
    return true;
}
}
