"""
BERT-Medium: 어휘 줄이기 -> 줄인 어휘로 학습 -> 저장  (브리프의 2~4단계)

  2단계  단어 목록 만들기 : 특수 토큰 + 학습 문장 토큰 + 글자 조각 + 자주 쓰는 영어 단어 (VOCAB_BUDGET 이하)
  3단계  임베딩 자르기    : 목록에 있는 행만 남김 (남긴 단어의 사전학습 지식은 그대로)
  4단계  학습            : 줄인 토크나이저로 토큰화해서 장치/동작/양 헤드 + 본체 미세조정
         평가            : 학습 템플릿과 겹치지 않는 처음 보는 문장(UNSEEN)으로 따로 평가

이 파일을 모델 파일(config.json, pytorch_model.bin, vocab.txt)과 같은 폴더에 두고 실행:
    python medium_train_v2.py
결과는 같은 폴더의 pruned\\ 에 저장 (원본 파일은 건드리지 않음)
다시 학습하려면 pruned\\model_finetuned.pt 를 지우고 실행
"""

import argparse
import os
import random
import shutil
import string
from datetime import datetime

import torch
import torch.nn as nn
from transformers import AutoModel, AutoTokenizer, BertConfig, BertModel, BertTokenizer

BASE = os.path.dirname(os.path.abspath(__file__))  # 원본 모델 파일이 있는 폴더
OUT = os.path.join(BASE, "pruned")
SAVE = os.path.join(OUT, "model_finetuned.pt")

VOCAB_BUDGET = (
    1300  # 보드1 플래시 예산으로 정한 최대 단어 수 (추정값, 바뀌면 여기만 수정)
)
MAX_LEN = 64  # 최대 토큰 수 ([CLS], [SEP] 포함)
EPOCHS, BATCH, LR = 4, 32, 5e-5
FINETUNE_EPOCHS, FINETUNE_LR, HARD_REPEAT = 2, 5e-6, 5
CONF_THRESHOLD = 0.6  # 이보다 확신이 낮으면 HOLD
DEVICE = torch.device("cuda" if torch.cuda.is_available() else "cpu")

DEVICES = ["led", "motor", "fan"]
ACTIONS = ["on", "off", "change", "none"]  # none = 명령이 아닌 문장 (비트 11)
IGNORE = -100  # CrossEntropyLoss 가 건너뛰는 라벨

random.seed(0)
torch.manual_seed(0)

