# libuv native system statistics dependency

The CMake FetchContent declaration pins libuv 1.52.1 from its official archive:
https://dist.libuv.org/dist/v1.52.1/libuv-v1.52.1.tar.gz
SHA256: 66d511b9e6e334c0e62279eb234fbfb2b3110b1479c09b95b44c7afca8cff9e7

The full source is downloaded at configure time (or supplied through the standard
FetchContent source override); no Node binary or JS runtime is linked. The helper
uses uv_loadavg, uv_cpu_info and uv_free_cpu_info, matching the Node OS primitives.
LICENSE and LICENSE-extra are retained for native binary distribution notices.
Runtime-sensitive test qualification is recorded under artifacts/server1/H01.
