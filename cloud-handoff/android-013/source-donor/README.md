# Nuvio Engine

Nuvio Engine is a cross-platform torrent streaming engine built for Nuvio.

> [!IMPORTANT]
> This project is under active development. It is not yet considered production-ready, and APIs or behavior may change without notice.

## Supported platforms

- Android
- iOS
- macOS
- Windows
- Linux

## Overview

Built in C++20 and powered by libtorrent, Nuvio Engine provides a shared native core for torrent playback, including playback-aware piece scheduling, file selection, caching, and local HTTP range streaming.

## Development

Build and run the native test suite with CMake and Ninja:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

## License

Licensed under the [GNU General Public License v3.0 or later](LICENSE). See [Third-party notices](THIRD_PARTY_NOTICES.md) for dependency licensing.
