# Colosseum OS: Arc 66 architecture discussion

Source: accepted Preflight-Architect Arc 66 architecture, dated 2026-10-06.
Colosseum code reference: master 536f1be5fceb23484092b58a424b4f79e4533e05.
This is a compact, public-facing discussion reference, not a runtime qualification.

## Accepted architecture
Colosseum OS is a media appliance operating system, also available as an
enthusiast PC/HTPC side OS. The user-visible shell remains the existing
Qt Quick/QML + native C++ Colosseum application. It is not an app autostarted
behind KDE/GNOME, a new custom Linux kernel or a rewritten Colosseum product.

Boot: firmware/UEFI -> bootloader -> conventional minimal Linux -> systemd
and platform services -> established Wayland compositor/kiosk session ->
Colosseum graphical shell. Power-on should show Colosseum directly.

One family, two profiles: TV is remote/controller-first with CEC, refresh-rate
switching, suspend/resume and safe recovery as later goals; PC/HTPC supports
keyboard, mouse, controllers, Live USB and optional dual-boot or second drive.
The Linux platform adapter should connect to established services such as
NetworkManager, BlueZ, PipeWire/WirePlumber, logind/systemd and UDisks2.
Do not write those systems from scratch.

## Proof status
The design was ACCEPTED on 2026-10-06, but Arc 66 marked OS implementation
NOT STARTED. This source export does not prove any bootable Colosseum OS,
Linux VM kiosk boot, CEC, DRM, HDR, hardware compatibility, installer or
atomic updates. Arc 17 Linux app progress is a foundation, not OS proof.
The first proposed proof is a minimal Linux VM that boots directly into
Colosseum QML with defined close/crash behavior, no desktop, and a reproducible
session configuration. Hardware and distribution choices remain open.

## Discussion prompts
Where should Colosseum taskbar state end and Linux process management begin?
How can the OS protect playback and browsers from unrelated UI stalls, while
retaining fast switching? What happens after a crash, power loss or OS update?
How should constrained TV memory affect retained Feria tabs and warm libmpv?
Avoid treating proposed functionality as already implemented.