# ---------------------------------------------------------------- 학습 데이터 (v2: 표현 다양화 + 처음 보는 문장 평가셋 확대)
# 장치 이름. 평가용으로 일부러 빼 둔 단어(bulb, lighting, rotor, ventilator 등)는 학습에 넣지 않는다.
DEV_WORDS = {
    0: [
        "led",
        "leds",
        "light",
        "lights",
        "lamp",
        "desk lamp",
        "ceiling light",
        "led strip",
    ],
    1: ["motor", "engine", "drive motor", "dc motor", "motors"],
    2: ["fan", "blower", "cooler", "cooling fan", "desk fan", "ceiling fan"],
}
# 양 표현. 퍼센트는 올림(p × 7 / 100) 규칙으로 0~7 단계에 맞춤. "level 1~7"은 아래에서 자동 추가.
AMT_WORDS = {
    1: [
        "barely",
        "very low",
        "a tiny bit",
        "minimum",
        "the lowest setting",
        "10 percent",
    ],
    2: ["a little", "low", "slightly", "20 percent"],
    3: ["a bit low", "30 percent", "40 percent"],
    4: ["half", "medium", "50 percent", "halfway"],
    5: ["a bit high", "60 percent", "70 percent"],
    6: ["high", "strong", "a lot", "80 percent"],
    7: ["max", "full", "maximum", "100 percent", "full power"],
}
ON = [
    "turn on the {d}",
    "turn the {d} on",
    "switch on the {d}",
    "switch the {d} on",
    "start the {d}",
    "start up the {d}",
    "power up the {d}",
    "power on the {d}",
    "fire up the {d}",
    "activate the {d}",
    "enable the {d}",
    "{d} on",
    "i want the {d} on",
    "the {d} should be on",
    "get the {d} running",
]
ON_AMT = [
    "turn on the {d} {a}",
    "turn the {d} on {a}",
    "start the {d} at {a}",
    "turn on the {d} at {a}",
    "run the {d} at {a}",
    "power up the {d} to {a}",
]
OFF = [
    "turn off the {d}",
    "turn the {d} off",
    "switch off the {d}",
    "switch the {d} off",
    "stop the {d}",
    "shut down the {d}",
    "shut off the {d}",
    "shut the {d} off",
    "power off the {d}",
    "power down the {d}",
    "kill the {d}",
    "cut the {d}",
    "disable the {d}",
    "deactivate the {d}",
    "{d} off",
    "i want the {d} off",
    "the {d} should be off",
    "no more {d}",
]
CHG = [
    "set the {d} to {a}",
    "make the {d} {a}",
    "change the {d} to {a}",
    "adjust the {d} to {a}",
    "put the {d} at {a}",
    "bring the {d} to {a}",
    "keep the {d} at {a}",
    "{d} to {a}",
    "i want the {d} at {a}",
]
# 상대 변경 -> action=change. 올림=6, 내림=2, 조금 올림=5, 조금 내림=3, 끝까지 올림=7, 끝까지 내림=1
REL = [
    ("turn up the {d}", 6),
    ("turn the {d} up", 6),
    ("turn down the {d}", 2),
    ("turn the {d} down", 2),
    ("increase the {d}", 6),
    ("decrease the {d}", 2),
    ("raise the {d}", 6),
    ("lower the {d}", 2),
    ("turn the {d} up a little", 5),
    ("turn the {d} down a little", 3),
    ("turn the {d} all the way up", 7),
    ("turn the {d} all the way down", 1),
]
REL_DEV = {
    0: [
        ("brighten the {d}", 6),
        ("make the {d} brighter", 6),
        ("dim the {d}", 2),
        ("make the {d} dimmer", 2),
        ("make the {d} a little brighter", 5),
        ("dim the {d} a little", 3),
    ],
    1: [
        ("speed up the {d}", 6),
        ("make the {d} faster", 6),
        ("slow down the {d}", 2),
        ("make the {d} slower", 2),
    ],
    2: [
        ("speed up the {d}", 6),
        ("make the {d} stronger", 6),
        ("slow down the {d}", 2),
        ("make the {d} weaker", 2),
    ],
}
PREFIX = ["please ", "hey ", "can you ", "could you ", "would you "]
SUFFIX = [" please", " now", " for me", " right now"]

# 장치 이름이 없거나 상황만 말하는 문장 -> "판단"이 필요한 예시 (문장, 장치, 동작, 양)
VAGUE = [
    ("it is so hot in here", 2, 0, 6),
    ("i am sweating", 2, 0, 6),
    ("it is really hot", 2, 0, 6),
    ("i am burning up", 2, 0, 6),
    ("this room is like an oven", 2, 0, 6),
    ("it is way too hot", 2, 0, 6),
    ("i feel overheated", 2, 0, 6),
    ("it is hot", 2, 0, 6),
    ("the air feels stuffy", 2, 0, 4),
    ("it is a little warm", 2, 0, 4),
    ("it is kind of warm", 2, 0, 4),
    ("the room feels muggy", 2, 0, 4),
    ("it is getting cold", 2, 1, 0),
    ("i am freezing", 2, 1, 0),
    ("it is too cold", 2, 1, 0),
    ("i feel chilly", 2, 1, 0),
    ("i am shivering", 2, 1, 0),
    ("it is cold in here", 2, 1, 0),
    ("the fan is too strong", 2, 2, 2),
    ("the wind is too strong", 2, 2, 2),
    ("the fan is too weak", 2, 2, 6),
    ("i can not feel the fan", 2, 2, 6),
    ("it is too dark", 0, 0, 7),
    ("i can not see anything", 0, 0, 7),
    ("it is pitch black", 0, 0, 7),
    ("the room is dark", 0, 0, 7),
    ("i need some light", 0, 0, 7),
    ("i can not see", 0, 0, 7),
    ("the room is too bright", 0, 2, 2),
    ("the light hurts my eyes", 0, 2, 2),
    ("it is too bright in here", 0, 2, 2),
    ("it is a bit dark in here", 0, 2, 6),
    ("the light is too dim", 0, 2, 6),
    ("i am going to sleep", 0, 1, 0),
    ("i am going to bed", 0, 1, 0),
    ("time to sleep", 0, 1, 0),
    ("i am leaving the room", 0, 1, 0),
    ("nobody is here now", 0, 1, 0),
    ("the motor is too loud", 1, 2, 2),
    ("the motor is too fast", 1, 2, 2),
    ("the engine is too noisy", 1, 2, 2),
    ("the motor is too slow", 1, 2, 6),
    ("the engine needs more speed", 1, 2, 6),
    ("the motor is overheating", 1, 1, 0),
    ("the engine smells like burning", 1, 1, 0),
]

