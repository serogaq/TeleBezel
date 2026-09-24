#pragma once
#include "telebezel/runtime/account_state.hpp"
#include <chrono>
#include <exception>
#include <optional>
#include <stdexcept>
#include <td/telegram/td_api.h>
namespace telebezel::runtime {
using Account = AccountState;
namespace td_api = td::td_api;
inline std::chrono::milliseconds operation_budget(std::chrono::steady_clock::time_point deadline) {
  const auto remaining =
      std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
  if (remaining <= std::chrono::milliseconds(0))
    throw std::runtime_error("operation.outcome_unknown");
  return remaining;
}
void report_thread_failure(const char *thread, const char *error) noexcept;
template <class Body> void guarded_iteration(const char *thread, Body &&body) noexcept {
  try {
    body();
  } catch (const std::exception &exception) {
    report_thread_failure(thread, exception.what());
  } catch (...) {
    report_thread_failure(thread, "unknown");
  }
}
// Identifies the session a read was served from.
struct ReadFence {
  std::string generation;
  std::string epoch;
  std::int32_t client{0};
  std::uint64_t authorization_generation{0};
  std::uint64_t sequence{0};
};
std::optional<ReadFence> read_fence(const AccountState &account);
bool fence_valid(const AccountState &account, const ReadFence &fence);
bool td_error_code(const td_api::object_ptr<td_api::Object> &object, int code);
bool td_error_message(const td_api::object_ptr<td_api::Object> &object, const std::string &message);
void throw_runtime_control_error(const td_api::object_ptr<td_api::Object> &object);
std::string nullable_string(const nlohmann::json &value, const char *key);
// Fingerprints from an earlier rule carry a different prefix and are discarded
// on discovery rather than being compared against a current one.
inline constexpr const char *fingerprint_version = "2:";
std::string command_fingerprint(nlohmann::json command);
std::pair<std::string, bool> delivery_method(const td_api::AuthenticationCodeType *type);
nlohmann::json message_content(const td_api::MessageContent *content);
nlohmann::json message_projection(const td_api::message &message);
nlohmann::json sending_state_name(const td_api::MessageSendingState *state);
std::string utf8_prefix(const std::string &text, std::size_t bytes);
SendContext send_context(const td_api::chat &chat);
MemberStatus member_status(const td_api::ChatMemberStatus *status, bool channel);
std::string member_key(const std::string &kind, std::int64_t peer);
nlohmann::json can_send_projection(const Account &account, std::int64_t chat_id);
void decorate_sender(const Account &account, nlohmann::json &message);
std::string chat_type(const td_api::ChatType *type);
void apply_position(nlohmann::json &projection, const td_api::chatPosition *position);
nlohmann::json chat_projection(const td_api::chat &chat);
void decorate_chat(const Account &account, nlohmann::json &chat);
// Returns nullptr when no updateNewChat has been seen for `chat_id`.
nlohmann::json *existing_chat(Account &account, std::int64_t chat_id);
ChatOrderLog &order_log(Account &account, const std::string &list);
void record_order_change(Account &account, const nlohmann::json &before, const nlohmann::json &after);
std::string authorization_name(std::int32_t id);
std::string connection_name(std::int32_t id);
std::vector<std::string> allowed_actions(const Account &account);
nlohmann::json safe_error(const std::string &code, int status);
nlohmann::json safe_error(const std::string &code, int status, std::int64_t retry_after);
std::optional<std::int64_t> flood_wait_seconds(const std::string &message);
nlohmann::json notification_projection(const td_api::chatNotificationSettings *settings);
nlohmann::json account_json(const Account &account);
} // namespace telebezel::runtime
