#include "notify.h"
#include <stdio.h>
#include <string.h>

static uint32_t now(const TbNotify *notify) { return notify->ports.now(notify->ports.context); }

static bool expired(uint32_t current, uint32_t at) { return at != 0 && (int32_t)(current - at) >= 0; }

static int top_index(const TbNotify *notify) {
  int best = -1;
  for (int index = 0; index < notify->count; ++index) {
    const TbNotification *item = &notify->items[index];
    if (best < 0 || item->level > notify->items[best].level ||
        (item->level == notify->items[best].level && item->serial > notify->items[best].serial)) {
      best = index;
    }
  }
  return best;
}

static void remove_at(TbNotify *notify, int index) {
  for (int next = index + 1; next < notify->count; ++next) { notify->items[next - 1] = notify->items[next]; }
  --notify->count;
  memset(&notify->items[notify->count], 0, sizeof(TbNotification));
}

static void arm(TbNotify *notify) {
  notify->ports.cancel(notify->ports.context);
  const uint32_t current = now(notify);
  bool any = false;
  uint32_t earliest = 0;
  for (int index = 0; index < notify->count; ++index) {
    const uint32_t at = notify->items[index].expires;
    if (at == 0) { continue; }
    const uint32_t remaining = expired(current, at) ? 0 : at - current;
    if (!any || remaining < earliest) {
      earliest = remaining;
      any = true;
    }
  }
  if (any) { notify->ports.schedule(notify->ports.context, earliest == 0 ? 1 : earliest); }
}

static void changed(TbNotify *notify) {
  if (notify->count == 0) { notify->focused = false; }
  arm(notify);
  notify->ports.changed(notify->ports.context);
}

void tb_notify_init(TbNotify *notify, TbNotifyPorts ports) {
  memset(notify, 0, sizeof(*notify));
  notify->ports = ports;
}

bool tb_notify_post(TbNotify *notify, TbNotifySource source, TbNotifyLevel level, TbNotifyAction action, const char *title, const char *body) {
  int slot = -1;
  for (int index = 0; index < notify->count; ++index) {
    if (notify->items[index].source == source) { slot = index; }
  }
  if (slot >= 0) {
    TbNotification *existing = &notify->items[slot];
    if (existing->level == level && existing->action == action && strcmp(existing->title, title ? title : "") == 0 &&
        strcmp(existing->body, body ? body : "") == 0) {
      existing->expires = level == TB_NOTIFY_INFO ? now(notify) + TB_NOTIFY_INFO_TTL : 0;
      if (existing->expires == 0 && level == TB_NOTIFY_INFO) { existing->expires = 1; }
      arm(notify);
      return true;
    }
  } else if (notify->count < TB_NOTIFY_CAPACITY) {
    slot = notify->count++;
  } else {
    int weakest = 0;
    for (int index = 1; index < notify->count; ++index) {
      const TbNotification *item = &notify->items[index];
      if (item->level < notify->items[weakest].level ||
          (item->level == notify->items[weakest].level && item->serial < notify->items[weakest].serial)) {
        weakest = index;
      }
    }
    if (notify->items[weakest].level > level) { return false; }
    slot = weakest;
  }
  TbNotification *item = &notify->items[slot];
  memset(item, 0, sizeof(*item));
  item->source = source;
  item->level = level;
  item->action = action;
  item->serial = ++notify->serial;
  if (level == TB_NOTIFY_INFO) {
    item->expires = now(notify) + TB_NOTIFY_INFO_TTL;
    if (item->expires == 0) { item->expires = 1; }
  }
  snprintf(item->title, sizeof(item->title), "%s", title ? title : "");
  snprintf(item->body, sizeof(item->body), "%s", body ? body : "");
  changed(notify);
  return true;
}

void tb_notify_clear(TbNotify *notify, TbNotifySource source) {
  bool removed = false;
  for (int index = notify->count - 1; index >= 0; --index) {
    if (notify->items[index].source == source) {
      remove_at(notify, index);
      removed = true;
    }
  }
  if (removed) { changed(notify); }
}

void tb_notify_dismiss(TbNotify *notify) {
  const int index = top_index(notify);
  if (index < 0) { return; }
  remove_at(notify, index);
  notify->focused = false;
  changed(notify);
}

const TbNotification *tb_notify_top(const TbNotify *notify) {
  const int index = top_index(notify);
  return index < 0 ? NULL : &notify->items[index];
}

void tb_notify_focus(TbNotify *notify, bool focused) {
  const bool value = focused && notify->count > 0;
  if (notify->focused == value) { return; }
  notify->focused = value;
  notify->ports.changed(notify->ports.context);
}

void tb_notify_timer(TbNotify *notify) {
  const uint32_t current = now(notify);
  bool removed = false;
  for (int index = notify->count - 1; index >= 0; --index) {
    if (expired(current, notify->items[index].expires)) {
      remove_at(notify, index);
      removed = true;
    }
  }
  if (removed) { changed(notify); }
  else { arm(notify); }
}

void tb_notify_reset(TbNotify *notify) {
  notify->ports.cancel(notify->ports.context);
  memset(notify->items, 0, sizeof(notify->items));
  notify->count = 0;
  notify->focused = false;
}