# 명령이 아닌 문장 -> action=none. 장치 단어가 들어 있지만 명령이 아닌 문장(뒷부분)도 포함
NONE = [
    "hello there",
    "how are you",
    "what time is it",
    "thank you",
    "good morning",
    "tell me a joke",
    "i like pizza",
    "the weather is nice",
    "what is your name",
    "see you later",
    "i am reading a book",
    "my cat is cute",
    "open the door",
    "call my mom",
    "nice to meet you",
    "the movie was fun",
    "i am hungry",
    "where is my phone",
    "ok",
    "never mind",
    "yes",
    "no",
    "maybe later",
    "hi",
    "how is it going",
    "what day is today",
    "i need to study",
    "let me think",
    "that is interesting",
    "play some music",
    "set an alarm for seven",
    "remind me tomorrow",
    "i am bored",
    "what are you doing",
    "sounds good",
    "i bought a new lamp",
    "the fan was expensive",
    "my car has a big engine",
    "what is a motor",
    "the sunlight is nice today",
    "i like the color of the led",
    "how does an electric motor work",
    "my friend has a ceiling fan",
    "the light bulb was invented long ago",
    "fans cheered at the game",
    "i am a big fan of this band",
    "light travels very fast",
    "this bag is light",
]
# 명령 동사지만 우리 장치가 아닌 물건 -> none (평가셋의 tv, car는 일부러 뺌)
OTHER = [
    "radio",
    "computer",
    "oven",
    "truck",
    "washing machine",
    "printer",
    "music",
    "alarm",
    "microwave",
    "laptop",
    "game console",
    "kettle",
]
NONE_OTHER = [
    "turn on the {o}",
    "turn off the {o}",
    "switch on the {o}",
    "start the {o}",
    "stop the {o}",
    "i turned on the {o}",
    "she switched off the {o}",
    "he started the {o}",
]
# 장치 단어가 들어가지만 명령이 아닌 문장 -> none
NONE_NOUN = [
    "i bought a new {d}",
    "where did you buy that {d}",
    "what is a {d}",
    "i saw a {d} at the store",
    "my {d} is blue",
    "this {d} looks nice",
    "i read about the {d}",
    "we talked about the {d}",
]

