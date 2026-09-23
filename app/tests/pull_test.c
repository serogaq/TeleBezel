#include <assert.h>
#include <string.h>
#include "pull.h"

typedef struct {
  uint32_t now;
  bool timer;
  uint32_t delay;
  uint32_t due;
  int triggers;
  int changes;
} Clock;

static void changed(void *context) { ((Clock *)context)->changes++; }
static void trigger(void *context) { ((Clock *)context)->triggers++; }
static bool schedule(void *context, uint32_t delay) {
  Clock *clock = context;
  clock->timer = true;
  clock->delay = delay;
  clock->due = clock->now + delay;
  return true;
}
static void cancel(void *context) { ((Clock *)context)->timer = false; }
static uint32_t now(void *context) { return ((Clock *)context)->now; }

static void run(TbPull *pull, Clock *clock, uint32_t until) {
  while (clock->timer && clock->due <= until) {
    clock->now = clock->due;
    clock->timer = false;
    tb_pull_tick(pull);
  }
  clock->now = until;
}

int main(void) {
  Clock clock = {.now = 1000};
  TbPull pull;
  tb_pull_init(&pull, (TbPullPorts){changed, trigger, schedule, cancel, now, &clock});

  tb_pull_press(&pull);
  assert(pull.phase == TB_PULL_RISING_HALF && clock.timer);
  run(&pull, &clock, 1080);
  assert(pull.level > 0 && pull.level < TB_PULL_HALF);
  run(&pull, &clock, 1200);
  assert(pull.level == TB_PULL_HALF && pull.phase == TB_PULL_WAITING);
  run(&pull, &clock, 1449);
  assert(pull.level == TB_PULL_HALF);
  run(&pull, &clock, 1600);
  assert(pull.phase == TB_PULL_FALLING && pull.level < TB_PULL_HALF);
  run(&pull, &clock, 2200);
  assert(pull.phase == TB_PULL_IDLE && pull.level == 0 && clock.triggers == 0);

  tb_pull_press(&pull);
  run(&pull, &clock, clock.now + 100);
  tb_pull_press(&pull);
  assert(pull.phase == TB_PULL_RISING_FULL);
  const uint32_t pressed = clock.now;
  run(&pull, &clock, pressed + 60);
  assert(pull.level > TB_PULL_HALF / 2 && pull.level < TB_PULL_FULL && clock.triggers == 0);
  run(&pull, &clock, pressed + 200);
  assert(clock.triggers == 1 && pull.level == TB_PULL_FULL);
  tb_pull_press(&pull);
  assert(pull.phase == TB_PULL_HOLDING);
  run(&pull, &clock, pressed + 1000);
  assert(pull.phase == TB_PULL_IDLE && pull.level == 0 && clock.triggers == 1);

  tb_pull_press(&pull);
  run(&pull, &clock, clock.now + TB_PULL_WINDOW + 10);
  tb_pull_press(&pull);
  assert(pull.phase == TB_PULL_RISING_HALF);
  run(&pull, &clock, clock.now + 1000);
  assert(clock.triggers == 1 && pull.level == 0);

  tb_pull_drag(&pull, 400);
  assert(pull.phase == TB_PULL_DRAGGING && pull.level == 400 && !clock.timer);
  tb_pull_drag(&pull, 1500);
  assert(pull.level == TB_PULL_FULL);
  tb_pull_drag(&pull, 700);
  tb_pull_release(&pull);
  assert(pull.phase == TB_PULL_FALLING && clock.triggers == 1);
  run(&pull, &clock, clock.now + 500);
  assert(pull.phase == TB_PULL_IDLE && pull.level == 0);

  tb_pull_drag(&pull, TB_PULL_FULL);
  tb_pull_release(&pull);
  assert(clock.triggers == 2 && pull.phase == TB_PULL_HOLDING);
  run(&pull, &clock, clock.now + 1000);
  assert(pull.level == 0);

  tb_pull_press(&pull);
  tb_pull_reset(&pull);
  assert(pull.phase == TB_PULL_IDLE && pull.level == 0 && !clock.timer);
  return 0;
}
