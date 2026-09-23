#include "errors.h"
#include "generated/protocol.h"

int32_t tb_error_from_outcome(TbRequestOutcome outcome) {
  switch (outcome) {
    case TB_OUTCOME_UNREACHABLE: return TB_ERROR_PHONE_UNREACHABLE;
    case TB_OUTCOME_PROTOCOL: return TB_RESULT_PROTOCOL_ERROR;
    case TB_OUTCOME_CANCELLED: return TB_ERROR_NONE;
    default: return TB_ERROR_NO_RESPONSE;
  }
}

int32_t tb_error_submit_failed(const TbRequestLayer *layer) { return tb_error_from_outcome(tb_requests_immediate_outcome(layer)); }

int32_t tb_error_from_response(const TbResponse *response) {
  switch (response->outcome) {
    case TB_OUTCOME_UNREACHABLE: return TB_ERROR_PHONE_UNREACHABLE;
    case TB_OUTCOME_TIMEOUT: return TB_ERROR_NO_RESPONSE;
    case TB_OUTCOME_PROTOCOL: return TB_RESULT_PROTOCOL_ERROR;
    case TB_OUTCOME_CANCELLED: return TB_ERROR_NONE;
    default: return response->result;
  }
}

bool tb_error_is_connection_level(int32_t error) {
  return error == TB_RESULT_CONFIG_MISSING || error == TB_RESULT_CONFIG_INVALID || error == TB_RESULT_API_UNAUTHORIZED ||
         error == TB_RESULT_WRONG_TOKEN_TYPE;
}

bool tb_error_is_account_level(int32_t error) {
  return error == TB_RESULT_ACCOUNT_NEEDS_LOGIN || error == TB_RESULT_ACCOUNT_GONE;
}
