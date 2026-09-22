#ifndef TELEBEZEL_STATUS_STATE_H
#define TELEBEZEL_STATUS_STATE_H
#include <stdbool.h>
#include <stdint.h>
typedef enum { TB_STATUS_CHECKING, TB_STATUS_CONNECTED, TB_STATUS_CONFIG_MISSING, TB_STATUS_CONFIG_INVALID, TB_STATUS_BACKEND_UNAVAILABLE, TB_STATUS_API_UNAUTHORIZED, TB_STATUS_BACKEND_NOT_READY, TB_STATUS_PROTOCOL_ERROR, TB_STATUS_PHONE_UNREACHABLE } TbStatus;
typedef struct { uint32_t sequence; TbStatus status; } TbStatusState;
void tb_status_state_init(TbStatusState *state);
uint32_t tb_status_state_begin(TbStatusState *state);
bool tb_status_state_apply(TbStatusState *state, uint32_t sequence, int32_t result);
void tb_status_state_set(TbStatusState *state, TbStatus status);
#endif
