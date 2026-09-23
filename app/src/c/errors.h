#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "request_layer.h"

#define TB_ERROR_NONE 0
#define TB_ERROR_PHONE_UNREACHABLE 100
#define TB_ERROR_NO_RESPONSE 101
#define TB_ERROR_OUT_OF_MEMORY 102

int32_t tb_error_from_response(const TbResponse *response);
int32_t tb_error_from_outcome(TbRequestOutcome outcome);
int32_t tb_error_submit_failed(const TbRequestLayer *layer);
bool tb_error_is_account_level(int32_t error);
bool tb_error_is_connection_level(int32_t error);
