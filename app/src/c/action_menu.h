#pragma once
#include <pebble.h>

#define TB_ACTIONS_MAX 4

typedef void (*TbActionChosen)(void *context, uint8_t action);

void tb_actions_open(const char *const *labels, const uint8_t *actions, uint8_t count, TbActionChosen chosen, void *context);
