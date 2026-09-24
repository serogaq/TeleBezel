#include "dictation.h"
#include <stdlib.h>
#include <string.h>
#include "diag.h"

static void deliver(void *context) {
  TbDictation *dictation = context;
  dictation->timer = NULL;
  char *text = dictation->text;
  const size_t length = dictation->length;
  dictation->text = NULL;
  dictation->length = 0;
  tb_diag_event("dictation", NULL);
  if (dictation->done) { dictation->done(dictation->context, dictation->result, text ? text : "", length); }
  free(text);
}

#if defined(PBL_MICROPHONE)
static TbDictationResult result_of(DictationSessionStatus status) {
  switch (status) {
    case DictationSessionStatusSuccess: return TB_DICTATION_OK;
    case DictationSessionStatusFailureTranscriptionRejected:
    case DictationSessionStatusFailureTranscriptionRejectedWithError: return TB_DICTATION_CANCELLED;
    case DictationSessionStatusFailureNoSpeechDetected: return TB_DICTATION_NO_SPEECH;
    case DictationSessionStatusFailureConnectivityError: return TB_DICTATION_OFFLINE;
    case DictationSessionStatusFailureDisabled: return TB_DICTATION_DISABLED;
    default: return TB_DICTATION_FAILED;
  }
}

static void transcribed(DictationSession *session, DictationSessionStatus status, char *transcription, void *context) {
  (void)session;
  TbDictation *dictation = context;
  tb_diag_stack();
  free(dictation->text);
  dictation->text = NULL;
  dictation->length = 0;
  dictation->result = result_of(status);
  if (dictation->result == TB_DICTATION_OK && transcription) {
    const size_t length = strlen(transcription);
    dictation->text = malloc(length + 1);
    if (dictation->text) {
      memcpy(dictation->text, transcription, length + 1);
      dictation->length = length;
    } else {
      dictation->result = TB_DICTATION_FAILED;
    }
  }
  if (!dictation->timer) { dictation->timer = app_timer_register(0, deliver, dictation); }
}
#endif

bool tb_dictation_open(TbDictation *dictation, TbDictationDone done, void *context) {
  tb_dictation_close(dictation);
  dictation->done = done;
  dictation->context = context;
#if defined(PBL_MICROPHONE)
  DictationSession *session = dictation_session_create(0, transcribed, dictation);
  if (session) {
    dictation_session_enable_confirmation(session, true);
    dictation_session_enable_error_dialogs(session, true);
  }
  dictation->session = session;
#endif
  return dictation->session != NULL;
}

bool tb_dictation_available(const TbDictation *dictation) { return dictation->session != NULL; }

bool tb_dictation_start(TbDictation *dictation) {
#if defined(PBL_MICROPHONE)
  if (!dictation->session) { return false; }
  return dictation_session_start(dictation->session) == DictationSessionStatusSuccess;
#else
  (void)dictation;
  return false;
#endif
}

void tb_dictation_close(TbDictation *dictation) {
  if (dictation->timer) {
    app_timer_cancel(dictation->timer);
    dictation->timer = NULL;
  }
#if defined(PBL_MICROPHONE)
  if (dictation->session) { dictation_session_destroy(dictation->session); }
#endif
  dictation->session = NULL;
  free(dictation->text);
  dictation->text = NULL;
  dictation->length = 0;
}
