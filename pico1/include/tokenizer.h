#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool tokenizer_load_vocab(const char *path);
bool tokenizer_encode(const char *text, uint16_t *ids, size_t capacity,
                      size_t *count);
