// libFuzzer target for the vendored miniz ZIP reader — the actual untrusted-input
// parser behind CBZ comic archives (native/third_party/miniz/miniz.c, MZ_VERSION
// 11.0.2). MangaTankoban::CbzArchive is a thin path-based Qt wrapper over exactly
// these calls; this target drives the same operation sequence in-memory so no Qt
// (and thus no /MD vs /MT CRT conflict with the static libFuzzer runtime) is
// involved, and no temp file is written per input:
//
//   mz_zip_reader_init_mem      -> parse End-Of-Central-Directory + central dir
//   mz_zip_reader_file_stat     -> per-entry central-directory-header parse
//   mz_zip_reader_extract_to_heap -> local-header parse + inflate (deflate/store)
//
// Built with clang-cl -fsanitize=fuzzer,address. A heap/stack overflow or UAF in
// miniz's parsing of a malformed archive surfaces as an ASan report. Entries that
// merely CLAIM a very large uncompressed size are skipped for extraction so pure
// decompression-bomb OOM does not dominate the campaign; the header parse for
// those entries still runs on every input.

#include "third_party/miniz/miniz.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {
// Skip extraction (not stat) above this claimed output size, so the run explores
// parser memory-safety rather than drowning in alloc-what-the-header-claims OOM.
constexpr mz_uint64 kMaxExtractBytes = 64ull * 1024ull * 1024ull;
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));

    if (!mz_zip_reader_init_mem(&zip, data, size, 0))
        return 0; // not a parseable zip container; nothing more to exercise

    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, i, &stat))
            continue;
        if (stat.m_is_directory)
            continue;
        if (stat.m_uncomp_size > kMaxExtractBytes)
            continue;
        size_t out_size = 0;
        void* p = mz_zip_reader_extract_to_heap(&zip, i, &out_size, 0);
        if (p)
            mz_free(p);
    }

    mz_zip_reader_end(&zip);
    return 0;
}
