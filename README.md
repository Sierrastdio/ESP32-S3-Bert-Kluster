# ESP32-S3 BERT Cluster

ai: https://1drv.ms/u/c/09a71dc2aa7bfa58/IQCsVi3dmqDqQ5wyhk7g-aeCAb0ddoUg3yvbtwGJx5Z_ym8?e=rWUAFq
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

------------------------------------
training guide
```
sudo apt update && sudo apt install -y python3-venv python3-pip
python3 -m venv ~/ai
source ~/ai/bin/activate
pip install torch --index-url https://download.pytorch.org/whl/cpu
pip install transformers
```

```
python medium_train.py
```
