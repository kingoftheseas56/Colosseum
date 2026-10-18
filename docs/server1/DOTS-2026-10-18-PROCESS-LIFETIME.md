# Media-process lifetime repair

Continues the Server 1 branch after the Linux build/storage checkpoints. No app
production integration or release is claimed.

## Reproduced failures

- The unchanged Release M00 discovery executable crashed with SIGSEGV on four
  consecutive reruns. The source removed/destroyed its QProcess during that
  process's FailedToStart signal. ASan variants did not reproduce; no stack-trace
  confirmation is claimed. The Release failure and ASan non-reproductions remain
  separate evidence.
- The graceful-stop fixture sent termination after QProcess::started, before
  Python had necessarily installed its ignore-SIGTERM handler. The corrected
  fixture emits and waits for an explicit handler-ready marker; it preserves
  the forced-kill assertion and adds no readiness sleep.
- With that fixture corrected, the unchanged implementation failed its existing
  no-orphan-grandchild assertion. Leader exit removed the owner before the grace
  timer could clean up descendants.

## Repair

- Defer FailedToStart terminal handling and queue finished/output/user-started
  callbacks so user actions cannot destroy QProcess in its own signal stack.
- Copy user callbacks before invocation; claim terminal delivery before draining
  final output and guard against owner deletion/nested event-loop reentrancy.
- Check Unix process-group creation, capture the owned group while the child is
  alive, and clean up inherited-group descendants on requested stop/destruction,
  including when the leader has already exited. Consume the group identity before
  external callbacks. This does not contain descendants that deliberately leave
  their group.
- Add owner-deletion/callback-capture and nested-event-loop regressions.

## Executed qualification

```sh
cmake --build build-server1-linux --target server1_m00_process_tests -j2
ctest --test-dir build-server1-linux -R 'M00-' \
  --repeat until-fail:20 --output-on-failure --timeout 30
```

All three M00 test invocations pass, including real subprocess execution and the
existing unrelated-process-survival assertion. Twenty repeats each: **60 PASS,
0 FAIL, 0 skipped**. GCC 14.2.0 Release, Qt 6.11.1, native Linux.

Executable SHA256:
`c8ae345b740920cc18c7dd0665a347f300defe7b531d470b655591aefbf4e5e1`

A dedicated combined witness for leader-exit-before-owner-deletion is still a
coverage gap; existing tests exercise those components separately. Windows is
NOT RUN. Passing process components do not qualify the assembled media server.
