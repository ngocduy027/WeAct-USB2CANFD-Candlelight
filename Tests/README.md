# Firmware regression tests

Run from the firmware repository:

```sh
python3 -m unittest discover -s Tests -v
```

Requirements: Python 3, a host C compiler (`cc`), GNU make, and the ARM GCC/newlib
cross toolchain. Test binaries and firmware builds are created in temporary directories.

The native tests execute extracted firmware functions or compile the actual frame
codec with modeled peripherals and undefined-behavior sanitization. Coverage includes:

- Bulk/EP0 memory bounds, malformed wire records, deterministic FD padding, and deferred control status/cancellation.
- Every endpoint address byte, interface bounds, successful alternate-zero resets with IN replay/staged OUT protection, and propagation of failed class requests.
- TX completion/cancellation ownership, echo-pool exhaustion, backpressure, duplicate events and pool conservation.
- Deferred USB reset/reinitialization, IN replay after clear-halt, staged OUT records, and a second reset during teardown.
- Error enqueue failures, first-event immediacy and repeated-error throttling, retained bus-off/restart transitions, CRC location and directional error counters.
- HAL error-state shutdown, hardware stop timeout, recovery after a stale 24.8-day tick and across tick wrap, failed recovery/retry and logical channel ownership.
- Timestamp epoch/pending-update/32-bit wrap, 51.2 ms frame age, 1 us capture resolution, and 5 kbit/s classic / 20 kbit/s FD nominal minimums.
- Wide bus-load arithmetic, IRQ counter updates, bit-timing/SJW constraints and the WeAct 5 Mbit/s ceiling.
- All user-flash indices, all data lengths 1–2046 (including unaligned source buffers), padding and injected erase/program/lock failures.
- Deferred DFU reset, consumed retained request, ROM vector validation, watchdog initialization and retained reset reasons.
- Pre-configuration vendor OUT rejection, inactive CAN interrupt acknowledgement and Candlelight-only FDCAN interrupt ownership (SLCAN remains polled).
- WeAct READY/activity LED GPIO polarity, startup/close/suspend/bus-off behavior, 15 ms pulses and 40 ms gaps, first-frame tick zero, tick wrap and nested IRQ masks; other-board status policy preserved.
- USB requests cannot flash WeAct RX; unsupported identification is rejected. Same-day software revisions retain the USB descriptor year/month.
- Header/HAL/compiler-flag/linker dependencies, no-op builds and full cleanup.

They do not replace flashing, CAN loopback, peer traffic, USB reset/suspend, Windows
driver binding or electrical measurements. Flash tests use a fixed-address RAM
mapping on Linux; that fixture disables host fortify object-size inference for the
mapping while retaining undefined-behavior checks.

See `../docs/VALIDATION.md` for current verification scope and remaining hardware limitations.
