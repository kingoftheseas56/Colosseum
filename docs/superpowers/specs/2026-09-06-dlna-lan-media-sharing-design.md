# DLNA LAN Media Sharing Design

**Date:** 2026-09-06

**Status:** Approved product design

**Desktop baseline inspected:** `master` at `88f8dd5cc432d67c6712f8fa2ac751227e852dc6`

**Android baseline inspected:** `recon/android-master-2026-09-05` at `174f4c4754a0c05334cec3a667855dd997409d4c`

## Product Contract

Colosseum gets one persisted Settings switch beside the existing content preference controls:

**Share Colosseum over DLNA**

The switch is OFF by default. When enabled on a desktop Colosseum instance, the app becomes a LAN-only UPnP AV / DLNA MediaServer. Compatible devices on the same local Wi-Fi or Ethernet network can discover, browse, and directly play eligible media already owned by Colosseum.

The exported library includes playable media represented by the existing Vault index, including completed Downloads already projected into that index by `VaultDownloadsRoot`. DLNA does not create a second scanner, database, media identity system, or download owner.

Android Colosseum is a first-party client. It discovers nearby Colosseum MediaServers, browses them through a small native remote-library surface, and plays selected media using the existing Android Media3 player path.

## User-Facing Scope

V1 has no pairing screen, account requirement, cloud relay, Internet sharing, port forwarding, remote-access configuration, transcoding controls, or renderer profiles.

The desktop setting copy is:

- Title: `Share Colosseum over DLNA`
- Description: `Make playable media in Colosseum available to devices on this Wi-Fi/LAN. Anyone on this local network can browse and play it.`

Android needs no corresponding sharing setting to consume another Colosseum server. A minimal `Nearby Colosseum` source is visible only while one or more compatible servers are discovered.

## Ownership and Source of Truth

`VaultIndex` remains the authoritative media catalogue. Its `FileRow` already owns the durable `vault:<sha1>` identity, path, title, kind, size, duration, identity metadata, away state, and media-admission state.

`VaultDownloadsRoot` already projects completed container downloads into the same Vault index and deduplicates overlap with ordinary Vault roots. The DLNA projection therefore reads `VaultIndex`; it does not walk directories and does not call download backbones directly.

V1 exports current playable video rows. Book/comic containers such as EPUB, PDF, and CBZ are not advertised merely because they are indexed. Future canonical audio/image kinds may be added through the same projection without changing ownership.

## Protocol Foundation and Code Provenance

The only external implementation code introduced for DLNA is **pupnp/libupnp 22.0.6**, pinned to upstream commit:

`837c6e4401de68deca1aef6254475326a8b87b2a`

Colosseum consumes libupnp as a third-party library through its public API. It does not paste selected `ssdp`, `soap`, or sample implementation files into Colosseum-owned source.

The upstream BSD-style three-clause license is preserved in a shipped license file and `THIRD_PARTY_NOTICES.md`.

Colosseum-owned C++ implements the MediaServer semantics, catalogue mapping, DIDL-Lite generation, service action handling, LAN policy, and Android integration.

These projects are **reference only; zero source code is copied from them**:

- Gerbera, GPLv2: ContentDirectory/ConnectionManager behavior, metadata and compatibility reference.
- ReadyMedia/MiniDLNA, GPLv2: smallest useful MediaServer behavior reference.
- Jellyfin DLNA plugin, GPL-3.0: current service/profile compatibility reference.
- Universal Media Server, GPL-2.0: renderer quirk reference.
- Kodi/Platinum, GPL-family: embedded media-app UPnP behavior reference.
- Rygel/GUPnP: external-application catalogue projection architecture reference.
- jUPnP/Cling family: Android fallback reference only if the shared native stack proves non-viable.

## Desktop Architecture

New code lives under `native/dlna/` and is intentionally narrow:

1. `DlnaCatalogueProjection` maps `VaultIndex` rows into stable DLNA containers/items.
2. `DlnaDescription` and `DlnaDidlWriter` generate original XML using Qt XML APIs.
3. `DlnaMediaServer` owns libupnp lifecycle, SSDP advertisements, service callbacks, and local-interface changes.
4. `DlnaVirtualFileServer` maps opaque media object IDs to read-only files through libupnp's virtual-directory HTTP hooks.
5. `DlnaClient` implements the shared UPnP control-point path used by Android.
6. `DlnaAndroidRelay` exists only on Android to relay a validated discovered LAN HTTP resource to Media3 over loopback.

The desktop server advertises:

- `urn:schemas-upnp-org:device:MediaServer:1`
- `urn:schemas-upnp-org:service:ContentDirectory:1`
- `urn:schemas-upnp-org:service:ConnectionManager:1`