# 학습에 쓰지 않는 "처음 보는 문장" 평가셋 (어휘 목록에도 강제로 넣지 않음)
UNSEEN_GROUPS = {
    "직접 명령": [
        ("flip the light on", 0, 0, 7),
        ("flip off the lamp", 0, 1, 0),
        ("lights out", 0, 1, 0),
        ("switch the bulb on", 0, 0, 7),
        ("turn off the lighting", 0, 1, 0),
        ("can i get some light in here", 0, 0, 7),
        ("set the bulb to level 3", 0, 2, 3),
        ("make it brighter in here", 0, 2, 6),
        ("tone down the lamp", 0, 2, 2),
        ("i would like the lamp at half brightness", 0, 2, 4),
        ("bump the light up a notch", 0, 2, 5),
        ("lamp on full brightness", 0, 0, 7),
        ("halt the engine", 1, 1, 0),
        ("quit the motor", 1, 1, 0),
        ("spin up the rotor", 1, 0, 7),
        ("run the motor at quarter speed", 1, 0, 2),
        ("give the motor some more power", 1, 2, 6),
        ("throttle the motor down", 1, 2, 2),
        ("the motor needs to stop", 1, 1, 0),
        ("set the drive to 70 percent", 1, 2, 5),
        ("start the rotor at low speed", 1, 0, 2),
        ("cut the power to the motor", 1, 1, 0),
        ("i need the motor running", 1, 0, 7),
        ("make the motor go a bit faster", 1, 2, 5),
        ("crank the fan all the way up", 2, 2, 7),
        ("kick on the ventilator", 2, 0, 7),
        ("shut the air circulator off", 2, 1, 0),
        ("get some air moving", 2, 0, 7),
        ("i want the fan running at medium", 2, 0, 4),
        ("enough with the fan", 2, 1, 0),
        ("ventilator to level 5", 2, 2, 5),
        ("slow the fan down a bit", 2, 2, 3),
        ("make the fan blow harder", 2, 2, 6),
        ("could you start the desk fan on low", 2, 0, 2),
    ],
    "애매한 표현": [
        ("it is sweltering in here", 2, 0, 6),
        ("i am roasting", 2, 0, 6),
        ("this heat is unbearable", 2, 0, 6),
        ("i am dripping with sweat", 2, 0, 6),
        ("it is slightly warm", 2, 0, 4),
        ("the air is heavy", 2, 0, 4),
        ("my hands are freezing", 2, 1, 0),
        ("i need a sweater", 2, 1, 0),
        ("it is freezing cold", 2, 1, 0),
        ("the fan is blowing my papers away", 2, 2, 2),
        ("too much wind", 2, 2, 2),
        ("i can barely see the keyboard", 0, 0, 7),
        ("it is so dark i can not find my keys", 0, 0, 7),
        ("the room is gloomy", 0, 0, 7),
        ("the lights are blinding", 0, 2, 2),
        ("it is way too bright", 0, 2, 2),
        ("my eyes hurt", 0, 2, 2),
        ("time for bed", 0, 1, 0),
        ("i am heading to bed", 0, 1, 0),
        ("good night", 0, 1, 0),
        ("the motor is making a racket", 1, 2, 2),
        ("the engine is screaming", 1, 2, 2),
        ("the motor is crawling", 1, 2, 6),
        ("the motor smells hot", 1, 1, 0),
        ("something is wrong with the engine", 1, 1, 0),
    ],
    "명령 아님": [
        (s, IGNORE, 3, IGNORE)
        for s in [
            "what is the weather like",
            "i love this song",
            "can you hear me",
            "thanks a lot",
            "who are you",
            "lets play a game",
            "how old are you",
            "i had a long day",
            "what should i eat for dinner",
            "tell me something fun",
            "good job",
            "i will be back soon",
            "do you like music",
            "i am a huge fan of soccer",
            "the fans were screaming at the concert",
            "this box is very light",
            "my uncle repairs car engines",
            "the lamp in the store was cheap",
            "electric motors are efficient",
            "light is faster than sound",
            "what does a cooling fan cost",
            "is the fan on",
            "i turned on the tv",
            "turn off your phone in class",
            "he started the car",
        ]
    ],
}
UNSEEN = [x for items in UNSEEN_GROUPS.values() for x in items]


def contractions(s):
    """it is -> it's / its, i am -> i'm / im, can not -> can't / cant 변형을 함께 만든다"""
    out = {s}
    for full, shorts in (
        ("it is", ["it's", "its"]),
        ("i am", ["i'm", "im"]),
        ("can not", ["can't", "cant"]),
    ):
        if full in s:
            out.update(s.replace(full, short) for short in shorts)
    return sorted(out)


def polite(s):
    """명령 문장 일부에 앞뒤 말을 붙여 문장 구조를 다양하게 만든다"""
    if random.random() < 0.3:
        s = random.choice(PREFIX) + s
    if random.random() < 0.3:
        s = s + random.choice(SUFFIX)
    return s


def capped(items, n):
    """조합이 너무 많은 템플릿은 무작위로 n개만 쓴다 (특정 유형이 데이터를 독차지하지 않도록)"""
    random.shuffle(items)
    return items[:n]


