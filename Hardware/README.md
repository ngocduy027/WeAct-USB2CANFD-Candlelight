# WeAct USB2CANFD V2 references

Board: STM32G431CB, 16 MHz crystal, isolated CA-IS2062A transceiver.
The supplied schematic and transceiver datasheet were copied unchanged from
WeActStudio's hardware repository. See their original publisher notices.

| MCU pin | Function |
|---|---|
| PB8 / PB9 | FDCAN1 RX / TX |
| PB7 | CAN receiver enable |
| PA0 | RX LED |
| PA1 | TX LED |
| PA2 | READY LED |

The three LEDs are active-low. READY is now a usable-CAN-channel indicator, not
a direct power indicator. Both external end terminators must be configured for
an actual CAN link; firmware does not control the board's physical termination.
