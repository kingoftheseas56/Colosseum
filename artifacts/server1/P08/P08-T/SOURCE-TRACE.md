# P08-T contract trace

- Control authority: accepted `docs/server1/CONTROL-GRAPH-ADDENDUM.json`, P08-T worker digest `82efdcc83eafb64edae458ed20ddb2f941e380ad63bed6c5ab92290b9fd9cef2`.
- Causal inputs: accepted P08-A feasibility seam and B-W3C candidate `cefb6fe21524c21ccf526d5f2e70904b1ee793aa`.
- P08-A capability matrix pins libtorrent 2.0.11.0 revision `6e1587799` and proves exact selected-peer block request, picker suppression, reservation hotswap, partial delivery, metadata/discovery control, honest peer statistics, settings scope, and choke/interest observation on real local TCP peers.
- K02 fixes exact wire block length at 16 KiB and supports final tails. K03/K04 fix selection identity, request ownership, hotswap cancellation and generation-safe terminal behavior.
- The public contract therefore carries request id, selection id, generation, peer handle and exact piece/offset/length together on request, cancellation, block and failure paths. It exposes typed peer/block/failure observations, honest statistics, interest/choke actions, and explicit close.
- The public header contains no libtorrent type. K10 remains the sole future adapter implementation owner.
