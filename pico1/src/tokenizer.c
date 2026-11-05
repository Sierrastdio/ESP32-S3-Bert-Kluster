#include "tokenizer.h"
#include "pico_config.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Simple line-based vocabulary suitable for small prototype vocabularies.
// Greedy WordPiece searches the loaded token list; vocab IDs are line numbers.
typedef struct { const char *text; uint16_t id; } vocab_entry_t;
static vocab_entry_t vocab[4096];
static size_t vocab_count;
static uint16_t unk_id, cls_id, sep_id;
static bool have_unk, have_cls, have_sep;

bool tokenizer_load_vocab(const char *path) {
  FILE *f = fopen(path, "r");
  if (!f) return false;
  char line[160];
  while (fgets(line, sizeof(line), f) && vocab_count < 4096) {
    line[strcspn(line, "\r\n")] = 0;
    if (!line[0]) continue;
    vocab[vocab_count].text = strdup(line);
    if (!vocab[vocab_count].text) { fclose(f); return false; }
    vocab[vocab_count].id = (uint16_t)vocab_count;
    if (!strcmp(line, "[UNK]")) { unk_id = (uint16_t)vocab_count; have_unk = true; }
    if (!strcmp(line, "[CLS]")) { cls_id = (uint16_t)vocab_count; have_cls = true; }
    if (!strcmp(line, "[SEP]")) { sep_id = (uint16_t)vocab_count; have_sep = true; }
    vocab_count++;
  }
  fclose(f);
  return vocab_count && have_unk && have_cls && have_sep;
}

bool tokenizer_load_vocab_mem(const char *const *words, size_t n) {
  vocab_count = 0; have_unk = have_cls = have_sep = false;
  for (size_t i = 0; i < n && i < 4096; i++) {
    vocab[i].text = words[i];
    vocab[i].id = (uint16_t)i;
    if (!strcmp(words[i], "[UNK]")) { unk_id = (uint16_t)i; have_unk = true; }
    if (!strcmp(words[i], "[CLS]")) { cls_id = (uint16_t)i; have_cls = true; }
    if (!strcmp(words[i], "[SEP]")) { sep_id = (uint16_t)i; have_sep = true; }
    vocab_count++;
  }
  return vocab_count && have_unk && have_cls && have_sep;
}

static int find_token(const char *s) {
  for (size_t i = 0; i < vocab_count; i++)
    if (!strcmp(vocab[i].text, s)) return (int)vocab[i].id;
  return -1;
}

static bool is_punct(char c) {
  return ispunct((unsigned char)c) && c != '#' && c != '[' && c != ']';
}

static bool emit_word(const char *word, size_t len, uint16_t *ids,
                      size_t cap, size_t *n) {
  if (!len) return true;
  if (len > CONFIG_MAX_WORD_CHARS) return *n < cap ? (ids[(*n)++] = unk_id, true) : false;
  char piece[CONFIG_MAX_WORD_CHARS + 3];
  size_t offset = 0, start_n = *n;
  while (offset < len) {
    bool found = false;
    for (size_t end = len; end > offset; end--) {
      size_t p = 0;
      if (offset) { piece[p++] = '#'; piece[p++] = '#'; }
      memcpy(piece + p, word + offset, end - offset);
      piece[p + end - offset] = 0;
      int id = find_token(piece);
      if (id >= 0) {
        if (*n >= cap) return false;
        ids[(*n)++] = (uint16_t)id;
        offset = end;
        found = true;
        break;
      }
    }
    if (!found) { *n = start_n; if (*n >= cap) return false; ids[(*n)++] = unk_id; return true; }
  }
  return true;
}

bool tokenizer_encode(const char *text, uint16_t *ids, size_t capacity,
                      size_t *count) {
  if (!text || !ids || !count || !have_cls || !have_sep || capacity < 2) return false;
  size_t n = 0;
  ids[n++] = cls_id;
  char word[CONFIG_MAX_WORD_CHARS + 1];
  size_t w = 0;
  for (const unsigned char *p = (const unsigned char *)text;; p++) {
    if (*p >= 128) return false;
    char c = (char)tolower(*p);
    if (isalnum((unsigned char)c)) {
      if (w < CONFIG_MAX_WORD_CHARS) word[w++] = c;
      else { while (isalnum(p[1])) p++; w = CONFIG_MAX_WORD_CHARS + 1; }
    } else {
      if (w > CONFIG_MAX_WORD_CHARS) { if (n >= capacity) return false; ids[n++] = unk_id; }
      else if (!emit_word(word, w, ids, capacity - 1, &n)) return false;
      w = 0;
      if (is_punct(c)) {
        char punct[2] = {c, 0}; int id = find_token(punct);
        if (n >= capacity - 1) return false;
        ids[n++] = id < 0 ? unk_id : (uint16_t)id;
      }
      if (!*p) break;
    }
  }
  if (n >= capacity) return false;
  ids[n++] = sep_id;
  *count = n;
  return true;
}
