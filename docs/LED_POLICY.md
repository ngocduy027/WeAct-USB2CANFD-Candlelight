# READY/activity LED policy

The requested policy applies only to the WeAct Candlelight build. Other boards
and SLCAN retain their preceding behavior.

READY requires configured USB and a physically running/open CAN controller, with
no suspend, recovery, hardware error state or bus-off. CAN warning/error-passive
alone does not make an otherwise usable channel unready. No additional PSR read
is performed just for LEDs, so protocol errors remain available to the reporter.

TX/RX use independent 15 ms pulses and a 40 ms minimum off interval. Their update
paths preserve nested interrupt masks, handle the first frame at HAL tick zero,
use a fresh tick when expiring pulses, and work across unsigned tick wrap.
Startup, USB requests and error/status display do not light TX/RX. Identify is
not advertised and Identify commands are rejected as unsupported. Suspend and
close leave all three LEDs off. ROM DFU has its own GPIO behavior.

The software version is `0x26111501` (YYMMDD, revision 01); USB bcdDevice remains
`0x2611`. The LED revision is built and has passed flash readback and internal
classic/FD loopback. Visual confirmation of each LED state is not recorded.
