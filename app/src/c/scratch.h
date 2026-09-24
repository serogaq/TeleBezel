#pragma once
#include <stdbool.h>
#include <stddef.h>

#define TB_SCRATCH_LONG 480
#define TB_SCRATCH_LINE 160

typedef enum { TB_SCRATCH_CONTENT, TB_SCRATCH_TEXT, TB_SCRATCH_SHORT } TbScratchSlot;

bool tb_scratch_init(void);
void tb_scratch_deinit(void);
char *tb_scratch(TbScratchSlot slot);
size_t tb_scratch_size(TbScratchSlot slot);
