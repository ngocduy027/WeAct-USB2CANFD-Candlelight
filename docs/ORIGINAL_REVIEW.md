> Historical review of the original checkout. Current fixes and validation are summarized in [VALIDATION.md](VALIDATION.md). Workspace-only links are preserved as plain archival references.

# Candlelight firmware review — WeAct USB2CANFD V2 (STM32G431CBT6) — unified report

This document merges two independent reviews into one self-contained report:

- Claude's review (`CANDLELIGHT_FIRMWARE_REVIEW.md`)
- Codex's review (`CANDLELIGHT_FIRMWARE_REVIEW_CODEX.md`)

Each reviewer's findings were cross-checked by the other. Every finding below is supported by both; where the two disagree on severity, both ratings are shown. The two source documents are superseded by this one. The old finding IDs are mapped to the new ones in appendix A.

**Historical review of the original checkout.** No changes were made during that review. Subsequent authorized fixes and their evidence are tracked in CANDLELIGHT_FIX_PROGRESS.md (`CANDLELIGHT_FIX_PROGRESS.md`; original-workspace reference); board acceptance remains incomplete. See CANDLELIGHT_BOARD_TESTS.md (`CANDLELIGHT_BOARD_TESTS.md`; original-workspace reference) for the outstanding hardware checks.

**Implementation status, 2026-11-15:** all 47 IDs are accounted for; 46 have
implemented source changes, while M20 remains partial because board power
consumption is unmeasured and the isolated converter cannot be shut down by
firmware. These are source states, not 46 claims of complete hardware acceptance.
The latest controller/USB soak passed 1984 exact FD RX/TX pairs and real query,
RX and TX 32-bit microsecond timestamp rollover, followed by 32 fresh frames.
Current evidence and remaining measurement/fault-injection gaps are recorded
in the progress ledger and acceptance audit (`CANDLELIGHT_HARDWARE_LOGS/current-acceptance-audit.json`; original-workspace reference).

The subsequent Claude patch review has also been addressed in source for candidate
**0x261115**; see the disposition and release notes (`CANDLELIGHT_PATCH_REVIEW_RESULTS.md`; original-workspace reference).
The timestamp soak above applies to the earlier **0x261113** image. The new 1 µs policy passed 16-bit capture checks and a 2,904-pair 32-bit
query/RX/TX rollover soak. Electrical current qualification remains incomplete.

## 0. Scope, method and evidence

| | |
|---|---|
| Firmware | `CANable-2.5-firmware-Slcan-and-Candlelight`, commit `9a2121d`, version `0x260416` |
| Target | `Make_G431_Candle_WeActStudio` (Candlelight / gs_usb, 16 MHz crystal) |
| Hardware references | WeAct README, V1.0 schematic, `Doc/CA-IS2062A_datasheet_en.pdf`, RM0440 FDCAN and USB chapters bundled in the firmware's `Documentation/` folder |
| Code reviewed | everything in `Source/` and `Source/Candlelight/`, the relevant parts of the bundled HAL 1.2.5, makefiles, linker script, startup file, HAL config |
| Out of scope | Slcan-specific code, the desktop sample applications |
| Host assumed | Linux kernel `gs_usb` driver (legacy protocol). The ElmueSoft protocol is covered where it shares code. |

Paths are relative to the firmware repository. Line numbers refer to the unmodified checkout. The source files are Windows-1252 encoded, not UTF-8: edit them with a tool that preserves the encoding, or convert them deliberately (and use `grep -a` when searching).

### What was verified, and how

- **Build:** the WeAct target builds with `arm-none-eabi-gcc 14.2.1` without warnings (text 37332, data 532, bss 18640). Both reviewers built into scratch directories outside the repository and got the same sizes.
- **Disassembly:** of that build, and of the binary WeAct ships (`Firmware/Candlelight2.5/STM32G431_Candlelight2.5_WeActStudio_0x260416.hex`), for the DFU jump only. Full binary equivalence between source and shipped HEX was not established.
- **Host harnesses (Codex):** three original functions were extracted unchanged and run on a PC with simulated peripheral inputs (bulk receive, EP0 receive stage, bus-load timer). This verifies software logic, not USB electrical behaviour or CAN waveforms.
- **Reference documents:** clock and FDCAN behaviour against RM0440, transceiver figures against the CA-IS2062A datasheet, Linux behaviour against the `gs_usb.c` source of v6.18 and current mainline, and against the `gs_usb.ko` installed on the review machine (kernel 7.0.0-43-generic).
- **Not done:** nothing was run on real hardware. No malformed transfers were sent to a real adapter. No flashing.

### Evidence labels

- **Confirmed** — proven from the source, the HAL source, disassembly, the map file, a datasheet or the kernel source. This does not mean it was exercised on a board.
- **Host harness** — additionally executed in a PC harness.
- **Code analysis** — follows from reading the code and the host driver's behaviour; should be reproduced on the board.
- **Needs hardware check** — the defect is certain, its visible effect is not.

This is a substantial source review, not proof that every defect has been found. Sustained CAN FD traffic, reset/suspend timing and electrical margins still need board tests.

---

## 1. Summary

| ID | Severity | Evidence | Found by | Problem |
|---|---|---|---|---|
| H1 | High | Code analysis | Claude | Linux TX locks up permanently: dropped frames are never echoed, so the driver's 10 TX slots leak |
| H2 | High | Confirmed (disassembly); effect needs hardware check | Claude | Software DFU entry jumps to the wrong address, with interrupts off and peripherals live |
| H3 | High | Confirmed | Claude | Vendor OUT requests accept any `wLength`: host can overflow `ep0_buf` (2008 bytes) |
| H4 | High | Confirmed, host harness | Codex | Bulk OUT transfers longer than 80 bytes overflow the receive buffer by up to 48 bytes |
| H5 | High | Confirmed (RM0440) | Claude | FDCAN time-quanta clock (160 MHz) is faster than its APB clock (80 MHz), which RM0440 forbids |
| H6 | High | Code analysis | Claude | CAN open/close, buffer resets and flash writes run inside the USB interrupt and race with the main loop |
| H7 | High | Confirmed (code, disassembly); effect needs hardware check | Claude | The clock-recovery unit is configured but never clocked, so USB runs on the untrimmed 48 MHz RC oscillator |
| M1 | Medium–High (Codex: High) | Confirmed | Codex | Standard endpoint requests index endpoint arrays with an unvalidated address |
| M2 | Medium (Codex: High) | Confirmed, host harness | Codex | EP0 acts on the declared length, not the bytes received: stale commands and a further overflow |
| M3 | Medium | Code analysis | Claude | Hardware timestamps are extended to 32 bit incorrectly: some frames are stamped 65.536 ms late |
| M4 | Medium | Confirmed | Claude | Errors in SETUP requests with data are never reported; a failed "start" looks like success to Linux |
| M5 | Medium | Code analysis | Claude | CAN RX is polled with a 3-frame hardware FIFO; several code paths block for milliseconds to seconds |
| M6 | Medium | Code analysis | Claude | Session state survives USB reset and close: stale frames after reopen, start-while-open silently ignored |
| M7 | Medium | Confirmed (map file) | Claude | User-flash bounds check uses `_etext`: segment 45 erases part of the firmware image |
| M8 | Medium | Confirmed (schematic) | Claude | BOOT0 option-byte handling is unnecessary on this board and can disable the BOOT0 button |
| M9 | Medium | Confirmed | Claude + Codex | Error frames do not match Linux `can/error.h`; RX FIFO loss has no standard encoding; error codes partly lost |
| M10 | Medium (Claude: Low–Medium) | Confirmed | Codex | An error report is marked as sent before it is queued; bus-off can go unreported |
| M11 | Medium | Confirmed | Claude | Bus-off recovery ignores HAL return codes and never retries |
| M12 | Medium | Confirmed | Codex | Bus-off recovery leaves a stale pending-TX count; a false timeout can discard subsequently queued frames |
| M13 | Medium | Confirmed | Claude + Codex | Host frames and bit-timing values are not validated |
| M14 | Medium (High with affected drivers; verified in upstream v6.18) | Confirmed (kernel source) | Claude; kernel facts by Codex | Bit timing is erased on close; hosts that do not resend it fail silently on the second open |
| M15 | Low–Medium (Codex: Medium) | Confirmed | Codex | A requested BRS bit is silently cleared when nominal and data bit rates are equal |
| M16 | Low–Medium (Codex: Medium) | Confirmed | Codex | Classical CAN DLC 9–15 is rejected or treated as an FD payload length |
| M17 | Low–Medium (Codex: Medium) | Confirmed | Codex | ElmueSoft frames with a length between two FD sizes are padded with bytes from an earlier transfer |
| M18 | Low–Medium (Codex: Medium) | Confirmed, host harness | Codex | Bus-load arithmetic overflows 32 bits for report intervals the firmware accepts |
| M19 | Medium (development) | Confirmed (reproduced) | Codex | Header changes do not trigger a rebuild, and `clean` leaves stale objects |
| M20 | Low–Medium | Confirmed (datasheet) | Claude | USB descriptor declares 150 mA; the transceiver alone can draw 150 mA |
| M21 | Low–Medium | Confirmed | Claude | No watchdog; fault handlers are infinite loops |
| M22 | Low–Medium | Code analysis | Claude | A clear-halt request on the bulk IN endpoint stops all traffic to the host |
| L1 | Low (Codex: Low–Medium) | Confirmed | Codex | Bus-load measurement state is not restarted with the measurement |
| L2 | Low | Confirmed | Codex | Valid GET_STATUS requests for the bulk endpoints are rejected |
| L3–L18 | Low | — | Claude | See the table in section 4 |

---

## 2. High severity

### H1 — Linux TX path locks up permanently when a frame is dropped

**Found by** Claude. **Evidence** code analysis; the Linux side is confirmed from the kernel source.

**Where**

- `Source/Candlelight/buffer.c:162-168` — frame discarded when bus-off, listen-only or invalid: no echo.
- `Source/can.c:424-430` with `Source/Candlelight/buffer.c:47-61` — TX timeout wipes the whole TX queue: no echo for any queued frame.
- `Source/Candlelight/buffer.c:231-243` — echo silently skipped if the host pool is empty.
- `Source/Candlelight/usb_class.c:544-548` — frame discarded when the CAN pool is full: no echo.

**What is wrong**

In the legacy (gs_usb) protocol the Linux driver owns 10 TX contexts (`GS_MAX_TX_URBS`). One is taken per transmitted frame and released only when the device returns a frame carrying the same `echo_id`. When all 10 are in use the driver calls `netif_stop_queue()` and only wakes the queue from the echo handler. Contexts are reset only when the interface goes down. This is the same in v6.18 and in current mainline.

The firmware sends the echo at the moment a frame enters the FDCAN TX FIFO (`buffer.c:219-244`). Every path that consumes a host frame without reaching that point leaks one context. The author's own comment at `buffer.c:227-228` describes the resulting symptom ("stops sending after 10 USB OUT transfers ... No buffer space available"), but the drop paths were not covered.

