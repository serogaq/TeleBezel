#pragma once
#include "telebezel/config.hpp"
#include "telebezel/runtime/account_state.hpp"
#include "telebezel/status.hpp"
#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <set>
#include <unordered_map>
namespace telebezel::runtime {
// Account entries have stable addresses. The receive loop and operations lock
// this store while reading/mutating account state, never while awaiting TDLib.
struct AccountStore {
  explicit AccountStore(const Config &config) : limits_(config) {}

  using MessageKey = std::pair<std::int64_t, std::int64_t>;

  void erase_message(AccountState &account, const MessageKey &key) {
    const auto found = account.messages.find(key);
    if (found == account.messages.end())
      return;
    account.message_bytes -= found->second.dump().size();
    account.messages.erase(found);
  }

  void clear_messages(AccountState &account) {
    account.messages.clear();
    account.message_bytes = 0;
  }

  void put_message(AccountState &account, const MessageKey &key, nlohmann::json projection) {
    erase_message(account, key);
    account.message_bytes += projection.dump().size();
    account.messages.emplace(key, std::move(projection));
    while (std::count_if(account.messages.begin(), account.messages.end(), [&key](const auto &entry) {
             return entry.first.first == key.first;
           }) > static_cast<std::ptrdiff_t>(limits_.cache_messages_per_chat)) {
      const auto oldest = account.messages.lower_bound({key.first, 0});
      if (oldest == account.messages.end() || oldest->first.first != key.first)
        break;
      erase_message(account, oldest->first);
    }
    while (account.messages.size() > limits_.cache_messages_per_account)
      evict_one(&account);
    std::size_t count = 0;
    std::size_t bytes = 0;
    for (const auto &[id, state] : accounts_) {
      static_cast<void>(id);
      count += state.messages.size();
      bytes += state.message_bytes;
    }
    while (count > limits_.cache_messages_per_process || bytes > limits_.cache_projection_bytes) {
      const auto before_count = count;
      const auto before_bytes = bytes;
      evict_one(nullptr);
      count = 0;
      bytes = 0;
      for (const auto &[id, state] : accounts_) {
        static_cast<void>(id);
        count += state.messages.size();
        bytes += state.message_bytes;
      }
      if (count == before_count && bytes == before_bytes)
        break;
    }
  }

private:
  void evict_one(AccountState *scope) {
    AccountState *victim_account = nullptr;
    MessageKey victim_key{};
    bool victim_active = true;
    std::int64_t victim_date = 0;
    for (auto &[uuid, account] : accounts_) {
      static_cast<void>(uuid);
      if (scope != nullptr && &account != scope)
        continue;
      for (const auto &[key, projection] : account.messages) {
        const auto active = account.interest_counts.contains(key.first) && account.interest_counts.at(key.first) > 0;
        const auto date = projection.value("date", std::int64_t{0});
        if (victim_account == nullptr || (victim_active && !active) ||
            (victim_active == active && date < victim_date)) {
          victim_account = &account;
          victim_key = key;
          victim_active = active;
          victim_date = date;
        }
      }
    }
    if (victim_account != nullptr)
      erase_message(*victim_account, victim_key);
  }

public:
  mutable std::mutex mutex_;
  std::condition_variable condition_;
  StatusSnapshot status_;
  std::map<std::string, AccountState> accounts_;
  std::unordered_map<std::int32_t, std::string> client_accounts_;
  std::set<std::string> orphan_accounts_;
  std::map<std::string, std::string> manifest_errors_;
  const Config &limits_;
};
} // namespace telebezel::runtime
