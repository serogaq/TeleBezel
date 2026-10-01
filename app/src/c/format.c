#include "format.h"
#ifndef PBL_SDK_3
#include <stdio.h>
#endif
#include <stddef.h>
#include <string.h>
#include "errors.h"
#include "generated/protocol.h"

#define TB_STRING(name) ((uint8_t)(offsetof(TbStrings, name) / sizeof(const char *)))

static const uint8_t s_result_strings[] = {
  0, TB_STRING(config_missing), TB_STRING(config_invalid), TB_STRING(backend_unavailable), TB_STRING(api_unauthorized),
  TB_STRING(backend_not_ready), TB_STRING(protocol_error), TB_STRING(account_needs_login), TB_STRING(account_gone), TB_STRING(chat_not_found),
  TB_STRING(rate_limited), TB_STRING(busy), TB_STRING(busy), TB_STRING(too_many_views), TB_STRING(message_unavailable), TB_STRING(wrong_token_type),
  TB_STRING(send_forbidden), TB_STRING(reply_unavailable), TB_STRING(text_too_long_error), TB_STRING(draft_lost), TB_STRING(send_unknown),
  TB_STRING(send_rate_limited)};

static const uint8_t s_local_strings[] = {TB_STRING(phone_unreachable), TB_STRING(no_response), TB_STRING(out_of_memory)};

static const char *string_at(const TbStrings *strings, uint8_t index) { return ((const char *const *)(const void *)strings)[index]; }

const char *tb_error_text(const TbStrings *strings, int32_t error) {
  if (error > 0 && error < (int32_t)sizeof(s_result_strings)) { return string_at(strings, s_result_strings[error]); }
  if (error >= TB_ERROR_PHONE_UNREACHABLE && error <= TB_ERROR_OUT_OF_MEMORY) { return string_at(strings, s_local_strings[error - TB_ERROR_PHONE_UNREACHABLE]); }
  return strings->protocol_error;
}

const char *tb_kind_label(const TbStrings *strings, uint8_t kind) {
  if (kind >= TB_KIND_PHOTO && kind <= TB_KIND_UNSUPPORTED) { return string_at(strings, (uint8_t)(TB_STRING(kind_photo) + kind - TB_KIND_PHOTO)); }
  return strings->kind_unsupported;
}

const char *tb_action_label(const TbStrings *strings, uint8_t action) {
  if (action >= TB_ACTION_MEMBERS_ADDED && action <= TB_ACTION_TOPIC_CREATED) {
    return string_at(strings, (uint8_t)(TB_STRING(action_members_added) + action - TB_ACTION_MEMBERS_ADDED));
  }
  return strings->kind_service;
}

const char *tb_account_state_text(const TbStrings *strings, uint8_t state) {
  switch (state) {
    case TB_ACCOUNT_STATE_READY: return strings->state_ready;
    case TB_ACCOUNT_STATE_NEEDS_LOGIN: return strings->state_needs_login;
    case TB_ACCOUNT_STATE_LOGGING_OUT: return strings->state_logging_out;
    case TB_ACCOUNT_STATE_REMOVING: return strings->state_removing;
    default: return strings->state_connecting;
  }
}

void tb_format_duration(char *out, size_t size, uint16_t seconds) {
  snprintf(out, size, "%d:%02d", seconds / 60, seconds % 60);
}

static bool has_duration(uint8_t kind) {
  return kind == TB_KIND_VOICE_NOTE || kind == TB_KIND_VIDEO_NOTE || kind == TB_KIND_AUDIO || kind == TB_KIND_VIDEO ||
         kind == TB_KIND_CALL;
}

void tb_format_content(char *out, size_t size, const TbStrings *strings, uint8_t kind, uint8_t action, uint16_t duration,
                       const char *extra, const char *text) {
  if (size == 0) { return; }
  extra = extra ? extra : "";
  text = text ? text : "";
  if (kind == TB_KIND_TEXT || kind == TB_KIND_NONE) {
    snprintf(out, size, "%s", text);
    return;
  }
  if (kind == TB_KIND_SERVICE) {
    if (action == TB_ACTION_CUSTOM && *text) {
      snprintf(out, size, "%s", text);
    } else if (*extra) {
      snprintf(out, size, "%s: %s", tb_action_label(strings, action), extra);
    } else {
      snprintf(out, size, "%s", tb_action_label(strings, action));
    }
    return;
  }
  char detail[64] = "";
  if (*extra) {
    snprintf(detail, sizeof(detail), " %s", extra);
  } else if (has_duration(kind) && duration > 0) {
    char time[16];
    tb_format_duration(time, sizeof(time), duration);
    snprintf(detail, sizeof(detail), " %s", time);
  }
  if (*text) {
    snprintf(out, size, "[%s%s] %s", tb_kind_label(strings, kind), detail, text);
  } else {
    snprintf(out, size, "[%s%s]", tb_kind_label(strings, kind), detail);
  }
}