**Concrete scenario (predicted, please reproduce)**

Adapter alone on the bus (or peer powered off), retransmission enabled (the default):

1. Frames 1–3 enter the 3-slot TX FIFO and are echoed.
2. Further frames wait in `list_to_can` because the FIFO is full. They are not echoed.
3. 500 ms after the last FIFO insert, `can.c:424-430` aborts the FIFO and calls `buf_clear_can_buffer()`. The waiting frames vanish without an echo.
4. Each cycle leaks the frames that were waiting. Once 10 have leaked, the Linux queue is stopped for good.

```
ip link set can0 up type can bitrate 500000
cangen can0 -g 50          # nothing else on the bus
# expected: within a few seconds "write: No buffer space available"
# and it stays broken even after a second node is connected,
# until: ip link set can0 down && ip link set can0 up
```

Frames discarded during the bus-off window also leak their echo contexts. The stale pending counter can cause another queue clear after recovery (M12); that leaks additional contexts only if unechoed frames remain queued when the timeout fires.

A second, related defect: frames that were echoed and then aborted by the 500 ms timeout are reported to the host as transmitted. Linux counts them in `tx_packets` and loops them back to local sockets, although they never reached the bus.

**Suggested fix**

The primary design should be back-pressure and clear ownership of each frame until it completes, not echoing after the fact:

1. `buf_process_can()`: reserve the echo object from `list_host_pool` *before* removing the frame from `list_to_can`. If none is available, leave the frame queued and retry next loop. This turns loss into back-pressure.
2. Rule for the legacy protocol: every frame taken from the host must produce exactly one echo, whether it was sent or discarded. gs_usb has no "failed" completion, so an echo plus an error frame is the best the protocol allows. Be aware that an echo for a discarded frame makes the host account for a successful transmission; decide this policy deliberately (Codex refinement).
3. `do_not_send` path: still send the echo, then raise the error flag as today.
4. Replace `buf_clear_can_buffer()` in the timeout and bus-off paths with a drain loop that echoes each queued frame and returns it to the pool, under a proper lock (see H6).
5. As a second step, make the echo real: store the pending echo header in a small table indexed by `MessageMarker`, emit the echo from the TX event in `can_process()` (`can.c:332`) with the real TX timestamp, and emit it with an error frame when the frame is aborted. Track hardware TX completion and cancellation explicitly (M12).

**Test** The `cangen` scenario above; no-ACK transmission for longer than 500 ms, then connect a peer and confirm TX resumes without cycling the interface; bus-off and recovery while sending.

---

### H2 — Software DFU entry is broken

**Found by** Claude. **Evidence** confirmed by disassembly of the build and of WeAct's shipped binary; the visible effect needs a hardware check.

**Where** `Source/dfu.c:64-83`, reached from `usb_class.c:496-502` (DFU_DETACH).

**What is wrong**

```c
SCB->VTOR = dfu_sys_memory_base;
__set_MSP(*(__IO uint32_t*)dfu_sys_memory_base);
tBootloader fBootloader = (tBootloader)(dfu_sys_memory_base + 4);   // <-- not dereferenced
fBootloader();
```

The reset vector is the 32-bit value *stored at* `0x1FFF0004`. The code calls the address `0x1FFF0004` itself, so the CPU tries to execute the vector table. Because the address is even, the branch also clears the Thumb bit, which is an immediate usage fault on a Cortex-M4.

Disassembly of the build, identical in the binary WeAct ships (`0x08001d00` there):

```
ldr.w  r1, [r2], #4     ; r1 = [0x1FFF0000], r2 becomes 0x1FFF0004
msr    MSP, r1
bx     r2               ; branch to 0x1FFF0004, bit 0 clear
```

Further defects in the same function:

- `__disable_irq()` is never undone, so PRIMASK stays set on entry to the ROM code.
- Nothing is de-initialised: USB stays attached with its pull-up on, the PLL stays at 160 MHz, SysTick and TIM3 keep running. The host never sees a disconnect.

**Effect on this board (needs hardware check)**

What happens after the fault depends on the ROM's own fault handler, which could not be verified. The comments in `dfu.c:28-39` and `dfu.c:62-63` say the function only works when the BOOT0 pin is enabled, which suggests that on ordinary CANable boards DFU is reached through a reset with BOOT0 high (CAN RX idles high there), not through a working jump.

On the WeAct board that cannot happen. After any reset PB7 floats, R9 pulls `/OE` of U5 high, the buffer is disabled and R13 pulls BOOT0 low. So DFU_DETACH (`dfu-util -e`, or the ElmueSoft updater) is expected to end in a hang or a plain restart of the application. Only the BOOT0 button method from the WeAct README is known to work.

**Suggested fix**

Use the standard reset-then-jump pattern, which starts the ROM from a clean reset state and does not depend on the BOOT0 pin:

```c
// RAM that the startup code does not clear (add a .noinit section to the linker script)
static uint32_t dfu_magic __attribute__((section(".noinit")));
#define DFU_MAGIC 0xB00710ADu

void dfu_request(void)            // called 300 ms after DFU_DETACH, as today
{
    dfu_magic = DFU_MAGIC;
    NVIC_SystemReset();           // drops the USB pull-up, resets clocks and peripherals
}

void dfu_check_early(void)        // first statement of the reset path, before any clock setup
{
    if (dfu_magic != DFU_MAGIC) return;
    dfu_magic = 0;
    const uint32_t *vt = (const uint32_t *)0x1FFF0000;
    __set_MSP(vt[0]);
    ((void (*)(void))vt[1])();    // vt[1] already carries the Thumb bit
}
```

Notes:

- The snippet is illustrative. Changing MSP from C and then calling through a pointer depends on the compiler not using the stack in between; do the stack switch and jump in a few lines of assembly or a `naked` function (Codex refinement).
- `startup_STM32G431xx.s` zeroes `.bss` *before* calling `SystemInit` (lines 78-98), so the flag must live in a `.noinit` section or a backup register, not in `.bss`.
- Test on the board that the G431 ROM stays in DFU with BOOT0 low. This is the method ST describes in AN2606 and it is widely used on G4, but it could not be confirmed here.
- With this in place the option-byte calls in `dfu_switch_to_bootloader()` are not needed for the WeAct build (see M8).

**Test** `dfu-util -l` after `dfu-util -e`; the device must re-enumerate as `0483:df11` without touching the button. Keep the BOOT0 button and an SWD probe as fallback.

---

### H3 — Host can overflow the EP0 buffer through `wLength`

**Found by** Claude. **Evidence** confirmed.

**Where** `Source/Candlelight/control.c:186-240`, buffer at `control.c:34`.

**What is wrong**

For OUT requests the code only checks `req->wLength < min_len`. There is no upper bound, except for `ELM_ReqWriteFlash`. It then calls `USBD_CtlPrepareRx(pdev, ep0_buf, req->wLength)`. `ep0_buf` is 2008 bytes; `wLength` can be 65535. `USBD_LL_DataOutStage()` (`usb_core.c:142-146`) keeps receiving 64-byte packets into an advancing pointer for as long as the host sends data.

Example: `GS_ReqSetBitTiming` with `wLength = 4096` is accepted. In this build `ep0_buf` ends at `0x20001530`; `GS_CapabilityFD` follows at `0x20001578` and `buf_inst` (the linked-list heads and frame pools) at `0x200015e8`. Overwriting list pointers gives the host arbitrary writes on the device.

Any program allowed to open the USB device can trigger this. A buggy host library is enough.

**Suggested fix**

Require the exact expected length per request and stall otherwise:

```c
if (req->wLength != expected_len)        // WriteFlash: 1 .. MAX_FLASH_DATA_LEN
{
    ELM_LastError = FBK_InvalidParameter;
    return false;                        // stall
}
```

At minimum add `if (req->wLength > sizeof(ep0_buf)) return false;` before `USBD_CtlPrepareRx()`. A guard inside `USBD_CtlPrepareRx()` itself would protect future callers too. An upper bound alone is not the whole fix: see M2 for the case where the bytes received differ from `wLength`.

**Test** After the fix: `dev.ctrl_transfer(0x41, 1, 0, 0, bytes(4096))` with pyusb must be stalled and must not change device behaviour. Do not run this against unfixed firmware you care about; it corrupts RAM.

---

### H4 — Bulk OUT transfers longer than 80 bytes overflow the receive buffer

**Found by** Codex (N01); verified by Claude against the HAL source and the map file. **Evidence** confirmed, host harness.

**Where** `Source/Candlelight/buffer.h:158-159`; `Source/Candlelight/usb_class.c:412`, `551`; HAL `stm32g4xx_hal_pcd.c:1842-1890` and `HAL_PCD_EP_DB_Receive()` at `1986-2053`; `USB_ReadPMA()` in `stm32g4xx_ll_usb.c:865`.

**What is wrong**

`from_host_buf` is 80 bytes, and the class arms an 80-byte receive on a 64-byte, double-buffered OUT endpoint. `HAL_PCD_EP_DB_Receive()` subtracts the received packet size from the remaining transfer length, saturating at zero, and then copies **the entire packet** with no check against the space left.

For two full-size packets:

1. First 64 bytes: remaining length becomes 16, destination advances by 64.
2. Second 64 bytes: remaining length becomes zero, but all 64 bytes are copied.
3. Only afterwards does the class callback run. Total written: 128 bytes into an 80-byte buffer.

Any transfer longer than 80 bytes overflows, by up to 48 bytes. Ordinary 20- and 76-byte gs_usb records do not trigger it, but one plain libusb bulk write does.

In this build `from_host_buf` ends at `0x200044bc`. Directly behind it lie `GLB_ProtoElmue`, `DFU_Status`, `EpToChannel` and `uwTick` (confirmed in the map file). Corruption can switch the protocol, break timing, and misroute callbacks: a corrupted `EpToChannel[2]` is then used as an index into `buf_inst`, so the damage spreads.

This is independent of the EP0 overflow (H3) and of the missing frame validation (M13).

The extracted HAL function, with simulated PMA packet counts, produced:

```text
Bulk OUT: received=128 remaining=0 bytes_written_beyond_80=48
```

**Suggested fix**

- Smallest storage change: make the receive buffer two full packets (128 bytes), while keeping the armed receive length explicitly limited to the protocol's 80-byte maximum. The HAL can write up to 128 bytes for that request. Keep storage capacity, armed receive length and validated record length as separate quantities; update both receive-arm sites (`usb_class.c:412`, `551`) rather than letting `sizeof(from_host_buf)` set the requested length.
- Also change the copy at `usb_class.c:541`: it currently copies `sizeof(from_host_buf)` into an 80-byte `kHostFrameLegacy`. Enlarging only the receive buffer makes that copy overflow the frame by 48 bytes, even for a valid short transfer. Reject received lengths above 80 or inconsistent with the record header, initialise the destination frame, and copy only the validated record length, bounded by `sizeof(obj_to_can->frame)`. Apply the format and padding rules in M13, M16 and M17.
- Alternative: receive one 64-byte packet at a time and assemble records into a separately bounded buffer.
- If HAL multi-packet reception is kept, also enforce the destination bound inside the HAL copy path.

