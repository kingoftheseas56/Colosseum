// libFuzzer target for the update-manifest parser. Colosseum::Update::parseManifest
// turns manifest bytes into a Manifest (JSON parse + schema/version/hex-hash field
// extraction, incl. Version::parseCanonical/parseTag).
//
// Threat model note: the parameter is `verifiedUtf8` — in production these bytes are
// cryptographically verified (UpdateTrust) BEFORE parseManifest sees them, so this is
// a DEFENSE-IN-DEPTH target, not a directly remote-attacker-controlled one like the
// Watch Party decoder. Still worth fuzzing: a parser crash reachable by a
// compromised-but-signed manifest, or any gap in verification, would land here.
//
// Built with clang-cl -fsanitize=fuzzer-no-link,address + /MD libFuzzer + Qt6Core.

#include "update/UpdateManifest.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QString>

#include <cstddef>
#include <cstdint>

using namespace Colosseum::Update;

namespace {
QCoreApplication* g_app = nullptr;
} // namespace

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv) {
    g_app = new QCoreApplication(*argc, *argv);
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    const QByteArray bytes(reinterpret_cast<const char*>(data),
                           static_cast<qsizetype>(size));
    QString error;
    parseManifest(bytes, &error);
    return 0;
}
