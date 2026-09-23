#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "codec.h"

typedef struct {
  size_t used;
  size_t limit;
} TbBudget;

size_t tb_utf8_fit(const uint8_t *data, size_t length, size_t capacity);
void tb_copy_span(char *out, size_t capacity, TbSpan value);
bool tb_copy_id(char *out, size_t capacity, TbSpan value, bool allow_negative);
bool tb_copy_uuid(char *out, size_t capacity, TbSpan value);
bool tb_parse_id(const char *value, int64_t *out);
char *tb_budget_copy(TbBudget *budget, TbSpan value, size_t max_length, bool force);
void tb_budget_free(TbBudget *budget, char *value);
const char *tb_or_empty(const char *value);