A length check in `USBD_GS_DataOut()` alone is too late: the HAL has already copied the packets by then.

**Test** Transfers of 0, 1, 20, 64, 76, 80, 81, 127, 128 and 129 bytes, with guard bytes around both the receive storage and the destination frame object. Valid short records must not overwrite the destination guards. Oversized input must neither alter the guards nor become a CAN frame. Verify that the next valid transfer is handled correctly.

---

### H5 — FDCAN clock configuration violates RM0440

**Found by** Claude; fix corrected by Codex. **Evidence** confirmed against RM0440.

**Where** `Source/system.c:132` (APB1 = HCLK/2 = 80 MHz), `system.c:141` (FDCAN kernel clock = PLLQ = 160 MHz), `Source/can.c:132` (`FDCAN_CLOCK_DIV1`).

**What is wrong**

RM0440, FDCAN chapter, section 44.3.3 (the PDF is in `Documentation/`):

> The FDCAN requires that the CAN time quanta clock is always below or equal to the APB clock (fdcan_tq_ck < fdcan_pclk).

Here `fdcan_tq_ck` = 160 MHz and `fdcan_pclk` = PCLK1 = 80 MHz. The peripheral's clock-domain synchronisers are designed for the opposite relation.

The comment at `system.c:122-127` calls the 80 MHz setting a bugfix because "the maximum is 80 MHz". As far as we know that limit belongs to other STM32 families; the STM32G4 datasheet lists 170 MHz for both APB buses in boost mode. Confirm in DS12589, "General operating conditions".

**Effect** Not predictable. Out-of-spec clock crossings typically show up as rare, unexplained glitches. The "8 Mbaud only works with a 50 % sample point, very weird" comments (`can.c:153-155`, `can.c:650-651`) may or may not be related.

**Suggested fix** (pick one)

- **A, preferred:** `RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;` giving PCLK1 = 160 MHz. Nothing changes for the host. TIM3 keeps a 160 MHz clock (the ×2 rule no longer applies, the result is the same), so the 1 µs timestamp prescaler at `system.c:219` stays correct.
- **B:** keep PCLK1 at 80 MHz and bring the CAN clock down to 80 MHz. Two ways, which are not equivalent:
  - PLLQ ÷4: `system_get_can_clock()` reads the RCC kernel clock, so `fclk_can`, the TDC offset and the bus-load maths follow automatically.
  - `FDCAN_CLOCK_DIV2`: the divider sits inside the FDCAN, after the point `HAL_RCCEx_GetPeriphCLKFreq()` reports. `fclk_can` would still say 160 MHz and every host would get half the requested bit rate. `system_get_can_clock()` must then return the divided value, and TDC and bus-load must use it too.

  At 80 MHz all current rates remain reachable, including 10 Mbit/s (8 time quanta, e.g. BRP 1, Seg1 5, Seg2 2). That is arithmetic, not a recommendation: the transceiver is rated for 5 Mbit/s.

**Test** After either fix, measure the actual bit rate on the bus at 125 k, 500 k, 1 M and the FD data rates in use; verify the clock tree against the silicon limits.

---

### H6 — Work done inside the USB interrupt races with the main loop

**Found by** Claude; lock-helper point by Codex. **Evidence** code analysis.

**Where**

- `control.c:429` / `control.c:434` — `can_open()` / `can_close()` called from `control_setup_OUT_data()`, which runs in the USB ISR.
- `usb_lowlevel.c:86` — `can_close_all()` in the suspend callback (ISR).
- `usb_class.c:500` — `dfu_switch_to_bootloader()` in the ISR, including option-byte programming.
- `control.c:482`, `control.c:490` — option-byte and flash programming (about 25 ms) in the ISR.
- `buffer.c:47-73` — `buf_clear_buffers()` rebuilds the lists with no IRQ lock, called from both contexts.
- `system.h:59-70` — the lock helpers.

**What is wrong**

The single-item list helpers are IRQ-locked, but the larger operations are not coordinated.

*Race 1: close in the ISR while the main loop holds a frame.* `buf_process_can()` removes a frame object from `list_to_can` (`buffer.c:116`) and returns it at `buffer.c:166` or `buffer.c:247`. If the USB ISR runs `can_close()` → `can_reset()` → `buf_clear_can_buffer()` in between, all 64 objects are put back into the pool, including the one the main loop still holds. The main loop then inserts it a second time. The pool list is now corrupt: items are orphaned, and an object can later be handed out while it is still linked. The window is most of `buf_process_can()`, on every loop pass with TX traffic. Typical trigger: `ip link set can0 down` while frames are still queued. The next open rebuilds the lists, which hides the damage.

*Race 2: timeout clear in the main loop against a USB OUT packet.* `can_process()` calls `buf_clear_can_buffer()` at `can.c:428` with interrupts enabled. `USBD_GS_DataOut()` can fire in the middle of `list_init()` / `list_add_tail()` and take or insert an item on a half-built list. In the worst interleaving `list_to_can` ends up pointing into the pool list head, and a list head is then treated as a frame object: stale frames go onto the bus and memory is corrupted. The window is a few instructions wide, so this is rare, but the outcome is severe.

*Lock helpers do not nest.* `system_enable_irq()` enables interrupts unconditionally. Any `*_locked()` list call inside an outer critical section silently ends that section.

*Stalls.* While the ISR programs flash or option bytes, or runs `HAL_FDCAN_Init()`, the main loop is stopped. `system_write_flash()` does not even check whether CAN is open (`system.c:417-421`), so CAN reception is lost for about 22 ms (see M5).

**Suggested fix**

1. Replace the lock helpers first with save/restore of PRIMASK, one token per critical section:
   `uint32_t s = __get_PRIMASK(); __disable_irq(); ... __set_PRIMASK(s);`
2. Do not act in the ISR. In `USBD_GS_EP0_RxReady()` copy the request into a pending slot and set a flag. Execute it from the main loop.
3. Hold back the status stage until the main loop has finished, then answer with `USBD_CtlSendStatus()` on success or `USBD_CtlError()` on failure, with `USB_LP_IRQn` masked around the call. This also fixes M4. EP0 NAKs in the meantime, which is legal; the Linux driver waits up to 1 s.
4. Same for suspend and DFU_DETACH: set a flag, act in the main loop.
5. Serialise the whole lifecycle transition (open, close, protocol switch), not just the pointer writes; include the error-state snapshots (M10). Make clearing a queue a drain of `list_to_can` rather than a rebuild of both lists, so an object held elsewhere is never re-added.

**Test** `ip link set can0 down` in a loop while `cangen` floods; USB suspend/resume under traffic; run for hours with list-consistency assertions (pool count + queue count + held objects = 64) enabled in a debug build.

---

### H7 — Clock recovery for USB is never actually running

**Found by** Claude. **Evidence** confirmed in code and disassembly; the effect needs a hardware check.

**Where** `Source/system.c:147-157`.

**What is wrong**

The USB clock is the internal 48 MHz RC oscillator (HSI48). On its own it is not accurate enough for USB, so the code configures the Clock Recovery System to trim it against the host's 1 ms SOF packets. But the CRS is an APB1 peripheral with its own clock gate (`RCC_APB1ENR1.CRSEN`), and nothing ever sets it:

- `__HAL_RCC_CRS_CLK_ENABLE()` does not appear anywhere in `Source/` or the board config.
- `HAL_RCCEx_CRSConfig()` (HAL 1.2.5) pulses the CRS reset and writes `CRS->CFGR` / `CRS->CR`, but does not enable the clock.
- In the disassembly of `system_init()` the only bit set in `APB1ENR1` is TIM3 (bit 1); bit 8 (CRS) is never set.

With its clock gated, a peripheral's registers cannot be written. So auto-trimming is off and HSI48 free-runs at its factory calibration.

**Effect (needs hardware check)**

USB full speed requires ±0.25 %. A factory-trimmed HSI48 is typically close at room temperature, which is why the adapter enumerates, but the datasheet allows a deviation of several percent over temperature and supply (see the HSI48 table in DS12589). Expect marginal behaviour on some units, when the board warms up (the isolated DC/DC converter sits next to the MCU), or on strict hosts and hubs: enumeration errors, `-71`/`-EPROTO` in `dmesg`, sporadic disconnects. The failure probability can only be established by measurement.

**A second bug hides behind the first.** `pInit.HSI48CalibrationValue = 32` (`system.c:155`). On the G4 the trim field is 7 bits wide and mid-scale is 64 (`RCC_CRS_HSI48CALIBRATION_DEFAULT = 0x40`, `stm32g4xx_hal_rcc_ex.h:522`); 32 is the default of older families with a 6-bit field. Today that write is lost, so the register keeps its reset value of 64. If only the clock enable is added, the oscillator will start several percent low and crawl up during enumeration.

**Suggested fix** (both lines together)

```c
__HAL_RCC_CRS_CLK_ENABLE();                                   // before HAL_RCCEx_CRSConfig()
pInit.HSI48CalibrationValue = RCC_CRS_HSI48CALIBRATION_DEFAULT; // 64 on G4
```

Deriving 48 MHz from the crystal through the PLL is not a practical alternative here: no PLL setting gives both an exact 48 MHz and the 160/80 MHz CAN clock that the 5, 8 and 10 Mbit/s rates need. HSI48 with a working CRS is the right design.

**Test** With a debugger read `RCC->APB1ENR1` bit 8 and `CRS->CR` / `CRS->ISR`. With CRS working, `CRS->ISR` shows SYNCOK and the TRIM field moves. Repeat enumeration cold and warm.

---

## 3. Medium severity

### M1 — Unvalidated endpoint addresses reach out-of-bounds accesses

**Found by** Codex (N02); verified by Claude. **Severity** Codex: High. Claude: Medium–High. **Evidence** confirmed in the HAL source; no invalid request was sent to hardware.

**Where** `Source/usb_ctrlreq.c:159-308`; `USBD_LL_IsStallEP()` at `Source/usb_lowlevel.c:247-255`; HAL `stm32g4xx_hal_pcd.c:1522-1544` (`HAL_PCD_EP_SetStall`); `EP_ADDR_MSK` at `stm32g4xx_ll_usb.h:167`; array sizes at `stm32g4xx_hal_pcd.h:104-105`.

**What is wrong**

The standard endpoint request handler uses the low byte of `wIndex` without validating reserved bits, the endpoint number, or whether that direction exists. The bundled HAL does not compensate:

- `HAL_PCD_EP_SetStall()` tests `(ep_addr & 0x7) > dev_endpoints`. With `dev_endpoints = 8` that test never rejects anything.
- Its OUT branch then indexes `hpcd->OUT_ep[ep_addr]` **without masking**. That array has eight elements.
- GET_STATUS checks `is_used` with `ep_addr & 0xF`, but then indexes the arrays with `ep_addr & 0x7F` (`usb_ctrlreq.c:257-274`).

