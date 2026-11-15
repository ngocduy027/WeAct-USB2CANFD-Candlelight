# Source provenance and notices

Firmware source was copied from the locally reviewed WeActStudio CANable project,
base commit `9a2121d2feca6d1fcec6f30312df9491b23b1204`, with the authorized fixes
and WeAct READY/activity LED policy applied. Original source headers and notices
are retained byte-for-byte. Original history remains in the development workspace;
this publication folder starts a new Git history.

- Firmware headers name the MIT License and authors including ElmueSoft,
  Nakanishi Kiyomaro, Normadotcom and Hubert Denkmair.
- Arm CMSIS headers retain their Apache-2.0 identifiers and license notices.
- STM32 HAL, CMSIS device support and startup files retain their STMicroelectronics
  and other vendor notices; consult each component's original headers.
- PDFs, diagrams and the upstream HTML manual retain their original publisher
  information and attributions. They are reference documentation.

Licensing follows the notices in the individual files. No repository-wide license
replacement is applied to the bundled third-party components.

The public package omits other cloned repositories, the unused Nucleo project,
Windows prebuilt demo executables, private flash backups, raw USB/device inventories,
local IDE settings, caches, intermediate builds and obsolete release copies.