def build_dataset():
    amts = [(p, m) for m, ps in AMT_WORDS.items() for p in ps] + [
        (f"level {n}", n) for n in range(1, 8)
    ]
    cmd = []
    for dev, words in DEV_WORDS.items():
        on, off, on_amt, chg, rel = [], [], [], [], []
        for w in words:
            on += [(t.format(d=w), dev, 0, 7) for t in ON]
            off += [(t.format(d=w), dev, 1, 0) for t in OFF]
            on_amt += [(t.format(d=w, a=p), dev, 0, m) for t in ON_AMT for p, m in amts]
            chg += [(t.format(d=w, a=p), dev, 2, m) for t in CHG for p, m in amts]
            rel += [(t.format(d=w), dev, 2, m) for t, m in REL + REL_DEV[dev]]
        cmd += on + off + capped(on_amt, 200) + capped(chg, 300) + rel
    data = [(polite(s), d, a, m) for s, d, a, m in cmd]
    data += [(v, d, a, m) for s, d, a, m in VAGUE for v in contractions(s)] * 4
    data += [(v, IGNORE, 3, IGNORE) for s in NONE for v in contractions(s)] * 4
    data += [(t.format(o=o), IGNORE, 3, IGNORE) for t in NONE_OTHER for o in OTHER] * 2
    data += [
        (t.format(d=w), IGNORE, 3, IGNORE)
        for t in NONE_NOUN
        for words in DEV_WORDS.values()
        for w in words
    ] * 2
    leaked = {s for s, *_ in data} & {s for s, *_ in UNSEEN}
    if leaked:
        print(f"  ※ 평가 문장이 학습 데이터에 섞였습니다: {sorted(leaked)}")
    random.shuffle(data)
    return data


def build_finetune_dataset():
    data = build_dataset()
    hard_examples = load_hard_examples()
    hard_texts = {s for s, *_ in hard_examples}
    leaked = hard_texts & {s for s, *_ in UNSEEN}
    if leaked:
        raise RuntimeError(f"보강 학습 문장이 고정 평가셋과 겹칩니다: {sorted(leaked)}")
    data.extend(hard_examples * HARD_REPEAT)
    random.shuffle(data)
    print(
        f"[보강] {BASE}/hard_examples.tsv에서 어려운 예시 {len(hard_examples)}개를 "
        f"{HARD_REPEAT}회 반복 추가"
    )
    return data


def load_hard_examples():
    path = os.path.join(BASE, "hard_examples.tsv")
    examples = []
    with open(path, encoding="utf-8") as f:
        for line_no, raw in enumerate(f, 1):
            line = raw.rstrip("\r\n")
            if not line or line.lstrip().startswith("#"):
                continue
            fields = line.split("\t")
            if len(fields) != 4:
                raise ValueError(
                    f"{path}:{line_no}: text, device, action, amount 4개 열이 필요합니다"
                )
            text, device, action, amount = (field.strip() for field in fields)
            if not text or action not in ACTIONS:
                raise ValueError(
                    f"{path}:{line_no}: 문장 또는 action 라벨이 올바르지 않습니다"
                )
            if device == "-":
                dev_id = IGNORE
                if action != "none" or amount != "-":
                    raise ValueError(
                        f"{path}:{line_no}: none 예시는 device와 amount를 모두 '-'로 써야 합니다"
                    )
                amount_id = IGNORE
            else:
                if device not in DEVICES or amount not in {str(i) for i in range(8)}:
                    raise ValueError(
                        f"{path}:{line_no}: device 또는 amount 라벨이 올바르지 않습니다"
                    )
                dev_id = DEVICES.index(device)
                amount_id = int(amount)
                if action == "none":
                    raise ValueError(
                        f"{path}:{line_no}: none 예시의 device는 '-'여야 합니다"
                    )
            examples.append((text, dev_id, ACTIONS.index(action), amount_id))
    if not examples:
        raise ValueError(f"보강 예시가 없습니다: {path}")
    return examples


# ---------------------------------------------------------------- 2단계: 단어 목록 만들기
def build_vocab_ids(tok, sentences):
    vocab = tok.get_vocab()  # 토큰 -> 원래 번호
    inv = {i: t for t, i in vocab.items()}
    keep = set(tok.all_special_ids)  # [PAD] [UNK] [CLS] [SEP] [MASK]
    for s in sentences:  # 학습 문장에 나오는 토큰은 전부
        keep.update(tok(s.lower())["input_ids"])
    for ch in string.ascii_lowercase + string.digits + string.punctuation:
        for t in (ch, "##" + ch):  # 처음 보는 단어도 글자로는 쪼개지도록
            if t in vocab:
                keep.add(vocab[t])
    required = len(keep)
    if required > VOCAB_BUDGET:
        raise SystemExit(
            f"필수 토큰만 {required}개라 예산 {VOCAB_BUDGET}개를 넘습니다. VOCAB_BUDGET을 늘리세요."
        )

    # 남는 자리는 자주 쓰는 영어 토큰으로 채움.
    # BERT 어휘는 일반 토큰이 대략 빈도순으로 번호가 매겨져 있어서 번호 순서대로 채우면 된다.
    # 입력은 ASCII만 받으므로 비ASCII 토큰과 [unused..] 자리는 건너뜀.
    for i in sorted(inv):
        if len(keep) >= VOCAB_BUDGET:
            break
        t = inv[i]
        if i in keep or t.startswith("[") or not t.isascii():
            continue
        keep.add(i)
    print(
        f"[2단계] 어휘 {len(vocab):,}개 -> {len(keep):,}개 "
        f"(필수 {required}개 + 자주 쓰는 토큰 {len(keep) - required}개)"
    )
    return sorted(keep)  # [PAD]=0 이 맨 앞에 유지됨


