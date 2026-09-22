#include "telebezel/crypto.hpp"
#include "telebezel/runtime/components.hpp"
#include "telebezel/runtime/support.hpp"
#include "telebezel/runtime/update_journal.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <td/telegram/td_api.h>
namespace telebezel::runtime {
nlohmann::json AuthorizationService::authorization_action(const std::string &uuid,
                                                          const AuthorizationCommand &command) {
  std::int32_t client = 0;
  std::string action;
  std::string state;
  {
    std::lock_guard lock(context_.mutex_);
    auto &account = lifecycle_.validated_account(uuid, command.account);
    action = action_name(command.action);
    const auto actions = allowed_actions(account);
    if (!account.reconciled || command.version != account.authorization_version ||
        std::find(actions.begin(), actions.end(), action) == actions.end()) {
      return safe_error("authorization.invalid_state", 409);
    }
    if (account.busy)
      return safe_error("operation.conflict", 409);
    account.busy = true;
    account.authorization_generation =
        command.account.authorization_generation.value_or(account.authorization_generation);
    client = account.client_id;
    state = account.authorization_state;
  }
  td_api::object_ptr<td_api::Function> function;
  const std::string value = command.value.value_or("");
  if (action == "submit_phone_number") {
    function = td_api::make_object<td_api::setAuthenticationPhoneNumber>(
        value, td_api::make_object<td_api::phoneNumberAuthenticationSettings>());
  } else if (action == "submit_code")
    function = td_api::make_object<td_api::checkAuthenticationCode>(value);
  else if (action == "submit_password")
    function = td_api::make_object<td_api::checkAuthenticationPassword>(value);
  else if (action == "submit_email_address")
    function = td_api::make_object<td_api::setAuthenticationEmailAddress>(value);
  else if (action == "submit_email_code") {
    function = td_api::make_object<td_api::checkAuthenticationEmailCode>(
        td_api::make_object<td_api::emailAddressAuthenticationCode>(value));
  } else if (action == "start_qr") {
    function = td_api::make_object<td_api::requestQrCodeAuthentication>(std::vector<std::int64_t>{});
  } else if (state == "awaiting_email_code") {
    function = td_api::make_object<td_api::resendLoginEmailAddressCode>();
  } else {
    function = td_api::make_object<td_api::resendAuthenticationCode>(
        td_api::make_object<td_api::resendCodeReasonUserRequest>());
  }
  td_api::object_ptr<td_api::Object> response;
  try {
    response = broker_.request(client, std::move(function), std::chrono::seconds(8), true);
  } catch (...) {
    std::lock_guard lock(context_.mutex_);
    if (const auto found = context_.accounts_.find(uuid); found != context_.accounts_.end())
      found->second.busy = false;
    throw;
  }
  std::lock_guard lock(context_.mutex_);
  const auto found = context_.accounts_.find(uuid);
  if (found == context_.accounts_.end())
    return safe_error("account.not_found", 404);
  auto &account = found->second;
  account.busy = false;
  if (!response || response->get_id() == td_api::error::ID) {
    const int code =
        response && response->get_id() == td_api::error::ID ? static_cast<td_api::error &>(*response).code_ : 500;
    if (code == 504)
      return safe_error("operation.outcome_unknown", 504);
    const std::string message = response && response->get_id() == td_api::error::ID
                                    ? static_cast<td_api::error &>(*response).message_
                                    : std::string{};
    if (code == 429 || message.find("FLOOD_WAIT") != std::string::npos) {
      const auto seconds = flood_wait_seconds(message).value_or(60);
      const auto available = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
      account.resend_available_at = std::max(account.resend_available_at, available);
      return safe_error("authorization.flood_wait", 429, seconds);
    }
    const std::string error = message.find("CODE_EXPIRED") != std::string::npos   ? "authorization.code_expired"
                              : message.find("CODE_INVALID") != std::string::npos ? "authorization.invalid_code"
                              : message.find("PASSWORD_HASH_INVALID") != std::string::npos
                                  ? "authorization.invalid_password"
                              : action == "submit_code" || action == "submit_email_code" ? "authorization.invalid_code"
                              : action == "submit_password" ? "authorization.invalid_password"
                                                            : "telegram.operation_failed";
    return safe_error(error, 422);
  }
  return account_json(account);
}

} // namespace telebezel::runtime
