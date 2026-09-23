#include "pull.h"
#include <string.h>

static uint32_t now(const TbPull *pull) { return pull->ports.now(pull->ports.context); }

static void changed(TbPull *pull) { pull->ports.changed(pull->ports.context); }

static void animate(TbPull *pull, TbPullPhase phase, uint16_t to, uint32_t duration) {
  pull->phase = phase;
  pull->from = pull->level;
  pull->to = to;
  pull->started = now(pull);
  pull->duration = duration;
  pull->ports.schedule(pull->ports.context, TB_PULL_FRAME);
}

static void fire(TbPull *pull) {
  pull->level = TB_PULL_FULL;
  pull->armed_until = 0;
  animate(pull, TB_PULL_HOLDING, TB_PULL_FULL, TB_PULL_HOLD);
  pull->ports.trigger(pull->ports.context);
}

static uint16_t eased(const TbPull *pull, uint32_t elapsed) {
  if (pull->duration == 0 || elapsed >= pull->duration) { return pull->to; }
  const int32_t span = (int32_t)pull->to - (int32_t)pull->from;
  const int32_t progress = (int32_t)(elapsed * 1000 / pull->duration);
  const int32_t smooth = progress * progress * (3000 - 2 * progress) / 1000000;
  return (uint16_t)((int32_t)pull->from + span * smooth / 1000);
}

static void settle(TbPull *pull) {
  switch (pull->phase) {
    case TB_PULL_RISING_HALF: {
      const uint32_t current = now(pull);
      pull->phase = TB_PULL_WAITING;
      const uint32_t left = pull->armed_until > current ? pull->armed_until - current : 0;
      pull->ports.schedule(pull->ports.context, left > 0 ? left : 1);
      break;
    }
    case TB_PULL_WAITING:
      pull->armed_until = 0;
      animate(pull, TB_PULL_FALLING, 0, TB_PULL_TAP_FALL);
      break;
    case TB_PULL_RISING_FULL:
      fire(pull);
      break;
    case TB_PULL_HOLDING:
      animate(pull, TB_PULL_FALLING, 0, TB_PULL_FADE);
      break;
    case TB_PULL_FALLING:
      pull->phase = TB_PULL_IDLE;
      break;
    default:
      break;
  }
}

void tb_pull_init(TbPull *pull, TbPullPorts ports) {
  memset(pull, 0, sizeof(*pull));
  pull->ports = ports;
}

void tb_pull_press(TbPull *pull) {
  if (pull->phase == TB_PULL_RISING_FULL || pull->phase == TB_PULL_HOLDING || pull->phase == TB_PULL_DRAGGING) { return; }
  const uint32_t current = now(pull);
  if (pull->armed_until && current < pull->armed_until) {
    const uint32_t duration = (uint32_t)(TB_PULL_FULL - pull->level) * TB_PULL_DOUBLE_RISE / TB_PULL_FULL;
    animate(pull, TB_PULL_RISING_FULL, TB_PULL_FULL, duration > 0 ? duration : 1);
    return;
  }
  pull->armed_until = current + TB_PULL_WINDOW;
  const uint32_t duration = pull->level >= TB_PULL_HALF ? 1 : (uint32_t)(TB_PULL_HALF - pull->level) * TB_PULL_TAP_RISE / TB_PULL_HALF;
  animate(pull, TB_PULL_RISING_HALF, TB_PULL_HALF, duration);
}

void tb_pull_drag(TbPull *pull, uint16_t level) {
  if (pull->phase == TB_PULL_RISING_FULL || pull->phase == TB_PULL_HOLDING) { return; }
  pull->ports.cancel(pull->ports.context);
  pull->phase = TB_PULL_DRAGGING;
  pull->armed_until = 0;
  pull->level = level > TB_PULL_FULL ? TB_PULL_FULL : level;
  changed(pull);
}

void tb_pull_release(TbPull *pull) {
  if (pull->phase != TB_PULL_DRAGGING) { return; }
  if (pull->level >= TB_PULL_FULL) {
    fire(pull);
  } else {
    animate(pull, TB_PULL_FALLING, 0, (uint32_t)pull->level * TB_PULL_RETURN / TB_PULL_FULL + 1);
  }
  changed(pull);
}

void tb_pull_tick(TbPull *pull) {
  if (pull->phase == TB_PULL_IDLE || pull->phase == TB_PULL_DRAGGING) { return; }
  if (pull->phase == TB_PULL_WAITING) {
    if (now(pull) >= pull->armed_until) { settle(pull); }
    else { pull->ports.schedule(pull->ports.context, pull->armed_until - now(pull)); }
    changed(pull);
    return;
  }
  const uint32_t elapsed = now(pull) - pull->started;
  pull->level = eased(pull, elapsed);
  if (elapsed >= pull->duration) { settle(pull); }
  else { pull->ports.schedule(pull->ports.context, TB_PULL_FRAME); }
  changed(pull);
}

void tb_pull_reset(TbPull *pull) {
  pull->ports.cancel(pull->ports.context);
  pull->phase = TB_PULL_IDLE;
  pull->level = 0;
  pull->armed_until = 0;
}