Examples:

- SET_FEATURE(ENDPOINT_HALT), endpoint recipient, `wIndex = 0x10`, `wValue = 0`, `wLength = 0` writes to `OUT_ep[16]`.
- GET_STATUS with `wIndex = 0x90` passes the EP0 usage check and then accesses `ep_in[16]` and `IN_ep[16]`.

These paths can corrupt state or fault without using any vendor command.

**Reachability (why the two ratings differ)** As far as Claude recalls, Linux usbfs refuses standard requests addressed to an endpoint the device does not declare, so an unprivileged Linux program cannot send these. Other operating systems and a malicious host can.

**Suggested fix** Validate endpoint requests centrally before any indexing: reject non-zero reserved address bits and high `wIndex` bits; require an implemented endpoint and direction; validate request-specific `wValue` and `wLength`. For this target the valid addresses are `0x00`, `0x80`, `0x81` and `0x02`, with request-specific restrictions on EP0. Also fix the HAL bound to `index >= array_count` and use the validated index consistently. Masking an invalid address into a different valid endpoint is not sufficient.

**Test** Enumerate all 256 endpoint-address values against GET_STATUS, SET_FEATURE and CLEAR_FEATURE in a simulated control handler. Invalid addresses must STALL without touching endpoint state. Repeat selected cases on hardware only after the fix.

---

### M2 — EP0 acts on the declared size, not on the bytes actually received

**Found by** Codex (N03); verified by Claude. **Severity** Codex: High. Claude: Medium. **Evidence** confirmed, host harness.

**Where** `Source/usb_core.c:134-155`; `Source/Candlelight/control.c:230-240`, `338-367`, `488-492`; HAL `stm32g4xx_hal_pcd.c:1790-1804`.

**What is wrong**

For a final EP0 OUT packet the USB core calls `EP0_RxReady()` solely because the expected remaining length is at most 64. It does not require the packet's actual size to equal that length. `ep0_buf` retains bytes from previous requests. The HAL copies every EP0 OUT packet in full, with no bound.

Consequences:

- A SET_BITTIMING declaring 20 bytes but delivering 4 executes with four new bytes and sixteen old ones, then reports success.
- A truncated flash-write command can persist old control-buffer contents as if the host had supplied them.
- Even after H3 is fixed, an allowed 2000-byte flash write is unsafe if its final packet has 64 bytes instead of the expected 16: the HAL copies to offsets 1984–2047 before any validation, and `ep0_buf` is 2008 bytes, so **40 bytes are overwritten**.

The extracted `USBD_LL_DataOutStage()` with a configured device and a four-byte final packet produced:

```text
EP0: expected=20 supplied=4 command_executed=1 success_status=1
```

**Reachability (why the two ratings differ)** A standard host controller always sends exactly `wLength` bytes in the data stage, so libusb or a kernel driver cannot produce a short or oversized data stage. This needs purpose-built host hardware.

**Suggested fix** Track expected and actual control-transfer lengths, reject premature short packets and inconsistent final packets, and never dispatch a command until its complete payload has arrived. Use a packet-sized staging buffer, or enforce the remaining-capacity bound before the PMA-to-RAM copy. Clear pending command state on a new SETUP or USB reset. The exact request sizes and `sizeof(ep0_buf)` checks from H3 remain necessary.

**Test** Truncate every control command at each field boundary; test a short middle packet and an oversized last packet (in a harness or with a programmable host). No command, flash operation or mode change may execute with incomplete data.

---

### M3 — 16-bit hardware timestamps are extended incorrectly

**Found by** Claude; atomicity refinement by Codex. **Evidence** code analysis.

**Where** `Source/can.c:341`, `can.c:372`, `Source/system.c:238-258`.

**What is wrong**

The FDCAN captures a 16-bit timestamp from TIM3 at *start of frame* (RM0440: "captured on start of frame (both Rx and Tx)"). The firmware later builds the 32-bit value with `(system_get_timewrap() << 16) | stamp`, using the wrap counter at the time the frame is *read*. If TIM3 wrapped between start-of-frame and the read, the upper half is one too high and the frame is stamped 65.536 ms in the future.

Probability per frame ≈ (frame duration + polling delay) / 65.536 ms. A classic 8-byte frame at 500 kbit/s lasts about 230 µs, so roughly 1 frame in 280 is affected; at 125 kbit/s roughly 1 in 65.

The firmware advertises `GS_DevFlagTimestamp`, and the Linux driver enables hardware timestamps whenever the feature is advertised, so this affects every Linux setup that reads hardware timestamps.

Related weaknesses:

- `system_get_timestamp()` (`system.c:248-252`) reads `timestamp_wrap` and `TIM3->CNT` non-atomically, and `timestamp_wrap` is not `volatile`.
- The wrap counter is driven by the FDCAN wrap interrupt, so it only advances while a channel is open, and only FDCAN1 is wired up (`system.c:230-234`).

**Suggested fix** (sketch, not a complete implementation)

```c
// consistent 32-bit "now"
uint32_t system_get_timestamp(void)
{
    uint32_t hi, lo;
    do { hi = timestamp_wrap; lo = TIM3->CNT; } while (hi != timestamp_wrap);
    return (hi << 16) | lo;
}

// extend a captured 16-bit stamp (valid while the frame is less than 65 ms old)
static uint32_t extend_stamp(uint16_t stamp16)
{
    uint32_t now = system_get_timestamp();
    uint32_t ts  = (now & 0xFFFF0000u) | stamp16;
    if (stamp16 > (uint16_t)now) ts -= 0x10000u;   // captured before the last wrap
    return ts;
}
```

What the final version must add:

- Count wraps with the TIM3 update interrupt instead of the FDCAN wrap interrupt, so the counter runs whether or not a channel is open and is independent of the CAN instance. Declare `timestamp_wrap` as `volatile`.
- Handle a pending, not-yet-served wrap. The read-twice loop only covers a wrap interrupt that *runs* between the reads. If the timer wraps while interrupts are masked (the list helpers do this), or the caller is itself in an interrupt of equal or higher priority, both reads return the old high word with a post-wrap low word. Check the timer's pending update flag and add one epoch when it is set and the low word is small.
- Account for the software-generated update event during timer initialisation (`system.c:222`).
- Define a maximum age for extending a captured stamp: a frame older than one 65.536 ms epoch cannot be dated from its low word.

**Test** `candump -H -t d can0` with a periodic sender; look for deltas that are off by ±65.5 ms. Check monotonicity around the 16-bit wrap, across idle and closed intervals, with delayed servicing, and over the 32-bit wrap (about 71 minutes).

---

### M4 — Errors in SETUP requests with OUT data never reach the host

**Found by** Claude. **Evidence** confirmed.

**Where** `Source/usb_core.c:150-153`, `Source/Candlelight/control.c:328-337`, `usb_class.c:480-484`.

**What is wrong**

After the data stage, `USBD_LL_DataOutStage()` calls `EP0_RxReady()` and then unconditionally `USBD_CtlSendStatus()`. The return value is ignored. The comment at `control.c:331-336` concludes that the HAL cannot stall at this point. It can: the stall is simply overwritten by the status ZLP that the core sends next.

Consequence for Linux, which never calls `ELM_ReqGetLastError`: these all look like success.

- `GS_ReqSetDeviceMode` start failing with `FBK_BaudrateNotSet`, `FBK_ErrorFromHAL` or `FBK_AdapterMustBeClosed`.
- Bit timing rejected by the `IS_FDCAN_*` range checks.

The interface is then "UP" but silent, with no message in `dmesg`.

**Suggested fix**

```c
// usb_core.c
if (pdev->pClass->EP0_RxReady(pdev) == USBD_OK) USBD_CtlSendStatus(pdev);
else                                             USBD_CtlError(pdev, NULL);
```

Have `control_setup_OUT_data()` return success or failure. A STALL in the status stage is the standard way to reject a request; Linux then gets `-EPIPE` and `ip link set up` fails visibly. `ELM_ReqGetLastError` can stay for the detailed code. If H6 is fixed by deferring to the main loop, do it there instead.

**Test** Start without a bit rate set, and with an out-of-range bit timing; `ip link set can0 up` must fail with an error.

---

### M5 — CAN reception depends on an uninterrupted main loop

**Found by** Claude. **Evidence** code analysis.

**Where** `Source/can.c:369`, `can.c:383` (polling), `Source/main.c:55-59` with `Source/led.c:87-108`, `Source/system.c:417-472`.

**What is wrong**

The G4 FDCAN has fixed 3-element RX FIFOs. They are drained by polling, one frame per main-loop pass. At 1 Mbit/s the shortest frames arrive every ~50 µs, so any stall above ~150 µs loses frames (reported only as `APP_CanRxFail`, see M9).

Stalls that exist today:

- `led_blink_power_on()` blocks for 1.2 s after power-on and after every USB resume. If the host brings the interface up during that time (udev rule, systemd-networkd), CAN is already open (opened from the ISR) and nothing drains the FIFO.
- Flash and option-byte programming: 22–25 ms each, allowed while CAN is open.
- Every USB interrupt, including the unused 1 kHz SOF interrupt (see L8), and `sprintf`-based debug messages.

**Suggested fix**

- Enable the FDCAN "new message" interrupts for RX FIFO 0/1 (and the TX-event FIFO) on an interrupt line with priority above USB. In the ISR, move frames into a software ring buffer; the main loop then only converts and forwards.
- Make the power-on blink non-blocking (a small state machine in `led_process()`).
- Refuse flash and option-byte writes while any channel is open (`FBK_AdapterMustBeClosed`).

**Test** Full bus load at 1 Mbit/s with short frames while the host opens the interface immediately after plug-in; while the host pauses reading; and during an ElmueSoft flash write. No frame loss, or a correctly reported loss.

---

### M6 — Session state is not reset on USB reset, re-enumeration or close

**Found by** Claude. **Evidence** code analysis.

**Where** `Source/usb_core.c:254-278`, `Source/Candlelight/usb_class.c:390-417`, `Source/can.c:110-129`, `Source/Candlelight/control.c:377-383`.

**What is wrong**

- **USB reset or re-enumeration does not close CAN.** After a host crash, a driver reload or a warm reboot, the channel is still open with the old bit rate and flags. A new `GS_ReqSetBitTiming` returns `FBK_AdapterMustBeClosed` and the following start returns the same; with M4 the host sees neither. The adapter keeps running at the old bit rate while the host believes the new one is active. Only USB suspend closes the channel.
- **The to-host queue is never cleared.** `can_open()` and `can_close()` only clear the CAN side (`buf_clear_can_buffer`). Up to 70 queued frames, plus one already armed in the IN endpoint with `TxBusy` still set, are delivered at the start of the next session with old timestamps. Stale echoes produce "Unexpected unused echo id" in the kernel log.
- **Suspend closes CAN silently** (`usb_lowlevel.c:86`) and does not restore it on resume. After resume the host's interface is still up and nothing tells it that the channel is closed.