# ---------------------------------------------------------------- 3단계: 임베딩 자르기
def prune(tok, base, keep_ids):
    inv = {i: t for t, i in tok.get_vocab().items()}
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, "vocab.txt"), "w", encoding="utf-8") as f:
        f.write(
            "\n".join(inv[i] for i in keep_ids) + "\n"
        )  # 새 번호 = 이 파일의 줄 번호

    emb = base.embeddings.word_embeddings
    new_emb = nn.Embedding(len(keep_ids), emb.embedding_dim, padding_idx=0)
    new_emb.weight.data = emb.weight.data[keep_ids].clone()  # 남길 행만 같은 순서로
    base.embeddings.word_embeddings = new_emb
    base.config.vocab_size = len(keep_ids)
    base.config.save_pretrained(OUT)

    new_tok = BertTokenizer.from_pretrained(OUT, do_lower_case=True)
    print(
        f"[3단계] 임베딩 {len(inv) * emb.embedding_dim:,} -> {len(keep_ids) * emb.embedding_dim:,} 숫자 "
        f"(FP16 기준 {len(keep_ids) * emb.embedding_dim * 2 / 1e6:.2f}MB)"
    )
    return new_tok, base


# ---------------------------------------------------------------- 모델
class Controller(nn.Module):
    def __init__(self, base):
        super().__init__()
        self.base = base
        h = base.config.hidden_size
        self.dev = nn.Linear(h, len(DEVICES))
        self.act = nn.Linear(h, len(ACTIONS))
        self.amt = nn.Linear(h, 8)

    def forward(self, **x):
        cls = self.base(**x).last_hidden_state[:, 0]  # [CLS] 위치 벡터
        return self.dev(cls), self.act(cls), self.amt(cls)


def encode(tok, sentences):
    batch = tok(
        [s.lower() for s in sentences],
        padding=True,
        truncation=True,
        max_length=MAX_LEN,
        return_tensors="pt",
    )
    return {name: value.to(DEVICE) for name, value in batch.items()}


def predict_batch(model, tok, sentences):
    model.eval()
    out = []
    with torch.no_grad():
        for i in range(0, len(sentences), 64):
            out.append(
                torch.stack(
                    [o.argmax(-1) for o in model(**encode(tok, sentences[i : i + 64]))],
                    1,
                )
            )
    return torch.cat(out)


def accuracy(model, tok, data):
    """명령 문장은 장치/동작/양 모두, none 문장은 동작만 채점"""
    pred = predict_batch(model, tok, [s for s, *_ in data])
    ok = 0
    for (_, d, a, m), (pd, pa, pm) in zip(data, pred.tolist()):
        ok += (pa == a) and (d == IGNORE or (pd == d and pm == m))
    return ok / len(data)


# ---------------------------------------------------------------- 4단계: 학습
def train(model, tok, data, epochs=EPOCHS, lr=LR):
    split = int(len(data) * 0.85)
    train_set, test_set = data[:split], data[split:]
    opt = torch.optim.AdamW(model.parameters(), lr=lr)
    ce = nn.CrossEntropyLoss(ignore_index=IGNORE)
    steps = (len(train_set) + BATCH - 1) // BATCH
    for ep in range(epochs):
        model.train()
        random.shuffle(train_set)
        total = 0.0
        for i in range(0, len(train_set), BATCH):
            b = train_set[i : i + BATCH]
            y = [
                torch.tensor(col, device=DEVICE)
                for col in zip(*[(d, a, m) for _, d, a, m in b])
            ]
            loss = sum(
                ce(o, t) for o, t in zip(model(**encode(tok, [s for s, *_ in b])), y)
            )
            opt.zero_grad()
            loss.backward()
            opt.step()
            total += loss.item()
        print(
            f"  epoch {ep + 1}/{epochs}  loss {total / steps:.3f}  "
            f"test acc {accuracy(model, tok, test_set):.1%}  "
            f"처음 보는 문장 acc {accuracy(model, tok, UNSEEN):.1%} (양까지 정확히 일치 기준)"
        )


