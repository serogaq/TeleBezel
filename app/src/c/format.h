#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef PBL_SDK_3
#include <pebble.h>
#else
#include <time.h>
#endif
#include "generated/localization.h"

const char *tb_error_text(const TbStrings *strings, int32_t error);
const char *tb_kind_label(const TbStrings *strings, uint8_t kind);
const char *tb_action_label(const TbStrings *strings, uint8_t action);
const char *tb_account_state_text(const TbStrings *strings, uint8_t state);
void tb_format_duration(char *out, size_t size, uint16_t seconds);
void tb_format_content(char *out, size_t size, const TbStrings *strings, uint8_t kind, uint8_t action, uint16_t duration,
                       const char *extra, const char *text);
void tb_format_time(char *out, size_t size, time_t date, time_t now, bool clock24);
void tb_format_day(char *out, size_t size, const TbStrings *strings, time_t date, time_t now);
bool tb_same_day(time_t left, time_t right);
void tb_format_badge(char *out, size_t size, uint16_t unread, uint8_t flags);
