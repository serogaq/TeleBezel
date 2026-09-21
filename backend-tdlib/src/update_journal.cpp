#include "telebezel/runtime/update_journal.hpp"
#include "telebezel/runtime/support.hpp"
namespace telebezel::runtime {
void UpdateJournal::append(Account &account, const std::string &type, std::int64_t chat_id, std::int64_t message_id) {
  nlohmann::json event{{"sequence", ++account.event_sequence}, {"type", type}, {"chat_id", std::to_string(chat_id)}};
  if (message_id != 0)
    event["message_id"] = std::to_string(message_id);
  account.events.push_back(std::move(event));
  while (account.events.size() > 5000)
    account.events.pop_front();
}

} // namespace telebezel::runtime