void tb_format_message(char *out, size_t size, const TbStrings *strings, uint8_t kind, uint8_t action, uint16_t duration,
                       const char *extra, const char *text, uint8_t media, uint8_t count) {
  text = text ? text : "";
  const bool paid = kind == TB_KIND_PAID_MEDIA;
  const char *mark = (media & TB_MEDIA_FLAG_RESTRICTED) && !paid ? strings->media_disappearing
                     : (media & TB_MEDIA_FLAG_SPOILER)          ? strings->media_spoiler
                                                                : "";
  char detail[64];
  if ((media & TB_MEDIA_FLAG_ALBUM) && count > 1) {
    snprintf(detail, sizeof(detail), "%s %d%s%s", paid ? tb_kind_label(strings, kind) : strings->media_album, count, *mark ? " · " : "", mark);
    snprintf(out, size, *text ? "[%s] %s" : "[%s]", detail, text);
    return;
  }
  if (*mark) {
    snprintf(detail, sizeof(detail), "%s%s· %s", extra ? extra : "", extra && *extra ? " " : "", mark);
    extra = detail;
  }
  tb_format_content(out, size, strings, kind, action, duration, extra, text);
}

bool tb_same_day(time_t left, time_t right) {
  struct tm a = *localtime(&left);
  struct tm b = *localtime(&right);
  return a.tm_year == b.tm_year && a.tm_yday == b.tm_yday;
}

void tb_format_clock(char *out, size_t size, time_t date, bool clock24) {
  struct tm value = *localtime(&date);
  if (clock24) {
    snprintf(out, size, "%02d:%02d", value.tm_hour, value.tm_min);
  } else {
    const int hour = value.tm_hour % 12 == 0 ? 12 : value.tm_hour % 12;
    snprintf(out, size, "%d:%02d%s", hour, value.tm_min, value.tm_hour < 12 ? "am" : "pm");
  }
}

void tb_format_time(char *out, size_t size, time_t date, time_t now, bool clock24) {
  struct tm value = *localtime(&date);
  if (tb_same_day(date, now)) {
    tb_format_clock(out, size, date, clock24);
    return;
  }
  struct tm current = *localtime(&now);
  if (value.tm_year == current.tm_year) {
    snprintf(out, size, "%02d.%02d", value.tm_mday, value.tm_mon + 1);
  } else {
    snprintf(out, size, "%02d.%02d.%02d", value.tm_mday, value.tm_mon + 1, value.tm_year % 100);
  }
}

void tb_format_ago(char *out, size_t size, const TbStrings *strings, time_t date, time_t now, bool clock24) {
  const time_t elapsed = now > date ? now - date : 0;
  if (elapsed < 60) {
    snprintf(out, size, "%s", strings->just_now);
    return;
  }
  if (elapsed < 3600) {
    snprintf(out, size, strings->minutes_ago, (int)(elapsed / 60));
    return;
  }
  char clock[16];
  tb_format_clock(clock, sizeof(clock), date, clock24);
  if (elapsed < 86400) {
    snprintf(out, size, "%s", clock);
  } else if (elapsed < 172800) {
    snprintf(out, size, strings->yesterday_at, clock);
  } else {
    struct tm value = *localtime(&date);
    char day[16];
    snprintf(day, sizeof(day), "%02d.%02d", value.tm_mday, value.tm_mon + 1);
    snprintf(out, size, strings->day_at, day, clock);
  }
}

void tb_format_day(char *out, size_t size, const TbStrings *strings, time_t date, time_t now) {
  if (tb_same_day(date, now)) {
    snprintf(out, size, "%s", strings->today);
    return;
  }
  if (tb_same_day(date, now - 86400)) {
    snprintf(out, size, "%s", strings->yesterday);
    return;
  }
  struct tm value = *localtime(&date);
  snprintf(out, size, "%02d.%02d.%04d", value.tm_mday, value.tm_mon + 1, value.tm_year + 1900);
}

void tb_format_count(char *out, size_t size, uint32_t value) {
  if (value > 9999) {
    snprintf(out, size, "%luk", (unsigned long)(value / 1000 > 999 ? 999 : value / 1000));
  } else {
    snprintf(out, size, "%lu", (unsigned long)value);
  }
}

void tb_format_badge(char *out, size_t size, uint16_t unread, uint8_t flags) {
  if (unread > 99) {
    snprintf(out, size, "%s99+", (flags & TB_CHAT_FLAG_MENTION) ? "@" : "");
  } else if (unread > 0) {
    snprintf(out, size, "%s%d", (flags & TB_CHAT_FLAG_MENTION) ? "@" : "", unread);
  } else if (flags & TB_CHAT_FLAG_MARKED_UNREAD) {
    snprintf(out, size, "%s", "\xE2\x80\xA2");
  } else {
    out[0] = '\0';
  }
}
