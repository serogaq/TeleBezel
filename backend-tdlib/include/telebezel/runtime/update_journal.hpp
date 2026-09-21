#pragma once
#include "telebezel/runtime/account_state.hpp"
namespace telebezel::runtime {
class UpdateJournal final {
public:
  static void append(AccountState &account, const std::string &type, std::int64_t chat_id, std::int64_t message_id = 0);
};
} // namespace telebezel::runtime
