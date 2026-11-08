#!/usr/bin/env python3
"""
export_weights.py — model_finetuned.pt → node1.bin ~ node4.bin

바이너리 레이아웃:
  [64B 파일 헤더][N × 64B 텐서 디렉토리][텐서 데이터 (4B 정렬)]

사용법:
  python export_weights.py
  python export_weights.py --dtype fp32
  python export_weights.py --pt pruned/model_finetuned.pt --out weights/
"""

import argparse
import os
import struct
import torch

# ── 헤더 및 모델 상수 ────────────────────────────────────────────────────────
MAGIC = 0x42455254       # 'BERT' (little-endian)
FILE_VERSION = 1
HEADER_SIZE = 64
DIRENTRY_SIZE = 64

DTYPE_FP32 = 0
DTYPE_FP16 = 1

HIDDEN = 512
N_LAYERS = 8
N_HEADS = 8
HEAD_DIM = 64
FFN_SIZE = 2048

# 노드별 담당 층 (0-based, inclusive)
NODE_LAYER_RANGES = {1: (0, 1), 2: (2, 3), 3: (4, 5), 4: (6, 7)}


# ── 텐서 명세 목록 생성 ──────────────────────────────────────────────────────

def emb_specs():
    """임베딩 레이어 텐서 명세 (노드 1)"""
    p = "base.embeddings"
    return [
        ("emb.word.w", f"{p}.word_embeddings.weight", True),
        ("emb.pos.w",  f"{p}.position_embeddings.weight", True),
        ("emb.type.w", f"{p}.token_type_embeddings.weight", False),  # 2행뿐이라 FP32 유지
        ("emb.ln.w",   f"{p}.LayerNorm.weight", False),
        ("emb.ln.b",   f"{p}.LayerNorm.bias", False),
    ]


def layer_specs(n):
    """트랜스포머 n번째 레이어 텐서 명세 (16개 텐서)"""
    p = f"base.encoder.layer.{n}"
    specs = []

    # 1. Self-Attention Q, K, V, Output dense
    attn_parts = [
        ("attn.q", "attention.self.query"),
        ("attn.k", "attention.self.key"),
        ("attn.v", "attention.self.value"),
        ("attn.o", "attention.output.dense"),
    ]
    for short, full in attn_parts:
        specs.append((f"l{n}.{short}.w", f"{p}.{full}.weight", True))
        specs.append((f"l{n}.{short}.b", f"{p}.{full}.bias", False))

    # 2. Attention LayerNorm
    specs.append((f"l{n}.attn.ln.w", f"{p}.attention.output.LayerNorm.weight", False))
    specs.append((f"l{n}.attn.ln.b", f"{p}.attention.output.LayerNorm.bias", False))

    # 3. FFN Intermediate & Output
    specs.append((f"l{n}.ffn.w1", f"{p}.intermediate.dense.weight", True))
    specs.append((f"l{n}.ffn.b1", f"{p}.intermediate.dense.bias", False))
    specs.append((f"l{n}.ffn.w2", f"{p}.output.dense.weight", True))
    specs.append((f"l{n}.ffn.b2", f"{p}.output.dense.bias", False))

    # 4. FFN LayerNorm
    specs.append((f"l{n}.ffn.ln.w", f"{p}.output.LayerNorm.weight", False))
    specs.append((f"l{n}.ffn.ln.b", f"{p}.output.LayerNorm.bias", False))

    return specs


def head_specs():
    """분류 헤드 텐서 명세 (노드 4: dev, act, amt)"""
    specs = []
    for h in ("dev", "act", "amt"):
        specs.append((f"head.{h}.w", f"{h}.weight", True))
        specs.append((f"head.{h}.b", f"{h}.bias", False))
    return specs


# ── 바이너리 패킹 ────────────────────────────────────────────────────────────

def pack_header(node_id, layer_start, layer_end, flags, n_tensors,
                dev_cls, act_cls, amt_cls, vocab_size, max_pos, wt_dtype):
    """
    64바이트 파일 헤더 생성.
    ESP32 bert_weight_header_t 구조체와 1:1로 일치합니다.
    """
    raw = struct.pack(
        "<IHBBHHBBBHHBBBBHHH",
        MAGIC,
        FILE_VERSION,
        node_id,
        flags,
        HIDDEN,
        N_LAYERS,
        layer_start,
        layer_end,
        N_HEADS,
        HEAD_DIM,
        FFN_SIZE,
        dev_cls,
        act_cls,
        amt_cls,
        wt_dtype,
        n_tensors,
        vocab_size,
        max_pos,
    )
    return raw.ljust(HEADER_SIZE, b"\x00")