**Suggested fix**

- In `USBD_LL_Reset()` and at the start of `USBD_GS_Init()`: request close of all channels, reset `GLB_ProtoElmue` and the user flags, clear both queues, clear `TxBusy`.
- At session boundaries in `can_open()` and `can_close()`, serialize teardown with USB callbacks, stop submissions, cancel the armed IN transfer, and clear `list_to_host`. Use a real endpoint abort/reset procedure that prevents the old PMA packet or a pending completion callback from crossing into the next session. Reset `TxBusy`, `SendZLP` and transfer bookkeeping only after cancellation is coordinated, then re-arm endpoints as appropriate.
- Do not rely on `USBD_LL_FlushEP()` for cancellation on this target. It calls `HAL_PCD_EP_Flush()`, which reaches `USB_FlushTxFifo()` / `USB_FlushRxFifo()` in `stm32g4xx_ll_usb.c:183-213`; both are compatibility stubs that return `HAL_OK` without changing hardware. Validate the chosen abort/reset sequence against the FS peripheral's endpoint status, pending interrupt and data-toggle behavior.
- Treat "start while open" as close-then-open, or reject it with a STALL (M4).
- Make sure queued records never cross a protocol or session change.

**Test** Kill a libusb host application without closing, restart it with a different bit rate; reload the kernel driver; USB reset; suspend/resume; legacy ↔ ElmueSoft transitions. Include a session change while an IN packet is armed and while its completion interrupt is pending. No stale packets or callbacks may affect the new session, and the bit rate on the bus must match the request.

---

### M7 — User-flash bounds check can erase part of the firmware

**Found by** Claude. **Evidence** confirmed from the map file (both builds have the same section sizes).

**Where** `Source/system.c:393-410`.

**What is wrong**

`system_get_flash_addr()` refuses a segment only if its start address is below `_etext`. In the linker script `_etext` marks the end of `.text` only. `.rodata`, `.ARM`, `.init_array` and the load image of `.data` come after it.

From this build's map: `_etext = 0x08008F98`, image end = `0x08009410`. Segment 45 starts at `0x08009000`, which is not below `_etext`, so it is accepted. That page holds the tail of `.rodata` and the complete `.data` initialisers. `ELM_ReqWriteFlash` with `wValue = 45` therefore erases them: constants change under the running firmware and the next boot starts with a blank `.data` image. Recovery needs the BOOT0 button.

The exact segment number depends on the compiler; the logic error does not.

**Suggested fix**

- Reserve a fixed user-data region in the linker script (for example the last 8 pages) and accept only segments inside it. The `FLASH` region is already limited to 126 K, so the top page is free today.
- Or, at minimum, export the true image end and compare against it, rounded up to a page:
  `_flash_image_end = LOADADDR(.data) + SIZEOF(.data);`
- Add the "must be closed" check from M5.

**Test** Request every segment number 0–255; only segments inside the reserved region may be accepted. Verify the firmware CRC after each rejected request.

---

### M8 — BOOT0 option-byte handling does not fit this board

**Found by** Claude. **Evidence** confirmed from the schematic.

**Where** `Source/system.c:292-366`, `Source/Candlelight/control.c:474-487`, `Source/dfu.c:31-45`.

**What is wrong**

The firmware assumes the usual CANable layout where BOOT0 is wired straight to CAN RX. WeAct solved that in hardware: U5 (74LVC1G125) isolates CAN RX from PB8/BOOT0, `/OE` is pulled high by R9 at reset, and R13 pulls BOOT0 low unless K1 is pressed. `CAN_RX_EN` (PB7) enables the buffer after boot, which `can_init()` does correctly.

On this board:

- `ELM_ReqSetPinStatus(PINID_BOOT0, PINOP_Disable)` programs nSWBOOT0 = 0. From then on the BOOT0 button, the only flashing method WeAct documents, is ignored. If the application is ever damaged in that state, only SWD (header P1) can recover the board. The ElmueSoft tools offer this as a button.
- `dfu_switch_to_bootloader()` programs option bytes from inside the USB ISR for no benefit.

Separately, `system_init()` programs the BOR level into the option bytes on first boot (`system.c:169`). The result is ignored, and the new level only takes effect after the next power cycle. This is harmless, but it is a write to option bytes on a board that does not need it; decide whether you want it.

**Suggested fix**

- For `WeActStudio`, return `FBK_UnsupportedFeature` for `OPT_BOOT0_Disable` and report BOOT0 as enabled.
- Remove the option-byte calls from the DFU path once H2 is fixed.

---

### M9 — Error frames do not match Linux conventions, and RX FIFO loss is hidden

**Found by** Claude; RX-overflow part by Codex (N07). **Evidence** confirmed.

**Where** `Source/Candlelight/buffer.c:361-467`, `Source/Candlelight/candlelight_def.h:242`, `candlelight_def.h:342`, `Source/error.c:55`, `Source/can.c:394-416`.

**What is wrong**

- **Hardware RX FIFO loss has no standard encoding.** FIFO loss asserts `APP_CanRxFail` (`can.c:400-412`). The legacy encoder maps only `APP_UsbInOverflow` to RX overflow (`buffer.c:430`); the comment at `buffer.c:428` says there is no legacy flag for `APP_CanRxFail`. The flag goes only into the private byte 5, and `FRM_Overflow` is never used. With no other error present, the resulting error frame has no standard class at all. This is the loss most likely to happen (M5), and the host cannot recognise it.
- `ERID_CRC_Error = 0x0200` is "added by ElmüSoft", but in Linux `can/error.h` bit `0x200` is `CAN_ERR_CNT` ("error counters valid in data[6], data[7]"). In legacy mode a CRC error is therefore announced as "counters valid".
- The counters are always written to `data[6]`/`data[7]` (`buffer.c:442-443`), but `CAN_ERR_CNT` is never set on purpose, so tools such as `candump -e` do not show them.
- Passive and warning flags are derived from "counter > 0" (`buffer.c:389-394`). With TEC = 128 and REC = 3 both "TX passive" and "RX passive" are reported.
- `CAN_ERR_RESTARTED` is never sent after bus-off recovery. Linux happens to return to error-active through the `CAN_ERR_CRTL_ACTIVE` path, so this is cosmetic.
- Reading `FDCAN_PSR` resets the last-error-code field to "no change" (RM0440). PSR is read twice per loop: in `error_is_report_due()` (`error.c:55`), which uses the code, and in `can_process()` (`can.c:416`), which discards it. Any protocol error latched between the two reads is lost.

**Suggested fix**

- Map hardware RX FIFO loss, as well as host-queue loss, to `CAN_ERR_CRTL` with `CAN_ERR_CRTL_RX_OVERFLOW`, and/or the gs_usb frame overflow flag. Keep the private detail for ElmueSoft. Keep a sticky loss counter until reporting succeeds (M10).
- Legacy mode: report CRC errors as `CAN_ERR_PROT` with `data[3] = 0x08` (CRC sequence), set `CAN_ERR_CNT` whenever the counters are filled in, and keep `0x200` out of other uses.
- Derive warning/passive per direction from thresholds (≥ 96, ≥ 128) and the RP flag.
- Read PSR once per loop pass, accumulate LEC/DLEC into the error state, and share the result.

**Test** Deliberately overrun FIFO 0 while the host keeps reading: require a recognisable standard receive-overflow notification and a monotonic internal loss counter. Check `candump -e` output for CRC errors and counters.

---

### M10 — A failed error enqueue is treated as a successful report

**Found by** Codex (N08); verified by Claude. **Severity** Codex: Medium. Claude: Low–Medium (needs a host that is not reading). **Evidence** confirmed.

**Where** `Source/error.c:89-93`, `122-125`; `Source/Candlelight/control.c:501-508`; `Source/Candlelight/buffer.c:365-367`; `Source/can.c:474-503`.

**What is wrong**

`error_is_report_due()` updates `last_tick`, copies `cur_state` into `last_state` and clears `report_now` **before** `buf_store_error()` obtains storage. If the host pool is exhausted, `buf_store_error()` simply returns. The report was never queued, but the bookkeeping says it was.

For bus-off, automatic recovery still proceeds, and a fast recovery then clears the error state. The host can miss the bus-off event completely and later see only the recovery. Other unchanged errors may wait three seconds for a retry. The comment promising that bus-off is reported before recovery is not guaranteed.

**Suggested fix** Make the enqueue return a result, and commit the reported snapshot and time only after a frame was queued. Keep important transitions separately from the latest sampled state, so a quick recovery cannot erase an unreported bus-off. Reserve a little pool capacity for error events, or keep a pending event until space exists. Protect taking and clearing error snapshots against interrupt-side flag updates as part of H6.

**Test** Fill the host pool, trigger bus-off, allow recovery, then resume host reads. Both transitions must appear, in order.

---

### M11 — Bus-off recovery cannot fail gracefully

**Found by** Claude. **Evidence** confirmed.

**Where** `Source/can.c:474-503`.

**What is wrong**

`HAL_FDCAN_Stop()` and `HAL_FDCAN_Start()` return values are ignored. `HAL_FDCAN_Stop()` waits for the INIT acknowledge with a loop counter of only 10 iterations (`FDCAN_TIMEOUT_VALUE`); on timeout it sets the handle state to ERROR and `Start` then fails. Because `recover_bus_off` is already true, recovery is never attempted again. The channel stays in bus-off until the host closes and reopens it, and the host is only told "Start recovery".

Also, frames the host sends during the bus-off window are discarded without an echo (H1), and the pending counter is left stale (M12).

**Suggested fix** Check both results. On failure clear `recover_bus_off` so the next pass retries, or fall back to a full `HAL_FDCAN_DeInit()` / re-init using the stored configuration. Report a failed recovery to the host.

---

### M12 — The software pending-TX count diverges from the hardware

**Found by** Codex (N09); verified by Claude, who added the link to H1. **Evidence** confirmed; the runtime effect depends on cancellation and event loss.

**Where** `Source/can.c:304-308` (increment), `356-360` (decrement), `394-398` (TX event lost), `424-430` (timeout), `487-489` (bus-off abort).

**What is wrong**

`tx_pending` increments when a frame is inserted and decrements only when a TX event is retrieved. Bus-off recovery aborts all three hardware TX buffers but does not reconcile the count. A lost TX-event FIFO entry likewise leaves a stale positive count until a timeout or explicit reset clears it.

Example: three queued frames followed by bus-off cancellation can leave `tx_pending == 3` after recovery, with nothing pending in hardware. Once 500 ms elapses without a new FIFO insertion or retrieved TX event refreshing `last_tx_tick`, the timeout fires: it reports `APP_CanTxTimeout` for requests that no longer exist and calls `buf_clear_can_buffer()`. If unechoed frames remain in that software queue, they are discarded and their Linux TX contexts leak (H1). An empty queue loses no frames, so additional slot leakage is conditional rather than an inevitable consequence of every bus-off. Successful later traffic increments and decrements around the stale baseline and refreshes the timeout without correcting the count.

