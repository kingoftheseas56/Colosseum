# Linux component and real-wire checkpoint

## Result

**81 CTest invocations executed, 81 PASS, 0 FAIL, 0 skipped** on the recorded
fixed-interface Linux profile. This is the complete currently registered suite,
including the twelve previously unavailable PowerShell wire drivers. It is not
the unimplemented full Server 1 parity matrix.

- [Complete CTest output](evidence/dots-2026-10-18/ctest-linux.txt)
- [20-case transport run](evidence/dots-2026-10-18/ctest-wire.txt)
- [Retained earlier failures](evidence/dots-2026-10-18/retained-failures.txt)
- [Source-file and executable identities](evidence/dots-2026-10-18/qualification.json)

The existing host still reports `streaming-ready=NO`. K12/H01/H02 composition,
remaining media/network/archive services, embedding, player operation and Windows
qualification remain open. Do not treat the skeleton-host test as a working-server
test or count the current suite as complete Stremio parity.

## Actual transport evidence

The native adapter exchanged infohash/magnet metadata with a local libtorrent
seeder. Controlled Python loopback peers witnessed exact block requests,
sequential requests, HAVE ordering, peer reuse, failure/cancel draining,
thread-affinity guards, disconnect lifecycle, pause/generation handling and upload
framing/caps/teardown. The upload runner executed all four of its subcases.
Inbound uTP qualification retained its positive TCP control and three UDP SYN
negative probes. Original wire assertions were not relaxed.

## Supported environment qualification

TCP/UDP IPv4/IPv6 sockets work here. NETLINK socket creation is denied with EPERM;
the default libtorrent IP-change notifier aborts session startup. The failed
default attempt is retained locally. No security setting or network policy changed.

`TorrentOpenRequest.monitorNetworkChanges` defaults to true. The explicit
`SERVER1_TEST_FIXED_INTERFACE=1` test profile sets it false for controlled fixtures
(any presence of that test variable, including `0`, enables this profile),
using libtorrent's documented `enable_ip_notifier` setting. This disables optional
interface-change observation; it does not claim that functionality works here.
Normal production defaults remain unchanged. The legacy factory's two-argument
entry point keeps its original default.

Official PowerShell archive:
https://github.com/PowerShell/PowerShell/releases/download/v7.6.7/powershell-7.6.7-linux-x64.tar.gz

Verified SHA256:
`6896f9e3a6b0a354bf470835a96da76d2f801cd50dcf047ad7c4563bf93bf431`

PowerShell's initial startup failed because its default cache was read-only.
Process-scoped XDG_CACHE_HOME/XDG_CONFIG_HOME/XDG_DATA_HOME pointing to disposable
task storage resolved that setup issue. No user profile or system policy changed.

## Reproduce

Build as described in [LINUX-BUILD.md](LINUX-BUILD.md), put pwsh and Python on PATH,
and use writable disposable XDG locations when required by the host:

```sh
SERVER1_TEST_FIXED_INTERFACE=1 POWERSHELL_TELEMETRY_OPTOUT=1 \
  ctest --test-dir build-server1-linux --output-on-failure --timeout 90
```

The wrapper timeout does not replace the runners' internal readiness/time bounds.
This run completed in 54.59 seconds. The older 65-case/12-NOT-RUN attempt and
default-notifier failures remain distinct from this result.

## Publication and CI

All work stays on feature/colosseum-server-1.0. Existing branch push triggers
unexpectedly started desktop-ci/code-quality checks for the first checkpoints.
They are not proof for this dedicated native server suite. No workflow was edited
or manually dispatched. Further checkpoint messages use `[skip ci]` to avoid
repeated unrelated builds. Existing runs are tracked separately; no merge/release
is authorized by this checkpoint.
