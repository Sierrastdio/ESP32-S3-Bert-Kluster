#!/usr/bin/env python3
"""
flash_weights.py — ESP32-S3 BERT 노드 가중치 바이너리 플래싱 도구

ESP32-S3 N16R8 (16MB Flash)의 14MB 모델 파티션 (오프셋 0x200000)에
각 노드별 가중치 파일(node1.bin ~ node4.bin)을 esptool을 통해 플래시합니다.

사용법:
  # 대화형 모드 (포트 및 노드 번호 대화형 선택)
  python flash_weights.py

  # 특정 노드 지정 플래시
  python flash_weights.py --port COM3 --node 1

  # 전송 속도 및 .pt 경로 지정
  python flash_weights.py --port COM3 --node 2 --baud 921600 --pt ../Bert/pruned/model_finetuned.pt
"""

import argparse
import os
import subprocess
import sys
from pathlib import Path

MODEL_PARTITION_OFFSET = 0x200000  # 2MB 지점 (partitions.csv의 'model' 파티션)
DEFAULT_BAUD = 921600


def get_available_com_ports():
    """사용 가능한 시리얼 포트 목록 반환"""
    try:
        import serial.tools.list_ports

        ports = list(serial.tools.list_ports.comports())
        return [(p.device, p.description) for p in ports]
    except ImportError:
        return []


def ensure_weights_exported(pt_path, weights_dir, dtype="fp16"):
    """
    node1.bin ~ node4.bin이 존재하는지 확인하고,
    없거나 .pt가 더 최신이면 export_weights.py 로직을 호출하여 생성합니다.
    """
    weights_path = Path(weights_dir)
    weights_path.mkdir(parents=True, exist_ok=True)
    pt_file = Path(pt_path)

    if not pt_file.is_file():
        raise FileNotFoundError(f"체크포인트 파일을 찾을 수 없습니다: {pt_path}")

    pt_mtime = pt_file.stat().st_mtime
    needs_export = False

    for node_id in range(1, 5):
        bin_path = weights_path / f"node{node_id}.bin"
        if not bin_path.is_file() or bin_path.stat().st_mtime < pt_mtime:
            needs_export = True
            break

    if needs_export:
        print(f"[*] 가중치 바이너리 생성/갱신 중: {pt_path} -> {weights_dir}")
        bert_dir = Path(__file__).resolve().parent.parent / "Bert"
        export_script = bert_dir / "export_weights.py"

        cmd = [
            sys.executable,
            str(export_script),
            "--pt",
            str(pt_file),
            "--out",
            str(weights_path),
            "--dtype",
            dtype,
        ]
        res = subprocess.run(cmd)
        if res.returncode != 0:
            raise RuntimeError(f"가중치 내보내기 실패 (코드 {res.returncode})")
        print("[+] 가중치 바이너리 생성 완료.\n")


def flash_node(port, node_id, weights_dir, baud=DEFAULT_BAUD):
    """지정된 노드의 바이너리를 ESP32-S3 모델 파티션에 플래시"""
    bin_file = Path(weights_dir) / f"node{node_id}.bin"
    if not bin_file.is_file():
        raise FileNotFoundError(f"바이너리 파일을 찾을 수 없습니다: {bin_file}")

    file_size_mb = bin_file.stat().st_size / (1024 * 1024)
    print(f"\n=======================================================")
    print(f" [Node {node_id}] 플래싱 시작")
    print(f"   포트:     {port}")
    print(f"   속도:     {baud} bps")
    print(f"   파일:     {bin_file.name} ({file_size_mb:.2f} MB)")
    print(f"   오프셋:   0x{MODEL_PARTITION_OFFSET:X}")
    print(f"=======================================================\n")

    cmd = [
        sys.executable,
        "-m",
        "esptool",
        "--chip",
        "esp32s3",
        "--port",
        port,
        "--baud",
        str(baud),
        "--before",
        "default_reset",
        "--after",
        "hard_reset",
        "write_flash",
        "-z",
        f"0x{MODEL_PARTITION_OFFSET:X}",
        str(bin_file),
    ]

    print("실행 명령:", " ".join(cmd))
    res = subprocess.run(cmd)
    if res.returncode != 0:
        print(f"\n[!] Node {node_id} 플래싱 실패!")
        return False
    else:
        print(f"\n[v] Node {node_id} 플래싱 성공!")
        return True


