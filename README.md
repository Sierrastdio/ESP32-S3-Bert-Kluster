# ESP32-S3 BERT Cluster

ai: <https://1drv.ms/u/c/09a71dc2aa7bfa58/IQCsVi3dmqDqQ5wyhk7g-aeCAb0ddoUg3yvbtwGJx5Z_ym8?e=rWUAFq>
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

## BERT-Medium training

Chronological Arch Linux WSL setup, virtual-environment activation, GPU check,
first training, and later automated rounds are in [`Bert/README.md`](Bert/README.md).
The active trainer is `Bert/medium_train_v2.py`.

---

## sierrastdio Windows Arch WSL training env

Windows 본 프로젝트를 Arch WSL의 사용자 소유 Linux 경로로 복사해 학습합니다. **Arch WSL을 연 뒤** 아래 순서로 최초 준비합니다.

```bash
WIN_PROJECT=/mnt/c/Users/sierra/Documents/ESP32-S3-Bert-Kluster
WSL_PROJECT="$HOME/projects/ESP32-S3-Bert-Kluster"
mkdir -p "$WSL_PROJECT"
cp -r "$WIN_PROJECT/." "$WSL_PROJECT/"
chmod -R u+rwX "$WSL_PROJECT"
cd "$WSL_PROJECT/Bert"
if [ ! -f pytorch_model.bin ]; then cp ~/clone/ESP32-S3-Bert-Kluster/Bert/pytorch_model.bin .; fi
bash setup_arch.sh cu132 # 최초 한 번: RTX 3060용 CUDA PyTorch와 패키지 설치
source .venv/bin/activate
python -c "import torch; print(torch.__version__, torch.cuda.is_available(), torch.cuda.get_device_name(0) if torch.cuda.is_available() else '')"
bash run_train.sh        # 최초 학습만; 저장 체크포인트가 있으면 평가만 함
```

NVIDIA GPU를 쓰려면 WSL에서 `nvidia-smi`를 확인하고, [PyTorch 공식 설치 안내](https://pytorch.org/get-started/locally/)에서 확인한 CUDA wheel 태그로 `bash setup_arch.sh cuXXX`를 실행합니다. 스크립트는 CUDA 사용 가능 여부에 따라 GPU 또는 CPU를 자동 선택합니다. 상세 안내는 [`Bert/README.md`](Bert/README.md)를 참고하세요.

`~/clone`은 읽기 전용일 수 있으므로 학습은 `~/projects/ESP32-S3-Bert-Kluster` 복사본에서 진행합니다. 이후 새 WSL 터미널에서는 먼저 `cd "$HOME/projects/ESP32-S3-Bert-Kluster/Bert"`와 `source .venv/bin/activate`를 실행합니다. Windows 본 프로젝트의 `Bert/hard_examples.tsv`에 새 라벨 예시를 추가한 뒤 `bash train_cycle.sh`로 동기화·GPU 확인·보강 학습·검증·자동 채택/보류를 진행합니다. 데이터가 바뀌지 않으면 중복 학습은 건너뜁니다. 산출물은 WSL 복사본의 `Bert/pruned/`에 생깁니다. 상세 시간순 절차와 라벨 형식은 [`Bert/README.md`](Bert/README.md)를 참고하세요.
