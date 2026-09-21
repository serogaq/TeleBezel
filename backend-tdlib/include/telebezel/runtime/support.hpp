#pragma once
#include "telebezel/runtime/account_state.hpp"
#include <td/telegram/td_api.h>
namespace telebezel::runtime {
using Account = AccountState;
namespace td_api = td::td_api;
bool td_error_code(const td_api::object_ptr<td_api::Object> &object, int code);
bool td_error_message(const td_api::object_ptr<td_api::Object> &object, const std::string &message);
void throw_runtime_control_error(const td_api::object_ptr<td_api::Object> &object);
std::string nullable_string(const nlohmann::json &value, const char *key);
std::string command_fingerprint(nlohmann::json command);
std::pair<std::string, bool> delivery_method(const td_api::AuthenticationCodeType *type);
nlohmann::json message_content(const td_api::MessageContent *content);
nlohmann::json message_projection(const td_api::message &message);
std::string chat_type(const td_api::ChatType *type);
void apply_position(nlohmann::json &projection, const td_api::chatPosition *position);
nlohmann::json chat_projection(const td_api::chat &chat);
std::string authorization_name(std::int32_t id);
std::string connection_name(std::int32_t id);
std::vector<std::string> allowed_actions(const Account &account);
nlohmann::json safe_error(const std::string &code, int status);
nlohmann::json account_json(const Account &account);
} // namespace telebezel::runtime
