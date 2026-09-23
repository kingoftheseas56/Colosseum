import re
base=r'C:\b\_k10diag2\native\colosseum_server_v1'
p=base+r'\src\transport\LibTorrent2Adapter.cpp'
s=open(p,newline='').read()
old='static_cast<int>(lt::alert_category::error | lt::alert_category::status));'
assert s.count(old)==1
s=s.replace(old,'static_cast<int>(lt::alert_category::error | lt::alert_category::status | lt::alert_category::peer | lt::alert_category::connect));')
old='        for (const auto *alert : alerts) {\n            if (const auto *added'
assert s.count(old)==1
s=s.replace(old,'        for (const auto *alert : alerts) {\n            std::fprintf(stderr, "DIAG %lld %s: %s'+chr(92)+'n", (long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(), alert->what(), alert->message().c_str());\n            if (const auto *added')
s='#include <cstdio>\n#include <chrono>\n'+s
open(p,'w',newline='').write(s)
t=base+r'\tests\test_native_transport.cpp'
s=open(t,newline='').read()
old='    expect(transport->poll().empty(), "K10-E real libtorrent metadata repeated");'
assert s.count(old)==1
s=s.replace(old,'    { const auto late = transport->poll(); for (const auto &item : late) std::cerr << "DIAG late observation index=" << item.index() << (std::holds_alternative<PeerObservation>(item) ? " PeerObservation" : std::holds_alternative<AvailablePiecesObservation>(item) ? " AvailablePieces" : std::holds_alternative<MetadataReadyObservation>(item) ? " MetadataReady" : std::holds_alternative<FailureObservation>(item) ? (" Failure " + std::get<FailureObservation>(item).error) : std::string(" other")) << "\n";\n      expect(late.empty(), "K10-E real libtorrent metadata repeated"); }')
open(t,'w',newline='').write(s)