Required V1 ContentDirectory actions:

- `Browse`
- `GetSystemUpdateID`
- `GetSearchCapabilities`
- `GetSortCapabilities`

Required V1 ConnectionManager behavior:

- truthful `GetProtocolInfo`
- harmless zero-current-connection responses for mandatory connection queries needed by tested clients

The HTTP resource path supports GET, HEAD, single byte ranges, `Accept-Ranges: bytes`, correct `Content-Range`, and `206 Partial Content`. Protocol info declares range operation support with `DLNA.ORG_OP=01`. No untested DLNA profile name is advertised.

## Catalogue Shape

The root object ID is `0`.

Group containers derive from existing Vault group keys and use a stable hash-based DLNA ID. Media items derive from `VaultIndex::FileRow::id`, so identity stays stable across reboots without another database.

Rows are omitted when the root is away or the media path is no longer available. V1 supports MIME types with a deterministic extension mapping, beginning with MP4, MKV, WebM, AVI, and MOV.

`VaultIndex::changed()` invalidates the projection. `GetSystemUpdateID` derives from the existing index revision clock rather than inventing a second mutation counter.

## LAN Boundary

The feature is LAN-only by product contract.

The server binds a selected active private/local IPv4 address on an Ethernet or Wi-Fi interface. It never deliberately exposes the MediaServer on a public/WAN address and never asks libtorrent/router UPnP to create port mappings.

The server stops when the setting is switched off. When the selected LAN interface/address changes, it cleanly unregisters the old device and re-registers on the new local interface.

The root-device UDN is generated once and persisted independently of IP address, so a DHCP address change does not make Colosseum look like an entirely new server.

## Android Network Boundary

The Android integration branch currently targets the Media3 player path and keeps cleartext HTTP disabled except for `127.0.0.1`. V1 does not weaken that global security policy.

Ordinary DLNA media URLs are LAN HTTP URLs. `DlnaAndroidRelay` therefore listens only on loopback, accepts only session tokens created from resources returned by a discovered MediaServer, validates that the remote target resolves to the active local network, forwards GET/HEAD/Range natively, and exposes only the loopback URL to Media3.

The relay is not a general-purpose proxy and must reject arbitrary URLs, public destinations, unexpected redirects, unsupported methods, and unknown session tokens.

Android SSDP discovery uses a Wi-Fi multicast lock only for the active discovery window and releases it on success, timeout, cancellation, or lifecycle suspension.

While targetSdk remains 36, V1 uses existing Internet access plus `CHANGE_WIFI_MULTICAST_STATE` and does not request Android 17's `ACCESS_LOCAL_NETWORK`. When targetSdk becomes 37+, local-network runtime permission handling becomes mandatory before SSDP/LAN access.

## Explicit Non-Goals

V1 does not implement:

- transcoding or remuxing
- WAN/Internet access
- router UPnP/NAT-PMP port mapping
- authentication/pairing/PINs
- remote writes, deletes, downloads, or library edits
- a DLNA-specific media scanner/database
- syncing remote DLNA rows into local `VaultIndex`
- EPUB/PDF/CBZ reading over DLNA
- arbitrary generic DLNA renderer control/casting
- broad device-specific quirk tables without a reproduced client failure

## Acceptance Criteria

Desktop acceptance requires all of the following:

1. With sharing OFF, no Colosseum MediaServer is advertised.
2. Turning sharing ON makes one stable `Colosseum on <hostname>` MediaServer discoverable on the local LAN.
3. A client can Browse the root and a video group, select a media item, and retrieve its bytes.
4. HEAD and byte-range GET work correctly; seeking during playback succeeds.
5. A completed video Download appears through the same canonical catalogue path without a second scan.
6. A Vault/index mutation advances the UPnP SystemUpdateID.
7. Turning sharing OFF unregisters the device and closes the serving path.
8. No public/WAN bind or router port mapping occurs.

Android acceptance requires all of the following:

1. Colosseum discovers a desktop Colosseum MediaServer on the same LAN.
2. `Nearby Colosseum` renders server/library rows without persisting them into local Vault.
3. Selecting a remote video opens the existing Media3 playback surface.
4. Seeking works through the loopback relay and preserves byte-range semantics.
5. The existing cleartext policy remains globally disabled except for loopback.
6. Multicast lock and any future Android local-network permission are released/handled cleanly across lifecycle changes.

## Implementation Rule

Before implementation starts, create a fresh feature/integration branch from the then-current desktop `master` and reconcile the latest Android integration branch into it. Do not build this feature on a stale Android snapshot merely because the design was inspected against `recon/android-master-2026-09-05`.