def interactive_menu(pt_path, weights_dir, default_baud):
    """대화형 CLI 메뉴"""
    print("=" * 60)
    print("  ESP32-S3 BERT 클러스터 가중치 플래셔 (N16R8 16MB Flash)")
    print("=" * 60)

    # 1. COM 포트 검색
    ports = get_available_com_ports()
    if not ports:
        port = input("검색된 COM 포트가 없습니다. 포트 이름을 직접 입력하세요 (예: COM3): ").strip()
    else:
        print("\n연결된 시리얼 포트 목록:")
        for idx, (dev, desc) in enumerate(ports, start=1):
            print(f"  [{idx}] {dev} - {desc}")
        print(f"  [{len(ports) + 1}] 직접 입력")

        while True:
            sel = input(f"포트를 선택하세요 [1-{len(ports) + 1}]: ").strip()
            if sel.isdigit():
                sel_idx = int(sel)
                if 1 <= sel_idx <= len(ports):
                    port = ports[sel_idx - 1][0]
                    break
                elif sel_idx == len(ports) + 1:
                    port = input("COM 포트를 직접 입력하세요 (예: COM3): ").strip()
                    break
            print("올바른 번호를 입력해 주세요.")

    # 2. 노드 선택
    print("\n플래시할 노드를 선택하세요:")
    print("  [1] Node 1 (임베딩 + 레이어 0~1)")
    print("  [2] Node 2 (레이어 2~3)")
    print("  [3] Node 3 (레이어 4~5)")
    print("  [4] Node 4 (레이어 6~7 + 분류 헤드)")
    print("  [A] All (1 -> 2 -> 3 -> 4 차례대로 보드 교체 안내 받으며 플래시)")

    while True:
        node_input = input("노드 선택 [1/2/3/4/A]: ").strip().upper()
        if node_input in ("1", "2", "3", "4", "A"):
            break
        print("1, 2, 3, 4 또는 A를 입력하세요.")

    # 3. 가중치 준비
    ensure_weights_exported(pt_path, weights_dir)

    if node_input in ("1", "2", "3", "4"):
        flash_node(port, int(node_input), weights_dir, default_baud)
    else:
        for nid in range(1, 5):
            input(f"\n>> Node {nid} 보드를 연결하고 Enter 키를 누르세요...")
            flash_node(port, nid, weights_dir, default_baud)


def main():
    parser = argparse.ArgumentParser(description="ESP32-S3 가중치 바이너리 플래싱 도구")
    parser.add_argument("--port", help="ESP32-S3 직렬 COM 포트 (예: COM3)")
    parser.add_argument("--node", type=int, choices=[1, 2, 3, 4], help="플래시할 노드 번호 (1~4)")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD, help=f"전송 속도 (기본: {DEFAULT_BAUD})")
    parser.add_argument(
        "--pt",
        default=str(Path(__file__).resolve().parent.parent / "Bert" / "pruned" / "model_finetuned.pt"),
        help="학습 완료된 .pt 파일 경로",
    )
    parser.add_argument(
        "--weights-dir",
        default=str(Path(__file__).resolve().parent.parent / "Bert" / "weights"),
        help="생성된 nodeN.bin 저장 디렉토리",
    )
    parser.add_argument("--dtype", choices=["fp16", "fp32"], default="fp16", help="가중치 정밀도 (기본: fp16)")

    args = parser.parse_args()

    # CLI 인자가 모두 주어지지 않은 경우 대화형 모드 실행
    if not args.port or not args.node:
        interactive_menu(args.pt, args.weights_dir, args.baud)
    else:
        ensure_weights_exported(args.pt, args.weights_dir, args.dtype)
        flash_node(args.port, args.node, args.weights_dir, args.baud)


if __name__ == "__main__":
    main()
