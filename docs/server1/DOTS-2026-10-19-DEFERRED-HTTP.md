# Deferred HTTP response lifecycle

This checkpoint adds weak deferred response handles for asynchronous metadata and
upstream response acquisition. Handles follow response moves, expire on retirement,
and cannot complete twice. Connections emit no headers until completion and keep
pipelined requests ordered. Disconnect/stop/destroy retire deferred ownership.

H00-11 covers moves, real TCP pipelining, disconnect, stop, destroy, reentrant
completion/lifetime callbacks, inline completion, completion then queued shutdown,
and HEAD Content-Length. HTTP framing suppresses duplicate Content-Length,
Transfer-Encoding and Connection fields; 206 and 416 have correct reason phrases.
Header injection validation requested during integration is a separate pending check.

Parent independently rebuilt and ran all H00-01..11 in both ordinary and
AddressSanitizer/UndefinedBehaviorSanitizer builds on 2026-10-19. Both passed.
LeakSanitizer is unavailable in this ptrace environment; detect_leaks=0 was explicit.
These are HTTP lifecycle/component results, not full server or Windows qualification.
