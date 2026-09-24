#include "text.h"
#include <stdlib.h>
#include <string.h>

size_t tb_utf8_fit(const uint8_t *data, size_t length, size_t capacity) {
  if (capacity == 0 || !data) { return 0; }
  size_t size = length < capacity - 1 ? length : capacity - 1;
  if (size < length) {
    while (size > 0 && (data[size] & 0xC0) == 0x80) { --size; }
  }
  return size;
}

void tb_copy_span(char *out, size_t capacity, TbSpan value) {
  if (capacity == 0) { return; }
  const size_t size = tb_utf8_fit(value.data, value.length, capacity);
  if (size > 0) { memcpy(out, value.data, size); }
  out[size] = '\0';
}

bool tb_copy_id(char *out, size_t capacity, TbSpan value, bool allow_negative) {
  if (value.length == 0 || value.length >= capacity) { return false; }
  size_t start = 0;
  if (value.data[0] == '-') {
    if (!allow_negative || value.length < 2) { return false; }
    start = 1;
  }
  if (value.data[start] == '0') { return false; }
  for (size_t index = start; index < value.length; ++index) {
    if (value.data[index] < '0' || value.data[index] > '9') { return false; }
  }
  memcpy(out, value.data, value.length);
  out[value.length] = '\0';
  int64_t parsed = 0;
  if (!tb_parse_id(out, &parsed)) {
    out[0] = '\0';
    return false;
  }
  return true;
}

bool tb_copy_uuid(char *out, size_t capacity, TbSpan value) {
  if (value.length != 36 || capacity < 37) { return false; }
  for (size_t index = 0; index < 36; ++index) {
    const uint8_t c = value.data[index];
    const bool dash = index == 8 || index == 13 || index == 18 || index == 23;
    const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    if (dash ? c != '-' : !hex) { return false; }
  }
  memcpy(out, value.data, 36);
  out[36] = '\0';
  return true;
}

bool tb_parse_id(const char *value, int64_t *out) {
  if (!value || !*value) { return false; }
  bool negative = *value == '-';
  const char *cursor = negative ? value + 1 : value;
  if (!*cursor) { return false; }
  uint64_t result = 0;
  for (; *cursor; ++cursor) {
    if (*cursor < '0' || *cursor > '9') { return false; }
    const uint64_t digit = (uint64_t)(*cursor - '0');
    if (result > UINT64_C(922337203685477580) || (result == UINT64_C(922337203685477580) && digit > 7)) {
      if (!(negative && result == UINT64_C(922337203685477580) && digit == 8 && !cursor[1])) { return false; }
      *out = INT64_MIN;
      return true;
    }
    result = result * 10 + digit;
  }
  *out = negative ? -(int64_t)result : (int64_t)result;
  return true;
}

char *tb_budget_copy(TbBudget *budget, TbSpan value, size_t max_length, bool force) {
  if (value.length == 0) { return NULL; }
  const size_t size = tb_utf8_fit(value.data, value.length, max_length + 1);
  if (size == 0) { return NULL; }
  if (!force && budget->used + size + 1 > budget->limit) { return NULL; }
  char *copy = malloc(size + 1);
  if (!copy) { return NULL; }
  memcpy(copy, value.data, size);
  copy[size] = '\0';
  budget->used += size + 1;
  return copy;
}

void tb_budget_free(TbBudget *budget, char *value) {
  if (!value) { return; }
  const size_t size = strlen(value) + 1;
  budget->used = budget->used > size ? budget->used - size : 0;
  free(value);
}

const char *tb_or_empty(const char *value) { return value ? value : ""; }