# ---------------------------------------------------------------- 판단 & 출력
def predict(model, tok, text):
    if not text.isascii():
        return "English only (영어만 입력하세요)"
    model.eval()
    with torch.no_grad():
        out = model(**encode(tok, [text]))
    probs = [o.softmax(-1)[0] for o in out]
    d, a, m = (int(p.argmax()) for p in probs)
    if ACTIONS[a] == "none":
        return f"[NONE] -- 11 ---  |  no command  |  confidence {float(probs[1].max()):.0%}"
    conf = min(float(p.max()) for p in probs)
    if ACTIONS[a] == "off":
        m = 0  # off 이면 양은 항상 0
    tag = "OK  " if conf >= CONF_THRESHOLD else "HOLD"
    return (
        f"[{tag}] {d:02b} {a:02b} {m:03b}  |  device={DEVICES[d]:<5} action={ACTIONS[a]:<6} "
        f"amount={m}/7  |  confidence {conf:.0%}"
    )


def evaluate_unseen(model, tok):
    pred = predict_batch(model, tok, [s for s, *_ in UNSEEN]).tolist()
    scores, errors = {}, []
    i = 0
    for group, items in UNSEEN_GROUPS.items():
        hit = 0
        for s, d, a, m in items:
            pd, pa, pm = pred[i]
            i += 1
            if a == 3:
                ok = pa == 3
                expected = "none"
            else:
                ok = pd == d and pa == a and (a == 1 or abs(pm - m) <= 1)
                expected = f"{DEVICES[d]}/{ACTIONS[a]}/{m}"
            if not ok:
                predicted = "none" if pa == 3 else f"{DEVICES[pd]}/{ACTIONS[pa]}/{pm}"
                errors.append((group, s, expected, predicted))
            hit += ok
        scores[group] = (hit, len(items))
    return scores, errors


def weighted_unseen_score(scores):
    # 명령 아닌 문장을 잘못 실행하는 오류를 더 무겁게 취급한다.
    direct = scores["직접 명령"][0]
    vague = scores["애매한 표현"][0]
    none = scores["명령 아님"][0]
    return direct + vague + 2 * none


def save_unseen_errors(errors):
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, "last_unseen_errors.tsv")
    with open(path, "w", encoding="utf-8") as f:
        f.write("group\tsentence\texpected\tpredicted\n")
        for row in errors:
            f.write("\t".join(value.replace("\t", " ") for value in row) + "\n")
    return path


def report_unseen(model, tok):
    scores, errors = evaluate_unseen(model, tok)
    print("\n--- 처음 보는 문장 중 틀린 것 (양은 ±1까지 정답으로 봄) ---")
    for group, sentence, expected, _ in errors:
        print(
            f"X [{group}] {sentence:<38} 정답 {expected:<16} -> {predict(model, tok, sentence)}"
        )
    summary = [f"{group} {hit}/{total}" for group, (hit, total) in scores.items()]
    total_hit = sum(hit for hit, _ in scores.values())
    print(
        f"\n처음 보는 문장 정답률: {', '.join(summary)}  |  전체 {total_hit}/{len(UNSEEN)}"
    )
    print(f"오답 목록: {save_unseen_errors(errors)}")
    return scores


def load_pruned_model():
    tok = BertTokenizer.from_pretrained(OUT, do_lower_case=True)
    model = Controller(BertModel(BertConfig.from_pretrained(OUT)))
    model.load_state_dict(torch.load(SAVE, map_location="cpu")["state_dict"])
    return tok, model.to(DEVICE)


