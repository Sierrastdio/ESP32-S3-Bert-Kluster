import sys
words = [l.rstrip("\r\n") for l in open(sys.argv[1], encoding="utf-8")]
words = [w for w in words if w]          # 기존 로더처럼 빈 줄은 건너뜀 (ID = 남은 줄 번호)
def esc(s): return s.replace("\\", "\\\\").replace('"', '\\"')
with open(sys.argv[2], "w", encoding="utf-8") as f:
    f.write('#include <stddef.h>\n#include "vocab_data.h"\n')
    f.write("const char *const vocab_words[] = {\n")
    for w in words: f.write(f'  "{esc(w)}",\n')
    f.write("};\n")
    f.write(f"const size_t vocab_word_count = {len(words)};\n")