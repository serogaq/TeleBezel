#include "telebezel/runtime/update_journal.hpp"
#include "telebezel/runtime/account_store.hpp"
#include "telebezel/runtime/support.hpp"
namespace telebezel::runtime {
void UpdateJournal::append(Account &account, const std::string &type, std::int64_t chat_id, std::int64_t message_id,
                           const std::string &field) {
  nlohmann::json event{{"sequence", ++account.event_sequence}, {"type", type}, {"chat_id", std::to_string(chat_id)}};
  if (message_id != 0)
    event["message_id"] = std::to_string(message_id);
  if (!field.empty())
    event["field"] = field;
  account.events.push_back(std::move(event));
  while (account.events.size() > 5000)
    account.events.pop_front();
}

void UpdateJournal::append(AccountStore &store, Account &account, const std::string &type, std::int64_t chat_id,
                           std::int64_t message_id, const std::string &field) {
  append(account, type, chat_id, message_id, field);
  store.condition_.notify_all();
}

} // namespace telebezel::runtime
