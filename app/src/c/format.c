#include "format.h"
#ifndef PBL_SDK_3
#include <stdio.h>
#endif
#include <string.h>
#include "errors.h"
#include "generated/protocol.h"

const char *tb_error_text(const TbStrings *strings, int32_t error) {
  switch (error) {
    case TB_RESULT_CONFIG_MISSING: return strings->config_missing;
    case TB_RESULT_CONFIG_INVALID: return strings->config_invalid;
    case TB_RESULT_BACKEND_UNAVAILABLE: return strings->backend_unavailable;
    case TB_RESULT_API_UNAUTHORIZED: return strings->api_unauthorized;
    case TB_RESULT_WRONG_TOKEN_TYPE: return strings->wrong_token_type;
    case TB_RESULT_BACKEND_NOT_READY: return strings->backend_not_ready;
    case TB_RESULT_ACCOUNT_NEEDS_LOGIN: return strings->account_needs_login;
    case TB_RESULT_ACCOUNT_GONE: return strings->account_gone;
    case TB_RESULT_CHAT_NOT_FOUND: return strings->chat_not_found;
    case TB_RESULT_RATE_LIMITED: return strings->rate_limited;
    case TB_RESULT_BUSY: return strings->busy;
    case TB_RESULT_CURSOR_LOST: return strings->busy;
    case TB_RESULT_TOO_MANY_VIEWS: return strings->too_many_views;
    case TB_RESULT_MESSAGE_UNAVAILABLE: return strings->message_unavailable;
    case TB_RESULT_SEND_FORBIDDEN: return strings->send_forbidden;
    case TB_RESULT_REPLY_UNAVAILABLE: return strings->reply_unavailable;
    case TB_RESULT_TEXT_TOO_LONG: return strings->text_too_long_error;
    case TB_RESULT_DRAFT_LOST: return strings->draft_lost;
    case TB_RESULT_SEND_UNKNOWN: return strings->send_unknown;
    case TB_RESULT_SEND_RATE_LIMITED: return strings->send_rate_limited;
    case TB_ERROR_PHONE_UNREACHABLE: return strings->phone_unreachable;
    case TB_ERROR_NO_RESPONSE: return strings->no_response;
    case TB_ERROR_OUT_OF_MEMORY: return strings->out_of_memory;
    default: return strings->protocol_error;
  }
}

const char *tb_kind_label(const TbStrings *strings, uint8_t kind) {
  switch (kind) {
    case TB_KIND_PHOTO: return strings->kind_photo;
    case TB_KIND_VIDEO: return strings->kind_video;
    case TB_KIND_VOICE_NOTE: return strings->kind_voice_note;
    case TB_KIND_VIDEO_NOTE: return strings->kind_video_note;
    case TB_KIND_STICKER: return strings->kind_sticker;
    case TB_KIND_DOCUMENT: return strings->kind_document;
    case TB_KIND_AUDIO: return strings->kind_audio;
    case TB_KIND_ANIMATION: return strings->kind_animation;
    case TB_KIND_LOCATION: return strings->kind_location;
    case TB_KIND_VENUE: return strings->kind_venue;
    case TB_KIND_CONTACT: return strings->kind_contact;
    case TB_KIND_POLL: return strings->kind_poll;
    case TB_KIND_DICE: return strings->kind_dice;
    case TB_KIND_CALL: return strings->kind_call;
    case TB_KIND_GAME: return strings->kind_game;
    case TB_KIND_STORY: return strings->kind_story;
    case TB_KIND_EXPIRED: return strings->kind_expired;
    case TB_KIND_PAID_MEDIA: return strings->kind_paid_media;
    case TB_KIND_SERVICE: return strings->kind_service;
    default: return strings->kind_unsupported;
  }
}

const char *tb_action_label(const TbStrings *strings, uint8_t action) {
  switch (action) {
    case TB_ACTION_MEMBERS_ADDED: return strings->action_members_added;
    case TB_ACTION_MEMBER_JOINED: return strings->action_member_joined;
    case TB_ACTION_MEMBER_LEFT: return strings->action_member_left;
    case TB_ACTION_TITLE_CHANGED: return strings->action_title_changed;
    case TB_ACTION_PHOTO_CHANGED: return strings->action_photo_changed;
    case TB_ACTION_CHAT_CREATED: return strings->action_chat_created;
    case TB_ACTION_PINNED: return strings->action_pinned;
    case TB_ACTION_SCREENSHOT: return strings->action_screenshot;
    case TB_ACTION_CONTACT_JOINED: return strings->action_contact_joined;
    case TB_ACTION_VIDEO_CHAT: return strings->action_video_chat;
    case TB_ACTION_AUTO_DELETE: return strings->action_auto_delete;
    case TB_ACTION_UPGRADED: return strings->action_upgraded;
    case TB_ACTION_TOPIC_CREATED: return strings->action_topic_created;
    default: return strings->kind_service;
  }
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