**Suggested fix** Track hardware-buffer ownership and completion or cancellation, or reconcile the pending state against `TXBRP` / `TXBTO` / `TXBCF` and event-FIFO loss. On a controlled cancellation or reset, complete or cancel every corresponding software record and reset the timeout bookkeeping. Do not interpret a missing event as proof that a transmission is still pending. Coordinate with the echo fixes in H1.

**Test** Cancel pending TX through bus-off, restore a healthy bus and wait beyond 500 ms: no timeout may be reported for cancelled records. Repeat with a deliberate TX-event FIFO overflow.

---

### M13 — Frames and settings from the host are not validated

**Found by** Claude; extended by Codex. **Evidence** confirmed.

**Where** `Source/Candlelight/buffer.c:139-158`, `181-189`; `Source/Candlelight/usb_class.c:515-553`; `Source/Candlelight/control.c:358`, `364`.

**What is wrong**

- `can_dlc` is taken from the host unchecked (`buffer.c:157`) and passed to the HAL as `DataLength`. The HAL indexes `DLCtoBytes[16]` with it and shifts it into the TX element word, so a value above 15 reads past the table, sets unrelated control bits (BRS, FDF, event control) and copies up to 255 bytes into the message RAM. `assert_param` is compiled out.
- The received length of an OUT transfer is never checked. `usb_class.c:541` always copies 80 bytes from `from_host_buf`. A short or truncated packet is processed with leftover bytes from earlier frames.
- ElmueSoft protocol: `header.size` is trusted (`buffer.c:139`). A value below 8 gives a negative byte count, which `utils_byte_count_to_dlc()` turns into DLC 15, sending 64 bytes of leftover data.
- FD flag combined with RTR is not rejected.
- A host frame with the error flag set (`CAN_ID_Error`) is stripped to its ID bits and sent as an ordinary data frame (`buffer.c:181-189`).
- Bit timing: `timing->prop + timing->seg1` (`control.c:358`, `364`) can overflow 32 bits before the range check, so a wrapped value can pass. Only individual register ranges are checked, not the relationship of SJW to the segments.

**Suggested fix** Validation must be format-aware and happen at several layers:

- Transport: buffer capacity before copying (H4), actual control lengths (M2).
- Record: in `USBD_GS_DataOut()` take `USBD_LL_GetRxDataSize()` and drop packets shorter than the header plus the payload implied by DLC and flags. Reject `can_dlc > 15`, FD + RTR, the error flag, and an inconsistent `header.size`.
- Semantics: classic DLC handling (M16), deterministic FD padding (M17).
- Settings: check `prop + seg1` for overflow, and SJW ≤ min(seg1, seg2).

In legacy mode still send the echo (H1) together with an error frame.

**Test** Fuzz the bulk OUT record fields and the bit-timing request in a host harness first, then on hardware once the memory bounds are fixed.

---

### M14 — Bit timing is erased when the channel closes

**Found by** Claude; kernel behaviour established by Codex and re-verified. **Severity** Medium; High with affected drivers, verified in upstream v6.18. **Evidence** confirmed (firmware and kernel source).

**Where** `Source/can.c:94-95`, used at `can.c:117-118`.

**What is wrong**

`can_reset()` zeroes the nominal and data bit timing on every close. A host that sets the bit rate once and then cycles down/up gets `FBK_BaudrateNotSet` on the second open, invisibly (M4).

Which kernels are affected:

