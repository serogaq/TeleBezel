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
  if (layer->submitting) {
    layer->immediate = outcome;
    return;
  }
  if (callback) {
    const TbResponse response = {.outcome = outcome, .result = result, .final = true, .total = 1};
    callback(owner, sequence, &response);
  }
}

static void attempt(TbRequestLayer *layer, TbRequestSlot *slot) {
  const uint32_t now = layer->ports.now(layer->ports.context);
  while (slot->attempts < slot->max_attempts) {
    const TbSendResult sent = layer->ports.send(layer->ports.context, slot->sequence, &slot->args);
    if (sent == TB_SEND_BUSY) {
      if (!slot->queued) { slot->queued_since = now; }
      if (slot->queued && expired(now, slot->queued_since + slot->timeout)) { break; }
      slot->queued = true;
      slot->deadline = now + TB_QUEUE_RETRY_MS;
      arm(layer);
      return;
    }
    ++slot->attempts;
    slot->queued = false;
    if (sent == TB_SEND_OK) {
      slot->deadline = now + slot->timeout;
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
  layer->submitting = false;
  layer->immediate = TB_OUTCOME_TIMEOUT;
  for (int index = 0; index < TB_REQUEST_SLOTS; ++index) { layer->slots[index].active = false; }
}

uint32_t tb_requests_submit(TbRequestLayer *layer, const TbRequestArgs *args, uint32_t timeout, uint8_t max_attempts,
                            bool resend_on_timeout, TbRequestCallback callback, void *owner) {
  TbRequestSlot *slot = NULL;
  for (int index = 0; index < TB_REQUEST_SLOTS && !slot; ++index) {
    if (!layer->slots[index].active) { slot = &layer->slots[index]; }
  }
  if (!slot || max_attempts == 0 || !args) {
    layer->immediate = TB_OUTCOME_TIMEOUT;
    return 0;
  }
  do { ++layer->next_sequence; } while (layer->next_sequence == 0 || layer->next_sequence > INT32_MAX || find(layer, layer->next_sequence));
  slot->active = true;
  slot->queued = false;
  slot->resend_on_timeout = resend_on_timeout;
  slot->unreachable = false;
  slot->attempts = 0;
  slot->max_attempts = max_attempts;
  slot->next_chunk = 0;
  slot->sequence = layer->next_sequence;
  slot->timeout = timeout;
  slot->callback = callback;
  slot->owner = owner;
  slot->args = *args;
  const uint32_t sequence = slot->sequence;
  layer->submitting = true;
  layer->immediate = TB_OUTCOME_TIMEOUT;
  attempt(layer, slot);
  layer->submitting = false;
  return slot->active && slot->sequence == sequence ? sequence : 0;
}

bool tb_requests_chunk(TbRequestLayer *layer, uint32_t sequence, const TbResponse *chunk) {
  TbRequestSlot *slot = find(layer, sequence);
  if (!slot) { return false; }
  if (chunk->total == 0 || chunk->index >= chunk->total || chunk->index != slot->next_chunk) {
    complete(layer, slot, TB_OUTCOME_PROTOCOL, 0);
    return true;
  }
  TbResponse delivered = *chunk;
  delivered.outcome = TB_OUTCOME_RESPONSE;
  delivered.final = chunk->index + 1 == chunk->total;
  const TbRequestCallback callback = slot->callback;
  void *owner = slot->owner;
  if (delivered.final) {
    slot->active = false;
  } else {
    ++slot->next_chunk;
    slot->deadline = layer->ports.now(layer->ports.context) + slot->timeout;
  }
  arm(layer);
  if (callback) { callback(owner, sequence, &delivered); }
  return true;
}

bool tb_requests_response(TbRequestLayer *layer, uint32_t sequence, int32_t result) {
  const TbResponse response = {.outcome = TB_OUTCOME_RESPONSE, .result = result, .index = 0, .total = 1};
  return tb_requests_chunk(layer, sequence, &response);
}

void tb_requests_send_failed(TbRequestLayer *layer, uint32_t sequence, bool unreachable) {
  TbRequestSlot *slot = find(layer, sequence);
  if (!slot || slot->next_chunk > 0) { return; }
  slot->unreachable = unreachable;
  attempt(layer, slot);
}

void tb_requests_outbox_ready(TbRequestLayer *layer) {
  for (int index = 0; index < TB_REQUEST_SLOTS; ++index) {
    TbRequestSlot *slot = &layer->slots[index];
    if (slot->active && slot->queued) {
      attempt(layer, slot);
      return;
    }
  }
}

void tb_requests_tick(TbRequestLayer *layer) {
  const uint32_t now = layer->ports.now(layer->ports.context);
  for (int index = 0; index < TB_REQUEST_SLOTS; ++index) {
    TbRequestSlot *slot = &layer->slots[index];
    if (!slot->active || !expired(now, slot->deadline)) { continue; }
    if (slot->queued || (slot->resend_on_timeout && slot->next_chunk == 0)) {
      slot->unreachable = false;
      attempt(layer, slot);
    } else {
      complete(layer, slot, TB_OUTCOME_TIMEOUT, 0);
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

TbRequestOutcome tb_requests_immediate_outcome(const TbRequestLayer *layer) { return layer->immediate; }
