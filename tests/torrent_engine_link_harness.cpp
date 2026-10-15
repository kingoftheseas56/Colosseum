// Phase 0 exit gate — tankorent engine import (one engine per job).
// PROVES: libtorrent links into Colosseum's MSVC build and a session constructs.
// Verdict rides the exit code; throws are caught -> nonzero (house law: a hung
// or throwing harness must still exit with a verdict).
// Spec: Brotherhood docs/superpowers/specs/2026-07-13-colosseum-tankorent-import-design.md
#include <libtorrent/session.hpp>
#include <libtorrent/version.hpp>
#include <cstdio>
#include <exception>

int main()
{
    try {
        lt::session session;
        std::printf("libtorrent %s — session constructed OK\n", LIBTORRENT_VERSION);
        return 0;
    } catch (const std::exception& e) {
        std::printf("FAIL: session construction threw: %s\n", e.what());
        return 1;
    } catch (...) {
        std::printf("FAIL: unknown throw during session construction\n");
        return 1;
    }
}
