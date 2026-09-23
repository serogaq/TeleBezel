#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  void (*changed)(void *context);
  bool (*schedule)(void *context, uint32_t milliseconds);
  void (*cancel)(void *context);
  uint32_t (*now)(void *context);
  void *context;
} TbViewPorts;
