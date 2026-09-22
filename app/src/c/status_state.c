#include "status_state.h"
#include "generated/protocol.h"
void tb_status_state_init(TbStatusState *state) { state->sequence = 0; state->status = TB_STATUS_CHECKING; }
uint32_t tb_status_state_begin(TbStatusState *state) {
  state->sequence++;
  if (state->sequence == 0) { state->sequence = 1; }
  state->status = TB_STATUS_CHECKING;
  return state->sequence;
}
bool tb_status_state_apply(TbStatusState *state, uint32_t sequence, int32_t result) {
  if (sequence != state->sequence) { return false; }
  switch (result) {
    case TB_RESULT_OK: state->status = TB_STATUS_CONNECTED; break;
    case TB_RESULT_CONFIG_MISSING: state->status = TB_STATUS_CONFIG_MISSING; break;
    case TB_RESULT_CONFIG_INVALID: state->status = TB_STATUS_CONFIG_INVALID; break;
    case TB_RESULT_BACKEND_UNAVAILABLE: state->status = TB_STATUS_BACKEND_UNAVAILABLE; break;
    case TB_RESULT_API_UNAUTHORIZED: state->status = TB_STATUS_API_UNAUTHORIZED; break;
    case TB_RESULT_BACKEND_NOT_READY: state->status = TB_STATUS_BACKEND_NOT_READY; break;
    default: state->status = TB_STATUS_PROTOCOL_ERROR; break;
  }
  return true;
}
void tb_status_state_set(TbStatusState *state, TbStatus status) { state->status = status; }
