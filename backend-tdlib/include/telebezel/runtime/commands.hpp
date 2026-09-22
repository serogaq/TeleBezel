#pragma once
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
namespace telebezel::runtime {
enum class ActivationMode : std::uint8_t { create, restore };
struct AccountCommand {
  std::string uuid;
  std::string generation;
  std::uint64_t revision;
  std::optional<std::uint64_t> authorization_generation;
  std::string effective_config_id;
  std::optional<std::int32_t> telegram_api_id;
  std::optional<std::string> telegram_api_hash;
  std::string lifecycle;
  ActivationMode mode;
  std::string operation_id;
  std::string logout_operation_id;
  nlohmann::json proxy;
  std::string fingerprint;
  static AccountCommand parse(const std::string &uuid, const nlohmann::json &input);
};
enum class AuthorizationAction : std::uint8_t { phone, code, password, email, email_code, qr, resend };
std::string action_name(AuthorizationAction action);
struct AuthorizationCommand {
  AccountCommand account;
  AuthorizationAction action;
  std::string version;
  std::optional<std::string> value;
  static AuthorizationCommand parse(const std::string &uuid, const nlohmann::json &input);
};
} // namespace telebezel::runtime
