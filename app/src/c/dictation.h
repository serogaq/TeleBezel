#pragma once
#include <pebble.h>

typedef enum { TB_DICTATION_OK, TB_DICTATION_CANCELLED, TB_DICTATION_NO_SPEECH, TB_DICTATION_OFFLINE, TB_DICTATION_DISABLED,
               TB_DICTATION_FAILED } TbDictationResult;

typedef void (*TbDictationDone)(void *context, TbDictationResult result, const char *text, size_t length);

typedef struct {
  void *session;
  AppTimer *timer;
  TbDictationDone done;
  void *context;
  TbDictationResult result;
  char *text;
  size_t length;
} TbDictation;

bool tb_dictation_open(TbDictation *dictation, TbDictationDone done, void *context);
bool tb_dictation_available(const TbDictation *dictation);
bool tb_dictation_start(TbDictation *dictation);
void tb_dictation_close(TbDictation *dictation);
