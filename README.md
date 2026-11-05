# ESP32-S3 BERT Cluster

Project notes and firmware examples for a Pico 1 controller and ESP32-S3
inference nodes. See [the project brief](multi_esp_project_brief.md) for the
system design and maturity labels.

## Build Pico 1 firmware on Windows

Install CMake and Ninja, and use Pico SDK 2.3.1 with its TinyUSB submodule
initialized. Pico-PIO-USB is a separate dependency in this setup. These are
the paths used to build this workspace:

```text
Pico SDK:       %USERPROFILE%\.pico-sdk\sdk\2.3.1
Pico-PIO-USB:   %USERPROFILE%\.pico-sdk\pico-pio-usb
ARM toolchain:  C:\Program Files\Raspberry Pi\Pico SDK v1.5.1\gcc-arm-none-eabi
```

Open PowerShell and run this once to configure the Pico 1 build directory:

```powershell
cd C:\Users\sierra\Documents\ESP32-S3-Bert-Kluster\pico1
$env:PICO_SDK_PATH = "$env:USERPROFILE\.pico-sdk\sdk\2.3.1"
$env:PICO_PIO_USB_PATH = "$env:USERPROFILE\.pico-sdk\pico-pio-usb"
$env:PICO_TOOLCHAIN_PATH = "C:\Program Files\Raspberry Pi\Pico SDK v1.5.1\gcc-arm-none-eabi"
$env:PATH = "$env:PICO_TOOLCHAIN_PATH\bin;$env:PATH"
cmake -S . -B build-arm -G Ninja `
  -DPICO_BOARD=pico `
  -DPICO_SDK_PATH="$env:PICO_SDK_PATH" `
  -DPICO_PIO_USB_PATH="$env:PICO_PIO_USB_PATH"
```

After changing firmware source files (including `src/tusb_config.h`), run
this command from the `pico1` directory. CMake/Ninja will rebuild changed
files and refresh the UF2:

```powershell
cmake --build build-arm --target pico1_controller
```

Use the UF2 at `build-arm\pico1_controller.uf2`. Do not build or copy the UF2
from `build-sdk-2.3.1`: that directory was configured for Pico 2 (`pico2`),
not this Pico 1 project.

If `build-arm` has a cache from a different SDK or board, run this once to
recreate it for Pico 1, then use the commands above for later builds:

```powershell
cmake --fresh -S . -B build-arm -G Ninja `
  -DPICO_BOARD=pico `
  -DPICO_SDK_PATH="$env:PICO_SDK_PATH" `
  -DPICO_PIO_USB_PATH="$env:PICO_PIO_USB_PATH"
```

Do not reuse a build tree created with `NMake Makefiles` when switching to
Ninja. The `.gitignore` excludes these build trees and generated firmware
files.

On success, the UF2 is:

```text
pico1\build-arm\pico1_controller.uf2
```

Hold BOOTSEL while connecting the Pico over USB, then copy the UF2 to the
`RPI-RP2` drive.

The current firmware blinks the onboard LED for 25 ms on every keyboard
character event as an input diagnostic.

## Pico 1 pin connections

The table reflects the current values in `pico1/include/pico_config.h`.
These are prototype assignments only; verify them against the actual wiring
before connecting the ESP32-S3 nodes.

| Pico 1 GPIO | Signal | Connects to | Notes |
| --- | --- | --- | --- |
| GP0 | UART TX | ESP32-S3 nodes' UART RX inputs (shared broadcast line) | 115200 baud, TX only from Pico |
| GP2 | SPI SCK | Node 1 SPI SCK | SPI master clock, 1 MHz |
| GP3 | SPI MOSI | Node 1 SPI MOSI | Pico sends token packet |
| GP5 | SPI CS | Node 1 SPI CS | Active low, controlled by Pico |
| GP6 | READY input | Nodes' shared READY output | Current prototype treats READY as one shared signal |
| GND | Ground | Common ground for Pico and all nodes | Required signal reference |
| USB | USB CDC / power | Notebook | Current input is serial text, not a PIO USB keyboard |

SPI MISO is not used by the current Pico token sender. The ESP32-S3 node pin
numbers and the shared READY electrical arrangement are not finalized here;
coordinate them with the circuit and node firmware before wiring.

## Current Pico prototype limitations

- Notebook input currently uses USB CDC line input. PIO USB keyboard host
  support is not implemented yet.
- Tokenizer expects a newline-separated `vocab.txt`, but a Pico runtime
  filesystem or embedded-vocabulary mechanism has not been implemented.
- SPI and UART pins in `pico1/include/pico_config.h` are placeholders and must
  match the circuit.
- The current node SPI verification sketch uses a different frame format;
  the Pico protocol and node firmware are not interoperable yet.
- The Pico prototype assumes hidden size 512 and FP16 for packet-size math.
  Result reception and retry handling are still pending.

See [Pico-specific notes](pico1/README.md) and the `examples` folder for
details on the current experiments.