def save_model(model, keep_backup=False):
    if keep_backup and os.path.exists(SAVE):
        stamp = datetime.now().strftime("%Y%m%d_%H%M%S_%f")
        backup = os.path.join(OUT, f"model_finetuned_before_{stamp}.pt")
        shutil.copy2(SAVE, backup)
        print(f"기존 모델 백업: {backup}")
    cpu_state = {
        name: value.detach().cpu() for name, value in model.state_dict().items()
    }
    temp_save = SAVE + ".tmp"
    torch.save(
        {
            "state_dict": cpu_state,
            "devices": DEVICES,
            "actions": ACTIONS,
            "max_len": MAX_LEN,
        },
        temp_save,
    )
    os.replace(temp_save, SAVE)
    print(f"저장: {SAVE}")


def finetune_with_gate(tok, model):
    before_state = {
        name: value.detach().cpu().clone() for name, value in model.state_dict().items()
    }
    before = evaluate_unseen(model, tok)[0]
    before_score = weighted_unseen_score(before)
    print(f"보강 전 점수: {before}, 안전 가중 점수={before_score}")

    data = build_finetune_dataset()
    print(f"[보강 학습] 연산 장치: {DEVICE}, epoch={FINETUNE_EPOCHS}, lr={FINETUNE_LR}")
    train(model, tok, data, epochs=FINETUNE_EPOCHS, lr=FINETUNE_LR)

    after = evaluate_unseen(model, tok)[0]
    after_score = weighted_unseen_score(after)
    none_not_worse = after["명령 아님"][0] >= before["명령 아님"][0]
    improved = after_score > before_score
    print(f"보강 후 점수: {after}, 안전 가중 점수={after_score}")
    if none_not_worse and improved:
        save_model(model, keep_backup=True)
        print("결과 채택: 명령 아님 점수가 유지되고 안전 가중 점수가 올랐습니다.")
        return True

    model.load_state_dict(before_state)
    print(
        "결과 보류: 명령 아님 점수가 떨어졌거나 안전 가중 점수가 개선되지 않았습니다."
    )
    print("저장된 기존 체크포인트는 그대로 유지했습니다.")
    return False


def main():
    parser = argparse.ArgumentParser(description="BERT-Medium controller training")
    parser.add_argument(
        "--finetune",
        "--iterate",
        dest="finetune",
        action="store_true",
        help="현재 체크포인트에서 보강 학습 후 평가 기준으로 자동 채택/보류",
    )
    parser.add_argument(
        "--no-interactive", action="store_true", help="평가 후 입력 대기 없이 종료"
    )
    args = parser.parse_args()

    if args.finetune:
        if not os.path.exists(SAVE):
            raise SystemExit(
                f"이어 학습할 체크포인트가 없습니다: {SAVE}\n먼저 기본 학습을 실행하세요."
            )
        tok, model = load_pruned_model()
        print(f"기존 학습 모델에서 보강 학습: {SAVE}")
        finetune_with_gate(tok, model)
    elif os.path.exists(SAVE):
        tok, model = load_pruned_model()
        print(f"학습된 모델을 불러왔습니다: {SAVE}")
    else:
        tok = AutoTokenizer.from_pretrained(BASE)
        base = AutoModel.from_pretrained(BASE)
        c = base.config
        print(
            f"\n모델: {c.num_hidden_layers}층 × {c.hidden_size}차원, 어텐션 헤드 {c.num_attention_heads}개"
        )
        if (c.num_hidden_layers, c.hidden_size) != (8, 512):
            print(
                "  ※ BERT-Medium(8층 × 512차원)이 아닙니다. 이 모델로 계속 진행합니다."
            )

        data = build_dataset()
        keep_ids = build_vocab_ids(tok, [s for s, *_ in data])
        tok, base = prune(tok, base, keep_ids)

        model = Controller(base).to(DEVICE)
        print(f"[4단계] 학습 데이터 {len(data):,}문장, 연산 장치: {DEVICE}")
        train(model, tok, data)
        save_model(model)

    report_unseen(model, tok)

    if not args.no_interactive:
        print("\n영어 문장을 입력하세요 (빈 줄이면 종료)")
        while True:
            s = input("> ").strip()
            if not s:
                break
            print(predict(model, tok, s))


if __name__ == "__main__":
    print(
        f"연산 장치: {DEVICE}"
        + (f" ({torch.cuda.get_device_name(0)})" if DEVICE.type == "cuda" else "")
    )
    main()
