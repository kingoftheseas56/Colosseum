# Player 2 — Colour and HDR Policy

**Status:** authoritative for the Player 2 engine as of Task 12. Honest statement of what the engine
does today, so nothing is silently mishandled. The decision logic lives in one pure function,
`resolveColorConversion()` (`native/player2/video/ColorHdrPolicy.{h,cpp}`), and is unit-tested in
`tests/player2/player2_device_recovery_test.cpp`.

## What the engine outputs

The D3D11 VideoProcessor path converts every decoded frame to **8-bit `R8G8B8A8_UNORM` (SDR)**
before it reaches the scene graph. There is **no HDR output path**: no 10-bit swap-chain, no PQ/HLG
scan-out, no HDR10 metadata forwarding. This is the same output the frozen Task-3 prototype produced.

## Matrix and range (supported, exact)

Resolved from the decoded frame's `colorspace` and `color_range`:

| Source | Matrix | Notes |
|---|---|---|
| `AVCOL_SPC_BT709` | BT.709 | HD |
| `AVCOL_SPC_UNSPECIFIED` **and** height ≥ 720 | BT.709 | **untagged-HD fallback** (preserved from the prototype) |
| `AVCOL_SPC_BT2020_NCL` / `_CL` | BT.2020 → approximated as BT.709 in the legacy struct | see HDR below |
| anything else | BT.601 | SD |

- Range: `AVCOL_RANGE_JPEG` → **full** (`Nominal_Range = 2`), otherwise **studio** (`= 1`).
- Output is full-range RGB (`RGB_Range = 0`, `Nominal_Range = 2`), matching the prototype.
- The D3D11 legacy `D3D11_VIDEO_PROCESSOR_COLOR_SPACE` struct can only express BT.601 (`0`) and
  BT.709 (`1`); BT.2020 is therefore approximated as BT.709. This is an accepted, documented
  approximation, not a silent error.

## HDR (identified, tone-mapped to SDR — never a silent passthrough)

An HDR source is detected when the frame declares a PQ transfer (`AVCOL_TRC_SMPTE2084`), an HLG
transfer (`AVCOL_TRC_ARIB_STD_B67`), or BT.2020 primaries/colourspace. Because the engine has no HDR
output, such a source is **tone-mapped down to SDR** by the VideoProcessor (`HdrHandling::TonemapToSdr`)
and that fact is reported in diagnostics (`colorConversion` includes "HDR tone-mapped to SDR").

`HdrHandling::Passthrough` intentionally does not exist: the engine cannot output HDR, so passthrough
is never claimed. The policy never silently treats HDR as SDR — it labels it.

## 10-bit / P010

10-bit decoded frames (`P010`) are accepted and down-converted to 8-bit RGBA by the VideoProcessor.
Both 8-bit (`NV12`) and 10-bit (`P010`) SDR are supported; the extra precision is not preserved on
output (8-bit RGBA), which is acceptable for SDR display.

## What is explicitly NOT supported (the honest gaps)

- **True HDR passthrough / HDR10 / Dolby Vision** — not implemented; HDR is tone-mapped to SDR.
- **Wide-gamut (BT.2020) primaries preservation** — approximated to BT.709.
- **10-bit output** — down-converted to 8-bit RGBA.

These are recorded here rather than hidden. Closing any of them is a future task (a 10-bit HDR
swap-chain + true tone-map/passthrough decision), out of scope for the current promotion path, which
targets SDR parity with the existing mpv player's default output.