- Verified in [upstream Linux v6.18](https://raw.githubusercontent.com/torvalds/linux/v6.18/drivers/net/can/usb/gs_usb.c): bit timing is sent when configured through netlink (`do_set_bittiming` callback); `gs_can_open()` does not resend it, and [`open_candev()`](https://raw.githubusercontent.com/torvalds/linux/v6.18/drivers/net/can/dev/dev.c) only validates the saved timing. With that implementation, `ip link set can0 down; ip link set can0 up` without a new timing request leaves the adapter closed while the interface shows UP.
- [Mainline inspected during this review](https://raw.githubusercontent.com/torvalds/linux/master/drivers/net/can/usb/gs_usb.c) calls `gs_usb_set_bittiming()` (and the data variant for FD) inside `gs_can_open()`.
- **The review machine** runs `7.0.0-43-generic`. Disassembly of its installed `gs_usb.ko` confirms nominal and FD data timing requests inside `gs_can_open()`, so this specific timing-retention failure does not affect that driver. This conclusion is based on the call path, not merely the presence of an error-message string.
- Other releases and distribution kernels must be checked by driver implementation and backports. The introduction commit and complete affected release range were not established; the v6.18 result does not prove that every earlier kernel behaves identically.

Other gs_usb hosts may also omit timing requests on reopen.

The data bit timing is also what switches FD mode on (`can.c:924-927`). Clearing it on close is what makes "fd off" work, so this cannot simply be removed.

**Suggested fix** Keep the nominal timing across close. Decide FD mode from the `GS_DevFlagCAN_FD` bit in the start request rather than from "data timing present", and keep the data timing as well.

**Test** Repeated down/up with and without resending the timing; FD → classic → FD transitions.

---

### M15 — BRS is silently removed when the two configured bit rates are equal

**Found by** Codex (N04); verified by Claude. **Severity** Codex: Medium. Claude: Low–Medium (the behaviour is deliberate and documented in `can.c:149-151`, but the side effects are real). **Evidence** confirmed; wire-level verification pending.

**Where** `Source/can.c:156-163`, `287-294`, `930-934`; `Source/Candlelight/buffer.c:191-198`, `234`.

**What is wrong**

`can_using_BRS()` returns true only when the calculated data bit rate is **greater than** the nominal one. `can_open()` then selects `FDCAN_FRAME_FD_NO_BRS` for equal rates, and `can_send_packet()` clears `tx_header->BitRateSwitch` even when the host explicitly requested BRS.

Equal bit rates do not make the BRS bit meaningless: it selects the data-phase timing, which may have a different sample point and SJW. The request and the frame on the wire differ. The legacy echo is copied from the unmodified host frame, so it still reports BRS set.

**Suggested fix** When the channel is opened in CAN FD mode, enable the controller's BRS capability and honour each frame's BRS flag. Validate the timing relationship separately. Do not infer a frame flag from comparing two rounded bit-rate calculations. If a configuration is deliberately unsupported, reject it visibly instead of changing the frame.

**Test** Nominal and data bit rate both 500 kbit/s, with identical and with different timing tuples. Send otherwise identical FD frames with BRS off and on, and inspect them with an independent receiver or analyser. Received BRS and echo metadata must agree with the request.

---

### M16 — Classical CAN DLC 9–15 is confused with CAN FD length

**Found by** Codex (N05); verified by Claude. **Severity** Codex: Medium. Claude: Low–Medium. **Evidence** confirmed. In Classical CAN these DLC codes mean eight data bytes, not 12–64.

**Where** `Source/Candlelight/buffer.c:203-206`, `280-310`; `Source/utils.c:62-78`; `Source/can.c:978-995`; HAL `stm32g4xx_hal_fdcan.c:2319-2341`, `3524-3530`.

**What is wrong**

Three forms of the same length mistake:

- **Classic-only mode, TX:** `buf_process_can()` rejects every DLC above 8, even without FDF. It then still emits a normal legacy echo although no frame was sent. On Linux this only triggers when the user enables `cc-len8-dlc on`; the driver advertises `CAN_CTRLMODE_CC_LEN8_DLC` (confirmed in v6.18 and mainline).
- **FD enabled, TX:** classic frames with those DLCs pass, and the HAL copies the FD-mapped byte count into message RAM. The frame on the wire is correct (eight bytes); the extra copy is harmless.
- **ElmueSoft RX:** a classic frame with DLC 15 becomes a 64-byte payload. The HAL reads the whole corresponding region of the RX element, so bytes beyond the eight-byte payload are old message-RAM contents exported to the host. The bus-load calculation likewise counts 64 payload bytes for an eight-byte frame.

**Suggested fix** Calculate payload length from both FDF and DLC: zero for RTR, `min(dlc, 8)` for classic data, the FD lookup only for FD data. Keep the raw DLC separately where the protocol supports it. Use a zeroed 64-byte TX staging area when calling a HAL that always uses the FD table. ElmueSoft needs either an explicit raw-DLC field (protocol version extension) or a documented normalisation.

**Test** Classic DLC 8–15 in classic-only and FD-enabled sessions, plus RTR. Verify exactly eight data bytes on the wire, raw DLC behaviour, no stale payload exported, and truthful TX completion.

---

### M17 — ElmueSoft's rounded FD lengths send bytes from a previous USB transfer

**Found by** Codex (N06); verified by Claude. **Severity** Codex: Medium. Claude: Low–Medium (ElmueSoft protocol only). **Evidence** confirmed. Distinct from the malformed-size case in M13.

**Where** `Source/Candlelight/buffer.c:131-148`, `200-209`; `Source/utils.c:80-90`; `Source/Candlelight/usb_class.c:539-551`; HAL `stm32g4xx_hal_fdcan.c:3524-3530`.

**What is wrong**

A valid ElmueSoft TX frame with nine payload bytes becomes DLC 9, which is twelve bytes on the bus. Nothing initialises bytes 9–11. The USB receive buffer is reused and the callback copies all 80 bytes regardless of the received length, so the extra three bytes come from the previous USB transfer. The same happens at other lengths between two FD sizes, such as 17 → 20 or 49 → 64.

This can expose bytes of earlier CAN frames on the bus and makes the padding non-deterministic. Rejecting malformed sizes alone does not fix a correctly sized nine-byte request.

**Suggested fix** Decide the protocol policy explicitly: either reject payload sizes that CAN FD cannot represent, or zero-pad to the next size. If padding is supported, clear the whole staging payload before copying the supplied bytes, and validate the header size against the actual transfer size first.

**Test** Send 64 bytes of a recognisable pattern, then 9, 13, 17, 21, 25, 33 and 49 fresh payload bytes. The rounded tail must be the documented padding, never bytes from the previous record.

---

### M18 — Bus-load arithmetic wraps before dividing

**Found by** Codex (N10); recomputed by Claude. **Severity** Codex: Medium. Claude: Low–Medium (ElmueSoft telemetry only). **Evidence** confirmed, host harness.

**Where** `Source/can.c:522-529`; field types at `Source/can.h:79-83`.

**What is wrong**

Both operands of `bit_count_total * nom_bit_len_ns` (`can.c:525`) are `uint32_t`. The product is accumulated bus time in nanoseconds and overflows above about 4.295 seconds, although report intervals up to ten seconds are accepted.

Example: 500 kbit/s, ten-second interval, 2,500,000 counted nominal bits:

```text
mathematical product: 2,500,000 × 2,000 = 5,000,000,000 ns
32-bit product:                            705,032,704 ns
existing formula including stuffing:       7 %
same formula with wide arithmetic:        56 %
```

The unchanged `can_timer_100ms()` in a host harness emitted `busload_report=7`. This is deterministic arithmetic corruption, separate from the documented approximation of the stuffing factor.

**Suggested fix** Cast before the multiplication and keep 64-bit intermediates through the normalisation. Normalise against the time actually elapsed (L1). Check all later products and choose the rounding deliberately. Keep the "estimate" label: counting successful frames with an assumed stuffing factor is not exact bus occupancy.

**Test** Compare against a 64-bit reference for every allowed interval, several bit rates and 0–100 % load, especially just below and above 4.295 seconds of accumulated bus time.

---

### M19 — Build dependencies and `clean` can leave obsolete firmware objects

**Found by** Codex (N12); both dry runs reproduced by Claude. **Severity** Medium for development; no effect on a shipped binary built from scratch. **Evidence** confirmed.

**Where** `GCC_Rules.mk:123-124`, `140-141`, `169`, `175-182`, `188-193`.

**What is wrong**

The compile rules depend on the `.c` / `.s` inputs but do not generate or include header dependencies. A change to pin definitions, queue sizes, protocol structure layout or HAL configuration in a header does not trigger recompilation. The makefiles and the linker script are also missing as prerequisites.

After a successful build:

```sh
make -n -f Make_G431_Candle_WeActStudio BUILD_DIR=<dir> -W Source/settings.h
```

prints `Nothing to be done for 'all'.`

`clean` removes only the top-level object files and final artefacts. It leaves `$(BUILD_DIR)/Candlelight/*.o` and the HAL library. So even `make clean && make` can link freshly compiled common code against firmware-specific objects built with an old header layout. `Build.cmd` works around this on Windows by deleting the build folders; a plain `make` on Linux does not.

**Workaround until fixed** Delete the whole build directory before every build.

**Suggested fix** Use `-MMD -MP` and include the dependency files for application and HAL objects; add the linker script as a prerequisite of the link; make configuration and compiler-flag changes invalidate the affected objects. Make `clean` remove all application objects, with a separate, clearly named full clean for the HAL. Keep build directories separate per configuration (already the case).

**Test** After an initial build, change in turn a shared header, a Candlelight-only header, a HAL config header, a build define and the linker script. Verify the expected rebuild and relink. Verify that a full clean leaves no object files.

---

### M20 — Declared USB current is too low for this board

**Found by** Claude. **Evidence** confirmed from the datasheet; the board's real consumption was not measured.

**Where** `Source/Candlelight/usb_class.c:131` (`0x4B` = 150 mA).

CA-IS2062A datasheet, logic-side supply current: 105 mA typical, 150 mA maximum while dominant into 60 Ω; 10/20 mA recessive. Add the MCU at 160 MHz in boost mode and three LEDs, and the board can exceed the 150 mA it declares.

Related: in USB suspend the firmware spins in a busy loop at 160 MHz (`main.c:42-47`). The USB limit for a suspended device is 2.5 mA. The isolated DC/DC converter cannot be switched off on this hardware, so the limit cannot be fully met, but the MCU part can be reduced.

**Suggested fix** Set the descriptor from a measured or calculated board budget through a board-specific define in `settings.h`; 250 mA (`0x7D`) is a reasonable proposal, not a verified figure. Optionally enter Stop or low-power run mode while suspended.

---

### M21 — No watchdog, and faults hang forever

**Found by** Claude. **Evidence** confirmed. Both reviewers agree this is a robustness improvement rather than a defect in CAN traffic; the infinite fault loops are real.

**Where** `Source/interrupts.c:18-44`, `Source/main.c:27-31`.

`HardFault_Handler` and the other fault handlers are empty infinite loops, initialisation failure is another one, and there is no IWDG. Combined with H3, H4 and H6, a single memory corruption leaves the adapter dead until it is unplugged.

**Suggested fix** Enable the IWDG (about 1 s) and refresh it once per main-loop pass. Remove the blocking 1.2 s blink first (M5), keep maintenance operations within the timeout, and never refresh from an ISR. In the fault handlers, store a reason code in a `.noinit` variable and call `NVIC_SystemReset()`; report the reason to the host after restart. A watchdog improves recovery; it does not repair the causes of corruption.

---

### M22 — A clear-halt request on the bulk IN endpoint stops all traffic to the host

**Found by** Claude (second pass). **Evidence** code analysis.

**Where** `Source/usb_ctrlreq.c:225-234`; HAL `USB_EPClearStall()` at `stm32g4xx_ll_usb.c:653`; `Source/Candlelight/usb_class.c:676-686` and `713`.

**What is wrong**

`CLEAR_FEATURE(ENDPOINT_HALT)` for endpoint `0x81` ends in `USB_EPClearStall()`, which sets the endpoint's TX status to NAK unconditionally, whether or not it was halted. If a frame is armed at that moment, it is cancelled. The transfer-complete interrupt never comes, `USBD_GS_DataIn()` never runs, and `TxBusy` stays true. From then on the firmware sends nothing to the host until the device is reconfigured.

The Linux kernel driver does not send this request. Some libusb-based programs call `libusb_clear_halt()` routinely when they open a device, and because of M6 there is often a stale frame armed at exactly that time.

**Suggested fix** After a clear-halt on a data endpoint, tell the class: for IN, clear `TxBusy` and `SendZLP` (dropping or re-queuing the cancelled frame); for OUT, re-arm the receive. Clearing the queues on open (M6) removes the usual trigger.

**Test** With RX traffic running, issue `libusb_clear_halt()` on `0x81`; traffic to the host must continue.

---

## 4. Low severity and cleanup

### L1 — Bus-load interval state is not restarted with the measurement

**Found by** Codex (N11); verified by Claude. **Severity** Codex: Low–Medium. Claude: Low. **Evidence** confirmed.

**Where** `Source/can.c:90-105`, `166-178`, `507-542`, `889-896`; `Source/main.c:67-72`.

`can_reset()` clears `busload_interval`, but not `bit_count_total`, `busload_counter` or `old_busload_pct`. `can_enable_busload()` only changes the interval. A disable/re-enable, close/reopen or interval change can therefore combine old traffic with the new interval, and even with a new nominal bit rate.

The timer also checks the counter before incrementing it, so the first report after enabling comes one period late and covers one period too many. The main loop calls the timer once after a delay rather than accounting for missed periods, and the denominator stays the nominal interval even when sampling was delayed.

**Suggested fix** Initialise all measurement state together on enable, open and interval change; record a real start timestamp; compare elapsed time with the interval; report using the duration actually measured. Decide whether an interval change discards or finalises the partial measurement.

**Test** Disable halfway through a busy interval, reopen at a different bit rate and leave the bus idle: no old traffic may show up. Add an artificial main-loop delay and verify the normalisation.

### L2 — Opened bulk endpoints are never marked in use in the USB core

**Found by** Codex (N13); verified by Claude. **Evidence** confirmed.

**Where** `Source/Candlelight/usb_class.c:390-417`; `Source/usb_ctrlreq.c:257-272`; `Source/usb_core.c:258`, `264`.

GET_STATUS checks `pdev->ep_in/out[index].is_used`. EP0 is marked used during reset, but the Candlelight class opens `0x81` and `0x02` without setting their flags. A standard GET_STATUS for either valid bulk endpoint therefore stalls while the endpoint is active. Ordinary CAN traffic works; this affects endpoint inspection, compliance tests and recovery software that checks halt status.

**Suggested fix** Set the usage flag only after a successful open, and clear it on close, deconfiguration and reset. Keep it consistent with the endpoints in the descriptor. Combine with M1's validation so a correct flag does not enable an invalid alias address.

**Test** GET_STATUS must succeed for each configured bulk endpoint, reflect a requested halt, and be rejected for an endpoint that is not in the current configuration.

### L3–L18 — Table

| ID | Where | Problem | Suggested fix |
|---|---|---|---|
| L3 | `can.c:424`, `led.c:190` | Tick arithmetic breaks when the 32-bit millisecond tick wraps (49.7 days): `tick_now >= last + 500`, `tick_now >= next_blink`. | Use differences: `(int32_t)(tick_now - last_tx_tick) >= 500`. `last_tx_tick` can be 1 ms ahead of the cached `tick_now`, so use a signed compare or re-read the tick. |
| L4 | `led.c:261-264`, called from `led.c:70`, `81`, `97`, `179` | Other boards only, but a regression from the WeAct commit: when `LED_RDY_PINS` is `-1`, `HAL_GPIO_WritePin(GPIOA, 0xFFFF, …)` writes all 16 pins of port A on every loop pass. | Guard `SetRdyLed()` with `if (SET_LedRdyPins[channel] < 0) return;`. Not needed for the WeAct build, needed before sending anything upstream. |
| L5 | `can.c:866`, `can.c:876` vs `can.c:56` | "No pin" is tested as `<= 0` in some places and `>= 0` in others. Valid pin masks are positive, so this is not a demonstrated failure on this board. | Use one sentinel and one test everywhere. |
| L6 | `settings.h:21-25` | Home-made `bool` enum. It is 1 byte (short enums), so values such as `0x100` become `false` (`led_blink_identify(channel, *mode)`, `control.c:441`). It also fails to compile in C23, the default from GCC 15 onward (checked with `-std=gnu23`). | `#include <stdbool.h>`; set the language standard explicitly in the makefile. |
| L7 | `usb_ctrlreq.c:135-138`, `control.c:217-224` | `ELM_ReqWriteFlash` with `wLength = 0` ("erase only") never executes: there is no data stage, so `EP0_RxReady` is not called, yet the request reports success. | Handle zero-length OUT requests directly in the setup stage. |
| L8 | `usb_lowlevel.c:151` | SOF interrupt enabled but no SOF callback is used: 1000 needless interrupts per second. CRS does not need it. Not itself evidence of data loss. | `Sof_enable = DISABLE`. |
| L9 | `usb_ctrlreq.c:664` | `uint8_t idx` in `USBD_GetStringDescr()` wraps for strings longer than 127 characters. Latent: current strings are short. | Use `uint16_t`, bound it against `sizeof(USBD_StrDesc)`, and enforce the USB descriptor length limit (255 bytes). |
| L10 | `usb_ctrlreq.c:111-154`, `581-588`, `225-234` | Unhandled standard requests (interface GET_STATUS, SET_FEATURE other than remote wakeup, CLEAR_FEATURE with other selectors) get no answer, so the host times out instead of receiving a STALL. SET_INTERFACE with any alternate setting is accepted. Remote wakeup is accepted although the descriptor does not offer it. | Stall everything that is not explicitly handled. See also M1 and L2. |
| L11 | `usb_class.c:594-644` | The Microsoft OS descriptor path bypasses `USBD_LL_SetupStage()`, so `ep0_data_len` is stale and an extra ZLP may be armed for the 64-byte feature descriptor. `bmRequestType` is not checked. Windows only; the host-visible symptom needs Windows testing. | Set `ep0_state` and `ep0_data_len` in that path; check type and direction. |
| L12 | `control.c:468-473`, `control.c:395-402` | A bus-load interval set under the ElmueSoft protocol while the channel is closed survives a legacy start; bus-load messages are then sent to a legacy host, which parses them as CAN frames. `usb_class.c:673` also indexes `GLB_UserFlags` with a channel byte from such frames. | Reset `busload_interval` when `GLB_ProtoElmue` is cleared; clamp the channel index. Covered by the session isolation work in M6. |
| L13 | `can.c:193-233` | TDC is enabled for any data prescaler. The Bosch M_CAN manual limits the data prescaler to 1 or 2 when TDC is on (RM0440 does not repeat this; verify for this controller). The SSP is fixed at 50 %. The presets and limits allow 8–10 Mbit/s, but the CA-IS2062A is specified to 5 Mbit/s with 255 ns maximum loop delay. | Enable TDC only when the data prescaler is ≤ 2; consider placing the SSP at the data sample point; document the 5 Mbit/s board limit. Do not treat an occasional high-rate success as support. |
| L14 | `system.c:79-120`, `main.c:27-31` | If the crystal does not start, the firmware stops with all LEDs on. No fallback to HSI, no clock security system. A robustness enhancement; stopping is deliberate. | Fall back to HSI16 as PLL source and report it in `BoardFlags`; optionally enable CSS. |
| L15 | `system.c:75`, `169`; `usb_lowlevel.c:156`; `can.c:195`, `248` | Return values ignored (`HAL_PWREx_ControlVoltageScaling`, `system_set_option_bytes`, `HAL_PCD_Init`, `HAL_FDCAN_DisableTxDelayCompensation`, `HAL_FDCAN_ConfigGlobalFilter`). `can_open()` leaves the handle half-initialised when a later step fails. | Check them; on a late failure in `can_open()` call `HAL_FDCAN_DeInit()`. |
| L16 | `led.c:200-211`, `error.c:47-48`, `main.c:44` | App error flags are only cleared when a report is sent, and reports stop while the channel is closed, so the TX and RX LEDs can stay on until the next open. In suspend all three LEDs are off, including READY, so the board looks unpowered (a product choice; the comment in `main.c` expects a hard-wired red power LED). | Clear app flags on close; consider a dim or slow READY indication in suspend. |
| L17 | `can.c:277` | `HAL_FDCAN_Stop()` is called without waiting for an ongoing transmission. This is expected to truncate the frame on the bus; it was inferred from the source, not measured. | Decide a stop policy (abort pending TX and wait briefly for idle), and confirm with a wire capture. |
| L18 | various | Stale or wrong comments: LED colours (on this board RX = green PA0, TX = blue PA1, READY = red PA2); `can.h:17`, `35` say "always samplepoint 87.5 %"; `usb_class.c:435`, `633` say stack buffers cannot be used with `USBD_CtlSendData()` while `control.c:120`, `266`, `293` do exactly that (it works only because responses of ≤ 64 bytes are copied to the PMA immediately). `system.h:76-79` defines non-static weak syscall stubs in a header with wrong signatures, which is a real ABI problem if they are ever called. `USBD_DetachAttach()` is unused. `-O3` is used although size is not a constraint (an engineering choice). | Tidy up when touching the files. |

---

## 5. Checked and found correct

So you know these were looked at, not skipped:

- **Pin mapping against the schematic:** PA0 RX LED, PA1 TX LED, PA2 READY LED (all active low, matches `LED_ON = GPIO_PIN_RESET`), PB8 FDCAN1_RX, PB9 FDCAN1_TX (AF9), PB7 `CAN_RX_EN` active low and driven low in `can_init()`.
- **Clock arithmetic:** 16 MHz ÷1 ×20 = 320 MHz VCO, ÷2 = 160 MHz SYSCLK and FDCAN clock; flash latency and boost mode are set; TIM3 prescaler gives 1 µs.
- **USB descriptors:** lengths and total length (50 bytes) are consistent; the DFU functional descriptor is well formed; PMA layout fits (376 of 1024 bytes); ZLP handling on the bulk IN endpoint is correct.
- **gs_usb compatibility:** request numbers, feature bits and the layouts of `kDeviceVersion`, `kCapabilityClassic`, `kCapabilityFD`, `kBitTiming`, `kDeviceMode` and `kHostFrameLegacy` match the Linux driver. Frame sizes sent (20/24/76/80 bytes) match what it expects.
- **Interrupt priorities:** SysTick 0, FDCAN 0, USB 2, so `HAL_GetTick()` advances inside the USB ISR and HAL timeouts work there.
- **Memory:** about 17.6 kB of 32 kB RAM is static; roughly 15 kB remains for the stack.
- **HAL:** version 1.2.5, `DataLength` is used as a plain DLC code consistently.

---

## 6. Suggested order of work

1. **Reliable builds: M19.** Do this before touching shared structures, so changes cannot silently link against old object layouts. Until then, delete the build directory before every build.
2. **Memory safety: H3, H4, M1, M2, M13.** Bound `wLength`, size the bulk receive buffer, validate endpoint addresses, check actual lengths, validate records. Check bounds before copying, not only after parsing. Exercise malformed transfers in a host harness before fuzzing hardware.
3. **Ownership and lifecycle: H6, M4, M6, H1, M12, M10.** PRIMASK save/restore, request execution in the main loop with a STALL on failure, session teardown, back-pressure and an echo for every frame, real TX ownership, reliable error reporting. Queued records must never cross a protocol or session change, and a failed open must reach the host.
4. **Basic board correctness: H7, H5, H2, M8, M7.** Enable the CRS clock and fix the trim, fix the FDCAN clock relation, proper DFU entry, drop the BOOT0 option-byte handling, flash boundaries. Check clocks and bit rates and test recovery on the real board.
5. **Protocol fidelity: M14, M15, M16, M17, M9, M11.** Timing retention and FD mode flag, BRS, classic DLC, padding, standard error encoding, bus-off recovery.
6. **Timing and throughput: M3, M5, M18, L1.** Timestamps, interrupt-driven RX and non-blocking blink, bus-load telemetry.
7. **The rest: M20, M21, M22, L2–L18.** USB standards cleanup and optional recovery features.

Keep an SWD probe on header P1 available while working on H2, M7 and M8.

### Minimum board regression matrix after fixes

- Classic and FD at the rates the board supports; listen-only; internal and external loopback; one-shot and retransmission; standard and extended IDs; RTR; classic DLC 9–15; every FD DLC.
- Equal-rate BRS on/off with independently observed frames.
- Host reader pauses, host queue exhaustion, no-ACK TX, bus-off and recovery, and error-event delivery under congestion.
- Repeated down/up with and without resending the timing; FD → classic transitions; USB reset and deconfiguration; suspend/resume; unplug/replug; legacy ↔ ElmueSoft transitions.
- Timestamp monotonicity around the 16-bit wrap, across idle and closed intervals, with delayed servicing, and over the long-run 32-bit wrap.
- Flash and option-byte maintenance only under a defined closed-channel policy; exact flash boundaries; recovery from an interrupted update.
- Controlled load measurements across all report intervals; invalid USB request and packet sizes once the memory bounds are fixed.
- Enumeration cold and warm, on a direct port and through a hub; software DFU entry and the BOOT0 button.

---

## 7. Verification record

```text
Compiler: arm-none-eabi-gcc 14.2.1 20241119
WeAct Candlelight build: successful, no warnings (both reviewers)
Size: text 37332 / data 532 / bss 18640
Map: _etext 0x08008F98, image end 0x08009410, ep0_buf 0x20000d58 (0x7d8), buf_inst 0x200015e8 (0x2ed4)
Disassembly: dfu_timer_100ms ends in "bx r2" with r2 = 0x1FFF0004 (same in WeAct's shipped HEX)
Disassembly: system_init never sets RCC_APB1ENR1.CRSEN
Extracted bulk receive function: 128 bytes copied; 48 beyond the 80-byte storage
Extracted EP0 receive-stage function: expected 20, supplied 4; command and success status executed
Extracted bus-load timer: 7 % for a case whose wide-arithmetic result is 56 % (recomputed independently)
Header dependency dry run: no rebuild for a simulated settings.h change (reproduced)
Clean dry run: Candlelight and HAL object directories are retained (reproduced)
Kernel source: v6.18 gs_can_open() does not resend bit timing; mainline does; GS_MAX_TX_URBS = 10 in both
Review machine: kernel 7.0.0-43-generic; installed gs_usb.ko disassembly confirms nominal and FD data timing requests inside gs_can_open()
```

The harnesses model PMA and register inputs and callbacks; they are not full peripheral emulators. No malformed transfers were sent to a real adapter. No hardware tests of any kind were performed.

Corrections made while merging:

- Claude's original H4 fix (option B) wrongly claimed `FDCAN_CLOCK_DIV2` updates the reported CAN clock and that 10 Mbit/s is unreachable at 80 MHz. Corrected in H5.
- Claude's original M10 assumed all current kernels resend the bit timing. Corrected in M14 with the verified kernel behaviour.
- Claude's timestamp snippet and IRQ-lock suggestion were incomplete. Caveats added in M3 and H6.
- Codex's line references for `utils.c` (N05, N06) were wrong. Corrected in M16 and M17. Other line references were aligned with the checkout.
- Codex's statement that the installed kernel was not checked is superseded by the check above.
- H4's buffer-enlargement suggestion now separates storage capacity from request/copy lengths and explicitly prevents the existing sizeof-based copy from overflowing the 80-byte frame.
- M6 now requires actual endpoint cancellation coordinated with callbacks; the bundled FS HAL's FIFO-flush functions are no-op compatibility stubs.
- H1/M12 now distinguish the stale-counter timeout from conditional additional frame loss and echo-context leakage.
- M14's affected-kernel claim is limited to verified implementations; no universal Linux ≤ 6.18 boundary is asserted.

---

## Appendix A — Old finding IDs

| This report | Claude's report | Codex's report |
|---|---|---|
| H1 | H1 | (refinement of H1) |
| H2 | H2 | — |
| H3 | H3 | — |
| H4 | — | N01 |
| H5 | H4 | (correction of H4) |
| H6 | H5 | (refinement of H5) |
| H7 | H6 | — |
| M1 | — | N02 |
| M2 | — | N03 |
| M3 | M1 | (refinement of M1) |
| M4 | M2 | — |
| M5 | M3 | — |
| M6 | M4 | — |
| M7 | M5 | — |
| M8 | M6 | — |
| M9 | M7 | N07 |
| M10 | — | N08 |
| M11 | M8 | — |
| M12 | — | N09 |
| M13 | M9 | (extension of M9) |
| M14 | M10 | (correction of M10) |
| M15 | — | N04 |
| M16 | — | N05 |
| M17 | — | N06 |
| M18 | — | N10 |
| M19 | — | N12 |
| M20 | M11 | — |
| M21 | M12 | — |
| M22 | A1 | — |
| L1 | — | N11 |
| L2 | — | N13 |
| L3–L18 | L1–L16 | — |