def pack_direntry(name, dtype, shape, offset, size):
    """64바이트 텐서 디렉토리 항목 생성"""
    name_bytes = name.encode("ascii")[:40].ljust(40, b"\x00")
    ndim = len(shape)
    shape4 = (list(shape) + [0, 0, 0, 0])[:4]
    body = struct.pack("<BB4HII", dtype, ndim, *shape4, offset, size)
    return (name_bytes + body).ljust(DIRENTRY_SIZE, b"\x00")


def tensor_to_bytes(t, dtype):
    t = t.detach().float()
    return (t.half() if dtype == DTYPE_FP16 else t).numpy().tobytes()


# ── 노드별 바이너리 생성 ─────────────────────────────────────────────────────

def build_node_bin(sd, node_id, wt_dtype, out_dir):
    layer_start, layer_end = NODE_LAYER_RANGES[node_id]
    has_emb = (node_id == 1)
    has_head = (node_id == 4)

    # 1. 해당 노드가 맡은 텐서 목록 취합
    specs = []
    if has_emb:
        specs += emb_specs()
    for li in range(layer_start, layer_end + 1):
        specs += layer_specs(li)
    if has_head:
        specs += head_specs()

    flags = (1 if has_emb else 0) | (2 if has_head else 0)
    dev_cls = int(sd["dev.weight"].shape[0]) if has_head else 0
    act_cls = int(sd["act.weight"].shape[0]) if has_head else 0
    amt_cls = int(sd["amt.weight"].shape[0]) if has_head else 0
    vocab_size = int(sd["base.embeddings.word_embeddings.weight"].shape[0])
    max_pos = int(sd["base.embeddings.position_embeddings.weight"].shape[0])

    # 2. 텐서 바이트 변환 및 오프셋 계산
    n_tensors = len(specs)
    data_start = HEADER_SIZE + n_tensors * DIRENTRY_SIZE
    offset = data_start

    entries = []
    for short, key, is_weight in specs:
        t = sd[key]
        dtype = wt_dtype if is_weight else DTYPE_FP32
        raw = tensor_to_bytes(t, dtype)
        pad = (4 - len(raw) % 4) % 4  # 4바이트 정렬
        entries.append((short, dtype, tuple(t.shape), offset, raw, pad))
        offset += len(raw) + pad

    # 3. 파일 작성
    out_path = os.path.join(out_dir, f"node{node_id}.bin")
    with open(out_path, "wb") as f:
        # [1구역] 64B 파일 헤더
        hdr = pack_header(
            node_id, layer_start, layer_end, flags, n_tensors,
            dev_cls, act_cls, amt_cls, vocab_size, max_pos, wt_dtype
        )
        f.write(hdr)

        # [2구역] n_tensors × 64B 텐서 디렉토리
        for short, dtype, shape, off, raw, _ in entries:
            f.write(pack_direntry(short, dtype, shape, off, len(raw)))

        # [3구역] 텐서 원시 데이터 + 4바이트 정렬 패딩
        for *_, raw, pad in entries:
            f.write(raw)
            if pad > 0:
                f.write(b"\x00" * pad)

    # 기본 매직 검증
    with open(out_path, "rb") as f:
        assert struct.unpack("<I", f.read(4))[0] == MAGIC, f"매직 불일치: {out_path}"

    size_mb = os.path.getsize(out_path) / 1024 / 1024
    extra = []
    if has_emb:
        extra.append("emb")
    if has_head:
        extra.append("head")
    extra_str = f" +{'/'.join(extra)}" if extra else ""

    print(f"  node{node_id}.bin  {size_mb:.2f} MB  ({n_tensors} tensors, L{layer_start}-{layer_end}{extra_str})")
    return out_path


# ── 메인 ─────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="BERT .pt → node1.bin ~ node4.bin 변환")
    parser.add_argument("--pt", default="pruned/model_finetuned.pt", help=".pt 파일 경로")
    parser.add_argument("--out", default="weights", help="출력 디렉토리")
    parser.add_argument("--dtype", choices=["fp16", "fp32"], default="fp16",
                        help="가중치 정밀도 (bias/LayerNorm은 항상 FP32 유지)")
    args = parser.parse_args()

    wt_dtype = DTYPE_FP16 if args.dtype == "fp16" else DTYPE_FP32
    os.makedirs(args.out, exist_ok=True)

    print(f"Loading {args.pt} ...")
    s = torch.load(args.pt, map_location="cpu")
    sd = s
    for key in ("model_state_dict", "state_dict"):
        if isinstance(s, dict) and key in s and isinstance(s[key], dict):
            sd = s[key]
            break

    print(f"dtype={args.dtype}, output → {os.path.abspath(args.out)}/\n")
    for node_id in (1, 2, 3, 4):
        build_node_bin(sd, node_id, wt_dtype, args.out)

    print("\n완료. esptool.py를 사용해 각 node_N.bin을 ESP32-S3 데이터 파티션에 플래시하세요.")


if __name__ == "__main__":
    main()
