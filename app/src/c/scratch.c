#include "scratch.h"
#include <stdlib.h>

static char *s_block;

bool tb_scratch_init(void) {
  if (!s_block) { s_block = calloc(1, 2 * TB_SCRATCH_LONG + TB_SCRATCH_LINE); }
  return s_block != NULL;
}

void tb_scratch_deinit(void) {
  free(s_block);
  s_block = NULL;
}

char *tb_scratch(TbScratchSlot slot) {
  if (!s_block && !tb_scratch_init()) { return NULL; }
  switch (slot) {
    case TB_SCRATCH_CONTENT: return s_block;
    case TB_SCRATCH_TEXT: return s_block + TB_SCRATCH_LONG;
    default: return s_block + 2 * TB_SCRATCH_LONG;
  }
}

size_t tb_scratch_size(TbScratchSlot slot) { return slot == TB_SCRATCH_SHORT ? TB_SCRATCH_LINE : TB_SCRATCH_LONG; }
