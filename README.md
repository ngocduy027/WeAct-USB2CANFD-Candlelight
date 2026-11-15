# WeAct USB2CANFD V2 — Candlelight firmware

STM32G431 Candlelight/gs_usb firmware for the WeAct USB2CANFD V2, with CAN FD,
1 µs timestamps and reviewed CAN/USB reliability fixes. Current release:
**`0x26111501`** (2026-11-15, revision 01).

| LED | Behavior |
|---|---|
| Red READY | On when USB is configured and CAN is open/running; off during close, suspend, recovery or bus-off |
| TX | 15 ms pulse for an actual transmitted CAN frame |
| RX | 15 ms pulse for an actual received CAN frame |

TX/RX remain off at idle and have a minimum 40 ms off interval between pulses.
USB requests, startup, errors and identification do not generate traffic blinks.
A connected USB cable with CAN DOWN leaves all three LEDs off.

## Files

- `Source/`, `STM32/`: firmware and bundled vendor support.
- `Tests/`: 25 native firmware regression tests.
- `tools/`: portable release verification and ROM-DFU backup/flashing tools.
- `dist/0x26111501/`: ready-to-flash HEX/BIN plus ELF/MAP debug artifacts.
- `Hardware/`: board schematic and transceiver datasheet.
- `docs/`: changes, validation scope and the historical original review.
- `Documentation/`: upstream reference manuals and firmware documentation.

Source files retain their original CP1252 content and mixed line endings. Git
preserves their bytes; use an encoding-aware editor.

## Build and test

Requirements: GNU make, `arm-none-eabi-gcc`/binutils, newlib with nano/nosys specs,
Python 3 and a native C compiler (`cc`). Vendor sources are bundled; no submodules
or dependency download is required. The release was built using ARM GCC 14.2.1.

```sh
make -j4
make test
make test-host
make verify
```

Build outputs are under `Build_STM32G431xx_Candlelight_WeActStudio/` and are ignored
by Git. Other upstream targets remain available through their `Make_*` files.
`make verify` checks the bundled release, including matching HEX/BIN contents and
the frozen source manifest. After editing firmware, build and validate a new
release; the flash helper intentionally uses the bundled release.

## Flash the bundled release

Use the **WeAct USB2CANFD V2 / STM32G431CB with 128 KiB flash**. Disconnect CAN.
The portable helper uses the physical BOOT0 entry method, so it can also replace
factory firmware without relying on any installed USB protocol.

1. Hold BOOT0 while reconnecting USB, then release it.
2. List ROM DFU devices and copy your target's serial:

   ```sh
   sudo dfu-util -l
   ```

3. Flash that exact serial:

   ```sh
   sudo python3 tools/flash.py --serial YOUR_ROM_SERIAL
   ```

The helper checks the release and the 128 KiB flash geometry, backs up all flash
before programming, generates a hash-guarded restore script, and verifies the
candidate bytes and every reserved user-page byte. Backups and logs stay under
ignored `local/flash.*/`; retain them locally. It never programs option bytes.
After PASS, reconnect USB **without BOOT0**. The tool performs no USB runtime or
CAN traffic test; readback and runtime acceptance are separate checks.

To check files without USB/root:

```sh
python3 tools/flash.py --check-only
```

The HEX file includes load addresses and is also suitable for a compatible STM32
programmer. The raw BIN must be loaded at **`0x08000000`**.

## Linux SocketCAN

With `gs_usb` loaded, find the adapter's current interface using `ip -brief link`.
For example, if it is `can0`:

```sh
sudo ip link set can0 type can bitrate 500000
sudo ip link set can0 up
ip -details -statistics link show can0
```

READY should light. TX/RX pulse only when traffic flows. Close with:

```sh
sudo ip link set can0 down
```

CAN wiring needs H-to-H, L-to-L, CAN-side ground and proper end termination.
Supported nominal minimums are 5 kbit/s classic and 20 kbit/s with FD enabled;
WeAct's FD data-rate ceiling is 5 Mbit/s. Physical FD timing margins have not been
qualified with an external FD peer.

## Verification and limitations

The LED release passed 25 firmware tests, 254 workspace host-verifier tests, four
warning-free builds, full flash readback and classic/FD controller loopback after
cold return. Visual LED confirmation has not been recorded. Broader USB lifecycle,
physical bus-off and timestamp rollover results apply to its `0x261115` predecessor.
See [validation details](docs/VALIDATION.md) for exact scope.

The 250 mA USB declaration is a provisional engineering budget. Maximum-load and
suspend current are unmeasured; the isolated converter cannot be disabled by
firmware. This project does not claim complete electrical/USB qualification.
Windows/macOS driver binding remains untested.

## Upstream and publishing

Derived from [WeActStudio's CANable firmware](https://github.com/WeActStudio/CANable-2.5-firmware-Slcan-and-Candlelight)
at commit `9a2121d2feca6d1fcec6f30312df9491b23b1204`, based on ElmueSoft,
Nakanishi Kiyomaro, Normadotcom and the other authors named in the source.
Board references come from [WeActStudio.USB2CANFDV2](https://github.com/WeActStudio/WeActStudio.USB2CANFDV2).
See [NOTICE.md](NOTICE.md) for retained component notices.

This folder has its own Git repository on `main`. After creating an empty GitHub
repository, set your own remote and push:

```sh
git remote add origin https://github.com/YOUR_ACCOUNT/YOUR_REPOSITORY.git
git push -u origin main
```
