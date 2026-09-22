#include "telebezel/registry.hpp"
#include "telebezel/runtime/commands.hpp"
#include "telebezel/runtime/support.hpp"
#include <limits>
#include <stdexcept>
namespace telebezel::runtime {
AccountCommand AccountCommand::parse(const std::string &uuid, const nlohmann::json &input) {
  try {
    if (!input.is_object() || !valid_uuid(uuid) || input.value("uuid", "") != uuid ||
        !valid_uuid(input.value("generation", "")) || !input.contains("revision") ||
        !input.at("revision").is_number_integer() || input.at("revision").get<std::int64_t>() < 1)
      throw std::runtime_error("request.invalid");
    AccountCommand command;
    command.uuid = uuid;
    command.generation = input.at("generation").get<std::string>();
    command.revision = input.at("revision").get<std::uint64_t>();
    if (input.contains("authorization_generation")) {
      if (!input.at("authorization_generation").is_number_integer() ||
          input.at("authorization_generation").get<std::int64_t>() < 1)
        throw std::runtime_error("request.invalid");
      command.authorization_generation = input.at("authorization_generation").get<std::uint64_t>();
    }
    command.effective_config_id = input.value("effective_config_id", "");
    if (input.contains("telegram_api_id")) {
      const auto &id = input.at("telegram_api_id");
      if (!id.is_number_integer() || id.get<std::int64_t>() < 1 ||
          id.get<std::int64_t>() > std::numeric_limits<std::int32_t>::max())
        throw std::runtime_error("request.invalid");
      command.telegram_api_id = id.get<std::int32_t>();
    }
    if (input.contains("telegram_api_hash"))
      command.telegram_api_hash = input.at("telegram_api_hash").get<std::string>();
    command.lifecycle = input.value("lifecycle", "provisioning");
    const auto mode = input.value("mode", "create");
    if (mode != "create" && mode != "restore")
      throw std::runtime_error("request.invalid");
    command.mode = mode == "restore" ? ActivationMode::restore : ActivationMode::create;
    command.operation_id = nullable_string(input, "operation_id");
    command.logout_operation_id = nullable_string(input, "logout_operation_id");
    command.proxy = input.value("proxy", nlohmann::json::object());
    if (!command.proxy.is_object())
      throw std::runtime_error("request.invalid");
    command.fingerprint = command_fingerprint(input);
    return command;
  } catch (const nlohmann::json::exception &) {
    throw std::runtime_error("request.invalid");
  }
}
std::string action_name(AuthorizationAction action) {
  switch (action) {
  case AuthorizationAction::phone:
    return "submit_phone_number";
  case AuthorizationAction::code:
    return "submit_code";
  case AuthorizationAction::password:
    return "submit_password";
  case AuthorizationAction::email:
    return "submit_email_address";
  case AuthorizationAction::email_code:
    return "submit_email_code";
  case AuthorizationAction::qr:
    return "start_qr";
  case AuthorizationAction::resend:
    return "resend_code";
  }
  throw std::logic_error("unhandled authorization action");
}
AuthorizationCommand AuthorizationCommand::parse(const std::string &uuid, const nlohmann::json &input) {
  try {
    AuthorizationCommand command{AccountCommand::parse(uuid, input), AuthorizationAction::phone,
                                 input.value("authorization_version", ""), std::nullopt};
    const auto action = input.value("action", "");
    if (action == "submit_phone_number")
      command.action = AuthorizationAction::phone;
    else if (action == "submit_code")
      command.action = AuthorizationAction::code;
    else if (action == "submit_password")
      command.action = AuthorizationAction::password;
    else if (action == "submit_email_address")
      command.action = AuthorizationAction::email;
    else if (action == "submit_email_code")
      command.action = AuthorizationAction::email_code;
    else if (action == "start_qr")
      command.action = AuthorizationAction::qr;
    else if (action == "resend_code")
      command.action = AuthorizationAction::resend;
    else
      throw std::runtime_error("request.invalid");
    const bool needs_value = command.action != AuthorizationAction::qr && command.action != AuthorizationAction::resend;
    if (input.contains("value"))
      command.value = input.at("value").get<std::string>();
    if ((needs_value && (!command.value || command.value->empty())) || (!needs_value && command.value))
      throw std::runtime_error("request.invalid");
    return command;
  } catch (const nlohmann::json::exception &) {
    throw std::runtime_error("request.invalid");
  }
}
} // namespace telebezel::runtime
