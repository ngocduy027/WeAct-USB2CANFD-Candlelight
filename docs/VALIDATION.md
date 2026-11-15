# Validation scope

## Current LED release: 0x26111501

Image: 42,868 bytes, SHA-256:
`820827be2ec0fd100475f313f2fea9a6b18f32f2123ace6c4665f1cfcf4b06f5`.
The packaged C/assembly/build inputs are byte-identical to the reviewed release.
The native test README's workspace-only link is adjusted for this repository.

- 25 native firmware tests pass, executing actual firmware functions with modeled
  hardware and undefined-behavior sanitization. Run with `make test`.
- 254 workspace host-verifier tests passed before export. Their transcript is in
  `verification/workspace-host-verifier-tests.log`. Those historical workspace
  helpers depend on local inventories/releases and are not shipped as public tools.
- Four targets build without warnings: WeAct G431, Multiboard G431, dual G473
  Candlelight and G431 Multiboard SLCAN compatibility.
- Saved host flash records independently confirm the exact current BIN, all
  reserved user pages preserved, and cold-return loopback: 16 classic and 16 FD
  controller RX/TX pairs. Visual LED confirmation is not recorded.
- The new portable ROM-DFU tool passes eight offline host tests (`make test-host`).
  It has not itself been exercised against USB in this sandbox. Earlier successful
  flashing used the workspace helper; the shipped tool uses manual BOOT0 entry,
  exact serial selection, full backup/readback and reserved-page verification.

The standalone project was rebuilt after packaging: both BIN and HEX match the
frozen LED release byte-for-byte. Its 25 native tests pass from this new location.
The package records 273 source/build/test inputs; only the native test README
link was adjusted from the workspace export.

## Earlier release: 0x261115

The following extensive board results apply to the predecessor image, not to a
repeat of every test on the LED release:

- Seven changed-path phases: queued interface reset, unconfigured requests,
  timing boundaries, 1 µs captures and error throttling; 182 raw records checked.
- Actual dfu-util runtime detach/alternate-zero, stable ROM, unchanged full flash,
  cold return and 48 before/after RX/TX pairs; 96 raw records checked.
- Nine USB traffic/lifecycle phases, both endpoints' queued halt-clear recovery,
  12 retained-timing sessions: 883 raw records, 448 unique TX retirements and 433 RX.
  A deliberate 250 ms no-reader overload lost 15 RX copies with overflow reported;
  this was recovery/accounting acceptance, not a lossless overload claim.
- Actual classical CAN BUSOFF then RESTARTED, one unique fault-owner retirement,
  and 352 exact physical frames (176 each way) without reopening WeAct.
- Timestamp soak: 363 batches / 2,904 exact FD pairs over 3,349.21 seconds; all
  5,808 raw records independently checked. Query/RX/TX crossed one 32-bit rollover,
  followed by 32 fresh post-wrap frames. The 24.8-day recovery bug is covered by
  native stale-tick tests, not a physical multiweek soak.

## Review disposition

The historical [original review](ORIGINAL_REVIEW.md) describes the original
checkout. Implemented changes include CAN frame bounds/padding, TX ownership,
error reporting/recovery, USB lifecycle/control validation, independent timestamps,
flash/DFU safety, startup/watchdog handling and build dependencies. The follow-up
patch review additionally corrected the long-uptime comparison, alternate-zero
reset, error rate limit, inactive IRQ acknowledgement and unconfigured OUT handling.
SLCAN keeps polled ownership. G473 storage is now fixed to segments 0–7 at
0x0801C000–0x0801FFFF; old larger-device end-of-flash data is not auto-migrated.
WeAct's 128 KiB storage mapping is unchanged.

## Remaining limitations

- Maximum-load and suspend current are unmeasured; the 250 mA declaration is an
  engineering budget. The isolated converter has no firmware enable. Original M20
  and the follow-up power measurement remain partial.
- Windows/macOS USB binding/DFU, external physical FD/TDC margins, absolute clock
  calibration, exact reset/control overlap and deliberate fault injection remain
  unproven. The available peer was classical-only.
- Raw hardware logs, serial-number inventories and board backups stay in the
  original workspace and are intentionally excluded from the public package.
