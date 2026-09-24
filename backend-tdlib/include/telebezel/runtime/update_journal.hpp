#pragma once
#include "telebezel/runtime/account_state.hpp"
namespace telebezel::runtime {
struct AccountStore;
class UpdateJournal final {
public:
  static void append(AccountState &account, const std::string &type, std::int64_t chat_id, std::int64_t message_id = 0,
                     const std::string &field = {});
  static void append(AccountStore &store, AccountState &account, const std::string &type, std::int64_t chat_id,
                     std::int64_t message_id = 0, const std::string &field = {});
  static void append_event(AccountStore &store, AccountState &account, nlohmann::json event);
};
} // namespace telebezel::runtime
