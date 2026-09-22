#include "request_layer.h"
#include <stddef.h>

static bool expired(uint32_t now, uint32_t deadline) { return (int32_t)(now - deadline) >= 0; }

static TbRequestSlot *find(TbRequestLayer *layer, uint32_t sequence) {
  if (sequence == 0) { return NULL; }
  for (int index = 0; index < TB_REQUEST_SLOTS; ++index) {
    if (layer->slots[index].active && layer->slots[index].sequence == sequence) { return &layer->slots[index]; }
  }
  return NULL;
}

static void arm(TbRequestLayer *layer) {
  layer->ports.cancel(layer->ports.context);
  const uint32_t now = layer->ports.now(layer->ports.context);
  bool any = false;
  uint32_t earliest = 0;
  for (int index = 0; index < TB_REQUEST_SLOTS; ++index) {
    const TbRequestSlot *slot = &layer->slots[index];
    if (!slot->active) { continue; }
    const uint32_t remaining = expired(now, slot->deadline) ? 0 : slot->deadline - now;
    if (!any || remaining < earliest) { earliest = remaining; any = true; }
  }
  if (any) { layer->ports.schedule(layer->ports.context, earliest == 0 ? 1 : earliest); }
}

static void complete(TbRequestLayer *layer, TbRequestSlot *slot, TbRequestOutcome outcome, int32_t result) {
  const uint32_t sequence = slot->sequence;
  const TbRequestCallback callback = slot->callback;
  void *owner = slot->owner;
  slot->active = false;
  arm(layer);
  if (callback) { callback(owner, sequence, outcome, result); }
}

static void attempt(TbRequestLayer *layer, TbRequestSlot *slot) {
  while (slot->attempts < slot->max_attempts) {
    ++slot->attempts;
    const TbSendResult sent = layer->ports.send(layer->ports.context, slot->kind, slot->sequence);
    if (sent == TB_SEND_OK) {
      slot->deadline = layer->ports.now(layer->ports.context) + slot->timeout;
      arm(layer);
      return;
    }
    slot->unreachable = sent == TB_SEND_UNREACHABLE;
  }
  complete(layer, slot, slot->unreachable ? TB_OUTCOME_UNREACHABLE : TB_OUTCOME_TIMEOUT, 0);
}

void tb_requests_init(TbRequestLayer *layer, TbRequestPorts ports) {
  layer->ports = ports;
  layer->next_sequence = 0;
  for (int index = 0; index < TB_REQUEST_SLOTS; ++index) { layer->slots[index].active = false; }
}

uint32_t tb_requests_submit(TbRequestLayer *layer, uint8_t kind, uint32_t timeout, uint8_t max_attempts,
                            TbRequestCallback callback, void *owner, uint32_t *sequence_out) {
  if (sequence_out) { *sequence_out = 0; }
  TbRequestSlot *slot = NULL;
  for (int index = 0; index < TB_REQUEST_SLOTS && !slot; ++index) {
    if (!layer->slots[index].active) { slot = &layer->slots[index]; }
  }
  if (!slot || max_attempts == 0) { return 0; }
  do { ++layer->next_sequence; } while (layer->next_sequence == 0 || layer->next_sequence > INT32_MAX || find(layer, layer->next_sequence));
  slot->active = true;
  slot->kind = kind;
  slot->attempts = 0;
  slot->max_attempts = max_attempts;
  slot->unreachable = false;
  slot->sequence = layer->next_sequence;
  slot->timeout = timeout;
  slot->callback = callback;
  slot->owner = owner;
  const uint32_t sequence = slot->sequence;
  if (sequence_out) { *sequence_out = sequence; }
  attempt(layer, slot);
  return sequence;
}

bool tb_requests_response(TbRequestLayer *layer, uint32_t sequence, int32_t result) {
  TbRequestSlot *slot = find(layer, sequence);
  if (!slot) { return false; }
  complete(layer, slot, TB_OUTCOME_RESPONSE, result);
  return true;
}

void tb_requests_send_failed(TbRequestLayer *layer, uint32_t sequence, bool unreachable) {
  TbRequestSlot *slot = find(layer, sequence);
  if (!slot) { return; }
  slot->unreachable = unreachable;
  attempt(layer, slot);
}

void tb_requests_tick(TbRequestLayer *layer) {
  const uint32_t now = layer->ports.now(layer->ports.context);
  for (int index = 0; index < TB_REQUEST_SLOTS; ++index) {
    TbRequestSlot *slot = &layer->slots[index];
    if (slot->active && expired(now, slot->deadline)) {
      slot->unreachable = false;
      attempt(layer, slot);
    }
  }
  arm(layer);
}

void tb_requests_cancel(TbRequestLayer *layer, uint32_t sequence) {
  TbRequestSlot *slot = find(layer, sequence);
  if (slot) { complete(layer, slot, TB_OUTCOME_CANCELLED, 0); }
}

void tb_requests_cancel_all(TbRequestLayer *layer) {
  for (int index = 0; index < TB_REQUEST_SLOTS; ++index) {
    if (layer->slots[index].active) { complete(layer, &layer->slots[index], TB_OUTCOME_CANCELLED, 0); }
  }
  layer->ports.cancel(layer->ports.context);
}

bool tb_requests_pending(const TbRequestLayer *layer, uint32_t sequence) {
  for (int index = 0; index < TB_REQUEST_SLOTS; ++index) {
    if (layer->slots[index].active && layer->slots[index].sequence == sequence) { return true; }
  }
  return false;
}
