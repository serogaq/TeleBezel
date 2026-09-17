#include <assert.h>
#include <stdint.h>
#include "status_state.h"
#include "generated/protocol.h"
int main(void) {
  TbStatusState state;
  tb_status_state_init(&state);
  uint32_t first = tb_status_state_begin(&state);
  uint32_t second = tb_status_state_begin(&state);
  assert(!tb_status_state_apply(&state, first, TB_RESULT_OK));
  assert(tb_status_state_apply(&state, second, TB_RESULT_API_UNAUTHORIZED));
  assert(state.status == TB_STATUS_API_UNAUTHORIZED);
  state.sequence = UINT32_MAX;
  assert(tb_status_state_begin(&state) == 1);
  assert(tb_status_state_apply(&state, 1, 999));
  assert(state.status == TB_STATUS_PROTOCOL_ERROR);
  return 0;
}
