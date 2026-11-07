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

---

## sierrastdio Windows Arch WSL training env

Claude/Codex와 Git은 Windows 본 프로젝트에서 사용하고, 학습은 Arch WSL의 Linux 복사본에서 합니다. **PyTorch/CUDA 설치, 원본 BERT 파일 준비 등 최초 환경 설정은** [`Bert/README_WSL_SETUP.md`](Bert/README_WSL_SETUP.md)에 따릅니다. `hard_examples.tsv` 작성법은 [`Bert/README_HARD_EXAMPLES_GUIDE.md`](Bert/README_HARD_EXAMPLES_GUIDE.md)에 있습니다. 아래는 환경 설치 이후 학습에 사용하는 명령입니다.

```bash
WIN_PROJECT=/mnt/c/Users/sierra/Documents/ESP32-S3-Bert-Kluster
WSL_PROJECT="$HOME/projects/ESP32-S3-Bert-Kluster"
bash "$WIN_PROJECT/Bert/sync_to_wsl.sh"
cd "$WSL_PROJECT/Bert"
source .venv/bin/activate
python -c "import torch; print(torch.cuda.is_available(), torch.cuda.get_device_name(0) if torch.cuda.is_available() else 'CUDA unavailable')"
bash run_train.sh --no-interactive # 최초 학습: pruned 체크포인트가 없을 때
bash "$WIN_PROJECT/Bert/sync_from_wsl.sh" # WSL 저장소를 Windows로 동기화
```

`pruned/model_finetuned.pt`가 이미 있으면 `bash run_train.sh`는 기본적으로 학습하지 않고 모델을 불러와 평가합니다. 보강 학습은 아래 자동 회차 명령을 사용합니다.

최초 설정 후에는 Windows에서 코드와 `Bert/hard_examples.tsv`를 수정·저장하고, WSL에서 아래를 실행해 한 학습 회차를 돌립니다.

```bash
cd "$HOME/projects/ESP32-S3-Bert-Kluster/Bert"
source .venv/bin/activate
bash /mnt/c/Users/sierra/Documents/ESP32-S3-Bert-Kluster/Bert/train_cycle.sh
```

`sync_to_wsl.sh`는 Windows 저장소를 WSL로, `sync_from_wsl.sh`는 WSL 저장소를 Windows로 동기화합니다. 두 명령 모두 `.git`, WSL 전용 `Bert/.venv`, Pico 빌드 결과를 보존하고, `Bert/pruned/`와 원본 모델 가중치를 포함한 나머지 파일을 복사합니다. Git에서는 `Bert/pruned/*.pt` 체크포인트만 제외하고 로그와 평가 자료 등은 공유합니다. 같은 경로 파일은 덮어쓰지만 대상 쪽에만 있는 파일은 삭제하지 않습니다. GitHub push는 Windows 저장소에서만 합니다. 상세 절차는 [`Bert/README_MAIN.md`](Bert/README_MAIN.md)를 참고하세요.
