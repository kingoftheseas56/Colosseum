# K03-A source trace

- Oracle: `server.js`, SHA-256 `405eb494d6708406a30e716c3cfb5abae7a5e9c7a8b79446d64c3f821385930f`.
- Authority: M814 lines 72439-72764, SHA-256 `05eba72a8229b9705b0e657af5a4223fb88809f1b90325614cb33a5ecefb005e`.
- K03-01: `toNumber` maps true to 1 and false/falsy to 0; `engine.select` gives each selection from/to/offset/priority and sorts descending. Native sorting is stable for equal priorities and bounds explicit `readFrom`/`selectTo` to the parent selection.
- K03-02: `onupdatewire` sends a zero-downloaded peer through the reverse selection/reverse piece scan and a peer with prior payload through the normal priority-sorted forward scan.
- K03-03: `gc` advances over completed pieces, notifies after offset movement and again on terminal removal, updates interest, and enters idle. The native event ledger emits idle only on the transition, preventing duplicate idle actions from repeated collection.
- Critical width follows `engine.critical(piece, width || 1)` and reset clears the critical bit.

The component returns deterministic policy choices/events only. It does not call a transport or convenience downloader.

Fix Round 1 (2026-09-15): the frozen M814 `shufflePriority` behavior is exposed as stable truthy-group rotation after request-budget fill, and source defaulting makes critical width zero act as one.
